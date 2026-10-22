#include "PlaybackQueueCatalog.h"
#include <algorithm>
namespace EchoMusic
{
namespace
{
bool localSource(const QString &key)
{
    return !key.startsWith("fm/") && key != "queue:home-discover" &&
           key != "queue:listen-together" && key != "queue:manual";
}
} // namespace
PlaybackQueueCatalog::PlaybackQueueCatalog(QObject *parent) : QAbstractListModel(parent)
{
    activate("default", tr("播放队列"), {}, -1);
}
QVariant PlaybackQueueCatalog::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() < 0 || i.row() >= rowCount())
        return {};
    const auto &e = m_entries[size_t(i.row())];
    switch (role) {
    case Qt::UserRole + 1:
        return e.key;
    case Qt::UserRole + 2:
        return e.title;
    case Qt::UserRole + 3:
        return e.model->rowCount();
    case Qt::UserRole + 4:
        return i.row() == m_active;
    default:
        return {};
    }
}
PlaybackQueueModel *PlaybackQueueCatalog::modelForKey(const QString &key) const
{
    for (const auto &entry : m_entries)
        if (entry.key == key)
            return entry.model.get();
    return nullptr;
}
QString PlaybackQueueCatalog::titleForKey(const QString &key) const
{
    for (const auto &entry : m_entries)
        if (entry.key == key)
            return entry.title;
    return {};
}
QStringList PlaybackQueueCatalog::prioritySources(const QString &exclude) const
{
    QStringList keys;
    if (m_lastLocal != exclude && modelForKey(m_lastLocal))
        keys.append(m_lastLocal);
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        const auto &entry = *it;
        if (entry.key != exclude && entry.key != m_lastLocal &&
            entry.key != "queue:listen-together")
            keys.append(entry.key);
    }
    return keys;
}
QString PlaybackQueueCatalog::priorityKey(const QString &exclude) const
{
    return priorityKey(exclude, {});
}
QString PlaybackQueueCatalog::priorityKey(const QString &exclude,
                                          const QString &currentTrackId) const
{
    // Try the last ordinary queue first, then newest-created queues.
    // Preloading reads this order without consuming queued-next entries.
    for (const auto &key : prioritySources(exclude))
        if (modelForKey(key)->peekNextPriority(currentTrackId) >= 0)
            return key;
    return {};
}
QPair<QString, int> PlaybackQueueCatalog::takePriority(const QString &exclude,
                                                       const QString &currentTrackId)
{
    for (const auto &key : prioritySources(exclude)) {
        const auto row = modelForKey(key)->takeNextPriority(currentTrackId);
        if (row >= 0)
            return {key, row};
    }
    return {{}, -1};
}

