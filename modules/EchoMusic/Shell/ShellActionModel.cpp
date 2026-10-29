#include "SettingsKeys.h"
#include "ShellActionModel.h"
#include <QSet>
#include <QThread>
#include <algorithm>
#include <limits>

namespace EchoMusic
{
namespace
{
QHash<int, QByteArray> actionRoles()
{
    using M = ShellActionModel;
    return {{M::KeyRole, "actionKey"},
            {M::IdRole, "actionId"},
            {M::TitleRole, "actionTitle"},
            {M::IconRole, "actionIcon"},
            {M::EnabledRole, "actionEnabled"},
            {M::BusyRole, "actionBusy"},
            {M::PreferredRole, "preferredZone"},
            {M::EffectiveRole, "effectiveZone"},
            {M::BadgeRole, "actionBadge"},
            {M::BadgeVisibleRole, "badgeVisible"},
            {M::ActiveRole, "actionActive"},
            {M::BadgeSupportedRole, "badgeSupported"},
            {M::BadgePreferredRole, "badgePreferred"},
            {M::TooltipRole, "actionTooltip"},
            {M::BadgeTitleRole, "badgeTitle"},
            {M::HoverRole, "actionHover"}};
}
} // namespace
ShellActionModel::ShellActionModel(EchoMusic::SettingsStore &preferences, Surface surface,
                                   QObject *parent)
    : QAbstractListModel(parent), m_preferences(preferences), m_surface(surface),
      m_prefix(surface == Titlebar ? EchoMusic::SettingsKeys::Shell::TitlebarPrefix
                                   : EchoMusic::SettingsKeys::Shell::PlayerbarPrefix)
{
    m_order = preferences.value(m_prefix + SettingsKeys::ShellFields::Order).toStringList();
    m_order.removeDuplicates();
    m_placements = preferences.value(m_prefix + SettingsKeys::ShellFields::Placements).toMap();
    m_badges = preferences.value(m_prefix + SettingsKeys::ShellFields::Badges).toMap();
    for (const auto zone : {Toolbar, Left, Center, Right})
        m_capacity.insert(zone, 0);
}
int ShellActionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}
QHash<int, QByteArray> ShellActionModel::roleNames() const
{
    return actionRoles();
}
QVariant ShellActionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.column() != 0 ||
        index.row() >= m_entries.size())
        return {};
    const auto &entry = m_entries[index.row()];
    const auto &a = entry.action;
    switch (role) {
    case KeyRole:
        return a.key;
    case IdRole:
        return a.id;
    case Qt::DisplayRole:
    case TitleRole:
        return a.title;
    case IconRole:
        return a.icon;
    case EnabledRole:
        return a.enabled && !entry.busy && entry.owner;
    case BusyRole:
        return entry.busy;
    case PreferredRole:
        return preferred(entry);
    // -2 denotes an invisible contribution; it must not appear in any projection.
    case EffectiveRole:
        return a.visible && entry.owner ? int(entry.effective) : -2;
    case BadgeRole:
        return entry.badge;
    case BadgeVisibleRole:
        return !entry.badge.isEmpty() && m_badges.value(a.key, a.badgeDefaultVisible).toBool();
    case ActiveRole:
        return entry.active;
    case BadgeSupportedRole:
        return a.supportsBadge || !entry.badge.isEmpty();
    case TooltipRole:
        return a.tooltip.isEmpty() ? a.title : a.tooltip;
    case BadgeTitleRole:
        return a.badgeTitle;
    case HoverRole:
        return a.hover;
    case BadgePreferredRole:
        return m_badges.value(a.key, a.badgeDefaultVisible).toBool();
    default:
        return {};
    }
}
int ShellActionModel::rowFor(const QString &key) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].action.key == key)
            return i;
    return -1;
}
bool ShellActionModel::validZone(Zone zone) const
{
    return zone == More ||
           (m_surface == Titlebar ? zone == Toolbar : zone >= Left && zone <= Right);
}
ShellActionModel::Zone ShellActionModel::preferred(const Entry &e) const
{
    const auto value = m_placements.value(e.action.key);
    bool ok = false;
    const auto zone = static_cast<Zone>(value.toInt(&ok));
    return ok && validZone(zone) ? zone : e.action.defaultZone;
}
ShellActionModel::Handle ShellActionModel::registerAction(QObject &owner, Contribution a)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (owner.thread() != thread() || a.key.isEmpty() || a.id.isEmpty() || a.title.isEmpty() ||
        a.icon.isEmpty() || !validZone(a.defaultZone) || !a.invoke)
        return {};
    const int previous = rowFor(a.key);
    if (previous >= 0 && m_entries[previous].owner != &owner)
        return {};
    const Handle handle{a.key, ++m_generation};
    if (previous >= 0) {
        disconnect(m_entries[previous].ownerConnection);
        beginRemoveRows({}, previous, previous);
        m_entries.removeAt(previous);
        endRemoveRows();
    }
    Entry entry;
    entry.action = std::move(a);
    entry.owner = &owner;
    entry.generation = handle.generation;
    const auto position = [&](const QString &key) {
        const int saved = int(m_order.indexOf(key));
        return saved < 0 ? std::numeric_limits<int>::max() : saved;
    };
    int row = 0;
    for (; row < m_entries.size(); ++row) {
        const auto &existing = m_entries[row].action;
        const int savedLeft = position(entry.action.key), savedRight = position(existing.key);
        if (savedLeft < savedRight ||
            (savedLeft == savedRight &&
             (entry.action.order < existing.order ||
              (entry.action.order == existing.order && entry.action.key < existing.key))))
            break;
    }
    beginInsertRows({}, row, row);
    m_entries.insert(row, std::move(entry));
    resolve();
    endInsertRows();
    if (rowCount() > 1)
        emit dataChanged(index(0), index(rowCount() - 1), {EffectiveRole});
    m_entries[rowFor(handle.key)].ownerConnection =
        connect(&owner, &QObject::destroyed, this, [this, handle] { unregisterAction(handle); });
    emit layoutChanged();
    return handle;
}
bool ShellActionModel::unregisterAction(const Handle &handle)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(handle.key);
    if (row < 0 || m_entries[row].generation != handle.generation)
        return false;
    disconnect(m_entries[row].ownerConnection);
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    resolve();
    endRemoveRows();
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1), {EffectiveRole});
    emit layoutChanged();
    return true;
}
bool ShellActionModel::setInteraction(const Handle &handle, const QString &tooltip,
                                      const QString &badgeTitle, bool hover)
{
    const int row = rowFor(handle.key);
    if (row < 0 || m_entries[row].generation != handle.generation)
        return false;
    auto &a = m_entries[row].action;
    if (a.tooltip == tooltip && a.badgeTitle == badgeTitle && a.hover == hover)
        return true;
    a.tooltip = tooltip;
    a.badgeTitle = badgeTitle;
    a.hover = hover;
    emit dataChanged(index(row), index(row), {TooltipRole, BadgeTitleRole, HoverRole});
    return true;
}
bool ShellActionModel::setState(const Handle &handle, bool enabled, bool visible, bool busy,
                                const QString &badge, bool active)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(handle.key);
    if (row < 0 || m_entries[row].generation != handle.generation || !m_entries[row].owner)
        return false;
    auto &e = m_entries[row];
    if (e.action.enabled == enabled && e.action.visible == visible && e.busy == busy &&
        e.badge == badge && e.active == active)
        return true;
    e.action.enabled = enabled;
    e.action.visible = visible;
    e.busy = busy;
    e.badge = badge;
    e.active = active;
    resolve(); // A hidden toolbar action releases its slot for the next one.
    emit dataChanged(index(0), index(rowCount() - 1));
    emit layoutChanged();
    return true;
}
bool ShellActionModel::activate(const QString &key)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(key);
    if (row < 0)
        return false;
    const auto &e = m_entries[row];
    if (!e.owner || !e.action.visible || !e.action.enabled || e.busy)
        return false;
    // A command may synchronously remove/re-register itself; never retain a row
    // reference across the provider call. Async providers publish busy via handle.
    const auto invoke = e.action.invoke;
    invoke();
    return true;
}
void ShellActionModel::resolve()
{
    QHash<QString, int> positions;
    for (int i = 0; i < m_order.size(); ++i)
        positions.insert(m_order[i], i);
    std::stable_sort(m_entries.begin(), m_entries.end(), [&](const auto &a, const auto &b) {
        const int left = positions.value(a.action.key, std::numeric_limits<int>::max());
        const int right = positions.value(b.action.key, std::numeric_limits<int>::max());
        if (left != right)
            return left < right;
        if (a.action.order != b.action.order)
            return a.action.order < b.action.order;
        return a.action.key < b.action.key;
    });
    auto remaining = m_capacity;
    for (auto &entry : m_entries) {
        const auto zone = preferred(entry);
        entry.effective = More;
        if (entry.action.visible && entry.owner && zone != More && remaining.value(zone) > 0) {
            entry.effective = zone;
            --remaining[zone];
        }
    }
}
void ShellActionModel::persist()
{
    m_preferences.setValue(m_prefix + SettingsKeys::ShellFields::Order, m_order);
    m_preferences.setValue(m_prefix + SettingsKeys::ShellFields::Placements, m_placements);
    m_preferences.setValue(m_prefix + SettingsKeys::ShellFields::Badges, m_badges);
}
bool ShellActionModel::setPlacement(const QString &key, Zone zone)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (rowFor(key) < 0 || !validZone(zone))
        return false;
    m_placements.insert(key, int(zone));
    resolve();
    persist();
    emit dataChanged(index(0), index(rowCount() - 1));
    emit layoutChanged();
    return true;
}
bool ShellActionModel::setBadgeVisible(const QString &key, bool visible)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto row = rowFor(key);
    if (row < 0)
        return false;
    m_badges.insert(key, visible);
    persist();
    emit dataChanged(index(row), index(row), {BadgeVisibleRole, BadgePreferredRole});
    return true;
}
bool ShellActionModel::reorder(const QStringList &keys)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QSet<QString> changed;
    for (const auto &key : keys) {
        if (changed.contains(key) || rowFor(key) < 0)
            return false;
        changed.insert(key);
    }
    auto order = m_order;
    for (const auto &e : m_entries)
        if (!order.contains(e.action.key))
            order.push_back(e.action.key);
    int cursor = 0;
    for (auto &key : order)
        if (changed.contains(key))
            key = keys[cursor++];
    if (order == m_order)
        return true;
    m_order = order;
    applyOrder();
    resolve();
    persist();
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1), {EffectiveRole});
    emit layoutChanged();
    return true;
}
void ShellActionModel::applyOrder()
{
    auto desired = m_entries;
    QHash<QString, int> positions;
    for (int i = 0; i < m_order.size(); ++i)
        positions.insert(m_order[i], i);
    std::stable_sort(desired.begin(), desired.end(), [&](const auto &a, const auto &b) {
        const auto left = positions.value(a.action.key, std::numeric_limits<int>::max());
        const auto right = positions.value(b.action.key, std::numeric_limits<int>::max());
        if (left != right)
            return left < right;
        if (a.action.order != b.action.order)
            return a.action.order < b.action.order;
        return a.action.key < b.action.key;
    });
    for (int target = 0; target < desired.size(); ++target) {
        const int source = rowFor(desired[target].action.key);
        if (source == target)
            continue;
        beginMoveRows({}, source, source, {}, target);
        m_entries.move(source, target);
        endMoveRows();
    }
}
QStringList ShellActionModel::projectedKeys(Zone zone, bool usePreferred) const
{
    QStringList keys;
    for (const auto &e : m_entries)
        if (e.owner && e.action.visible &&
            (zone == All || (usePreferred ? preferred(e) : e.effective) == zone))
            keys.push_back(e.action.key);
    return keys;
}
bool ShellActionModel::shiftAction(const QString &key, int offset, Zone projection)
{
    auto keys = projectedKeys(projection);
    const auto from = keys.indexOf(key), to = from + offset;
    if (from < 0 || to < 0 || to >= keys.size() || offset == 0)
        return false;
    keys.move(from, to);
    return reorder(keys);
}
bool ShellActionModel::moveBefore(const QString &key, const QString &beforeKey, Zone projection)
{
    auto keys = projectedKeys(projection);
    if (!keys.contains(key) || key == beforeKey ||
        (!beforeKey.isEmpty() && !keys.contains(beforeKey)))
        return false;
    keys.removeAll(key);
    keys.insert(beforeKey.isEmpty() ? keys.size() : keys.indexOf(beforeKey), key);
    return reorder(keys);
}
bool ShellActionModel::moveAfter(const QString &key, const QString &afterKey, Zone projection)
{
    auto keys = projectedKeys(projection);
    if (!keys.contains(key) || !keys.contains(afterKey) || key == afterKey)
        return false;
    keys.removeAll(key);
    keys.insert(keys.indexOf(afterKey) + 1, key);
    return reorder(keys);
}
void ShellActionModel::restoreDefaults()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_order.clear();
    m_placements.clear();
    m_badges.clear();
    applyOrder();
    resolve();
    persist();
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1),
                         {PreferredRole, EffectiveRole, BadgeVisibleRole, BadgePreferredRole});
    emit layoutChanged();
}
bool ShellActionModel::moveInPlayerZone(const QString &key, Zone zone, const QString &relativeKey,
                                        bool after)
{
    const int row = rowFor(key);
    if (m_surface != Playerbar || !validZone(zone) || row < 0 || !m_entries[row].action.visible ||
        key == relativeKey)
        return false;
    auto destination = projectedKeys(zone, true);
    if (!relativeKey.isEmpty() && !destination.contains(relativeKey))
        return false;
    destination.removeAll(key);
    destination.insert(relativeKey.isEmpty() ? destination.size()
                                             : destination.indexOf(relativeKey) + (after ? 1 : 0),
                       key);
    // Editor board order is center, left, right, then More.
    // Reordering preserves slots of unavailable or hidden contributors.
    QStringList board;
    for (const auto region : {Center, Left, Right, More}) {
        auto keys = region == zone ? destination : projectedKeys(region, true);
        if (region != zone)
            keys.removeAll(key);
        board += keys;
    }
    setPlacement(key, zone);
    return reorder(board);
}
bool ShellActionModel::shiftInPlayerZone(const QString &key, int offset)
{
    const auto row = rowFor(key);
    if (m_surface != Playerbar || row < 0)
        return false;
    const auto zone = preferred(m_entries[row]);
    const auto keys = projectedKeys(zone, true);
    const auto source = keys.indexOf(key), target = source + offset;
    if (source < 0 || target < 0 || target >= keys.size() || !offset)
        return false;
    return moveInPlayerZone(key, zone, keys[target], offset > 0);
}
void ShellActionModel::setCapacity(Zone zone, int capacity)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!validZone(zone) || zone == More)
        return;
    capacity = std::max(0, capacity);
    if (m_capacity.value(zone) == capacity)
        return;
    m_capacity[zone] = capacity;
    resolve();
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1), {EffectiveRole});
    emit layoutChanged();
}
ShellActionProjection::ShellActionProjection(QObject *parent) : QAbstractListModel(parent) {}
void ShellActionProjection::setActions(ShellActionModel *actions)
{
    if (actions == this->actions())
        return;
    if (m_actions)
        disconnect(m_actions, nullptr, this, nullptr);
    finishReorder(false);
    removeSourceRows(0, std::numeric_limits<int>::max());
    m_actions = actions;
    if (actions) {
        connect(actions, &QAbstractItemModel::rowsInserted, this,
                &ShellActionProjection::reconcile);
        connect(
            actions, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &, int first, int last) { removeSourceRows(first, last); });
        connect(actions, &QAbstractItemModel::rowsRemoved, this, &ShellActionProjection::reconcile);
        connect(actions, &QAbstractItemModel::rowsMoved, this, &ShellActionProjection::reconcile);
        connect(actions, &QAbstractItemModel::modelAboutToBeReset, this,
                [this] { removeSourceRows(0, std::numeric_limits<int>::max()); });
        connect(actions, &QAbstractItemModel::modelReset, this, &ShellActionProjection::reconcile);
        connect(actions, &QAbstractItemModel::dataChanged, this,
                &ShellActionProjection::sourceDataChanged);
        connect(actions, &QObject::destroyed, this, [this] {
            removeSourceRows(0, std::numeric_limits<int>::max());
            emit actionsChanged();
        });
    }
    reconcile();
    emit actionsChanged();
}
void ShellActionProjection::setZone(ShellActionModel::Zone zone)
{
    if (m_zone == zone)
        return;
    finishReorder(false);
    m_zone = zone;
    reconcile();
    emit zoneChanged();
}
int ShellActionProjection::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}
QHash<int, QByteArray> ShellActionProjection::roleNames() const
{
    return actionRoles();
}
QVariant ShellActionProjection::data(const QModelIndex &index, int role) const
{
    return index.isValid() && !index.parent().isValid() && index.column() == 0 &&
                   index.row() < m_rows.size()
               ? m_rows[index.row()].data(role)
               : QVariant{};
}
bool ShellActionProjection::beginReorder(const QString &key)
{
    finishReorder(false);
    for (const auto &row : m_rows) {
        if (row.data(ShellActionModel::KeyRole).toString() != key)
            continue;
        m_reorderSource = row;
        m_reorderStart = m_rows;
        m_reorderPreview = m_rows;
        return true;
    }
    return false;
}
void ShellActionProjection::previewReorder(int target)
{
    if (!m_reorderSource.isValid() || m_reorderPreview.isEmpty())
        return;
    const int source = m_reorderPreview.indexOf(m_reorderSource);
    target = qBound(0, target, int(m_reorderPreview.size()) - 1);
    if (source < 0 || source == target)
        return;
    m_reorderPreview.move(source, target);
    reconcile();
}
void ShellActionProjection::finishReorder(bool commit)
{
    if (m_reorderStart.isEmpty())
        return;
    QStringList keys;
    if (commit && m_reorderSource.isValid() && m_reorderPreview != m_reorderStart)
        for (const auto &row : m_reorderPreview)
            keys.append(row.data(ShellActionModel::KeyRole).toString());
    m_reorderSource = QPersistentModelIndex{};
    m_reorderStart.clear();
    m_reorderPreview.clear();
    if (!keys.isEmpty() && m_actions)
        m_actions->reorder(keys);
    reconcile();
}
void ShellActionProjection::removeSourceRows(int first, int last)
{
    // Detaching a complete view should retire one range, including a large
    // plugin catalog, rather than delivering one removal for every action.
    if (!m_rows.isEmpty() && first == 0 && last == std::numeric_limits<int>::max()) {
        m_reorderSource = QPersistentModelIndex{};
        m_reorderStart.clear();
        m_reorderPreview.clear();
        beginRemoveRows({}, 0, m_rows.size() - 1);
        m_rows.clear();
        endRemoveRows();
        return;
    }
    for (int row = m_rows.size() - 1; row >= 0; --row) {
        if (m_rows[row].isValid() && (m_rows[row].row() < first || m_rows[row].row() > last))
            continue;
        beginRemoveRows({}, row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }
}
void ShellActionProjection::reconcile()
{
    QList<QPersistentModelIndex> desired;
    if (m_actions)
        for (int row = 0; row < m_actions->rowCount(); ++row) {
            const auto source = m_actions->index(row, 0);
            const auto effective = source.data(ShellActionModel::EffectiveRole).toInt();
            const auto zone =
                m_preferred ? source.data(ShellActionModel::PreferredRole).toInt() : effective;
            if (effective != -2 && (m_zone == ShellActionModel::All || zone == m_zone))
                desired.append(source);
        }
    if (!m_reorderStart.isEmpty()) {
        if (desired == m_reorderStart && m_reorderSource.isValid()) {
            desired = m_reorderPreview;
        } else {
            // Provider removal/visibility/order changes invalidate the gesture,
            // including persistent indexes already retired by the source model.
            m_reorderSource = QPersistentModelIndex{};
            m_reorderStart.clear();
            m_reorderPreview.clear();
        }
    }
    const QSet<QPersistentModelIndex> keep(desired.cbegin(), desired.cend());
    if (m_rows.isEmpty()) {
        if (!desired.isEmpty()) {
            beginInsertRows({}, 0, desired.size() - 1);
            m_rows = std::move(desired);
            endInsertRows();
        }
        return;
    }
    for (int row = m_rows.size() - 1; row >= 0; --row) {
        if (keep.contains(m_rows[row]))
            continue;
        beginRemoveRows({}, row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }
    for (int target = 0; target < desired.size(); ++target) {
        if (target < m_rows.size() && m_rows[target] == desired[target])
            continue;
        const int source = m_rows.indexOf(desired[target], target);
        if (source < 0) {
            beginInsertRows({}, target, target);
            m_rows.insert(target, desired[target]);
            endInsertRows();
        } else {
            beginMoveRows({}, source, source, {}, target);
            m_rows.move(source, target);
            endMoveRows();
        }
    }
}
void ShellActionProjection::sourceDataChanged(const QModelIndex &first, const QModelIndex &last,
                                              const QList<int> &roles)
{
    // Badges/progress/labels cannot change membership. Forward them without
    // rescanning the registry; only placement/visibility needs reconciliation.
    if (roles.isEmpty() || roles.contains(ShellActionModel::EffectiveRole) ||
        (m_preferred && roles.contains(ShellActionModel::PreferredRole)))
        reconcile();
    int start = -1;
    for (int row = 0; row <= m_rows.size(); ++row) {
        const bool changed = row < m_rows.size() && m_rows[row].row() >= first.row() &&
                             m_rows[row].row() <= last.row();
        if (changed && start < 0)
            start = row;
        if (!changed && start >= 0) {
            emit dataChanged(index(start), index(row - 1), roles);
            start = -1;
        }
    }
}
void ShellActionProjection::setUsePreferredPlacement(bool value)
{
    if (m_preferred == value)
        return;
    finishReorder(false);
    m_preferred = value;
    reconcile();
    emit usePreferredPlacementChanged();
}
bool ShellActionModel::setPresentation(const Handle &handle, const QString &title,
                                       const QString &icon)
{
    const int row = rowFor(handle.key);
    if (row < 0 || m_entries[row].generation != handle.generation)
        return false;
    auto &action = m_entries[row].action;
    if (action.title == title && action.icon == icon)
        return true;
    action.title = title;
    action.icon = icon;
    emit dataChanged(index(row), index(row), {TitleRole, IconRole});
    return true;
}
} // namespace EchoMusic
