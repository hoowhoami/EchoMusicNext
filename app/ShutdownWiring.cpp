#include "SettingsKeys.h"
#include "AppContext_p.h"
#include "AboutService.h"
#include "AccountController.h"
#include "AlbumDetailService.h"
#include "ApiClient.h"
#include "ApplicationDrain.h"
#include "ApplicationShutdown.h"
#include "ArtistDetailService.h"
#include "CloudService.h"
#include "CommentAuthorService.h"
#include "DetailsService.h"
#include "DeviceRegistrationService.h"
#include "DiscoveryService.h"
#include "ExploreService.h"
#include "FavoritesService.h"
#include "HistoryService.h"
#include "PlaybackService.h"
#include "PlaylistDetailService.h"
#include "PluginManager.h"
#include "PluginRuntime.h"
#include "ProfileService.h"
#include "QQmlApplicationEngine"
#include "RecognitionService.h"
#include "RelaunchService.h"
#include "RouteController.h"
#include "SavedAccountModel.h"
#include "SearchModel.h"
#include "SettingsDataService.h"
#include "ShareResolveService.h"
#include "ShellController.h"
#include "ShortcutService.h"
#include "SongMetadataService.h"
#include "StartupController.h"
#include "StorageService.h"
#include "ThemeStore.h"
#include "ToastService.h"
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
void AppContext::wireShutdown()
{
    auto &settings = *preferences;
    auto &theme = *m_theme;
    auto &window = *m_window;
    auto &router = *m_router;
    auto &shellController = *m_shellController;
    auto &relaunch = *m_relaunch;
    auto &api = *m_api;
    auto &about = *m_about;
    auto &playback = *m_playback;
    auto &profile = *m_profile;
    auto &deviceRegistration = *m_deviceRegistration;
    auto &storage = *m_storage;
    auto &savedAccounts = *m_savedAccounts;
    auto &startup = *m_startup;
    auto &accountController = *m_accountController;
    auto &shortcuts = *m_shortcuts;
    auto &search = *m_search;
    auto &commentAuthors = *m_commentAuthors;
    auto &songMetadata = *m_songMetadata;
    auto &playlistDetails = *m_playlistDetails;
    auto &favorites = *m_favorites;
    auto &cloud = *m_cloud;
    auto &details = *m_details;
    auto &albumDetails = *m_albumDetails;
    auto &artistDetails = *m_artistDetails;
    auto &history = *m_history;
    auto &discovery = *m_discovery;
    auto &recognition = *m_recognition;
    auto &explore = *m_explore;
    auto &engine = *m_engine;
    auto &plugins = *m_plugins;
    auto &pluginManager = *m_pluginManager;
    auto &shareResolver = *m_shareResolver;
    auto &settingsData = *m_settingsData;
    beginShutdown = [this] { m_drain->request(); };
    m_shutdown = std::make_unique<ApplicationShutdown>(app, beginShutdown);
    auto &shutdown = *m_shutdown;
    m_drain = std::make_unique<ApplicationDrain>(ApplicationDrain::Actions{
        [&] {
            quitting = true;
            qInfo("Application shutdown started");
            startup.shutdown();
            shareResolver.shutdown();
            shortcuts.shutdown();
            about.shutdown();
            settingsData.shutdown();
            pluginManager.shutdown();
            plugins.shutdown();
        },
        [&] {
            theme.endBackgroundPreview();
            theme.cancelImageWorkForShutdown();
            cloud.shutdown();
            router.shutdown();
            shellController.shutdown();
            window.shutdown();
            settings.sync();
            recognition.shutdown();
            playback.shutdown();
            discovery.shutdown();
            explore.shutdown();
            playlistDetails.shutdown();
            favorites.shutdown();
            commentAuthors.shutdown();
            details.shutdown();
            albumDetails.shutdown();
            artistDetails.shutdown();
            history.shutdown();
            songMetadata.shutdown();
            search.shutdown();
            accountController.shutdown();
            profile.shutdown();
            savedAccounts.shutdown();
            deviceRegistration.shutdown();
            // Listening events must finish before their immutable API runtime closes.
            // The reporter bounds shutdown and discards uncertain writes without replay.
            // Plugins have drained before their business dependencies close.
            // The account catalog drains local-store writes before closing SQL.
        },
        [&] {
            settingsData.finishShutdown([&](const QString &error) {
                if (!error.isEmpty()) {
                    qCritical().noquote() << "Settings maintenance:" << error;
                    settings.setValue(EchoMusic::SettingsKeys::Maintenance::LastError, error);
                    settings.sync();
                    exitStatus = 1;
                }
                qInfo("Application shutdown complete");
                shutdown.complete();
            });
        }});
    bind(&startup, &StartupController::quitRequested, &m_callbacks, beginShutdown);
    bind(&relaunch, &RelaunchService::readyToQuit, &m_callbacks, beginShutdown);
    bind(&about, &AboutService::readyToQuit, &m_callbacks, beginShutdown);
    bind(&playback, &PlaybackService::listeningReportsDrained, &api, &ApiClient::shutdown);
    using Drain = ApplicationDrain::Service;
    bind(&songMetadata, &SongMetadataService::closed, &m_callbacks,
         [this] { m_drain->completed(Drain::Metadata); });
    bind(&plugins, &PluginRuntime::closed, &m_callbacks,
         [this] { m_drain->completed(Drain::Plugins); });
    QObject::disconnect(&engine, &QQmlEngine::quit, &app, nullptr);
    bind(&engine, &QQmlEngine::quit, &m_callbacks, beginShutdown);
    bind(&playback, &PlaybackService::closed, &m_callbacks,
         [this] { m_drain->completed(Drain::Playback); });
    bind(&deviceRegistration, &DeviceRegistrationService::closed, &m_callbacks,
         [this] { m_drain->completed(Drain::DeviceRegistration); });
    bind(&api, &ApiClient::closed, &m_callbacks, [this] { m_drain->completed(Drain::Api); });
    bind(&storage, &StorageService::closed, &m_callbacks,
         [this] { m_drain->completed(Drain::Storage); });
    bind(&savedAccounts, &SavedAccountModel::closed, &storage, [&] {
        m_drain->completed(Drain::Accounts);
        storage.shutdown();
    });
    bind(&app, &QGuiApplication::lastWindowClosed, &m_callbacks, beginShutdown);
    bind(&window, &EchoMusic::WindowController::quitRequested, &m_callbacks, beginShutdown);
    bind(&playback, &EchoMusic::PlaybackService::sleepActionRequested, &m_callbacks,
         [&](const QString &action) {
             if (action == "quit") {
                 beginShutdown();
                 return;
             }
             if (action != "shutdown")
                 return;
             // Fixed program/arguments match EchoMusic's platform request. No
             // shell, privilege escalation or forced application termination.
             auto *process = new QProcess(&app);
             auto *timeout = new QTimer(process);
             timeout->setSingleShot(true);
             timeout->setInterval(30000);
             bind(timeout, &QTimer::timeout, process, [process] { process->kill(); });
             auto done = std::make_shared<bool>(false);
             const auto finish = [&, process, timeout, done](bool success) {
                 if (std::exchange(*done, true))
                     return;
                 timeout->stop();
                 playback.finishSleepAction(
                     success ? QString() : QObject::tr("系统未完成关机请求，请检查系统权限"));
                 process->deleteLater();
             };
             bind(process, &QProcess::errorOccurred, &m_callbacks,
                  [finish](QProcess::ProcessError) { finish(false); });
             bind(process, &QProcess::finished, &m_callbacks,
                  [finish](int code, QProcess::ExitStatus status) {
                      finish(code == 0 && status == QProcess::NormalExit);
                  });
#ifdef Q_OS_MACOS
             process->start("/usr/bin/osascript",
                            {"-e", "tell application \"System Events\" to shut down"});
#elif defined(Q_OS_WIN)
            process->start("shutdown.exe", {"/s", "/t", "0"});
#else
            process->start("/usr/bin/systemctl", {"poweroff"});
#endif
             timeout->start();
         });
    bind(&engine, &QQmlApplicationEngine::objectCreationFailed, &m_callbacks, [&] {
        exitStatus = 1;
        beginShutdown();
    });
    bind(&storage, &EchoMusic::StorageService::failed, &m_callbacks, [&](const QString &message) {
        qCritical().noquote() << message;
        if (!useStartup) {
            exitStatus = 1;
            beginShutdown();
        }
    });
}
} // namespace EchoMusic
