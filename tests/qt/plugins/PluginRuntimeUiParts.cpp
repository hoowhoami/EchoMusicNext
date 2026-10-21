#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginUiStyles.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartMount.h"
#include <QElapsedTimer>
namespace
{
QByteArray partWindow = R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
ApplicationWindow {
 id: win; width: 600; height: 320; visible: true
 property color baseColor: "#112233"
 property string resourceKey: "song-a"
 property int activationCount: 0
 property real sampleScale: 1
 property real clippedOffset: 0
 color: Theme.main
 Item { width: win.width / win.sampleScale; height: win.height / win.sampleScale; scale: win.sampleScale; transformOrigin: Item.TopLeft
 RowLayout {
  id: layout; x: 24; y: 24; spacing: 8
  EchoButton { objectName: "partButton"; text: "播放 Play"; foreground: win.baseColor; accent: false; onClicked: win.activationCount++ }
  EchoButton { objectName: "partOtherButton"; text: "其它"; accent: false }
 }
 Item {
  x: 440; y: 120; width: 140; height: 50; clip: true
  EchoUiPartHost { objectName: "clippedPublicHost"; partRole: "page.clip"; partKey: "clip"; width: 140; height: 50; y: win.clippedOffset }
 }
 Row {
  x: 24; y: 270; spacing: 8
  EchoSwitch { objectName: "partSwitch"; checked: true }
  EchoCheckBox { objectName: "partCheck"; checked: true }
  EchoSelect { objectName: "partSelect"; options: [{label:"选项",value:"one"}]; value: "one" }
 }
 EchoIconButton { objectName: "partIcon"; glyph: "star"; text: "Style"; enabled: false; x: 500; y: 24 }
 EchoUiPartHost {
  id: host; objectName: "publicHost"; x: 24; y: 100; width: 400; height: 150
  partRole: "page.test"; partKey: win.resourceKey
  Rectangle { objectName: "originalPartBody"; anchors.fill: parent; color: win.baseColor }
 }
 }
})";
std::unique_ptr<QObject> createPartWindow(Host &host)
{
    QQmlComponent component(&host.engine);
    component.setData(partWindow, QUrl("qrc:/ui-parts-production-controls.qml"));
    auto *root = component.create();
    if (!root)
        qWarning().noquote() << component.errorString();
    return std::unique_ptr<QObject>(root);
}
QString activatePartPlugin(Host &host, const QString &id, const QByteArray &body,
                           const QByteArray &component = {})
{
    const auto source =
        pluginPackage(host.temporary.path(), id, "export function activate(c) { " + body + " }");
    if (!component.isEmpty())
        write(source + "/Mount.qml", component);
    host.runtime.importDirectory(source);
    QElapsedTimer elapsed;
    elapsed.start();
    QString installation;
    while (elapsed.elapsed() < 5000) {
        installation = host.runtime.installationForId(id);
        if (!installation.isEmpty() &&
            (host.runtime.state(installation) == PluginRuntime::Active ||
             host.runtime.state(installation) == PluginRuntime::Failed) &&
            !host.runtime.loading())
            break;
        QTest::qWait(1);
    }
    return installation;
}
QByteArray mountComponent = R"(
import QtQuick
import QtQuick.Layouts
import EchoMusic.PluginSdk 1.0
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName: "publicPartMountedVisual"
 implicitWidth: 32; implicitHeight: 24
 color: "#ff0066"
})";
} // namespace
void PluginRuntimeTest::uiStyleLayersRestoreLiveBindings()
{
    Host host;
    host.theme.setMotionEnabled(false);
    auto root = createPartWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *button = visual(window->contentItem(), "partButton");
    QVERIFY(button);
    auto *label = visual(button, "buttonLabel");
    QVERIFY(label);
    auto *background = button->property("background").value<QQuickItem *>();
    QVERIFY(background);
    const auto baseRadius = background->property("radius").toDouble();
    const auto a = activatePartPlugin(host, "test.style-a", R"(
 const rule = c.ui.styles.registerRule('paint', {role:'control.button',key:'partButton'}, {foreground:'#006633',radius:18}, 5);
 if (!rule) throw new Error(c.ui.styles.errorMessage);
 c.registerCommand('patch', function() { return rule.update({foreground:'#005599',radius:20}); });
)");
    QCOMPARE(host.runtime.state(a), PluginRuntime::Active);
    QTRY_COMPARE(label->property("color").value<QColor>(), QColor("#006633"));
    QTRY_COMPARE(background->property("radius").toDouble(), 18.);
    const auto b = activatePartPlugin(host, "test.style-b", R"(
 const rule = c.ui.styles.registerRule('paint', {role:'control.button',key:'partButton'}, {foreground:'#990055'}, 10);
 if (!rule) throw new Error(c.ui.styles.errorMessage);
)");
    QCOMPARE(host.runtime.state(b), PluginRuntime::Active);
    QTRY_COMPARE(label->property("color").value<QColor>(), QColor("#990055"));
    QCOMPARE(background->property("radius").toDouble(), 18.); // per-property, not whole-rule winner
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (const qreal scale : {1., 1.25}) {
            host.theme.setMode(mode);
            root->setProperty("sampleScale", scale);
            QTest::qWait(80);
            const auto image = window->grabWindow();
            QVERIFY(!image.isNull());
            const qreal ratio = image.width() / qreal(window->width());
            const auto area = label->mapRectToScene(label->boundingRect());
            const auto pixels = QRectF(area.topLeft() * ratio, area.size() * ratio)
                                    .toAlignedRect()
                                    .intersected(image.rect());
            int ink = 0;
            for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                for (int x = pixels.left(); x <= pixels.right(); ++x) {
                    const auto color = image.pixelColor(x, y);
                    if (qAbs(color.red() - 153) + color.green() + qAbs(color.blue() - 85) < 45)
                        ++ink;
                }
            QVERIFY2(ink > 10, "The real shared control must paint the winning plugin color");
            const auto directory = qEnvironmentVariable("ECHO_UI_PARTS_CAPTURE_DIR");
            if (!directory.isEmpty()) {
                QDir().mkpath(directory);
                QVERIFY(
                    image.save(directory + QString("/layers-%1-%2.png").arg(int(mode)).arg(scale)));
            }
        }
    root->setProperty("sampleScale", 1.);
    auto *scopeB = host.runtime.context(b);
    auto *oldRule = scopeB->findChild<EchoUiStyleRule *>();
    QVERIFY(oldRule);
    auto keepOldRule = host.engine.newQObject(oldRule);
    auto *replacementRule = scopeB->ui()->styles()->registerRule(
        "paint", {{"role", "control.button"}, {"key", "partButton"}}, {{"foreground", "#990055"}},
        5);
    QVERIFY(replacementRule);
    QVERIFY(!oldRule->active());
    oldRule->dispose();
    // Equal priority is independent of completion time: owner test.style-b wins.
    QTRY_COMPARE(label->property("color").value<QColor>(), QColor("#990055"));
    QCOMPARE(EchoUiPartRegistry::forEngine(host.engine)->styleCount(), 2);
    auto *other = visual(window->contentItem(), "partOtherButton");
    QVERIFY(other);
    auto *otherLabel = visual(other, "buttonLabel");
    QVERIFY(otherLabel);
    auto *themeRule = scopeB->ui()->styles()->registerRule(
        "theme-font", {{"key", "partOtherButton"}},
        {{"foreground", "#c05a00"}, {"fontSize", 24}, {"fontWeight", 700}, {"letterSpacing", 1.5}});
    QVERIFY(themeRule);
    QTRY_COMPARE(otherLabel->property("font").value<QFont>().pixelSize(), 24);
    host.theme.setMode(ThemeStore::Light);
    host.theme.setMode(ThemeStore::Dark);
    QTRY_COMPARE(other->property("foreground").value<QColor>(), host.theme.text());
    QCOMPARE(otherLabel->property("font").value<QFont>().families(),
             other->property("font").value<QFont>().families());
    themeRule->dispose();
    QTRY_COMPARE(otherLabel->property("color").value<QColor>(), host.theme.text());
    QTRY_COMPARE(otherLabel->property("font").value<QFont>(),
                 other->property("font").value<QFont>());

    auto *icon = visual(window->contentItem(), "partIcon");
    QVERIFY(icon);
    auto *iconBackground = icon->property("background").value<QQuickItem *>();
    QVERIFY(iconBackground);
    QVERIFY(!iconBackground->property("item").value<QObject *>());
    auto *iconStyle = scopeB->ui()->styles()->registerRule("icon-paint", {{"key", "partIcon"}},
                                                           {{"foreground", "#ff0000"}});
    QVERIFY(iconStyle);
    QTest::qWait(20);
    QVERIFY2(!iconBackground->property("item").value<QObject *>(),
             "Foreground-only styles must not instantiate transparent backgrounds");
    QVERIFY(iconStyle->update({{"background", "#ff0066"}}));
    QTRY_VERIFY(iconBackground->property("item").value<QObject *>());
    window->hide();
    QTRY_VERIFY(!iconBackground->property("item").value<QObject *>());
    window->show();
    QTRY_VERIFY(iconBackground->property("item").value<QObject *>());
    iconStyle->dispose();
    QTRY_VERIFY(!iconBackground->property("item").value<QObject *>());
    auto *toggle = visual(window->contentItem(), "partSwitch");
    auto *check = visual(window->contentItem(), "partCheck");
    auto *select = visual(window->contentItem(), "partSelect");
    QVERIFY(toggle && check && select);
    auto *toggleIndicator = toggle->property("indicator").value<QQuickItem *>();
    auto *checkIndicator = check->property("indicator").value<QQuickItem *>();
    auto *selectBackground = select->property("background").value<QQuickItem *>();
    QVERIFY(toggleIndicator && checkIndicator && selectBackground);
    const auto originalToggle = toggleIndicator->property("color").value<QColor>();
    const auto originalCheck = checkIndicator->property("color").value<QColor>();
    const auto originalSelect = selectBackground->property("color").value<QColor>();
    auto *toggleRule = scopeB->ui()->styles()->registerRule("toggle", {{"key", "partSwitch"}},
                                                            {{"accent", "#123456"}});
    auto *checkRule = scopeB->ui()->styles()->registerRule("check", {{"key", "partCheck"}},
                                                           {{"accent", "#ab1234"}});
    auto *selectRule = scopeB->ui()->styles()->registerRule(
        "select", {{"key", "partSelect"}},
        {{"background", "#345678"}, {"foreground", "#fedcba"}, {"radius", 17}, {"fontSize", 19}});
    QVERIFY(toggleRule && checkRule && selectRule);
    QTRY_COMPARE(toggleIndicator->property("color").value<QColor>(), QColor("#123456"));
    QTRY_COMPARE(checkIndicator->property("color").value<QColor>().red(), 171);
    QTRY_COMPARE(selectBackground->property("color").value<QColor>(), QColor("#345678"));
    QTRY_COMPARE(selectBackground->property("radius").toDouble(), 17.);
    auto *selectContent = select->property("contentItem").value<QQuickItem *>();
    QVERIFY(selectContent);
    bool selectedTextStyled = false;
    for (auto *child : selectContent->childItems())
        if (child->property("text").toString() == QString::fromUtf8("选项") && child->isVisible()) {
            QCOMPARE(child->property("color").value<QColor>(), QColor("#fedcba"));
            QCOMPARE(child->property("font").value<QFont>().pixelSize(), 19);
            selectedTextStyled = true;
        }
    QVERIFY(selectedTextStyled);
    toggleRule->dispose();
    checkRule->dispose();
    selectRule->dispose();
    QTRY_COMPARE(toggleIndicator->property("color").value<QColor>(), originalToggle);
    QTRY_COMPARE(checkIndicator->property("color").value<QColor>(), originalCheck);
    QTRY_COMPARE(selectBackground->property("color").value<QColor>(), originalSelect);
    root->setProperty("baseColor", QColor("#667788"));
    QCOMPARE(button->property("foreground").value<QColor>(), QColor("#667788"));
    host.theme.setMode(ThemeStore::Dark);
    auto *scopeA = host.runtime.context(a);
    auto *op = scopeA->executeCommand("patch");
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    QTRY_COMPARE(background->property("radius").toDouble(), 20.);
    auto *styles = scopeA->ui()->styles();
    QVERIFY(!styles->registerRule("invalid", {{"role", "control.button"}}, {{"width", 300}}, 0));
    QVERIFY(!styles->registerRule("invalid", {}, {{"foreground", "not a color"}}, 0));
    QVERIFY(!styles->registerRule("invalid", {{"bogus", "x"}}, {}, 0));
    QCOMPARE(label->property("color").value<QColor>(), QColor("#990055"));
    QVERIFY(host.runtime.setEnabled(b, false));
    QTRY_COMPARE(host.runtime.state(b), PluginRuntime::Disabled);
    QTRY_COMPARE(label->property("color").value<QColor>(), QColor("#005599"));
    QVERIFY(host.runtime.setEnabled(a, false));
    QTRY_COMPARE(host.runtime.state(a), PluginRuntime::Disabled);
    QTRY_COMPARE(label->property("color").value<QColor>(), QColor("#667788"));
    QTRY_COMPARE(background->property("radius").toDouble(), baseRadius);
    QCOMPARE(EchoUiPartRegistry::forEngine(host.engine)->styleCount(), 0);
    // The original binding remains live, rather than restoring an activation snapshot.
    root->setProperty("baseColor", QColor("#334455"));
    QTRY_COMPARE(label->property("color").value<QColor>(), QColor("#334455"));
    // Late instances pick up rules; a failed activation never leaves a style behind.
    const auto failed = activatePartPlugin(host, "test.style-failed", R"(
 c.ui.styles.registerRule('paint', {}, {foreground:'#ff0000'});
 throw new Error('style activation must roll back');
)");
    QTRY_COMPARE(host.runtime.state(failed), PluginRuntime::Failed);
    QCOMPARE(EchoUiPartRegistry::forEngine(host.engine)->styleCount(), 0);
}
void PluginRuntimeTest::uiPartsIdentityObservationAndPublicActions()
{
    Host host;
    const auto installation = activatePartPlugin(host, "test.parts", R"(
 c.ui.setState({added:0,removed:0,events:0});
 c.ui.parts.observe('watch',{role:'control.button'},function(e) {
  var s=c.ui.state;
  c.ui.setState({added:s.added+(e.type==='added'?1:0), removed:s.removed+(e.type==='removed'?1:0), events:s.events+(e.type==='event'?1:0)});
 });
)");
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    auto root = createPartWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    QTRY_COMPARE(scope->ui()->state().value("added").toInt(), 2);
    auto *parts = scope->ui()->parts();
    auto *button = parts->query({{"role", "control.button"}, {"key", "partButton"}});
    QVERIFY(button);
    auto keep = host.engine.newQObject(button);
    QVERIFY(button->valid());
    QVERIFY(button->visible());
    QVERIFY(!button->window().isEmpty());
    QCOMPARE(button->geometry().size(),
             QSizeF(visual(window->contentItem(), "partButton")->width(), 36));
    auto *op = button->invoke("activate");
    QVERIFY(op);
    QCOMPARE(op->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(root->property("activationCount").toInt(), 1);
    QTRY_COMPARE(scope->ui()->state().value("events").toInt(), 1);
    auto *page = parts->query({{"role", "page.test"}});
    QVERIFY(page);
    auto keepPage = host.engine.newQObject(page);
    const auto oldId = page->id();
    QSignalSpy invalidated(page, &EchoUiPartRef::invalidated);
    root->setProperty("resourceKey", "song-b");
    QTRY_VERIFY(!page->valid());
    QCOMPARE(invalidated.count(), 1);
    auto *next = parts->query({{"role", "page.test"}});
    QVERIFY(next);
    QVERIFY(next->id() != oldId);
    QCOMPARE(next->key(), "song-b");
    QVERIFY(!parts->query({{"id", oldId}}));
    const auto startCount = EchoUiPartRegistry::forEngine(host.engine)->partCount();
    QVERIFY(startCount >= 3);
    // Session changes reject old commands immediately and republish identities.
    const auto previousId = button->id();
    QVERIFY(QMetaObject::invokeMethod(&host.api, "sessionChanged", Q_ARG(qulonglong, 1)));
    QVERIFY(!button->valid());
    QTRY_VERIFY(parts->query({{"role", "control.button"}, {"key", "partButton"}})->id() !=
                previousId);
    QTRY_COMPARE(scope->ui()->state().value("removed").toInt(), 2);
    const int removedBeforeClose = scope->ui()->state().value("removed").toInt();
    root.reset();
    QTRY_VERIFY(!button->valid());
    QTRY_COMPARE(scope->ui()->state().value("removed").toInt(), removedBeforeClose + 2);
    QCOMPARE(EchoUiPartRegistry::forEngine(host.engine)->partCount(), 0);
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QVERIFY(!button->valid());
}
void PluginRuntimeTest::uiPartMountsReplaceAndRetire()
{
    Host host;
    auto root = createPartWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    const auto a = activatePartPlugin(host, "test.mount-a", "", mountComponent);
    QCOMPARE(host.runtime.state(a), PluginRuntime::Active);
    auto *scope = host.runtime.context(a);
    auto *parts = scope->ui()->parts();
    auto *part = parts->query({{"role", "page.test"}});
    QVERIFY(part);
    auto keep = host.engine.newQObject(part);
    auto *mount = parts->mount("replacement", part, "replace", "Mount.qml", 0);
    QVERIFY(mount);
    auto keepMount = host.engine.newQObject(mount);
    QTRY_VERIFY(mount->loaded());
    auto *original = visual(window->contentItem(), "originalPartBody");
    QVERIFY(original);
    QTRY_VERIFY(!original->isVisible());
    QPointer<QQuickItem> first = visual(window->contentItem(), "publicPartMountedVisual");
    QVERIFY(first);
    QCOMPARE(first->size(), QSizeF(400, 150));
    const auto b = activatePartPlugin(host, "test.mount-b", R"(
 var p=c.ui.parts.query({role:'page.test'});
 if (!p || !c.ui.parts.mount('replacement',p,'replace','Mount.qml',10)) throw new Error('mount unavailable');
)",
                                      mountComponent);
    QCOMPARE(host.runtime.state(b), PluginRuntime::Active);
    QTRY_VERIFY(first.isNull());
    QTRY_VERIFY(!original->isVisible());
    auto *topMount = host.runtime.context(b)->findChild<EchoUiPartMount *>();
    QVERIFY(topMount);
    topMount->dispose();
    QVERIFY2(mount->loaded() || original->isVisible(),
             "A withdrawn replacement must not hide native contents while its fallback reloads");
    QVERIFY(host.runtime.setEnabled(b, false));
    QTRY_COMPARE(host.runtime.state(b), PluginRuntime::Disabled);
    QTRY_VERIFY(mount->loaded());
    QPointer<QQuickItem> restored = visual(window->contentItem(), "publicPartMountedVisual");
    QVERIFY(restored);
    window->hide();
    QTRY_VERIFY(restored.isNull());
    QTRY_VERIFY(!mount->loaded());
    window->show();
    QTRY_VERIFY(mount->loaded());
    // All insertion modes create real package QML, and disposal releases it.
    for (const auto &position :
         {"append", "prepend", "before", "after", "background", "foreground"}) {
        auto *handle = parts->mount(QString("mount-") + position, part, position, "Mount.qml");
        QVERIFY(handle);
        QTRY_VERIFY(handle->loaded());
        handle->dispose();
        QTRY_VERIFY(!handle->loaded());
    }
    auto *clippedRef = parts->query({{"role", "page.clip"}});
    QVERIFY(clippedRef);
    auto keepClippedRef = host.engine.newQObject(clippedRef);
    auto *clippedMount = parts->mount("clipped-layer", clippedRef, "foreground", "Mount.qml");
    QVERIFY(clippedMount);
    auto keepClippedMount = host.engine.newQObject(clippedMount);
    QTRY_VERIFY(clippedMount->loaded());
    root->setProperty("clippedOffset", 100);
    QTRY_VERIFY(!clippedRef->visible());
    QTRY_VERIFY(!clippedMount->loaded());
    QVERIFY(clippedRef->valid()); // culling is not delegate identity reuse
    root->setProperty("clippedOffset", 0);
    QTRY_VERIFY(clippedMount->loaded());
    clippedMount->dispose();
    auto *buttonRef = parts->query({{"role", "control.button"}, {"key", "partButton"}});
    QVERIFY(buttonRef);
    auto *button = visual(window->contentItem(), "partButton");
    auto *before = parts->mount("layout-before", buttonRef, "before", "Mount.qml");
    QVERIFY(before);
    QTRY_VERIFY(before->loaded());
    QQuickItem *inserted = nullptr;
    for (auto *child : button->parentItem()->childItems())
        if (child->objectName() == "publicPartMountedVisual")
            inserted = child;
    QVERIFY(inserted);
    QTRY_VERIFY(inserted->x() + inserted->width() <= button->x());
    QPointer<QQuickItem> insertedGuard = inserted;
    before->dispose();
    QTRY_VERIFY(insertedGuard.isNull());
    const auto buttonWidth = button->width();
    auto *replaceButton = parts->mount("replace-button", buttonRef, "replace", "Mount.qml");
    QVERIFY(replaceButton);
    QTRY_VERIFY(replaceButton->loaded());
    QTRY_COMPARE(button->width(), buttonWidth);
    auto *activation = buttonRef->invoke("activate");
    QVERIFY(activation);
    QCOMPARE(root->property("activationCount").toInt(), 1);
    replaceButton->dispose();
    QTRY_VERIFY(!replaceButton->loaded());
    QTRY_COMPARE(button->width(), buttonWidth);
    root->setProperty("resourceKey", "song-next");
    QTRY_VERIFY(!part->valid());
    QTRY_VERIFY(!mount->loaded());
    auto *current = parts->query({{"role", "page.test"}});
    QVERIFY(current);
    QVERIFY(current->id() != part->id());
    QVERIFY(original->isVisible());
    QVERIFY(!parts->mount("stale", part, "replace", "Mount.qml"));
    QVERIFY(host.runtime.setEnabled(a, false));
    QTRY_COMPARE(host.runtime.state(a), PluginRuntime::Disabled);
    QVERIFY(!visual(window->contentItem(), "publicPartMountedVisual"));
}
void PluginRuntimeTest::uiPartsProductionPlaybackAndMini()
{
    Host host;
    host.theme.setMotionEnabled(false);
    FloatingPlayerController floating(host.settings, false);
    floating.showMini();
    const auto installation = activatePartPlugin(host, "test.player-ui", "", mountComponent);
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback
import EchoMusic.Shell
ApplicationWindow {
 id: win
 required property PlaybackService playback
 required property FloatingPlayerController floating
 width: 1000; height: 760; visible: true
 readonly property color expectedMiniColor: Theme.floating.elevated
 PlaybackPage { id: page; playback: win.playback; width: win.width; height: win.height; Component.onCompleted: open() }
 MiniPlayerWindow { objectName: "productionMini"; playback: win.playback; service: win.floating }
})",
                      QUrl("qrc:/parts-real-playback-mini.qml"));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"playback", QVariant::fromValue(&host.playback)},
                                               {"floating", QVariant::fromValue(&floating)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window && Test::prepareNativeInput(window));
    auto *parts = scope->ui()->parts();
    QTRY_VERIFY(parts->query({{"role", "playback.page"}}));
    auto *pageRef = parts->query({{"role", "playback.page"}});
    auto *miniRef = parts->query({{"role", "mini.panel"}});
    QVERIFY(miniRef);
    QVERIFY(pageRef->replaceable());
    QVERIFY(miniRef->replaceable());
    QVERIFY(pageRef->window() != miniRef->window());
    auto keepPage = host.engine.newQObject(pageRef), keepMini = host.engine.newQObject(miniRef);
    auto *page = root->findChild<QObject *>("playbackPage");
    QVERIFY(page);
    auto *mini = root->findChild<QQuickWindow *>("productionMini");
    QVERIFY(mini);
    auto *card = visual(mini->contentItem(), "miniPlayerCard");
    QVERIFY(card);
    auto *rule = scope->ui()->styles()->registerRule("mini-paint", {{"role", "mini.panel"}},
                                                     {{"background", "#102030"}});
    QVERIFY(rule);
    QTRY_COMPARE(card->property("color").value<QColor>(), QColor("#102030"));
    auto *fullPage = parts->mount("full-page", pageRef, "replace", "Mount.qml");
    auto *fullMini = parts->mount("full-mini", miniRef, "replace", "Mount.qml");
    QVERIFY(fullPage && fullMini);
    QTRY_VERIFY(fullPage->loaded());
    QTRY_VERIFY(fullMini->loaded());
    QTRY_VERIFY(!page->property("resourceActive").toBool());
    auto *body = visual(window->contentItem(), "nativePlaybackContents");
    QVERIFY(body);
    QVERIFY(!body->isVisible());
    QPointer<QQuickItem> pageItem = visual(window->contentItem(), "publicPartMountedVisual");
    QPointer<QQuickItem> miniItem = visual(mini->contentItem(), "publicPartMountedVisual");
    QVERIFY(pageItem && miniItem);
    QVERIFY(pageItem->width() >= 900 && pageItem->height() >= 700);
    QCOMPARE(miniItem->width(), card->width());
    // The replacement is the full visual content; no host playerbar is imposed.
    QVERIFY(!visual(pageItem, "playerbar"));
    mini->hide();
    QTRY_VERIFY(miniItem.isNull());
    QVERIFY(pageItem); // another window's lifecycle is independent
    mini->show();
    QTRY_VERIFY(fullMini->loaded());
    host.theme.setMode(ThemeStore::Dark);
    fullMini->dispose();
    QTRY_VERIFY(!fullMini->loaded());
    QTRY_COMPARE(card->property("color").value<QColor>(), QColor("#102030"));
    rule->dispose();
    QTRY_COMPARE(card->property("color").value<QColor>(),
                 root->property("expectedMiniColor").value<QColor>());
    fullPage->dispose();
    QTRY_VERIFY(pageItem.isNull());
    QTRY_VERIFY(body->isVisible());
    QTRY_VERIFY(page->property("resourceActive").toBool());
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
}
void PluginRuntimeTest::uiPartInstanceStylesAndLeasesStayBounded()
{
    Host host;
    auto root = createPartWindow(host);
    QVERIFY(root);
    const auto installation = activatePartPlugin(host, "test.part-reuse", "", mountComponent);
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    auto *parts = scope->ui()->parts();
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    const auto liveParts = registry->partCount();
    QPointer<EchoUiStyleRule> firstDiscarded;
    QElapsedTimer elapsed;
    elapsed.start();
    for (int index = 0; index < 100; ++index) {
        auto *ref = parts->query({{"role", "page.test"}});
        QVERIFY(ref);
        auto keepRef = host.engine.newQObject(ref);
        auto *rule = scope->ui()->styles()->registerRule("instance-paint", {{"id", ref->id()}},
                                                         {{"background", "#113355"}});
        QVERIFY(rule);
        auto keepRule = host.engine.newQObject(rule);
        if (!index)
            firstDiscarded = rule;
        QCOMPARE(registry->styleCount(), 1);
        root->setProperty("resourceKey", QString("resource-%1").arg(index));
        QVERIFY(!ref->valid());
        QVERIFY(!rule->active());
        QCOMPARE(registry->styleCount(), 0);
        QVERIFY(!scope->ui()->styles()->registerRule("stale", {{"id", ref->id()}},
                                                     {{"background", "#ffffff"}}));
        QCOMPARE(registry->partCount(), liveParts);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    host.engine.collectGarbage();
    host.engine.collectGarbage();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(firstDiscarded.isNull());
    QCOMPARE(scope->findChildren<EchoPluginRegistration *>().size(), 0);
    QCOMPARE(registry->styleCount(), 0);
    QCOMPARE(registry->partCount(), liveParts);
    qInfo() << "100 public-instance reuse/style cycles" << elapsed.elapsed() << "ms; live parts"
            << liveParts;
    // Cancel asynchronous construction before it can create an Item.
    auto *ref = parts->query({{"role", "page.test"}});
    QVERIFY(ref);
    auto keepRef = host.engine.newQObject(ref);
    auto *mount = parts->mount("cancel-loading", ref, "foreground", "Mount.qml");
    QVERIFY(mount);
    auto keepMount = host.engine.newQObject(mount);
    mount->dispose();
    QTest::qWait(80);
    QVERIFY(!mount->loaded());
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(!visual(window->contentItem(), "publicPartMountedVisual"));
    QCOMPARE(scope->findChildren<EchoUiPartMount *>().size(), 0);
}
