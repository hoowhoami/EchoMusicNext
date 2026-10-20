#include "AppContext_p.h"
#include "SingleInstance.h"
#include "LogService.h"
#include "RelaunchService.h"
#include "RouteController.h"
#include "ShellController.h"
#include "ShellSettings.h"
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
void AppContext::createShell()
{
    auto &settings = *preferences;
    m_theme = std::make_unique<ThemeStore>(
        settings, nullptr,
        smoke ? smokeDirectory.path() + QStringLiteral("/theme-images") : QString());
    auto &theme = *m_theme;
    theme.pruneImageAssets();
    m_shell = std::make_unique<ShellSettings>(settings);
    auto &shell = *m_shell;
    theme.setMotionEnabled(shell.motionEnabled());
    bind(&shell, &EchoMusic::ShellSettings::changed, &theme,
         [&] { theme.setMotionEnabled(shell.motionEnabled()); });
    m_window = std::make_unique<WindowController>(settings, !smoke);
    auto &window = *m_window;
    bind(&theme, &EchoMusic::ThemeStore::changed, &window,
         [&] { window.setAppearance(theme.windowFrosted(), theme.dark(), theme.transparency()); });
    if (m_instance)
        bind(m_instance, &SingleInstance::activationRequested, &window, [&] {
            activatedDuringStartup = true;
            window.showWindow();
            qInfo("Instance activation: main window restored");
        });
    m_router = std::make_unique<RouteController>();
    auto &router = *m_router;
    router.setHomeLayout(shell.sidebarLayout());
    m_shellController = std::make_unique<ShellController>(settings, router);
    auto &shellController = *m_shellController;
    // Imports/share URLs are one-shot actions and must not execute again after
    // relaunch. Preserve explicit runtime diagnostics and trusted-plugin mode.
    m_relaunch = std::make_unique<RelaunchService>(
        m_instance ? m_instance->lockFilePath() : QString(),
        RelaunchService::persistentArguments(app.arguments()), ECHO_DISTRIBUTION_BUILD && !smoke);
    auto &relaunch = *m_relaunch;
    bind(&relaunch, &EchoMusic::RelaunchService::notice, &shellController,
         [&](const QString &message) { shellController.toasts()->show(message); });
    if (m_logging)
        bind(m_logging, &LogService::notice, &shellController,
             [&](const QString &message) { shellController.toasts()->show(message); });
    shellController.attachTheme(theme);
    router.setCachePolicy(shell.keepAliveEnabled(), shell.keepAliveMax());
    bind(&shell, &EchoMusic::ShellSettings::changed, &router,
         [&] { router.setCachePolicy(shell.keepAliveEnabled(), shell.keepAliveMax()); });
}
} // namespace EchoMusic
