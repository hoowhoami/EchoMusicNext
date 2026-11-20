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
#include "MainWindowHost.h"
#include <QQmlComponent>
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
    const QVariantMap mainProperties{
        {QStringLiteral("startup"), QVariant::fromValue(useStartup ? &startup : nullptr)},
        {QStringLiteral("pluginManager"), QVariant::fromValue(&pluginManager)},
        {QStringLiteral("shortcutService"), QVariant::fromValue(&shortcuts)},
        {QStringLiteral("aboutService"), QVariant::fromValue(&about)},
        {QStringLiteral("experimentalSettingsService"), QVariant::fromValue(&experimentalSettings)},
        {QStringLiteral("settingsDataService"), QVariant::fromValue(&settingsData)},
        {QStringLiteral("networkPreferences"), QVariant::fromValue(&network)},
        {QStringLiteral("logService"), QVariant::fromValue(m_logging)},
        {QStringLiteral("relaunchService"), QVariant::fromValue(&relaunch)},
        {QStringLiteral("playbackDiscussion"), QVariant::fromValue(details.playbackDiscussion())},
        {QStringLiteral("songDetails"), QVariant::fromValue(&details)},
        {QStringLiteral("playbackService"), QVariant::fromValue(&playback)},
        {QStringLiteral("libraryController"), QVariant::fromValue(&libraryController)},
        {QStringLiteral("accountController"), QVariant::fromValue(&accountController)},
        {QStringLiteral("shellController"), QVariant::fromValue(&shellController)},
        {QStringLiteral("searchService"), QVariant::fromValue(&search)},
        {QStringLiteral("router"), QVariant::fromValue(&router)},
        {QStringLiteral("shellSettings"), QVariant::fromValue(&shell)},
        {QStringLiteral("windowService"), QVariant::fromValue(&window)},
        {QStringLiteral("visible"), false}};
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
        if (pluginStartupReady || quitting)
            return;
        plugins.start(m_pluginDirectories);
        if (!smoke)
            m_pluginManager->startStartupUpdateCheck();
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
    // Store the initial appearance before the hidden Windows HWND is attached;
    // attachment applies it before publishing first-presentation readiness.
    window.setAppearance(theme.windowFrosted(), theme.dark(), theme.transparency());
    window.initializeSystemIntegration();
    QQmlComponent auxiliary(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/AuxiliaryWindows.qml"));
    m_auxiliaryWindows.reset(auxiliary.createWithInitialProperties(
        {{"startup", QVariant::fromValue(useStartup ? &startup : nullptr)},
         {"windowService", QVariant::fromValue(&window)},
         {"shellSettings", QVariant::fromValue(&shell)},
         {"shellController", QVariant::fromValue(&shellController)},
         {"playbackService", QVariant::fromValue(&playback)}}));
    if (!m_auxiliaryWindows) {
        qCritical().noquote() << auxiliary.errorString();
        exitStatus = 1;
        beginShutdown();
        return;
    }
    m_mainUi = std::make_unique<MainWindowHost>(
        engine, window, shortcuts,
        [this, mainProperties]() -> QQuickWindow * {
            QQmlComponent component(m_engine.get(), QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
            auto *object = component.createWithInitialProperties(mainProperties);
            auto *surface = qobject_cast<QQuickWindow *>(object);
            if (!surface) {
                qWarning().noquote() << "Main window creation failed:" << component.errorString();
                delete object;
            }
            return surface;
        },
        &router);
    bind(&router, &RouteController::settingsRequested, &window, [&](const QString &section) {
        if (!window.mainWindow())
            window.requestSettings(section);
    });
    bind(m_mainUi.get(), &MainWindowHost::creationFailed, &m_callbacks, [&] {
        // A failed rebuild must leave playback, plugins and recovery reachable.
        shellController.toasts()->show(QObject::tr("主窗口打开失败，请通过托盘重试"),
                                       ToastService::Warning);
        if (!window.trayAvailable()) {
            exitStatus = 1;
            beginShutdown();
        }
    });
    // Background initialization does not require an invisible main window.
    // First-home consent remains a real visible interaction.
    if (!activatedDuringStartup && window.startMinimized() && window.trayAvailable() &&
        shell.userAgreementAccepted())
        window.applyStartupVisibility();
    else
        m_mainUi->show();
    if (useStartup) {
        bind(&startup, &StartupController::changed, &m_callbacks, [&] {
            if (startup.failed() && !quitting)
                window.showWindow();
        });
        QTimer::singleShot(0, &startup, &StartupController::start);
    } else if (!quitting) {
        startBusiness();
    }
}
} // namespace EchoMusic
