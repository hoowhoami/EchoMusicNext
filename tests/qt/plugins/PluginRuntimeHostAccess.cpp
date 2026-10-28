#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoCoverRegistry.h"
#include "EchoPluginHost.h"
#include "PlaybackPreferences.h"
#include "LyricAppearance.h"
namespace
{
EchoPluginContext *hostAccess(Host &host, bool approve = true, const QString &id = "host-access")
{
    if (approve)
        QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                         host.runtime.permissions(),
                         [&host](const QString &request, const QVariantMap &) {
                             host.runtime.permissions()->decide(request, true);
                         });
    const auto source = pluginPackage(
        host.temporary.path(), id, "export function activate(c){}",
        {{"capabilities", QJsonArray{"navigation", "hostSettings", "cover"}},
         {"requires",
          QJsonObject{{"hostVersion", ">=0.1.0"},
                      {"sdkVersion", ">=1.0.0 <2.0.0"},
                      {"features", QJsonArray{"ui.navigation", "settings.host", "ui.actions",
                                              "cover.themed", "ui.cover"}}}}});
    host.runtime.importDirectory(source);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId(id)) ==
                       PluginRuntime::Active;
            },
            5000))
        return nullptr;
    auto *scope = host.runtime.context(host.runtime.installationForId(id));
    host.engine.globalObject().setProperty("probe", host.engine.newObject());
    host.engine.globalObject().setProperty("c", host.engine.newQObject(scope));
    return scope;
}
EchoPluginRegistration *registration(Host &host, EchoPluginOperation *op)
{
    auto keep = host.engine.newQObject(op);
    if (!QTest::qWaitFor([op] { return op->finished(); }, 5000) ||
        op->state() != EchoPluginOperation::Succeeded) {
        qWarning() << op->errorCode() << op->errorMessage();
        return nullptr;
    }
    return qobject_cast<EchoPluginRegistration *>(op->result().value<QObject *>());
}
int actionRow(ShellActionModel *model, const QString &key)
{
    for (int i = 0; i < model->rowCount(); ++i)
        if (model->data(model->index(i), ShellActionModel::KeyRole).toString() == key)
            return i;
    return -1;
}
} // namespace
void PluginRuntimeTest::hostNavigationRoutesHistoryCancellationAndAccount()
{
    Host host;
    auto *scope = hostAccess(host);
    QVERIFY(scope);
    auto *nav = scope->ui()->navigation();
    host.router.registerPage(
        {"album-detail", "/album/:id", QUrl("qrc:/album.qml"), {}, false, true, false, {}});
    QSignalSpy activated(&host.router, &RouteController::pageActivated);
    auto *open = nav->navigate({{"name", "album-detail"},
                                {"resourceId", "123"},
                                {"query", QVariantMap{{"tab", "songs"}}}});
    auto keep = host.engine.newQObject(open);
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.router.current()->resourceId(), "123");
    QCOMPARE(host.router.current()->query().value("tab").toString(), "songs");
    QVERIFY(!activated.isEmpty());
    auto *back = nav->history("back");
    auto keepBack = host.engine.newQObject(back);
    QTRY_COMPARE(back->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.router.current()->name(), "search");
    auto *forward = nav->history("forward");
    auto keepForward = host.engine.newQObject(forward);
    QTRY_COMPARE(forward->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.router.current()->name(), "album-detail");
    auto *cancel = nav->navigate({{"name", "search"}, {"query", QVariantMap{{"q", "canceled"}}}});
    cancel->cancel();
    QCoreApplication::processEvents();
    QCOMPARE(host.router.current()->name(), "album-detail");
    QCOMPARE(nav->navigate({{"name", "private-login"}})->errorCode(), "InvalidArgument");
    QCOMPARE(nav->navigate({{"name", "search"}, {"query", QVariantMap{{"q", 7}}}})->errorCode(),
             "InvalidArgument");
    auto *missing = nav->navigate({{"name", "album-detail"}});
    auto keepMissing = host.engine.newQObject(missing);
    QTRY_COMPARE(missing->state(), EchoPluginOperation::Failed);
    auto *observer =
        registration(host, nav->observe("route", js(host, "(function(v){probe.lastRoute=v;})")));
    QVERIFY(observer);
    auto observerRoot = host.engine.newQObject(observer);
    auto *late =
        nav->navigate({{"name", "search"}, {"query", QVariantMap{{"q", "previous account"}}}});
    auto keepLate = host.engine.newQObject(late);
    host.api.advanceSession();
    QTRY_COMPARE(late->state(), EchoPluginOperation::Canceled);
    QVERIFY(!observer->active());
    host.router.navigate("search", {{"q", "new account"}});
    QVERIFY(js(host, "probe.lastRoute").isUndefined());
}
void PluginRuntimeTest::hostSettingsPersistenceSubscriptionsAndRevocation()
{
    Host host;
    auto *scope = hostAccess(host);
    QVERIFY(scope);
    auto *settings = scope->settings();
    QCOMPARE(settings->catalog().size(), 6);
    for (const auto &entry : settings->catalog()) {
        const auto domain = entry.toMap().value("domain").toString();
        auto *read = settings->read(domain);
        auto readRoot = host.engine.newQObject(read);
        QTRY_COMPARE(read->state(), EchoPluginOperation::Succeeded);
        QCOMPARE(read->result().toMap().size(), entry.toMap().value("properties").toList().size());
    }
    auto *mini =
        settings->update("mini", {{"alwaysOnTop", true}, {"fontSize", 20}, {"fontWeight", 780}});
    auto miniRoot = host.engine.newQObject(mini);
    QTRY_COMPARE(mini->state(), EchoPluginOperation::Succeeded);
    QVERIFY(host.floating.mini()->alwaysOnTop());
    QCOMPARE(host.floating.mini()->fontSize(), 20);
    auto *lyrics = settings->update("lyrics", {{"fontScale", 1.2}, {"enableSpring", false}});
    auto lyricsRoot = host.engine.newQObject(lyrics);
    QTRY_COMPARE(lyrics->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.playback.lyricAppearance()->fontScale(), 1.2);
    QVERIFY(!host.playback.lyricAppearance()->enableSpring());
    auto *appearance = settings->update("appearance", {{"mode", 2}, {"panelOpacity", 68}});
    auto appearanceRoot = host.engine.newQObject(appearance);
    QTRY_COMPARE(appearance->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.theme.mode(), ThemeStore::Dark);
    QCOMPARE(host.theme.panelOpacity(), 68);
    auto *desktop =
        settings->update("desktop", {{"playedColor", "#334466"}, {"alignment", "both"}});
    auto desktopRoot = host.engine.newQObject(desktop);
    QTRY_COMPARE(desktop->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.floating.desktop()->playedColor(), "#334466");
    QCOMPARE(host.floating.desktop()->alignment(), "both");

    auto *write = settings->update(
        "playback", {{"autoNext", true}, {"autoNextDelaySeconds", 8}, {"volumeFadeMs", 1800}});
    auto root = host.engine.newQObject(write);
    QTRY_COMPARE(write->state(), EchoPluginOperation::Succeeded);
    QVERIFY(host.playback.preferences()->autoNext());
    PlaybackPreferences restored(host.settings);
    QCOMPARE(restored.autoNextDelaySeconds(), 8);
    QCOMPARE(restored.volumeFadeMs(), 1800);
    QCOMPARE(settings->update("playback", {{"autoNext", false}, {"volumeFadeMs", 2}})->errorCode(),
             "InvalidArgument");
    QVERIFY(host.playback.preferences()->autoNext());
    QCOMPARE(settings->update("shell", {{"userAgreementAccepted", true}})->errorCode(),
             "InvalidArgument");
    QCOMPARE(
        settings->update("appearance", {{"globalFont", "unknown nonexistent font"}})->errorCode(),
        "InvalidArgument");
    auto *shell = settings->update("shell", {{"motionEnabled", false}, {"keepAliveMax", 12}});
    auto shellRoot = host.engine.newQObject(shell);
    QTRY_COMPARE(shell->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!host.shellSettings.motionEnabled());
    ShellSettings restoredShell(host.settings);
    QCOMPARE(restoredShell.keepAliveMax(), 12);
    auto *watch = registration(
        host, settings->observe("prefs", "playback",
                                js(host, "(function(v){probe.prefCalls=(probe.prefCalls||"
                                         "0)+1; probe.savedPrefs=v;})")));
    QVERIFY(watch);
    auto watchRoot = host.engine.newQObject(watch);
    QTRY_COMPARE(js(host, "probe.prefCalls").toInt(), 1);
    host.playback.preferences()->setAutoNextDelaySeconds(9);
    host.playback.preferences()->setVolumeFadeMs(1900);
    QTRY_COMPARE(js(host, "probe.prefCalls").toInt(), 2);
    QCOMPARE(js(host, "probe.savedPrefs.autoNextDelaySeconds").toInt(), 9);
    for (const auto &row : host.runtime.permissions()->records())
        if (row.toMap().value("capability") == "hostSettings")
            QVERIFY(host.runtime.permissions()->revoke(row.toMap().value("id").toString()));
    QVERIFY(!watch->active());
    QSignalSpy revokedNotifications(settings, &EchoPluginSettings::sourceChanged);
    host.playback.preferences()->setAutoNextDelaySeconds(10);
    QCOMPARE(revokedNotifications.count(), 0);
    QCoreApplication::processEvents();
    QCOMPARE(js(host, "probe.prefCalls").toInt(), 2);
    auto *regrant = settings->read("playback");
    auto keepRegrant = host.engine.newQObject(regrant);
    QTRY_COMPARE(regrant->state(), EchoPluginOperation::Succeeded);
    auto *accountWatch =
        registration(host, settings->observe("new-account", "mini",
                                             js(host, "(function(v){probe.miniPreference=v;})")));
    QVERIFY(accountWatch);
    auto accountWatchRoot = host.engine.newQObject(accountWatch);
    auto *pending = settings->update("playback", {{"autoNext", false}});
    auto pendingRoot = host.engine.newQObject(pending);
    host.api.advanceSession();
    QVERIFY(!accountWatch->active());
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(host.playback.preferences()->autoNext());
}
void PluginRuntimeTest::hostActionsDynamicAsyncAndLifecycle()
{
    Host host;
    auto *scope = hostAccess(host);
    QVERIFY(scope);
    auto *model = host.shell.playerbarActions();
    model->setCapacity(ShellActionModel::Right, 4);
    auto definition = js(
        host,
        R"(({title:'动态操作', icon:'music',tooltip:'实际提示',defaultPlacement:'right',order:3,badge:function(){return c.ui.state.count||0;},badgeTitle:'计数',visible:function(){return !c.ui.state.hidden;},disabled:function(){return !!c.ui.state.disabled;},onClick:function(){probe.runs=(probe.runs||0)+1;return new Promise(function(resolve){probe.finishAction=resolve;});}}))");
    auto *action = scope->ui()->registerPlayerbarActionDefinition("dynamic", definition);
    QVERIFY(action);
    auto actionRoot = host.engine.newQObject(action);
    const auto key = action->key();
    QTRY_COMPARE(action->state().value("badge").toString(), "0");
    QVERIFY(model->activate(key));
    QVERIFY(action->busy());
    QVERIFY(!model->activate(key));
    QTRY_COMPARE(js(host, "probe.runs").toInt(), 1);
    scope->ui()->setState({{"count", 12}});
    QTRY_COMPARE(action->state().value("badge").toString(), "12");
    js(host, "probe.finishAction('done')");
    QTRY_VERIFY(!action->busy());
    const int row = actionRow(model, key);
    QVERIFY(row >= 0);
    QCOMPARE(model->data(model->index(row), ShellActionModel::TooltipRole).toString(), "实际提示");
    QVERIFY(model->setPlacement(key, ShellActionModel::Left));
    QVERIFY(action->update(js(host, "({title:'更新',badge:'new',onClick:function(){return "
                                    "Promise.reject(new Error('async action failure'));}})")));
    QTRY_COMPARE(action->state().value("title").toString(), "更新");
    QCOMPARE(
        model->data(model->index(actionRow(model, key)), ShellActionModel::PreferredRole).toInt(),
        int(ShellActionModel::Left));
    QSignalSpy failed(scope, &EchoPluginContext::callbackFailed);
    QVERIFY(model->activate(key));
    QTRY_COMPARE(failed.count(), 1);
    QTRY_VERIFY(!action->busy());
    scope->ui()->setState({{"hidden", true}});
    QTRY_VERIFY(!action->state().value("visible").toBool());
    QVERIFY(!model->activate(key));
    auto *replacement = scope->ui()->registerPlayerbarActionDefinition(
        "dynamic", js(host, "({title:'替换',icon:'music',onClick:function(){return new "
                            "Promise(function(resolve){probe.lateAction=resolve;});}})"));
    QVERIFY(replacement);
    auto keepReplacement = host.engine.newQObject(replacement);
    QVERIFY(!action->active());
    action->dispose();
    QVERIFY(replacement->active());
    auto *pending = replacement->invoke();
    auto keepPending = host.engine.newQObject(pending);
    QTRY_VERIFY(js(host, "probe.lateAction").isCallable());
    host.api.advanceSession();
    QVERIFY(!replacement->active());
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    QCOMPARE(actionRow(model, key), -1);
    js(host, "probe.lateAction('late')");
    QCoreApplication::processEvents();
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
}
void PluginRuntimeTest::hostActionsProductionHoverBadgeAndBusy()
{
    Host host;
    auto *scope = hostAccess(host);
    QVERIFY(scope);
    auto *model = host.shell.playerbarActions();
    model->setCapacity(ShellActionModel::Right, 32);
    auto *action = scope->ui()->registerPlayerbarActionDefinition(
        "hover", js(host, "({title:'悬停操作',icon:'music',tooltip:'悬停提示',trigger:'hover',"
                          "defaultPlacement:'right',badge:7,badgeTitle:'数字',onClick:function(){"
                          "probe.hovers=(probe.hovers||0)+1;return new "
                          "Promise(function(resolve){probe.finishHover=resolve;});}})"));
    QVERIFY(action);
    auto keepAction = host.engine.newQObject(action);
    auto *namedLikeHost = scope->ui()->registerPlayerbarActionDefinition(
        "play-toggle", js(host, "({title:'自定义操作',icon:'star',defaultPlacement:'right',"
                                "disabled:true,onClick:function(){}})"));
    QVERIFY(namedLikeHost);
    auto keepNamed = host.engine.newQObject(namedLikeHost);

    auto *titleAction = scope->ui()->registerTitlebarActionDefinition(
        "menu",
        js(host, "({title:'标题栏插件',icon:'music',tooltip:'更多提示',badge:2,onClick:function(){"
                 "probe.titleClicks=(probe.titleClicks||0)+1;return Promise.resolve();}})"));
    QVERIFY(titleAction);
    auto keepTitle = host.engine.newQObject(titleAction);
    auto window =
        ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Shell
ApplicationWindow {
 id: root; required property ShellActionModel actions; required property ShellActionModel titleActions
 width:500;height:420;visible:true
 PlayerActionStrip {objectName:'realPluginStrip';x:30;y:50;actions:root.actions;zone:ShellActionModel.Right;playbackLoading:true}
 Item {id:anchor;x:330;y:30;width:30;height:30}
 function openTitleMenu(){titleMenu.open();}
 function closeTitleMenu(){titleMenu.close();}
 EchoPopupLoader {id:titleMenu;hostItem:root.contentItem;sourceComponent:Component {TitleBarMorePopup {actions:root.titleActions;anchorItem:anchor}}}
})",
                        {{"actions", QVariant::fromValue(model)},
                         {"titleActions", QVariant::fromValue(host.shell.titlebarActions())}});
    QVERIFY(window);
    auto *win = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(Test::prepareNativeInput(win));
    QQuickItem *button = nullptr;
    QTRY_VERIFY(button = visual(win->contentItem(), "playerbar-action-hover"));
    auto *badge = visual(button, "actionBadge");
    QVERIFY(badge);
    QVERIFY(badge->isVisible());
    QQuickItem *customButton = nullptr;
    QTRY_VERIFY(customButton = visual(win->contentItem(), "playerbar-action-play-toggle"));
    QVERIFY(!customButton->isEnabled());
    QVERIFY(!customButton->property("animatePlayPause").toBool());
    QCOMPARE(customButton->width(), 36.);

    QCOMPARE(button->property("tooltipText").toString(), "悬停提示 · 数字: 7");
    const auto center =
        button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
    QTest::mouseMove(win, center);
    QTRY_COMPARE(js(host, "probe.hovers").toInt(), 1);
    QTRY_VERIFY(action->busy());
    QPointer<QQuickItem> spinner;
    QTRY_VERIFY(spinner = visual(button, "actionBusyIndicator"));
    QTRY_VERIFY(spinner->isVisible());
    js(host, "probe.finishHover()");
    QTRY_VERIFY(!action->busy());
    QTRY_VERIFY(!spinner || !spinner->isVisible());
    QTest::qWait(80);
    QCOMPARE(js(host, "probe.hovers").toInt(), 1);
    QTest::mouseMove(win, QPoint(450, 220));
    QVERIFY(QMetaObject::invokeMethod(window.get(), "openTitleMenu"));
    QQuickItem *menuButton = nullptr;
    QTRY_VERIFY(menuButton =
                    visual(win->contentItem(), "titlebarMenuAction-" + titleAction->key()));
    const auto menuCenter =
        menuButton->mapToScene(QPointF(menuButton->width() / 2, menuButton->height() / 2))
            .toPoint();
    QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, menuCenter);
    QTRY_COMPARE(js(host, "probe.titleClicks").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "closeTitleMenu"));
    host.runtime.setEnabled(host.runtime.installationForId("host-access"), false);
    QTRY_VERIFY(!visual(win->contentItem(), "playerbar-action-hover"));
}
void PluginRuntimeTest::hostCoverFallbackPriorityErrorsThemeAndRetirement()
{
    Host host;
    auto *scope = hostAccess(host);
    QVERIFY(scope);
    auto *registry = EchoCoverRegistry::forEngine(host.engine);
    QVERIFY(registry);
    auto *base = registration(
        host, scope->cover()->setFallback(
                  "base",
                  js(host, "(function(v){probe.coverContext=v; return "
                           "c.cover.createThemedIconCoverUrl('music',v.accentColor);})"),
                  1));
    QVERIFY(base);
    auto keepBase = host.engine.newQObject(base);
    const auto lowUrl = scope->cover()->createThemedIconCoverUrl("music", "#123456");
    QVERIFY(lowUrl.startsWith("data:image/svg+xml;base64,"));
    QVERIFY(scope->cover()->createThemedIconCoverUrl("nonexistent-icon").isEmpty());
    const QVariantMap custom{
        {"icon", QVariantMap{{"body", "<path fill='currentColor' d='M1 1h20v20H1z'/>"},
                             {"width", 24},
                             {"height", 24}}},
        {"color", "#336699"}};
    const auto customUrl = scope->cover()->createThemedIconCoverUrl(custom);
    QVERIFY(customUrl.startsWith("data:image/svg+xml;base64,"));
    const auto svg = QByteArray::fromBase64(customUrl.section(',', 1).toLatin1());
    QVERIFY(svg.contains("#336699"));
    QVERIFY(svg.contains("M1 1h20v20H1z"));
    QVERIFY(scope->cover()
                ->createThemedIconCoverUrl(
                    QVariantMap{{"icon", QVariantMap{{"body", "<script>alert(1)</script>"}}}})
                .isEmpty());
    QVERIFY(scope->cover()
                ->createThemedIconCoverUrl(QVariantMap{
                    {"icon", QVariantMap{{"body", "<use href='file:///private/secret'/>"}}}})
                .isEmpty());
    QVERIFY(scope->cover()
                ->createThemedIconCoverUrl(
                    QVariantMap{{"icon", QVariantMap{{"body", "<path onload='alert(1)'/>"}}}})
                .isEmpty());
    auto window = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow {width:300;height:240;visible:true; EchoCover {objectName:'realFallbackCover';x:30;y:30;width:100;height:100;coverScope:'mini';coverAlt:'当前歌曲'} })");
    QVERIFY(window);
    auto *win = qobject_cast<QQuickWindow *>(window.get());
    auto *cover = visual(win->contentItem(), "realFallbackCover");
    QVERIFY(cover);
    auto *source = cover->findChild<EchoCoverSource *>();
    QVERIFY(source);
    QTRY_VERIFY(!source->resolvedSource().isEmpty());
    QTRY_COMPARE(cover->property("status").toInt(), 1); // real Image.Ready
    QCOMPARE(js(host, "probe.coverContext.reason").toString(), "empty");
    QCOMPARE(js(host, "probe.coverContext.scope").toString(), "mini");
    cover->setProperty("source", QUrl(customUrl));
    QTRY_COMPARE(source->resolvedSource(), QUrl(customUrl));
    QTRY_COMPARE(cover->property("status").toInt(), 1);
    const auto normalCalls = js(host, "probe.coverCalls").toInt();
    host.theme.setAccent(QColor("#992266"));
    QTest::qWait(50);
    QCOMPARE(js(host, "probe.coverCalls").toInt(), normalCalls);
    cover->setProperty("source", QUrl{});
    QTRY_COMPARE(js(host, "probe.coverContext.reason").toString(), "empty");
    QTRY_COMPARE(cover->property("status").toInt(), 1);
    const auto oldUrl = source->resolvedSource();
    host.theme.setAccent(QColor("#135799"));
    QTRY_VERIFY(source->resolvedSource() != oldUrl);
    auto *top = registration(
        host,
        scope->cover()->setFallback(
            "top",
            js(host, "(function(){return c.cover.createThemedIconCoverUrl('star','#aa3344');})"),
            10));
    QVERIFY(top);
    auto keepTop = host.engine.newQObject(top);
    const QUrl topUrl(scope->cover()->createThemedIconCoverUrl("star", "#aa3344"));
    QTRY_COMPARE(source->resolvedSource(), topUrl);
    top->dispose();
    QTRY_VERIFY(source->resolvedSource() != topUrl);
    // A bad high-priority candidate must fall through after an actual decoder error.
    auto *broken = registration(
        host, scope->cover()->setFallback(
                  "broken", js(host, "(function(){return 'data:image/png;base64,YmFk';})"), 20));
    QVERIFY(broken);
    auto keepBroken = host.engine.newQObject(broken);
    QTRY_COMPARE(cover->property("status").toInt(), 1);
    QTRY_VERIFY(source->resolvedSource().toString().startsWith("data:image/svg+xml"));
    cover->setProperty("source", QUrl("http://127.0.0.1:1/absent.png"));
    QTRY_COMPARE(js(host, "probe.coverContext.reason").toString(), "error");
    QCOMPARE(js(host, "probe.coverContext.failedUrl").toString(), "http://127.0.0.1:1/absent.png");
    cover->setProperty("resourceActive", false);
    QTRY_VERIFY(source->resolvedSource().isEmpty());
    cover->setProperty("resourceActive", true);
    QTRY_COMPARE(cover->property("status").toInt(), 1);
    const auto secondPackage =
        pluginPackage(host.temporary.path(), "host-cover-b", "export function activate(c){}",
                      {{"capabilities", QJsonArray{"cover"}}});
    host.runtime.importDirectory(secondPackage);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("host-cover-b")),
                 PluginRuntime::Active);
    auto *second = host.runtime.context(host.runtime.installationForId("host-cover-b"));
    QVERIFY(second);
    const auto secondUrl = second->cover()->createThemedIconCoverUrl("star", "#1166aa");
    auto secondCallback = js(host, "(function(){return '" + secondUrl + "';})");
    auto *secondLease =
        registration(host, second->cover()->setFallback("shared", secondCallback, 5));
    QVERIFY(secondLease);
    auto secondRoot = host.engine.newQObject(secondLease);
    QTRY_COMPARE(source->resolvedSource(), QUrl(secondUrl));
    host.runtime.setEnabled(host.runtime.installationForId("host-cover-b"), false);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("host-cover-b")),
                 PluginRuntime::Disabled);
    QTRY_VERIFY(source->resolvedSource() != QUrl(secondUrl));
    QVERIFY(!secondLease->active());
    for (const auto &record : host.runtime.permissions()->records())
        if (record.toMap().value("capability") == "cover")
            QVERIFY(host.runtime.permissions()->revoke(record.toMap().value("id").toString()));
    QTRY_COMPARE(registry->count(), 0);
    QTRY_VERIFY(source->resolvedSource().isEmpty());
    auto *restored = registration(
        host,
        scope->cover()->setFallback(
            "restored",
            js(host,
               "(function(v){return c.cover.createThemedIconCoverUrl('music',v.accentColor);})")));
    QVERIFY(restored);
    auto keepRestored = host.engine.newQObject(restored);
    QTRY_COMPARE(cover->property("status").toInt(), 1);
    host.api.advanceSession();
    QCOMPARE(registry->count(), 0);
    QTRY_VERIFY(source->resolvedSource().isEmpty());
    QVERIFY(!base->active());
    QVERIFY(!broken->active());
}
void PluginRuntimeTest::hostAccessAuthorizationCancellationAndDisable()
{
    Host host;
    auto *scope = hostAccess(host, false);
    QVERIFY(scope);
    QSignalSpy questions(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested);
    auto *read = scope->settings()->read("playback");
    auto keep = host.engine.newQObject(read);
    QTRY_COMPARE(questions.count(), 1);
    host.runtime.permissions()->decide(questions.at(0).at(0).toString(), false);
    QTRY_COMPARE(read->errorCode(), "PermissionDenied");
    auto *pending = scope->ui()->navigation()->navigate(
        {{"name", "search"}, {"query", QVariantMap{{"q", "forbidden"}}}});
    auto keepPending = host.engine.newQObject(pending);
    QTRY_COMPARE(questions.count(), 2);
    pending->cancel();
    host.runtime.permissions()->decide(questions.at(1).at(0).toString(), true);
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(host.router.current()->query().isEmpty());
    auto *fallback = scope->cover()->setFallback(
        "waiting", js(host, "(function(){return c.cover.createThemedIconCoverUrl();})"));
    auto keepFallback = host.engine.newQObject(fallback);
    QTRY_COMPARE(questions.count(), 3);
    host.runtime.setEnabled(host.runtime.installationForId("host-access"), false);
    QTRY_COMPARE(fallback->state(), EchoPluginOperation::Canceled);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("host-access")),
                 PluginRuntime::Disabled);
    QVERIFY(!host.runtime.permissions()->waiting(host.runtime.installationForId("host-access")));
    QCOMPARE(EchoCoverRegistry::forEngine(host.engine)->count(), 0);
}
