#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiContribution.h"
#include "EchoPluginUiParts.h"
#include "SettingsKeys.h"
#include "ShellActionModel.h"
#include "LyricParser.h"
namespace
{
std::unique_ptr<QObject> compositionWindow(Host &host)
{
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
ApplicationWindow {
 id: win; width: 600; height: 320; visible: true
 property int value: 7
 EchoUiRow {
  objectName: "compositionRow"; partRole: "test.row"; partKey: "row"
  x: 16; y: 16; width: 400; height: 100; spacing: 8
  EchoUiPartHost {
   objectName: "compositionRegion"; partRole: "test.region"; partKey: "main"
   Layout.preferredWidth: 100; Layout.fillHeight: true
   partState: ({selected: win.value > 0})
   Rectangle { objectName: "compositionNative"; anchors.fill: parent; implicitWidth: 100; implicitHeight: 60; property int value: win.value; color: "#334455"
    EchoUiColumn { partRole: "test.nested"; partKey: "nested"; width: 10; height: 10 }
   }
  }
  EchoButton { objectName: "compositionButton"; text: "Original"; Layout.preferredWidth: 80 }
 }
 EchoUiColumn {
  objectName: "compositionDestination"; partRole: "test.destination"; partKey: "destination"
  x: 16; y: 150; width: 400; height: 100
 }
})",
                      QUrl("qrc:/composition-fixture.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    if (!root)
        qWarning().noquote() << component.errorString();
    return root;
}
QString compositionPlugin(Host &host, const QString &id, const QByteArray &body,
                          const QByteArray &view = {})
{
    const auto source =
        pluginPackage(host.temporary.path(), id, "export function activate(c) { " + body + " }");
    if (!view.isEmpty())
        write(source + "/View.qml", view);
    write(source + "/Broken.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Item { required property EchoPluginContext plugin; required property EchoUiPartRef part; required property string absent }
)");
    write(source + "/RuntimeError.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Item { required property EchoPluginContext plugin; required property EchoUiPartRef part; Component.onCompleted: { throw new Error("construction failed"); } }
)");
    write(source + "/SyntaxError.qml", "Item { @@@ }");
    host.runtime.importDirectory(source);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        const auto installation = host.runtime.installationForId(id);
        if (!host.runtime.loading() && !installation.isEmpty() &&
            host.runtime.state(installation) == PluginRuntime::Active)
            return installation;
        QTest::qWait(1);
    }
    return host.runtime.installationForId(id);
}
const QByteArray insertView = R"(
import QtQuick
import EchoMusic.PluginSdk 1.0
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName: "compositionInserted"
 implicitWidth: 48; implicitHeight: 32; color: "red"
})";
const QByteArray wrapView = R"(
import QtQuick
import QtQuick.Layouts
import EchoMusic.PluginSdk 1.0
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 required property EchoPluginViewContext page
 property bool pageIsActive: page.active
 objectName: "compositionWrapper"
 color: "#aa3344"
 RowLayout {
  anchors.fill: parent; spacing: 0
  EchoUiHostContent { objectName: "compositionRetainedContent"; part: parent.parent.part; Layout.fillWidth: true; Layout.fillHeight: true }
  Rectangle { objectName: "compositionExtraPanel"; Layout.preferredWidth: 24; Layout.fillHeight: true; color: "yellow" }
 }
})";
} // namespace
void PluginRuntimeTest::compositionNativeContainers_data()
{
    QTest::addColumn<QString>("container");
    for (const auto &name :
         {"Row", "Column", "Grid", "Flow", "RowLayout", "ColumnLayout", "GridLayout"})
        QTest::newRow(name) << QString(name);
}
void PluginRuntimeTest::compositionNativeContainers()
{
    QFETCH(QString, container);
    Host host;
    QQmlComponent component(&host.engine);
    const auto qml = QString(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
ApplicationWindow {
 id: win
 width: 600; height: 400; visible: true
 property bool live: true
 property int activated: 0
 Loader { active: win.live; sourceComponent: Component {
 %1 { objectName: "rawContainer"; width: 400; height: 200
  Button { objectName: "rawAnchor"; text: "Native"; implicitWidth: 70; implicitHeight: 32; onClicked: win.activated++ }
  Rectangle { objectName: "rawTail"; width: 30; height: 24; color: "blue" }
 }
 } }
})")
                         .arg(container)
                         .toUtf8();
    component.setData(qml, QUrl("qrc:/native-unmodified-containers.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    const auto baseline = registry->partCount();
    QCOMPARE(registry->structureWatchCount(), 0);
    QCOMPARE(registry->query({{"key", "rawAnchor"}}).size(), 0);
    const auto installation = compositionPlugin(host, "native-container", R"(
 c.ui.parts.contribute('insert',{key:'rawAnchor'},{operation:'insert',position:'after',component:'View.qml',layout:{preferredWidth:48,preferredHeight:32}});
 c.ui.parts.observe('parent',{parentOf:{key:'rawAnchor'}}, event => c.ui.setState({lastParentEvent:event.type}));
)",
                                                insertView);
    QTRY_VERIFY(visual(window->contentItem(), "compositionInserted"));
    auto *part = host.runtime.context(installation)->ui()->parts()->query({{"key", "rawAnchor"}});
    QVERIFY(part && part->automatic());
    QVERIFY(part->actions().contains("activate"));
    auto *invocation = part->invoke("activate");
    QVERIFY(invocation);
    QTRY_VERIFY(invocation->finished());
    QCOMPARE(invocation->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(root->property("activated").toInt(), 1);
    QVERIFY(part->insertionPositions().contains("after"));
    const auto parent = registry->query({{"parentOf", QVariantMap{{"key", "rawAnchor"}}}});
    QCOMPARE(parent.size(), 1);
    QCOMPARE(parent.first()->target()->objectName(), QString("rawContainer"));
    QCOMPARE(parent.first()->layoutEngine(),
             container.endsWith("Layout") ? QString("layout") : QString("positioner"));
    auto *anchor = visual(window->contentItem(), "rawAnchor");
    auto *inserted = visual(window->contentItem(), "compositionInserted");
    QTRY_COMPARE(inserted->width(), 48.);
    QTRY_COMPARE(inserted->height(), 32.);
    const auto children = anchor->parentItem()->childItems();
    auto *cell = inserted;
    while (cell && cell->parentItem() != anchor->parentItem())
        cell = cell->parentItem();
    QVERIFY(cell);
    QCOMPARE(children.indexOf(cell), children.indexOf(anchor) + 1);
    const auto a = anchor->mapRectToScene(anchor->boundingRect());
    const auto b = inserted->mapRectToScene(inserted->boundingRect());
    QVERIFY(!a.intersects(b));
    QPointer<QObject> retiredAnchor(anchor), retiredView(inserted);
    auto rootedRef = host.engine.newQObject(part);
    QVERIFY(root->setProperty("live", false));
    QTRY_VERIFY(retiredAnchor.isNull());
    QTRY_VERIFY(retiredView.isNull());
    QTRY_VERIFY(!part->valid());
    QVERIFY(root->setProperty("live", true));
    QTRY_VERIFY(visual(window->contentItem(), "compositionInserted"));
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(!visual(window->contentItem(), "compositionInserted"));
    QTRY_COMPARE(registry->partCount(), baseline);
    QCOMPARE(registry->indexedTargetCount(), baseline);
    QTRY_COMPARE(registry->structureWatchCount(), 0);
}
void PluginRuntimeTest::compositionNativeStylesRestoreBindings()
{
    Host host;
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
 id: win; width: 500; height: 300; visible: true
 property color base: "#123456"
 property real textSize: 17
 property bool showGroup: false
 Column { objectName: "rawColumn"; spacing: 8; visible: win.showGroup
  Rectangle { objectName: "rawRect"; width: 80; height: 40; color: win.base; radius: 3 }
  Text { objectName: "rawText"; text: "Native 字形"; font.pixelSize: win.textSize; color: win.base }
 }
})",
                      QUrl("qrc:/native-bound-styles.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *rect = visual(window->contentItem(), "rawRect");
    auto *text = visual(window->contentItem(), "rawText");
    auto *column = visual(window->contentItem(), "rawColumn");
    QVERIFY(rect && text && column);
    const auto low = compositionPlugin(host, "native-style-low", R"(
 c.ui.styles.registerRule('paint',{key:'rawRect'},{background:'#ff0000',radius:12},1);
 c.ui.styles.registerRule('text',{key:'rawText'},{foreground:'#ff0000',fontSize:23,preferredWidth:160},1);
 c.ui.styles.registerRule('gap',{parentOf:{key:'rawRect'}},{spacing:16},1);
 c.ui.styles.registerRule('hide',{key:'rawRect'},{visible:false},1);
)");
    QTRY_COMPARE(rect->property("color").value<QColor>(), QColor("#ff0000"));
    QTRY_COMPARE(rect->property("radius").toDouble(), 12.);
    QTRY_COMPARE(text->property("font").value<QFont>().pixelSize(), 23);
    QTRY_COMPARE(text->width(), 160.);
    QTRY_COMPARE(column->property("spacing").toDouble(), 16.);
    const auto high = compositionPlugin(host, "native-style-high", R"(
 c.ui.styles.registerRule('paint',{key:'rawRect'},{background:'#00ff00'},2);
 c.ui.styles.registerRule('natural',{key:'rawText'},{preferredWidth:-1},2);
)");
    QTRY_COMPARE(rect->property("color").value<QColor>(), QColor("#00ff00"));
    QTRY_COMPARE(text->width(), text->implicitWidth());
    QVERIFY(root->setProperty("showGroup", true));
    QVERIFY(!rect->isVisible());
    QVERIFY(root->setProperty("base", QColor("#345678")));
    QVERIFY(root->setProperty("textSize", 19.));
    QVERIFY(host.runtime.setEnabled(high, false));
    QTRY_COMPARE(rect->property("color").value<QColor>(), QColor("#ff0000"));
    QTRY_COMPARE(text->width(), 160.);
    QVERIFY(host.runtime.setEnabled(low, false));
    QTRY_COMPARE(rect->property("color").value<QColor>(), QColor("#345678"));
    QTRY_COMPARE(rect->property("radius").toDouble(), 3.);
    QTRY_VERIFY(rect->isVisible());
    QTRY_COMPARE(text->property("color").value<QColor>(), QColor("#345678"));
    QTRY_COMPARE(text->property("font").value<QFont>().pixelSize(), 19);
    QTRY_COMPARE(text->width(), text->implicitWidth());
    QTRY_COMPARE(column->property("spacing").toDouble(), 8.);
    QVERIFY(root->setProperty("base", QColor("#765432")));
    QTRY_COMPARE(rect->property("color").value<QColor>(), QColor("#765432"));
    QVERIFY(root->setProperty("textSize", 21.));
    QTRY_COMPARE(text->property("font").value<QFont>().pixelSize(), 21);
}
void PluginRuntimeTest::compositionProductionLyricToolbar()
{
    Host host;
    host.theme.setMotionEnabled(false);
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Playback
ApplicationWindow {
 id: win; width: 1000; height: 760; visible: true
 required property PlaybackService playback
 PlaybackPage { playback: win.playback; width: win.width; height: win.height; Component.onCompleted: open() }
})",
                      QUrl("qrc:/unmodified-production-playback.qml"));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"playback", QVariant::fromValue(&host.playback)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    // Match the real application's startup handoff before injecting the lyric
    // document; native initialization must not retire this first presentation.
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
    auto *page = root->findChild<QObject *>("playbackPage");
    QVERIFY(page);
    QTRY_VERIFY(page->property("opened").toBool());
    auto *tools = visual(window->contentItem(), "playbackLyricTools");
    QVERIFY(tools);
    // The real toolbar follows pointer-driven auto-hide. Enter it before
    // requiring a live presentation instead of relying on the previous test's cursor.
    Test::moveNativePointer(window, tools->mapToScene(tools->boundingRect().center()).toPoint());
    QTRY_VERIFY(tools->opacity() > .99);
    const auto installation = compositionPlugin(host, "lyric-toolbar", R"(
 c.ui.parts.contribute('action',{key:'lyricTextAction-translation',surface:'playback'},{operation:'insert',position:'after',component:'View.qml',layout:{preferredWidth:36,preferredHeight:36}});
)",
                                                R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.PluginSdk
