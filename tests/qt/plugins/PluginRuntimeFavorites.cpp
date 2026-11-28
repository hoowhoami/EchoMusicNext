#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginFavorites.h"
#include "PlaybackFavoriteExtensions.h"
#include "UserPlaylistModel.h"
#include "PlaylistMutationService.h"

namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    // Adopt a presentation without opening an audio device or real stream.
    static void favoriteTrack(PlaybackService &playback, const PlaybackTrack &track)
    {
        playback.m_track = track;
        emit playback.changed();
    }
};
} // namespace EchoMusic
namespace
{
EchoPluginContext *favoriteScope(Host &host, const QByteArray &body, bool network = false)
{
    host.engine.globalObject().setProperty("favoriteProbe", host.engine.newObject());
    auto *library = new UserPlaylistModel(host.api, &host.playback);
    auto *writer = new PlaylistMutationService(host.api, &host.playback);
    host.playback.attachLibrary(*library, *writer);
    host.shell.attachPlayback(host.playback);
    const auto path = package(host.temporary.path(), "export function activate(c){" + body + "}",
                              {"lifecycle", "playback.favorites"},
                              network ? QStringList{"audioSource", "unrestrictedNetwork"}
                                      : QStringList{"audioSource"});
    host.runtime.importDirectory(path);
    if (!QTest::qWaitFor(
            [&] { return host.runtime.state(host.installation()) == PluginRuntime::Active; }, 5000))
        return nullptr;
    auto *scope = host.runtime.context(host.installation());
    host.engine.globalObject().setProperty("c", host.engine.newQObject(scope));
    return scope;
}
PlaybackTrack station(const QString &id = "one")
{
    PlaybackTrack track;
    track.id = "plugin:test.runtime:" + id;
    track.title = "Test station";
    track.catalogMetadata["plugin_source"] = "test.runtime/station/" + id;
    return track;
}
QVariant action(Host &host, ShellActionModel::Role role)
{
    auto *model = host.shell.playerbarActions();
    for (int i = 0; i < model->rowCount(); ++i)
        if (model->data(model->index(i), ShellActionModel::KeyRole).toString() == "host/favorite")
            return model->data(model->index(i), role);
    return {};
}
} // namespace
void PluginRuntimeTest::favoriteProviderPageAndPlayerbarStaySynchronized()
{
    Host host;
    auto *scope = favoriteScope(host, R"(
      var saved=false, writes=0;
      var provider=c.player.favorites.registerProvider('radio',{
        sourcePrefix:'test.runtime/station/',
        getState:function(v){return Promise.resolve({favorite:saved});},
        setState:function(v){writes++;saved=v.favorite;return Promise.resolve({favorite:saved});}
      });
      c.registerCommand('page-save',function(v){saved=v.favorite;provider.invalidate(v.sourceId);return true;});
      c.registerCommand('inspect',function(){return {favorite:saved,writes:writes};});
      c.player.on('page-heart','favoritechange',function(s){c.ui.setState({favorite:s.favorite,known:s.favoriteKnown,busy:s.favoriteBusy});});
    )");
    QVERIFY(scope);
    auto *actions = host.playback.libraryActions();
    QSignalSpy login(actions, &PlaybackLibraryActions::loginRequested);
    QSignalSpy cloudWrites(actions, &PlaybackLibraryActions::favoriteWritten);
    PlaybackServiceTestAccess::favoriteTrack(host.playback, station());
    QTRY_VERIFY(actions->favoriteKnown());
    QVERIFY(actions->favoriteAvailable());
    QVERIFY(!actions->favorite());
    auto object = ecosystemWindow(host, R"(
      import QtQuick
      import EchoMusic.Shell
      Window {id:root;width:1200;height:160;visible:true
        required property ShellController controller
        required property RouteController router
        PlayerBar {anchors.fill:parent;controller:root.controller;router:root.router;uiScale:1}
      }
    )",
                                  {{"controller", QVariant::fromValue(&host.shell)},
                                   {"router", QVariant::fromValue(&host.router)}});
    QVERIFY(object);
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(window);
    QTRY_VERIFY(visual(window->contentItem(), "playerbar-action-favorite"));
    auto *heart = visual(window->contentItem(), "playerbar-action-favorite");
    QTRY_VERIFY(heart->isEnabled());
    const auto captures = qEnvironmentVariable("ECHO_FAVORITES_CAPTURE_DIR");
    const auto capture = [&](const QString &name) {
        if (captures.isEmpty())
            return true;
        QDir().mkpath(captures);
        const auto frame = window->grabWindow();
        return !frame.isNull() && frame.save(captures + '/' + name + ".png");
    };
    QVERIFY(capture("favorite-off"));
    QTest::mouseClick(
        window, Qt::LeftButton, Qt::NoModifier,
        heart->mapToScene(QPointF(heart->width() / 2, heart->height() / 2)).toPoint());
    QTRY_VERIFY(actions->favorite());
    QTRY_VERIFY(scope->ui()->state().value("favorite").toBool());
    QCOMPARE(heart->property("glyph").toString(), "heart-filled");
    QVERIFY(capture("favorite-on"));
    QVERIFY(action(host, ShellActionModel::ActiveRole).toBool());
    auto *save = scope->executeCommand(
        "page-save", {{"favorite", false}, {"sourceId", "test.runtime/station/one"}});
    QTRY_COMPARE(save->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(!actions->favoriteLoading());
    QTRY_VERIFY(!actions->favorite());
    QTRY_COMPARE(heart->property("glyph").toString(), "heart");
    QVERIFY(capture("favorite-page-off"));
    QCOMPARE(login.size(), 0);
    QCOMPARE(cloudWrites.size(), 0);
    // A plugin skin uses the same command and the same page event.
    auto *control = scope->player()->control("favorite");
    QTRY_COMPARE(control->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(scope->ui()->state().value("favorite").toBool());
    QCOMPARE(login.size(), 0);
    object.reset();
}
void PluginRuntimeTest::favoriteProviderErrorsCancellationAndNamespaces()
{
    Host host;
    auto *scope = favoriteScope(host, R"(
      var saved=false, writes=0, mode='normal', finish;
      var provider=c.player.favorites.registerProvider('radio',{
        sourcePrefix:'test.runtime/station/',
        getState:function(v){return {favorite:saved};},
        setState:function(v){writes++;c.ui.setState({writes:writes});if(mode==='error')throw new Error('storage refused');
          if(mode==='pending'){v.request.canceledChanged.connect(function(){c.ui.setState({canceled:v.request.canceled});});return new Promise(function(r){finish=r;});}
          saved=v.favorite;return {favorite:saved};}
      });
      c.registerCommand('mode',function(v){mode=v.value;return true;});
      c.registerCommand('finish',function(){finish({favorite:true});return true;});
      c.registerCommand('dispose',function(){provider.dispose();return true;});
      c.registerCommand('invalidate',function(){return provider.invalidate('test.runtime/station/one');});
      c.registerCommand('writes',function(){return writes;});
    )");
    QVERIFY(scope);
    auto *registry = host.playback.favoriteExtensions();
    auto *actions = host.playback.libraryActions();
    QSignalSpy notices(actions, &PlaybackLibraryActions::notice);
    PlaybackServiceTestAccess::favoriteTrack(host.playback, station());
    QTRY_VERIFY(actions->favoriteKnown());
    const auto mode = [&](const QString &value) {
        auto *op = scope->executeCommand("mode", {{"value", value}});
        return QTest::qWaitFor([op] { return op->finished(); }, 1000);
    };
    QVERIFY(mode("error"));
    actions->toggleFavorite();
    QTRY_VERIFY(!actions->favoriteBusy());
    QTRY_VERIFY(actions->favoriteError().contains("storage refused"));
    QVERIFY(!actions->favorite());
    QVERIFY(!notices.isEmpty());
    QVERIFY(mode("pending"));
    actions->toggleFavorite();
    QTRY_VERIFY(actions->favoriteBusy());
    QVERIFY(action(host, ShellActionModel::BusyRole).toBool());
    QVERIFY(!action(host, ShellActionModel::EnabledRole).toBool());
    actions->toggleFavorite();
    QTRY_COMPARE(scope->ui()->state().value("writes").toInt(), 2);
    auto *count = scope->executeCommand("writes");
    QTRY_VERIFY(count->finished());
    QCOMPARE(count->result().toInt(), 2); // One failed write, one pending write.
    PlaybackServiceTestAccess::favoriteTrack(host.playback, station("two"));
    QTRY_VERIFY(scope->ui()->state().value("canceled").toBool());
    auto *finish = scope->executeCommand("finish");
    QTRY_VERIFY(finish->finished());
    QVERIFY(!actions->favorite());
    QVERIFY(mode("normal"));
    auto *invalidate = scope->executeCommand("invalidate");
    QTRY_VERIFY(invalidate->finished());
    // Namespace capture and overlapping registrations never produce a live owner.
    QVERIFY(!scope->player()->favorites()->registerProvider(
        "foreign", js(host, "({sourcePrefix:'other/station/',getState:function(){return "
                            "{favorite:true}},setState:function(){return {favorite:true}}})")));
    auto *overlap = scope->player()->favorites()->registerProvider(
        "overlap", js(host, "({sourcePrefix:'test.runtime/station/',getState:function(){return "
                            "{favorite:true}},setState:function(){return {favorite:true}}})"));
    QVERIFY(overlap && !overlap->active());
    // A catalog track remains outside the plugin provider, even with source metadata.
    auto catalog = station();
    catalog.hash = QString(32, 'a');
    QVERIFY(!registry->state(catalog).available);
    QSignalSpy catalogLogin(actions, &PlaybackLibraryActions::loginRequested);
    actions->toggleFavoriteTrack(catalog);
    QCOMPARE(catalogLogin.size(),
             1); // Original catalog login path, despite plugin source metadata.
    // A first explicit row command must read and then write, even when that
    // row was never queried and another station is current.
    const auto rowTrack = station("row");
    actions->toggleFavoriteTrack(rowTrack, false);
    QTRY_VERIFY(actions->favoriteTrack(rowTrack));
    QVERIFY(!actions->favorite());
    auto *dispose = scope->executeCommand("dispose");
    QTRY_VERIFY(dispose->finished());
    QVERIFY(!actions->favoriteAvailable());
    actions->toggleFavorite();
    QVERIFY(notices.last().first().toString().contains("不支持收藏"));
}
void PluginRuntimeTest::favoriteProviderTimeoutAbortsLinkedOperation()
{
    Host host;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QPointer<QTcpSocket> socket;
    QObject connectionObserver;
    connect(&server, &QTcpServer::newConnection, &connectionObserver,
            [&] { socket = server.nextPendingConnection(); });
    const auto url = QString("http://127.0.0.1:%1/pending").arg(server.serverPort());
    auto *scope = favoriteScope(host,
                                R"(
      c.player.favorites.registerProvider('radio',{
        sourcePrefix:'test.runtime/station/',
        getState:function(){return {favorite:false};},
        setState:function(v){
          var op=c.net.request({url:')" +
                                    url.toUtf8() + R"('});
          if(!v.request.track(op))throw new Error('tracking failed');
          v.request.canceledChanged.connect(function(){c.ui.setState({canceled:v.request.canceled,aborted:op.state===4});});
          return new Promise(function(){});
        }
      });
    )",
                                true);
    QVERIFY(scope);
    auto *actions = host.playback.libraryActions();
    PlaybackServiceTestAccess::favoriteTrack(host.playback, station());
    QTRY_VERIFY(actions->favoriteKnown());
    actions->toggleFavorite();
    QTRY_VERIFY(actions->favoriteBusy());
    QTRY_VERIFY(socket);
    QTRY_VERIFY_WITH_TIMEOUT(!actions->favoriteBusy(), 6500);
    QVERIFY(actions->favoriteError().contains("超时"));
    QTRY_VERIFY(scope->ui()->state().value("canceled").toBool());
    QTRY_VERIFY(scope->ui()->state().value("aborted").toBool());
    QTRY_COMPARE(socket->state(), QAbstractSocket::UnconnectedState);
    QVERIFY(!actions->favorite());
    host.runtime.setEnabled(host.installation(), false);
    QTRY_VERIFY(!actions->favoriteAvailable());
    socket->deleteLater();
}

void PluginRuntimeTest::favoriteProviderReplacementAndAccountRetireOldResults()
{
    Host host;
    QVERIFY(host.api.setNetworkTransport(1));
    auto *scope = favoriteScope(host, R"(
      c.player.favorites.registerProvider('radio',{
        sourcePrefix:'test.runtime/station/',
        getState:function(){return {favorite:false};},
        setState:function(v){
          v.request.canceledChanged.connect(function(){c.ui.setState({canceled:v.request.canceled});});
          return new Promise(function(r){favoriteProbe.finishFavorite=r;});
        }
      });
    )");
    QVERIFY(scope);
    QSignalSpy invalidated(scope, &EchoPluginContext::sessionInvalidated);
    auto *actions = host.playback.libraryActions();
    PlaybackServiceTestAccess::favoriteTrack(host.playback, station());
    QTRY_VERIFY(actions->favoriteKnown());
    actions->toggleFavorite();
    QTRY_VERIFY(js(host, "typeof favoriteProbe.finishFavorite === 'function'").toBool());
    auto *replacement = scope->player()->favorites()->registerProvider(
        "radio",
        js(host, "({sourcePrefix:'test.runtime/station/',getState:function(){return "
                 "{favorite:false}},setState:function(v){return {favorite:v.favorite}}})"));
    QVERIFY(replacement && replacement->active());
    QTRY_VERIFY(scope->ui()->state().value("canceled").toBool());
    QTRY_VERIFY(actions->favoriteKnown());
    js(host, "favoriteProbe.finishFavorite({favorite:true})");
    QCoreApplication::processEvents();
    QVERIFY(!actions->favorite());
    const QPointer<EchoPluginFavoriteProvider> pending =
        scope->player()->favorites()->registerProvider(
            "radio", js(host, "({sourcePrefix:'test.runtime/station/',getState:function(){return "
                              "{favorite:false}},setState:function(v){return new "
                              "Promise(function(r){favoriteProbe.finishOldAccount=r})}})"));
    QVERIFY(pending && pending->active());
    QTRY_VERIFY(actions->favoriteKnown());
    actions->toggleFavorite();
    QTRY_VERIFY(js(host, "typeof favoriteProbe.finishOldAccount === 'function'").toBool());
    QVERIFY(host.api.setAccount(100, "offline-favorite-test"));
    QTRY_COMPARE(invalidated.size(), 1);
    QVERIFY(scope->active()); // Main activation remains available to the new account.
    QVERIFY(pending && pending->active());
    PlaybackServiceTestAccess::favoriteTrack(host.playback, station());
    QTRY_VERIFY(actions->favoriteKnown());
    QVERIFY(pending->invalidate("test.runtime/station/one"));
    QTRY_VERIFY(!actions->favoriteLoading());
    js(host, "favoriteProbe.finishOldAccount({favorite:true})");
    QCoreApplication::processEvents();
    QVERIFY(!actions->favorite());
}
