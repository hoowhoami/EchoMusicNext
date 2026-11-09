#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginAppearance.h"
#include "EchoPluginViewContext.h"
#include "AppearanceHost.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoPluginUiParts.h"
#include "EchoUiContribution.h"
#include <QWheelEvent>
namespace
{
bool routeTransitioning(QQuickItem *page)
{
    const auto *entry = page->findChild<QObject *>("routeEntryAnimation");
    const auto *exit = page->findChild<QObject *>("routeExitAnimation");
    return (entry && entry->property("running").toBool()) ||
           (exit && exit->property("running").toBool()) ||
           page->property("waitingEnter").toBool() || page->property("waitingExit").toBool();
}
QByteArray appearanceVisual = R"(
import QtQuick
import EchoMusic.PluginSdk
import EchoMusic.Theme
Rectangle {
 id: visual
 required property EchoPluginContext plugin
 required property EchoPluginViewContext page
 required property AppearanceConfiguration configuration
 objectName: "actualAppearanceVisual"
 implicitHeight: 900
 property real creationWidth: -1
 Component.onCompleted: {
   creationWidth=width;
   if(!parent)throw new Error('presentation parent missing at completion');
   plugin.ui.styles.registerRule('appearance-test', {role:'never-matches'}, {opacity:0.8}, 2);
   plugin.setTimer('appearance-timer', 10, true, function() { plugin.ui.setState({presentationTicks:Number(plugin.ui.state.presentationTicks || 0)+1}); });
 }
 color: configuration.values.color || "#163544"
 EchoPluginLyricsView { width:parent.width; height:Math.max(0,parent.height-100); page: visual.page }
 EchoPluginPlaybackControls { anchors.bottom: progress.top; anchors.horizontalCenter: parent.horizontalCenter; page: visual.page }
 EchoPluginProgress { id: progress; anchors.bottom: parent.bottom; width: parent.width; page: visual.page }
})";
QString appearancePackage(Host &host, const QByteArray &body, const QString &id = "test.appearance",
                          bool windows = false)
{
    QJsonArray capabilities{"theme", "lyricsPage"};
    if (windows)
        capabilities.append("windows");
    const auto path =
        pluginPackage(host.temporary.path(), id, "export function activate(c) {" + body + "}",
                      {{"capabilities", capabilities}});
    write(path + "/Wrapper.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Item {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName: "appearanceCompositionWrapper"
 EchoUiHostContent { part: parent.part; anchors.fill: parent; anchors.margins: 8 }
}
)");
    write(path + "/Visual.qml", appearanceVisual);
    auto redirect = appearanceVisual;
    redirect.replace("Component.onCompleted: {",
                     "Component.onCompleted: { plugin.theme.useSkin('cover');");
    write(path + "/Redirect.qml", redirect);
    write(path + "/Bad.qml", "import QtQuick\nRectangle { invalid syntax }");
    write(path + "/Nested.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
import EchoMusic.Theme
Item {
 required property EchoPluginContext plugin
 required property EchoPluginViewContext page
 required property AppearanceConfiguration configuration
 NestedFailure {}
})");
    write(path + "/NestedFailure.qml", R"(
import QtQuick
Item { Timer {interval:10;running:true;onTriggered: {throw new Error("nested runtime failure")}} }
)");
    return path;
}
QString installAppearance(Host &host, const QString &path, const QString &id = "test.appearance")
{
    host.runtime.importDirectory(path);
    QElapsedTimer elapsed;
    elapsed.start();
    QString installation;
    while (elapsed.elapsed() < 5000) {
        installation = host.runtime.installationForId(id);
        if (!host.runtime.loading() &&
            (installation.isEmpty() || host.runtime.state(installation) == PluginRuntime::Active ||
             host.runtime.state(installation) == PluginRuntime::Failed))
            break;
        QTest::qWait(1);
    }
    return installation;
}
QByteArray definitions = R"(
 const theme=c.theme.registerTheme({id:'ocean',title:'海洋',description:'Actual theme',lyricsIcons:['Flower','unknown-icon','Star'],
 light:{palette:{main:'#cee1ed',text:'#102030'},mini:{background:'#234567',text:'#ffffff'}},
 dark:{palette:{main:'#102030',text:'#e1f3ff'},mini:{background:'#234567',text:'#ffffff'}},
 miniComponent:'Visual.qml',settingsComponent:'Visual.qml',
 defaults:{color:'#163544',amount:1},resolve:state=>({palette:{accent:state.settings.color}}),validate:v=>v.amount>=0 && v.amount<=2});
 if(!theme)throw new Error(c.theme.errorMessage);
 const skin=c.ui.lyricsPage.registerSkin({id:'wide',title:'完整皮肤',component:'Visual.qml',settingsComponent:'Visual.qml',titlebar:'none',tools:'none',defaults:{color:'#801020'}});
 if(!skin)throw new Error(c.theme.errorMessage);
 c.ui.setState({prematurePatch:theme.configuration.patch({amount:2})});
 c.registerCommand('choose',()=>c.theme.useTheme('ocean'));
 c.registerCommand('skin',()=>c.ui.lyricsPage.useSkin('wide'));
)";
std::unique_ptr<QObject> productionWindow(Host &host, ShellSettings &settings)
{
    host.floating.showMini();
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Playback
import EchoMusic.Themes
import EchoMusic.Theme
ApplicationWindow {
 id: root; width: 1100; height: 820; visible: true
 required property PlaybackService playback
 required property FloatingPlayerController floating
 required property RouteController routes
 required property ShellSettings preferences
 Item {
  width: parent.width/root.preferences.uiScale; height: parent.height/root.preferences.uiScale
  scale: root.preferences.uiScale; transformOrigin: Item.TopLeft
  ThemeCenterPage { objectName: "productionThemeCenter"; anchors.fill: parent; router: root.routes; settings: root.preferences; context: root.routes.current }
 }
 PlaybackPage { id: playbackPage; playback: root.playback; uiScale: root.preferences.uiScale }
 MiniPlayerWindow { id: mini; objectName: "productionAppearanceMini"; playback: root.playback; service: root.floating }
 function openPlayback() { playbackPage.open(); }
})",
                      QUrl("qrc:/plugin-appearance-production.qml"));
    auto *object =
        component.createWithInitialProperties({{"playback", QVariant::fromValue(&host.playback)},
                                               {"floating", QVariant::fromValue(&host.floating)},
                                               {"routes", QVariant::fromValue(&host.router)},
                                               {"preferences", QVariant::fromValue(&settings)}});
    if (!object)
        qWarning().noquote() << component.errorString();
    return std::unique_ptr<QObject>(object);
}
} // namespace
void PluginRuntimeTest::builtDynamicThemePackage()
{
    const auto source = qEnvironmentVariable("ECHO_DYNAMIC_THEME_PACKAGE");
    if (source.isEmpty())
        QSKIP("Set ECHO_DYNAMIC_THEME_PACKAGE to the built dynamic-themes .echomod");
    Host host;
    host.shell.attachPlayback(host.playback);
    host.shell.attachTheme(host.theme);
    host.theme.setMotionEnabled(false);
    const auto sampleScale = qEnvironmentVariable("ECHO_DYNAMIC_THEME_SCALE").toDouble();
    for (int step = 0; step < 20 && sampleScale > host.shellSettings.uiScale() + .001; ++step)
        host.shellSettings.zoomIn();
    host.runtime.importDirectory(source);
    QTRY_VERIFY(!host.runtime.loading());
    const auto installation = host.runtime.installationForId("dynamic-themes");
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *catalog = host.theme.extensions();
    QCOMPARE(catalog->entries("theme").size(), 5);
    QVERIFY(catalog->entries("skin").isEmpty());
    QVERIFY(!catalog->theme());
    const auto originalSkin = catalog->selectedSkin();
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Themes
ApplicationWindow {
 id: root; width: 960; height: 540; visible: true
 required property RouteController routes
 required property ShellSettings preferences
 property bool galleryOpen: false
 property bool settingsOpen: false
 Item {
  width: parent.width/root.preferences.uiScale; height: parent.height/root.preferences.uiScale
  scale: root.preferences.uiScale; transformOrigin: Item.TopLeft
  ThemeBackground { anchors.fill: parent; opacity: 1-Theme.store.transparency/100 }
  ThemeCenterPage { anchors.fill: parent; visible: root.galleryOpen; router: root.routes; settings: root.preferences; context: root.routes.current }
  AppearanceHost { anchors.fill: parent; entry: Theme.store.extensions.theme; placement: "settings"; visible: root.settingsOpen }
 }
})",
                      QUrl("qrc:/dynamic-theme-production.qml"));
    std::unique_ptr<QObject> root(component.createWithInitialProperties(
        {{"routes", QVariant::fromValue(&host.router)},
         {"preferences", QVariant::fromValue(&host.shellSettings)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    const auto capture = qEnvironmentVariable("ECHO_DYNAMIC_THEME_CAPTURE_DIR");
    if (!capture.isEmpty())
        QDir().mkpath(capture);
    const auto select = [&](const QString &id) {
        return host.runtime.context(installation)->appearance()->useTheme(id);
    };
    for (const auto &id :
         QStringList{"autumn", "prism", "golden-ribbon", "willow", "ferris-wheel"}) {
        QVERIFY(select(id));
        QQuickItem *scene = nullptr;
        QTRY_VERIFY2((scene = visual(window->contentItem(), "dynamicThemeScene")),
                     qPrintable(id + ": " +
                                (catalog->find(installation + ":theme:" + id)
                                     ? catalog->find(installation + ":theme:" + id)->error
                                     : "missing entry")));
        QTRY_COMPARE(scene->property("kind").toString(), id);
        QVERIFY(catalog->theme()->error.isEmpty());
        QCOMPARE(catalog->selectedSkin(), originalSkin);
        auto *configuration = catalog->theme()->configuration();
        QVERIFY(!configuration->patch({{"speed", 0}}));
        QVERIFY(configuration->patch({{"speed", 1.5}}));
        QTRY_VERIFY(!configuration->busy());
        QTRY_COMPARE(scene->property("speed").toDouble(), 1.5);
        root->setProperty("settingsOpen", true);
        QTRY_VERIFY(visual(window->contentItem(), "dynamicThemeSettings"));
        root->setProperty("settingsOpen", false);
        QTRY_VERIFY(!visual(window->contentItem(), "dynamicThemeSettings"));
        // Let asynchronous artwork decode and the native scene graph compile.
        QTest::qWait(250);
        auto staticFrame = window->grabWindow();
        QVERIFY(!staticFrame.isNull());
        if (!capture.isEmpty())
            QVERIFY(staticFrame.save(capture + '/' + id + "-static.png"));
        QTest::qWait(70);
        QCOMPARE(window->grabWindow(), staticFrame);
        host.theme.setMotionEnabled(true);
        QTRY_VERIFY(scene->property("animated").toBool());
        QTest::qWait(250);
        const auto moving = window->grabWindow();
        const bool softwareEffect =
            window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software &&
            (id == "prism" || id == "golden-ribbon");
        if (!softwareEffect)
            QVERIFY(moving != staticFrame);
        if (!capture.isEmpty())
            QVERIFY(moving.save(capture + '/' + id + "-moving.png"));
        if (qEnvironmentVariableIsSet("ECHO_DYNAMIC_THEME_PROFILE")) {
            for (bool animate : {true, false}) {
                host.theme.setMotionEnabled(animate);
                QTest::qWait(100);
                QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                const auto cpu = std::clock();
                QElapsedTimer wall;
                wall.start();
                QTest::qWait(1500);
                qInfo().noquote() << "Dynamic theme profile" << id << "animated=" << animate
                                  << "cpuMs=" << (std::clock() - cpu) * 1000.0 / CLOCKS_PER_SEC
                                  << "wallMs=" << wall.elapsed() << "frames=" << frames.count();
            }
            host.theme.setMotionEnabled(true);
        }
        QVERIFY(configuration->patch({{"enabled", false}}));
        QTRY_VERIFY(!configuration->busy());
        QTRY_VERIFY(!scene->property("animated").toBool());
        QTest::qWait(60);
        const auto disabledFrame = window->grabWindow();
        QTest::qWait(60);
        QCOMPARE(window->grabWindow(), disabledFrame);
        QVERIFY(configuration->reset());
        QTRY_VERIFY(!configuration->busy());
        host.theme.setMotionEnabled(false);
        QPointer<QQuickItem> hiddenScene(scene);
        window->hide();
        QTRY_VERIFY(hiddenScene.isNull());
        window->show();
        QTRY_VERIFY(visual(window->contentItem(), "dynamicThemeScene"));
        QPointer<QQuickItem> transparentScene(visual(window->contentItem(), "dynamicThemeScene"));
        host.theme.setTransparency(100);
        QTRY_VERIFY(transparentScene.isNull());
        host.theme.setTransparency(0);
        QTRY_VERIFY(visual(window->contentItem(), "dynamicThemeScene"));
        QPointer<QQuickItem> oldScene(visual(window->contentItem(), "dynamicThemeScene"));
        host.theme.selectEcho();
        QTRY_VERIFY(oldScene.isNull());
    }
    QVERIFY(select("prism"));
    root->setProperty("galleryOpen", true);
    QTRY_VERIFY(visual(window->contentItem(), "themeCatalog-dynamic"));
    QCOMPARE(catalog->themeGroup("dynamic")->count(), 5);
    const auto visualItems = [&] {
        QList<QQuickItem *> items{window->contentItem()};
        for (qsizetype i = 0; i < items.size(); ++i)
            items.append(items[i]->childItems());
        return items;
    };
    for (auto *item : visualItems())
        if (item->inherits("QQuickFlickable") && !item->inherits("QQuickItemView"))
            item->setProperty(
                "contentY", qMax(0.0, item->property("contentHeight").toDouble() - item->height()));
    QQuickItem *dynamicCard = nullptr;
    QTRY_VERIFY(([&] {
        for (auto *item : visualItems())
            if (item->property("entryKey").toString() == installation + ":theme:autumn")
                dynamicCard = item;
        return dynamicCard != nullptr;
    })());
    QVERIFY(QMetaObject::invokeMethod(dynamicCard, "chosen"));
    QTRY_COMPARE(catalog->theme()->key, installation + ":theme:autumn");
    if (!capture.isEmpty()) {
        QTest::qWait(300);
        QVERIFY(window->grabWindow().save(capture + "/theme-center.png"));
    }
    root->setProperty("galleryOpen", false);
    QVERIFY(select("prism"));
    QTRY_VERIFY(visual(window->contentItem(), "dynamicThemeScene"));
    const auto selected = catalog->selectedTheme();
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QTRY_VERIFY(!visual(window->contentItem(), "dynamicThemeScene"));
    QCOMPARE(catalog->selectedTheme(), selected);
    QVERIFY(!catalog->theme());
    QVERIFY(host.runtime.setEnabled(installation, true));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QTRY_VERIFY(visual(window->contentItem(), "dynamicThemeScene"));
    QCOMPARE(catalog->selectedSkin(), originalSkin);
    QVERIFY(catalog->theme()->error.isEmpty());
    QPointer<QQuickItem> minimizedScene(visual(window->contentItem(), "dynamicThemeScene"));
    window->showMinimized();
    QTRY_VERIFY(minimizedScene.isNull());
    window->hide();
}
void PluginRuntimeTest::appearanceCatalogConfigurationAndRetirement()
{
    Host host;
    host.theme.setMotionEnabled(false);
    const auto installation = installAppearance(host, appearancePackage(host, definitions));
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QVERIFY(scope);
    auto *catalog = host.theme.extensions();
    QCOMPARE(catalog->themes()->rowCount(), 2);
    QCOMPARE(catalog->skins()->rowCount(), 4);
    QVERIFY(catalog->theme() == nullptr);
    QVERIFY(catalog->skin() == nullptr);
    QVERIFY(scope->appearance()->snapshot().contains("themeMetadata"));
    QVERIFY(scope->appearance()->snapshot().value("themeMetadata").isNull());
    QVERIFY(!scope->ui()->state().value("prematurePatch").toBool());
    auto *theme = catalog->entries("theme").first();
    auto *config = theme->configuration();
    QVERIFY(config->active());
    QVERIFY(!config->patch({{"amount", -1}}));
    QCOMPARE(config->values().value("amount").toInt(), 1);
    QSignalSpy catalogResets(catalog->themes(), &QAbstractItemModel::modelReset);
    QVERIFY(config->patch({{"amount", 1.5}, {"color", "#abcdef"}}));
    QVERIFY(!scope->appearance()->registerTheme(
        host.engine.evaluate("({id:'ocean',title:'replacement'})")));
    QCOMPARE(catalog->entries("theme").first(), theme);
    QTRY_VERIFY(!config->busy());
    QCOMPARE(catalogResets.count(), 0);
    QCOMPARE(config->values().value("amount").toDouble(), 1.5);
    QVERIFY(QFile::exists(scope->dataPath() + "/appearance/theme/ocean.json"));
    QVERIFY(scope->appearance()->useTheme("ocean"));
    const auto metadata = scope->appearance()->snapshot().value("themeMetadata").toMap();
    QCOMPARE(metadata.value("key").toString(), theme->key);
    QCOMPARE(metadata.value("title").toString(), QString("海洋"));
    QCOMPARE(metadata.value("description").toString(), QString("Actual theme"));
    QCOMPARE(metadata.value("lyricsIcons").toStringList(),
             QStringList({"Flower", "unknown-icon", "Star"}));
    // Names remain theme data; each renderer owns its supported icon vocabulary.
    // Reject malformed/unbounded contributions before they enter the catalog.
    for (const auto &icons : QStringList{"null", "'Star'", "[true]", "['']", "['   ']",
                                         "['a'.repeat(129)]", "Array(65).fill('Star')"}) {
        QVERIFY(!scope->appearance()->registerTheme(host.engine.evaluate(
            "({id:'invalid-icons',title:'Invalid',lyricsIcons:" + icons + "})")));
        QVERIFY(!catalog->find(installation + ":theme:invalid-icons"));
    }
    host.theme.setMode(ThemeStore::Light);
    QCOMPARE(host.theme.main(), QColor("#cee1ed"));
    host.theme.setMode(ThemeStore::Dark);
    QCOMPARE(host.theme.main(), QColor("#102030"));
    const auto selected = catalog->selectedTheme();
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QVERIFY(catalog->theme() == nullptr);
    QCOMPARE(catalog->selectedTheme(), selected);
    QVERIFY(host.theme.main() != QColor("#102030"));
    QVERIFY(host.runtime.setEnabled(installation, true));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QTRY_VERIFY(catalog->theme());
    QCOMPARE(catalog->theme()->configuration()->values().value("amount").toDouble(), 1.5);
    QCOMPARE(host.theme.main(), QColor("#102030"));
    // An accepted write must drain before the update snapshots private data.
    QVERIFY(catalog->theme()->configuration()->patch({{"amount", 1.75}}));
    QSignalSpy updateFinished(&host.runtime, &PluginRuntime::managementFinished);
    QVERIFY(host.runtime.installDirectory(
        appearancePackage(host, definitions + "throw new Error('appearance update rollback');")));
    QTRY_VERIFY(!updateFinished.isEmpty());
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QTRY_VERIFY(catalog->theme());
    QCOMPARE(catalog->theme()->configuration()->values().value("amount").toDouble(), 1.75);
    QVERIFY(catalog->previewTheme(catalog->theme()->key));
    QVERIFY(catalog->previewing());
    host.theme.selectSolid(QColor());
    QVERIFY(catalog->previewing());
    auto *selectedEntry = catalog->theme();
    selectedEntry->defaultMode = "dark";
    host.theme.setMode(ThemeStore::ThemeDefault);
    QVERIFY(host.theme.dark());
    host.theme.setMode(ThemeStore::Light);
    QVERIFY(!host.theme.dark());
    host.theme.setMode(ThemeStore::Dark);
    catalog->cancelPreview();
    QVERIFY(!catalog->previewing());
    host.theme.selectEcho();
    QVERIFY(catalog->selectedTheme().isEmpty());
}
void PluginRuntimeTest::appearanceProductionThemeSkinAndMini()
{
    Host host;
    host.theme.setMotionEnabled(false);
    ShellSettings settings(host.settings);
    const auto sampleScale = qEnvironmentVariable("ECHO_APPEARANCE_TEST_SCALE").toDouble();
    for (int step = 0; step < 20 && sampleScale > settings.uiScale() + .001; ++step)
        settings.zoomIn();
    const auto installation = installAppearance(host, appearancePackage(host, definitions));
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto root = productionWindow(host, settings);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *catalog = host.theme.extensions();
    auto *theme = catalog->entries("theme").first();
    QQuickItem *themeView = nullptr;
    QTRY_VERIFY((themeView = visual(window->contentItem(), "themeCatalog")));
    QTRY_COMPARE(themeView->property("count").toInt(), 2);
    // Exercise the actual production card's chosen signal, not a mock catalog.
    QQuickItem *themeCard = nullptr;
    QTRY_VERIFY(([&] {
        for (auto item : themeView->childItems())
            for (auto card : item->childItems())
                if (card->property("entryKey").toString() == theme->key)
                    themeCard = card;
        return themeCard != nullptr;
    })());
    QVERIFY(QMetaObject::invokeMethod(themeCard, "chosen"));
    QTRY_COMPARE(catalog->theme(), theme);
    const auto captures = qEnvironmentVariable("ECHO_APPEARANCE_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        QDir().mkpath(captures);
        QTest::qWait(160);
        QVERIFY(window->grabWindow().save(captures + "/theme-center.png"));
    }

    auto *mini = root->findChild<QQuickWindow *>("productionAppearanceMini");
    QVERIFY(mini);
    auto *miniHost = visual(mini->contentItem(), "pluginMiniThemeHost");
    QVERIFY(miniHost);
    QTRY_VERIFY(miniHost->property("loaded").toBool());
    QPointer<QQuickItem> miniVisual = visual(mini->contentItem(), "actualAppearanceVisual");
    QVERIFY(miniVisual);
    auto *parts = host.runtime.context(installation)->ui()->parts();
    auto *miniPart = parts->queryUnique(
        {{"key", "actualAppearanceVisual"}, {"owner", "test.appearance"}, {"surface", "mini"}});
    QVERIFY(miniPart && miniPart->registration()->target() == miniVisual);
    QVERIFY(miniPart->persistent());
    QVERIFY(miniVisual->property("creationWidth").toReal() > 0);
    QPointer<AppearanceConfiguration> configuration =
        miniVisual->property("configuration").value<AppearanceConfiguration *>();
    QVERIFY(configuration != theme->configuration());
    QCOMPARE(configuration->values(), theme->configuration()->values());
    QPointer<EchoPluginViewContext> view =
        miniVisual->property("page").value<EchoPluginViewContext *>();
    QVERIFY(view && view->active());
    auto *presentation = miniVisual->property("plugin").value<EchoPluginContext *>();
    QVERIFY(presentation && presentation != host.runtime.context(installation));
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    QVERIFY(registry);
    QTRY_COMPARE(registry->styleCount(), 1);
    QTRY_VERIFY(
        host.runtime.context(installation)->ui()->state().value("presentationTicks").toInt() > 1);
    mini->hide();
    QVERIFY(!view || !view->active());
    QVERIFY(!configuration || !configuration->patch({{"amount", 2}}));
    QCOMPARE(registry->styleCount(), 0);
    if (view) {
        auto *expired = view->command("volume", {{"value", 1}});
        QVERIFY(expired);
        QCOMPARE(expired->state(), EchoPluginOperation::Failed);
    }
    const auto stoppedTicks =
        host.runtime.context(installation)->ui()->state().value("presentationTicks");
    QTest::qWait(60);
    QCOMPARE(host.runtime.context(installation)->ui()->state().value("presentationTicks"),
             stoppedTicks);

    QTRY_VERIFY(miniVisual.isNull());
    mini->show();
    QTRY_VERIFY(miniHost->property("loaded").toBool());
    auto *beforeSession = visual(miniHost, "actualAppearanceVisual");
    QVERIFY(beforeSession);
    auto *oldScope = beforeSession->property("plugin").value<EchoPluginContext *>();
    QVERIFY(oldScope);
    QPointer<QObject> oldSessionVisual = beforeSession;
    QVERIFY(QMetaObject::invokeMethod(&host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QVERIFY(!oldScope->active());
    QVERIFY(!oldScope->ui()->setState({{"stale", true}}));
    QTRY_VERIFY(oldSessionVisual.isNull());
    QTRY_VERIFY(miniHost->property("loaded").toBool());
    auto *miniCard = visual(mini->contentItem(), "miniPlayerCard");
    QVERIFY(miniCard);
    miniCard->setOpacity(0);
    QTRY_VERIFY(!miniHost->property("loaded").toBool());
    QCOMPARE(registry->styleCount(), 0);
    miniCard->setOpacity(1);
    QTRY_VERIFY(miniHost->property("loaded").toBool());
    miniCard->setX(-miniCard->width() * 2);
    QTRY_VERIFY(!miniHost->property("loaded").toBool());
    miniCard->setX(0);
    QTRY_VERIFY(miniHost->property("loaded").toBool());
    const auto showThemeSettings = [&] {
        QQuickItem *card = nullptr;
        auto *grid = visual(window->contentItem(), "themeCatalog");
        if (grid)
            for (auto *item : grid->childItems())
                for (auto *candidate : item->childItems())
                    if (candidate->property("entryKey").toString() == theme->key)
                        card = candidate;
        return card && QMetaObject::invokeMethod(card, "settingsRequested");
    };
    QVERIFY(showThemeSettings());
    QPointer<QObject> themeDialog;
    QTRY_VERIFY((themeDialog = root->findChild<QObject *>("appearanceSettingsDialog")));
    QQuickItem *themeForm = nullptr;
    QTRY_VERIFY((themeForm = visual(window->contentItem(), "appearanceSettingsHost")));
    QTRY_VERIFY(themeForm->property("loaded").toBool());
    auto *themeViewport = themeForm->parentItem();
    while (themeViewport && !themeViewport->property("contentY").isValid())
        themeViewport = themeViewport->parentItem();
    QVERIFY(themeViewport);
    QVERIFY(themeViewport->height() > 0);
    QTRY_VERIFY2(themeViewport->property("contentHeight").toReal() > themeViewport->height(),
                 qPrintable(QString("content=%1 viewport=%2 form=%3 implicit=%4 dialog=%5")
                                .arg(themeViewport->property("contentHeight").toReal())
                                .arg(themeViewport->height())
                                .arg(themeForm->height())
                                .arg(themeForm->implicitHeight())
                                .arg(themeDialog->property("height").toReal())));
    if (!captures.isEmpty())
        QVERIFY(window->grabWindow().save(captures + "/theme-settings.png"));
    QVERIFY(QMetaObject::invokeMethod(themeDialog, "close"));
    QTRY_VERIFY(themeDialog.isNull());
    QVERIFY(QMetaObject::invokeMethod(root.get(), "openPlayback"));
    auto *page = root->findChild<QObject *>("playbackPage");
    QVERIFY(page);
    QTRY_VERIFY(page->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(page, "openSkins"));
    QQuickItem *skinCatalog = nullptr;
    QTRY_VERIFY((skinCatalog = visual(window->contentItem(), "lyricSkinCatalog")));
    QTRY_COMPARE(skinCatalog->property("count").toInt(), 4);
    auto *scope = host.runtime.context(installation);
    QVERIFY(scope->appearance()->useSkin("wide"));
    auto *skinHost = visual(window->contentItem(), "pluginPlaybackSkinHost");
    QVERIFY(skinHost);
    QTRY_VERIFY(skinHost->property("loaded").toBool());
    auto *native = visual(window->contentItem(), "nativePlaybackContents");
    QVERIFY(native);
    QVERIFY(!native->isVisible());
    auto *chrome = visual(window->contentItem(), "playbackWindowChrome");
    QVERIFY(chrome);
    QVERIFY(!chrome->isVisible());
    auto *skinVisual = visual(skinHost, "actualAppearanceVisual");
    auto *skinPart = parts->queryUnique(
        {{"key", "actualAppearanceVisual"}, {"owner", "test.appearance"}, {"surface", "playback"}});
    QVERIFY(skinPart && skinPart->registration()->target() == skinVisual);
    QVERIFY(skinVisual);
    QVERIFY(skinVisual->mapRectToScene(skinVisual->boundingRect()).width() >= 1000);
    QVERIFY(skinVisual->mapRectToScene(skinVisual->boundingRect()).height() >= 700);
    if (!captures.isEmpty())
        QVERIFY(window->grabWindow().save(captures + "/playback-takeover.png"));

    QPointer<QQuickItem> originalSkin = skinVisual;
    auto *wrapper =
        scope->ui()->parts()->contribute("skin-wrapper", {{"role", "playback.page"}},
                                         {{"operation", "wrap"}, {"component", "Wrapper.qml"}});
    QVERIFY(wrapper);
    auto keepWrapper = host.engine.newQObject(wrapper);
    QTRY_VERIFY(visual(window->contentItem(), "appearanceCompositionWrapper"));
    QTRY_VERIFY(skinHost->property("loaded").toBool());
    QVERIFY(originalSkin);
    QCOMPARE(visual(skinHost, "actualAppearanceVisual"), originalSkin.data());
    wrapper->dispose();
    QTRY_VERIFY(!visual(window->contentItem(), "appearanceCompositionWrapper"));
    QTRY_VERIFY(skinHost->property("loaded").toBool());
    QVERIFY(originalSkin);
    QCOMPARE(visual(skinHost, "actualAppearanceVisual"), originalSkin.data());

    QCOMPARE(skinVisual->property("configuration").value<AppearanceConfiguration *>()->values(),
             catalog->skin()->configuration()->values());
    auto *selectedCard = visual(skinCatalog, "pluginLyricSkinCard");
    QVERIFY(selectedCard);
    QVERIFY(QMetaObject::invokeMethod(selectedCard, "chosen"));
    QQuickItem *settingsHost = nullptr;
    QTRY_VERIFY((settingsHost = visual(window->contentItem(), "pluginSkinSettingsHost")));
    QTRY_VERIFY(settingsHost->property("loaded").toBool());
    auto *formVisual = visual(settingsHost, "actualAppearanceVisual");
    QVERIFY(formVisual);
    auto *formConfig = formVisual->property("configuration").value<AppearanceConfiguration *>();
    QVERIFY(formConfig && formConfig->patch({{"color", "#336699"}}));
    QTRY_COMPARE(skinVisual->property("configuration")
                     .value<AppearanceConfiguration *>()
                     ->values()
                     .value("color")
                     .toString(),
                 QString("#336699"));
    QQuickItem *viewport = settingsHost->parentItem();
    while (viewport && !viewport->property("contentY").isValid())
        viewport = viewport->parentItem();
    QVERIFY(viewport);
    QVERIFY(viewport->property("contentHeight").toReal() > viewport->height());
    ulong stamp = 10000;
    const auto wheel = [&](QPoint delta, Qt::ScrollPhase phase) {
        const auto pos = viewport->mapToScene({viewport->width() * .2, viewport->height() * .5});
        QWheelEvent event(pos, window->mapToGlobal(pos.toPoint()), delta, delta * 2, Qt::NoButton,
                          Qt::NoModifier, phase, false);
        event.setTimestamp(stamp += 16);
        QCoreApplication::sendEvent(window, &event);
    };
    wheel({}, Qt::ScrollBegin);
    for (int i = 0; i < 40 && !viewport->property("atYEnd").toBool(); ++i) {
        wheel({0, -100}, Qt::ScrollUpdate);
        QTest::qWait(16);
    }
    wheel({}, Qt::ScrollEnd);
    QTRY_VERIFY(viewport->property("contentY").toReal() > 0);
    QTRY_VERIFY(viewport->property("atYEnd").toBool());
    if (!captures.isEmpty())
        QVERIFY(window->grabWindow().save(captures + "/skin-settings-scrolled.png"));

    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(!skinHost->property("loaded").toBool());
    QVERIFY(showThemeSettings());
    QTRY_VERIFY((themeDialog = root->findChild<QObject *>("appearanceSettingsDialog")));
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QTRY_VERIFY(!miniHost->property("loaded").toBool());
    QTRY_VERIFY(themeDialog.isNull());
}
void PluginRuntimeTest::appearanceErrorsAndActivationRollback()
{
    Host host;
    QSignalSpy teardownWarnings(&host.engine, &QQmlEngine::warnings);
    QSignalSpy failures(&host.runtime, &PluginRuntime::pluginFailed);
    const auto failed = installAppearance(
        host, appearancePackage(host, definitions + "throw new Error('activation rollback');"));
    QVERIFY(failed.isEmpty());
    QVERIFY(!failures.isEmpty());
    QVERIFY(failures.last()[1].toString().contains("activation rollback"));
    QCOMPARE(host.theme.extensions()->themes()->rowCount(), 1);
    QCOMPARE(host.theme.extensions()->skins()->rowCount(), 3);
    const auto second = installAppearance(host,
                                          appearancePackage(host, R"(
 if(c.theme.registerTheme({id:"invalid",title:"Invalid",defaults:{},validate:()=>{throw "bad configuration";}}))throw new Error("primitive throw accepted");
 c.theme.registerTheme({id:'good',title:'Good',miniComponent:'Visual.qml'});
 c.theme.registerSkin({id:'nested',title:'Nested',component:'Nested.qml'});
 c.theme.registerSkin({id:'redirect',title:'Redirect',component:'Redirect.qml'});
 const handle=c.theme.registerSkin({id:'broken',title:'Broken',component:'Bad.qml'});
 if(!handle)throw new Error(c.theme.errorMessage);
)",
                                                            "test.bad-skin"),
                                          "test.bad-skin");
    QCOMPARE(host.runtime.state(second), PluginRuntime::Active);
    ShellSettings settings(host.settings);
    auto root = productionWindow(host, settings);
    QVERIFY(root);
    auto *scope = host.runtime.context(second);
    QVERIFY(scope->appearance()->useSkin("broken"));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "openPlayback"));
    QTRY_COMPARE(host.theme.extensions()->selectedSkin(), QString("cover"));
    QCOMPARE(host.runtime.state(second), PluginRuntime::Active);
    QVERIFY(!host.theme.extensions()->entries("skin").first()->errorMessage().isEmpty());
    QVERIFY(scope->appearance()->useTheme("good"));
    QVERIFY(scope->appearance()->useSkin("nested"));
    QTRY_COMPARE(host.theme.extensions()->selectedSkin(), QString("cover"));
    QVERIFY(host.theme.extensions()
                ->find(second + ":skin:nested")
                ->errorMessage()
                .contains("nested runtime failure"));
    QVERIFY(host.theme.extensions()->theme());
    QVERIFY(host.theme.extensions()->theme()->errorMessage().isEmpty());
    QCOMPARE(host.runtime.state(second), PluginRuntime::Active);
    QVERIFY(scope->appearance()->useSkin("redirect"));
    QTRY_COMPARE(host.theme.extensions()->selectedSkin(), QString("cover"));
    QTest::qWait(20);
    for (const auto &arguments : teardownWarnings)
        for (const auto &warning : arguments.first().value<QList<QQmlError>>())
            QVERIFY2(!warning.description().contains("Cannot read property"),
                     qPrintable(warning.toString()));
    QVERIFY(host.theme.extensions()->find(second + ":skin:redirect")->errorMessage().isEmpty());
}
void PluginRuntimeTest::playbackPresentationAndFloatingSdk()
{
    Host host;
    connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
            host.runtime.permissions(), [&host](const QString &id, const QVariantMap &) {
                host.runtime.permissions()->decide(id, true);
            });
    const auto installation =
        installAppearance(host, appearancePackage(host, "", "test.appearance", true));
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    auto *player = scope->player();
    auto *volume = player->control("volume", {{"value", 64.}});
    QTRY_COMPARE(volume->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.playback.volume(), 64.);
    auto *invalid = player->control("volume", {{"value", 151.}});
    QCOMPARE(invalid->state(), EchoPluginOperation::Failed);
    auto *mode = player->control("mode", {{"value", 2}});
    QTRY_COMPARE(mode->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.playback.playMode(), PlaybackService::Shuffle);
    QCOMPARE(player->snapshot().value("playMode").toInt(), 2);
    QVERIFY(player->lyricSnapshot().contains("lines"));
    QVERIFY(player->snapshot().contains("revision"));
    auto *windows = scope->windows();
    QVERIFY(windows->snapshot().value("available").toBool());
    auto *show = windows->command("desktop", "show");
    QTRY_COMPARE(show->state(), EchoPluginOperation::Succeeded);
    QVERIFY(host.floating.desktopEnabled());
    auto *hide = windows->command("desktop", "hide");
    QTRY_COMPARE(hide->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!host.floating.desktopEnabled());
    const auto base = scope->font();
    const auto font = scope->fonts()->build({"follow"}, 27, 650, 1.2);
    QCOMPARE(font.families(), base.families());
    QCOMPARE(font.pixelSize(), 27);
    QCOMPARE(font.weight(), QFont::Weight(650));
    QObject owner;
    owner.setProperty("active", true);
    EchoPluginViewContext view(*scope, "mini", &owner);
    auto *pending = view.command("volume", {{"value", 70.}});
    owner.setProperty("active", false);
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    QCOMPARE(host.playback.volume(), 64.);
}

void PluginRuntimeTest::appearancePreviewCatalogAndFloatingIsolation()
{
    Host host;
    const auto path = appearancePackage(host, definitions);
    const auto installation = installAppearance(host, path);
    auto *scope = host.runtime.context(installation);
    QVERIFY(scope);
    auto *api = scope->appearance();
    auto *catalog = host.theme.extensions();
    QVERIFY(api->useTheme("ocean"));
    const auto themeKey = catalog->selectedTheme();
    QVERIFY(api->themes().size() >= 2);
    QCOMPARE(api->skins().size(), 4);
    QVERIFY(api->previewTheme("ocean"));
    auto owner = std::make_unique<QObject>();
    auto *child = scope->createPresentation(owner.get(), "preview-test");
    QVERIFY(child);
    QVERIFY(child->appearance()->previewTheme(themeKey));
    api->cancelPreview();
    QVERIFY(catalog->previewing());
    child->appearance()->cancelPreview();
    QVERIFY(!catalog->previewing());
    QVERIFY(catalog->previewTheme(themeKey));
    api->cancelPreview();
    QVERIFY(catalog->previewing());
    catalog->cancelPreview();
    auto disposable = std::make_unique<QObject>();
    auto *transient = scope->createPresentation(disposable.get(), "preview-disposal");
    QVERIFY(transient->appearance()->previewTheme(themeKey));
    disposable.reset();
    QTRY_VERIFY(!catalog->previewing());
    QVERIFY(api->useSkin("lyric"));
    QCOMPARE(catalog->selectedSkin(), "lyric");
    QVERIFY(api->useSkin("portrait"));
    QCOMPARE(catalog->selectedSkin(), "portrait");
    const auto elevated = host.theme.elevated();
    QVERIFY(host.theme.overrides().put(
        "isolation",
        {{"light", QVariantMap{{"floating", QVariantMap{{"background", "#122334"}}},
                               {"mini", QVariantMap{{"background", "#abc123"}}}}},
         {"dark", QVariantMap{{"floating", QVariantMap{{"background", "#122334"}}},
                              {"mini", QVariantMap{{"background", "#abc123"}}}}}},
        0, {}));
    QCOMPARE(host.theme.elevated(), elevated);
    QCOMPARE(host.theme.floatingBackground(), QColor("#122334"));
    QCOMPARE(api->snapshot().value("mini").toMap().value("background").value<QColor>(),
             QColor("#abc123"));
    host.theme.overrides().remove("isolation");
    QCOMPARE(host.theme.floatingBackground(), elevated);
    host.engine.globalObject().setProperty("themeApi", host.engine.newQObject(api));
    auto observer = host.engine.evaluate(
        "var themeSnapshots=[]; themeApi.onSnapshot('audit', s=>themeSnapshots.push(s));");
    QVERIFY(!observer.isError());
    QTRY_COMPARE(host.engine.evaluate("themeSnapshots.length").toInt(), 1);
    QVERIFY(api->useTheme("echo"));
    QTRY_VERIFY(host.engine.evaluate("themeSnapshots.length").toInt() > 1);
    QCOMPARE(host.theme.themeKey(), "echo");
    QCOMPARE(catalog->selectedTheme(), "");
    QVERIFY(api->previewTheme("ocean"));
    QVERIFY(QMetaObject::invokeMethod(&host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(!catalog->previewing());
    QTRY_VERIFY(
        host.engine
            .evaluate(
                "themeSnapshots.length && !themeSnapshots[themeSnapshots.length-1].previewing")
            .toBool());
    const auto count = host.engine.evaluate("themeSnapshots.length").toInt();
    host.theme.setMode(ThemeStore::Dark);
    QTRY_VERIFY(host.engine.evaluate("themeSnapshots.length").toInt() > count);
    owner.reset();
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    const auto stopped = host.engine.evaluate("themeSnapshots.length").toInt();
    host.theme.setMode(ThemeStore::Light);
    QTest::qWait(20);
    QCOMPARE(host.engine.evaluate("themeSnapshots.length").toInt(), stopped);
    QQuickWindow window;
    for (int i = 0; i < 32; ++i) {
        auto *surface = new AppearanceHost(window.contentItem());
        QCOMPARE(surface->window(), &window);
        delete surface;
    }
    QVERIFY(window.contentItem()->childItems().isEmpty());
}

void PluginRuntimeTest::appearanceMaterialsAndRouteLifecycle()
{
    Host host;
    host.theme.setFloatingFrosted(true);
    const QVariantMap visuals{
        {"floating", QVariantMap{{"background", "#122334"}}},
        {"surface", QVariantMap{{"floating", .5}, {"dialog", .25}}},
        {"material",
         QVariantMap{{"floatingFrosted", false}, {"blurSigma", 12}, {"backdropFillOpacity", .6}}},
        {"pageTransition", QVariantMap{{"appear", false},
                                       {"durationMs", 400},
                                       {"easing", QVariantList{.2, 0, .2, 1}},
                                       {"enterY", 24},
                                       {"enterScale", .96},
                                       {"enterOpacity", 0}}}};
    QString error;
    QVERIFY2(EchoPluginAppearance::validateVisuals(visuals, error), qPrintable(error));
    QVERIFY(!EchoPluginAppearance::validateVisuals({{"material", QVariantMap{{"blurSigma", -1}}}},
                                                   error));
    QVERIFY(!EchoPluginAppearance::validateVisuals(
        {{"pageTransition", QVariantMap{{"easing", QVariantList{2, 0, .2, 1}}}}}, error));
    QVERIFY(host.theme.overrides().put("material", {{"light", visuals}, {"dark", visuals}}, 0, {}));
    QVERIFY(!host.theme.floatingFrosted());
    write(host.temporary.path() + "/Route.qml", R"(import QtQuick
import EchoMusic.Shell
Item {
 required property RouteContext context; required property RouteController router; required property ShellSettings settings
 objectName: context.name
 Rectangle { y: 110; width: parent.width; height: parent.height - y; color: context.name === "material-one" ? "steelblue" : "coral" }
}
)");
    const auto route = QUrl::fromLocalFile(host.temporary.path() + "/Route.qml");
    host.router.registerPage({"material-one", "/material-one", route, {}, true, false, false, {}});
    host.router.registerPage({"material-two", "/material-two", route, {}, true, false, false, {}});
    QVERIFY(host.router.navigate("material-one"));
    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
Window {
 id: root
 required property RouteController router
 required property ShellSettings settings
 width: 400; height: 240; visible: true; color: "transparent"
 RouteHost { objectName: "routeHost"; anchors.fill: parent; router: root.router; settings: root.settings }
 EchoFloatingSurface { objectName: "materialPanel"; width: 100; height: 100; radius: 0; dialogShadow: true; shadowEnabled: false }
})",
                      QUrl("qrc:/material-validation.qml"));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{"router", QVariant::fromValue(&host.router)},
         {"settings", QVariant::fromValue(&host.shellSettings)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *panel = window->findChild<QQuickItem *>("materialPanel");
    QVERIFY(panel);
    QCOMPARE(panel->property("blurSigma").toDouble(), 12.);
    QCOMPARE(panel->property("backdropFillOpacity").toDouble(), .6);
    QCOMPARE(panel->property("frostedEnabled").toBool(), false);
    QCOMPARE(panel->property("paintedColor").value<QColor>().red(), 18);
    QVERIFY(qAbs(panel->property("paintedColor").value<QColor>().alphaF() - .25) < .001);
    QTRY_VERIFY(visual(window->contentItem(), "material-one"));
    auto *page = visual(window->contentItem(), "material-one");
    QCOMPARE(page->parentItem()->opacity(), 1.);
    auto *routeHost = window->findChild<QQuickItem *>("routeHost");
    QVERIFY(routeHost->property("presentationLive").toBool());
    if (qEnvironmentVariableIsSet("ECHO_TEST_NATIVE_GPU")) {
        QTest::qWait(60);
        const auto pixels = window->grabWindow();
        QVERIFY(!pixels.isNull());
        const auto pixel = pixels.pixelColor(qRound(50 * window->effectiveDevicePixelRatio()),
                                             qRound(50 * window->effectiveDevicePixelRatio()));
        QVERIFY(qAbs(pixel.alpha() - 64) <= 1);
    }
    QPointer<QQuickItem> first(page);
    QVERIFY(host.router.navigate("material-two"));
    QTRY_VERIFY(visual(window->contentItem(), "material-two"));
    auto *customEntry = visual(window->contentItem(), "material-two")->parentItem();
    QVERIFY(routeTransitioning(customEntry));
    QVERIFY(customEntry->opacity() < 1.); // appear=false only suppresses the initial page.
    QVERIFY(customEntry->mapToItem(routeHost, QPointF()).y() > 0);
    QVERIFY(first); // The short exit transition still owns its visual item.
    QVERIFY(host.router.navigate("material-one"));
    QTRY_COMPARE(visual(window->contentItem(), "material-one"), first.data());
    QTest::qWait(450);
    QVERIFY(first); // A canceled exit must not unload a rapidly reactivated page.
    QVERIFY(host.router.navigate("material-two"));
    QTRY_VERIFY(visual(window->contentItem(), "material-two"));
    QTRY_VERIFY_WITH_TIMEOUT(first.isNull(), 1200);
    QCOMPARE(host.router.mountedCount(), 2);
    QPointer<QQuickItem> outgoing(visual(window->contentItem(), "material-two"));
    QVERIFY(host.router.navigate("material-one"));
    QTRY_VERIFY(visual(window->contentItem(), "material-one"));
    window->hide();
    QTRY_VERIFY(outgoing.isNull());
    QVERIFY(!routeHost->property("presentationLive").toBool());
    auto *second = visual(window->contentItem(), "material-one");
    QCOMPARE(second->parentItem()->opacity(), 1.);
    host.theme.setFloatingFrosted(false);
    host.theme.overrides().remove("material");
    QVERIFY(!host.theme.floatingFrosted());
    QCOMPARE(panel->property("blurSigma").toDouble(), 24.);
    QCOMPARE(panel->property("paintedColor").value<QColor>().alpha(), 255);
    // Revoking a plugin recipe restores entry-only default fading.
    window->show();
    QPointer<QQuickItem> defaultOutgoing(second);
    QVERIFY(host.router.navigate("material-two"));
    QTRY_VERIFY(visual(window->contentItem(), "material-two"));
    auto *defaultEntry = visual(window->contentItem(), "material-two")->parentItem();
    QVERIFY(defaultEntry->property("animateRoute").toBool());
    QVERIFY(!defaultEntry->property("customizedRoute").toBool());
    QCOMPARE(defaultEntry->property("durationMs").toInt(), 220);
    QVERIFY(routeTransitioning(defaultEntry));
    QVERIFY(defaultEntry->opacity() < 1.);
    QVERIFY(defaultEntry->isEnabled());
    QVERIFY(!defaultEntry->property("waitingEnter").toBool());
    QVERIFY(!defaultEntry->property("waitingExit").toBool());
    QTRY_VERIFY(defaultOutgoing.isNull());
    const auto captures = qEnvironmentVariable("ECHO_MOTION_ARTIFACT_DIR");
    if (!captures.isEmpty()) {
        QDir().mkpath(captures);
        QTest::qWait(35);
        QVERIFY(window->grabWindow().save(captures + "/route-default-enter.png"));
    }
    host.theme.setMotionEnabled(false);
    QCOMPARE(defaultEntry->opacity(), 1.);
    QTRY_VERIFY(!routeTransitioning(defaultEntry));
    if (!captures.isEmpty())
        QVERIFY(window->grabWindow().save(captures + "/route-default-after.png"));
    QVERIFY(host.router.navigate("material-one"));
    QTRY_VERIFY(visual(window->contentItem(), "material-one"));
    QCOMPARE(visual(window->contentItem(), "material-one")->parentItem()->opacity(), 1.);
    host.theme.setMotionEnabled(true);
    const auto setMode = [&](const QString &mode) {
        const QVariantMap transition{{"mode", mode},      {"durationMs", 240}, {"enterOpacity", 0},
                                     {"leaveOpacity", 0}, {"enterX", 32},      {"leaveX", -16}};
        const QVariantMap variant{{"pageTransition", transition}};
        QVERIFY(host.theme.overrides().put("motion-mode", {{"light", variant}, {"dark", variant}},
                                           0, {}));
    };
    setMode("out-in");
    QVERIFY(host.router.navigate("material-two"));
    QTRY_VERIFY(visual(window->contentItem(), "material-two"));
    auto *modeEntry = visual(window->contentItem(), "material-two")->parentItem();
    QVERIFY(modeEntry->property("waitingEnter").toBool());
    QTRY_VERIFY(!routeTransitioning(modeEntry));
    setMode("in-out");
    QPointer<QQuickItem> held(visual(window->contentItem(), "material-two"));
    QVERIFY(host.router.navigate("material-one"));
    QTRY_VERIFY(visual(window->contentItem(), "material-one"));
    QVERIFY(held && held->parentItem()->property("waitingExit").toBool());
    QTRY_VERIFY(held.isNull());
    // A canceled incoming page cannot strand an in-out outgoing visual.
    QVERIFY(host.router.navigate("material-two"));
    QVERIFY(host.router.navigate("material-one"));
    QVERIFY(host.router.navigate("material-two"));
    QTRY_VERIFY(visual(window->contentItem(), "material-two"));
    auto *finalEntry = visual(window->contentItem(), "material-two")->parentItem();
    QTRY_VERIFY(!routeTransitioning(finalEntry));
    QTRY_VERIFY(!visual(window->contentItem(), "material-one"));
}
