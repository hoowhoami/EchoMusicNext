#include "AppContext_p.h"
#include "QQmlApplicationEngine"
#include "RelaunchService.h"
#include "ShellSettings.h"
#include "SingleInstance.h"
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
int runRecoveryFailure(QGuiApplication &app, EchoMusic::SettingsStore &settings,
                       const QString &error, EchoMusic::SingleInstance *instance,
                       const QStringList &arguments)
{
    // Do not construct account stores, transport, plugins or audio against an
    // unfinished restore. The retry uses the same fully drained relaunch helper.
    EchoMusic::ThemeStore theme(settings);
    EchoMusic::ShellSettings shell(settings);
    EchoMusic::WindowController window(settings, false);
    theme.setMotionEnabled(shell.motionEnabled());
    QObject::connect(&theme, &EchoMusic::ThemeStore::changed, &window, [&] {
        window.setAppearance(theme.windowFrosted(), theme.dark(), theme.transparency());
    });
    EchoMusic::StartupController startup(error);
    EchoMusic::RelaunchService relaunch(instance ? instance->lockFilePath() : QString(), arguments,
                                        false);
    QQmlApplicationEngine engine;
    theme.attachToEngine(engine);
    const auto quit = [&] {
        startup.shutdown();
        window.shutdown();
        app.quit();
    };
    QObject::connect(&startup, &EchoMusic::StartupController::quitRequested, &engine, quit);
    QObject::connect(&startup, &EchoMusic::StartupController::restartApplicationRequested,
                     &relaunch, &EchoMusic::RelaunchService::restartForStartupRecovery);
    QObject::connect(&relaunch, &EchoMusic::RelaunchService::notice, &startup,
                     &EchoMusic::StartupController::reportRestartFailure);
    QObject::connect(&relaunch, &EchoMusic::RelaunchService::readyToQuit, &engine, quit);
    QObject::connect(&window, &EchoMusic::WindowController::quitRequested, &engine, quit);
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &engine, quit);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &engine, quit);
    engine.setInitialProperties({{"startup", QVariant::fromValue(&startup)},
                                 {"windowService", QVariant::fromValue(&window)},
                                 {"shellSettings", QVariant::fromValue(&shell)}});
    engine.loadFromModule("EchoMusic.App", "StartupWindow");
    if (engine.rootObjects().isEmpty())
        return 1;
    window.attach(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    window.setAppearance(theme.windowFrosted(), theme.dark(), theme.transparency());
    if (instance)
        QObject::connect(instance, &EchoMusic::SingleInstance::activationRequested, &window,
                         &EchoMusic::WindowController::showWindow);
    startup.start();
    const int result = app.exec();
    qDeleteAll(engine.rootObjects());
    return result;
}
} // namespace EchoMusic
