#include "WindowController.h"
#include "TrayIconTheme.h"
#include "PlaybackService.h"
#include "ApiClient.h"
#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>

using namespace EchoMusic;
#ifdef Q_OS_MACOS
bool dispatchMacMenuAction(QMenu *menu, const QString &text);
QStringList macDockMenuLabels();
bool dispatchMacDockAction(const QString &text);
quintptr macApplicationDelegate();
namespace EchoMusic
{
void retireMacMenu(QMenu *menu);
QObject *createMacDockMenuOwner(WindowController *controller, std::function<QMenu *()> create);
} // namespace EchoMusic
#endif
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void setPlaying(PlaybackService &playback, bool playing)
    {
        playback.m_playing = playing;
        emit playback.changed();
    }
};
class NativeMenusTestAccess
{
public:
    static QMenu *menu(WindowController &window)
    {
        return window.m_trayMenu;
    }
    static QMenu *create(WindowController &window)
    {
        return window.createTrayMenu();
    }
    static QMenu *createDock(WindowController &window)
    {
        return window.createDockMenu();
    }
    static QMenu *createTaskbarLyric(WindowController &window)
    {
        return window.createTaskbarLyricMenu();
    }
    static void activate(WindowController &window, QSystemTrayIcon::ActivationReason reason)
    {
        window.activateTray(int(reason));
    }
    static QSystemTrayIcon *installTrayMenuModel(WindowController &window)
    {
        window.m_tray = std::make_unique<QSystemTrayIcon>();
        window.m_tray->setIcon(TrayIconTheme::icon());
        window.installTrayMenuModel();
        return window.m_tray.get();
    }
};
} // namespace EchoMusic
namespace
{
QAction *find(QMenu *menu, const QString &text)
{
    for (auto *action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}
void deferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
} // namespace
class NativeMenusTest : public QObject
{
    Q_OBJECT
private slots:
    void exportedTrayModelSurvivesRemoteCloseAndKeepsActionsCurrent()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("exported-menu.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"));
        playback.setStartupHeld(true);
        WindowController window(settings, false);
        window.attachPlayback(playback);
        QQuickWindow main;
        window.attach(&main);
        auto *tray = NativeMenusTestAccess::installTrayMenuModel(window);
        QPointer<QMenu> model = tray->contextMenu();
        QVERIFY(model);
        // The initial export must already contain the complete tree: a panel
        // can fetch GetLayout without sending AboutToShow or activated(Context).
        QVERIFY(find(model, "打开主窗口"));
        QVERIFY(find(model, "播放"));
        // The exported tray menu carries the playback modes as a titled group,
        // not a submenu: the shell shows this menu as a native menu, and its
        // converter hoists a submenu's items to the front of the parent, which
        // would put them above the window commands.
        QVERIFY(find(model, "播放模式"));
        QVERIFY(!find(model, "播放模式")->menu());
        QVERIFY(!find(model, "播放模式")->isEnabled());
        for (const auto *label : {"顺序播放", "列表循环", "随机播放", "单曲循环"}) {
            QVERIFY(find(model, label));
            QVERIFY(find(model, label)->isCheckable());
        }
        const auto actions = model->actions();
        QSignalSpy opening(&window, &WindowController::nativeMenuOpening);
        QSignalSpy quitting(&window, &WindowController::quitRequested);
        for (int cycle = 0; cycle < 4; ++cycle) {
            main.hide();
            NativeMenusTestAccess::activate(window, QSystemTrayIcon::Context);
            QVERIFY(!main.isVisible());
            QVERIFY(!NativeMenusTestAccess::menu(window));
            QVERIFY(!model->isVisible());
            QVERIFY(!model->testAttribute(Qt::WA_WState_Created));
            QVERIFY(QMetaObject::invokeMethod(model, "aboutToShow", Qt::DirectConnection));
            QCOMPARE(opening.size(), cycle + 1);
            playback.setVolume(40);
            find(model, "增大音量")->trigger();
            QCOMPARE(playback.volume(), 45.);
            QVERIFY(find(model, "音量 45%"));
            PlaybackServiceTestAccess::setPlaying(playback, true);
            QVERIFY(find(model, "暂停"));
            find(model, "随机播放")->trigger();
            QCOMPARE(playback.playMode(), PlaybackService::Shuffle);
            QVERIFY(find(model, "随机播放")->isChecked());
            // Qt maps a D-BusMenu root 'closed' event to aboutToHide. Remote
            // close must not invalidate the panel's cached action IDs/model.
            QVERIFY(QMetaObject::invokeMethod(model, "aboutToHide", Qt::DirectConnection));
            deferredDeletes();
            QCOMPARE(tray->contextMenu(), model.data());
            QCOMPARE(model->actions(), actions);
            PlaybackServiceTestAccess::setPlaying(playback, false);
            playback.setPlayMode(PlaybackService::ListLoop);
            QVERIFY(find(model, "播放"));
            QVERIFY(find(model, "列表循环")->isChecked());
            NativeMenusTestAccess::activate(window, QSystemTrayIcon::Trigger);
            QVERIFY(main.isVisible());
        }
        find(model, "退出EchoMusic")->trigger();
        QCOMPARE(quitting.size(), 1);
        window.shutdown();
        QVERIFY(!tray->contextMenu());
        QVERIFY(!model);
        NativeMenusTestAccess::activate(window, QSystemTrayIcon::Context);
        QVERIFY(!NativeMenusTestAccess::menu(window));
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
    void taskbarLyricMenuDisablesTheStripAboveQuit()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("lyric-menu.ini"), QSettings::IniFormat);
        WindowController window(settings, false);
        window.taskbar()->setEnabled(true);
        QSignalSpy quitting(&window, &WindowController::quitRequested);

        QMenu *menu = NativeMenusTestAccess::createTaskbarLyric(window);
        QVERIFY(menu);
        // The strip's own menu: reach the app, reach its settings, then the two
        // commands that take the strip away.
        QStringList labels;
        for (auto *action : menu->actions())
            labels.append(action->isSeparator() ? QStringLiteral("-") : action->text());
        QCOMPARE(labels, QStringList({QStringLiteral("打开主窗口"), QStringLiteral("打开设置页"),
                                      QStringLiteral("-"), QStringLiteral("关闭任务栏歌词"),
                                      QStringLiteral("退出EchoMusic")}));

        find(menu, QStringLiteral("关闭任务栏歌词"))->trigger();
        QVERIFY(!window.taskbar()->enabled());
        find(menu, QStringLiteral("退出EchoMusic"))->trigger();
        QCOMPARE(quitting.size(), 1);
        menu->deleteLater();
        deferredDeletes();
    }
    void cocoaDockUsesFreshStateAndReleasesRecipes()
    {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Requires Cocoa's application delegate and Dock commands");
        QTemporaryDir dir;
        QSettings settings(dir.filePath("dock.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"));
        playback.setStartupHeld(true);
        WindowController window(settings, false);
        window.attachPlayback(playback);
        const auto previousDelegate = macApplicationDelegate();
        int creations = 0;
        QPointer<QMenu> recipe, submenu;
        std::unique_ptr<QObject> owner(createMacDockMenuOwner(&window, [&] {
            ++creations;
            recipe = NativeMenusTestAccess::createDock(window);
            submenu = recipe ? find(recipe, "播放模式")->menu() : nullptr;
            return recipe.data();
        }));
        QCOMPARE(creations, 0);
        QVERIFY(macApplicationDelegate() != previousDelegate);
        playback.setVolume(40);
        for (int cycle = 0; cycle < 4; ++cycle) {
            const auto labels = macDockMenuLabels();
            QVERIFY(labels.contains("播放"));
            QVERIFY(labels.contains("上一首"));
            QVERIFY(labels.contains("下一首"));
            QVERIFY(labels.contains("音量 40%"));
            QVERIFY(!labels.contains("打开主窗口"));
            QVERIFY(!labels.contains("退出EchoMusic"));
            QVERIFY(!recipe && !submenu);
            QVERIFY(dispatchMacDockAction("增大音量"));
            QCOMPARE(playback.volume(), 45.);
            QVERIFY(!recipe && !submenu);
            QVERIFY(dispatchMacDockAction("减小音量"));
            QCOMPARE(playback.volume(), 40.);
            QVERIFY(dispatchMacDockAction("静音 / 取消静音"));
            QCOMPARE(playback.volume(), 0.);
            QVERIFY(dispatchMacDockAction("静音 / 取消静音"));
            QCOMPARE(playback.volume(), 40.);
            QVERIFY(dispatchMacDockAction("随机播放"));
            QCOMPARE(playback.playMode(), PlaybackService::Shuffle);
            QVERIFY(dispatchMacDockAction("列表循环"));
            QCOMPARE(playback.playMode(), PlaybackService::ListLoop);
            QVERIFY(!dispatchMacDockAction("锁定桌面歌词"));
            window.floating()->setDesktopEnabled(true);
            QVERIFY(dispatchMacDockAction("锁定桌面歌词"));
            QVERIFY(window.floating()->locked());
            QVERIFY(dispatchMacDockAction("解锁桌面歌词"));
            QVERIFY(!window.floating()->locked());
            window.floating()->setDesktopEnabled(false);
            QVERIFY(!recipe && !submenu);
        }
        window.shutdown();
        QVERIFY(macDockMenuLabels().isEmpty());
        QVERIFY(!dispatchMacDockAction("增大音量"));
        QVERIFY(!recipe && !submenu);
        owner.reset();
        QCOMPARE(macApplicationDelegate(), previousDelegate);
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
#else
        QSKIP("macOS only");
#endif
    }
    void cocoaCommandsSurviveRetirement()
    {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Requires the native Cocoa action dispatch path");
        QTemporaryDir dir;
        QSettings settings(dir.filePath("native.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"));
        playback.setStartupHeld(true);
        WindowController window(settings, false);
        window.attachPlayback(playback);
        const std::unique_ptr<QObject> dockOwner(createMacDockMenuOwner(
            &window, [&] { return NativeMenusTestAccess::createDock(window); }));
        QSignalSpy quitting(&window, &WindowController::quitRequested);
        for (int cycle = 0; cycle < 4; ++cycle) {
            playback.setVolume(40);
            QPointer<QMenu> menu = NativeMenusTestAccess::create(window);
            QVERIFY(dispatchMacMenuAction(menu, "增大音量"));
            retireMacMenu(menu);
            QTRY_COMPARE(playback.volume(), 45.);
            QTRY_VERIFY(!menu);
            menu = NativeMenusTestAccess::create(window);
            // The tray menu keeps the modes as a titled group, not a submenu.
            QVERIFY(find(menu, "播放模式"));
            QVERIFY(!find(menu, "播放模式")->menu());
            playback.setPlayMode(PlaybackService::ListLoop);
            QVERIFY(dispatchMacMenuAction(menu, "随机播放"));
            retireMacMenu(menu);
            QTRY_COMPARE(playback.playMode(), PlaybackService::Shuffle);
            QTRY_VERIFY(!menu);
            menu = NativeMenusTestAccess::create(window);
            QVERIFY(dispatchMacMenuAction(menu, "退出EchoMusic"));
            retireMacMenu(menu);
            QTRY_COMPARE(quitting.size(), cycle + 1);
            QTRY_VERIFY(!menu);
        }
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
#else
        QSKIP("macOS only");
#endif
    }
    void serviceActionsAndLifetime()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("menus.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"));
        playback.setStartupHeld(true);
        WindowController window(settings, false);
        window.attachPlayback(playback);
        QVERIFY(!NativeMenusTestAccess::menu(window));
        QSignalSpy quitting(&window, &WindowController::quitRequested);
        for (int cycle = 0; cycle < 4; ++cycle) {
            NativeMenusTestAccess::activate(window, QSystemTrayIcon::Context);
            QPointer<QMenu> menu = NativeMenusTestAccess::menu(window);
            QVERIFY(menu);
            NativeMenusTestAccess::activate(window, QSystemTrayIcon::Context);
            QCOMPARE(NativeMenusTestAccess::menu(window), menu.data());
            QVERIFY(find(menu, "打开主窗口"));
            QVERIFY(find(menu, "打开 Mini 窗口"));
            QVERIFY(find(menu, "播放"));
            PlaybackServiceTestAccess::setPlaying(playback, true);
            QVERIFY(find(menu, "暂停"));
            PlaybackServiceTestAccess::setPlaying(playback, false);
            QVERIFY(find(menu, "播放"));
            QVERIFY(find(menu, "上一首"));
            QVERIFY(find(menu, "下一首"));
            playback.setVolume(40);
            QVERIFY(find(menu, "音量 40%"));
            QVERIFY(!find(menu, "音量 40%")->isEnabled());
            find(menu, "增大音量")->trigger();
            QCOMPARE(playback.volume(), 45.);
            find(menu, "减小音量")->trigger();
            QCOMPARE(playback.volume(), 40.);
            find(menu, "静音 / 取消静音")->trigger();
            QCOMPARE(playback.volume(), 0.);
            find(menu, "静音 / 取消静音")->trigger();
            QCOMPARE(playback.volume(), 40.);
            // The modes are a titled group here, not a submenu (see
            // addPlaybackMenuActions), so they are addressed on the menu itself.
            const QList<QAction *> modes{find(menu, "顺序播放"), find(menu, "列表循环"),
                                         find(menu, "随机播放"), find(menu, "单曲循环")};
            for (auto *mode : modes) {
                QVERIFY(mode);
                mode->trigger();
                QCOMPARE(int(playback.playMode()), mode->data().toInt());
                for (auto *other : modes)
                    QCOMPARE(other->isChecked(), other == mode);
            }
            playback.setPlayMode(PlaybackService::ListLoop);
            QVERIFY(find(menu, "列表循环")->isChecked());
            QVERIFY(!find(menu, "锁定桌面歌词")->isEnabled());
            window.floating()->setDesktopEnabled(true);
            QVERIFY(find(menu, "锁定桌面歌词")->isEnabled());
            find(menu, "锁定桌面歌词")->trigger();
            QVERIFY(window.floating()->locked());
            find(menu, "解锁桌面歌词")->trigger();
            QVERIFY(!window.floating()->locked());
            window.floating()->setDesktopEnabled(false);
            find(menu, "退出EchoMusic")->trigger();
            QCOMPARE(quitting.size(), cycle + 1);
            menu->close();
            // Cocoa retires after queued native commands have reached QAction.
            // Observe completion instead of assuming one event pass suffices.
            QTRY_VERIFY(!menu);
            QVERIFY(!NativeMenusTestAccess::menu(window));
        }
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        window.shutdown();
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
    void leftRestoresCurrentModeAndIgnoresOtherButtons()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("window.ini"), QSettings::IniFormat);
        WindowController window(settings, false);
        QQuickWindow main, mini;
        window.attach(&main);
        main.hide();
        NativeMenusTestAccess::activate(window, QSystemTrayIcon::DoubleClick);
        NativeMenusTestAccess::activate(window, QSystemTrayIcon::MiddleClick);
        QVERIFY(!main.isVisible());
        NativeMenusTestAccess::activate(window, QSystemTrayIcon::Trigger);
        QVERIFY(main.isVisible());
        window.showMiniWindow();
        window.floating()->attachMini(&mini);
        mini.hide();
        NativeMenusTestAccess::activate(window, QSystemTrayIcon::Trigger);
        QVERIFY(window.floating()->miniActive());
        QVERIFY(mini.isVisible());
        QVERIFY(!main.isVisible());
        window.showMiniWindow();
        QVERIFY(window.floating()->miniActive());
        // A closed tray-owned Mini releases its surface but retains mode
        // intent. No native tray exists offscreen, so drive that same teardown
        // directly rather than the intentional no-tray fallback to main.
        window.floating()->attachMini(nullptr);
        window.floating()->leaveMini();
        mini.hide();
        NativeMenusTestAccess::activate(window, QSystemTrayIcon::Trigger);
        QVERIFY(window.floating()->miniActive());
        QVERIFY(!main.isVisible());
        window.showWindow();
        QVERIFY(!window.floating()->miniActive());
        QVERIFY(main.isVisible());
        window.toggleVisibility();
        NativeMenusTestAccess::activate(window, QSystemTrayIcon::Trigger);
        QVERIFY(!window.floating()->miniActive());
        QVERIFY(main.isVisible());
        window.shutdown();
    }
    void blackAndWhiteIconsKeepIdenticalSourceAlpha()
    {
        const auto systemIcon = TrayIconTheme::icon();
        QVERIFY(!systemIcon.isNull());
        QVERIFY(!systemIcon.pixmap(16, 16).isNull());
        QVERIFY(!systemIcon.pixmap(32, 32).isNull());
        const auto black = TrayIconTheme::monochrome(Qt::black);
        const auto white = TrayIconTheme::monochrome(Qt::white);
        for (const int size : {16, 20, 24, 32, 40, 48, 64}) {
            const auto a =
                black.pixmap(size, size).toImage().convertToFormat(QImage::Format_ARGB32);
            const auto b =
                white.pixmap(size, size).toImage().convertToFormat(QImage::Format_ARGB32);
            QCOMPARE(a.size(), b.size());
            bool opaque = false, transparent = false;
            for (int y = 0; y < a.height(); ++y)
                for (int x = 0; x < a.width(); ++x) {
                    const auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
                    QCOMPARE(p.alpha(), q.alpha());
                    if (p.alpha()) {
                        QCOMPARE(p.red(), 0);
                        QCOMPARE(q.red(), 255);
                        opaque = true;
                    } else {
                        transparent = true;
                    }
                }
            QVERIFY(opaque && transparent);
        }
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("EchoMusicNext");
    app.setApplicationDisplayName("EchoMusicNext");
    app.setQuitOnLastWindowClosed(false);
    if (app.arguments().contains("--native-probe")) {
        // Same production owner/helpers, temporary preferences, no real account,
        // no API/audio startup and no global shortcuts or single-instance lock.
        QTemporaryDir dir;
        QSettings settings(dir.filePath("probe.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, dir.filePath("playback.sqlite"));
        playback.setStartupHeld(true);
        WindowController owner(settings, true);
        owner.attachPlayback(playback);
        QQuickWindow main;
        main.setTitle("EchoMusicNext native menu probe");
        main.setColor(Qt::white);
        owner.attach(&main);
        main.show();
        QObject::connect(&owner, &WindowController::settingsRequested, &app,
                         [&main](const QString &) { main.setTitle("Preferences requested"); });
        QObject::connect(&owner, &WindowController::quitRequested, &app, &QApplication::quit);
        const int result = app.exec();
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        owner.shutdown();
        playback.shutdown();
        api.shutdown();
        if (closed.isEmpty())
            closed.wait(15000);
        if (apiClosed.isEmpty())
            apiClosed.wait(15000);
        return result;
    }
    NativeMenusTest test;
    if (QGuiApplication::platformName() == "cocoa") {
        int result = 1;
        QTimer::singleShot(0, &app, [&] {
            result = QTest::qExec(&test, argc, argv);
            app.exit(result);
        });
        app.exec();
        return result;
    }
    return QTest::qExec(&test, argc, argv);
}
#include "NativeMenusTest.moc"
