#include "SettingsKeys.h"
#include "AppContext_p.h"
#include "AccountController.h"
#include "AccountService.h"
#include "AlbumDetailService.h"
#include "ApiClient.h"
#include "ArtistDetailService.h"
#include "CloudService.h"
#include "CloudUploadService.h"
#include "CommentAuthorService.h"
#include "ContentBlacklistService.h"
#include "DetailsService.h"
#include "DiscoveryRouteCoordinator.h"
#include "DiscoveryService.h"
#include "ExploreService.h"
#include "FavoritesService.h"
#include "HistoryService.h"
#include "HistoryStatsShare.h"
#include "LibraryCollectionService.h"
#include "LibraryController.h"
#include "ListenTogetherUi.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "PlaylistDetailService.h"
#include "PlaylistMutationService.h"
#include "PlaylistOrderModel.h"
#include "ProfileRouteCoordinator.h"
#include "ProfileService.h"
#include "PurchasedService.h"
#include "RecognitionCapture.h"
#include "RecognitionService.h"
#include "RouteController.h"
#include "SearchModel.h"
#include "SearchRouteCoordinator.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "SongMetadataService.h"
#include "ToastService.h"
#include "UserPlaylistModel.h"
#include "VideoCollectionService.h"
#include "WindowController.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSet>
#include <QStandardPaths>
#include <QProcess>
#include <QUrlQuery>