EchoSoftButton {
 id: action
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName: "pluginLyricAction"
 implicitWidth: 36; implicitHeight: 36
 text: "新"; leftPadding: 8; rightPadding: 8
 colors: EchoUiPartPalette { part: action.part }
 onClicked: plugin.ui.setState({clicked:true})
})");
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    // The real toolbar is unchanged and uses ordinary nested Columns. A source
    // without translation still allows a sibling action in the live group.
    const auto parsed = LyricParser::parse(
        LyricParser::Source::Document, {{"format", "lrc"}, {"content", "[00:00.00]Native lyrics"}});
    QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
    QVERIFY(host.playback.lyricText()->replace(parsed.lyrics, host.playback.trackId()));
    const auto diagnostic = [&] {
        QVariantList nodes;
        auto *registry = EchoUiPartRegistry::forEngine(host.engine);
        for (auto *part : registry->query({{"key", "lyricTextAction-translation"}})) {
            nodes.append(QVariantMap{{"id", part->identity()},
                                     {"surface", part->surface()},
                                     {"contributions", registry->contributions(part->identity())}});
            for (auto *item = part->target(); item; item = item->parentItem())
                nodes.append(
                    QVariantMap{{"class", QString::fromLatin1(item->metaObject()->className())},
                                {"name", item->objectName()},
                                {"visible", item->isVisible()},
                                {"opacity", item->opacity()},
                                {"width", item->width()},
                                {"height", item->height()},
                                {"x", item->x()},
                                {"y", item->y()}});
        }
        const auto image =
            QString("/tmp/echo-ui-toolbar-failure-%1.png").arg(QCoreApplication::applicationPid());
        window->grabWindow().save(image);
        return QString::fromUtf8(
            QJsonDocument::fromVariant(QVariantMap{{"nodes", nodes},
                                                   {"lyrics", host.playback.lyrics()->rowCount()},
                                                   {"engineReady", host.playback.engineReady()},
                                                   {"exposed", window->isExposed()},
                                                   {"image", image}})
                .toJson());
    };
    QTRY_VERIFY2(visual(window->contentItem(), "pluginLyricAction"), qPrintable(diagnostic()));
    auto *button = visual(window->contentItem(), "pluginLyricAction");
    QTRY_VERIFY(button->isVisible());
    QTRY_COMPARE(button->width(), 36.);
    auto *anchor = visual(window->contentItem(), "lyricTextAction-translation");
    QVERIFY(anchor && !anchor->isVisible());
    auto *group = anchor->parentItem();
    const auto beforeHeight = group->height();
    QVERIFY(group->inherits("QQuickColumn"));
    auto *scope = host.runtime.context(installation);
    auto *parts = scope->ui()->parts();
    auto *ref = parts->query({{"key", "lyricTextAction-translation"}, {"surface", "playback"}});
    QVERIFY(ref);
    QVERIFY(!ref->palette().isEmpty());
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    auto *layout = registry->find(ref->parentId());
    QVERIFY(layout && layout->automatic() && layout->key().startsWith('@'));
    registry->setPreference(layout->address(), {"@host"});
    QCOMPARE(registry->preference(layout->address()), QStringList({"@host"}));
    const auto accounts = host.settings.value(SettingsKeys::Plugins::UiComposition).toMap();
    QVERIFY(accounts.contains("0"));
    QVERIFY(!accounts.value("0").toMap().value("order").toMap().contains(layout->address()));
    registry->setPreference(layout->address(), {});
    QCOMPARE(button->property("colors").value<QObject *>()->property("text"),
             ref->palette().value("text"));
    const auto point = button->mapToScene(button->boundingRect().center()).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_VERIFY(scope->ui()->state().value("clicked").toBool());
    QPointer<QQuickItem> retired(button);
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(retired.isNull());
    QTRY_VERIFY(group->height() < beforeHeight);
    QVERIFY(!anchor->isVisible());
}
void PluginRuntimeTest::compositionSemanticTreeAndLayoutRestore()
{
    Host host;
    auto root = compositionWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QTRY_VERIFY(visual(window->contentItem(), "compositionNative"));
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    auto parts = registry->query({{"role", "test.region"}});
    QCOMPARE(parts.size(), 1);
    auto *part = parts.first();
    QVERIFY(!part->parentId().isEmpty());
    QCOMPARE(registry->find(part->parentId())->role(), QString("test.row"));
    QVERIFY(part->nativeFactory());
    auto *region = visual(window->contentItem(), "compositionRegion");
    QTRY_COMPARE(region->width(), 100.);
    const auto plugin = compositionPlugin(host, "layout-probe", R"(
 var layer=c.ui.parts.contribute('size',{role:'test.region'},{operation:'layout',layout:{preferredWidth:180}});
 if(!layer) throw new Error(c.ui.parts.errorMessage);
)");
    QCOMPARE(host.runtime.state(plugin), PluginRuntime::Active);
    QTRY_COMPARE(region->width(), 180.);
    QVERIFY(host.runtime.setEnabled(plugin, false));
    QTRY_COMPARE(region->width(), 100.);
}
void PluginRuntimeTest::compositionInsertionParticipatesInLayout()
{
    Host host;
    auto root = compositionWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *button = visual(window->contentItem(), "compositionButton");
    QVERIFY(button);
    QTRY_VERIFY(button->x() > 0);
    const auto before = button->x();
    const auto plugin = compositionPlugin(host, "insert-probe", R"(
 var layer=c.ui.parts.contribute('insert',{role:'test.region'},{operation:'insert',position:'after',component:'View.qml',layout:{preferredWidth:48,fillHeight:true}});
 if(!layer) throw new Error(c.ui.parts.errorMessage);
)",
                                          insertView);
    QCOMPARE(host.runtime.state(plugin), PluginRuntime::Active);
    QTRY_VERIFY(visual(window->contentItem(), "compositionInserted"));
    auto *region = visual(window->contentItem(), "compositionRegion");
    QTRY_COMPARE(region->width(), 100.);
    auto *inserted = visual(window->contentItem(), "compositionInserted");
    const auto insertedRect = [inserted] {
        return inserted->mapRectToScene(inserted->boundingRect());
    };
    QTRY_COMPARE(inserted->width(), 48.);
    QTRY_VERIFY(insertedRect().left() >= region->mapToScene(QPointF(region->width(), 0)).x());
    QTRY_VERIFY(button->mapToScene(QPointF()).x() >= insertedRect().right());
    QVERIFY(host.runtime.setEnabled(plugin, false));
    QTRY_VERIFY(!visual(window->contentItem(), "compositionInserted"));
    QTRY_COMPARE(button->x(), before);
}
void PluginRuntimeTest::compositionWrapPreservesNativeContentAndRetires()
{
    Host host;
    auto root = compositionWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QTRY_VERIFY(visual(window->contentItem(), "compositionNative"));
    const auto plugin = compositionPlugin(host, "wrap-probe", R"(
 var layer=c.ui.parts.contribute('wrap',{role:'test.region'},{operation:'wrap',component:'View.qml',priority:20});
 if(!layer) throw new Error(c.ui.parts.errorMessage);
)",
                                          wrapView);
    QCOMPARE(host.runtime.state(plugin), PluginRuntime::Active);
    QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
    QTRY_VERIFY(visual(window->contentItem(), "compositionNative"));
    QTRY_VERIFY(
        visual(window->contentItem(), "compositionWrapper")->property("pageIsActive").toBool());
    auto *native = visual(window->contentItem(), "compositionNative");
    QVERIFY(root->setProperty("value", 29));
    QTRY_COMPARE(native->property("value").toInt(), 29);
    QPointer<QQuickItem> oldWrapper = visual(window->contentItem(), "compositionWrapper");
    window->hide();
    QTRY_VERIFY(oldWrapper.isNull());
    window->show();
    QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    registry->setPreference(
        registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address(), {"@host"});
    QTRY_VERIFY(!visual(window->contentItem(), "compositionWrapper"));
    QTRY_VERIFY(visual(window->contentItem(), "compositionNative"));
    registry->setPreference(
        registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address(), {});
    QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
    QVERIFY(host.runtime.setEnabled(plugin, false));
    QTRY_VERIFY(!visual(window->contentItem(), "compositionWrapper"));
    QTRY_VERIFY(visual(window->contentItem(), "compositionNative"));
}
void PluginRuntimeTest::compositionMoveConflictAndRestoration()
{
    Host host;
    auto root = compositionWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *region = visual(window->contentItem(), "compositionRegion");
    QVERIFY(region);
    QPointer<QQuickItem> original = region->parentItem();
    const auto plugin = compositionPlugin(host, "move-probe", R"(
 var origin=c.ui.parts.query({role:'test.row'});
 var layer=c.ui.parts.contribute('move',{role:'test.region',parentId:origin.id},{operation:'move',destination:{role:'test.destination'},layout:{preferredHeight:60,fillWidth:true}});
 if(!layer) throw new Error(c.ui.parts.errorMessage);
)");
    QCOMPARE(host.runtime.state(plugin), PluginRuntime::Active);
    QTRY_COMPARE(region->parentItem()->objectName(), QString("compositionDestination"));
    QTest::qWait(100);
    QCOMPARE(region->parentItem()->objectName(), QString("compositionDestination"));
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    QCOMPARE(registry->query({{"role", "test.region"}}).size(), 1);
    const auto secondary = compositionPlugin(host, "move-secondary", R"(
      c.ui.parts.contribute('move',{role:'test.region'},{operation:'move',destination:{role:'test.row'},priority:-10});
    )");
    QCOMPARE(host.runtime.state(secondary), PluginRuntime::Active);
    QTRY_COMPARE(region->parentItem()->objectName(), QString("compositionDestination"));
    registry->setPreference(
        registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address(),
        {"move-secondary:move"});
    QTRY_COMPARE(region->parentItem(), original.data());
    registry->setPreference(
        registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address(), {});
    QTRY_COMPARE(region->parentItem()->objectName(), QString("compositionDestination"));
    const auto regionPart = registry->query({{"role", "test.region"}}).first();
    const auto nested = registry->query({{"role", "test.nested"}});
    QCOMPARE(nested.size(), 1);
    QVERIFY(!registry->putMove("cycle", regionPart->identity(), nested.first()->identity(),
                               "append", "probe", "cycle", 999));
    registry->setEnabled(false);
    QTRY_COMPARE(region->parentItem(), original.data());
    registry->setEnabled(true);
    QTRY_COMPARE(region->parentItem()->objectName(), QString("compositionDestination"));
    QVERIFY(host.runtime.setEnabled(plugin, false));
    QTRY_COMPARE(region->parentItem(), original.data());
    QVERIFY(host.runtime.setEnabled(secondary, false));
    QTRY_COMPARE(region->parentItem(), original.data());
    const auto batchPlugin = compositionPlugin(host, "batch-move", "", wrapView);
    auto *batchScope = host.runtime.context(batchPlugin);
    QVERIFY(batchScope);
    auto moveEntry = [](const QString &id, const QString &source, const QString &destination) {
        return QVariantMap{
            {"id", id},
            {"selector", QVariantMap{{"role", source}}},
            {"definition", QVariantMap{{"operation", "move"},
                                       {"destination", QVariantMap{{"role", destination}}}}}};
    };
    QVariantList initial{moveEntry("region", "test.region", "test.destination")};
    auto *group = batchScope->ui()->parts()->applyBatch("move", initial);
    QVERIFY(group);
    QTRY_VERIFY(!group->pending());
    QVERIFY2(group->errorMessage().isEmpty(), qPrintable(group->errorMessage()));
    QTRY_COMPARE(region->parentItem()->objectName(), QString("compositionDestination"));
    const auto revision = group->revision();
    QVERIFY(!group->update({moveEntry("a", "test.row", "test.destination"),
                            moveEntry("b", "test.destination", "test.row")}));
    QVERIFY(group->errorMessage().contains("cycle"));
    QCOMPARE(group->revision(), revision);
    QCOMPARE(region->parentItem()->objectName(), QString("compositionDestination"));
    QVERIFY(group->update({QVariantMap{
        {"id", "prepare"},
        {"selector", QVariantMap{{"role", "test.region"}}},
        {"definition", QVariantMap{{"operation", "wrap"}, {"component", "View.qml"}}}}}));
    QVERIFY(group->pending());
    QVERIFY(host.runtime.setEnabled(batchPlugin, false));
    QTRY_COMPARE(host.runtime.state(batchPlugin), PluginRuntime::Disabled);
    QTRY_COMPARE(region->parentItem(), original.data());
    QVERIFY(!group->active());
    QVERIFY(!group->pending());
    auto *destination = visual(window->contentItem(), "compositionDestination");
    QVERIFY(destination);
    destination->setVisible(false);
    const auto scoped = compositionPlugin(host, "host", R"(
      c.ui.parts.contribute('self-scope',{role:'test.region'},{operation:'wrap',component:'View.qml'});
    )",
                                          R"(
import QtQuick
import EchoMusic.PluginSdk
Item {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 EchoUiHostContent { part: parent.part; anchors.fill: parent }
 Timer {
  interval: 1; running: !parent.plugin.ui.state.moved
  onTriggered: {
   parent.plugin.ui.setState({moved:true});
   parent.plugin.ui.parts.contribute('scoped-move',{role:'test.region'},{operation:'move',destination:{role:'test.destination'}});
  }
 }
})");
    QCOMPARE(host.runtime.state(scoped), PluginRuntime::Active);
    QTRY_VERIFY(host.runtime.context(scoped)->ui()->state().value("moved").toBool());
    QTRY_COMPARE(region->parentItem(), original.data());
    QVERIFY(host.runtime.setEnabled(scoped, false));
    QTRY_COMPARE(host.runtime.state(scoped), PluginRuntime::Disabled);
    QCOMPARE(region->parentItem(), original.data());
}

