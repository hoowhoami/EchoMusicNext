#include "AppContext_p.h"
#include "SingleInstance.h"
#include "ApplicationUrlHandler.h"
#include "AboutService.h"
#include "AccountService.h"
#include "ApiClient.h"
#include "ExperimentalSettingsService.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "PluginManager.h"
#include "PluginRuntime.h"
#include "AccountController.h"
#include "ShellSettings.h"
#include "QQmlApplicationEngine"
#include "RelaunchService.h"
#include "RouteController.h"
#include "SavedAccountModel.h"
#include "SettingsDataService.h"
#include "ShareResolveService.h"
#include "ShareTarget.h"
#include "SharingCommands.h"
#include "ShellController.h"
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
void AppContext::createPlugins()
{
    auto &settings = *preferences;
    auto &theme = *m_theme;
    auto &window = *m_window;
    auto &router = *m_router;
    auto &shellController = *m_shellController;
    auto &relaunch = *m_relaunch;
    auto &api = *m_api;
    auto &network = *m_network;
    auto &about = *m_about;
    auto &playback = *m_playback;
    auto &account = *m_account;
    auto &savedAccounts = *m_savedAccounts;
    auto &shareCommands = *m_shareCommands;
    m_engine = std::make_unique<QQmlApplicationEngine>();
    auto &engine = *m_engine;
    theme.attachToEngine(engine);
    const auto pluginRoot =
        (smoke ? smokeDirectory.path()
               : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) +
        QStringLiteral("/plugins");
    m_plugins = std::make_unique<PluginRuntime>(
        engine, settings,
        PluginHost{playback, theme, shellController, router, account, api, window.floating(),
                   nullptr, m_shell->sidebarLayout(), m_shortcuts.get(),
                   m_accountController->verification()},
        pluginRoot, m_pluginSafeMode);
    auto &plugins = *m_plugins;
    bind(theme.extensions(), &AppearanceCatalog::contributionFailed, &shellController,
         [&theme, &shellController](const QString &key, const QString &message) {
             auto *entry = theme.extensions()->find(key);
             const auto title = entry ? entry->title : QObject::tr("插件外观");
             qWarning().noquote() << "Appearance" << key << message;
             shellController.toasts()->show(QObject::tr("%1 加载失败").arg(title),
                                            ToastService::Warning, 6000);
         });
    m_pluginManager = std::make_unique<PluginManager>(plugins, settings);
    auto &pluginManager = *m_pluginManager;
    pluginManager.attachSharing(shareCommands);
    m_shareResolver = std::make_unique<ShareResolveService>(api, shareCommands, router,
                                                            shellController, window, !smoke);
    auto &shareResolver = *m_shareResolver;
    shareResolver.setPluginOpener(
        [&](const EchoMusic::ShareTarget &target) { return pluginManager.openShare(target); });
    m_settingsData = std::make_unique<SettingsDataService>(settings, plugins, relaunch, network,
                                                           applicationData, about.version());
    auto &settingsData = *m_settingsData;
    m_experimentalSettings = std::make_unique<ExperimentalSettingsService>(
        api, account, savedAccounts, settings, relaunch);
    auto &experimentalSettings = *m_experimentalSettings;
    experimentalSettings.setMaintenanceGuard([&] { return !settingsData.busy(); });
    bind(&experimentalSettings, &EchoMusic::ExperimentalSettingsService::notice, &shellController,
         [&](const QString &message, bool failure) {
             shellController.toasts()->show(message, failure ? EchoMusic::ToastService::Warning
                                                             : EchoMusic::ToastService::Success);
         });
    if (m_instance)
        settingsData.setInstanceLockFilePath(m_instance->lockFilePath());
    settingsData.setMaintenanceGuard([&] {
        return !theme.pendingImageImports() &&
               !playback.audioControls()->importingImpulseResponses() &&
               !playback.audioControls()->providers()->busy() &&
               !playback.effectCatalog()->downloading() && !pluginManager.busy() &&
               !plugins.loading() && !playback.cast()->busy();
    });
    bind(&settingsData, &EchoMusic::SettingsDataService::notice, &shellController,
         [&](const QString &message, bool failure) {
             shellController.toasts()->show(message, failure ? EchoMusic::ToastService::Warning
                                                             : EchoMusic::ToastService::Success);
         });
    pluginManager.setNetworkPreferences(&network);
    bind(&pluginManager, &PluginManager::notice, &shellController,
         [&](const QString &message, bool failure, bool informational) {
             shellController.toasts()->show(message, failure         ? ToastService::Warning
                                                     : informational ? ToastService::Info
                                                                     : ToastService::Success);
         });
    bind(&pluginManager, &PluginManager::shareOpened, &router,
         [&] { router.navigate("plugin-share-resolve"); });
    bind(&pluginManager, &PluginManager::sharedInstalled, &router, [&] {
        if (router.current() && router.current()->name() == "plugin-share-resolve") {
            pluginManager.setSearch(pluginManager.shareRecord().value("id").toString());
            pluginManager.setSourceFilter(pluginManager.shareRecord().value("sourceId").toString());
            router.replace("plugin-management", {{"view", "marketplace"}});
        }
    });
    openShared = [&](const QUrl &url) {
        if (!urlDeliveryReady) {
            if (pendingUrls.size() < 16)
                pendingUrls.append(url);
            return;
        }
        window.showWindow();
        if (url.isLocalFile() && url.path().endsWith(".echomod", Qt::CaseInsensitive)) {
            router.navigate("plugin-management");
            if (!pluginManager.installLocalBatch({url}))
                shellController.toasts()->show(QObject::tr("当前正在处理插件，请稍后安装"),
                                               ToastService::Warning);
            return;
        }
        shareResolver.openUrl(url);
    };
    if (m_instance)
        bind(m_instance, &SingleInstance::urlRequested, &pluginManager, openShared);
    if (m_urlHandler)
        bind(m_urlHandler, &ApplicationUrlHandler::urlRequested, &pluginManager, openShared);
    bind(&plugins, &PluginRuntime::pluginFailed, &shellController,
         [&](const QString &id, const QString &message) {
             qWarning().noquote() << "Plugin" << id << message;
             shellController.toasts()->show(message, ToastService::Danger, 6000,
                                            ToastService::Standard,
                                            QObject::tr("插件异常 · %1").arg(id));
         });
    bind(&plugins, &PluginRuntime::notice, &shellController,
         [&shellController](const QString &message, bool warning) {
             shellController.toasts()->show(message, warning ? ToastService::Warning
                                                             : ToastService::Success);
         });
    visibleUserId = account.userId();
    bind(&account, &EchoMusic::AccountService::changed, &router, [&] {
        if (visibleUserId == account.userId())
            return;
        visibleUserId = account.userId();
        // Adoptions invalidate through sessionAdopted, including same-ID login.
        // This observer handles logout without invalidating a new login twice.
        if (!visibleUserId)
            router.invalidateAccount();
    });
    bind(&account, &EchoMusic::AccountService::sessionAdopted, &router, [&] {
        // SQL startup restoration is not an explicit account switch. Recreate
        // account-owned pages without replacing the landing route (or a newer
        // user/deep-link navigation made while storage was loading).
        if (savedAccounts.restoringStartupAccount())
            router.invalidateAccountPreservingRoute();
        else
            router.invalidateAccount();
    });
}
} // namespace EchoMusic
