#include <QQmlEngine>
#include "SongCatalogActions.h"
#include "PlaybackLibraryActions.h"
#include "SongCommands.h"
#include <QRandomGenerator>
namespace EchoMusic
{
QString SongCatalogActions::requestPluginMenuContext(const QString &id, QObject *consumer)
{
    if (!consumer || !m_model.m_indices.contains(id))
        return {};
    for (auto it = m_menus.begin(); it != m_menus.end();) {
        if (it.value()->consumer == consumer) {
            delete it.value()->reader.data();
            it = m_menus.erase(it);
        } else
            ++it;
    }
    const auto ticket = QString::number(++m_menuTicket);
    auto state = std::make_shared<MenuSnapshot>();
    state->consumer = consumer;
    state->reader = new QObject(this);
    state->epoch = m_model.m_storageEpoch;
    state->id = id;
    state->source = m_key;
    state->selected = m_model.selectedIds().mid(0, 500);
    state->selectionCount = m_model.selectedCount();
    m_menus.insert(ticket, state);
    connect(consumer, &QObject::destroyed, this, [this, ticket] {
        if (const auto state = m_menus.take(ticket))
            delete state->reader.data();
    });
    const auto source = m_key;
    QTimer::singleShot(0, state->reader, [this, state, ticket, source] {
        QStringList ids{state->id};
        ids.append(state->selected);
        m_model.requestRows(ids, state->reader, [this, state, ticket, source, ids](auto tracks) {
            if (!state->consumer || state->epoch != m_model.m_storageEpoch ||
                tracks.size() != ids.size())
                return;
            for (const auto &id : ids)
                if (const auto found = m_model.m_indices.constFind(id);
                    found != m_model.m_indices.cend())
                    state->versions.insert(id, m_model.m_entries[*found].presentationRevision);
            QVariantList selected;
            for (int i = 1; i < tracks.size(); ++i)
                selected.append(tracks[i].publicValue());
            const QVariantMap context{{"track", tracks.first().publicValue()},
                                      {"selectedTracks", selected},
                                      {"selectionCount", state->selectionCount},
                                      {"truncated", state->selectionCount > 500},
                                      {"sourceId", source}};
            emit pluginMenuContextReady(ticket, context);
        });
    });
    return ticket;
}
bool SongCatalogActions::pluginMenuContextCurrent(const QString &ticket) const
{
    const auto state = m_menus.value(ticket);
    if (!state || !state->consumer || state->versions.isEmpty() ||
        state->epoch != m_model.m_storageEpoch || state->source != m_key ||
        state->selectionCount != m_model.selectedCount() ||
        state->selected != m_model.selectedIds().mid(0, 500))
        return false;
    for (auto it = state->versions.cbegin(); it != state->versions.cend(); ++it) {
        const auto found = m_model.m_indices.constFind(it.key());
        if (found == m_model.m_indices.cend() ||
            m_model.m_entries[*found].presentationRevision != it.value())
            return false;
    }
    return true;
}

SongCatalogActions::SongCatalogActions(SongCatalogModel &model, PlaybackService &playback,
                                       QObject *parent)
    : QObject(parent), m_model(model), m_playback(playback), m_title(tr("播放队列")),
      m_key(QStringLiteral("default"))
{
    model.attachMetadata(playback.metadata());
    connect(&model, &SongCatalogModel::readFailed, this,
            [this](const QString &error) { emit m_playback.notice(error, 2); });
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
QObject *SongCatalogActions::beginPlayRead()
{
    delete m_playRead.data();
    m_playRead = new QObject(this);
    return m_playRead;
}
void SongCatalogActions::play(const QString &id, bool replaceContext, bool selectedOnly)
{
    const auto request = ++m_playRequest;
    const auto sequence = m_playback.clock().value("trackSeq");
    const auto title = m_title, key = m_key;
    const auto inCurrentQueue = m_playInCurrentQueue;
    m_model.requestTracks(selectedOnly, beginPlayRead(),
                          [this, request, sequence, id, replaceContext, title, key,
                           inCurrentQueue](PlaybackTracks tracks) {
                              if (request != m_playRequest)
                                  return;
                              m_playRead->deleteLater();
                              if (sequence != m_playback.clock().value("trackSeq") ||
                                  tracks.isEmpty())
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
                              } else if (replaceContext && !inCurrentQueue &&
                                         m_playback.playMode() == PlaybackService::Shuffle)
                                  index = QRandomGenerator::global()->bounded(int(tracks.size()));
                              if (inCurrentQueue)
                                  m_playback.playInCurrentQueue(tracks[index]);
                              else if (replaceContext)
                                  m_playback.playTracks(tracks, index, title, key);
                              else
                                  m_playback.playFromSelection(tracks, index, title, key, true);
                          });
}
void SongCatalogActions::playOrToggle(const QString &id)
{
    if (!id.isEmpty() && id == m_playback.trackId() && m_model.trackProjection(id)) {
        ++m_playRequest;
        delete m_playRead.data();
        if (!m_playback.loading())
            m_playback.toggle();
    } else
        play(id, false);
}
void SongCatalogActions::playNow(const QString &id)
{
    if (!m_model.catalogPlayable(id))
        return;
    const auto request = ++m_playRequest;
    const auto sequence = m_playback.clock().value("trackSeq");
    const auto title = m_title, key = m_key;
    const auto inCurrentQueue = m_playInCurrentQueue;
    m_model.requestTrack(
        id, beginPlayRead(),
        [this, request, sequence, title, key, inCurrentQueue](PlaybackTrack track) {
            if (request != m_playRequest)
                return;
            m_playRead->deleteLater();
            if (sequence != m_playback.clock().value("trackSeq"))
                return;
            if (inCurrentQueue)
                m_playback.playInCurrentQueue(track);
            else
                m_playback.queueAndPlayTrack(track, title, key);
        });
}
void SongCatalogActions::appendSelected()
{
    m_model.requestTracks(true, this,
                          [this](PlaybackTracks tracks) { m_playback.appendTracks(tracks); });
}
void SongCatalogActions::addSelection()
{
    m_model.requestTracks(true, this, [this](PlaybackTracks tracks) {
        if (auto *a = m_playback.libraryActions())
            a->prepareTracks(tracks);
    });
}
bool SongCatalogActions::isFavorite(const QString &id) const
{
    const auto t = m_model.trackProjection(id);
    const auto *a = m_playback.libraryActions();
    return t && a && a->favoriteTrack(*t);
}
void SongCatalogActions::toggleFavorite(const QString &id)
{
    m_model.requestTrack(id, this, [this](PlaybackTrack track) {
        if (auto *a = m_playback.libraryActions())
            a->toggleFavoriteTrack(track);
    });
}
void SongCatalogActions::addSong(const QString &id)
{
    m_model.requestTrack(id, this, [this](PlaybackTrack track) {
        if (auto *a = m_playback.libraryActions())
            a->prepareTracks({track});
    });
}
void SongCatalogActions::appendSong(const QString &id, bool next)
{
    if (!m_model.catalogPlayable(id))
        return;
    m_model.requestTrack(id, this, [this, next](PlaybackTrack track) {
        if (track.playable())
            m_playback.enqueueTracks({track}, next);
    });
}
void SongCatalogActions::copySong(const QString &id)
{
    m_model.requestTrack(
        id, this, [this](PlaybackTrack track) { m_playback.songCommands()->copyInfo(track); });
}
void SongCatalogActions::shareSong(const QString &id)
{
    m_model.requestTrack(id, this,
                         [this](PlaybackTrack track) { m_playback.songCommands()->share(track); });
}
void SongCatalogActions::openComments(const QString &id)
{
    m_model.requestTrack(
        id, this, [this](PlaybackTrack track) { m_playback.songCommands()->openComments(track); });
}
void SongCatalogActions::openDetails(const QString &id)
{
    m_model.requestTrack(
        id, this, [this](PlaybackTrack track) { m_playback.songCommands()->openDetails(track); });
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
    m_model.requestTrack(id, this,
                         [this](PlaybackTrack track) { m_playback.songCommands()->openMv(track); });
}
bool SongCatalogActions::blacklistAvailable(const QString &id) const
{
    const auto t = m_model.trackProjection(id);
    return t && m_playback.songCommands()->blacklistAvailable(*t);
}
bool SongCatalogActions::blacklisted(const QString &id) const
{
    const auto t = m_model.trackProjection(id);
    return t && m_playback.songCommands()->blacklisted(*t);
}
void SongCatalogActions::addToBlacklist(const QString &id)
{
    m_model.requestTrack(id, this, [this](PlaybackTrack track) {
        m_playback.songCommands()->addToBlacklist(track);
    });
}
void SongCatalogActions::removeFromBlacklist(const QString &id)
{
    m_model.requestTrack(id, this, [this](PlaybackTrack track) {
        m_playback.songCommands()->removeFromBlacklist(track);
    });
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
