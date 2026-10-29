#include "WindowController.h"
#include "NativeWindow.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace EchoMusic;
QVariantMap nativeFloatingAppearance(QWindow *);
QVariantMap nativeMacApplicationPresentation();
QVariantMap nativeMacComposition(QWindow *, qint64);

namespace
{
struct Session
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings{directory.filePath("settings.ini")};
    QQuickWindow main, desktop, mini;
    WindowController windows{settings, true};
    Session()
    {
        main.setTitle("EchoMusicNext native overlay verification");
        windows.attach(&main);
        main.show();
        desktop.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        desktop.setColor(QColor("#9931cfa1"));
        mini.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        mini.setColor(QColor("#997e6bea"));
    }
    ~Session()
    {
        windows.setHideDockInBackground(false);
        windows.floating()->attachDesktop(nullptr);
        windows.floating()->attachMini(nullptr);
        windows.shutdown();
    }
    void open()
    {
        auto *floating = windows.floating();
        floating->setDesktopEnabled(true);
        floating->attachDesktop(&desktop);
        floating->toggleMini();
        floating->attachMini(&mini);
    }
};
} // namespace

class MacFloatingWindowTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Requires a real Cocoa session");
    }
    void dockPolicySurvivesPresentationChanges()
    {
        Session session;
        QVERIFY(Test::prepareNativeInput(&session.main));
        QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 0);
        bool policyChanged = false;
        QTimer sampler;
        connect(&sampler, &QTimer::timeout, this, [&] {
            policyChanged |=
                nativeMacApplicationPresentation().value("activationPolicy").toInt() != 0;
        });
        sampler.start(5);
        session.open();
        auto *floating = session.windows.floating();
        for (int cycle = 0; cycle < 4; ++cycle) {
            for (bool top : {false, true}) {
                floating->desktop()->setAlwaysOnTop(top);
                floating->mini()->setAlwaysOnTop(top);
                for (auto *window : {&session.desktop, &session.mini}) {
                    const auto state = nativeFloatingAppearance(window);
                    QCOMPARE(state.value("allSpaces").toBool(), top);
                    QCOMPARE(state.value("fullscreenAuxiliary").toBool(), top);
                    QCOMPARE(state.value("allApplications").toBool(), top);
                    QCOMPARE(state.value("fullscreenOverlayLevel").toBool(), top);
                    QVERIFY(state.value("panel").toBool());
                    QCOMPARE(state.value("nonactivating").toBool(), top);
                    QVERIFY(!state.value("hidesOnDeactivate").toBool());
                }
                floating->setLocked(true);
                floating->setLocked(false);
                QTest::qWait(400); // Beyond EchoMusic's 120/320ms Dock recovery timers.
                QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 0);
            }
        }
        // Mini must remain a regular application even with the user's explicit
        // background-only Dock preference enabled. No forced Dock-show timer.
        session.windows.setHideDockInBackground(true);
        QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 0);
        floating->showMain();
        floating->attachMini(nullptr);
        session.mini.hide();
        floating->setDesktopEnabled(false);
        QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 0);
        QVERIFY(!policyChanged);
        sampler.stop();
        // Explicit background-only hiding is intentional. Pinning a desktop
        // overlay must not undo it with an unconditional Dock restore.
        QVERIFY(session.windows.trayAvailable());
        session.windows.close();
        QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 1);
        floating->setDesktopEnabled(true);
        floating->desktop()->setAlwaysOnTop(false);
        floating->desktop()->setAlwaysOnTop(true);
        QTest::qWait(400);
        QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 1);
        session.windows.showWindow();
        QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 0);
        qInfo() << "Dock policy stayed Regular across desktop/Mini/lock/pin changes";
    }
    void pinnedWindowsJoinAnotherApplicationsFullscreen()
    {
        if (!qEnvironmentVariableIntValue("ECHO_NATIVE_MAC_FULLSCREEN"))
            QSKIP("Opt in to the temporary fullscreen Space with ECHO_NATIVE_MAC_FULLSCREEN=1");
        Session session;
        QVERIFY(Test::prepareNativeInput(&session.main));
        session.open();
        QTemporaryDir hostDirectory;
        QProcess host;
        const auto hostCleanup = qScopeGuard([&] {
            QFile stop(hostDirectory.filePath("stop"));
            if (stop.open(QIODevice::WriteOnly))
                stop.close();
            if (!host.waitForFinished(5000)) {
                host.terminate();
                if (!host.waitForFinished(2000)) {
                    host.kill();
                    host.waitForFinished(2000);
                }
            }
        });
        const auto binary =
            QCoreApplication::applicationDirPath() +
            "/echo-macos-fullscreen-host.app/Contents/MacOS/echo-macos-fullscreen-host";
        QVERIFY(QFile::exists(binary));
        host.start(binary, {hostDirectory.path()});
        QVERIFY(host.waitForStarted());
        QJsonObject state;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     QFile file(hostDirectory.filePath("state.json"));
                                     if (!file.open(QIODevice::ReadOnly))
                                         return false;
                                     state = QJsonDocument::fromJson(file.readAll()).object();
                                     return state.value("fullscreen").toBool();
                                 })(),
                                 15000);
        const auto hostWindow = state.value("window").toInteger();
        QVERIFY(state.value("pid").toInteger() != QCoreApplication::applicationPid());
        auto *floating = session.windows.floating();
        for (auto *window : {&session.desktop, &session.mini}) {
            qInfo() << window << nativeFloatingAppearance(window)
                    << nativeMacComposition(window, hostWindow);
            QTRY_VERIFY_WITH_TIMEOUT(
                nativeMacComposition(window, hostWindow).value("hostOnScreen").toBool(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(
                nativeMacComposition(window, hostWindow).value("aboveHost").toBool(), 5000);
        }
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (auto pair : {qMakePair(&session.desktop, floating->desktop()),
                              qMakePair(&session.mini, floating->mini())}) {
                pair.second->setAlwaysOnTop(false);
                QVERIFY(!nativeFloatingAppearance(pair.first).value("allSpaces").toBool());
                QVERIFY(
                    !nativeFloatingAppearance(pair.first).value("fullscreenOverlayLevel").toBool());
                // Unpinning removes the level guarantee; normal same-level
                // ordering can keep the old order until another window raises.
                QFile raise(hostDirectory.filePath("raise"));
                QVERIFY(raise.open(QIODevice::WriteOnly));
                raise.close();
                QTRY_VERIFY(!QFile::exists(raise.fileName()));
                QTRY_VERIFY(
                    !nativeMacComposition(pair.first, hostWindow).value("aboveHost").toBool());
                pair.second->setAlwaysOnTop(true);
                QTRY_VERIFY(
                    nativeMacComposition(pair.first, hostWindow).value("aboveHost").toBool());
                QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 0);
            }
            floating->setLocked(true);
            QTRY_VERIFY(
                nativeMacComposition(&session.desktop, hostWindow).value("aboveHost").toBool());
            floating->setLocked(false);
        }
        // Also open the overlays after the other app is already fullscreen.
        // Mini's activation must not return us to the hidden main window's Space.
        floating->setDesktopEnabled(false);
        floating->attachDesktop(nullptr);
        session.desktop.hide();
        floating->leaveMini();
        floating->attachMini(nullptr);
        session.mini.hide();
        session.open();
        for (auto *window : {&session.desktop, &session.mini}) {
            QTRY_VERIFY(nativeMacComposition(window, hostWindow).value("hostOnScreen").toBool());
            QTRY_VERIFY(nativeMacComposition(window, hostWindow).value("aboveHost").toBool());
        }
        QCOMPARE(nativeMacApplicationPresentation().value("activationPolicy").toInt(), 0);
        qInfo() << "Both overlays stayed above a separate application's real fullscreen Space; "
                   "Dock policy Regular";
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EchoMusicTests");
    QCoreApplication::setApplicationName("MacFloatingWindows");
    MacFloatingWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "MacFloatingWindowTest.moc"
