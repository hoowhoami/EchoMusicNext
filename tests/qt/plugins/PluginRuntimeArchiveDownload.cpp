#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "PluginArchiveDownload_p.h"
#include <QBuffer>

namespace
{
QString checksum(QByteArrayView bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
class ArchiveServer final
{
public:
    QTcpServer server;
    int requests = 0;
    std::function<void(QTcpSocket *, const QByteArray &)> respond;
    ArchiveServer()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    if (socket->property("answered").toBool())
                        return;
                    const auto bytes =
                        socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    if (!bytes.contains("\r\n\r\n"))
                        return;
                    socket->setProperty("answered", true);
                    ++requests;
                    respond(socket, bytes);
                });
            }
        });
    }
    QUrl url(const QString &path = "/archive.echomod") const
    {
        return QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + path);
    }
    static void header(QTcpSocket *socket, qint64 length)
    {
        socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(length) + "\r\n\r\n");
    }
};
class GeneratedArchive final : public QIODevice
{
public:
    explicit GeneratedArchive(qint64 size) : m_size(size)
    {
        open(QIODevice::ReadOnly);
    }
    qint64 maximumRead = 0;
    bool isSequential() const override
    {
        return true;
    }
    qint64 bytesAvailable() const override
    {
        return m_size - m_offset + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 size) override
    {
        maximumRead = qMax(maximumRead, size);
        const auto count = qMin(size, m_size - m_offset);
        for (qint64 i = 0; i < count; ++i)
            data[i] = char((m_offset + i) % 251);
        m_offset += count;
        return count;
    }
    qint64 writeData(const char *, qint64) override
    {
        return -1;
    }

private:
    qint64 m_size, m_offset = 0;
};
} // namespace

void PluginRuntimeTest::archiveDownloadBoundedReceiverAndStorageFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("package.echomod");
    constexpr qint64 total = 32 * 1024 * 1024;
    GeneratedArchive expected(total);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QVERIFY(hash.addData(&expected));
    PluginArchiveDownload download(path, QString::fromLatin1(hash.result().toHex()));
    QVERIFY(download.begin());
    GeneratedArchive source(total);
    QVERIFY(download.receive(source, total));
    QCOMPARE(download.received(), total);
    QVERIFY(source.maximumRead <= PluginArchiveDownload::ChunkBytes);
    QVERIFY(!QFileInfo::exists(path)); // Unverified output is never published.
    QVERIFY(download.finish());
    QFile result(path);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.size(), total);
    QCryptographicHash actual(QCryptographicHash::Sha256);
    QVERIFY(actual.addData(&result));
    QCOMPARE(actual.result(), hash.result());
    result.close();
    QVERIFY(QFile::remove(path));

    const QByteArray body("a short archive body");
    PluginArchiveDownload limited(path, checksum(body));
    QVERIFY(limited.begin());
    QBuffer input;
    input.setData(body);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QVERIFY(!limited.receive(input, body.size() - 1));
    QVERIFY(limited.limited());
    limited.cancel();
    QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    QVERIFY(limited.begin());
    QVERIFY(input.seek(0));
    QVERIFY(limited.receive(input, body.size()));
    QVERIFY(limited.finish()); // Exact ceiling is accepted after a failed attempt.
    QVERIFY(QFile::remove(path));

    PluginArchiveDownload wrongHash(path, QString(64, '0'));
    QVERIFY(wrongHash.begin());
    QVERIFY(input.seek(0));
    QVERIFY(wrongHash.receive(input, body.size()));
    QVERIFY(!wrongHash.finish());
    QVERIFY(!wrongHash.storageFailed());
    QVERIFY(!wrongHash.error().isEmpty());
    QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());

    PluginArchiveDownload unavailable(directory.filePath("missing/package.echomod"),
                                      checksum(body));
    QVERIFY(!unavailable.begin());
    QVERIFY(unavailable.storageFailed());
    PluginArchiveDownload writeFailure(path, checksum(body));
    QVERIFY(writeFailure.begin());
    // Qt's failed-write state exercises the real error path without filling the
    // developer's disk or relying on platform-specific filesystem fixtures.
    writeFailure.m_file->cancelWriting();
    QVERIFY(input.seek(0));
    QVERIFY(!writeFailure.receive(input, body.size()));
    QVERIFY(writeFailure.storageFailed());
    QVERIFY(!writeFailure.error().isEmpty());
    writeFailure.cancel();
    QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    PluginArchiveDownload commitFailure(path, checksum(body));
    QVERIFY(commitFailure.begin());
    QVERIFY(input.seek(0));
    QVERIFY(commitFailure.receive(input, body.size()));
    QVERIFY(QDir().mkpath(path)); // Atomic publication cannot replace a directory.
    QVERIFY(!commitFailure.finish());
    QVERIFY(commitFailure.storageFailed());
    QVERIFY(QDir(path).removeRecursively());
    QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

