#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoLyricEffectRegistry.h"
#include "EchoPluginLyricEffects.h"
#include "EchoUiPartMount.h"
void PluginRuntimeTest::ecosystemLyricsNativeContextsStylesAndReclamation()
{
    Host host;
    host.theme.setMotionEnabled(false);
    const auto decoration = QByteArray(R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName:"actualLyricDecoration"
 width:parent ? parent.width : 0
 height:4
 color:plugin.accent
 Component.onCompleted: plugin.setTimer("pulse",10,true,function(){var s=plugin.ui.state;s.ticks=(s.ticks||0)+1;plugin.ui.setState(s);})
})");
    auto *scope = ecosystem(host, R"(
 c.ui.setState({scrolls:0});
 c.lyricEffects.registerEffect('native',{scopes:['page','desktop'],order:20,style:{foreground:'#ff3355',opacity:0.75},component:'Effect.qml',autoScroll:function(v){c.ui.setState({scrolls:c.ui.state.scrolls+1});return false;}});
 )",
                            "effects", decoration);
    QVERIFY(scope);
    host.playback.lyrics()->replace(
        {{"scrollable", true},
         {"offset_ms", 0},
         {"lines",
          QJsonArray{QJsonObject{
              {"text", "Test 歌词"},
              {"start_ms", 0},
              {"duration_ms", 4000},
              {"words",
               QJsonArray{
                   QJsonObject{{"text", "Test "}, {"start_ms", 0}, {"duration_ms", 2000}},
                   QJsonObject{{"text", "歌词"}, {"start_ms", 2000}, {"duration_ms", 2000}}}}}}}});
    auto window = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Playback
