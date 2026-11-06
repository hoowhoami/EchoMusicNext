#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "LogService.h"

void PluginRuntimeTest::pluginDiagnosticsReachUnifiedLog()
{
    QTemporaryDir logs;
    QString failedId, quietId, detailedId, canceledId;
    {
        LogService logging(logs.path(), 2, false);
        QVERIFY(logging.active());
        Host host;
        host.runtime.importDirectory(
            package(host.temporary.path(),
                    "export function activate(ctx) { console.info('plugin-console-marker'); "
                    "ctx.registerCommand('echo', args => args); }"));
        QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
        auto *scope = host.runtime.context(host.installation());
        auto *quiet = scope->operation();
        quietId = quiet->id();
        quiet->succeed("private-success-value");
        auto *failed = scope->player()->setVolume(-1);
        failedId = failed->id();
        QCOMPARE(failed->state(), EchoPluginOperation::Failed);
        // A failed operation has exactly one terminal log, even if completed again.
        failed->fail("Again", "must-not-appear");
        NetworkPreferences preferences(host.settings, host.api);
        preferences.setGithubProxyUrl("https://mirror.invalid/");
        PluginManager manager(host.runtime, host.settings);
        manager.setNetworkPreferences(&preferences);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, peer, [peer] {
                peer->readAll();
                peer->write(
                    "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                peer->disconnectFromHost();
            });
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        });
        manager.m_resolveUrl = [&](const QUrl &) {
            return QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) +
                        "/fixture?token=private-network-token");
        };
        bool networkDone = false;
        manager.get(QUrl("https://raw.githubusercontent.com/test/repo/main/index.json"), 1024,
                    [&](QByteArray, QString error, QVariantMap) {
                        QVERIFY(!error.isEmpty());
                        networkDone = true;
                    });
        QTRY_VERIFY(networkDone);
        QVERIFY(logging.enableTemporaryDiagnostics());
        auto *detailed = scope->executeCommand("echo", {{"token", "private-command-argument"}});
        detailedId = detailed->id();
        QTRY_COMPARE(detailed->state(), EchoPluginOperation::Succeeded);
        auto *canceled = scope->operation();
        canceledId = canceled->id();
        canceled->cancel();
        scope->rejected("DiagnosticTest", "token=private-error-token");
        scope->rejected("BoundedTest", "bounded-marker " + QString(2000, QChar(0x4e2d)));
        QVERIFY(host.runtime.setEnabled(host.installation(), false));
        QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    }
    QString output;
    for (const auto &name : QDir(logs.path()).entryList({"*.log"}, QDir::Files)) {
        QFile file(logs.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        output += QString::fromUtf8(file.readAll());
    }
    QVERIFY(output.contains("echo.plugins.runtime"));
    QVERIFY(output.contains("plugin-console-marker"));
    QVERIFY(output.contains("state=Active"));
    QVERIFY(output.contains("state=Disabled"));
    QVERIFY(output.contains("shutdown drained"));
    QVERIFY(output.contains("plugin=test.runtime installation="));
    QCOMPARE(output.count("operation=" + failedId), 1);
    QVERIFY(output.contains("EchoPluginPlayer::setVolume"));
    QVERIFY(!output.contains(quietId));
    QVERIFY(output.contains("dispatch operation=" + detailedId));
    QVERIFY(output.contains("finished operation=" + detailedId));
    QVERIFY(output.contains("finished operation=" + canceledId));
    QVERIFY(output.contains("state=Canceled"));
    QVERIFY(output.contains("DiagnosticTest"));
    QVERIFY(output.contains("bounded-marker"));
    QVERIFY(output.contains("[truncated]"));
    const QRegularExpression networkAttempt(
        "request finished request=([^ ]+)[^\\n]*status=503[^\\n]*attempt=([12]) fallback=([01])");
    auto attempts = networkAttempt.globalMatch(output);
    QVERIFY(attempts.hasNext());
    const auto mirror = attempts.next();
    QCOMPARE(mirror.captured(2), QString("1"));
    QCOMPARE(mirror.captured(3), QString("1"));
    QVERIFY(attempts.hasNext());
    const auto origin = attempts.next();
    QCOMPARE(origin.captured(1), mirror.captured(1));
    QCOMPARE(origin.captured(2), QString("2"));
    QCOMPARE(origin.captured(3), QString("0"));
    QVERIFY(!attempts.hasNext());
    QVERIFY(!output.contains("private-network-token"));
    QVERIFY(!output.contains("private-error-token"));
    QVERIFY(!output.contains("private-command-argument"));
    QVERIFY(!output.contains("private-success-value"));
    QVERIFY(!output.contains("must-not-appear"));
}

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
    // Float processing/SDR tone mapping is not a native local HDR presenter.
    package(directory.path(), "throw new Error('must not execute');", {"graphics.hdr"});
    QVERIFY(PluginPackage::importDirectory(source, directory.path() + "/packages", "identity")
                .incompatible);
    package(directory.path(), "throw new Error('must not execute');", {"test.unsupported"});
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
