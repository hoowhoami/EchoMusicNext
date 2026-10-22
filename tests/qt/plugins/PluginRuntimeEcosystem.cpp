#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginShortcuts.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include <QKeyEvent>
void PluginRuntimeTest::ecosystemTasksActionsRetentionAndAccountLifetime()
{
    Host host;
    auto *scope = ecosystem(host, R"(
 c.ui.setState({calls:0});
 c.tasks.registerTask('work',{name:'Pending task',status:'pending',priority:42,retention:'action-required',actions:[{id:'go',label:'Start',onClick:function(){c.ui.setState({calls:c.ui.state.calls+1});}}],items:[{id:'one',name:'File one',description:'Real task detail',actions:[{id:'inspect',label:'Inspect',onClick:function(){c.ui.setState({calls:c.ui.state.calls+10});}}]}]});
 )");
    QVERIFY(scope);
    auto *model = host.shell.tasks();
    QCOMPARE(model->count(), 1);
    QAbstractItemModelTester consistency(model,
                                         QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto row = model->index(0);
    const auto key = model->data(row, TaskCenterModel::IdRole).toString(),
               token = model->data(row, TaskCenterModel::TokenRole).toString();
    QCOMPARE(model->data(row, TaskCenterModel::StatusRole).toInt(), int(TaskCenterModel::Pending));
    QVERIFY(model->invokeAction(key, token, "go"));
    QTRY_COMPARE(scope->ui()->state().value("calls").toInt(), 1);
    QVERIFY(model->invokeAction(key, token, "inspect", "one"));
    QTRY_COMPARE(scope->ui()->state().value("calls").toInt(), 11);
    auto *task = scope->tasks()->registerTask(
        "work", js(host, "({name:'Replacement',status:'pending',retention:{completed:{mode:'manual'"
                         "},error:{mode:'manual'},aborted:{mode:'auto',delayMs:25}}})"));
    QVERIFY(task);
    auto keep = host.engine.newQObject(task);
    QCOMPARE(model->count(), 1);
    QVERIFY(!model->invokeAction(key, token, "go"));
    QVERIFY(task->start(js(host, "({progress:{done:1,total:4,label:'Reading'}})")));
    QCOMPARE(task->status(), "running");
    QCOMPARE(model->data(model->index(0), TaskCenterModel::PercentRole).toDouble(), 25.);
    QVERIFY(!task->update(js(host, "({progress:{percent:NaN}})")));
    QVERIFY(task->update(js(host, "({priority:50,icon:'puzzle'})")));
    QCOMPARE(model->data(model->index(0), TaskCenterModel::IconRole).toString(), "puzzle");
    QVERIFY(task->finish("completed"));
    QCOMPARE(task->status(), "completed");
    QVERIFY(task->update(js(host, "({name:'Completed title'})")));
    QTest::qWait(40);
    QCOMPARE(model->count(), 1);
    auto window = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {id:win;required property TaskCenterModel tasks; width:700;height:600;visible:true;TaskCenterDialog {service:win.tasks;Component.onCompleted:open()}})",
                                  {{"tasks", QVariant::fromValue(model)}});
    QVERIFY(window);
    host.api.advanceSession();
    QTRY_COMPARE(model->count(), 0);
    QVERIFY(!task->active());
    QVERIFY(!task->update(js(host, "({name:'Stale'})")));
    auto *aborted = scope->tasks()->registerTask(
        "cancel", js(host, "({name:'Cancel',retention:{completed:{mode:'manual'},error:{mode:'"
                           "manual'},aborted:{mode:'auto',delayMs:25}}})"));
    QVERIFY(aborted);
    auto root = host.engine.newQObject(aborted);
    QSignalSpy cancel(aborted, &EchoPluginTask::cancelRequested);
    QVERIFY(aborted->cancel());
    QCOMPARE(cancel.size(), 1);
    QVERIFY(aborted->active());
    QVERIFY(aborted->canceled());
    QVERIFY(aborted->finish("aborted"));
    QTRY_VERIFY(!aborted->active());
    QTRY_COMPARE(model->count(), 0);
    host.runtime.setEnabled(host.runtime.installationForId("ecosystem"), false);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("ecosystem")),
                 PluginRuntime::Disabled);
}
void PluginRuntimeTest::ecosystemSidebarSongMenuAndPresentation()
{
    Host host;
    host.sidebar.moveBefore("history", "favorites");
    auto *scope = ecosystem(host, R"(
 c.ui.setState({calls:0});
 c.ui.registerSidebarItem('first',{title:'Plugin action',section:'tools',sectionTitle:'Tools',order:1,onClick:function(){c.ui.setState({calls:c.ui.state.calls+1});}});
 c.ui.registerSidebarItem('second',{title:'Second',section:'tools',order:2,onClick:function(){}});
 c.ui.registerShortcutCard('card',{title:'Pinned tool',onClick:function(){c.ui.setState({calls:c.ui.state.calls+10});}});
 c.ui.registerSongMenuItem('inspect',{label:'Inspect song',onSelect:function(v){c.ui.setState({calls:100,id:v.track.id,selected:v.selectedTracks.length});}});
 )");
    QVERIFY(scope);
    auto key = scope->registrationKey("sidebar", "first");
    // Registration keys are the same opaque values consumed by the real sidebar projection.
    const auto sections = host.sidebar.sections();
    QString first, second, card;
    for (const auto &s : sections)
        for (const auto &v : s.toMap().value("items").toList()) {
            auto r = v.toMap();
            if (r.value("title") == "Plugin action")
                first = r.value("key").toString();
            if (r.value("title") == "Second")
                second = r.value("key").toString();
        }
    for (const auto &v : host.sidebar.shortcutCatalog())
        if (v.toMap().value("title") == "Pinned tool")
            card = v.toMap().value("key").toString();
    QVERIFY(!first.isEmpty() && !second.isEmpty() && !card.isEmpty());
    QVERIFY(host.sidebar.activate(first));
    QTRY_COMPARE(scope->ui()->state().value("calls").toInt(), 1);
    host.sidebar.moveBefore(second, first);
    host.sidebar.toggleShortcut(card);
    QVERIFY(host.sidebar.activate(card));
    QTRY_COMPARE(scope->ui()->state().value("calls").toInt(), 11);
    SidebarLayoutModel reopened(host.settings, nullptr);
    QCOMPARE(reopened.data(reopened.index(0), SidebarLayoutModel::Key).toString(), "history");
    host.sidebar.toggleHidden(first);
    for (const auto &v : host.sidebar.sections())
        for (const auto &i : v.toMap().value("items").toList())
            if (i.toMap().value("key") == first)
                QVERIFY(i.toMap().value("hidden").toBool());
    SongCatalogModel songs;
    PlaybackTrack track;
    track.id = "actual-song";
    track.hash = QString(32, 'a');
    track.title = "A song";
    songs.replaceSongs(
        QJsonArray{QJsonObject{{"id", track.id}, {"hash", track.hash}, {"title", track.title}}});
    songs.selectAll(true);
    SongCatalogActions actions(songs, host.playback);
    auto context = actions.pluginMenuContext(track.id);
    QCOMPARE(context.value("selectedTracks").toList().size(), 1);
    auto *menus = EchoSongMenuRegistry::forEngine(host.engine);
    auto entries = menus->entries(context);
    QCOMPARE(entries.size(), 1);
    auto entry = entries.first().toMap();
    QVERIFY(menus->invoke(entry.value("key").toString(), entry.value("token").toString(), context));
    QTRY_COMPARE(scope->ui()->state().value("id").toString(), track.id);
    auto menuWindow = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Music
ApplicationWindow {
 id:win;required property var commands;width:500;height:500;visible:true
 function pickPlugin(): bool {for(let i=0;i<songMenu.count;i++){let row=songMenu.itemAt(i);if(row.action && row.action.text==="Inspect song"){row.action.trigger();songMenu.close();return true;}}return false;}
 SongContextMenu {id:songMenu;objectName:"actualSongMenu";commands:win.commands;songId:"actual-song";Component.onCompleted:Qt.callLater(open)}
})",
                                      {{"commands", QVariant::fromValue(&actions)}});
    QVERIFY(menuWindow);
    auto *songMenu = menuWindow->findChild<QObject *>("actualSongMenu");
    QVERIFY(songMenu);
    QTRY_VERIFY(songMenu->property("visible").toBool());
    scope->ui()->setState({{"id", QString()}, {"calls", 0}});
    bool picked = false;
    QVERIFY(QMetaObject::invokeMethod(menuWindow.get(), "pickPlugin", Q_RETURN_ARG(bool, picked)));
    QVERIFY(picked);
    QTRY_COMPARE(scope->ui()->state().value("id").toString(), track.id);
    host.api.advanceSession();
    QVERIFY(
        !menus->invoke(entry.value("key").toString(), entry.value("token").toString(), context));
    for (int i = 0; i < 24; i++) {
        auto *entry = scope->ui()->registerShortcutCard(
            "bulk-" + QString::number(i),
            js(host, QString("({title:'Bulk %1',onClick:function(){}})").arg(i)));
        QVERIFY(entry);
        host.sidebar.toggleShortcut(entry->key());
    }
    auto window = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {id:win;required property SidebarLayoutModel layout;required property RouteController router; width:300;height:500;visible:true; Column {width:parent.width;SidebarShortcuts {collapsed:false;layout:win.layout;router:win.router;width:parent.width}SidebarNavigation {collapsed:false;layout:win.layout;router:win.router;width:parent.width}}})",
                                  {{"layout", QVariant::fromValue(&host.sidebar)},
                                   {"router", QVariant::fromValue(&host.router)}});
    QVERIFY(window);
    host.runtime.setEnabled(host.runtime.installationForId("ecosystem"), false);
    QTRY_VERIFY(menus->entries(context).isEmpty());
    QTRY_VERIFY(!host.sidebar.isPlugin(first));
    QVERIFY(!host.sidebar.activate(card));
}
void PluginRuntimeTest::ecosystemShortcutsToastAndScroll()
{
    Host host;
    auto *scope = ecosystem(host, "c.ui.setState({calls:0});");
    QVERIFY(scope);
    auto callback =
        scope->engine()
            .evaluate(
                "(function(c){return function(){c.ui.setState({calls:c.ui.state.calls+1});};})")
            .call({host.engine.newQObject(scope)});
    auto *binding = scope->shortcuts()->registerShortcut("key", "Ctrl+Alt+K", callback);
    QVERIFY(binding && binding->active() && binding->registered());
    auto keep = host.engine.newQObject(binding);
    auto *duplicate = scope->shortcuts()->registerShortcut("duplicate", "Ctrl+Alt+K", callback);
    QVERIFY(duplicate && !duplicate->active() && !duplicate->errorMessage().isEmpty());
    auto keepDuplicate = host.engine.newQObject(duplicate);
    auto *global = scope->shortcuts()->registerShortcut("global", "Ctrl+Alt+J", callback, true);
    QVERIFY(global && !global->registered() && !global->errorMessage().isEmpty());
    auto keepGlobal = host.engine.newQObject(global);
    auto notice = scope->toast()->info("Owned notice", {{"durationMs", 0}});
    auto keepNotice = host.engine.newQObject(notice);
    QCOMPARE(notice->state(), EchoPluginOperation::Succeeded);
    auto id = notice->result().toMap().value("id").toString();
    QVERIFY(host.shell.toasts()->containsNotice(id.toULongLong()));
    host.api.advanceSession();
    QVERIFY(!host.shell.toasts()->containsNotice(id.toULongLong()));
    auto window = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow {width:500;height:300;visible:true;EchoPageScrollContainer {anchors.fill:parent;scrollView:rows; ListView {id:rows;objectName:"realScrollList";anchors.fill:parent;model:100;delegate:Item {width:400;height:40}}}})");
    QVERIFY(window);
    auto *win = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(win);
    auto *ref = scope->scroll()->current();
    QTRY_VERIFY(ref = scope->scroll()->current());
    auto root = host.engine.newQObject(ref);
    host.shortcuts.attach(win);
    win->requestActivate();
    QTRY_VERIFY(win->isActive());
    win->contentItem()->forceActiveFocus();
    const auto keyCalls = scope->ui()->state().value("calls").toInt();
    QTest::keyClick(win, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
    QTRY_COMPARE(scope->ui()->state().value("calls").toInt(), keyCalls + 1);
    host.shortcuts.beginRecording(0, false);
    QTest::keyClick(win, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
    QTRY_COMPARE(scope->ui()->state().value("calls").toInt(), keyCalls + 1);
    host.shortcuts.stopRecording();
    QTRY_VERIFY(ref->visible());
    QVERIFY(ref->state().value("maximum").toDouble() > 0);
    auto *move = scope->scroll()->scrollTo(ref, 400);
    auto keepMove = host.engine.newQObject(move);
    QCOMPARE(move->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(ref->state().value("position").toDouble() > 390);
    host.api.advanceSession();
    QVERIFY(!ref->valid());
    auto *stale = scope->scroll()->bottom(ref);
    auto keepStale = host.engine.newQObject(stale);
    QCOMPARE(stale->state(), EchoPluginOperation::Failed);
    binding->dispose();
    QVERIFY(!binding->registered());
    host.runtime.setEnabled(host.runtime.installationForId("ecosystem"), false);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("ecosystem")),
                 PluginRuntime::Disabled);
}