import EchoMusic.Theme
ApplicationWindow {id:win;required property PlaybackService playback; width:1100;height:800;visible:true;
PlaybackLyricViewport {colors:Theme.colors;playback:win.playback;width:550;height:700;renderTime:1500;resourceActive:true}
FloatingLyricViewport {x:580;y:100;width:450;height:250;playback:win.playback;resourceActive:true;renderTime:1500}
})",
                                  {{"playback", QVariant::fromValue(&host.playback)}});
    QVERIFY(window);
    auto *win = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(win);
    win->requestActivate();
    auto *parts = scope->ui()->parts();
    QTRY_VERIFY(parts->query({{"role", "lyrics.viewport"}, {"surface", "playback"}}));
    QTRY_VERIFY(parts->query({{"role", "lyrics.viewport"}, {"surface", "desktop"}}));
    auto *page = parts->query({{"role", "lyrics.viewport"}, {"surface", "playback"}});
    auto *desktop = parts->query({{"role", "lyrics.viewport"}, {"surface", "desktop"}});
    auto pageRoot = host.engine.newQObject(page), desktopRoot = host.engine.newQObject(desktop);
    QCOMPARE(page->state().value("scope").toString(), "page");
    QVERIFY(page->state().value("reducedMotion").toBool());
    QCOMPARE(desktop->state().value("scope").toString(), "desktop");
    QTRY_VERIFY(ecosystemVisual(win->contentItem(), "actualLyricDecoration"));
    QPointer<QQuickItem> decorated = ecosystemVisual(win->contentItem(), "actualLyricDecoration");
    QTRY_VERIFY(scope->ui()->state().value("ticks").toInt() > 0);
    win->hide();
    QTRY_VERIFY(decorated.isNull());
    const auto ticks = scope->ui()->state().value("ticks").toInt();
    QTest::qWait(80);
    QCOMPARE(scope->ui()->state().value("ticks").toInt(), ticks);
    win->show();
    QTRY_VERIFY(ecosystemVisual(win->contentItem(), "actualLyricDecoration"));
    QTRY_VERIFY(scope->ui()->state().value("ticks").toInt() > ticks);
    decorated = ecosystemVisual(win->contentItem(), "actualLyricDecoration");
    auto *line = parts->query({{"role", "lyrics.line"}, {"surface", "playback"}});
    QTRY_VERIFY(line = parts->query({{"role", "lyrics.line"}, {"surface", "playback"}}));
    auto lineRoot = host.engine.newQObject(line);
    QCOMPARE(line->visuals().value("foreground").value<QColor>(), QColor("#ff3355"));
    QTRY_VERIFY(!line->state().value("glyphs").toList().isEmpty());
    auto glyph = line->state().value("glyphs").toList().first().toMap();
    QVERIFY(glyph.value("rect").toRectF().width() > 0);
    QCOMPARE(glyph.value("startMs").toLongLong(), qint64(0));
    auto *desktopLine = parts->query({{"role", "lyrics.line"}, {"surface", "desktop"}});
    QVERIFY(desktopLine);
    auto desktopLineRoot = host.engine.newQObject(desktopLine);
    auto *desktopNative = EchoUiPartRegistry::forEngine(host.engine)->find(desktopLine->id());
    QVERIFY(desktopNative);
    QTRY_VERIFY(qAbs(desktopNative->target()->opacity() - .75) < .001);
    QTRY_VERIFY(page = parts->query({{"role", "lyrics.viewport"}, {"surface", "playback"}}));
    QTRY_VERIFY(page->visible());
    pageRoot = host.engine.newQObject(page);
    auto *registry = EchoLyricEffectRegistry::forEngine(host.engine);
    const auto before = scope->ui()->state().value("scrolls").toInt();
    QVERIFY(!registry->requestScroll(page->id(), {{"scope", "page"}, {"browsing", true}}));
    QCOMPARE(scope->ui()->state().value("scrolls").toInt(), before);
    QVERIFY(!registry->requestScroll(
        page->id(),
        {{"scope", "page"}, {"browsing", false}, {"position", 0}, {"reducedMotion", true}}));
    QCOMPARE(scope->ui()->state().value("scrolls").toInt(), before + 1);
    auto *higher = ecosystem(
        host,
        R"(c.ui.setState({calls:0});c.lyricEffects.registerEffect('higher',{order:100,scopes:['page'],style:{foreground:'#112233'},autoScroll:function(v){c.ui.setState({calls:c.ui.state.calls+1});v.host.invoke('scrollTo',{position:v.request.position});return true;}});)",
        "higher-effect");
    QVERIFY(higher);
    QTRY_COMPARE(line->visuals().value("foreground").value<QColor>(), QColor("#112233"));
    const auto lowerCalls = scope->ui()->state().value("scrolls").toInt();
    QVERIFY(registry->requestScroll(
        page->id(),
        {{"scope", "page"}, {"position", 0}, {"browsing", false}, {"reducedMotion", true}}));
    QCOMPARE(scope->ui()->state().value("scrolls").toInt(), lowerCalls);
    QVERIFY(higher->ui()->state().value("calls").toInt() > 0);
    host.runtime.setEnabled(host.runtime.installationForId("higher-effect"), false);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("higher-effect")),
                 PluginRuntime::Disabled);
    QTRY_COMPARE(line->visuals().value("foreground").value<QColor>(), QColor("#ff3355"));
    host.api.advanceSession();
    QTRY_VERIFY(!page->valid());
    QTRY_VERIFY(parts->query({{"role", "lyrics.viewport"}, {"surface", "playback"}}));
    host.playback.lyrics()->replace({{"scrollable", true},
                                     {"offset_ms", 0},
                                     {"lines", QJsonArray{QJsonObject{{"text", "Test 歌词"},
                                                                      {"start_ms", 0},
                                                                      {"duration_ms", 4000},
                                                                      {"words", QJsonArray{}}}}}});
    QTRY_VERIFY(!EchoUiPartRegistry::forEngine(host.engine)
                     ->query({{"role", "lyrics.line"}, {"surface", "playback"}})
                     .isEmpty());
    host.runtime.setEnabled(host.runtime.installationForId("effects"), false);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("effects")),
                 PluginRuntime::Disabled);
    QTRY_VERIFY(decorated.isNull());
    QVERIFY(!registry->hasHandlers("page"));
    QVERIFY(!registry->hasHandlers("desktop"));
    auto *nativeRegistry = EchoUiPartRegistry::forEngine(host.engine);
    const auto lines = nativeRegistry->query({{"role", "lyrics.line"}, {"surface", "playback"}});
    QVERIFY(!lines.isEmpty());
    QVERIFY(!lines.first()->overrides().contains("foreground"));
}