void PluginRuntimeTest::compositionRevisionRollbackAndReplacementLayers()
{
    Host host;
    auto root = compositionWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QTRY_VERIFY(visual(window->contentItem(), "compositionNative"));
    QPointer<QQuickItem> original = visual(window->contentItem(), "compositionNative");
    const auto installation = compositionPlugin(host, "revision-probe", "", wrapView);
    auto *scope = host.runtime.context(installation);
    QVERIFY(scope);
    auto *contribution = scope->ui()->parts()->contribute(
        "wrapper", {{"role", "test.region"}}, {{"operation", "wrap"}, {"component", "View.qml"}});
    QVERIFY(contribution);
    auto retained = host.engine.newQObject(contribution);
    QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
    QCOMPARE(visual(window->contentItem(), "compositionNative"), original.data());
    QPointer<QQuickItem> wrapper = visual(window->contentItem(), "compositionWrapper");
    QVERIFY(contribution->update({{"operation", "wrap"}, {"component", "SyntaxError.qml"}}));
    QTRY_VERIFY(!contribution->errorMessage().isEmpty());
    QCOMPARE(visual(window->contentItem(), "compositionWrapper"), wrapper.data());
    QVERIFY(contribution->update({{"operation", "wrap"}, {"component", "Broken.qml"}}));
    QTRY_VERIFY(contribution->errorMessage().contains("restored"));
    QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
    QVERIFY(original);
    QCOMPARE(visual(window->contentItem(), "compositionNative"), original.data());
    QCOMPARE(contribution->definition().value("component").toString(), QString("View.qml"));
    QVERIFY(contribution->update({{"operation", "wrap"}, {"component", "RuntimeError.qml"}}));
    QTRY_VERIFY(contribution->errorMessage().contains("restored"));
    QTRY_COMPARE(contribution->definition().value("component").toString(), QString("View.qml"));
    QVERIFY(original);
    // A replace chooses one base; all wrappers still compose around it.
    const auto replacement = compositionPlugin(host, "replacement-probe", R"(
      c.ui.parts.contribute('base',{role:'test.region'},{operation:'replace',component:'View.qml',priority:100});
    )",
                                               insertView);
    QTRY_VERIFY(visual(window->contentItem(), "compositionInserted"));
    QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
    QVERIFY(original && !original->isVisible());
    QVERIFY(host.runtime.setEnabled(replacement, false));
    QTRY_VERIFY(original && original->isVisible());
    QCOMPARE(visual(window->contentItem(), "compositionNative"), original.data());
    contribution->dispose();
    QTRY_VERIFY(!visual(window->contentItem(), "compositionWrapper"));
    QCOMPARE(visual(window->contentItem(), "compositionNative"), original.data());
}

