#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginWebSocketClient.h"
#include <QNetworkProxyFactory>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QWebSocket>
#include <QWebSocketServer>

namespace
{
EchoPluginContext *clientScope(Host &host, bool approve = true, bool declare = true)
{
    if (approve)
        QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                         host.runtime.permissions(),
                         [&host](const QString &id, const QVariantMap &) {
                             host.runtime.permissions()->decide(id, true);
                         });
    const auto path = pluginPackage(
        host.temporary.path(), "ws-client", "export function activate(c){}",
        {{"capabilities", declare ? QJsonArray{"webSocket"} : QJsonArray{}},
         {"requires", QJsonObject{{"hostVersion", ">=0.1.0"},
                                  {"sdkVersion", ">=1.0.0"},
                                  {"features", QJsonArray{"net.webSocket.client"}}}}});
    host.runtime.importDirectory(path);
    if (!QTest::qWaitFor([&] {
            return host.runtime.state(host.runtime.installationForId("ws-client")) ==
                   PluginRuntime::Active;
        }))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId("ws-client"));
}
EchoPluginWebSocketConnection *clientConnection(EchoPluginOperation *op)
{
    return qobject_cast<EchoPluginWebSocketConnection *>(op->result().value<QObject *>());
}
void echoPeer(QWebSocket *peer)
{
    QObject::connect(peer, &QWebSocket::textMessageReceived, peer,
                     [peer](const QString &text) { peer->sendTextMessage("echo:" + text); });
    QObject::connect(peer, &QWebSocket::binaryMessageReceived, peer,
                     &QWebSocket::sendBinaryMessage);
}
QString webSocketGrant(Host &host)
{
    for (const auto &value : host.runtime.permissions()->records()) {
        const auto record = value.toMap();
        if (record.value("capability") == "webSocket")
            return record.value("id").toString();
    }
    return {};
}
} // namespace

void PluginRuntimeTest::outboundWebSocketDuplexAndClose()
{
    Host host;
    auto *scope = clientScope(host);
    QVERIFY(scope);
    QVERIFY(!scope->package().capabilities.contains("webServer"));
    QWebSocketServer server("outbound", QWebSocketServer::NonSecureMode);
    server.setSupportedSubprotocols({"echo"});
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QJSValue> roots;
    const auto keep = [&](auto *o) {
        roots.append(host.engine.newQObject(o));
        return o;
    };
    auto *open = keep(scope->net()->webSocket()->connect(
        server.serverUrl().toString(),
        {{"protocols", QStringList{"echo"}}, {"headers", QVariantMap{{"X-Plugin-Test", "yes"}}}}));
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    auto *socket = keep(clientConnection(open));
    QVERIFY(socket);
    QCOMPARE(socket->protocol(), "echo");
    auto *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QCOMPARE(peer->request().rawHeader("X-Plugin-Test"), "yes");
    echoPeer(peer);
    QSignalSpy messages(socket, &EchoPluginWebSocketConnection::message);
    QSignalSpy pong(socket, &EchoPluginWebSocketConnection::pong);
    for (int i = 0; i < 3; ++i) {
        auto *sent = keep(socket->sendText(QString::number(i) + "推送"));
        QTRY_COMPARE(sent->state(), EchoPluginOperation::Succeeded);
        QTRY_COMPARE(messages.size(), 2 * i + 1);
        QCOMPARE(messages.last()[0].toString(), "echo:" + QString::number(i) + "推送");
        QVERIFY(!messages.last()[1].toBool());
        const QByteArray bytes("\0\xff\x01", 3);
        auto *binary = keep(socket->sendBytes(bytes));
        QTRY_COMPARE(binary->state(), EchoPluginOperation::Succeeded);
        QTRY_COMPARE(messages.size(), 2 * i + 2);
        QCOMPARE(messages.last()[0].toByteArray(), bytes);
        QVERIFY(messages.last()[1].toBool());
    }
    host.engine.globalObject().setProperty("__outboundSocket", host.engine.newQObject(socket));
    QVERIFY(!js(host, R"(
        var __outboundBinary = false;
        __outboundSocket.message.connect(function(data, binary) {
            if (binary) __outboundBinary = data instanceof ArrayBuffer && new Uint8Array(data)[1] === 255;
        });
        __outboundSocket.sendBytes(new Uint8Array([0, 255, 2]).buffer);
    )")
                 .isError());
    QTRY_COMPARE(messages.size(), 7);
    QTRY_VERIFY(js(host, "__outboundBinary").toBool());
    const QByteArray fullMessage(1024 * 1024, 'b');
    auto *large = keep(socket->sendBytes(fullMessage));
    QTRY_COMPARE(large->state(), EchoPluginOperation::Succeeded);
    QTRY_COMPARE(messages.size(), 8);
    QCOMPARE(messages.last()[0].toByteArray(), fullMessage);
    keep(socket->ping("probe"));
    QTRY_COMPARE(pong.size(), 1);
    QCOMPARE(pong[0][1].toByteArray(), "probe");
    // Server-originated push requires no preceding client request.
    peer->sendTextMessage("push");
    QTRY_COMPARE(messages.size(), 9);
    QCOMPARE(messages.last()[0].toString(), "push");
    QSignalSpy closed(socket, &EchoPluginWebSocketConnection::closed);
    peer->close(QWebSocketProtocol::CloseCode(4001), "remote done");
    QTRY_COMPARE(closed.size(), 1);
    QCOMPARE(socket->readyState(), 3);
    QCOMPARE(socket->closeCode(), 4001);
    QCOMPARE(socket->closeReason(), "remote done");
    QCOMPARE(closed[0][0].toInt(), 4001);
    host.engine.globalObject().setProperty("__outboundApi",
                                           host.engine.newQObject(scope->net()->webSocket()));
    host.engine.globalObject().setProperty("__outboundUrl", server.serverUrl().toString());
    const auto scriptConnect =
        js(host, "__outboundApi.connect(__outboundUrl, {protocols: ['echo']})");
    auto *again = keep(qobject_cast<EchoPluginOperation *>(scriptConnect.toQObject()));
    QVERIFY(again);
    QTRY_COMPARE(again->state(), EchoPluginOperation::Succeeded);
    auto *local = keep(clientConnection(again));
    QVERIFY(local);
    auto *localPeer = server.nextPendingConnection();
    QVERIFY(localPeer);
    local->close(4002, "client done");
    QCOMPARE(local->readyState(), 2);
    QTRY_COMPARE(local->readyState(), 3);
    QCOMPARE(localPeer->closeCode(), QWebSocketProtocol::CloseCode(4002));
    QCOMPARE(localPeer->closeReason(), "client done");
}