void PluginRuntimeTest::marketplaceArchiveStreamsToFile_data()
{
    QTest::addColumn<bool>("chunked");
    QTest::addColumn<bool>("redirect");
    QTest::newRow("content-length") << false << false;
    QTest::newRow("chunked") << true << false;
    QTest::newRow("redirect-body-excluded") << false << true;
}
void PluginRuntimeTest::marketplaceArchiveStreamsToFile()
{
    QFETCH(bool, chunked);
    QFETCH(bool, redirect);
    Host host;
    PluginManager manager(host.runtime, host.settings);
    QTemporaryDir directory;
    const auto path = directory.filePath("package.echomod");
    const QByteArray first(96 * 1024, 'a'), last(113 * 1024, 'b');
    const auto body = first + last;
    auto download = QSharedPointer<PluginArchiveDownload>::create(path, checksum(body));
    ArchiveServer wire;
    QVERIFY(wire.server.listen(QHostAddress::LocalHost));
    QPointer<QTcpSocket> pending;
    wire.respond = [&](QTcpSocket *socket, const QByteArray &request) {
        if (request.startsWith("GET /redirect ")) {
            socket->write("HTTP/1.1 302 Found\r\nLocation: /archive.echomod\r\n"
                          "Content-Length: 7\r\nConnection: close\r\n\r\nignored");
            socket->disconnectFromHost();
            return;
        }
        pending = socket;
        if (chunked)
            socket->write(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n" +
                QByteArray::number(first.size(), 16) + "\r\n" + first + "\r\n");
        else {
            ArchiveServer::header(socket, body.size());
            socket->write(first);
        }
    };
    int completed = 0;
    QString error;
    manager.get(
        wire.url(redirect ? "/redirect" : "/archive.echomod"), body.size(),
        [&](QByteArray bytes, QString message, QVariantMap) {
            QVERIFY(bytes.isEmpty());
            ++completed;
            error = message;
        },
        {}, 30000, {}, true, download);
    QTRY_COMPARE(download->received(), first.size());
    QCOMPARE(completed, 0);
    QVERIFY(pending);
    QVERIFY(!QFileInfo::exists(path));
    QCOMPARE(manager.m_replies.size(), 1);
    QCOMPARE((*manager.m_replies.begin())->readBufferSize(), PluginArchiveDownload::ChunkBytes);
    if (chunked)
        pending->write(QByteArray::number(last.size(), 16) + "\r\n" + last + "\r\n0\r\n\r\n");
    else
        pending->write(last);
    pending->disconnectFromHost();
    QTRY_COMPARE(completed, 1);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QFile archive(path);
    QVERIFY(archive.open(QIODevice::ReadOnly));
    QCOMPARE(archive.readAll(), body);
    QCOMPARE(download->received(), body.size());
    QCOMPARE(wire.requests, redirect ? 2 : 1);
    QVERIFY(manager.m_replies.isEmpty());
}

