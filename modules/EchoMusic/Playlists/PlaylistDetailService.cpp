#include <QQmlEngine>
#include "PlaylistDetailService.h"
#include <utility>
namespace EchoMusic
{
PlaylistDetailService::PlaylistDetailService(
    ApiClient &api, PlaybackService &playback, UserPlaylistModel &library,
    PlaylistMutationService &mutations, PlaylistOrderModel &order, SongMetadataService &metadata,
    RouteController &router, bool networkEnabled, QObject *parent, CommentAuthorService *authors)
    : QObject(parent), m_authors(authors), m_api(api), m_playback(playback), m_library(library),
      m_mutations(mutations), m_order(order), m_editor(api, library, mutations, this),
      m_metadata(metadata), m_router(router), m_network(networkEnabled)
{
    // QML borrows this application-owned object, including explicit invokable
    // returns. GC and engine teardown must never delete a C++ member/context.
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&m_editor, &PlaylistEditModel::notice, this, &PlaylistDetailService::notice);
    connect(&api, &ApiClient::sessionChanged, this, [this] { cancelPlayback(); });
    connect(&api, &ApiClient::closed, this, [this] { cancelPlayback(); });
    connect(&playback, &PlaybackService::changed, this, [this] {
        if (m_pendingPlayback &&
            m_playback.clock().value("trackSeq", "0").toString() != m_pendingPlaySequence)
            cancelPlayback();
    });
}
void PlaylistDetailService::attachLibraryController(LibraryController &controller)
{
    if (m_controller)
        return;
    m_controller = &controller;
    connect(&controller, &LibraryController::playlistPlayRequested, this,
            &PlaylistDetailService::playPlaylist);
    connect(&controller, &LibraryController::removed, this, [this](const QString &id) {
        if (m_pendingResource == id)
            cancelPlayback();
        const auto current = m_contexts.value(m_router.current());
        if (current && current->detail().value("global_id").toString() == id) {
            if (m_router.canGoBack())
                m_router.back();
            else
                m_router.navigateHome(true);
        }
    });
}
void PlaylistDetailService::cancelPlayback(bool replacing)
{
    auto context = std::exchange(m_pendingPlayback, {});
    const auto id = std::exchange(m_pendingPlayListId, 0);
    m_pendingResource.clear();
    if (context) {
        disconnect(context, nullptr, this, nullptr);
        context->shutdown();
        context->deleteLater();
    }
    // A replacement command has already published its new busy row, possibly
    // the same row as the retired intent. Do not clear that newer state.
    if (m_controller && !replacing)
        m_controller->finishPlaylistPlayback(id);
}
void PlaylistDetailService::playPlaylist(const QString &resourceId, quint64 cloudId)
{
    cancelPlayback(true);
    if (m_closed) {
        if (m_controller)
            m_controller->finishPlaylistPlayback(cloudId);
        return;
    }
    const auto current = m_contexts.value(m_router.current());
    if (current && m_router.current()->resourceId() == resourceId && current->complete() &&
        !current->loading() && current->errorMessage().isEmpty()) {
        if (m_controller)
            m_controller->finishPlaylistPlayback(cloudId);
        if (current->songs()->tracks().isEmpty())
            emit notice(tr("歌单暂无可播放歌曲"), true);
        else
            current->actions()->play();
        return;
    }
    m_pendingPlayListId = cloudId;
    m_pendingResource = resourceId;
    m_pendingPlaySequence = m_playback.clock().value("trackSeq", "0").toString();
    m_pendingPlayback = new PlaylistDetailContext(m_api, m_playback, m_library, m_mutations,
                                                  m_metadata, resourceId, m_network, this);
    connect(m_pendingPlayback.data(), &PlaylistDetailContext::changed, this,
            &PlaylistDetailService::settlePlayback);
    settlePlayback();
}
void PlaylistDetailService::settlePlayback()
{
    auto *context = m_pendingPlayback.data();
    if (!context || context->loading())
        return;
    // Detach the intent before publishing state or starting playback: either
    // can synchronously trigger another playlist command.
    m_pendingPlayback.clear();
    disconnect(context, nullptr, this, nullptr);
    const auto id = std::exchange(m_pendingPlayListId, 0);
    m_pendingResource.clear();
    if (m_controller)
        m_controller->finishPlaylistPlayback(id);
    if (!context->complete()) {
        emit notice(context->errorMessage().isEmpty() ? tr("歌单暂时无法加载，请稍后重试")
                                                      : context->errorMessage(),
                    true);
    } else if (context->songs()->tracks().isEmpty())
        emit notice(tr("歌单暂无可播放歌曲"), true);
    else
        context->actions()->play();
    context->shutdown();
    context->deleteLater();
}
PlaylistDetailService::~PlaylistDetailService()
{
    shutdown();
    // RouteController may outlive the application services at stack teardown.
    // Retire its child contexts while their borrowed service references live.
    for (const auto &context : std::as_const(m_contexts))
        delete context.data();
    m_contexts.clear();
}
PlaylistDetailContext *PlaylistDetailService::contextFor(RouteContext *route)
{
    if (m_closed || !route || route->name() != "playlist-detail")
        return nullptr;
    if (auto *existing = m_contexts.value(route).data())
        return existing;
    auto *context = new PlaylistDetailContext(m_api, m_playback, m_library, m_mutations, m_metadata,
                                              *route, m_network, m_authors);
    if (m_collections) {
        context->attachCollections(*m_collections);
        m_collections->ensureLoaded();
    }
    m_contexts.insert(route, context);
    connect(context, &PlaylistDetailContext::notice, this, &PlaylistDetailService::notice);
    connect(context->comments(), &CommentsController::notice, this, &PlaylistDetailService::notice);
    connect(context->comments(), &CommentsController::readFailed, this,
            [this](const QString &message) { emit notice(message, true); });
    connect(route, &QObject::destroyed, this, [this, route] { m_contexts.remove(route); });
    return context;
}
void PlaylistDetailService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    cancelPlayback();
    m_editor.close();
    for (const auto &context : std::as_const(m_contexts))
        if (context)
            context->shutdown();
}
} // namespace EchoMusic

namespace EchoMusic
{
void PlaylistDetailService::registerRoutes(RouteController &router)
{
    router.registerPage({"playlist-detail",
                         "/main/playlist/:id",
                         QUrl("qrc:/qt/qml/EchoMusic/Playlists/PlaylistDetailPage.qml"),
                         {"tab"},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