void PluginRuntimeTest::compositionStateAccountAndPreferences()
{
    Host host;
    auto root = compositionWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    const auto installation = compositionPlugin(host, "conditional-probe", "", insertView);
    auto *parts = host.runtime.context(installation)->ui()->parts();
    auto *contribution =
        parts->contribute("hide", {{"role", "test.region"}},
                          {{"operation", "hide"}, {"when", QVariantMap{{"selected", true}}}});
    QVERIFY(contribution);
    auto retained = host.engine.newQObject(contribution);
    auto *region = visual(window->contentItem(), "compositionRegion");
    QVERIFY(region);
    QTRY_VERIFY(!region->isVisible());
    root->setProperty("value", -1);
    QTRY_VERIFY(region->isVisible());
    QVERIFY(!parts->contribute(
        "bad", {}, {{"operation", "layout"}, {"layout", QVariantMap{{"rowSpan", 1.5}}}}));
    QVERIFY(!parts->contribute(
        "bad", {},
        {{"operation", "layout"},
         {"layout", QVariantMap{{"minimumWidth", 200}, {"maximumWidth", 100}}}}));
    QVERIFY(!parts->contribute("bad", {}, {{"operation", "layout"}, {"layout", "not a map"}}));
    registry->setPreference(
        registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address(), {"@host"});
    auto accountMap = host.settings.value(SettingsKeys::Plugins::UiComposition).toMap();
    QVERIFY(accountMap.contains("0"));
    // Publish an account through the production account/API adoption path.
    auto *oldRef = parts->query({{"role", "test.region"}});
    QVERIFY(oldRef);
    auto oldRefRoot = host.engine.newQObject(oldRef);
    QVERIFY(host.account.adoptLoginAuthorization(R"({"user_id":1001,"token":"fixture-token"})"));
    QTRY_VERIFY(
        registry
            ->preference(
                registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address())
            .isEmpty());
    QTRY_VERIFY(!oldRef->valid());
    registry->setPreference(
        registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address(),
        {"conditional-probe:hide"});
    host.account.signOut();
    QTRY_COMPARE(
        registry->preference(
            registry->query({{"role", "test.region"}, {"key", "main"}}).first()->address()),
        QStringList({"@host"}));
    accountMap = host.settings.value(SettingsKeys::Plugins::UiComposition).toMap();
    QVERIFY(accountMap.contains("1001"));
    QVERIFY(accountMap.value("1001").toMap() != accountMap.value("0").toMap());
    QVERIFY(host.runtime.setEnabled(installation, false));
}

