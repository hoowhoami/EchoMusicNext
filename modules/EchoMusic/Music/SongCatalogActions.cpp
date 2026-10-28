#include <QQmlEngine>
#include "SongCatalogActions.h"
#include "PlaybackLibraryActions.h"
#include "SongCommands.h"
#include <QRandomGenerator>
namespace EchoMusic
{
QVariantMap SongCatalogActions::pluginMenuContext(const QString &id) const
{
    const auto track = m_model.track(id);
    if (!track)
        return {};
    QVariantList selection;
    const auto selected = m_model.selectedIds();
    for (const auto &t : selected) {
        if (selection.size() >= 500)
            break;
        if (auto value = m_model.track(t))
            selection.append(value->publicValue());
    }
    return {{"track", track->publicValue()},
            {"selectedTracks", selection},
            {"selectionCount", m_model.selectedCount()},
            {"truncated", m_model.selectedCount() > 500},
            {"sourceId", m_key}};
}

SongCatalogActions::SongCatalogActions(SongCatalogModel &model, PlaybackService &playback,
                                       QObject *parent)
    : QObject(parent), m_model(model), m_playback(playback)
{
    model.attachMetadata(playback.metadata());
    connect(&playback, &PlaybackService::metadataServiceChanged, &model,
            [&model, &playback] { model.attachMetadata(playback.metadata()); });
    // QML borrows this application-owned object, including explicit invokable
    // returns. GC and engine teardown must never delete a C++ member/context.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    connect(&playback, &PlaybackService::changed, this, &SongCatalogActions::playbackChanged);
    connect(&playback, &PlaybackService::sharingChanged, this, &SongCatalogActions::sharingChanged);
    if (auto *actions = playback.libraryActions())
        connect(actions, &PlaybackLibraryActions::changed, this, [this] {
            ++m_revision;
            emit favoritesChanged();
        });
    connect(playback.songCommands(), &SongCommands::changed, this,
            &SongCatalogActions::blacklistChanged);
}
void SongCatalogActions::setQueueContext(const QString &title, const QString &key)
{
    m_title = title;
    m_key = key;
}
void SongCatalogActions::play(const QString &id, bool replaceContext, bool selectedOnly)
{
    const auto tracks = m_model.tracks(selectedOnly);
    if (tracks.isEmpty())
        return;
    int index = 0;
    if (!id.isEmpty()) {
        index = -1;
        for (int i = 0; i < tracks.size(); ++i)
            if (tracks[i].id == id) {
                index = i;
                break;
            }
        if (index < 0)
            return;
    } else if (replaceContext && m_playback.playMode() == PlaybackService::Shuffle)
        index = QRandomGenerator::global()->bounded(int(tracks.size()));
    if (replaceContext)
        m_playback.playTracks(tracks, index, m_title, m_key);
    else
        m_playback.playFromSelection(tracks, index, m_title, m_key, true);
}
void SongCatalogActions::playOrToggle(const QString &id)
{
    if (!id.isEmpty() && id == m_playback.trackId() && m_model.track(id))
        m_playback.toggle();
    else
        play(id, false);
}
void SongCatalogActions::playNow(const QString &id)
{
    if (const auto track = m_model.track(id); track && m_model.catalogPlayable(id))
        m_playback.queueAndPlayTrack(*track, m_title, m_key);
}
void SongCatalogActions::appendSelected()
{
    m_playback.appendTracks(m_model.tracks(true));
}
void SongCatalogActions::addSelection()
{
    if (auto *a = m_playback.libraryActions())
        a->prepareTracks(m_model.tracks(true));
}
bool SongCatalogActions::isFavorite(const QString &id) const
{
    const auto t = m_model.track(id);
    const auto *a = m_playback.libraryActions();
    return t && a && a->favoriteTrack(*t);
}
void SongCatalogActions::toggleFavorite(const QString &id)
{
    const auto t = m_model.track(id);
    if (auto *a = m_playback.libraryActions(); t && a)
        a->toggleFavoriteTrack(*t);
}
void SongCatalogActions::addSong(const QString &id)
{
    const auto t = m_model.track(id);
    if (auto *a = m_playback.libraryActions(); t && a)
        a->prepareTracks({*t});
}
void SongCatalogActions::appendSong(const QString &id, bool next)
{
    const auto t = m_model.track(id);
    if (t && m_model.catalogPlayable(id) && t->playable())
        m_playback.enqueueTracks({*t}, next);
}
void SongCatalogActions::copySong(const QString &id)
{
    const auto t = m_model.track(id);
    if (!t)
        return;
    // 显示名处理和空值回退统一由命令服务完成。
    m_playback.songCommands()->copyInfo(*t);
}
void SongCatalogActions::shareSong(const QString &id)
{
    const auto t = m_model.track(id);
    if (t)
        m_playback.songCommands()->share(*t);
}
void SongCatalogActions::openDetails(const QString &id)
{
    if (const auto track = m_model.track(id))
        m_playback.songCommands()->openDetails(*track);
}
void SongCatalogActions::openArtist(const QString &id)
{
    m_playback.songCommands()->openArtist(id);
}
void SongCatalogActions::openAlbum(const QString &id)
{
    m_playback.songCommands()->openAlbum(id);
}
void SongCatalogActions::openMv(const QString &id)
{
    if (const auto track = m_model.track(id))
        m_playback.songCommands()->openMv(*track);
}
bool SongCatalogActions::blacklistAvailable(const QString &id) const
{
    const auto t = m_model.track(id);
    return t && m_playback.songCommands()->blacklistAvailable(*t);
}
bool SongCatalogActions::blacklisted(const QString &id) const
{
    const auto t = m_model.track(id);
    return t && m_playback.songCommands()->blacklisted(*t);
}
void SongCatalogActions::addToBlacklist(const QString &id)
{
    const auto t = m_model.track(id);
    if (t)
        m_playback.songCommands()->addToBlacklist(*t);
}
void SongCatalogActions::removeFromBlacklist(const QString &id)
{
    const auto t = m_model.track(id);
    if (t)
        m_playback.songCommands()->removeFromBlacklist(*t);
}
bool SongCatalogActions::blacklistBusy() const
{
    return m_playback.songCommands()->busy();
}
quint64 SongCatalogActions::blacklistRevision() const
{
    return m_playback.songCommands()->revision();
}
} // namespace EchoMusic