PlaybackQueueModel *PlaybackQueueCatalog::activeModel() const
{
    return m_active >= 0 ? m_entries[size_t(m_active)].model.get() : nullptr;
}
PlaybackQueueModel *PlaybackQueueCatalog::viewModel() const
{
    return m_view >= 0 ? m_entries[size_t(m_view)].model.get() : nullptr;
}
QString PlaybackQueueCatalog::activeTitle() const
{
    return m_active >= 0 ? m_entries[size_t(m_active)].title : QString();
}
QList<int> PlaybackQueueCatalog::previewRows() const
{
    // EchoMusic drawer: current, explicit manual queue, then recently used
    // ordinary sources. Usage order is independent of capacity eviction order.
    // Projection is read-only: browsing cannot reorder storage or activate audio.
    QList<int> rows;
    if (activeModel() && activeModel()->rowCount())
        rows.append(m_active);
    for (int i = 0; i < rowCount(); ++i)
        if (i != m_active && m_entries[size_t(i)].key == "queue:manual" &&
            m_entries[size_t(i)].model->rowCount())
            rows.append(i);
    QList<int> recent;
    for (int i = rowCount() - 1; i >= 0; --i)
        if (!rows.contains(i) && m_entries[size_t(i)].model->rowCount() &&
            localSource(m_entries[size_t(i)].key))
            recent.append(i);
    std::stable_sort(recent.begin(), recent.end(), [this](int a, int b) {
        return m_entries[size_t(a)].lastUsed > m_entries[size_t(b)].lastUsed;
    });
    rows.append(recent);
    return rows;
}
QString PlaybackQueueCatalog::viewTitle() const
{
    return m_view >= 0 ? (m_entries[size_t(m_view)].key.startsWith("search/")
                              ? tr("搜索结果")
                              : m_entries[size_t(m_view)].title)
                       : tr("播放队列");
}
QString PlaybackQueueCatalog::viewSubtitle() const
{
    return m_view >= 0 && m_entries[size_t(m_view)].key.startsWith("search/")
               ? m_entries[size_t(m_view)].key.mid(7)
               : QString();
}
QString PlaybackQueueCatalog::viewStatus() const
{
    return !viewModel() || !viewModel()->rowCount()          ? QString()
           : viewActive()                                    ? tr("当前")
           : m_entries[size_t(m_view)].key == "queue:manual" ? tr("待播")
                                                             : tr("历史");
}
void PlaybackQueueCatalog::switchPreview(int direction)
{
    const auto rows = previewRows();
    const int target = rows.indexOf(m_view) + direction;
    if (target >= 0 && target < rows.size())
        setViewIndex(rows[target]);
}
void PlaybackQueueCatalog::resetPreview()
{
    setViewIndex(m_active);
}
void PlaybackQueueCatalog::removeViewed()
{
    if (m_view < 0)
        return;
    if (viewActive() || m_entries[size_t(m_view)].key == "default") {
        const bool removeDefault = !viewActive() && m_entries[size_t(m_view)].key == "default";
        viewModel()->clear();
        if (removeDefault) {
            m_defaultPresent = false;
            m_view = m_active;
            emit changed();
        }
        return;
    }
    const int victim = m_view;
    const bool removedPreferred = m_entries[size_t(victim)].key == m_lastLocal;
    beginRemoveRows({}, victim, victim);
    m_entries.erase(m_entries.begin() + victim);
    if (m_active > victim)
        --m_active;
    m_view = m_active;
    endRemoveRows();
    if (removedPreferred) {
        m_lastLocal = "default";
        for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
            if (localSource(it->key) && (it->key != "default" || m_defaultPresent)) {
                m_lastLocal = it->key;
                break;
            }
    }
    emit changed();
}
void PlaybackQueueCatalog::setViewIndex(int i)
{
    if (i < 0 || i >= rowCount() || i == m_view)
        return;
    m_view = i;
    emit changed();
}
void PlaybackQueueCatalog::activateViewed()
{
    if (m_view < 0 || m_active == m_view)
        return;
    m_active = m_view;
    m_entries[size_t(m_active)].lastUsed = ++m_usage;
    if (localSource(activeKey()))
        m_lastLocal = activeKey();
    removeInactiveTransient();
    emit dataChanged(index(0), index(rowCount() - 1));
    emit changed();
}
PlaybackQueueModel *PlaybackQueueCatalog::activate(const QString &key, const QString &title,
                                                   const PlaybackTracks &tracks, int current)
{
    int found = ensureQueue(key, title);
    if (key == "default" && !tracks.isEmpty()) {
        materializeDefault();
        trimCapacity();
        for (int i = 0; i < rowCount(); ++i)
            if (m_entries[size_t(i)].key == key)
                found = i;
    }
    const int old = m_active;
    if (localSource(key))
        m_lastLocal = key;
    m_active = m_view = found;
    if (old >= 0)
        emit dataChanged(index(old), index(old));
    removeInactiveTransient();
    found = m_active;
    auto &e = m_entries[size_t(found)];
    e.title = title;
    e.lastUsed = ++m_usage;
    e.model->replace(tracks, current);
    emit dataChanged(index(found), index(found));
    emit changed();
    return e.model.get();
}
bool PlaybackQueueCatalog::appendTo(const QString &key, const PlaybackTracks &tracks)
{
    for (auto &entry : m_entries)
        if (entry.key == key) {
            entry.model->append(tracks, false);
            return true;
        }
    return false;
}
void PlaybackQueueCatalog::clear()
{
    beginResetModel();
    m_entries.clear();
    m_active = m_view = -1;
    m_lastLocal = "default";
    m_defaultPresent = false;
    endResetModel();
    activate("default", tr("播放队列"), {}, -1);
}
PlaybackQueueStates PlaybackQueueCatalog::snapshot() const
{
    PlaybackQueueStates states;
    for (const auto &e : m_entries)
        if (e.key != "default" || m_defaultPresent)
            states.append({e.key, e.title, e.model->tracks(), e.model->currentIndex(),
                           e.model->nextKeys(), e.lastUsed});
    return states;
}
void PlaybackQueueCatalog::restore(const PlaybackQueueStates &states, const QString &activeKey,
                                   const QString &lastLocal, bool discardTransient)
{
    m_restoring = true;
    clear();
    for (const auto &state : states) {
        if (discardTransient &&
            (state.key.startsWith("fm/") || state.key == "queue:home-discover" ||
             state.key == "queue:listen-together"))
            continue;
        if (state.key == "default")
            materializeDefault();
        auto *model = activate(state.key, state.title, state.tracks, state.current);
        model->restoreNext(state.next);
        m_entries[size_t(m_active)].lastUsed = state.lastUsed;
        m_usage = qMax(m_usage, state.lastUsed);
    }
    QString preferred = activeKey;
    if (discardTransient && !modelForKey(preferred)) {
        if (activeKey.startsWith("fm/") && modelForKey(lastLocal) &&
            (lastLocal != "default" || m_defaultPresent))
            preferred = lastLocal;
        else if (!m_entries.empty())
            preferred = m_entries.back().key;
    }
    for (int i = 0; i < rowCount(); ++i)
        if (m_entries[size_t(i)].key == preferred)
            m_active = m_view = i;
    const auto liveKey = this->activeKey();
    m_lastLocal = modelForKey(lastLocal) && localSource(lastLocal) &&
                          (lastLocal != "default" || m_defaultPresent)
                      ? lastLocal
                      : (localSource(liveKey) ? liveKey : QString("default"));
    m_restoring = false;
    removeInactiveTransient();
    emit changed();
}
void PlaybackQueueCatalog::trimCapacity()
{
    // Enforce the queue limit before activation. Protect the current queue
    // and materialized default; evict the oldest-created remaining queue.
    while (rowCount() - (m_defaultPresent ? 0 : 1) > 4) {
        int victim = 0;
        while (victim < rowCount() &&
               (victim == m_active || m_entries[size_t(victim)].key == "default"))
            ++victim;
        if (victim >= rowCount())
            return;
        beginRemoveRows({}, victim, victim);
        m_entries.erase(m_entries.begin() + victim);
        if (m_active > victim)
            --m_active;
        if (m_view == victim)
            m_view = m_active;
        else if (m_view > victim)
            --m_view;
        endRemoveRows();
    }
}
void PlaybackQueueCatalog::materializeDefault()
{
    if (m_defaultPresent)
        return;
    m_defaultPresent = true;
    for (int row = 0; row < rowCount(); ++row) {
        if (m_entries[size_t(row)].key != "default")
            continue;
        const int last = rowCount() - 1;
        if (row == last)
            return;
        beginMoveRows({}, row, row, {}, rowCount());
        auto entry = std::move(m_entries[size_t(row)]);
        m_entries.erase(m_entries.begin() + row);
        m_entries.push_back(std::move(entry));
        if (m_active == row)
            m_active = last;
        else if (m_active > row)
            --m_active;
        if (m_view == row)
            m_view = last;
        else if (m_view > row)
            --m_view;
        endMoveRows();
        return;
    }
}
void PlaybackQueueCatalog::removeInactiveTransient()
{
    if (m_restoring)
        return;
    for (int row = rowCount() - 1; row >= 0; --row) {
        const auto key = m_entries[size_t(row)].key;
        if (row == m_active || (key != "fm/personal" && key != "queue:home-discover"))
            continue;
        beginRemoveRows({}, row, row);
        m_entries.erase(m_entries.begin() + row);
        if (m_active > row)
            --m_active;
        if (m_view == row)
            m_view = m_active;
        else if (m_view > row)
            --m_view;
        endRemoveRows();
        emit transientQueueRemoved(key);
    }
}
} // namespace EchoMusic

