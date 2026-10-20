#include "AppContext_p.h"
#include "AboutService.h"
#include "AccountController.h"
#include "AccountService.h"
#include "AlbumDetailService.h"
#include "ApiClient.h"
#include "ApplicationDrain.h"
#include "ApplicationShutdown.h"
#include "ArtistDetailService.h"
#include "CloudService.h"
#include "CommentAuthorService.h"
#include "DetailsService.h"
#include "DeviceRegistrationService.h"
#include "DiscoveryRouteCoordinator.h"
#include "DiscoveryService.h"
#include "ExperimentalSettingsService.h"
#include "ExploreService.h"
#include "FavoritesService.h"
#include "HistoryService.h"
#include "LibraryCollectionService.h"
#include "LibraryController.h"
#include "ListenTogetherUi.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "PlaylistDetailService.h"
#include "PlaylistImportTaskCoordinator.h"
#include "PlaylistMutationService.h"
#include "PluginManager.h"
#include "PluginRuntime.h"
#include "ProfileRouteCoordinator.h"
#include "ProfileService.h"
#include "PurchasedService.h"
#include "QQmlApplicationEngine"
#include "QrLoginService.h"
#include "RecognitionCapture.h"
#include "RecognitionService.h"
#include "RelaunchService.h"
#include "RouteController.h"
#include "SavedAccountModel.h"
#include "SearchModel.h"
#include "SearchRouteCoordinator.h"
#include "SettingsDataService.h"
#include "ShareResolveService.h"
#include "SharingCommands.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "ShortcutActions.h"
#include "ShortcutService.h"
#include "SidebarRouteCoordinator.h"
#include "SongMetadataService.h"
#include "StartupController.h"
#include "StorageService.h"
#include "ThemeStore.h"
#include "UserPlaylistModel.h"
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
int AppContext::prepare()
{

    preferences = smoke ? std::make_unique<QSettings>(smokeDirectory.path() +
                                                          QStringLiteral("/preferences.ini"),
                                                      QSettings::IniFormat)
                        : std::make_unique<QSettings>();
    auto &settings = *preferences;
    applicationData = smoke
                          ? smokeDirectory.path()
                          : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto restoreError = EchoMusic::SettingsDataService::recover(applicationData, settings);
    if (!restoreError.isEmpty()) {
        recoveryHandled = true;
        qCritical().noquote() << "Cannot recover settings restore:" << restoreError;
        return runRecoveryFailure(app, settings, restoreError, m_instance,
                                  RelaunchService::persistentArguments(app.arguments()));
    }
    return 0;
}
} // namespace EchoMusic

namespace EchoMusic
{
AppContext::AppContext(QGuiApplication &application, SingleInstance *instance,
                       QStringList directories, bool safeMode, QUrl openUrl,
                       ApplicationUrlHandler *urlHandler, LogService *logging)
    : app(application), m_instance(instance), m_pluginDirectories(std::move(directories)),
      m_pluginSafeMode(safeMode), m_openUrl(std::move(openUrl)), m_urlHandler(urlHandler),
      m_logging(logging), startupSmoke(app.arguments().contains("--startup-smoke-test")),
      smoke(startupSmoke || app.arguments().contains("--smoke-test")),
      useStartup(!smoke || startupSmoke)
{
}
AppContext::~AppContext()
{
    for (const auto &connection : m_connections)
        QObject::disconnect(connection);
    // QML bindings must release references before their injected service owners.
    if (m_engine)
        qDeleteAll(m_engine->rootObjects());
}
int AppContext::run()
{
    const int preparation = prepare();
    if (preparation != 0 || recoveryHandled)
        return preparation;
    qInfo("Bootstrap phase: createShell");
    createShell();
    qInfo("Bootstrap phase: createNetworkPlayback");
    createNetworkPlayback();
    qInfo("Bootstrap phase: createAccount");
    createAccount();
    qInfo("Bootstrap phase: createPages");
    createPages();
    qInfo("Bootstrap phase: createPlugins");
    createPlugins();
    qInfo("Bootstrap phase: registerRoutes");
    registerRoutes();
    qInfo("Bootstrap phase: wireShutdown");
    wireShutdown();
    qInfo("Bootstrap phase: wireSmoke");
    wireSmoke();
    qInfo("Bootstrap phase: mountUi");
    mountUi();
    const int result = app.exec();
    return exitStatus ? exitStatus : result;
}
} // namespace EchoMusic
