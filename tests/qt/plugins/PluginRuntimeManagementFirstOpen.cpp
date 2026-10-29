#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include <QQmlIncubationController>
namespace
{
QList<QQuickItem *> cards(QQuickItem *root)
{
    QList<QQuickItem *> result;
    if (root->isVisible() && root->objectName().startsWith("pluginCard-cold-open"))
        result.append(root);
    for (auto *child : root->childItems())
        result.append(cards(child));
    return result;
}
} // namespace
void PluginRuntimeTest::managementFirstOpenAfterRestart_data()
{
    QTest::addColumn<int>("pluginCount");
    QTest::addColumn<bool>("dark");
    QTest::addColumn<double>("scale");
    for (const int count : {1, 7})
        for (const bool dark : {false, true})
            for (const double scale : {1., 1.25})
                QTest::newRow(qPrintable(
                    QString("plugins-%1-%2-%3").arg(count).arg(dark ? "dark" : "light").arg(scale)))
                    << count << dark << scale;
}
void PluginRuntimeTest::managementFirstOpenAfterRestart()
{
    QFETCH(int, pluginCount);
    QFETCH(bool, dark);
    QFETCH(double, scale);
    QTemporaryDir directory;
    EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
    ApiClient api(DeviceIdentity::temporary());
    AccountService account(api);
    PlaybackService playback(api, preferences, nullptr, directory.filePath("playback.sqlite"));
    const auto closeServices = qScopeGuard([&] {
        playback.shutdown();
        api.shutdown();
    });
    ThemeStore theme(preferences);
    RouteController router;
    ShellSettings settings(preferences);
    ShellController shell(preferences, router);
    QQmlIncubationController incubation;
    QQmlEngine engine;
    theme.attachToEngine(engine);
    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    theme.setMotionEnabled(false);
    const auto rootPath = directory.filePath("plugins");
    QStringList sourcePaths;
    for (int i = 0; i < pluginCount; ++i) {
        sourcePaths.append(pluginPackage(
            directory.path(), "cold-open-" + QString::number(i), "export function activate(c){}",
            {{"name", "冷启动插件 " + QString::number(i)},
             {"description", i % 2 ? QString("多行描述 ").repeated(20) : "短描述"}}));
    }
    {
        PluginRuntime first(engine, preferences, {playback, theme, shell, router, account, api},
                            rootPath);
        first.start(sourcePaths);
        const auto stop = qScopeGuard([&] {
            QSignalSpy closed(&first, &PluginRuntime::closed);
            first.shutdown();
            QVERIFY(QTest::qWaitFor([&] { return closed.count() == 1; }, 7000));
        });
        QTRY_VERIFY(!first.loading());
        QTRY_COMPARE(first.rowCount(), pluginCount);
        QTRY_COMPARE(first.enabledCount(), pluginCount);
    }
    // Recreate the runtime using the persisted installation catalog. The page
    // is not present until the first navigation through the production RouteHost.
    PluginRuntime runtime(engine, preferences, {playback, theme, shell, router, account, api},
                          rootPath);
    const auto stop = qScopeGuard([&] {
        QSignalSpy closed(&runtime, &PluginRuntime::closed);
        runtime.shutdown();
        QVERIFY(QTest::qWaitFor([&] { return closed.count() == 1; }, 7000));
    });
    runtime.start();
    QTRY_VERIFY(!runtime.loading());
    QCOMPARE(runtime.rowCount(), pluginCount);
    QCOMPARE(runtime.enabledCount(), pluginCount);
    PluginManager manager(runtime, preferences);
    QVERIFY(router.registerPage({"plugin-management",
                                 "/plugins",
                                 QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginManagementPage.qml"),
                                 {"view"},
                                 true,
                                 false,
                                 false,
                                 {{"manager", QVariant::fromValue(&manager)}}}));
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Shell
ApplicationWindow {
 id: win
 required property RouteController router
 required property ShellSettings settings
 property real uiScale: 1
 width:1280; height:900; visible:true; color:Theme.main
 Item {
  width: win.width / win.uiScale; height: win.height / win.uiScale
  scale: win.uiScale; transformOrigin: Item.TopLeft
  RouteHost { anchors.fill:parent; router:win.router; settings:win.settings }
 }
})",
                      QUrl("qrc:/plugin-first-open-window.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> scene(
        component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                               {"settings", QVariant::fromValue(&settings)},
                                               {"uiScale", scale}}));
    QVERIFY2(scene, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(scene.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    engine.setIncubationController(&incubation);
    const auto settle = [&] {
        return QTest::qWaitFor(
            [&] {
                incubation.incubateFor(1);
                auto *list = visual(window->contentItem(), "pluginCardList");
                const auto firstCards = cards(window->contentItem());
                if (incubation.incubatingObjectCount() != 0 || !list || firstCards.isEmpty())
                    return false;
                for (auto *card : firstCards)
                    if (card->width() <= 100 || card->height() <= 100 ||
                        card->height() + .1 < card->implicitHeight())
                        return false;
                return true;
            },
            7000);
    };
    QStringList warnings;
    const auto warningConnection =
        connect(&engine, &QQmlEngine::warnings, &manager, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings.append(error.toString());
        });
    const auto stopWarnings =
        qScopeGuard([warningConnection] { QObject::disconnect(warningConnection); });
    const auto teardown = qScopeGuard([&] {
        for (int i = 0; i < 4; ++i) {
            incubation.incubateFor(5);
            QCoreApplication::processEvents();
        }
        scene.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        engine.setIncubationController(nullptr);
    });
    QVERIFY(router.navigate("plugin-management"));
    const bool firstShown = settle();
    if (!firstShown) {
        auto *pendingList = visual(window->contentItem(), "pluginCardList");
        qInfo() << "First-open state" << "runtime count" << runtime.rowCount() << "list"
                << (pendingList ? pendingList->property("count") : QVariant()) << "incubating"
                << incubation.incubatingObjectCount();
        for (auto *card : cards(window->contentItem()))
            qInfo() << card->objectName() << card->size() << "implicit" << card->implicitHeight();
    }
    QVERIFY2(firstShown, qPrintable(warnings.join('\n')));
    auto *list = visual(window->contentItem(), "pluginCardList");
    QVERIFY(list);
    QVERIFY(list->property("count").toInt() > 0);
    auto firstCards = cards(window->contentItem());
    for (auto *card : firstCards) {
        QVERIFY(card->isVisible());
        QVERIFY(card->width() > 100);
        QVERIFY(card->height() + .1 >= card->implicitHeight());
        auto *title = visual(card, "pluginCardTitle");
        QVERIFY(title);
        QVERIFY(title->property("text").toString().startsWith("冷启动插件"));
    }
    const auto firstBounds = firstCards.first()->mapRectToScene(firstCards.first()->boundingRect());
    QVERIFY(firstBounds.top() >= 0);
    QVERIFY(firstBounds.bottom() <= window->height());
    const auto captures = qEnvironmentVariable("ECHO_PLUGIN_FIRST_OPEN_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        QVERIFY(QDir().mkpath(captures));
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        window->requestUpdate();
        QTRY_VERIFY(!frames.isEmpty());
        QVERIFY(window->grabWindow().save(captures + '/' +
                                          QString::fromLatin1(QTest::currentDataTag()) + ".png"));
    }
    // A later resize must preserve the initial result and recompute real text
    // heights/columns, including a partially filled last row.
    QSignalSpy resizedFrames(window, &QQuickWindow::frameSwapped);
    window->resize(900, 900);
    window->requestUpdate();
    QTRY_VERIFY(!resizedFrames.isEmpty());
    QVERIFY(settle());
    firstCards = cards(window->contentItem());
    for (auto *card : firstCards)
        QVERIFY(card->height() + .1 >= card->implicitHeight());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}