namespace EchoMusic
{
void PlaybackQueueCatalog::removeInactiveRoomQueue()
{
    for (int i = 0; i < rowCount(); ++i) {
        if (i == m_active || m_entries[size_t(i)].key != "queue:listen-together")
            continue;
        beginRemoveRows({}, i, i);
        m_entries.erase(m_entries.begin() + i);
        if (m_active > i)
            --m_active;
        if (m_view == i)
            m_view = m_active;
        else if (m_view > i)
            --m_view;
        endRemoveRows();
        emit changed();
        return;
    }
}
} // namespace EchoMusic

int EchoMusic::PlaybackQueueCatalog::ensureQueue(const QString &key, const QString &title)
{
    int found = -1;
    for (int i = 0; i < rowCount(); ++i)
        if (m_entries[size_t(i)].key == key) {
            found = i;
            break;
        }
    if (found < 0) {
        found = rowCount();
        beginInsertRows({}, found, found);
        Entry e{key, title, std::make_unique<PlaybackQueueModel>(this)};
        auto *model = e.model.get();
        m_entries.push_back(std::move(e));
        endInsertRows();
        connect(model, &PlaybackQueueModel::changed, this, [this, model] {
            if (modelForKey("default") == model && model->rowCount())
                materializeDefault();
            for (int i = 0; i < rowCount(); ++i)
                if (m_entries[size_t(i)].model.get() == model) {
                    m_entries[size_t(i)].lastUsed = ++m_usage;
                    emit dataChanged(index(i), index(i));
                }
            trimCapacity();
            emit changed();
        });
        emit modelAdded(model);
        trimCapacity();
        found = rowCount() - 1;
    }
    return found;
}
EchoMusic::PlaybackQueueModel *EchoMusic::PlaybackQueueCatalog::retain(const QString &key,
                                                                       const QString &title,
                                                                       const PlaybackTracks &tracks)
{
    const int row = ensureQueue(key, title);
    auto *model = m_entries[size_t(row)].model.get();
    m_entries[size_t(row)].title = title;
    // Replacing the active selection must retire its transport and lyrics too.
    if (model == activeModel())
        model->clear();
    model->replace(tracks);
    emit changed();
    return model;
}
