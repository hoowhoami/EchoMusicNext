#pragma once
#include <QGuiApplication>
#include <QList>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>
#include <utility>
class QQmlApplicationEngine;

namespace EchoMusic
{
class AboutService;
class AccountController;
class AccountService;
class AlbumDetailService;
class ApiClient;
class ApplicationDrain;
class ApplicationShutdown;
class ArtistDetailService;
class CloudService;
class CommentAuthorService;
class DetailsService;
class DeviceRegistrationService;
class DiscoveryRouteCoordinator;
class DiscoveryService;
class ExperimentalSettingsService;
class ExploreService;
class FavoritesService;
class HistoryService;
class LibraryCollectionService;
class LibraryController;
class ListenTogetherUi;
class NativeRecognitionCapture;
class NetworkPreferences;
class PlaybackService;
class PlaylistDetailService;
class PlaylistImportTaskCoordinator;
class PlaylistMutationService;
class PluginManager;
class PluginRuntime;
class ProfileRouteCoordinator;
class ProfileService;
class PurchasedService;
class QrLoginService;
class RecognitionService;
class RelaunchService;
class RouteController;
class SavedAccountModel;
class SearchModel;
class SearchRouteCoordinator;
class SettingsDataService;
class ShareResolveService;
class SharingCommands;
class ShellController;
class ShellSettings;
class ShortcutActions;
class ShortcutService;
class SidebarRouteCoordinator;
class SongMetadataService;
class StartupController;
class StorageService;
class ThemeStore;
class UserPlaylistModel;
class WindowController;
class SingleInstance;
class ApplicationUrlHandler;
class LogService;

int runRecoveryFailure(QGuiApplication &, QSettings &, const QString &, SingleInstance *,
                       const QStringList &);

// Context owns services explicitly in construction order. Phase functions wire
// references; only this owner survives the event loop and drains all workers.
class AppContext final
{
public:
    AppContext(QGuiApplication &application, SingleInstance *instance, QStringList directories,
               bool safeMode, QUrl openUrl, ApplicationUrlHandler *urlHandler, LogService *logging);
    ~AppContext();
    int run();

private:
    int prepare();
    void createShell();
    void createNetworkPlayback();
    void createAccount();
    void createPages();
    void createPlugins();
    void registerRoutes();
    void wireShutdown();
    void wireSmoke();
    void mountUi();
    template <typename... Args> QMetaObject::Connection bind(Args &&...args)
    {
        auto connection = QObject::connect(std::forward<Args>(args)...);
        m_connections.append(connection);
        return connection;
    }
    QGuiApplication &app;
    SingleInstance *m_instance;
    QStringList m_pluginDirectories;
    bool m_pluginSafeMode;
    QUrl m_openUrl;
    ApplicationUrlHandler *m_urlHandler;
    LogService *m_logging;
    bool startupSmoke, smoke, useStartup;
    QTemporaryDir smokeDirectory;
    std::unique_ptr<QSettings> preferences;
    QString applicationData;
    std::unique_ptr<ThemeStore> m_theme;
    std::unique_ptr<ShellSettings> m_shell;
    std::unique_ptr<WindowController> m_window;
    std::unique_ptr<RouteController> m_router;
    std::unique_ptr<ShellController> m_shellController;
    std::unique_ptr<RelaunchService> m_relaunch;
    std::unique_ptr<ApiClient> m_api;
    std::unique_ptr<NetworkPreferences> m_network;
    std::unique_ptr<AboutService> m_about;
    std::unique_ptr<PlaybackService> m_playback;
    std::unique_ptr<AccountService> m_account;
    std::unique_ptr<ProfileService> m_profile;
    std::unique_ptr<UserPlaylistModel> m_userPlaylists;
    std::unique_ptr<PlaylistMutationService> m_playlistMutations;
    std::unique_ptr<LibraryCollectionService> m_collections;
    std::unique_ptr<QrLoginService> m_qrLogin;
    std::unique_ptr<DeviceRegistrationService> m_deviceRegistration;
    std::unique_ptr<StorageService> m_storage;
    std::unique_ptr<SavedAccountModel> m_savedAccounts;
    std::unique_ptr<StartupController> m_startup;
    std::unique_ptr<AccountController> m_accountController;
    std::unique_ptr<LibraryController> m_libraryController;
    std::unique_ptr<SidebarRouteCoordinator> m_sidebarRoutes;
    std::unique_ptr<PlaylistImportTaskCoordinator> m_importTasks;
    std::unique_ptr<SharingCommands> m_shareCommands;
    std::unique_ptr<ShortcutService> m_shortcuts;
    std::unique_ptr<ShortcutActions> m_shortcutActions;
    std::unique_ptr<SearchModel> m_search;
    std::unique_ptr<CommentAuthorService> m_commentAuthors;
    std::unique_ptr<SongMetadataService> m_songMetadata;
    std::unique_ptr<PlaylistDetailService> m_playlistDetails;
    std::unique_ptr<FavoritesService> m_favorites;
    std::unique_ptr<CloudService> m_cloud;
    std::unique_ptr<PurchasedService> m_purchased;
    std::unique_ptr<DetailsService> m_details;
    std::unique_ptr<AlbumDetailService> m_albumDetails;
    std::unique_ptr<ArtistDetailService> m_artistDetails;
    std::unique_ptr<HistoryService> m_history;
    std::unique_ptr<DiscoveryService> m_discovery;
    std::unique_ptr<NativeRecognitionCapture> m_recognitionCapture;
    std::unique_ptr<RecognitionService> m_recognition;
    std::unique_ptr<ExploreService> m_explore;
    std::unique_ptr<DiscoveryRouteCoordinator> m_discoveryRoutes;
    std::unique_ptr<ListenTogetherUi> m_togetherUi;
    std::unique_ptr<SearchRouteCoordinator> m_searchRoutes;
    std::unique_ptr<ProfileRouteCoordinator> m_profileRoutes;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<PluginRuntime> m_plugins;
    std::unique_ptr<PluginManager> m_pluginManager;
    std::unique_ptr<ShareResolveService> m_shareResolver;
    std::unique_ptr<SettingsDataService> m_settingsData;
    std::unique_ptr<ExperimentalSettingsService> m_experimentalSettings;
    std::unique_ptr<ApplicationShutdown> m_shutdown;
    std::unique_ptr<ApplicationDrain> m_drain;
    QTimer smokePageProbe;
    bool activatedDuringStartup = false, urlDeliveryReady = false, quitting = false;
    bool smokeApiReady = false, pluginStartupReady = false;
    bool recoveryHandled = false;
    int exitStatus = 0;
    quint64 visibleUserId = 0;
    QList<QUrl> pendingUrls;
    std::function<void(const QString &, bool)> detailNotice;
    std::function<void(const QUrl &)> openShared;
    std::function<void()> beginShutdown, finishSmoke, deliverUrls, startBusiness;
    QList<QMetaObject::Connection> m_connections;
    QObject m_callbacks;
};
} // namespace EchoMusic
