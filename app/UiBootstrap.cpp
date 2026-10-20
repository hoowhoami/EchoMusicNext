#include "SettingsKeys.h"
#include "AppContext_p.h"
#include "LogService.h"
#include "ApplicationUrlHandler.h"
#include "AboutService.h"
#include "AccountController.h"
#include "AccountService.h"
#include "DetailsService.h"
#include "ExperimentalSettingsService.h"
#include "LibraryController.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "PluginManager.h"
#include "PluginRuntime.h"
#include "QQmlApplicationEngine"
#include "RelaunchService.h"
#include "RouteController.h"
#include "SearchModel.h"
#include "SettingsDataService.h"
#include "ShareResolveService.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "ShortcutService.h"
#include "StartupController.h"
#include "ThemeStore.h"
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
void AppContext::mountUi()
{
    auto &settings = *preferences;
    auto &theme = *m_theme;
    auto &shell = *m_shell;
    auto &window = *m_window;
    auto &router = *m_router;
    auto &shellController = *m_shellController;
    auto &relaunch = *m_relaunch;
    auto &network = *m_network;
    auto &about = *m_about;
    auto &playback = *m_playback;
    auto &startup = *m_startup;
    auto &accountController = *m_accountController;
    auto &libraryController = *m_libraryController;
    auto &shortcuts = *m_shortcuts;
    auto &search = *m_search;
    auto &details = *m_details;
    auto &engine = *m_engine;
    auto &plugins = *m_plugins;
    auto &pluginManager = *m_pluginManager;
    auto &shareResolver = *m_shareResolver;
    auto &settingsData = *m_settingsData;
    auto &experimentalSettings = *m_experimentalSettings;
    shellController.bindBarrageSettings(shell);
    engine.setInitialProperties(
        {{QStringLiteral("startup"), QVariant::fromValue(useStartup ? &startup : nullptr)},
         {QStringLiteral("pluginManager"), QVariant::fromValue(&pluginManager)},
         {QStringLiteral("shortcutService"), QVariant::fromValue(&shortcuts)},
         {QStringLiteral("aboutService"), QVariant::fromValue(&about)},
         {QStringLiteral("experimentalSettingsService"),
          QVariant::fromValue(&experimentalSettings)},
         {QStringLiteral("settingsDataService"), QVariant::fromValue(&settingsData)},
         {QStringLiteral("networkPreferences"), QVariant::fromValue(&network)},
         {QStringLiteral("logService"), QVariant::fromValue(m_logging)},
         {QStringLiteral("relaunchService"), QVariant::fromValue(&relaunch)},
         {QStringLiteral("playbackDiscussion"), QVariant::fromValue(details.playbackDiscussion())},
         {QStringLiteral("playbackService"), QVariant::fromValue(&playback)},
         {QStringLiteral("libraryController"), QVariant::fromValue(&libraryController)},
         {QStringLiteral("accountController"), QVariant::fromValue(&accountController)},
         {QStringLiteral("shellController"), QVariant::fromValue(&shellController)},
         {QStringLiteral("searchService"), QVariant::fromValue(&search)},
         {QStringLiteral("router"), QVariant::fromValue(&router)},
         {QStringLiteral("shellSettings"), QVariant::fromValue(&shell)},
         {QStringLiteral("windowService"), QVariant::fromValue(&window)}});
    deliverUrls = [&] {
        urlDeliveryReady = true;
        shareResolver.startClipboardMonitoring();
        if (m_urlHandler)
            m_urlHandler->beginDelivery();
        if (!m_openUrl.isEmpty())
            openShared(m_openUrl);
        for (const auto &url : std::as_const(pendingUrls))
            openShared(url);
        pendingUrls.clear();
    };
    startBusiness = [&] {
        plugins.start(m_pluginDirectories);
        pluginStartupReady = true;
        deliverUrls();
        const auto maintenanceError =
            settings.value(EchoMusic::SettingsKeys::Maintenance::LastError).toString();
        if (!maintenanceError.isEmpty()) {
            shellController.toasts()->show(maintenanceError, EchoMusic::ToastService::Warning);
            settings.remove(EchoMusic::SettingsKeys::Maintenance::LastError);
        }
        finishSmoke();
        if (!smoke && about.autoCheck())
            QTimer::singleShot(5000, &about, [&] { about.check(true); });
    };
    bind(&startup, &EchoMusic::StartupController::enteredMain, &m_callbacks, [&] {
        if (quitting)
            return;
        router.navigateHome();
        search.initialize();
        playback.setStartupHeld(false);
        startBusiness();
    });
    engine.loadFromModule(QStringLiteral("EchoMusic.App"), QStringLiteral("Main"));
    if (!quitting && !useStartup)
        startBusiness();
    if (!engine.rootObjects().isEmpty()) {
        window.attach(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
        shortcuts.attach(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
        // Queue real initialization after the startup QML exists. Do not wait
        // for frameSwapped: an occluded/tray launch may never render a frame.
        if (useStartup)
            QTimer::singleShot(0, &startup, &EchoMusic::StartupController::start);
        window.setAppearance(theme.windowFrosted(), theme.dark(), theme.transparency());
        QTimer::singleShot(0, &window, [&] {
            if (!activatedDuringStartup)
                window.applyStartupVisibility();
        });
    }
}
} // namespace EchoMusic