void PluginRuntimeTest::compositionProductionRegionsAndInspector()
{
    Host host;
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Plugins
ApplicationWindow {
 id: win; width: 1100; height: 800; visible: true
 required property ShellController shell
 required property RouteController router
 required property ShellSettings settings
 property real uiScale: 1
 PlayerBar { width: parent.width; height: 88; controller: win.shell; router: win.router; uiScale: win.uiScale }
 function openInspector() { owner.open(); }
 function closeInspector() { owner.close(); }
 EchoPopupLoader { id: owner; hostItem: win.contentItem; sourceComponent: Component { PluginUiInspectorDialog { uiScale: win.uiScale } } }
})",
                      QUrl("qrc:/composition-production.qml"));
    auto root = std::unique_ptr<QObject>(component.createWithInitialProperties(
        {{"shell", QVariant::fromValue(&host.shell)},
         {"router", QVariant::fromValue(&host.router)},
         {"settings", QVariant::fromValue(&host.shellSettings)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    QTRY_COMPARE(registry->query({{"role", "shell.playerbar"}}).size(), 1);
    QVERIFY(registry->query({{"role", "shell.playerbar"}}).first()->nativeFactory());
    QVERIFY(!registry->query({{"role", "layout.playerbar"}}).isEmpty());
    host.theme.setMotionEnabled(false);
    auto *bar = visual(window->contentItem(), "playerbar");
    QVERIFY(bar);
    auto *factory = qobject_cast<EchoUiRetainedContentFactory *>(
        registry->query({{"role", "shell.playerbar"}}).first()->nativeFactory());
    QVERIFY(factory && factory->content());
    QCOMPARE(factory->content()->parentItem(), bar);
    QTRY_COMPARE(bar->width(), double(window->width()));
    QTRY_COMPARE(visual(bar, "playerbarLeftArea")->width(), bar->property("sideWidth").toDouble());
    QList<QPair<QPointer<QQuickItem>, QRectF>> geometry;
    for (const auto &name : {"playerbarLeftArea", "playerbarCenterArea", "playerbarRightArea",
                             "playerbarProgressRow"}) {
        auto *item = visual(bar, name);
        QVERIFY(item);
        QTRY_VERIFY(item->width() > 0);
        geometry.append({item, item->mapRectToScene(item->boundingRect())});
    }
    QTRY_VERIFY(visual(bar, "playerbar-action-play-toggle"));
    QVERIFY(QMetaObject::invokeMethod(bar, "updateCapacity"));
    // Cocoa incubates ListView delegates after model/capacity changes. Measure
    // the complete native action tree before testing contribution retirement.
    for (const auto &name :
         {"playerbarLeftActions", "playerbarCenterActions", "playerbarRightActions"}) {
        auto *strip = visual(bar, name);
        QVERIFY(strip);
        QVERIFY(QMetaObject::invokeMethod(strip, "forceLayout"));
        auto *model =
            qobject_cast<QAbstractItemModel *>(strip->property("model").value<QObject *>());
        QVERIFY(model);
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto id = model->data(model->index(row, 0), ShellActionModel::IdRole).toString();
            QTRY_VERIFY(visual(strip, "playerbar-action-" + id));
        }
    }
    const auto baselineIndex = registry->indexedTargetCount();
    QSet<QString> baselineIds;
    for (auto *part : registry->query({}))
        baselineIds.insert(part->identity());
    const auto baselineParts = registry->partCount();
    const auto installation = compositionPlugin(host, "production-wrap", R"(
      c.ui.parts.contribute('bar',{role:'shell.playerbar'},{operation:'wrap',component:'View.qml'});
    )",
                                                wrapView);
    QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "openInspector"));
    QTRY_VERIFY(visual(window->contentItem(), "uiInspectorNodes"));
    QPointer<QQuickItem> inspector = visual(window->contentItem(), "uiInspectorNodes");
    for (const auto &node : registry->snapshot())
        QVERIFY(node.toMap().value("surface") != "inspector");
    QVERIFY(QMetaObject::invokeMethod(root.get(), "closeInspector"));
    QTRY_VERIFY(inspector.isNull());
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(!visual(window->contentItem(), "compositionWrapper"));
    QVERIFY(visual(window->contentItem(), "playerbar"));
    QTRY_COMPARE(factory->content()->parentItem(), bar);
    for (const auto &entry : geometry) {
        QVERIFY(entry.first);
        QTRY_COMPARE(entry.first->mapRectToScene(entry.first->boundingRect()), entry.second);
    }
    if (registry->partCount() != baselineParts)
        for (auto *part : registry->query({}))
            if (!baselineIds.contains(part->identity()))
                qInfo() << "Additional part" << part->role() << part->key() << part->surface()
                        << part->owner();
    QTRY_COMPARE(registry->partCount(), baselineParts);
    QTRY_COMPARE(registry->indexedTargetCount(), baselineIndex);
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    // Repeated scope creation/revocation must leave only the original host tree.
    for (int cycle = 0; cycle < 10; ++cycle) {
        QVERIFY(host.runtime.setEnabled(installation, true));
        QTRY_VERIFY(visual(window->contentItem(), "compositionWrapper"));
        const auto nativePart = registry->query({{"role", "layout.playerbar"}});
        for (auto *part : nativePart)
            QCOMPARE(part->owner(), QString("@host"));
        QVERIFY(host.runtime.setEnabled(installation, false));
        QTRY_VERIFY(!visual(window->contentItem(), "compositionWrapper"));
        QTRY_COMPARE(registry->partCount(), baselineParts);
        QTRY_COMPARE(registry->indexedTargetCount(), baselineIndex);
        QTRY_COMPARE(factory->content()->parentItem(), bar);
        QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    }
}
