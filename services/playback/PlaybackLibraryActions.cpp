#include "PlaybackLibraryActions.h"
#include "PlaybackService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
namespace EchoMusic
{
namespace
{
QString favoriteKey(const PlaybackTrack &track)
{
    return track.hash.toUpper();
}
QString errorText(const QByteArray &bytes)
{
    const auto object = QJsonDocument::fromJson(bytes).object();
    return object.value("message").toString(QObject::tr("歌单操作失败，请重试"));
}
} // namespace
PlaybackLibraryActions::PlaybackLibraryActions(PlaybackService &playback, ApiClient &api,
                                               UserPlaylistModel &library,
                                               PlaylistMutationService &writer, QObject *parent)
    : QAbstractListModel(parent), m_playback(playback), m_api(api), m_library(library),
      m_writer(writer)
{
    connect(&api, &ApiClient::sessionChanged, this, &PlaybackLibraryActions::reset);
    connect(&api, &ApiClient::closed, this, &PlaybackLibraryActions::reset);
    connect(&api, &ApiClient::succeeded, this, &PlaybackLibraryActions::favoritePage);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &bytes) {
        if (!id || id != m_favoriteRead || m_revision != m_api.sessionRevision())
            return;
        m_favoriteRead = 0;
        failFavoriteRead(errorText(bytes));
    });
    connect(&library, &UserPlaylistModel::changed, this, [this] {
        rebuild();
        if (m_library.state() == UserPlaylistModel::State::Ready && !m_favoriteRead &&
            !m_favoritesReady && !m_ownedWrite)
            refreshFavorites();
        if (m_favoriteIntent && (m_library.state() == UserPlaylistModel::State::Error ||
                                 m_library.state() == UserPlaylistModel::State::RestartRequired))
            failFavoriteRead(m_library.errorMessage());
    });
    connect(playback.queueCatalog(), &PlaybackQueueCatalog::changed, this,
            &PlaybackLibraryActions::rebuild);
    connect(&playback, &PlaybackService::changed, this, &PlaybackLibraryActions::changed);
    connect(&writer, &PlaylistMutationService::completed, this,
            &PlaybackLibraryActions::writeCompleted);
    connect(&writer, &PlaylistMutationService::changed, this, [this] {
        if (!m_ownedWrite)
            return;
        const auto state = m_writer.state();
        if (state == PlaylistMutationService::State::OutcomeUnknown ||
            state == PlaylistMutationService::State::RefreshRequired)
            writeFailed(m_writer.errorMessage());
        else if (state == PlaylistMutationService::State::Ready &&
                 !m_writer.errorMessage().isEmpty())
            writeFailed(m_writer.errorMessage());
    });
    reset();
}
PlaybackLibraryActions::~PlaybackLibraryActions()
{
    if (m_favoriteRead)
        m_api.cancel(m_favoriteRead);
}
int PlaybackLibraryActions::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_targets.size();
}
QHash<int, QByteArray> PlaybackLibraryActions::roleNames() const
{
    return {{TargetKey, "targetKey"},         {TargetTitle, "targetTitle"},
            {TargetSection, "targetSection"}, {TargetCount, "songCount"},
            {TargetSelected, "selected"},     {TargetResult, "resultText"},
            {TargetFailed, "failed"}};
}
QVariant PlaybackLibraryActions::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_targets.size())
        return {};
    const auto &t = m_targets[index.row()];
    switch (role) {
    case TargetKey:
        return t.key;
    case TargetTitle:
        return t.title;
    case TargetSection:
        return t.section;
    case TargetCount:
        return t.count;
    case TargetSelected:
        return m_selected.contains(t.key);
    case TargetResult:
        return m_results.value(t.key).text;
    case TargetFailed:
        return !m_results.value(t.key).failed.isEmpty();
    default:
        return {};
    }
}
bool PlaybackLibraryActions::favorite() const
{
    return favoriteTrack(m_playback.currentTrack());
}
int PlaybackLibraryActions::selectedCount() const
{
    int count = 0;
    for (const auto &t : m_targets)
        if (m_selected.contains(t.key))
            ++count;
    return count;
}
bool PlaybackLibraryActions::hasFailures() const
{
    for (const auto &r : m_results)
        if (!r.failed.isEmpty())
            return true;
    return false;
}
void PlaybackLibraryActions::notifyRows()
{
    if (rowCount())
        emit dataChanged(index(0), index(rowCount() - 1));
    emit changed();
}
void PlaybackLibraryActions::reset()
{
    if (m_favoriteRead)
        m_api.cancel(m_favoriteRead);
    m_favoriteRead = 0;
    m_revision = m_api.sessionRevision();
    m_likedId = 0;
    m_favoritesReady = false;
    m_favoriteIntent.reset();
    m_favorites.clear();
    m_readFavorites.clear();
    m_ownedWrite = m_busy = m_favoriteWrite = false;
    m_multiple = false;
    m_pending.clear();
    m_selected.clear();
    m_results.clear();
    m_songs.clear();
    m_writeSongs.clear();
    m_remaining.clear();
    m_failed.clear();
    m_writeTargets.clear();
    m_currentTarget.clear();
    m_removing = false;
    m_error.clear();
    rebuild();
}
int PlaybackLibraryActions::likedRow() const
{
    // Single shared lookup in the library model; the player's like state and
    // the favorites page must resolve the same row.
    return m_library.likedRow();
}
void PlaybackLibraryActions::rebuild()
{
    QList<Target> rows;
    auto *catalog = m_playback.queueCatalog();
    for (int row = 0; m_playbackQueues && row < catalog->rowCount(); ++row) {
        const auto i = catalog->index(row);
        const auto key = catalog->data(i, Qt::UserRole + 1).toString();
        if (key.startsWith("fm/"))
            continue;
        rows.append({"queue/" + key, catalog->data(i, Qt::UserRole + 2).toString(), tr("播放队列"),
                     0, catalog->data(i, Qt::UserRole + 3).toInt()});
    }
    QList<Target> playlists;
    const auto favoriteRow = likedRow();
    for (int row = 0; row < m_library.rowCount(); ++row)
        if (const auto id = m_library.cloudIdAt(row)) {
            const auto i = m_library.index(row);
            playlists.append({"playlist/" + QString::number(id->value),
                              m_library.data(i, UserPlaylistModel::TitleRole).toString(),
                              tr("歌单"), id->value,
                              m_library.data(i, UserPlaylistModel::SongCountRole).toInt()});
            if (row == favoriteRow)
                playlists.move(playlists.size() - 1, 0);
        }
    rows.append(playlists);
    beginResetModel();
    m_sectionCount = 0;
    QString previousSection;
    for (const auto &target : rows) {
        if (target.section != previousSection)
            ++m_sectionCount;
        previousSection = target.section;
    }
    m_targets = std::move(rows);
    endResetModel();
    emit changed();
}
void PlaybackLibraryActions::refreshFavorites()
{
    const int row = likedRow();
    const auto id = m_library.cloudIdAt(row);
    if (m_favoriteRead || m_ownedWrite || !m_api.currentAccountUserId())
        return;
    if (!id) {
        if (!m_favoriteIntent)
            return;
        if (m_library.state() == UserPlaylistModel::State::Idle)
            m_library.reload();
        else if (m_library.canFetchMore({}))
            QTimer::singleShot(0, this, [this] {
                if (m_favoriteIntent)
                    m_library.fetchMore({});
            });
        else if (m_library.state() != UserPlaylistModel::State::Loading)
            failFavoriteRead(tr("未找到收藏歌单，请刷新歌单后重试"));
        return;
    }
    m_likedId = id->value;
    m_revision = m_api.sessionRevision();
    m_favoritePage = 1;
    m_favoriteSeen = 0;
    m_favoriteTotal = 0;
    m_favoriteVersion = 0;
    m_readFavorites.clear();
    readFavoritePage();
}
void PlaybackLibraryActions::readFavoritePage()
{
    m_favoriteRead = m_api.cloudPlaylistTracks({m_likedId}, m_favoritePage, 300);
    if (!m_favoriteRead)
        failFavoriteRead(tr("无法加载收藏歌曲"));
    emit changed();
}
void PlaybackLibraryActions::favoritePage(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_favoriteRead || m_revision != m_api.sessionRevision())
        return;
    m_favoriteRead = 0;
    const auto root = QJsonDocument::fromJson(bytes).object();
    const auto page = root.value("page").toObject();
    const auto version = quint64(root.value("revision").toInteger(-1));
    const auto total = page.value("total").toInteger(-1);
    const auto items = page.value("items").toArray();
    if (quint64(root.value("user_id").toInteger()) != m_api.currentAccountUserId() ||
        quint64(root.value("cloud_list_id").toInteger()) != m_likedId ||
        !page.value("items").isArray() || (m_favoritePage > 1 && version != m_favoriteVersion)) {
        failFavoriteRead(tr("收藏列表已改变，请重试"));
        return;
    }
    const auto previousCount = m_readFavorites.size();
    m_favoriteVersion = version;
    m_favoriteTotal = quint64(qMax<qint64>(0, total));
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto track = PlaybackTrack::fromDomain(item.value("song").toObject());
        const auto file = item.value("file_id").toInteger();
        // This projection can act only on hash/file identities. Incomplete
        // records must not discard other favorites in the accepted page.
        if (track.hash.isEmpty() || file <= 0)
            continue;
        m_readFavorites.insert(favoriteKey(track), quint64(file));
    }
    m_favoriteSeen += items.size();
    if (m_readFavorites.size() > previousCount &&
        (total >= 0 ? m_favoriteSeen < quint64(total) : items.size() >= 200)) {
        ++m_favoritePage;
        readFavoritePage();
        return;
    }
    m_favorites = std::move(m_readFavorites);
    m_favoritesReady = true;
    m_error.clear();
    emit changed();
    // Reading can span several pages. Resume only the captured track in the
    // same account; never turn a late click into a mutation of the next song.
    QTimer::singleShot(0, this, &PlaybackLibraryActions::resumeFavorite);
}
void PlaybackLibraryActions::failFavoriteRead(const QString &message)
{
    m_favoritesReady = false;
    const bool requested = m_favoriteIntent.has_value();
    m_favoriteIntent.reset();
    m_readFavorites.clear();
    m_error = message;
    if (requested)
        emit notice(message, true);
    emit changed();
}
void PlaybackLibraryActions::resumeFavorite()
{
    if (!m_favoriteIntent)
        return;
    const auto song = *m_favoriteIntent;
    m_favoriteIntent.reset();
    if (m_revision == m_api.sessionRevision() &&
        (!m_favoriteIntentRequiresCurrent || song.key() == m_playback.currentTrack().key()))
        toggleFavoriteTrack(song, m_favoriteIntentRequiresCurrent);
    emit changed();
}
bool PlaybackLibraryActions::favoriteTrack(const PlaybackTrack &track) const
{
    return m_favorites.contains(favoriteKey(track));
}
void PlaybackLibraryActions::toggleFavorite()
{
    // Preserve the shared unauthenticated entry behavior even with no track.
    toggleFavoriteTrack(m_playback.currentTrack(), true);
}
void PlaybackLibraryActions::toggleFavoriteTrack(const PlaybackTrack &song, bool requireCurrent)
{
    if (!m_api.currentAccountUserId()) {
        emit loginRequested();
        return;
    }
    if (m_busy || m_favoriteIntent || !song.playable())
        return;
    if (!m_favoritesReady) {
        m_favoriteIntent = song;
        m_favoriteIntentRequiresCurrent = requireCurrent;
        refreshFavorites();
        emit changed();
        return;
    }
    if (song.hash.isEmpty())
        return;
    m_removing = favoriteTrack(song);
    const bool accepted =
        m_removing
            ? m_writer.removeTracks({m_likedId}, {{m_favorites.value(favoriteKey(song))}})
            : m_writer.addTracks({m_likedId}, {{song.title, song.hash, song.albumId, song.songId}});
    if (!accepted) {
        m_error = m_writer.errorMessage().isEmpty() ? tr("歌单正在处理中，请稍后重试")
                                                    : m_writer.errorMessage();
        emit notice(m_error, true);
        emit changed();
        return;
    }
    m_writeSongs = {song};
    m_favoriteWrite = m_ownedWrite = m_busy = true;
    m_error.clear();
    emit changed();
}
void PlaybackLibraryActions::prepareTracks(const PlaybackTracks &tracks, bool playbackQueues)
{
    if (m_busy || tracks.isEmpty())
        return;
    m_playbackQueues = playbackQueues;
    m_songs.clear();
    for (const auto &track : tracks)
        if (track.playable())
            m_songs.append(track);
    if (m_songs.isEmpty())
        return;
    m_selected.clear();
    m_results.clear();
    m_multiple = false;
    m_error.clear();
    rebuild();
    if (m_api.currentAccountUserId() && m_library.state() == UserPlaylistModel::State::Idle)
        m_library.reload();
    emit dialogRequested();
}
void PlaybackLibraryActions::prepareCurrent()
{
    if (m_busy || !m_playback.hasTrack())
        return;
    m_playbackQueues = true;
    m_songs = {m_playback.currentTrack()};
    m_selected.clear();
    m_results.clear();
    m_multiple = false;
    m_error.clear();
    rebuild();
    if (m_api.currentAccountUserId() && m_library.state() == UserPlaylistModel::State::Idle)
        m_library.reload();
    emit dialogRequested();
}
void PlaybackLibraryActions::prepareViewedQueue()
{
    if (m_busy || !m_playback.queueCatalog()->viewModel())
        return;
    m_playbackQueues = true;
    m_songs = m_playback.queueCatalog()->viewModel()->tracks();
    if (m_songs.isEmpty())
        return;
    m_selected.clear();
    m_results.clear();
    m_multiple = false;
    m_error.clear();
    rebuild();
    emit dialogRequested();
}
void PlaybackLibraryActions::setMultiple(bool value)
{
    if (!m_busy && m_multiple != value) {
        m_multiple = value;
        m_selected.clear();
        m_results.clear();
        notifyRows();
    }
}
void PlaybackLibraryActions::selectAll(bool checked)
{
    if (m_busy || !m_multiple || !m_results.isEmpty())
        return;
    m_selected.clear();
    if (checked)
        for (const auto &t : m_targets)
            m_selected.insert(t.key);
    notifyRows();
}
void PlaybackLibraryActions::choose(int row)
{
    if (m_busy || row < 0 || row >= m_targets.size() || m_songs.isEmpty())
        return;
    const auto key = m_targets[row].key;
    if (m_multiple) {
        if (!m_results.isEmpty())
            return;
        if (m_selected.contains(key))
            m_selected.remove(key);
        else
            m_selected.insert(key);
        notifyRows();
    } else {
        m_selected = {key};
        submitSelection();
    }
}
void PlaybackLibraryActions::submitSelection()
{
    if (m_busy || m_songs.isEmpty() || !selectedCount())
        return;
    m_pending.clear();
    m_writeTargets.clear();
    for (const auto &target : m_targets)
        m_writeTargets.insert(target.key, target);
    for (const auto &t : m_targets)
        if (m_selected.contains(t.key) &&
            (!hasFailures() || !m_results.value(t.key).failed.isEmpty()))
            m_pending.enqueue(t.key);
    if (m_pending.isEmpty())
        return;
    m_busy = true;
    m_favoriteWrite = false;
    m_removing = false;
    m_error.clear();
    m_currentTarget.clear();
    nextWrite();
}
void PlaybackLibraryActions::nextWrite()
{
    if (!m_busy)
        return;
    if (m_revision != m_api.sessionRevision()) {
        reset();
        return;
    }
    if (m_remaining.isEmpty()) {
        if (!m_currentTarget.isEmpty())
            m_results[m_currentTarget] = {
                m_failed.isEmpty() ? tr("添加成功") : tr("%1 首添加失败").arg(m_failed.size()),
                m_failed};
        m_currentTarget.clear();
        m_failed.clear();
        if (m_pending.isEmpty()) {
            m_busy = false;
            if (!m_favoritesReady && m_selected.contains("playlist/" + QString::number(m_likedId)))
                refreshFavorites();
            notifyRows();
            int failed = 0;
            for (const auto &result : std::as_const(m_results))
                failed += result.failed.size();
            emit notice(failed ? tr("添加完成，%1 首添加失败").arg(failed) : tr("添加成功"),
                        failed > 0);
            if (!hasFailures())
                emit completed();
            return;
        }
        m_currentTarget = m_pending.dequeue();
        m_remaining = m_results.value(m_currentTarget).failed.isEmpty()
                          ? m_songs
                          : m_results.value(m_currentTarget).failed;
    }
    const auto target = m_writeTargets.constFind(m_currentTarget);
    if (target == m_writeTargets.cend()) {
        writeFailed(tr("添加目标已不存在"));
        return;
    }
    if (!target->cloudId) {
        const bool ok = m_playback.queueCatalog()->appendTo(target->key.mid(6), m_remaining);
        if (!ok)
            m_failed = m_remaining;
        m_remaining.clear();
        notifyRows();
        QTimer::singleShot(0, this, &PlaybackLibraryActions::nextWrite);
        return;
    }
    if (!m_api.currentAccountUserId()) {
        writeFailed(tr("请先登录"));
        return;
    }
    m_writeSongs = m_remaining.mid(0, 300);
    QList<ApiClient::PlaylistTrackInput> inputs;
    for (const auto &song : m_writeSongs)
        inputs.append({song.title, song.hash, song.albumId, song.songId});
    if (!m_writer.addTracks({target->cloudId}, inputs)) {
        writeFailed(m_writer.errorMessage().isEmpty() ? tr("歌单正在处理中，请稍后重试")
                                                      : m_writer.errorMessage());
        return;
    }
    m_ownedWrite = true;
    m_results[m_currentTarget] = {tr("添加中…"), {}};
    notifyRows();
}
void PlaybackLibraryActions::writeCompleted(const QByteArray &bytes)
{
    if (!m_ownedWrite || m_revision != m_api.sessionRevision())
        return;
    m_ownedWrite = false;
    const auto root = QJsonDocument::fromJson(bytes).object();
    const auto outcomes = root.value("outcomes").toArray();
    const auto expectedId =
        m_favoriteWrite ? m_likedId : m_writeTargets.value(m_currentTarget).cloudId;
    if (quint64(root.value("user_id").toInteger()) != m_api.currentAccountUserId() ||
        quint64(root.value("cloud_list_id").toInteger()) != expectedId ||
        (!(m_favoriteWrite && m_removing) && outcomes.size() != m_writeSongs.size())) {
        writeFailed(tr("歌单写入结果无法核对"));
        return;
    }
    if (m_favoriteWrite) {
        const bool ok =
            m_removing || (!outcomes.isEmpty() &&
                           outcomes.first().toObject().value("resource_code").toInt() == 1);
        if (ok) {
            const auto key = favoriteKey(m_writeSongs.first());
            if (m_removing)
                m_favorites.remove(key);
            else
                applyFavoriteAdds(outcomes);
            if (m_removing)
                emit favoriteWritten(m_writeSongs.first(), false);
            emit notice(m_removing ? tr("已取消收藏") : tr("收藏成功"), false);
        } else {
            m_error = tr("音乐服务未接受收藏操作");
            emit notice(m_error, true);
        }
        m_favoriteWrite = m_busy = false;
        // The acknowledged row has already passed the shared version readback.
        // Keep the complete membership index instead of re-reading every page
        // after a known single-song change. Unknown file IDs still need a read.
        m_favoritesReady =
            ok && (m_removing || m_favorites.value(favoriteKey(m_writeSongs.first())) > 0);
        m_removing = false;
        if (!m_favoritesReady)
            refreshFavorites();
        emit changed();
        return;
    }
    // Adding to the account's liked playlist is the same membership change as
    // the heart action, including batch/partial successes. Other lists are not.
    const auto liked = m_library.cloudIdAt(likedRow());
    if (liked && expectedId == liked->value) {
        m_likedId = liked->value;
        applyFavoriteAdds(outcomes);
    }
    for (int i = 0; i < outcomes.size(); ++i)
        if (outcomes[i].toObject().value("resource_code").toInt() != 1)
            m_failed.append(m_writeSongs[i]);
    m_remaining.remove(0, m_writeSongs.size());
    m_writeSongs.clear();
    // Completion is emitted inside the writer's signal stack. Defer the next
    // target until its reconciliation lifecycle has completely returned.
    QTimer::singleShot(0, this, &PlaybackLibraryActions::nextWrite);
}
void PlaybackLibraryActions::applyFavoriteAdds(const QJsonArray &outcomes)
{
    for (int i = 0; i < outcomes.size(); ++i) {
        const auto outcome = outcomes[i].toObject();
        if (outcome.value("resource_code").toInt() != 1)
            continue;
        const auto &song = m_writeSongs[i];
        const auto key = favoriteKey(song);
        if (key.isEmpty()) {
            m_favoritesReady = false;
            continue;
        }
        const bool alreadyFavorite = m_favorites.contains(key);
        const auto file = outcome.value("file_id").toInteger();
        // Keep a known removal identity when an idempotent add omits it.
        if (file > 0 || !alreadyFavorite)
            m_favorites.insert(key, file > 0 ? quint64(file) : 0);
        if (!m_favorites.value(key))
            m_favoritesReady = false;
        if (!alreadyFavorite)
            emit favoriteWritten(song, true);
    }
}
void PlaybackLibraryActions::writeFailed(const QString &message)
{
    m_error = message;
    emit notice(message, true);
    m_ownedWrite = false;
    if (!m_currentTarget.isEmpty()) {
        m_failed.append(m_remaining);
        m_results[m_currentTarget] = {tr("添加失败"), m_failed};
    }
    for (const auto &key : m_pending)
        m_results[key] = {tr("尚未添加"), m_songs};
    m_pending.clear();
    m_remaining.clear();
    m_favoriteWrite = m_busy = m_removing = false;
    notifyRows();
}
void PlaybackLibraryActions::fetchMoreTargets()
{
    if (m_library.canFetchMore({}))
        m_library.fetchMore({});
}
} // namespace EchoMusic
