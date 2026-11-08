#include "EchoUiColumnGroup.h"
#include "EchoUiModelOrder.h"
#include <QQmlProperty>
#include <QMetaProperty>
#include <QTimer>

namespace EchoMusic
{
EchoUiColumnGroup::EchoUiColumnGroup(QObject *owner)
    : QObject(owner), m_layout(qobject_cast<QQuickItem *>(owner))
{
    connect(this, &EchoUiColumnGroup::changed, this, &EchoUiColumnGroup::schedule);
}
EchoUiColumnGroup::~EchoUiColumnGroup()
{
    restore();
}
void EchoUiColumnGroup::setController(QObject *value)
{
    if (m_controller == value)
        return;
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    m_controller = value;
    if (value) {
        const auto index = value->metaObject()->indexOfProperty("uiColumnAdapter");
        if (index >= 0 && value->metaObject()->property(index).hasNotifySignal())
            m_connections << connect(value, value->metaObject()->property(index).notifySignal(),
                                     this,
                                     metaObject()->method(metaObject()->indexOfSlot("schedule()")));
        m_connections << connect(value, &QObject::destroyed, this, &EchoUiColumnGroup::schedule);
    }
    emit changed();
}
void EchoUiColumnGroup::setRow(const QVariantMap &row)
{
    if (m_row != row) {
        m_row = row;
        emit changed();
    }
}
void EchoUiColumnGroup::setRowIndex(int index)
{
    if (m_rowIndex != index) {
        m_rowIndex = index;
        emit changed();
    }
}
void EchoUiColumnGroup::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        emit changed();
    }
}
void EchoUiColumnGroup::schedule()
{
    // Ordinary, unextended lists do not enqueue work on each row data change.
    if (!m_adapter &&
        (!m_controller || !m_controller->property("uiColumnAdapter").value<QObject *>()))
        return;
    if (std::exchange(m_pending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        refresh();
    });
}
void EchoUiColumnGroup::restore()
{
    for (auto cell : std::exchange(m_cells, {}))
        if (cell) {
            cell->setVisible(false);
            cell->setParentItem(nullptr);
            cell->deleteLater();
        }
    for (auto *state : std::exchange(m_hidden, {})) {
        state->relay->restore();
        delete state;
    }
    for (int i = m_native.size() - 2; i >= 0; --i)
        if (m_layout && m_native[i] && m_native[i + 1] && m_native[i]->parentItem() == m_layout &&
            m_native[i + 1]->parentItem() == m_layout)
            m_native[i]->stackBefore(m_native[i + 1]);
}
void EchoUiColumnGroup::refresh()
{
    if (!m_layout)
        return;
    auto *adapter = m_controller ? qobject_cast<EchoUiModelAdapter *>(
                                       m_controller->property("uiColumnAdapter").value<QObject *>())
                                 : nullptr;
    if (m_adapter != adapter) {
        restore();
        disconnect(m_adapterConnection);
        m_adapter = adapter;
        if (adapter)
            m_adapterConnection =
                connect(adapter, &EchoUiModelAdapter::changed, this, &EchoUiColumnGroup::schedule);
    }
    if (!adapter)
        return;
    if (m_native.isEmpty()) {
        const auto children = m_layout->childItems();
        for (auto *child : children)
            if (!child->property("_echoUiInternal").toBool())
                m_native.append(child);
        if (m_native.size() != m_keys.size())
            return;
    }
    const auto entries = adapter->columnEntries();
    QList<QVariantMap> values;
    for (const auto &entry : entries)
        values.append(entry.toMap());
    EchoUiModelOrder order;
    QString error;
    if (!order.build(
            m_native.size(), values, [this](const QString &key) { return m_keys.indexOf(key); },
            error))
        return;
    QSet<QString> keep;
    QList<QQuickItem *> shown;
    for (int i = 0; i < order.count(); ++i) {
        const int native = order.nativeAt(i), extra = order.extraAt(i);
        if (native >= 0 && extra < 0) {
            if (m_native[native])
                shown.append(m_native[native]);
            continue;
        }
        if (extra < 0)
            continue;
        const auto entry = values[extra];
        const auto identity = entry.value("identity").toString();
        keep.insert(identity);
        auto *cell = m_cells.value(identity).data();
        if (!cell) {
            if (m_header) {
                QQmlComponent component(
                    qmlEngine(m_layout),
                    QUrl("qrc:/qt/qml/EchoMusic/Controls/EchoUiColumnHeader.qml"));
                cell = qobject_cast<QQuickItem *>(component.createWithInitialProperties(
                    {{"adapter", QVariant::fromValue<QObject *>(adapter)}, {"entry", entry}}));
            } else
                cell = new EchoUiModelCell;
            if (!cell)
                continue;
            if (!qmlContext(cell))
                QQmlEngine::setContextForObject(cell, qmlContext(m_layout));
            cell->setProperty("_echoUiInternal", true);
            cell->setParent(m_layout);
            cell->setParentItem(m_layout);
            m_cells[identity] = cell;
        }
        const auto width = entry.value("width", 160).toDouble();
        QQmlProperty(cell, "Layout.preferredWidth", qmlContext(m_layout)).write(width);
        QQmlProperty(cell, "Layout.minimumWidth", qmlContext(m_layout)).write(width);
        QQmlProperty(cell, "Layout.maximumWidth", qmlContext(m_layout)).write(width);
        QQmlProperty(cell, "Layout.fillHeight", qmlContext(m_layout)).write(true);
        if (auto *modelCell = qobject_cast<EchoUiModelCell *>(cell)) {
            modelCell->setRecipe(adapter->recipeFor(identity));
            modelCell->setEntryData(adapter->entryFor(identity));
            modelCell->setRowData(m_row);
            modelCell->setViewRow(m_rowIndex);
            modelCell->setViewColumn(i);
            modelCell->setVisible(m_active);
            if (!modelCell->property("_echoUiComplete").toBool()) {
                modelCell->setProperty("_echoUiComplete", true);
                modelCell->finishCreation();
            }
        } else
            cell->setProperty("entry", entry);
        shown.append(cell);
    }
    for (auto it = m_cells.begin(); it != m_cells.end();) {
        if (keep.contains(it.key())) {
            ++it;
            continue;
        }
        if (it.value()) {
            it.value()->setVisible(false);
            it.value()->setParentItem(nullptr);
            it.value()->deleteLater();
        }
        it = m_cells.erase(it);
    }
    for (auto item : m_native) {
        if (!item)
            continue;
        const bool hidden = !shown.contains(item);
        if (hidden && !m_hidden.contains(item)) {
            auto *state = new EchoUiColumnVisibility;
            state->setParent(this);
            state->relay =
                std::make_unique<EchoUiPropertyRelay>(item, "visible", state, "nativeVisible");
            state->relay->capture();
            item->setVisible(false);
            m_hidden[item] = state;
        } else if (!hidden && m_hidden.contains(item)) {
            auto *state = m_hidden.take(item);
            state->relay->restore();
            delete state;
        }
    }
    for (int i = shown.size() - 2; i >= 0; --i)
        shown[i]->stackBefore(shown[i + 1]);
}
} // namespace EchoMusic
