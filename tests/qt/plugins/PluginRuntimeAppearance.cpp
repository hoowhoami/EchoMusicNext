#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginAppearance.h"
#include "EchoPluginViewContext.h"
#include "AppearanceHost.h"
#include "EchoUiPartRegistry.h"
#include <QWheelEvent>
namespace
{
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
QString appearancePackage(Host &host, const QByteArray &body, const QString &id = "test.appearance")
{
    const auto path =
        pluginPackage(host.temporary.path(), id, "export function activate(c) {" + body + "}",
                      {{"capabilities", QJsonArray{"theme", "lyricsPage"}}});
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
        if (!installation.isEmpty() && !host.runtime.loading() &&
            (host.runtime.state(installation) == PluginRuntime::Active ||
             host.runtime.state(installation) == PluginRuntime::Failed))
            break;
        QTest::qWait(1);
    }
    return installation;
}
QByteArray definitions = R"(
 const theme=c.theme.registerTheme({id:'ocean',title:'海洋',description:'Actual theme',
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
    QCOMPARE(catalog->skins()->rowCount(), 3);
    QVERIFY(catalog->theme() == nullptr);
    QVERIFY(catalog->skin() == nullptr);
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
    auto *miniCard = miniHost->parentItem();
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
    QTRY_COMPARE(skinCatalog->property("count").toInt(), 3);
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
    QVERIFY(skinVisual);
    QVERIFY(skinVisual->mapRectToScene(skinVisual->boundingRect()).width() >= 1000);
    QVERIFY(skinVisual->mapRectToScene(skinVisual->boundingRect()).height() >= 700);
    if (!captures.isEmpty())
        QVERIFY(window->grabWindow().save(captures + "/playback-takeover.png"));

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
    const auto failed = installAppearance(
        host, appearancePackage(host, definitions + "throw new Error('activation rollback');"));
    QTRY_COMPARE(host.runtime.state(failed), PluginRuntime::Failed);
    QCOMPARE(host.theme.extensions()->themes()->rowCount(), 1);
    QCOMPARE(host.theme.extensions()->skins()->rowCount(), 2);
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
    QSignalSpy teardownWarnings(&host.engine, &QQmlEngine::warnings);
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
    const auto installation = installAppearance(host, appearancePackage(host, ""));
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
