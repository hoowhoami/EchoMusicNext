#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginTcp.h"
#include "EchoPluginWebServer.h"
#include "EchoPluginWindow.h"
#include <QNetworkAccessManager>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>
#include <thread>
#include <atomic>
#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#endif
namespace
{
EchoPluginContext *resources(Host &host, const QByteArray &entry = "", bool approve = true)
{
    if (approve)
        QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                         host.runtime.permissions(),
                         [&host](const QString &id, const QVariantMap &) {
                             host.runtime.permissions()->decide(id, true);
                         });
    const auto path = pluginPackage(host.temporary.path(), "resource-fixture",
                                    "export function activate(c){" + entry + "}",
                                    {{"capabilities", QJsonArray{"windows", "tcp", "webServer"}},
                                     {"windows", QJsonArray{QJsonObject{{"id", "tools"},
                                                                        {"title", "工具窗口"},
                                                                        {"component", "Tools.qml"},
                                                                        {"defaultWidth", 360},
                                                                        {"defaultHeight", 240},
                                                                        {"minWidth", 180},
                                                                        {"minHeight", 100}}}}});
    write(path + "/Tools.qml", R"(import QtQuick
import EchoMusic.PluginSdk
Rectangle {
    required property EchoPluginContext plugin
    required property EchoPluginWindow window
    objectName: "resourceWindowContent"
    color: "#3264a0"
    Component.onCompleted: plugin.setTimer("window-timer", 100, true, function(){})
})");
    host.runtime.importDirectory(path);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId("resource-fixture")) ==
                       PluginRuntime::Active;
            },
            7000))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId("resource-fixture"));
}
QString grantId(Host &host, const QString &cap)
{
    for (const auto &r : host.runtime.permissions()->records()) {
        const auto v = r.toMap();
        if (v.value("capability") == cap)
            return v.value("id").toString();
    }
    return {};
}
EchoPluginTcpConnection *connection(EchoPluginOperation *op)
{
    return qobject_cast<EchoPluginTcpConnection *>(op->result().value<QObject *>());
}
QQuickWindow *pluginWindow()
{
    for (auto *w : QGuiApplication::allWindows())
        if (w->objectName() == "pluginWindow-resource-fixture-tools")
            return qobject_cast<QQuickWindow *>(w);
    return nullptr;
}
} // namespace
void PluginRuntimeTest::customWindowsRealEntryScopesAndRetirement()
{
    Host host;
    auto *scope = resources(host);
    QVERIFY(scope);
    auto *handle = scope->windows()->get("tools");
    QVERIFY(handle);
    QVERIFY(!pluginWindow());
    PluginManager manager(host.runtime, host.settings);
    auto card = ecosystemWindow(host, R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Plugins
ApplicationWindow {
    id: root
    required property PluginManager manager
    required property var record
    width: 900; height: 740; visible: true
    PluginCard { width: 500; manager: root.manager; record: root.record }
})",
                                {{"manager", QVariant::fromValue(&manager)},
                                 {"record", host.runtime.record(scope->installationId())}});
    QVERIFY(card);
    auto *cardWindow = qobject_cast<QQuickWindow *>(card.get());
    QVERIFY(cardWindow);
    QVERIFY(QTest::qWaitForWindowExposed(cardWindow));
    QQuickItem *button = nullptr;
    QTRY_VERIFY((button = visual(cardWindow->contentItem(), "pluginOpenWindow-tools")));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QTRY_VERIFY(handle->snapshot().value("visible").toBool());
    auto *w = pluginWindow();
    QVERIFY(w);
    QVERIFY(w->contentItem()->findChild<QQuickItem *>("resourceWindowContent"));
    QCOMPARE(w->size(), QSize(360, 240));
    auto keep = host.engine.newQObject(handle);
    auto *resize = handle->setBounds({{"width", 420}, {"height", 280}});
    QTRY_COMPARE(resize->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(w->size(), QSize(420, 280));
    const auto pos = w->position();
    auto *drag = handle->beginMove(100, 100);
    QCOMPARE(drag->state(), EchoPluginOperation::Succeeded);
    handle->updatePointer(120, 115);
    handle->endPointer();
    QCOMPARE(w->position(), pos + QPoint(20, 15));
    auto *ignore = handle->setIgnoreMouseEvents(true);
    QTRY_COMPARE(ignore->state(), EchoPluginOperation::Succeeded);
    QVERIFY(w->flags() & Qt::WindowTransparentForInput);
    auto *focus = handle->showOnTop(false);
    QTRY_COMPARE(focus->state(), EchoPluginOperation::Succeeded);
    handle->hide();
    QTRY_VERIFY(!pluginWindow());
    QVERIFY(!handle->snapshot().value("visible").toBool());
    auto *reopen = scope->windows()->show("tools");
    QTRY_COMPARE(reopen->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(pluginWindow()->size(), QSize(420, 280));
    auto authorization = ecosystemWindow(host, R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Plugins
import EchoMusic.Theme
ApplicationWindow {
    id: root
    required property PluginManager manager
    width: 900; height: 740; visible: true; color: Theme.main
    PluginGrantsDialog { manager: root.manager; Component.onCompleted: open() }
})",
                                         {{"manager", QVariant::fromValue(&manager)}});
    QVERIFY(authorization);
    auto *authorizationWindow = qobject_cast<QQuickWindow *>(authorization.get());
    QVERIFY(authorizationWindow);
    QVERIFY(QTest::qWaitForWindowExposed(authorizationWindow));
    auto *dialog = authorization->findChild<QObject *>("pluginGrantsDialog");
    QVERIFY(dialog);
    auto *permissions = dialog->findChild<QQuickItem *>("pluginResourcePermissions");
    QVERIFY(permissions);
    QTRY_COMPARE(permissions->property("count").toInt(), 1);
    const auto output = qEnvironmentVariable("ECHO_PLUGIN_UI_OUTPUT");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        for (double scale : {1.0, 1.25}) {
            dialog->setProperty("uiScale", scale);
            for (bool dark : {false, true}) {
                host.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                QTest::qWait(400);
                QVERIFY(dialog->property("width").toDouble() * scale <=
                        authorizationWindow->width());
                QVERIFY(authorizationWindow->grabWindow().save(
                    output + QString("/resource-permissions-%1-%2.png")
                                 .arg(dark ? "dark" : "light")
                                 .arg(scale == 1.0 ? "100" : "125")));
            }
        }
    }
    QQuickItem *revoke = nullptr;
    QTRY_VERIFY((revoke = visual(authorizationWindow->contentItem(),
                                 "pluginResourceRevoke-" + grantId(host, "windows"))));
    QVERIFY(QMetaObject::invokeMethod(revoke, "clicked"));
    QTRY_VERIFY(!pluginWindow());
    authorization.reset();
    QVERIFY(!scope->permissions()->granted("windows"));
    auto *owner = new QObject;
    auto *presentation = scope->createPresentation(owner, "window-caller");
    QVERIFY(presentation);
    auto *pendingWindow = presentation->windows()->show("tools");
    auto pendingRoot = host.engine.newQObject(pendingWindow);
    delete owner;
    QTRY_COMPARE(pendingWindow->state(), EchoPluginOperation::Canceled);
    QTest::qWait(20);
    QVERIFY(!pluginWindow());
    auto *again = scope->windows()->show("tools");
    QTRY_COMPARE(again->state(), EchoPluginOperation::Succeeded);
    QVERIFY(pluginWindow());
    const auto installation = scope->installationId();
    QPointer<EchoPluginContext> retired(scope);
    host.runtime.setEnabled(installation, false);
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QTRY_VERIFY(retired.isNull());
    QVERIFY(!pluginWindow());
}
void PluginRuntimeTest::resourceAuthorizationDenialCancelAndIsolation()
{
    Host host;
    auto *scope = resources(host, {}, false);
    QVERIFY(scope);
    QSignalSpy prompts(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested);
    // Resolve synchronously to avoid presenting native UI in the headless probe.
    auto denied =
        QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                         host.runtime.permissions(), [&](const QString &id, const QVariantMap &) {
                             host.runtime.permissions()->decide(id, false);
                         });
    auto *op = scope->net()->tcp()->connect({{"host", "127.0.0.1"}, {"port", 12345}});
    QCOMPARE(op->state(), EchoPluginOperation::Failed);
    QCOMPARE(op->errorCode(), "PermissionDenied");
    QCOMPARE(prompts.size(), 1);
    QVERIFY(host.runtime.permissions()->records().isEmpty());
    QObject::disconnect(denied);
    QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                     host.runtime.permissions(), [&](const QString &id, const QVariantMap &) {
                         QTimer::singleShot(0, host.runtime.permissions(), [&, id] {
                             host.runtime.permissions()->decide(id, true);
                         });
                     });
    auto *pending = scope->permissions()->request("tcp", "取消测试");
    pending->cancel();
    QTest::qWait(5);
    QVERIFY(!scope->permissions()->granted("tcp"));
    auto *allowed = scope->permissions()->request("tcp", "连接设备");
    QTRY_COMPARE(allowed->state(), EchoPluginOperation::Succeeded);
    QVERIFY(scope->permissions()->granted("tcp"));
    QCOMPARE(host.runtime.permissions()->records().size(), 1);
    auto pkg = scope->package();
    pkg.installationId = "another-install";
    auto *other = new EchoPluginContext(pkg, 900, host.engine, scope->host(),
                                        host.temporary.path() + "/other", &host.runtime);
    QVERIFY(!other->permissions()->granted("tcp"));
    other->beginRetire();
    QTRY_VERIFY(other->drained());
    delete other;
    pkg = scope->package();
    pkg.contentHash = "changed-content";
    auto *updated = new EchoPluginContext(pkg, 901, host.engine, scope->host(),
                                          host.temporary.path() + "/updated", &host.runtime);
    QVERIFY(!updated->permissions()->granted("tcp"));
    updated->beginRetire();
    QTRY_VERIFY(updated->drained());
    delete updated;
    const auto before = prompts.size();
    auto *first = scope->permissions()->request("webServer", "并发调用");
    auto *second = scope->permissions()->request("webServer", "并发调用");
    first->cancel();
    QTRY_COMPARE(second->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(prompts.size(), before + 1);
    QTcpServer peer;
    QVERIFY(peer.listen(QHostAddress::LocalHost));
    auto *racing =
        scope->net()->tcp()->connect({{"host", "127.0.0.1"}, {"port", peer.serverPort()}});
    auto racingRoot = host.engine.newQObject(racing);
    QVERIFY(host.runtime.permissions()->revoke(grantId(host, "tcp")));
    QTRY_COMPARE(racing->state(), EchoPluginOperation::Failed);
    QCOMPARE(racing->errorCode(), "PermissionDenied");
    QVERIFY(!peer.hasPendingConnections());
    auto *restoreGrant = scope->permissions()->request("tcp", "重开账本测试");
    QTRY_COMPARE(restoreGrant->state(), EchoPluginOperation::Succeeded);
    EchoMusic::SettingsStore reopened(host.settings.fileName());
    PluginPermissionStore store(reopened, nullptr);
    QVERIFY(store.granted(*scope, "tcp"));
}
void PluginRuntimeTest::tcpRealBinaryCancellationAndRevocation()
{
    Host host;
    auto *scope = resources(host);
    QVERIFY(scope);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    host.engine.globalObject().setProperty("__resourceScope", host.engine.newQObject(scope));
    const auto opened = js(host, QString("__resourceScope.net.tcp.connect({host:'127.0.0.1',port:%"
                                         "1,keepAlive:true,keepAliveInitialDelayMs:1000})")
                                     .arg(server.serverPort()));
    auto *open = qobject_cast<EchoPluginOperation *>(opened.toQObject());
    QVERIFY(open);
    auto root = host.engine.newQObject(open);
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    QVERIFY(server.hasPendingConnections());
    auto *peer = server.nextPendingConnection();
    auto *c = connection(open);
    QVERIFY(c);
    auto cr = host.engine.newQObject(c);
    host.engine.globalObject().setProperty("__resourceConnection", host.engine.newQObject(c));
    auto *read =
        qobject_cast<EchoPluginOperation *>(js(host, "__resourceConnection.read()").toQObject());
    QVERIFY(read);
    auto rr = host.engine.newQObject(read);
    auto *busy = c->read();
    QTRY_COMPARE(busy->state(), EchoPluginOperation::Failed);
    QCOMPARE(busy->errorCode(), "Busy");
    const QByteArray bytes("\0\1binary\xff", 9);
    peer->write(bytes);
    QTRY_COMPARE(read->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(read->result().toByteArray(), bytes);
    auto *write = qobject_cast<EchoPluginOperation *>(
        js(host,
           "__resourceConnection.write(new Uint8Array([0,1,98,105,110,97,114,121,255]).buffer)")
            .toQObject());
    QVERIFY(write);
    auto wr = host.engine.newQObject(write);
    QTRY_COMPARE(write->state(), EchoPluginOperation::Succeeded);
    QTRY_COMPARE(peer->bytesAvailable(), qint64(bytes.size()));
    QCOMPARE(peer->readAll(), bytes);
    auto *large = c->write(QByteArray(1024 * 1024 + 1, 'a'));
    QCOMPARE(large->errorCode(), "InvalidArgument");
    auto *end =
        qobject_cast<EchoPluginOperation *>(js(host, "__resourceConnection.end()").toQObject());
    QVERIFY(end);
    auto er = host.engine.newQObject(end);
    QTRY_COMPARE(end->state(), EchoPluginOperation::Succeeded);
    auto *eof = c->read();
    auto eofRoot = host.engine.newQObject(eof);
    QTRY_COMPARE(eof->state(), EchoPluginOperation::Succeeded);
    QVERIFY(eof->result().isNull());
    host.engine.globalObject().setProperty("__resourceEof", host.engine.newQObject(eof));
    QVERIFY(js(host, "__resourceEof.result === null").toBool());
    c->close();
    QTRY_VERIFY(c->closed());
    auto *open2 =
        scope->net()->tcp()->connect({{"host", "127.0.0.1"}, {"port", server.serverPort()}});
    auto or2 = host.engine.newQObject(open2);
    QTRY_COMPARE(open2->state(), EchoPluginOperation::Succeeded);
    auto *c2 = connection(open2);
    QVERIFY(c2);
    auto c2r = host.engine.newQObject(c2);
    auto *pending = c2->read();
    auto pr = host.engine.newQObject(pending);
    pending->cancel();
    QTRY_VERIFY(c2->closed());
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    auto *open3 =
        scope->net()->tcp()->connect({{"host", "127.0.0.1"}, {"port", server.serverPort()}});
    auto or3 = host.engine.newQObject(open3);
    QTRY_COMPARE(open3->state(), EchoPluginOperation::Succeeded);
    auto *c3 = connection(open3);
    QVERIFY(c3);
    auto c3r = host.engine.newQObject(c3);
    auto *pending3 = c3->read();
    auto pr3 = host.engine.newQObject(pending3);
    QVERIFY(host.runtime.permissions()->revoke(grantId(host, "tcp")));
    QTRY_VERIFY(c3->closed());
    QTRY_COMPARE(pending3->state(), EchoPluginOperation::Canceled);
}
void PluginRuntimeTest::localHttpRealRequestsChunkingAndErrors()
{
    Host host;
    auto *scope = resources(host, R"(
        c.webServer.onConnection('mixed',function(s){s.close();});
        c.webServer.onRequest('http', function(r){
            if(r.path==='/pending')return new Promise(function(){});
            if(r.path==='/error')throw new Error('handler failure');
            return {status:201,headers:{'Content-Type':'application/octet-stream','X-Query':String(r.query.q),'X-Values':['one','two'],'X-Request-Values':String(r.headers['x-value'])},body:r.body};
        });
    )");
    QVERIFY(scope);
    auto *listen = scope->webServer()->listen({{"maxConnections", 1}});
    auto lr = host.engine.newQObject(listen);
    QTRY_COMPARE(listen->state(), EchoPluginOperation::Succeeded);
    const auto origin = listen->result().toMap().value("origin").toString();
    QVERIFY(!origin.isEmpty());
    QNetworkAccessManager network;
    auto *reply = network.post(QNetworkRequest(QUrl(origin + "/echo?q=one&q=two")),
                               QByteArray("\0http\xff", 6));
    QSignalSpy finished(reply, &QNetworkReply::finished);
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 201);
    QCOMPARE(reply->readAll(), QByteArray("\0http\xff", 6));
    QCOMPARE(reply->rawHeader("X-Query"), QByteArray("one,two"));
    reply->deleteLater();
    QTcpSocket chunked;
    chunked.connectToHost(QHostAddress::LocalHost,
                          quint16(listen->result().toMap().value("port").toInt()));
    QTRY_COMPARE(chunked.state(), QAbstractSocket::ConnectedState);
    chunked.write(
        "POST /echo HTTP/1.1\r\nHost: " + QUrl(origin).authority().toLatin1() +
        "\r\nTransfer-Encoding: chunked\r\nX-Value: alpha\r\nX-Value: beta\r\n\r\n3\r\nabc\r\n");
    QTest::qWait(5);
    chunked.write("2\r\nde\r\n0\r\n\r\n");
    QTRY_COMPARE(chunked.state(), QAbstractSocket::UnconnectedState);
    const auto raw = chunked.readAll();
    QVERIFY(raw.startsWith("HTTP/1.1 201"));
    QVERIFY(raw.endsWith("abcde"));
    QVERIFY(raw.contains("X-Values: one\r\nX-Values: two\r\n"));
    QVERIFY(raw.contains("X-Request-Values: alpha,beta\r\n"));
    auto *error = network.get(QNetworkRequest(QUrl(origin + "/error")));
    QSignalSpy ef(error, &QNetworkReply::finished);
    QTRY_COMPARE(ef.size(), 1);
    QCOMPARE(error->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 500);
    error->deleteLater();
    auto *waiting = network.get(QNetworkRequest(QUrl(origin + "/pending")));
    QSignalSpy wf(waiting, &QNetworkReply::finished);
    QTRY_COMPARE(scope->webServer()->status().value("pendingRequests").toInt(), 1);
    QWebSocket mixed;
    QSignalSpy mixedConnected(&mixed, &QWebSocket::connected);
    mixed.open(QUrl(QString(origin).replace("http:", "ws:") + "/"));
    QTRY_COMPARE(mixedConnected.size(), 1);
    QTRY_COMPARE(mixed.state(), QAbstractSocket::UnconnectedState);
    QVERIFY(!waiting->isFinished());
    auto *upgrade = scope->webServer()->onUpgrade("independent-upgrade",
                                                  js(host, "(function(){ return true; })"));
    QVERIFY(upgrade);
    upgrade->dispose();
    QTest::qWait(20);
    QVERIFY(!waiting->isFinished());
    QCOMPARE(scope->webServer()->status().value("pendingRequests").toInt(), 1);
    waiting->abort();
    QTRY_COMPARE(scope->webServer()->status().value("pendingRequests").toInt(), 0);
    waiting->deleteLater();
    auto *close = scope->webServer()->close();
    auto clr = host.engine.newQObject(close);
    QTRY_COMPARE(close->state(), EchoPluginOperation::Succeeded);
    QVERIFY(!scope->webServer()->status().value("running").toBool());
    QTcpServer reclaimed;
    QVERIFY(reclaimed.listen(QHostAddress::LocalHost,
                             quint16(listen->result().toMap().value("port").toInt())));
}
void PluginRuntimeTest::localWebSocketUpgradeBinaryPingAndLimits()
{
    Host host;
    auto *scope = resources(host, R"(
        var socketCleanups=0;var authorizedConnections={};var upgradeLinked=false;c.registerCommand('socket-cleanups',function(){return socketCleanups;});c.registerCommand('upgrade-linked',function(){return upgradeLinked;});
        c.webServer.onUpgrade('upgrade',function(r){authorizedConnections[r.connectionId]=true;return {accept:r.query.deny!=='1',protocol:'echo'};});
        c.webServer.onConnection('socket',function(s){upgradeLinked=!!authorizedConnections[s.id]&&s.request.connectionId===s.id;s.message.connect(function(data,binary){if(binary)s.sendBytes(data);else s.sendText('echo:'+data);});return function(){socketCleanups++;};});
    )");
    QVERIFY(scope);
    auto *listen = scope->webServer()->listen(
        {{"path", "/socket"}, {"maxMessageBytes", 64}, {"maxBufferedBytes", 128}});
    auto lr = host.engine.newQObject(listen);
    QTRY_COMPARE(listen->state(), EchoPluginOperation::Succeeded);
    const auto origin = listen->result().toMap().value("origin").toString();
    const auto url = QUrl(QString(origin).replace("http:", "ws:") + "/socket");
    QWebSocket socket;
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({"echo"});
    socket.open(url, options);
    QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
    QCOMPARE(socket.subprotocol(), QString("echo"));
    auto *linked = scope->executeCommand("upgrade-linked");
    auto linkedRoot = host.engine.newQObject(linked);
    QTRY_COMPARE(linked->state(), EchoPluginOperation::Succeeded);
    QVERIFY(linked->result().toBool());
    QSignalSpy text(&socket, &QWebSocket::textMessageReceived);
    socket.sendTextMessage("hello");
    QTRY_COMPARE(text.size(), 1);
    QCOMPARE(text[0][0].toString(), QString("echo:hello"));
    QSignalSpy binary(&socket, &QWebSocket::binaryMessageReceived);
    const QByteArray bytes("\0\xff"
                           "abc",
                           5);
    socket.sendBinaryMessage(bytes);
    QTRY_COMPARE(binary.size(), 1);
    QCOMPARE(binary[0][0].toByteArray(), bytes);
    QSignalSpy pong(&socket, &QWebSocket::pong);
    socket.ping("ping");
    QTRY_COMPARE(pong.size(), 1);
    QWebSocket rejected;
    rejected.open(QUrl(url.toString() + "?deny=1"), options);
    QSignalSpy rejectedError(&rejected, &QWebSocket::errorOccurred);
    QTRY_VERIFY(rejectedError.size() > 0);
    QCOMPARE(rejected.state(), QAbstractSocket::UnconnectedState);
    QWebSocket foreign("https://foreign.invalid");
    foreign.open(url, options);
    QSignalSpy foreignError(&foreign, &QWebSocket::errorOccurred);
    QTRY_VERIFY(foreignError.size() > 0);
    socket.sendTextMessage(QString(65, 'x'));
    QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
    QTRY_COMPARE(scope->webServer()->status().value("connections").toInt(), 0);
    auto *cleaned = scope->executeCommand("socket-cleanups");
    auto cleanedRoot = host.engine.newQObject(cleaned);
    QTRY_COMPARE(cleaned->state(), EchoPluginOperation::Succeeded);
    QVERIFY(cleaned->result().toInt() >= 1);
    QWebSocket live;
    live.open(url, options);
    QTRY_COMPARE(live.state(), QAbstractSocket::ConnectedState);
    QVERIFY(host.runtime.permissions()->revoke(grantId(host, "webServer")));
    QTRY_COMPARE(live.state(), QAbstractSocket::UnconnectedState);
    QTRY_VERIFY(!scope->webServer()->status().value("running").toBool());
}
void PluginRuntimeTest::resourceAccountAndDisableDrainRealTransports()
{
    Host host;
    auto *scope = resources(host);
    QVERIFY(scope);
    auto *server = scope->webServer();
    auto *listen = server->listen();
    auto lr = host.engine.newQObject(listen);
    QTRY_COMPARE(listen->state(), EchoPluginOperation::Succeeded);
    const auto port = listen->result().toMap().value("port").toInt();
    QTcpServer peer;
    QVERIFY(peer.listen(QHostAddress::LocalHost));
    auto *open = scope->net()->tcp()->connect({{"host", "127.0.0.1"}, {"port", peer.serverPort()}});
    auto cr = host.engine.newQObject(open);
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    auto *c = connection(open);
    QVERIFY(c);
    auto connRoot = host.engine.newQObject(c);
    auto *read = c->read();
    auto rr = host.engine.newQObject(read);
    auto *window = scope->windows()->show("tools");
    auto wr = host.engine.newQObject(window);
    QTRY_COMPARE(window->state(), EchoPluginOperation::Succeeded);
    QVERIFY(pluginWindow());
    QVERIFY(host.api.setAccount(42, "test-token", "test-refresh"));
    QTRY_VERIFY(c->closed());
    QCOMPARE(read->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(!pluginWindow());
    QTRY_VERIFY(!server->status().value("running").toBool());
    auto *restart = server->listen({{"port", port}});
    auto sr = host.engine.newQObject(restart);
    QTRY_COMPARE(restart->state(), EchoPluginOperation::Succeeded);
    const auto installation = scope->installationId();
    QPointer<EchoPluginContext> retired(scope);
    host.runtime.setEnabled(installation, false);
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QTRY_VERIFY(retired.isNull());
    QTcpServer reclaimed;
    QVERIFY(reclaimed.listen(QHostAddress::LocalHost, quint16(port)));
}

void PluginRuntimeTest::tcpHalfCloseReceivesResponseAfterFin()
{
#ifndef Q_OS_UNIX
    QSKIP("POSIX FIN probe; Qt transport tests run on every platform");
#else
    Host host;
    auto *scope = resources(host);
    QVERIFY(scope);
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    QVERIFY(listener >= 0);
    const auto listenerGuard = qScopeGuard([listener] { ::close(listener); });
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    QVERIFY(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    QVERIFY(::listen(listener, 1) == 0);
    socklen_t length = sizeof(address);
    QVERIFY(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == 0);
    std::atomic<bool> sawFin = false;
    std::jthread peer([&] {
        pollfd ready{listener, POLLIN, 0};
        if (::poll(&ready, 1, 5000) <= 0)
            return;
        const int socket = ::accept(listener, nullptr, nullptr);
        if (socket < 0)
            return;
        timeval timeout{5, 0};
        ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        char buffer[64];
        ssize_t count;
        while ((count = ::recv(socket, buffer, sizeof(buffer), 0)) > 0) {
        }
        if (count == 0) {
            sawFin = true;
            ::send(socket, "after-fin", 9, 0);
            ::shutdown(socket, SHUT_WR);
        }
        ::close(socket);
    });
    auto *open =
        scope->net()->tcp()->connect({{"host", "127.0.0.1"}, {"port", ntohs(address.sin_port)}});
    auto root = host.engine.newQObject(open);
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    auto *c = connection(open);
    QVERIFY(c);
    auto connRoot = host.engine.newQObject(c);
    auto *write = c->write("before-fin");
    auto wr = host.engine.newQObject(write);
    QTRY_COMPARE(write->state(), EchoPluginOperation::Succeeded);
    auto *end = c->end();
    auto er = host.engine.newQObject(end);
    QTRY_COMPARE(end->state(), EchoPluginOperation::Succeeded);
    auto *read = c->read();
    auto rr = host.engine.newQObject(read);
    QTRY_COMPARE(read->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(read->result().toByteArray(), QByteArray("after-fin"));
    QVERIFY(sawFin.load());
    auto *eof = c->read();
    auto eofRoot = host.engine.newQObject(eof);
    QTRY_COMPARE(eof->state(), EchoPluginOperation::Succeeded);
    QVERIFY(eof->result().isNull());
#endif
}

void PluginRuntimeTest::resourceAuthorizationDoesNotConsumeActivationDeadline()
{
    Host host;
    bool remainedActivating = false;
    connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
            &host.runtime, [&](const QString &id, const QVariantMap &) {
                QTimer::singleShot(5100, &host.runtime, [&] {
                    remainedActivating =
                        host.runtime.state(host.runtime.installationForId("resource-fixture")) ==
                        PluginRuntime::Activating;
                });
                QTimer::singleShot(5200, &host.runtime,
                                   [&, id] { host.runtime.permissions()->decide(id, true); });
            });
    auto *scope = resources(host, R"(return new Promise(function(resolve,reject){
        var operation=c.permissions.request('tcp','启动服务');
        function done(){if(operation.state===2)resolve();else if(operation.state>=3)reject(operation.errorMessage);}
        operation.completed.connect(done);done();
    });)",
                            false);
    QVERIFY(scope);
    QVERIFY(remainedActivating);
    QVERIFY(scope->permissions()->granted("tcp"));
}
