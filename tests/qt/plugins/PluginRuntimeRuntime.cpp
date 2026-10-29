#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

void PluginRuntimeTest::initTestCase()
{
    configureUiTypography();
}

void PluginRuntimeTest::packageIntegrityAndCompatibility()
{
    QTemporaryDir directory;
    const auto source = package(directory.path(), "export function activate(ctx) {}\n");
    const auto first =
        PluginPackage::importDirectory(source, directory.path() + "/packages", "identity");
    QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
    QVERIFY(first.root != source);
    QCOMPARE(PluginPackage::importDirectory(source, directory.path() + "/packages", "identity")
                 .contentHash,
             first.contentHash);
    write(source + "/entry.mjs", "export function activate(ctx) { return 1; }\n");
    const auto second =
        PluginPackage::importDirectory(source, directory.path() + "/packages", "identity");
    QVERIFY(second.root != first.root);
    QVERIFY(first.resolve("../entry.mjs").isEmpty());
    write(source + "/qmldir", "module Unsafe\nplugin malicious\n");
    QVERIFY(!PluginPackage::importDirectory(source, directory.path() + "/packages", "identity")
                 .error.isEmpty());
    QFile::remove(source + "/qmldir");
    package(directory.path(), "throw new Error('must not execute');", {"graphics.compute"});
    QVERIFY(PluginPackage::importDirectory(source, directory.path() + "/packages", "identity")
                .incompatible);
}

void PluginRuntimeTest::reloadUsesNewCodeUrlAndIdentity()
{
    Host host;
    const auto source = package(host.temporary.path(),
                                "export function activate(ctx) { ctx.player.setVolume(20); }\n");
    host.runtime.importDirectory(source);
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    QTRY_COMPARE(host.playback.volume(), 20);
    QTRY_VERIFY(!host.runtime.loading());
    const auto identity = host.installation();
    const auto firstRoot = host.runtime.context(identity)->package().root;
    write(source + "/entry.mjs", "export function activate(ctx) { ctx.player.setVolume(70); }\n");
    QVERIFY(host.runtime.reload(identity));
    QTRY_COMPARE(host.runtime.state(identity), PluginRuntime::Active);
    QTRY_COMPARE(host.playback.volume(), 70);
    QCOMPARE(host.installation(), identity);
    QVERIFY(host.runtime.context(identity)->package().root != firstRoot);
}

void PluginRuntimeTest::actualHttpAndCancellation()
{
    Host host;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            socket->readAll();
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nreal");
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    host.runtime.importDirectory(package(host.temporary.path(), "export function activate(ctx) {}",
                                         {"net.request"}, {"unrestrictedNetwork"}));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    auto *io = host.runtime.context(host.installation())->net();
    const auto url = QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/test");
    auto *request = io->request({{"url", url}, {"responseType", "arrayBuffer"}});
    QTRY_COMPARE(request->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(request->result().toMap().value("status").toInt(), 200);
    QCOMPARE(request->result().toMap().value("data").toByteArray(), QByteArray("real"));
    auto *canceled = io->request({{"url", url}, {"responseType", "arrayBuffer"}});
    QSignalSpy completed(canceled, &EchoPluginOperation::completed);
    canceled->cancel();
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(canceled->state(), EchoPluginOperation::Canceled);
}