void PluginRuntimeTest::marketplaceArchiveFailuresClean_data()
{
    QTest::addColumn<QString>("mode");
    for (const auto *name : {"hash", "truncated", "timeout", "http", "declared-limit",
                             "actual-limit", "cancel", "open", "write", "commit"})
        QTest::newRow(name) << QString::fromLatin1(name);
}
void PluginRuntimeTest::marketplaceArchiveFailuresClean()
{
    QFETCH(QString, mode);
    Host host;
    NetworkPreferences preferences(host.settings, host.api);
    preferences.setGithubProxyUrl("https://mirror.invalid/");
    PluginManager manager(host.runtime, host.settings);
    manager.setNetworkPreferences(&preferences);
    QTemporaryDir directory;
    const auto path =
        directory.filePath(mode == "open" ? "missing/package.echomod" : "package.echomod");
    const QByteArray body(16 * 1024, 'z');
    auto download = QSharedPointer<PluginArchiveDownload>::create(
        path, mode == "hash" ? QString(64, '0') : checksum(body));
    ArchiveServer wire;
    QVERIFY(wire.server.listen(QHostAddress::LocalHost));
    const bool limited = mode.endsWith("limit");
    const auto limit = limited ? body.size() - 1 : body.size();
    manager.m_resolveUrl = [&](const QUrl &) { return wire.url(); };
    wire.respond = [&](QTcpSocket *socket, const QByteArray &) {
        if (mode == "http")
            socket->write(
                "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        else if (mode == "actual-limit")
            socket->write(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n" +
                QByteArray::number(body.size(), 16) + "\r\n" + body + "\r\n0\r\n\r\n");
        else {
            ArchiveServer::header(socket, body.size());
            socket->write(mode == "truncated" || mode == "cancel" || mode == "timeout"
                              ? body.left(body.size() / 2)
                              : body);
        }
        if (mode != "cancel" && mode != "timeout")
            socket->disconnectFromHost();
    };
    int completed = 0;
    QString error;
    QVariantMap failure;
    manager.get(
        QUrl("https://raw.githubusercontent.com/author/repo/HEAD/package.echomod"), limit,
        [&](QByteArray bytes, QString message, QVariantMap detail) {
            QVERIFY(bytes.isEmpty());
            ++completed;
            error = message;
            failure = detail;
        },
        {}, mode == "timeout" ? 100 : 30000, {}, true, download);
    if (mode == "write")
        download->m_file->cancelWriting();
    if (mode == "commit")
        QVERIFY(QDir().mkpath(path));
    if (mode == "cancel") {
        QTRY_COMPARE(download->received(), body.size() / 2);
        QVERIFY(!manager.m_replies.isEmpty());
        (*manager.m_replies.begin())->abort();
    }
    QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 5000);
    QVERIFY(!error.isEmpty());
    QVERIFY(manager.m_replies.isEmpty());
    if (mode == "commit") {
        QVERIFY(QDir(path).exists());
        QVERIFY(QDir(path).removeRecursively());
    }
    QVERIFY(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    const bool local = mode == "open" || mode == "write" || mode == "commit";
    QCOMPARE(wire.requests, mode == "open" ? 0 : local || mode == "cancel" ? 1 : 2);
    if (limited)
        QCOMPARE(failure.value("category").toString(), QString("size-limit"));
    if (mode == "cancel")
        QCOMPARE(failure.value("category").toString(), QString("cancelled"));
    if (local)
        QCOMPARE(failure.value("category").toString(), QString("storage"));
}

void PluginRuntimeTest::marketplaceArchiveCancelRetiresStaging_data()
{
    QTest::addColumn<QString>("mode");
    for (const auto *name : {"cancel", "shutdown", "destroy"})
        QTest::newRow(name) << QString::fromLatin1(name);
}
void PluginRuntimeTest::marketplaceArchiveCancelRetiresStaging()
{
    QFETCH(QString, mode);
    Host host;
    NetworkPreferences preferences(host.settings, host.api);
    preferences.setGithubProxyUrl("https://mirror.invalid/");
    auto manager = std::make_unique<PluginManager>(host.runtime, host.settings);
    manager->setNetworkPreferences(&preferences);
    ArchiveServer wire;
    QVERIFY(wire.server.listen(QHostAddress::LocalHost));
    const QByteArray body(32 * 1024, 'a');
    int archiveRequests = 0;
    wire.respond = [&](QTcpSocket *socket, const QByteArray &request) {
        if (request.contains(" /stats ")) {
            const QByteArray json(R"({"ok":true,"plugins":[]})");
            ArchiveServer::header(socket, json.size());
            socket->write(json);
            socket->disconnectFromHost();
            return;
        }
        ++archiveRequests;
        ArchiveServer::header(socket, body.size());
        socket->write(body.left(body.size() / 2));
    };
    manager->m_resolveUrl = [&](const QUrl &url) {
        return wire.url(url.path().endsWith(".echomod") ? "/archive.echomod" : "/stats");
    };
    manager->m_entries = {QVariantMap{
        {"id", "org.test.streaming"},
        {"version", "1.0.0"},
        {"key", "streaming"},
        {"compatible", true},
        {"sourceId", manager->sourceIds().first()},
        {"artifactUrl", "https://raw.githubusercontent.com/author/repo/HEAD/package.echomod"},
        {"sha256", checksum(body)}}};
    int notices = 0;
    connect(manager.get(), &PluginManager::notice, &wire.server,
            [&](const QString &, bool, bool) { ++notices; });
    QVERIFY(manager->install("streaming"));
    QTRY_COMPARE(manager->m_archiveDownload->received(), body.size() / 2);
    QVERIFY(manager->busy());
    const auto staging = manager->m_staging->path();
    QVERIFY(QDir(staging).exists());
    if (mode == "cancel") {
        (*manager->m_replies.begin())->abort();
        QTRY_VERIFY(!manager->busy());
        QVERIFY(!manager->m_staging);
        QCOMPARE(notices, 1);
    } else if (mode == "shutdown") {
        manager->shutdown();
        QVERIFY(manager->m_replies.isEmpty());
        QVERIFY(!manager->m_staging);
        QCOMPARE(notices, 0);
    } else {
        manager.reset();
        QCOMPARE(notices, 0);
    }
    QTRY_VERIFY(!QDir(staging).exists());
    QCOMPARE(archiveRequests, 1); // Cancellation never falls back to origin.
    QVERIFY(host.runtime.installationForId("org.test.streaming").isEmpty());
    if (mode == "cancel") {
        QVERIFY(manager->install("streaming"));
        QTRY_COMPARE(manager->m_archiveDownload->received(), body.size() / 2);
        QCOMPARE(archiveRequests, 2);
        const auto retryStage = manager->m_staging->path();
        QVERIFY(retryStage != staging);
        manager->shutdown();
        QTRY_VERIFY(!QDir(retryStage).exists());
    }
}

void PluginRuntimeTest::marketplaceArchiveInstallsStreamedPackage()
{
    const auto configured = qEnvironmentVariable("ECHO_PLUGIN_STREAM_ARCHIVE");
    const auto source =
        configured.isEmpty()
            ? QString(QFINDTESTDATA("../fixtures/plugins/marketplace/archive.echomod"))
            : configured;
    QFile archive(source);
    QVERIFY(archive.open(QIODevice::ReadOnly));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QVERIFY(hash.addData(&archive));
    QVERIFY(archive.seek(0));
    QTemporaryDir inspected;
    QVERIFY(PluginPackage::extractArchive(source, inspected.path()).isEmpty());
    const auto package = PluginPackage::inspectDirectory(inspected.path());
    QVERIFY2(package.error.isEmpty(), qPrintable(package.error));
    Host host;
    PluginManager manager(host.runtime, host.settings);
    ArchiveServer wire;
    QVERIFY(wire.server.listen(QHostAddress::LocalHost));
    QPointer<QTcpSocket> pending;
    const auto firstSize = qMin(PluginArchiveDownload::ChunkBytes, archive.size() / 2);
    wire.respond = [&](QTcpSocket *socket, const QByteArray &request) {
        if (request.contains(" /stats ")) {
            const QByteArray json(R"({"ok":true,"plugins":[]})");
            ArchiveServer::header(socket, json.size());
            socket->write(json);
            socket->disconnectFromHost();
            return;
        }
        pending = socket;
        ArchiveServer::header(socket, archive.size());
        socket->write(archive.read(firstSize));
    };
    manager.m_resolveUrl = [&](const QUrl &url) {
        return url.host() == "127.0.0.1" ? url : wire.url("/stats");
    };
    manager.m_entries = {QVariantMap{{"id", package.id},
                                     {"version", package.manifest.value("version")},
                                     {"name", package.manifest.value("name")},
                                     {"manifest", package.manifest},
                                     {"key", "streaming"},
                                     {"compatible", true},
                                     {"sourceId", manager.sourceIds().first()},
                                     {"artifactUrl", wire.url().toString()},
                                     {"sha256", QString::fromLatin1(hash.result().toHex())}}};
    QVERIFY(manager.install("streaming"));
    QTRY_COMPARE(manager.m_archiveDownload->received(), firstSize);
    const auto staging = manager.m_staging->path();
    QVERIFY(manager.busy());
    QVERIFY(host.runtime.installationForId(package.id).isEmpty());
    QTimer producer;
    connect(&producer, &QTimer::timeout, &producer, [&] {
        if (!pending) {
            producer.stop();
            return;
        }
        if (pending->bytesToWrite() < PluginArchiveDownload::ChunkBytes) {
            pending->write(archive.read(PluginArchiveDownload::ChunkBytes));
            if (archive.atEnd()) {
                producer.stop();
                pending->disconnectFromHost();
            }
        }
    });
    producer.start(1);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 15000);
    const auto installation = host.runtime.installationForId(package.id);
    QVERIFY(!installation.isEmpty());
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QCOMPARE(host.runtime.record(installation).value("version"), package.manifest.value("version"));
    QVERIFY(!manager.m_archiveDownload);
    QTRY_VERIFY(!QDir(staging).exists());
    manager.shutdown();
}