void PluginRuntimeTest::outboundWebSocketErrorsAuthorizationAndLimits()
{
    Host host;
    auto *scope = clientScope(host);
    QVERIFY(scope);
    QList<QJSValue> roots;
    const auto keep = [&](auto *o) {
        roots.append(host.engine.newQObject(o));
        return o;
    };
    for (const auto &url :
         {"http://localhost/", "ws://user:secret@localhost/", "ws://localhost/#x"}) {
        auto *op = keep(scope->net()->webSocket()->connect(url));
        QCOMPARE(op->errorCode(), "InvalidArgument");
    }
    auto *badOption = keep(scope->net()->webSocket()->connect(
        "wss://localhost", {{"tls", QVariantMap{{"rejectUnauthorized", false}}}}));
    QCOMPARE(badOption->errorCode(), "InvalidArgument");
    for (const auto &options :
         QList<QVariantMap>{{{"protocols", QStringList{"echo\n"}}},
                            {{"headers", QVariantMap{{"Bad\n", "value"}}}},
                            {{"headers", QVariantMap{{"Sec-WebSocket-Key", "override"}}}},
                            {{"connectTimeoutMs", false}}}) {
        auto *invalid = keep(scope->net()->webSocket()->connect("ws://localhost/", options));
        QCOMPARE(invalid->errorCode(), "InvalidArgument");
    }
    QTcpServer reject;
    QVERIFY(reject.listen(QHostAddress::LocalHost));
    QObject::connect(&reject, &QTcpServer::newConnection, &reject, [&reject] {
        auto *p = reject.nextPendingConnection();
        QObject::connect(p, &QTcpSocket::readyRead, p, [p] {
            p->readAll();
            p->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
            p->disconnectFromHost();
        });
    });
    auto *failed = keep(
        scope->net()->webSocket()->connect(QString("ws://127.0.0.1:%1/").arg(reject.serverPort())));
    QTRY_COMPARE(failed->state(), EchoPluginOperation::Failed);
    QCOMPARE(failed->errorCode(), "NetworkError");
    QVERIFY(!failed->errorMessage().isEmpty());
    QWebSocketServer server("limit", QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    auto *open = keep(scope->net()->webSocket()->connect(server.serverUrl().toString()));
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    auto *socket = keep(clientConnection(open));
    QVERIFY(socket);
    auto *tooBig = keep(socket->sendBytes(QByteArray(1024 * 1024 + 1, 'x')));
    QCOMPARE(tooBig->errorCode(), "InvalidArgument");
    auto *ping = keep(socket->ping(QByteArray(126, 'x')));
    QCOMPARE(ping->errorCode(), "InvalidArgument");
    QSignalSpy error(socket, &EchoPluginWebSocketConnection::error);
    socket->close(1006);
    QCOMPARE(error.size(), 1);
    QCOMPARE(socket->readyState(), 1);
    auto *peer = server.nextPendingConnection();
    QVERIFY(peer);
    peer->sendBinaryMessage(QByteArray(1024 * 1024 + 1, 'x'));
    QTRY_COMPARE(socket->readyState(), 3);
    QCOMPARE(socket->closeCode(), 1009);
    auto *cancelOpen = keep(scope->net()->webSocket()->connect(server.serverUrl().toString()));
    QTRY_COMPARE(cancelOpen->state(), EchoPluginOperation::Succeeded);
    auto *cancelSocket = keep(clientConnection(cancelOpen));
    QVERIFY(cancelSocket);
    auto *cancelPeer = server.nextPendingConnection();
    QVERIFY(cancelPeer);
    auto *cancelSend = keep(cancelSocket->sendText("cancel"));
    cancelSend->cancel();
    QCOMPARE(cancelSend->state(), EchoPluginOperation::Canceled);
    QCOMPARE(cancelSocket->readyState(), 3);
    QTRY_COMPARE(cancelPeer->state(), QAbstractSocket::UnconnectedState);
    Host undeclared;
    auto *noCapability = clientScope(undeclared, false, false);
    QVERIFY(noCapability);
    auto *denied = noCapability->net()->webSocket()->connect("ws://localhost/");
    auto deniedRoot = undeclared.engine.newQObject(denied);
    QCOMPARE(denied->errorCode(), "CapabilityDenied");
    Host refused;
    auto *refusedScope = clientScope(refused, false);
    QVERIFY(refusedScope);
    QObject::connect(refused.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                     refused.runtime.permissions(),
                     [&refused](const QString &id, const QVariantMap &) {
                         refused.runtime.permissions()->decide(id, false);
                     });
    auto *refusedOp = refusedScope->net()->webSocket()->connect(server.serverUrl().toString());
    auto refusedRoot = refused.engine.newQObject(refusedOp);
    QTRY_COMPARE(refusedOp->state(), EchoPluginOperation::Failed);
    QCOMPARE(refusedOp->errorCode(), "PermissionDenied");
}

void PluginRuntimeTest::outboundWebSocketTls()
{
    const auto original = QSslConfiguration::defaultConfiguration();
    const auto restore =
        qScopeGuard([original] { QSslConfiguration::setDefaultConfiguration(original); });
    QFile certFile(QFINDTESTDATA("../../fixtures/network/proxy-test-cert.pem"));
    QFile keyFile(QFINDTESTDATA("../../fixtures/network/proxy-test-key.pem"));
    QFile caFile(QFINDTESTDATA("../../fixtures/network/proxy-test-ca.pem"));
    QVERIFY(certFile.open(QIODevice::ReadOnly));
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    QVERIFY(caFile.open(QIODevice::ReadOnly));
    const QSslCertificate cert(certFile.readAll());
    const QSslKey key(keyFile.readAll(), QSsl::Rsa);
    const QSslCertificate ca(caFile.readAll());
    QVERIFY(!cert.isNull() && !key.isNull() && !ca.isNull());
    auto configuration = original;
    configuration.setLocalCertificate(cert);
    configuration.setPrivateKey(key);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    QWebSocketServer server("tls", QWebSocketServer::SecureMode);
    server.setSslConfiguration(configuration);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const auto url = QString("wss://localhost:%1/").arg(server.serverPort());
    Host host;
    auto *scope = clientScope(host);
    QVERIFY(scope);
    QList<QJSValue> roots;
    const auto keep = [&](auto *o) {
        roots.append(host.engine.newQObject(o));
        return o;
    };
    auto *untrusted = keep(scope->net()->webSocket()->connect(url));
    QTRY_COMPARE(untrusted->state(), EchoPluginOperation::Failed);
    QCOMPARE(untrusted->errorCode(), "TlsError");
    auto trusted = original;
    trusted.addCaCertificate(ca);
    QSslConfiguration::setDefaultConfiguration(trusted);
    auto *open = keep(scope->net()->webSocket()->connect(url));
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    auto *socket = keep(clientConnection(open));
    QVERIFY(socket);
    auto *peer = server.nextPendingConnection();
    QVERIFY(peer);
    echoPeer(peer);
    QSignalSpy messages(socket, &EchoPluginWebSocketConnection::message);
    keep(socket->sendText("secure"));
    keep(socket->sendBytes(QByteArray("\0\xff", 2)));
    QTRY_COMPARE(messages.size(), 2);
    QCOMPARE(messages[0][0].toString(), "echo:secure");
    QCOMPARE(messages[1][0].toByteArray(), QByteArray("\0\xff", 2));
    socket->close();
    QTRY_COMPARE(socket->readyState(), 3);
}

void PluginRuntimeTest::outboundWebSocketLifecycle()
{
    Host host;
    auto *scope = clientScope(host);
    QVERIFY(scope);
    QList<QJSValue> roots;
    const auto keep = [&](auto *o) {
        roots.append(host.engine.newQObject(o));
        return o;
    };
    QTcpServer stalled;
    QVERIFY(stalled.listen(QHostAddress::LocalHost));
    const auto url = QString("ws://127.0.0.1:%1/").arg(stalled.serverPort());
    auto *timeout = keep(scope->net()->webSocket()->connect(url, {{"connectTimeoutMs", 30}}));
    QTRY_COMPARE(timeout->state(), EchoPluginOperation::Failed);
    QCOMPARE(timeout->errorCode(), "Timeout");
    while (stalled.hasPendingConnections())
        stalled.nextPendingConnection()->deleteLater();
    auto *opening = keep(scope->net()->webSocket()->connect(url));
    QTRY_VERIFY(stalled.hasPendingConnections());
    auto *openingPeer = stalled.nextPendingConnection();
    QVERIFY(openingPeer);
    opening->cancel();
    QCOMPARE(opening->state(), EchoPluginOperation::Canceled);
    QTRY_COMPARE(openingPeer->state(), QAbstractSocket::UnconnectedState);
    QWebSocketServer server("lifecycle", QWebSocketServer::NonSecureMode);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    for (int action = 0; action < 4; ++action) {
        auto *open = keep(scope->net()->webSocket()->connect(server.serverUrl().toString()));
        QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
        auto *socket = keep(clientConnection(open));
        QVERIFY(socket);
        auto *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QSignalSpy messages(socket, &EchoPluginWebSocketConnection::message);
        auto *pending = keep(scope->net()->webSocket()->connect(url));
        QTRY_VERIFY(stalled.hasPendingConnections());
        auto *handshake = stalled.nextPendingConnection();
        QVERIFY(handshake);
        QTRY_COMPARE(handshake->state(), QAbstractSocket::ConnectedState);
        peer->sendTextMessage("late push");
        const auto installation = scope->installationId();
        QPointer<EchoPluginContext> old(scope);
        if (action == 0)
            QVERIFY(host.runtime.permissions()->revoke(webSocketGrant(host)));
        else if (action == 1)
            QVERIFY(host.api.setAccount(42, "websocket-test", "refresh"));
        else if (action == 2)
            QVERIFY(host.runtime.reload(installation));
        else
            QVERIFY(host.runtime.setEnabled(installation, false));
        if (action < 2)
            QCOMPARE(socket->readyState(), 3);
        else
            QTRY_COMPARE(socket->readyState(), 3);
        const auto deliveredBeforeRetirement = messages.size();
        QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
        QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
        QTRY_COMPARE(handshake->state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(messages.size(), deliveredBeforeRetirement);
        if (action < 2)
            QCOMPARE(messages.size(), 0);
        if (action == 2) {
            QTRY_VERIFY(old.isNull());
            QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
            scope = host.runtime.context(installation);
            QVERIFY(scope);
        } else if (action == 3) {
            QTRY_VERIFY(old.isNull());
            QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
        }
    }
}

void PluginRuntimeTest::outboundWebSocketHostProxy_data()
{
    QTest::addColumn<bool>("secure");
    QTest::newRow("ws") << false;
    QTest::newRow("wss") << true;
}
void PluginRuntimeTest::outboundWebSocketHostProxy()
{
    QFETCH(bool, secure);
    const auto originalSsl = QSslConfiguration::defaultConfiguration();
    const auto restoreSsl =
        qScopeGuard([originalSsl] { QSslConfiguration::setDefaultConfiguration(originalSsl); });
    const auto previousProxy = QNetworkProxy::applicationProxy();
    const auto restore =
        qScopeGuard([previousProxy] { QNetworkProxy::setApplicationProxy(previousProxy); });
    Host host({}, true);
    auto *scope = clientScope(host);
    QVERIFY(scope);
    QWebSocketServer server("proxy-target", secure ? QWebSocketServer::SecureMode
                                                   : QWebSocketServer::NonSecureMode);
    QObject::connect(&server, &QWebSocketServer::sslErrorsOccurred, &server,
                     [](QSslSocket *, const QList<QSslError> &errors) {
                         for (const auto &error : errors)
                             qWarning() << "Local WebSocket TLS server:" << error.errorString();
                     });
    if (secure) {
        QFile cert(QFINDTESTDATA("../../fixtures/network/plugin-websocket-test-cert.pem"));
        QFile key(QFINDTESTDATA("../../fixtures/network/plugin-websocket-test-key.pem"));
        QFile ca(QFINDTESTDATA("../../fixtures/network/plugin-websocket-test-ca.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly));
        QVERIFY(key.open(QIODevice::ReadOnly));
        QVERIFY(ca.open(QIODevice::ReadOnly));
        auto configuration = originalSsl;
        const QSslCertificate certificate(cert.readAll());
        configuration.setLocalCertificate(certificate);
        configuration.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
        server.setSslConfiguration(configuration);
        auto trusted = originalSsl;
        trusted.addCaCertificate(QSslCertificate(ca.readAll()));
        QSslConfiguration::setDefaultConfiguration(trusted);
    }
    server.setProxy(QNetworkProxy::NoProxy);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTcpServer proxy;
    proxy.setProxy(QNetworkProxy::NoProxy);
    QVERIFY(proxy.listen(QHostAddress::LocalHost));
    auto seen = std::make_shared<QByteArray>();
    QObject::connect(
        &proxy, &QTcpServer::newConnection, &proxy, [&proxy, seen, port = server.serverPort()] {
            auto *client = proxy.nextPendingConnection();
            auto *upstream = new QTcpSocket(client);
            upstream->setProxy(QNetworkProxy::NoProxy);
            auto tunneled = std::make_shared<bool>(false);
            QObject::connect(client, &QTcpSocket::readyRead, client,
                             [client, upstream, seen, tunneled, port] {
                                 if (*tunneled) {
                                     upstream->write(client->readAll());
                                     return;
                                 }
                                 seen->append(client->readAll());
                                 if (!seen->contains("\r\n\r\n"))
                                     return;
                                 *tunneled = true;
                                 upstream->connectToHost(QHostAddress::LocalHost, port);
                             });
            QObject::connect(upstream, &QTcpSocket::connected, client, [client] {
                client->write("HTTP/1.1 200 Connection Established\r\n\r\n");
            });
            QObject::connect(upstream, &QTcpSocket::readyRead, client,
                             [client, upstream] { client->write(upstream->readAll()); });
            QObject::connect(client, &QTcpSocket::disconnected, upstream, &QTcpSocket::abort);
        });
    QSignalSpy applied(host.network.get(), &NetworkPreferences::applied);
    host.network->apply("fixed_servers", {}, QString("http://127.0.0.1:%1").arg(proxy.serverPort()),
                        {}, {}, false, {}, 0, 60);
    QTRY_COMPARE(applied.size(), 1);
    host.network->installApplicationTransport();
    // An unresolvable remote name prevents Qt's native loopback proxy bypass from hiding this path.
    auto *open = scope->net()->webSocket()->connect(secure ? "wss://plugin-proxy.invalid:8080/feed"
                                                           : "ws://plugin-proxy.invalid:8080/feed");
    auto openRoot = host.engine.newQObject(open);
    QTRY_VERIFY(!seen->isEmpty());
    QTRY_VERIFY(open->finished());
    QVERIFY2(open->state() == EchoPluginOperation::Succeeded, qPrintable(open->errorMessage()));
    auto *socket = clientConnection(open);
    QVERIFY(socket);
    auto socketRoot = host.engine.newQObject(socket);
    QVERIFY(seen->startsWith("CONNECT plugin-proxy.invalid:8080 "));
    auto *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QSignalSpy messages(socket, &EchoPluginWebSocketConnection::message);
    peer->sendTextMessage("via host proxy");
    QTRY_COMPARE(messages.size(), 1);
    QCOMPARE(messages[0][0].toString(), "via host proxy");
    socket->close();
    QTRY_COMPARE(socket->readyState(), 3);
}
