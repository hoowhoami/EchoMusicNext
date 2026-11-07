#include "EchoUiModelAdapter.h"
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
    wire("model", "nativeModelChanged()");
    wire("delegate", "nativeDelegateChanged()");
    wire("rowHeightProvider", "nativeRowHeightChanged()");
    wire("columnWidthProvider", "nativeColumnWidthChanged()");
    wire("contentHeight", "nativeHeightChanged()");
    wire("contentWidth", "nativeWidthChanged()");
    wire("flickableDirection", "nativeDirectionChanged()");
    connect(&m_projection, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        if (!m_view)
            return;
        const auto index = m_view->property("currentIndex").toInt();
        if (index < m_projection.nativeRows())
            m_savedIndex = index;
        m_savedPosition = {m_view->property("contentX").toDouble(),
                           m_view->property("contentY").toDouble()};
    });
    connect(&m_projection, &QAbstractItemModel::modelReset, this, [this] {
        if (!m_view || m_writing || m_restored)
            return;
        if (m_view->metaObject()->indexOfProperty("currentIndex") >= 0)
            write("currentIndex", qMin(m_savedIndex, m_projection.nativeRows() - 1));
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
    m_modelWasObject = object;
    m_originalModel = value;
    m_nativeModel = sourceObject;
    if (!qobject_cast<QAbstractItemModel *>(m_nativeModel.data()))
        m_nativeModel = m_valuesModel.assign(value) ? &m_valuesModel : nullptr;
    m_projection.setSourceModel(qobject_cast<QAbstractItemModel *>(m_nativeModel.data()));
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
    const auto offset = index - (column ? nativeColumnCount() : nativeRowCount());
    const auto &sizes = column ? m_extraWidths : m_extraHeights;
    return offset >= 0 && offset < sizes.size() ? sizes[offset] : -1;
}
void EchoUiModelAdapter::applyMetrics()
{
    if (m_capturing || !m_view || m_restored || m_records.isEmpty())
        return;
    if (!m_metricWrapper.isCallable())
        m_metricWrapper = m_engine.evaluate(
            "(function(original,adapter,column) { return function(index) { const count = column ? "
            "adapter.nativeColumnCount : adapter.nativeRowCount; return index < count ? (typeof "
            "original === 'function' ? original.call(this,index) : -1) : "
            "adapter.extensionSize(index,column); }; })");
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
    const auto total = [](const auto &values) {
        return std::accumulate(values.begin(), values.end(), qreal(0));
    };
    if (m_heightRelay)
        write("contentHeight",
              m_baseHeight + total(m_extraHeights) +
                  m_extraHeights.size() * m_view->property("rowSpacing").toDouble());
    if (m_widthRelay)
        write("contentWidth",
              m_baseWidth + total(m_extraWidths) +
                  m_extraWidths.size() * m_view->property("columnSpacing").toDouble());
    if (m_directionRelay)
        write("flickableDirection",
              m_extraWidths.isEmpty() ? m_direction : int(QQuickFlickable::AutoFlickDirection));
}
bool EchoUiModelAdapter::put(const Record &record, QString &error)
{
    const bool menu = kind() == "menu";
    const bool semantic = kind() == "tabs" || kind() == "settings";
    if (!m_view ||
        (semantic ? record.kind != kind()
         : menu   ? record.kind != "menu"
                  : (record.kind != "rows" && record.kind != "columns")) ||
        (record.kind == "columns" && !m_view->inherits("QQuickTableView")) || kind().isEmpty()) {
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
        if (current.token != record.token)
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
    if (!m_records.remove(token) && !m_records.isEmpty())
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
        const auto index = m_view->property("currentIndex").toInt();
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
        for (auto *relay :
             {m_modelRelay.get(), m_delegateRelay.get(), m_rowRelay.get(), m_columnRelay.get(),
              m_heightRelay.get(), m_widthRelay.get(), m_directionRelay.get()})
            if (relay)
                relay->restore();
        if (reuse.isValid()) {
            auto binding = QQmlAnyBinding::ofProperty(reuse);
            if (binding)
                binding.refresh();
            else
                write("reuseItems", originalReuse);
        }
        m_capturing = false;
        if (m_view->metaObject()->indexOfProperty("currentIndex") >= 0)
            write("currentIndex", qMin(index < m_projection.nativeRows() ? index : m_savedIndex,
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
    auto *chooser = new ModelChooser(this);
    chooser->native = m_nativeDelegate;
    for (const auto &record : records) {
        if (!record.recipe || !record.recipe->active())
            continue;
        QQmlComponent provider(&m_engine);
        const bool table = m_view->inherits("QQuickTableView");
        const QByteArray coordinates =
            table ? "required property int row; required property int column; viewRow: row; "
                    "viewColumn: column"
                  : "required property int index; viewRow: index; viewColumn: 0";
        provider.setData("import QtQuick\nimport EchoMusic.Controls\nQtObject { id: provider; "
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
            chooser->deleteLater();
            emit changed();
            return;
        }
        object->setParent(chooser);
        auto *component = object->property("cell").value<QQmlComponent *>();
        if (!component) {
            m_error = "Unable to prepare model delegate factory";
            chooser->deleteLater();
            emit changed();
            return;
        }
        for (const auto &entry : record.entries) {
            chooser->components[entry.componentKey] = component;
            if (record.kind == "rows")
                rows.append(entry);
            else
                columns.append(entry);
        }
    }
    const auto index = m_view->property("currentIndex").toInt();
    const QPointF position(m_view->property("contentX").toDouble(),
                           m_view->property("contentY").toDouble());
    const auto previous = m_delegate;
    m_delegate = chooser;
    write("delegate", QVariant::fromValue(static_cast<QQmlComponent *>(chooser)));
    m_extraHeights.clear();
    m_extraWidths.clear();
    for (const auto &row : rows)
        m_extraHeights.append(row.values.value("height", 32).toDouble());
    for (const auto &column : columns)
        m_extraWidths.append(column.values.value("width", 160).toDouble());
    m_restored = false;
    applyMetrics();
    m_projection.setEntries(rows, columns);
    write("model", QVariant::fromValue(static_cast<QAbstractItemModel *>(&m_projection)));
    if (m_view->metaObject()->indexOfProperty("currentIndex") >= 0)
        write("currentIndex", qMin(index, m_projection.nativeRows() - 1));
    write("contentX", position.x());
    write("contentY", position.y());
    if (previous)
        previous->deleteLater();
    m_restored = false;
    m_error.clear();
    emit changed();
}
bool EchoUiModelAdapter::invokeEntry(const QString &token, const QString &key, bool checked)
{
    const auto record = m_records.value(token);
    if (!record.recipe || !record.recipe->active())
        return false;
    for (const auto &entry : record.entries)
        if (entry.key == key && entry.enabled && !entry.values.value("separator").toBool())
            return record.recipe->activate(entry.values, entry.values.value("checkable").toBool()
                                                             ? QVariantMap{{"checked", checked}}
                                                             : QVariantMap());
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
                descriptor["key"] = entry.key;
                entries.append(descriptor);
            }
    auto object = m_engine.newQObject(m_menuBridge);
    auto result =
        object.property("apply").callWithInstance(object, {m_engine.toScriptValue(entries)});
    m_error = result.isError() ? result.toString() : QString();
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
    const bool recover =
        oldFocus && m_view && m_view->isAncestorOf(oldFocus) &&
        oldFocus->property("index").toInt() >= m_view->property("labels").toList().size();
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
            for (const auto &entry : record.entries)
                entries.append(QVariantMap{{"token", record.token},
                                           {"key", entry.key},
                                           {"identity", record.token + ':' + entry.key},
                                           {"label", entry.values.value("label")},
                                           {"glyph", entry.values.value("glyph", "")},
                                           {"enabled", entry.enabled}});
    const bool modified = entries != m_semanticEntries;
    m_semanticEntries = entries;
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
    if (modified)
        emit changed();
}
} // namespace EchoMusic