namespace EchoMusic
{
void AppContext::createPages()
{
    auto &settings = *preferences;
    auto &shell = *m_shell;
    auto &window = *m_window;
    auto &router = *m_router;
    auto &shellController = *m_shellController;
    auto &api = *m_api;
    auto &network = *m_network;
    auto &playback = *m_playback;
    auto &account = *m_account;
    auto &profile = *m_profile;
    auto &userPlaylists = *m_userPlaylists;
    auto &playlistMutations = *m_playlistMutations;
    auto &collections = *m_collections;
    auto &accountController = *m_accountController;
    auto &libraryController = *m_libraryController;
    const auto showNotice = [&shellController](const QString &message, bool warning) {
        shellController.toasts()->show(message,
                                       warning ? ToastService::Warning : ToastService::Success);
    };
    bind(&libraryController, &LibraryController::notice, &shellController, showNotice);
    bind(libraryController.importer(), &PlaylistImportService::notice, &shellController,
         showNotice);
    auto &search = *m_search;
    m_commentAuthors = std::make_unique<CommentAuthorService>(api, !smoke);
    auto &commentAuthors = *m_commentAuthors;
    m_songMetadata = std::make_unique<SongMetadataService>(api);
    auto &songMetadata = *m_songMetadata;
    if (!smoke)
        songMetadata.enableCoverCache(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
            "/embedded-covers-v1");
    playback.attachMetadata(songMetadata);
    search.attachMetadata(songMetadata, !smoke);
    playback.listenTogether()->attachMetadata(songMetadata);
    m_playlistDetails = std::make_unique<PlaylistDetailService>(
        api, playback, userPlaylists, playlistMutations, *libraryController.order(), songMetadata,
        router, !smoke, nullptr, &commentAuthors);
    auto &playlistDetails = *m_playlistDetails;
    // 页面服务只创建数据所有者；RouteCatalog 在统一阶段显式挂载路由。
    m_favorites = std::make_unique<FavoritesService>(
        api, playback, userPlaylists, *libraryController.order(), songMetadata, router, !smoke);
    auto &favorites = *m_favorites;
    favorites.attachPlaylistMutations(playlistMutations);
    bind(&favorites, &FavoritesService::notice, &shellController, showNotice);
    bind(libraryController.order(), &EchoMusic::PlaylistOrderModel::notice, &shellController,
         [&](const QString &message, bool warning) {
             shellController.toasts()->show(message, warning ? EchoMusic::ToastService::Warning
                                                             : EchoMusic::ToastService::Success);
         });
    bind(&profile, &EchoMusic::ProfileService::relationshipsMutated, &favorites,
         &EchoMusic::FavoritesService::invalidateFollow);
    m_cloud = std::make_unique<CloudService>(api, playback, songMetadata, router, !smoke);
    auto &cloud = *m_cloud;
    cloud.upload()->attachTasks(*shellController.tasks(), router);
    bind(cloud.upload(), &CloudUploadService::cancelled, &shellController, [&shellController] {
        shellController.toasts()->show(QObject::tr("已取消上传"), ToastService::Info);
    });
    cloud.upload()->setProperty(
        "backgroundReminderMuted",
        settings.value(EchoMusic::SettingsKeys::Cloud::UploadBackgroundReminderMuted, false));
    bind(cloud.upload(), &EchoMusic::CloudUploadService::reminderChanged, &cloud, [&] {
        settings.setValue(EchoMusic::SettingsKeys::Cloud::UploadBackgroundReminderMuted,
                          cloud.upload()->property("backgroundReminderMuted"));
    });
    bind(&cloud, &EchoMusic::CloudService::notice, &shellController,
         [&](const QString &message, bool warning) {
             shellController.toasts()->show(message, warning ? EchoMusic::ToastService::Warning
                                                             : EchoMusic::ToastService::Success);
         });
    m_purchased = std::make_unique<PurchasedService>(api, playback, router, !smoke);
    m_details = std::make_unique<DetailsService>(api, playback, router, !smoke, nullptr,
                                                 &commentAuthors, &window);
    auto &details = *m_details;
    details.setNetworkPreferences(&network);
    details.attachShell(shellController);
    bind(&details, &EchoMusic::DetailsService::notice, &shellController,
         [&](const QString &message, bool warning) {
             shellController.toasts()->show(message, warning ? EchoMusic::ToastService::Warning
                                                             : EchoMusic::ToastService::Success);
         });
    m_albumDetails = std::make_unique<AlbumDetailService>(api, playback, router, !smoke, nullptr,
                                                          &commentAuthors);
    auto &albumDetails = *m_albumDetails;
    playlistDetails.attachCollections(collections);
    albumDetails.attachCollections(collections);
    detailNotice = [&](const QString &message, bool warning) {
        shellController.toasts()->show(message, warning ? EchoMusic::ToastService::Warning
                                                        : EchoMusic::ToastService::Success);
    };
    bind(&collections, &EchoMusic::LibraryCollectionService::notice, &shellController,
         detailNotice);
    bind(&albumDetails, &EchoMusic::AlbumDetailService::notice, &shellController, detailNotice);
    bind(&playlistDetails, &EchoMusic::PlaylistDetailService::notice, &shellController,
         detailNotice);
    bind(details.videoCollection(), &EchoMusic::VideoCollectionService::mutated, &favorites,
         [&](const QString &, bool) { favorites.invalidateVideos(); });
    m_artistDetails = std::make_unique<ArtistDetailService>(api, playback, router, shell, !smoke);
    auto &artistDetails = *m_artistDetails;
    bind(&artistDetails, &ArtistDetailService::notice, &shellController, showNotice);
    artistDetails.attachProfile(profile);
    bind(&profile, &EchoMusic::ProfileService::notice, &shellController, detailNotice);
    // The editor form scrolls; validation and save feedback use the shared toast
    // surface so the result is visible without scrolling to the form's end.
    bind(&profile, &EchoMusic::ProfileService::editNotice, &shellController,
         [&](const QString &message, int tone) {
             shellController.toasts()->show(message, EchoMusic::ToastService::Tone(tone));
         });
    bind(profile.blacklist(), &EchoMusic::ContentBlacklistService::singerWriteFinished,
         &shellController,
         [&](bool success, const QString &message) { detailNotice(message, !success); });
    m_history = std::make_unique<HistoryService>(api, playback, songMetadata, router, !smoke);
    auto &history = *m_history;
    bind(history.statsShare(), &EchoMusic::HistoryStatsShare::notice, &shellController,
         [&](const QString &message, bool failed) {
             shellController.toasts()->show(message, failed ? EchoMusic::ToastService::Danger
                                                            : EchoMusic::ToastService::Success);
         });
    bind(&history, &EchoMusic::HistoryService::notice, &shellController,
         [&](const QString &message) {
             shellController.toasts()->show(message, EchoMusic::ToastService::Success);
         });
    bind(&libraryController, &EchoMusic::LibraryController::playlistRequested, &router,
         [&](const QString &resourceId, quint64, bool) {
             router.navigate("playlist-detail", {}, resourceId);
         });
    m_discovery = std::make_unique<DiscoveryService>(api, playback, account);
    auto &discovery = *m_discovery;
    m_recognitionCapture = std::make_unique<NativeRecognitionCapture>(playback.nativeBridge());
    auto &recognitionCapture = *m_recognitionCapture;
    m_recognition =
        std::make_unique<RecognitionService>(api, playback, recognitionCapture, settings, router);
    m_explore = std::make_unique<ExploreService>(api, playback, router, !smoke);
    bind(m_explore.get(), &ExploreService::notice, &shellController, showNotice);
    m_discoveryRoutes = std::make_unique<DiscoveryRouteCoordinator>(discovery, router, !smoke);
    m_togetherUi = std::make_unique<ListenTogetherUi>(api, playback, userPlaylists, songMetadata,
                                                      router, !smoke);
    bind(playback.listenTogether(), &EchoMusic::ListenTogetherController::notice, &shellController,
         [&shellController](const QString &message, int level) {
             shellController.toasts()->show(message, EchoMusic::ToastService::Tone(level));
         });
    bind(playback.listenTogether(), &EchoMusic::ListenTogetherController::loginRequested,
         &accountController, &EchoMusic::AccountController::openLogin);
    // Routed discovery pages cannot reach ToastService; the service raises a
    // typed notice and the application forwards it to the shared toast surface.
    bind(&discovery, &EchoMusic::DiscoveryService::notice, &shellController,
         [&](const QString &message, int tone) {
             shellController.toasts()->show(message, EchoMusic::ToastService::Tone(tone));
         });
    m_searchRoutes = std::make_unique<SearchRouteCoordinator>(search, router, !smoke);
    m_profileRoutes =
        std::make_unique<ProfileRouteCoordinator>(accountController, profile, router, !smoke);
}
} // namespace EchoMusic
