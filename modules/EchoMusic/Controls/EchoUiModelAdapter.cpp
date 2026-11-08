#include "EchoUiModelAdapter.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiValue.h"
#include <QMetaProperty>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQmlProperty>
#include <QTimer>
#include <QScopedValueRollback>
#include <QtQml/private/qqmlproperty_p.h>
#include <QtQuick/private/qquicktableview_p_p.h>
#include <QtQml/private/qqmlanybinding_p.h>
#include <numeric>
#include <cmath>
#include <QtQmlModels/private/qqmlabstractdelegatecomponent_p.h>
#include <tuple>

namespace EchoMusic
{
namespace
{
class ModelChooser final : public QQmlAbstractDelegateComponent
{
public:
    ModelChooser(QObject *parent) : QQmlAbstractDelegateComponent(parent) {}
    QString role() const override
    {
        return "echoUiComponent";
    }
    QQmlComponent *delegate(QQmlAdaptorModel *model, int row, int column) const override
    {
        const auto key = value(model, row, column, role()).toString();
        if (!key.isEmpty())
            return components.value(key);
        if (auto *choice = qobject_cast<QQmlAbstractDelegateComponent *>(native.data()))
            return choice->delegate(model, row, column);
        return native;
    }
    QHash<QString, QPointer<QQmlComponent>> components;
    QHash<QString, QPointer<QQmlComponent>> recipes;
    QPointer<QQmlComponent> native;
};
} // namespace
EchoUiModelAdapter *EchoUiModelAdapter::forView(QQuickItem *view, QQmlEngine &engine, bool create)
{
    if (!view)
        return nullptr;
    auto *adapter = qobject_cast<EchoUiModelAdapter *>(
        view->property("_echoUiModelAdapter").value<QObject *>());
    if (!adapter && create) {
        adapter = new EchoUiModelAdapter(view, engine);
        view->setProperty("_echoUiModelAdapter", QVariant::fromValue<QObject *>(adapter));
    }
    return adapter;
}
EchoUiModelAdapter::EchoUiModelAdapter(QQuickItem *view, QQmlEngine &engine)
    : QObject(view), m_view(view), m_engine(engine), m_projection(this), m_valuesModel(this)
{
    const auto wire = [this, view](const char *name, const char *slot) {
        const auto index = view->metaObject()->indexOfProperty(name);
        if (index < 0)
            return;
        const auto property = view->metaObject()->property(index);
        if (property.hasNotifySignal())
            m_connections << connect(view, property.notifySignal(), this,
                                     metaObject()->method(metaObject()->indexOfSlot(slot)));
    };
    connect(&m_projection, &EchoUiExtensionModel::projectionErrorChanged, this,
            [this](const QString &error) {
                m_error = error;
                emit projectionErrorChanged(error);
                emit changed();
            });
    wire("model", "nativeModelChanged()");
    wire("delegate", "nativeDelegateChanged()");
    wire("rowHeightProvider", "nativeRowHeightChanged()");
    wire("columnWidthProvider", "nativeColumnWidthChanged()");
    wire("contentHeight", "nativeHeightChanged()");
    wire("contentWidth", "nativeWidthChanged()");
    wire("flickableDirection", "nativeDirectionChanged()");
    wire("selectionModel", "nativeSelectionChanged()");
    wire("labels", "nativeSemanticChanged()");
    connect(&m_projection, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        if (!m_view)
            return;
        const auto index = m_view->property("currentIndex").toInt();
        const int native = m_projection.nativeSection(index, false);
        if (native >= 0) {
            m_savedIndex = native;
            if (m_projection.sourceModel())
                m_savedCurrentSource = m_projection.sourceModel()->index(native, 0);
        }
        m_savedPosition = {m_view->property("contentX").toDouble(),
                           m_view->property("contentY").toDouble()};
    });
    connect(&m_projection, &QAbstractItemModel::modelReset, this, [this] {
        if (!m_view || m_writing || m_restored)
            return;
        if (m_view->metaObject()->indexOfProperty("currentIndex") >= 0)
            write("currentIndex", m_projection.nativePosition(
                                      m_savedCurrentSource.isValid()
                                          ? m_savedCurrentSource.row()
                                          : qMin(m_savedIndex, m_projection.nativeRows() - 1),
                                      false));
        write("contentX", m_savedPosition.x());
        write("contentY", m_savedPosition.y());
    });
}
EchoUiModelAdapter::~EchoUiModelAdapter()
{
    restore();
}
QString EchoUiModelAdapter::kind() const
{
    if (!m_view)
        return {};
    if (m_view->property("_echoUiModelKind") == "tabs" ||
        m_view->property("_echoUiModelKind") == "settings")
        return m_view->property("_echoUiModelKind").toString();
    if (m_view->property("_echoUiModelKind") == "menu" &&
        m_view->property("_echoUiModelOwner").value<QObject *>())
        return "menu";
    if (m_view->property("_echoUiColumns").value<QObject *>())
        return "composite";
    return m_view->inherits("QQuickTableView")  ? "columns"
           : m_view->inherits("QQuickItemView") ? "rows"
                                                : QString();
}
void EchoUiModelAdapter::write(const char *property, const QVariant &value)
{
    if (!m_view)
        return;
    const bool previous = std::exchange(m_writing, true);
    // Keep the source binding alive. A later host model/delegate assignment
    // reaches nativeModelChanged/nativeDelegateChanged and is projected again.
    const QQmlProperty target(m_view, QString::fromLatin1(property), m_engine.rootContext());
    QQmlPropertyPrivate::write(
        target, value, QQmlPropertyData::DontRemoveBinding | QQmlPropertyData::BypassInterceptor);
    m_writing = previous;
}
void EchoUiModelAdapter::setModelInput(const QVariant &value)
{
    auto *sourceObject = value.metaType().id() == qMetaTypeId<QJSValue>()
                             ? value.value<QJSValue>().toQObject()
                             : value.value<QObject *>();
    // Projection publication is output, never fresh host model intent. Native
    // view reset notifications can arrive while a value-model source is reset.
    if (sourceObject == &m_projection)
        return;
    const bool object = value.metaType().flags().testFlag(QMetaType::PointerToQObject);
    if (value.metaType().id() != qMetaTypeId<QJSValue>() && m_originalModel == value &&
        (!object || m_nativeModel == value.value<QObject *>()))
        return;
    const bool capturing = m_capturing;
    m_capturing = true;
    disconnect(m_sourceDestroyed);
    for (const auto &connection : m_modelContextConnections)
        disconnect(connection);
    m_modelContextConnections.clear();
    m_modelWasObject = object;
    m_originalModel = value;
    m_nativeModel = sourceObject;
    if (!qobject_cast<QAbstractItemModel *>(m_nativeModel.data()))
        m_nativeModel = m_valuesModel.assign(value) ? &m_valuesModel : nullptr;
    m_projection.setSourceModel(qobject_cast<QAbstractItemModel *>(m_nativeModel.data()));
    if (auto *model = qobject_cast<QAbstractItemModel *>(m_nativeModel.data())) {
        const auto notify = [this] {
            if (m_view)
                EchoUiPartRegistry::forEngine(m_engine)->modelDataChanged(m_view);
        };
        m_modelContextConnections << connect(model, &QAbstractItemModel::dataChanged, this, notify)
                                  << connect(model, &QAbstractItemModel::modelReset, this, notify);
    }
    if (m_nativeModel && m_nativeModel != &m_valuesModel)
        m_sourceDestroyed = connect(m_nativeModel, &QObject::destroyed, this, [this] {
            m_originalModel = QVariant::fromValue<QObject *>(nullptr);
            emit inputChanged();
            if (!m_records.isEmpty())
                refresh();
        });
    emit inputChanged();
    m_capturing = capturing;
    if (!capturing && !m_records.isEmpty())
        refresh();
}
void EchoUiModelAdapter::setDelegateInput(QQmlComponent *value)
{
    if (m_nativeDelegate == value)
        return;
    m_nativeDelegate = value;
    emit inputChanged();
    if (!m_capturing && !m_records.isEmpty())
        refresh();
}
void EchoUiModelAdapter::setRowHeightInput(const QJSValue &value)
{
    m_rowHeight = value;
    m_wrappedRow = {};
    emit inputChanged();
    if (!m_capturing)
        applyMetrics();
}
void EchoUiModelAdapter::setColumnWidthInput(const QJSValue &value)
{
    m_columnWidth = value;
    m_wrappedColumn = {};
    emit inputChanged();
    if (!m_capturing)
        applyMetrics();
}
void EchoUiModelAdapter::setBaseHeight(qreal value)
{
    m_baseHeight = value;
    emit inputChanged();
    if (!m_capturing)
        applyMetrics();
}
void EchoUiModelAdapter::setBaseWidth(qreal value)
{
    m_baseWidth = value;
    emit inputChanged();
    if (!m_capturing)
        applyMetrics();
}
void EchoUiModelAdapter::setDirectionInput(int value)
{
    m_direction = value;
    emit inputChanged();
    if (!m_capturing)
        applyMetrics();
}
void EchoUiModelAdapter::captureInputs()
{
    m_capturing = true;
    const auto capture = [this](auto &relay, const char *property, const char *sink) {
        if (m_view->metaObject()->indexOfProperty(property) < 0)
            return;
        relay = std::make_unique<EchoUiPropertyRelay>(m_view, QString::fromLatin1(property), this,
                                                      QString::fromLatin1(sink));
        relay->capture();
    };
    capture(m_modelRelay, "model", "modelInput");
    capture(m_delegateRelay, "delegate", "delegateInput");
    if (auto *table = qobject_cast<QQuickTableView *>(m_view.data())) {
        if (!m_selectionBridge)
            m_selectionBridge = new EchoUiSelectionModel(m_projection, this);
        capture(m_selectionRelay, "selectionModel", "selectionInput");
        capture(m_rowRelay, "rowHeightProvider", "rowHeightInput");
        capture(m_columnRelay, "columnWidthProvider", "columnWidthInput");
        auto *data = QQuickTableViewPrivate::get(table);
        if (data->explicitContentHeight.isValid())
            capture(m_heightRelay, "contentHeight", "baseHeight");
        if (data->explicitContentWidth.isValid())
            capture(m_widthRelay, "contentWidth", "baseWidth");
        capture(m_directionRelay, "flickableDirection", "directionInput");
    }
    m_capturing = false;
}
void EchoUiModelAdapter::setSelectionInput(QObject *value)
{
    if (value == m_selectionBridge)
        return;
    for (const auto &connection : m_selectionContextConnections)
        disconnect(connection);
    m_selectionContextConnections.clear();
    m_nativeSelection = qobject_cast<QItemSelectionModel *>(value);
    if (m_nativeSelection) {
        const auto notify = [this] {
            if (m_view)
                EchoUiPartRegistry::forEngine(m_engine)->modelDataChanged(m_view);
        };
        m_selectionContextConnections
            << connect(m_nativeSelection, &QItemSelectionModel::selectionChanged, this, notify)
            << connect(m_nativeSelection, &QItemSelectionModel::currentChanged, this, notify);
    }
    if (m_selectionBridge)
        m_selectionBridge->setSourceSelection(m_nativeSelection);
    emit inputChanged();
    if (!m_capturing && !m_restored && m_view)
        write("selectionModel", QVariant::fromValue<QItemSelectionModel *>(m_selectionBridge));
}
void EchoUiModelAdapter::nativeSemanticChanged()
{
    if (kind() == "tabs" && !m_records.isEmpty())
        refreshSemantic();
}
void EchoUiModelAdapter::nativeSelectionChanged()
{
    if (m_writing || m_capturing || !m_view ||
        m_view->property("selectionModel").value<QObject *>() == m_selectionBridge)
        return;
    if (m_selectionRelay && !m_records.isEmpty()) {
        m_capturing = true;
        m_selectionRelay->capture();
        m_capturing = false;
        write("selectionModel", QVariant::fromValue<QItemSelectionModel *>(m_selectionBridge));
    } else
        setSelectionInput(m_view->property("selectionModel").value<QObject *>());
}
void EchoUiModelAdapter::nativeModelChanged()
{
    if (m_writing || m_capturing || !m_view)
        return;
    if (m_view->property("model").value<QObject *>() == &m_projection)
        return;
    if (m_modelRelay && !m_records.isEmpty()) {
        m_capturing = true;
        m_modelRelay->capture();
        m_capturing = false;
        refresh();
    } else
        setModelInput(m_view->property("model"));
}
void EchoUiModelAdapter::nativeDelegateChanged()
{
    if (m_writing || m_capturing || !m_view ||
        m_view->property("delegate").value<QQmlComponent *>() == m_delegate)
        return;
    if (m_delegateRelay && !m_records.isEmpty()) {
        m_capturing = true;
        m_delegateRelay->capture();
        m_capturing = false;
        refresh();
    } else
        setDelegateInput(m_view->property("delegate").value<QQmlComponent *>());
}
void EchoUiModelAdapter::nativeRowHeightChanged()
{
    if (!m_writing && !m_capturing && m_rowRelay) {
        m_capturing = true;
        m_rowRelay->capture();
        m_capturing = false;
        applyMetrics();
    }
}
void EchoUiModelAdapter::nativeColumnWidthChanged()
{
    if (!m_writing && !m_capturing && m_columnRelay) {
        m_capturing = true;
        m_columnRelay->capture();
        m_capturing = false;
        applyMetrics();
    }
}
void EchoUiModelAdapter::nativeHeightChanged()
{
    if (!m_writing && !m_capturing && m_heightRelay) {
        m_capturing = true;
        m_heightRelay->capture();
        m_capturing = false;
        applyMetrics();
    }
}
void EchoUiModelAdapter::nativeWidthChanged()
{
    if (!m_writing && !m_capturing && m_widthRelay) {
        m_capturing = true;
        m_widthRelay->capture();
        m_capturing = false;
        applyMetrics();
    }
}
void EchoUiModelAdapter::nativeDirectionChanged()
{
    if (!m_writing && !m_capturing && m_directionRelay) {
        m_capturing = true;
        m_directionRelay->capture();
        m_capturing = false;
        applyMetrics();
    }
}
qreal EchoUiModelAdapter::extensionSize(int index, bool column) const
{
    const auto offset = m_projection.extraSection(index, column);
    const auto &sizes = column ? m_extraWidths : m_extraHeights;
    return offset >= 0 && offset < sizes.size() ? sizes[offset] : -1;
}
void EchoUiModelAdapter::applyMetrics()
{
    if (m_capturing || !m_view || m_restored || m_records.isEmpty())
        return;
    if (!m_metricWrapper.isCallable())
        m_metricWrapper = m_engine.evaluate(
            "(function(original,adapter,column) { return function(index) { const extra = "
            "adapter.extensionSize(index,column); "
            "const native=adapter.nativeSection(index,column); return extra>=0 ? extra : native>=0 "
            "? "
            "(typeof original === 'function' ? original.call(this,native) : -1) : -1; }; })");
    if (m_rowRelay) {
        if (!m_wrappedRow.isCallable())
            m_wrappedRow = m_metricWrapper.call({m_rowHeight, m_engine.newQObject(this), false});
        write("rowHeightProvider", QVariant::fromValue(m_wrappedRow));
    }
    if (m_columnRelay) {
        if (!m_wrappedColumn.isCallable())
            m_wrappedColumn =
                m_metricWrapper.call({m_columnWidth, m_engine.newQObject(this), true});
        write("columnWidthProvider", QVariant::fromValue(m_wrappedColumn));
    }
    const auto delta = [this](bool column) {
        const auto &sizes = column ? m_extraWidths : m_extraHeights;
        const auto provider = column ? m_columnWidth : m_rowHeight;
        qreal value = 0;
        for (int i = 0; i < sizes.size(); ++i)
            if (m_projection.extraPosition(i, column) >= 0)
                value += sizes[i];
        QSet<int> removed;
        for (const auto &record : m_records)
            if ((record.kind == "columns") == column)
                for (const auto &entry : record.entries) {
                    const auto operation = entry.values.value("operation").toString();
                    const auto target = entry.values.value("target").toString();
                    if ((operation != "hide" && operation != "replace") ||
                        !target.startsWith("native:"))
                        continue;
                    const auto key = target.mid(7);
                    const int index = key.startsWith('#') ? key.mid(1).toInt()
                                                          : m_projection.lookupNative(key, column);
                    if (index < 0 || removed.contains(index))
                        continue;
                    const int at = m_projection.nativePosition(index, column);
                    if (at >= 0 && m_projection.extraSection(at, column) < 0)
                        continue;
                    removed.insert(index);
                    if (provider.isCallable()) {
                        const auto original =
                            provider.callWithInstance(m_engine.newQObject(m_view), {index});
                        if (original.isNumber() && std::isfinite(original.toNumber()) &&
                            original.toNumber() >= 0)
                            value -= original.toNumber();
                    }
                }
        const int nativeCount = column ? m_projection.nativeColumns() : m_projection.nativeRows();
        const int projectedCount = column ? m_projection.columnCount() : m_projection.rowCount();
        return value + (qMax(0, projectedCount - 1) - qMax(0, nativeCount - 1)) *
                           m_view->property(column ? "columnSpacing" : "rowSpacing").toDouble();
    };
    if (m_heightRelay)
        write("contentHeight", m_baseHeight + delta(false));
    if (m_widthRelay)
        write("contentWidth", m_baseWidth + delta(true));
    if (m_directionRelay)
        write("flickableDirection",
              m_extraWidths.isEmpty() ? m_direction : int(QQuickFlickable::AutoFlickDirection));
}
QVariantMap EchoUiModelAdapter::contextData() const
{
    auto *source = qobject_cast<QAbstractItemModel *>(m_nativeModel.data());
    if (!source && m_view)
        source = m_view->property("model").value<QAbstractItemModel *>();
    const auto rowValues = [source](int row) {
        QVariantMap result;
        qsizetype budget = 8192;
        if (!source || row < 0 || row >= source->rowCount())
            return result;
        const auto roles = source->roleNames();
        for (auto role = roles.begin(); role != roles.end(); ++role) {
            const auto value = EchoUiValue::json(source->index(row, 0).data(role.key()), budget);
            if (value.isValid())
                result[QString::fromUtf8(role.value())] = value;
        }
        return result;
    };
    QVariantList selected;
    bool truncated = false;
    auto *selection = m_nativeSelection.data();
    if (!selection && m_view)
        selection = m_view->property("selectionModel").value<QItemSelectionModel *>();
    if (selection) {
        QSet<int> seen;
        for (const auto &range : selection->selection()) {
            for (int row = range.top(); row <= range.bottom(); ++row) {
                if (seen.contains(row))
                    continue;
                if (seen.size() == 64) {
                    truncated = true;
                    break;
                }
                seen.insert(row);
                selected.append(QVariantMap{{"index", row}, {"values", rowValues(row)}});
            }
            if (truncated)
                break;
        }
    }
    int current = selection ? selection->currentIndex().row()
                  : m_view  ? m_view->property("currentIndex").toInt()
                            : -1;
    if (!m_restored && !selection)
        current = m_projection.nativeSection(current, false);
    qsizetype budget = 32768;
    return EchoUiValue::json(QVariantMap{{"currentIndex", current},
                                         {"current", rowValues(current)},
                                         {"selection", selected},
                                         {"selectionTruncated", truncated}},
                             budget)
        .toMap();
}
void EchoUiModelAdapter::setStatus(const QString &token, const QVariantMap &status)
{
    if (m_status.value(token) == status)
        return;
    m_status[token] = status;
    emit EchoUiPartRegistry::forEngine(m_engine)->modelDiagnosticsChanged();
}
QVariantMap EchoUiModelAdapter::diagnostics() const
{
    QVariantList records;
    for (const auto &record : m_records) {
        auto value = m_status.value(record.token);
        value["owner"] = record.owner;
        value["id"] = record.id;
        value["kind"] = record.kind;
        value["priority"] = record.priority;
        value["entries"] = record.entries.size();
        records.append(value);
    }
    for (auto it = m_status.begin(); it != m_status.end(); ++it)
        if (!m_records.contains(it.key()))
            records.append(it.value());
    int rows = m_projection.nativeRows(), columns = m_projection.nativeColumns();
    if (kind() == "composite") {
        if (m_restored)
            rows = m_view->property("count").toInt();
        columns = m_view->property("_echoUiColumns")
                      .value<QObject *>()
                      ->property("uiColumnKeys")
                      .toStringList()
                      .size();
    } else if (kind() == "tabs") {
        rows = m_view->property("labels").toList().size();
        columns = 1;
    } else if (kind() == "settings") {
        rows = m_view->property("_echoUiNativeModelKeys").toList().size();
        columns = 1;
    } else if (kind() == "menu") {
        columns = 1;
        if (m_menuBridge)
            rows =
                m_menuBridge->property("nativeItems").value<QJSValue>().property("length").toInt();
        else if (auto *owner = m_view->property("_echoUiModelOwner").value<QObject *>())
            rows = owner->property("count").toInt();
    }
    int projectedRows = m_projection.rowCount(), projectedColumns = m_projection.columnCount();
    if (kind() == "tabs")
        projectedRows = displayEntries().size();
    else if (kind() == "settings")
        projectedRows = projectEntries(m_view->property("_echoUiNativeModelKeys").toList())
                            .value("entries")
                            .toList()
                            .size();
    else if (kind() == "menu") {
        if (auto *owner = m_view->property("_echoUiModelOwner").value<QObject *>())
            projectedRows = owner->property("count").toInt();
    } else if (kind() == "composite") {
        if (m_restored)
            projectedRows = rows;
        QList<QVariantMap> values;
        for (const auto &value : columnEntries())
            values.append(value.toMap());
        const auto keys = m_view->property("_echoUiColumns")
                              .value<QObject *>()
                              ->property("uiColumnKeys")
                              .toStringList();
        EchoUiModelOrder order;
        QString error;
        projectedColumns =
            order.build(
                columns, values, [keys](const QString &key) { return keys.indexOf(key); }, error)
                ? order.count()
                : columns;
    }
    return {{"kind", kind()},
            {"records", records},
            {"error", m_error},
            {"nativeRows", rows},
            {"nativeColumns", columns},
            {"projectedRows", projectedRows},
            {"projectedColumns", projectedColumns},
            {"selectionBridge", bool(m_selectionBridge && m_nativeSelection)},
            {"context", contextData()},
            {"maxRecords", 128},
            {"maxEntries", 1024}};
}
QVariantMap EchoUiModelAdapter::inspect(QQuickItem *view, QQmlEngine &engine, const Record &record)
{
    auto *existing = forView(view, engine, false);
    std::unique_ptr<EchoUiModelAdapter> temporary;
    if (!existing) {
        temporary.reset(new EchoUiModelAdapter(view, engine));
        existing = temporary.get();
    }
    QString error;
    const bool supported = existing->canPut(record, error);
    auto result = existing->diagnostics();
    result["supported"] = supported;
    result["error"] = error;
    return result;
}
QVariantMap EchoUiModelAdapter::inspectRecords(QQuickItem *view, QQmlEngine &engine,
                                               const QList<Record> &records)
{
    auto *adapter = forView(view, engine, false);
    std::unique_ptr<EchoUiModelAdapter> temporary;
    if (!adapter) {
        temporary.reset(new EchoUiModelAdapter(view, engine));
        adapter = temporary.get();
    }
    const auto saved = adapter->m_records;
    for (const auto &record : records) {
        for (auto it = adapter->m_records.begin(); it != adapter->m_records.end();) {
            if (it->owner == record.owner && it->id == record.id)
                it = adapter->m_records.erase(it);
            else
                ++it;
        }
        adapter->m_records[record.token] = record;
    }
    QString error;
    bool valid = true;
    for (const auto &record : records)
        if (!adapter->canPut(record, error)) {
            valid = false;
            break;
        }
    auto result = adapter->diagnostics();
    result["supported"] = valid;
    result["error"] = error;
    adapter->m_records = saved;
    return result;
}
bool EchoUiModelAdapter::canPut(const Record &record, QString &error)
{
    const bool semantic = kind() == "menu" || kind() == "tabs" || kind() == "settings";
    if (!m_view || kind().isEmpty() || (semantic && record.kind != kind()) ||
        (!semantic && record.kind != "rows" && record.kind != "columns") ||
        (record.kind == "columns" && kind() != "composite" &&
         !m_view->inherits("QQuickTableView"))) {
        error = "This native view does not support the requested model projection";
        return false;
    }
    int count = record.entries.size();
    for (const auto &existing : m_records)
        if (existing.token != record.token &&
            !(existing.owner == record.owner && existing.id == record.id))
            count += existing.entries.size();
    if (count > 1024 || (m_records.size() >= 128 && !m_records.contains(record.token))) {
        error = "Model contribution capacity exceeded";
        return false;
    }
    if (kind() == "composite" && record.kind == "columns") {
        auto records = m_records;
        for (auto it = records.begin(); it != records.end();) {
            if (it->owner == record.owner && it->id == record.id && it.key() != record.token)
                it = records.erase(it);
            else
                ++it;
        }
        records[record.token] = record;
        auto sorted = records.values();
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            return std::tuple(a.rank, a.priority, a.owner, a.id) <
                   std::tuple(b.rank, b.priority, b.owner, b.id);
        });
        QList<QVariantMap> values;
        for (const auto &current : sorted)
            if (current.kind == "columns")
                for (const auto &entry : current.entries) {
                    auto value = entry.values;
                    value["_echoUiKey"] = entry.owner + ':' + entry.contribution + ':' + entry.key;
                    values.append(value);
                }
        EchoUiModelOrder order;
        const auto keys = m_view->property("_echoUiColumns")
                              .value<QObject *>()
                              ->property("uiColumnKeys")
                              .toStringList();
        return order.build(
            keys.size(), values, [keys](const QString &key) { return keys.indexOf(key); }, error);
    }
    if (semantic) {
        QVariantList nativeKeys;
        if (kind() == "tabs")
            nativeKeys = m_view->property("labels").toList();
        else if (kind() == "settings")
            nativeKeys = m_view->property("_echoUiNativeModelKeys").toList();
        else if (kind() == "menu") {
            auto menu =
                m_engine.newQObject(m_view->property("_echoUiModelOwner").value<QObject *>());
            if (m_menuBridge) {
                auto bridge = m_engine.newQObject(m_menuBridge);
                nativeKeys =
                    bridge.property("nativeKeys").callWithInstance(bridge).toVariant().toList();
            } else
                for (int i = 0; i < menu.property("count").toInt(); ++i) {
                    const auto item = menu.property("itemAt").callWithInstance(menu, {i});
                    auto key = item.property("objectName").toString();
                    if (key.isEmpty())
                        key = item.property("text").toString();
                    nativeKeys.append(key);
                }
        }
        auto records = m_records;
        for (auto it = records.begin(); it != records.end();) {
            if (it->owner == record.owner && it->id == record.id && it.key() != record.token)
                it = records.erase(it);
            else
                ++it;
        }
        records[record.token] = record;
        auto sorted = records.values();
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            return std::tuple(a.rank, a.priority, a.owner, a.id) <
                   std::tuple(b.rank, b.priority, b.owner, b.id);
        });
        QVariantList values;
        for (const auto &current : sorted)
            for (const auto &entry : current.entries) {
                auto value = entry.values;
                value["_echoUiKey"] = entry.owner + ':' + entry.contribution + ':' + entry.key;
                values.append(value);
            }
        const auto order = orderEntries(nativeKeys, values);
        if (!order.value("valid").toBool()) {
            error = order.value("error").toString();
            return false;
        }
    }
    if (!semantic) {
        const auto value = m_restored ? m_view->property("model") : modelInput();
        EchoUiValueModel values;
        auto *source = value.value<QAbstractItemModel *>();
        if (!source && value.metaType().id() == qMetaTypeId<QJSValue>())
            source = qobject_cast<QAbstractItemModel *>(value.value<QJSValue>().toQObject());
        if (!source && values.assign(value))
            source = &values;
        EchoUiExtensionModel projection;
        projection.setSourceModel(source);
        if (!projection.compatible() ||
            !(m_restored ? m_view->property("delegate").value<QQmlComponent *>()
                         : m_nativeDelegate.data())) {
            error =
                "Model projection requires a native item model and delegate with unreserved roles";
            return false;
        }
        auto records = m_records;
        for (auto it = records.begin(); it != records.end();) {
            if (it->owner == record.owner && it->id == record.id && it.key() != record.token)
                it = records.erase(it);
            else
                ++it;
        }
        records[record.token] = record;
        auto sorted = records.values();
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            return std::tuple(a.rank, a.priority, a.owner, a.id) <
                   std::tuple(b.rank, b.priority, b.owner, b.id);
        });
        QList<EchoUiExtensionModel::Entry> rows, columns;
        for (const auto &item : sorted) {
            if (kind() == "composite" && item.kind == "columns")
                continue;
            (item.kind == "columns" ? columns : rows).append(item.entries);
        }
        if (!projection.setEntries(rows, columns, &error))
            return false;
        if (auto *table = qobject_cast<QQuickTableView *>(m_view.data())) {
            const auto *data = QQuickTableViewPrivate::get(table);
            for (const bool column : {false, true}) {
                const bool explicitExtent =
                    m_restored ? (column ? data->explicitContentWidth.isValid()
                                         : data->explicitContentHeight.isValid())
                               : (column ? bool(m_widthRelay) : bool(m_heightRelay));
                if (!explicitExtent)
                    continue;
                const auto provider =
                    m_restored
                        ? m_view->property(column ? "columnWidthProvider" : "rowHeightProvider")
                              .value<QJSValue>()
                    : column ? m_columnWidth
                             : m_rowHeight;
                for (const auto &entry : column ? columns : rows) {
                    const auto operation = entry.values.value("operation").toString();
                    const auto target = entry.values.value("target").toString();
                    if ((operation != "hide" && operation != "replace") ||
                        !target.startsWith("native:"))
                        continue;
                    const auto key = target.mid(7);
                    const int index = key.startsWith('#') ? key.mid(1).toInt()
                                                          : projection.lookupNative(key, column);
                    const auto size =
                        provider.isCallable()
                            ? provider.callWithInstance(m_engine.newQObject(m_view), {index})
                            : QJSValue();
                    if (!size.isNumber() || !std::isfinite(size.toNumber()) ||
                        size.toNumber() < 0) {
                        error = "Native structural edits of an explicit table extent require an "
                                "original size provider";
                        return false;
                    }
                }
            }
        }
    }
    error.clear();
    return true;
}
void EchoUiModelAdapter::beginPublication(const QSet<QString> &masked)
{
    if (m_publicationDepth++ == 0)
        m_heldRecords = m_records;
    for (const auto &token : masked)
        m_records.remove(token);
}
void EchoUiModelAdapter::endPublication(bool commit)
{
    if (m_publicationDepth <= 0)
        return;
    if (!commit)
        m_records = m_heldRecords;
    if (--m_publicationDepth)
        return;
    const bool modified = m_records != m_heldRecords;
    m_heldRecords.clear();
    if (modified) {
        if (m_records.isEmpty())
            restore();
        else
            refresh();
    }
}
bool EchoUiModelAdapter::put(const Record &record, QString &error)
{
    if (!canPut(record, error))
        return false;
    const bool menu = kind() == "menu";
    const bool semantic = kind() == "tabs" || kind() == "settings" ||
                          (kind() == "composite" && record.kind == "columns");
    if (!m_view ||
        (semantic ? (kind() != "composite" && record.kind != kind())
         : menu   ? record.kind != "menu"
                  : (record.kind != "rows" && record.kind != "columns")) ||
        (record.kind == "columns" && kind() != "composite" &&
         !m_view->inherits("QQuickTableView")) ||
        kind().isEmpty()) {
        error = "This native view does not support the requested model projection";
        return false;
    }
    if (!menu && !semantic && m_restored && !m_modelRelay) {
        captureInputs();
        m_restored = false;
    }
    if (!menu && !semantic && (!m_projection.compatible() || !m_nativeDelegate || !m_nativeModel)) {
        error = "Model projection requires a native item model and delegate with unreserved roles";
        restore();
        return false;
    }
    int count = record.entries.size();
    for (const auto &current : m_records)
        if (current.token != record.token &&
            !(current.owner == record.owner && current.id == record.id))
            count += current.entries.size();
    if (count > 1024 || (m_records.size() >= 128 && !m_records.contains(record.token))) {
        error = "Model contribution capacity exceeded";
        return false;
    }
    if (m_records.contains(record.token) && m_records.value(record.token) == record) {
        error = m_error;
        return error.isEmpty();
    }
    const bool replacing = m_records.contains(record.token);
    const auto previous = m_records.value(record.token);
    m_records.insert(record.token, record);
    if (m_publicationDepth) {
        error.clear();
        return true;
    }
    refresh();
    error = m_error;
    if (!error.isEmpty()) {
        if (replacing)
            m_records[record.token] = previous;
        else
            m_records.remove(record.token);
        if (!m_records.isEmpty())
            refresh();
        else
            restore();
    }
    return error.isEmpty();
}
void EchoUiModelAdapter::remove(const QString &token)
{
    m_status.remove(token);
    if (!m_records.remove(token) && !m_records.isEmpty())
        return;
    if (m_publicationDepth)
        return;
    if (m_records.isEmpty()) {
        restore();
        if (m_view && m_view->property("_echoUiModelAdapter").value<QObject *>() == this)
            m_view->setProperty("_echoUiModelAdapter", QVariant());
        deleteLater();
    } else
        refresh();
}
void EchoUiModelAdapter::restore()
{
    if (m_view)
        if (auto *columns = m_view->property("_echoUiColumns").value<QObject *>())
            if (columns->property("uiColumnAdapter").value<QObject *>() == this)
                columns->setProperty("uiColumnAdapter", QVariant::fromValue<QObject *>(nullptr));
    clearSelection();
    if (!m_semanticEntries.isEmpty()) {
        m_semanticEntries.clear();
        emit changed();
    }
    if (m_menuBridge) {
        QMetaObject::invokeMethod(m_menuBridge, "clear");
        delete m_menuBridge;
        m_menuBridge = nullptr;
    }
    if (m_restored)
        return;
    m_restored = true;
    if (m_view) {
        const auto displayed = m_view->property("currentIndex").toInt();
        const int index = m_projection.nativeSection(displayed, false);
        const auto position = QPointF(m_view->property("contentX").toDouble(),
                                      m_view->property("contentY").toDouble());
        m_capturing = true;
        const QQmlProperty reuse(m_view, QStringLiteral("reuseItems"), m_engine.rootContext());
        const auto originalReuse = reuse.read();
        if (reuse.isValid())
            write("reuseItems", false);
        // Retire the chooser and all old delegate contexts before switching
        // adaptor backend (QAbstractItemModel back to numbers/arrays). Neither
        // the native delegate nor a plugin cell may read an incompatible model
        // during this synchronous handoff.
        write("delegate", QVariant::fromValue<QQmlComponent *>(nullptr));
        // Never let TableView retarget the source's shared selection object.
        if (m_selectionRelay)
            write("selectionModel", QVariant::fromValue<QItemSelectionModel *>(nullptr));
        for (auto *relay :
             {m_modelRelay.get(), m_delegateRelay.get(), m_rowRelay.get(), m_columnRelay.get(),
              m_heightRelay.get(), m_widthRelay.get(), m_directionRelay.get()})
            if (relay)
                relay->restore();
        if (m_selectionRelay)
            m_selectionRelay->restore();
        if (reuse.isValid()) {
            auto binding = QQmlAnyBinding::ofProperty(reuse);
            if (binding)
                binding.refresh();
            else
                write("reuseItems", originalReuse);
        }
        m_capturing = false;
        if (m_view->metaObject()->indexOfProperty("currentIndex") >= 0)
            write("currentIndex", qMin(index >= 0                       ? index
                                       : m_savedCurrentSource.isValid() ? m_savedCurrentSource.row()
                                                                        : m_savedIndex,
                                       m_projection.nativeRows() - 1));
        write("contentX", position.x());
        write("contentY", position.y());
    }
    m_capturing = true;
    m_modelRelay.reset();
    m_delegateRelay.reset();
    m_rowRelay.reset();
    m_columnRelay.reset();
    m_heightRelay.reset();
    m_widthRelay.reset();
    m_directionRelay.reset();
    m_selectionRelay.reset();
    if (m_selectionBridge)
        m_selectionBridge->setSourceSelection(nullptr);
    m_capturing = false;
    if (m_delegate)
        m_delegate->deleteLater();
    m_delegate = nullptr;
    m_projection.setEntries({}, {});
}
void EchoUiModelAdapter::refresh()
{
    if (!m_view || m_records.isEmpty())
        return;
    if (kind() == "composite") {
        refreshColumns();
        bool rows = false;
        for (const auto &record : m_records)
            rows = rows || record.kind == "rows";
        if (!rows) {
            if (!m_restored)
                restore();
            refreshColumns();
            m_error.clear();
            return;
        }
    }
    if (kind() == "tabs" || kind() == "settings") {
        refreshSemantic();
        return;
    }
    if (kind() == "menu") {
        refreshMenu();
        return;
    }
    if (m_restored && !m_modelRelay) {
        captureInputs();
        m_restored = false;
    }
    if (!m_projection.compatible() || !m_nativeDelegate || !m_nativeModel) {
        m_error = "Native source model/delegate is unavailable";
        restore();
        emit changed();
        return;
    }
    auto records = m_records.values();
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return std::tuple(a.rank, a.priority, a.owner, a.id) <
               std::tuple(b.rank, b.priority, b.owner, b.id);
    });
    QList<EchoUiExtensionModel::Entry> rows, columns;
    QSet<QString> recipeKeys, componentKeys;
    for (const auto &record : records)
        if (record.recipe && record.recipe->active() &&
            !(kind() == "composite" && record.kind == "columns")) {
            recipeKeys.insert(record.token);
            for (const auto &entry : record.entries)
                componentKeys.insert(entry.componentKey);
        }
    auto *chooser = dynamic_cast<ModelChooser *>(m_delegate.data());
    // Qt's delegate/reuse caches can retain a Component's creation context.
    // Retire the whole chooser for structural membership changes; only data
    // revisions reuse it, keeping editors live without freeing cached recipes.
    if (!chooser || chooser->native != m_nativeDelegate ||
        QSet<QString>(chooser->recipes.keyBegin(), chooser->recipes.keyEnd()) != recipeKeys ||
        QSet<QString>(chooser->components.keyBegin(), chooser->components.keyEnd()) !=
            componentKeys) {
        chooser = new ModelChooser(this);
        chooser->native = m_nativeDelegate;
    }
    QSet<QString> usedComponents;
    for (const auto &record : records) {
        if (!record.recipe || !record.recipe->active() ||
            (kind() == "composite" && record.kind == "columns"))
            continue;
        auto *component = chooser->recipes.value(record.token).data();
        if (!component) {
            QQmlComponent provider(&m_engine);
            const bool table = m_view->inherits("QQuickTableView");
            const QByteArray coordinates =
                table ? "required property int row; required property int column; viewRow: row; "
                        "viewColumn: column"
                      : "required property int index; viewRow: index; viewColumn: 0";
            provider.setData(
                "import QtQuick\nimport EchoMusic.Controls\nQtObject { id: provider; "
                "required property QtObject recipe; property Component cell: Component { "
                "EchoUiModelCell { required property var model; " +
                    coordinates + R"(
 recipe: provider.recipe
 rowData: model.echoUiRow || ({})
 entryData: model.echoUiEntry || ({})
 width: ListView.view && ListView.view.orientation === ListView.Vertical ? ListView.view.width : GridView.view ? GridView.view.cellWidth : implicitWidth
 height: ListView.view && ListView.view.orientation === ListView.Horizontal ? ListView.view.height : GridView.view ? GridView.view.cellHeight : implicitHeight
} } })",
                QUrl("qrc:/echo-ui-model-cell.qml"));
            auto *object = provider.createWithInitialProperties(
                {{"recipe", QVariant::fromValue<QObject *>(record.recipe)}});
            if (!object) {
                m_error = provider.errorString();
                if (chooser != m_delegate)
                    chooser->deleteLater();
                emit changed();
                return;
            }
            object->setParent(chooser);
            component = object->property("cell").value<QQmlComponent *>();
            if (!component) {
                m_error = "Unable to prepare model delegate factory";
                if (chooser != m_delegate)
                    chooser->deleteLater();
                emit changed();
                return;
            }
            chooser->recipes[record.token] = component;
        }
        for (const auto &entry : record.entries) {
            chooser->components[entry.componentKey] = component;
            usedComponents.insert(entry.componentKey);
            if (record.kind == "rows")
                rows.append(entry);
            else
                columns.append(entry);
        }
    }
    const int index =
        m_restored ? m_view->property("currentIndex").toInt()
                   : m_projection.nativeSection(m_view->property("currentIndex").toInt(), false);
    const QPointF position(m_view->property("contentX").toDouble(),
                           m_view->property("contentY").toDouble());
    const auto previous = m_delegate;
    m_delegate = chooser;
    if (previous != chooser)
        write("delegate", QVariant::fromValue(static_cast<QQmlComponent *>(chooser)));
    m_extraHeights.clear();
    m_extraWidths.clear();
    for (const auto &row : rows)
        m_extraHeights.append(row.values.value("height", 32).toDouble());
    for (const auto &column : columns)
        m_extraWidths.append(column.values.value("width", 160).toDouble());
    m_restored = false;
    if (!m_projection.setEntries(rows, columns, &m_error)) {
        m_delegate = previous;
        write("delegate", QVariant::fromValue(m_delegate.data()));
        chooser->deleteLater();
        emit changed();
        return;
    }
    applyMetrics();
    if (m_selectionBridge)
        write("selectionModel", QVariant::fromValue<QItemSelectionModel *>(nullptr));
    if (m_view->property("model").value<QObject *>() != &m_projection)
        write("model", QVariant::fromValue(static_cast<QAbstractItemModel *>(&m_projection)));
    if (m_selectionBridge) {
        m_selectionBridge->setSourceSelection(m_nativeSelection);
        m_selectionBridge->synchronize();
        write("selectionModel", QVariant::fromValue<QItemSelectionModel *>(m_selectionBridge));
    }
    if (m_view->metaObject()->indexOfProperty("currentIndex") >= 0)
        write("currentIndex",
              m_projection.nativePosition(qMin(index, m_projection.nativeRows() - 1), false));
    write("contentX", position.x());
    write("contentY", position.y());
    if (previous && previous != chooser)
        previous->deleteLater();
    for (auto it = chooser->components.begin(); it != chooser->components.end();) {
        if (!usedComponents.contains(it.key()))
            it = chooser->components.erase(it);
        else
            ++it;
    }
    QSet<QString> liveRecipes;
    for (const auto &record : records)
        liveRecipes.insert(record.token);
    for (auto it = chooser->recipes.begin(); it != chooser->recipes.end();) {
        if (!liveRecipes.contains(it.key())) {
            if (it.value() && it.value()->parent())
                it.value()->parent()->deleteLater();
            it = chooser->recipes.erase(it);
        } else
            ++it;
    }
    m_restored = false;
    m_error.clear();
    emit changed();
}
bool EchoUiModelAdapter::armEntry(const QString &token, const QString &key)
{
    const auto record = m_records.value(token);
    if (!record.recipe || !record.recipe->active())
        return false;
    const auto find = [&](auto &&self, const QVariantMap &entry, const QString &path) -> bool {
        if (path == key)
            return record.recipe->arm(entry);
        for (const auto &child : entry.value("children").toList()) {
            const auto value = child.toMap();
            if (self(self, value, path + '/' + value.value("key").toString()))
                return true;
        }
        return false;
    };
    for (const auto &entry : record.entries)
        if (entry.enabled && find(find, entry.values, entry.key))
            return true;
    return false;
}
bool EchoUiModelAdapter::invokeEntry(const QString &token, const QString &key, bool checked)
{
    const auto record = m_records.value(token);
    if (!record.recipe || !record.recipe->active())
        return false;
    const auto find = [&](auto &&self, const QVariantMap &entry, const QString &path) -> bool {
        if (path == key && entry.value("enabled", true).toBool() &&
            !entry.value("separator").toBool())
            return record.recipe->activate(entry, entry.value("checkable").toBool()
                                                      ? QVariantMap{{"checked", checked}}
                                                      : QVariantMap());
        for (const auto &child : entry.value("children").toList()) {
            const auto value = child.toMap();
            if (self(self, value, path + '/' + value.value("key").toString()))
                return true;
        }
        return false;
    };
    for (const auto &entry : record.entries)
        if (entry.enabled && find(find, entry.values, entry.key))
            return true;
    return false;
}
void EchoUiModelAdapter::refreshMenu()
{
    if (!m_menuBridge) {
        QQmlComponent component(&m_engine,
                                QUrl("qrc:/qt/qml/EchoMusic/Controls/EchoUiMenuProjection.qml"));
        m_menuBridge = component.createWithInitialProperties(
            {{"adapter", QVariant::fromValue<QObject *>(this)},
             {"menu", m_view->property("_echoUiModelOwner")}});
        if (!m_menuBridge) {
            m_error = component.errorString();
            return;
        }
        m_menuBridge->setParent(this);
    }
    auto records = m_records.values();
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return std::tuple(a.rank, a.priority, a.owner, a.id) <
               std::tuple(b.rank, b.priority, b.owner, b.id);
    });
    QVariantList entries;
    for (const auto &record : records)
        if (record.recipe && record.recipe->active())
            for (const auto &entry : record.entries) {
                auto descriptor = entry.values;
                descriptor["token"] = record.token;
                descriptor["owner"] = record.owner;
                descriptor["key"] = entry.key;
                entries.append(descriptor);
            }
    m_semanticEntries = entries;
    auto object = m_engine.newQObject(m_menuBridge);
    auto result =
        object.property("apply").callWithInstance(object, {m_engine.toScriptValue(entries)});
    m_error = result.isError() || result.isString() ? result.toString() : QString();
    emit changed();
}
QVariantMap EchoUiModelAdapter::projectEntries(const QVariantList &nativeKeys) const
{
    return orderEntries(nativeKeys, m_semanticEntries);
}
QVariantMap EchoUiModelAdapter::orderEntries(const QVariantList &nativeKeys,
                                             const QVariantList &input) const
{
    QList<QVariantMap> entries;
    for (const auto &value : input)
        entries.append(value.toMap());
    EchoUiModelOrder order;
    QString error;
    if (!order.build(
            nativeKeys.size(), entries,
            [nativeKeys](const QString &key) {
                int result = -1;
                for (int i = 0; i < nativeKeys.size(); ++i)
                    if (nativeKeys[i].toString() == key) {
                        if (result >= 0)
                            return -1;
                        result = i;
                    }
                return result;
            },
            error))
        return {{"valid", false}, {"error", error}, {"entries", QVariantList()}};
    QVariantList values;
    for (int i = 0; i < order.count(); ++i) {
        const int native = order.nativeAt(i), extra = order.extraAt(i);
        if (extra >= 0)
            values.append(entries[extra]);
        else if (native >= 0)
            values.append(QVariantMap{{"nativeIndex", native}, {"label", nativeKeys[native]}});
    }
    return {{"valid", true}, {"error", ""}, {"entries", values}};
}
QVariantList EchoUiModelAdapter::displayEntries() const
{
    const auto labels = m_view ? m_view->property("labels").toList() : QVariantList();
    const auto projection = projectEntries(labels);
    if (projection.value("valid").toBool())
        return projection.value("entries").toList();
    QVariantList native;
    for (int i = 0; i < labels.size(); ++i)
        native.append(QVariantMap{{"nativeIndex", i}, {"label", labels[i]}});
    return native;
}
QVariantList EchoUiModelAdapter::columnEntries() const
{
    auto records = m_records.values();
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return std::tuple(a.rank, a.priority, a.owner, a.id) <
               std::tuple(b.rank, b.priority, b.owner, b.id);
    });
    QVariantList values;
    for (const auto &record : records)
        if (record.kind == "columns" && record.recipe && record.recipe->active())
            for (const auto &entry : record.entries) {
                auto value = entry.values;
                value["token"] = record.token;
                value["identity"] = record.token + ':' + entry.key;
                value["_echoUiKey"] = entry.owner + ':' + entry.contribution + ':' + entry.key;
                values.append(value);
            }
    return values;
}
void EchoUiModelAdapter::refreshColumns()
{
    if (auto *columns = m_view->property("_echoUiColumns").value<QObject *>())
        columns->setProperty("uiColumnAdapter", QVariant::fromValue<QObject *>(
                                                    columnEntries().isEmpty() ? nullptr : this));
    emit changed();
}
EchoUiModelRecipe *EchoUiModelAdapter::recipeFor(const QString &identity) const
{
    for (const auto &record : m_records)
        if (record.recipe && record.recipe->active())
            for (const auto &entry : record.entries)
                if (record.token + ':' + entry.key == identity)
                    return record.recipe;
    return nullptr;
}
QVariantMap EchoUiModelAdapter::entryFor(const QString &identity) const
{
    for (const auto &record : m_records)
        for (const auto &entry : record.entries)
            if (record.token + ':' + entry.key == identity) {
                auto values = entry.values;
                values["_echoUiKey"] = record.owner + ':' + record.id + ':' + entry.key;
                return values;
            }
    return {};
}
void EchoUiModelAdapter::clearSelection()
{
    if (m_selectedRecipe)
        m_selectedRecipe->select({});
    m_selectedRecipe = nullptr;
    if (m_selectedKey.isEmpty())
        return;
    const QPointer<QQuickItem> oldFocus =
        m_view && m_view->window() ? m_view->window()->activeFocusItem() : nullptr;
    const bool recover = oldFocus && m_view && m_view->isAncestorOf(oldFocus);
    m_selectedKey.clear();
    m_selectedToken.clear();
    emit changed();
    if (recover)
        QTimer::singleShot(0, this, [this, oldFocus] {
            if (!m_view || !m_view->window())
                return;
            auto *now = m_view->window()->activeFocusItem();
            if (now && now != oldFocus && now != m_view->window()->contentItem() &&
                !m_view->isAncestorOf(now))
                return;
            auto *native = m_view->property("_echoUiNativeFocusItem").value<QQuickItem *>();
            if (native && native->isVisible() && native->isEnabled())
                native->forceActiveFocus(Qt::OtherFocusReason);
        });
}
bool EchoUiModelAdapter::selectEntry(int index)
{
    if (index < 0 || index >= m_semanticEntries.size())
        return false;
    const auto descriptor = m_semanticEntries[index].toMap();
    const auto record = m_records.value(descriptor.value("token").toString());
    if (!record.recipe || !record.recipe->active())
        return false;
    for (const auto &entry : record.entries)
        if (entry.key == descriptor.value("key").toString() && entry.enabled) {
            if (m_selectedRecipe && m_selectedRecipe != record.recipe)
                m_selectedRecipe->select({});
            if (!record.recipe->select(entry.values)) {
                clearSelection();
                m_error = "Tab content target must resolve uniquely to composable content in the "
                          "same window";
                emit changed();
                return false;
            }
            m_selectedRecipe = record.recipe;
            m_selectedKey = descriptor.value("identity").toString();
            m_selectedToken = record.token;
            m_error.clear();
            emit changed();
            return true;
        }
    return false;
}
void EchoUiModelAdapter::refreshSemantic()
{
    auto records = m_records.values();
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return std::tuple(a.rank, a.priority, a.owner, a.id) <
               std::tuple(b.rank, b.priority, b.owner, b.id);
    });
    QVariantList entries;
    for (const auto &record : records)
        if (record.recipe && record.recipe->active())
            for (const auto &entry : record.entries) {
                auto value = entry.values;
                value["token"] = record.token;
                value["key"] = entry.key;
                value["identity"] = record.token + ':' + entry.key;
                value["_echoUiKey"] = record.owner + ':' + record.id + ':' + entry.key;
                value["enabled"] = entry.enabled;
                entries.append(value);
            }
    const bool modified = entries != m_semanticEntries;
    m_semanticEntries = entries;
    const auto projection = projectEntries(m_view->property("labels").toList());
    if (kind() == "tabs") {
        const auto error = projection.value("error").toString();
        if (m_error != error) {
            m_error = error;
            emit projectionErrorChanged(error);
        }
    }
    if (!m_selectedKey.isEmpty()) {
        const auto selected =
            std::find_if(entries.begin(), entries.end(), [this](const auto &value) {
                return value.toMap().value("identity").toString() == m_selectedKey;
            });
        if (selected == entries.end())
            clearSelection();
        else
            selectEntry(int(selected - entries.begin()));
    }
    if (modified || kind() == "tabs")
        emit changed();
}
} // namespace EchoMusic
