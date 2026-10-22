#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginFiles.h"
#include "EchoPluginDownloads.h"
#include <QApplication>
#include <QFileDialog>
#include <QNetworkReply>
using namespace EchoMusic::PluginTest;
namespace
{
struct Operations
{
    QQmlEngine &engine;
    QList<QJSValue> roots;
    EchoPluginOperation *keep(EchoPluginOperation *op)
    {
        roots.append(engine.newQObject(op));
        return op;
    }
    QVariantMap success(EchoPluginOperation *op)
    {
        keep(op);
        if (!QTest::qWaitFor([op] { return op->finished(); }, 10000) ||
            op->state() != EchoPluginOperation::Succeeded)
            qFatal("Operation failed: %s %s", qPrintable(op->errorCode()),
                   qPrintable(op->errorMessage()));
        return op->result().toMap();
    }
};
EchoPluginContext *installFiles(Host &host)
{
    host.runtime.importDirectory(package(host.temporary.path(), "export function activate(c) {}",
                                         {"files.references", "files.io", "files.dialogs",
                                          "files.media", "net.downloads", "net.request"},
                                         {"localFiles", "downloads", "unrestrictedNetwork"}));
    if (!QTest::qWaitFor(
            [&] { return host.runtime.state(host.installation()) == PluginRuntime::Active; }))
        return nullptr;
    return host.runtime.context(host.installation());
}
QFileDialog *picker()
{
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "pluginNativeFileDialog")
            return qobject_cast<QFileDialog *>(widget);
    return nullptr;
}
QVariantMap chooseDirectory(Operations &ops, EchoPluginFiles *files, const QString &path,
                            const QString &access = "read-write", bool persist = true)
{
    auto *op = ops.keep(files->requestDirectory({{"purpose", "Test file operation"},
                                                 {"access", access},
                                                 {"persist", persist},
                                                 {"defaultPath", path}}));
    if (!QTest::qWaitFor([] { return picker() != nullptr; }))
        qFatal("No native picker");
    picker()->selectFile(path);
    QMetaObject::invokeMethod(picker(), "accept");
    const auto result = ops.success(op);
    if (!QTest::qWaitFor([] { return !picker(); }))
        qFatal("Picker was not destroyed");
    return result.value("directory").toMap();
}
QVariantMap child(QVariantMap ref, const QString &path)
{
    ref["path"] = path;
    return ref;
}
struct DownloadServer : QTcpServer
{
    QByteArray payload = QByteArray(512 * 1024, 'x');
    QList<QByteArray> requests;
    bool corrupt = false;
    QList<QPointer<QTcpSocket>> sockets;
    DownloadServer()
    {
        if (!listen(QHostAddress::LocalHost))
            qFatal("Cannot listen");
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            sockets.append(socket);
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n") || socket->property("sent").toBool())
                    return;
                socket->setProperty("sent", true);
                requests.append(*request);
                qint64 offset = 0;
                const auto match = QRegularExpression("(?i)Range: bytes=(\\d+)-")
                                       .match(QString::fromLatin1(*request));
                const bool range = match.hasMatch();
                if (range)
                    offset = match.captured(1).toLongLong();
                QByteArray body = payload.mid(offset);
                if (corrupt && !body.isEmpty())
                    body[0] = 'z';
                QByteArray headers =
                    range ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                headers +=
                    "ETag: \"stable\"\r\nContent-Length: " + QByteArray::number(body.size()) +
                    "\r\nConnection: close\r\n";
                if (range)
                    headers += "Content-Range: bytes " + QByteArray::number(offset) + '-' +
                               QByteArray::number(payload.size() - 1) + '/' +
                               QByteArray::number(payload.size()) + "\r\n";
                socket->write(headers + "\r\n");
                auto *timer = new QTimer(socket);
                auto sent = std::make_shared<qsizetype>(0);
                connect(timer, &QTimer::timeout, socket, [socket, timer, body, sent] {
                    if (socket->state() != QAbstractSocket::ConnectedState) {
                        timer->stop();
                        return;
                    }
                    const auto bytes = body.mid(*sent, 8192);
                    socket->write(bytes);
                    *sent += bytes.size();
                    if (*sent >= body.size()) {
                        timer->stop();
                        socket->disconnectFromHost();
                    }
                });
                timer->start(5);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
    QString url() const
    {
        return QString("http://127.0.0.1:%1/file").arg(serverPort());
    }
};
} // namespace
void PluginRuntimeTest::fileSelectionReadWriteAndIsolation()
{
    Host host;
    auto *scope = installFiles(host);
    QVERIFY(scope);
    Operations ops{host.engine};
    const auto path = host.temporary.path() + "/chosen";
    QVERIFY(QDir().mkpath(path));
    const auto grant = chooseDirectory(ops, scope->files(), path);
    const auto root = grant.value("file").toMap();
    QVERIFY(!root.value("grantId").toString().isEmpty());
    auto *files = scope->files();
    const auto file = child(root, "hello.txt");
    ops.success(files->writeText(file, QString::fromUtf8("你好 world")));
    QCOMPARE(ops.success(files->readText(file)).value("text").toString(),
             QString::fromUtf8("你好 world"));
    QCOMPARE(ops.success(files->readBytes(file, {{"offset", 7}, {"maxBytes", 3}}))
                 .value("bytes")
                 .toByteArray(),
             QByteArray("wor"));
    auto *exists = ops.keep(files->writeText(file, "bad"));
    QTRY_VERIFY(exists->finished());
    QCOMPARE(exists->errorCode(), "AlreadyExists");
    const auto renamed = ops.success(files->writeText(file, "renamed", {{"conflict", "rename"}}))
                             .value("file")
                             .toMap();
    QVERIFY(renamed != file);
    ops.success(files->copyFile(file, child(root, "copy.txt")));
    ops.success(files->writeText(file, "replace", {{"conflict", "replace"}}));
    QCOMPARE(ops.success(files->readText(file)).value("text").toString(), "replace");
    ops.success(files->mkdir(child(root, "nested/deep"), {{"recursive", true}}));
    QVERIFY(ops.success(files->listFiles(root, {{"depth", 3}})).value("entries").toList().size() >=
            5);
    auto *parent = ops.keep(files->readText(child(root, "../settings.ini")));
    QTRY_VERIFY(parent->finished());
    QCOMPARE(parent->errorCode(), "InvalidReference");
    write(host.temporary.path() + "/outside.txt", "secret");
    QVERIFY(QFile::link(host.temporary.path() + "/outside.txt", path + "/link.txt"));
    auto *link = ops.keep(files->readText(child(root, "link.txt")));
    QTRY_VERIFY(link->finished());
    QCOMPARE(link->errorCode(), "InvalidReference");
    const auto readonly = chooseDirectory(ops, files, path, "read");
    auto *denied =
        ops.keep(files->writeText(child(readonly.value("file").toMap(), "denied.txt"), "no"));
    QTRY_VERIFY(denied->finished());
    QCOMPARE(denied->errorCode(), "ReadOnly");
    ops.success(files->deleteFile(child(root, "copy.txt")));
    QVERIFY(!QFileInfo::exists(path + "/copy.txt"));
    auto *nonempty = ops.keep(files->deleteFile(child(root, "nested")));
    QTRY_VERIFY(nonempty->finished());
    QCOMPARE(nonempty->state(), EchoPluginOperation::Failed);
    // Accepted completions stay queued on the GUI while the I/O slots fill.
    // Revocation must retain its control slot and invalidate future reads.
    for (int i = 0; i < 32; ++i)
        ops.keep(files->stat(file));
    ops.success(files->revoke(grant.value("id").toString()));
    auto *revoked = ops.keep(files->readText(file));
    QTRY_VERIFY(revoked->finished());
    QCOMPARE(revoked->errorCode(), "GrantRevoked");
    auto *reauthorize =
        ops.keep(files->reauthorize(grant.value("id").toString(),
                                    {{"purpose", "Test reauthorization"}, {"defaultPath", path}}));
    QTRY_VERIFY(picker());
    picker()->selectFile(path);
    QMetaObject::invokeMethod(picker(), "accept");
    const auto replacement = ops.success(reauthorize).value("directory").toMap();
    QCOMPARE(replacement.value("id"), grant.value("id"));
    QVERIFY(replacement.value("revision") != grant.value("revision"));
    QCOMPARE(ops.success(files->readText(child(replacement.value("file").toMap(), "hello.txt")))
                 .value("text")
                 .toString(),
             "replace");
    auto *old = ops.keep(files->stat(file));
    QTRY_VERIFY(old->finished());
    QCOMPARE(old->errorCode(), "GrantRevoked");
    QVERIFY(QFileInfo::exists(path + "/hello.txt"));
}
void PluginRuntimeTest::mediaLeaseRangesAndLifecycle()
{
    Host host;
    auto *scope = installFiles(host);
    QVERIFY(scope);
    Operations ops{host.engine};
    const auto root = ops.success(scope->files()->privateDirectory()).value("file").toMap();
    const auto ref = child(root, "media.bin");
    ops.success(scope->files()->writeBytes(ref, "0123456789"));
    auto *open = ops.keep(scope->files()->openMedia(ref, {{"lifetime", "scope"}}));
    QTRY_COMPARE(open->state(), EchoPluginOperation::Succeeded);
    auto *lease = open->result().value<EchoPluginMediaLease *>();
    QVERIFY(lease && lease->active());
    ops.roots.append(host.engine.newQObject(lease));
    QNetworkAccessManager net;
    QNetworkRequest request(lease->url());
    request.setRawHeader("Range", "bytes=2-5");
    auto *reply = net.get(request);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
    QCOMPARE(reply->readAll(), QByteArray("2345"));
    reply->deleteLater();
    request.setRawHeader("Range", "bytes=-3");
    reply = net.get(request);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->readAll(), QByteArray("789"));
    reply->deleteLater();
    request.setRawHeader("Range", "bytes=50-");
    reply = net.get(request);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 416);
    reply->deleteLater();
    auto *trackOpen = ops.keep(scope->files()->openMedia(ref));
    QTRY_COMPARE(trackOpen->state(), EchoPluginOperation::Succeeded);
    auto *trackLease = trackOpen->result().value<EchoPluginMediaLease *>();
    ops.roots.append(host.engine.newQObject(trackLease));
    PlaybackTrack next;
    next.id = "next";
    next.localPath = host.temporary.path() + "/missing.wav";
    host.playback.playTracks({next}, 0, "Files test", "queue:manual");
    QTRY_VERIFY(!trackLease->active());
    QVERIFY(lease->active());
    const auto oldUrl = lease->url();
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_VERIFY(!lease->active());
    QVERIFY(lease->url().isEmpty());
    reply = net.get(QNetworkRequest(oldUrl));
    QTRY_VERIFY(reply->isFinished());
    QVERIFY(reply->error() != QNetworkReply::NoError);
    reply->deleteLater();
}
void PluginRuntimeTest::downloadsResumeChecksumAndRetirement()
{
    Host host;
    auto *scope = installFiles(host);
    QVERIFY(scope);
    Operations ops{host.engine};
    DownloadServer server;
    const auto root = ops.success(scope->files()->privateDirectory()).value("file").toMap();
    const auto target = child(root, "download.bin");
    const auto digest =
        QCryptographicHash::hash(server.payload, QCryptographicHash::Sha256).toHex();
    QVariantMap options{
        {"url", server.url()},
        {"target", target},
        {"idempotencyKey", "one"},
        {"headers", QVariantMap{{"X-Test", "real"}}},
        {"expectedBytes", double(server.payload.size())},
        {"checksum", QVariantMap{{"algorithm", "sha256"}, {"value", QString::fromLatin1(digest)}}}};
    auto *start = ops.keep(scope->downloads()->start(options));
    QTRY_COMPARE(start->state(), EchoPluginOperation::Succeeded);
    auto *job = start->result().value<EchoPluginDownload *>();
    QVERIFY(job);
    ops.roots.append(host.engine.newQObject(job));
    QTRY_VERIFY(job->snapshot().value("receivedBytes").toDouble() > 0);
    ops.success(job->pause());
    QCOMPARE(job->snapshot().value("state").toString(), "paused");
    auto *again = ops.keep(scope->downloads()->start(options));
    QTRY_COMPARE(again->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(again->result().value<EchoPluginDownload *>()->id(), job->id());
    ops.success(job->resume());
    auto *wait = ops.keep(job->wait());
    QTRY_COMPARE_WITH_TIMEOUT(wait->state(), EchoPluginOperation::Succeeded, 15000);
    QCOMPARE(ops.success(scope->files()->readBytes(target)).value("bytes").toByteArray(),
             server.payload);
    QVERIFY(server.requests.size() >= 2);
    QVERIFY(server.requests.last().contains("Range: bytes=") ||
            server.requests.last().contains("range: bytes="));
    QVERIFY(server.requests.last().toLower().contains("if-range: \"stable\""));
    QVERIFY(server.requests.first().toLower().contains("x-test: real"));
    auto conflict = options;
    conflict["url"] = server.url() + "?different";
    auto *collision = ops.keep(scope->downloads()->start(conflict));
    QTRY_VERIFY(collision->finished());
    QCOMPARE(collision->errorCode(), "IdempotencyConflict");
    server.corrupt = true;
    options["idempotencyKey"] = "checksum";
    options["target"] = child(root, "checksum.bin");
    start = ops.keep(scope->downloads()->start(options));
    QTRY_COMPARE(start->state(), EchoPluginOperation::Succeeded);
    auto *bad = start->result().value<EchoPluginDownload *>();
    ops.roots.append(host.engine.newQObject(bad));
    wait = ops.keep(bad->wait());
    QTRY_COMPARE_WITH_TIMEOUT(wait->state(), EchoPluginOperation::Failed, 15000);
    QCOMPARE(wait->errorCode(), "ChecksumMismatch");
    auto *absent = ops.keep(scope->files()->stat(options.value("target").toMap()));
    QTRY_VERIFY(absent->finished());
    QCOMPARE(absent->errorCode(), "NotFound");
    server.corrupt = false;
    ops.success(bad->retry());
    wait = ops.keep(bad->wait());
    QTRY_COMPARE_WITH_TIMEOUT(wait->state(), EchoPluginOperation::Succeeded, 15000);
    options.remove("checksum");
    options["target"] = child(root, "interrupted.bin");
    options["idempotencyKey"] = "interrupt";
    start = ops.keep(scope->downloads()->start(options));
    QTRY_COMPARE(start->state(), EchoPluginOperation::Succeeded);
    auto *pending = start->result().value<EchoPluginDownload *>();
    ops.roots.append(host.engine.newQObject(pending));
    const auto id = pending->id();
    QTRY_VERIFY(pending->snapshot().value("receivedBytes").toDouble() > 0);
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_VERIFY(!pending->active());
    QCOMPARE(pending->resume()->errorCode(), "ScopeClosed");
    QTRY_VERIFY(!host.runtime.context(host.installation()));
    QVERIFY(host.runtime.setEnabled(host.installation(), true));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    scope = host.runtime.context(host.installation());
    QCOMPARE(ops.success(scope->downloads()->get(id)).value("state").toString(), "interrupted");
    auto *attach = ops.keep(scope->downloads()->attach(id));
    QTRY_COMPARE(attach->state(), EchoPluginOperation::Succeeded);
    auto *resumed = attach->result().value<EchoPluginDownload *>();
    ops.roots.append(host.engine.newQObject(resumed));
    ops.success(resumed->resume());
    wait = ops.keep(resumed->wait());
    QTRY_COMPARE_WITH_TIMEOUT(wait->state(), EchoPluginOperation::Succeeded, 15000);
    ops.success(scope->downloads()->remove(id));
    QVERIFY(!resumed->active());
}
void PluginRuntimeTest::unifiedNetworkRequestOptionsAndCancellation()
{
    Host host;
    auto *scope = installFiles(host);
    QVERIFY(scope);
    Operations ops{host.engine};
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QByteArray> wire;
    QPointer<QTcpSocket> pending;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *socket = server.nextPendingConnection();
        auto request = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
            request->append(socket->readAll());
            const auto end = request->indexOf("\r\n\r\n");
            if (end < 0 || socket->property("sent").toBool())
                return;
            const auto length = QRegularExpression("(?i)Content-Length: (\\d+)")
                                    .match(QString::fromLatin1(*request))
                                    .captured(1)
                                    .toInt();
            if (request->size() < end + 4 + length)
                return;
            socket->setProperty("sent", true);
            wire.append(*request);
            const auto route = request->split(' ').value(1);
            if (route == "/pending") {
                pending = socket;
                return;
            }
            QByteArray status = "200 OK", headers, body = "{\"ok\":true}";
            if (route == "/redirect") {
                status = "302 Found";
                headers = "Location: /json\r\n";
                body.clear();
            }
            if (route == "/invalid")
                body = "invalid json";
            if (route == "/large")
                body = QByteArray(10000, 'x');
            if (route == "/status") {
                status = "500 Internal Server Error";
                body = "{\"error\":true}";
            }
            if (route == "/binary")
                body = QByteArray("\0\x01\xff", 3);
            socket->write("HTTP/1.1 " + status + "\r\n" + headers +
                          "X-Value: first\r\nX-Value: second\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
    const auto base = QString("http://127.0.0.1:%1").arg(server.serverPort());
    const auto response = ops.success(scope->net()->request(
        {{"url", base + "/json"},
         {"method", "PATCH"},
         {"headers",
          QVariantMap{{"Host", "custom.test"}, {"Cookie", "custom=yes"}, {"User-Agent", "plugin"}}},
         {"body", QVariantMap{{"hello", "world"}}}}));
    QCOMPARE(response.value("data").toMap().value("ok").toBool(), true);
    QVERIFY(wire.last().startsWith("PATCH /json"));
    QVERIFY(wire.last().contains("Host: custom.test"));
    QVERIFY(wire.last().contains("Cookie: custom=yes"));
    QVERIFY(wire.last().endsWith("{\"hello\":\"world\"}"));
    QCOMPARE(
        ops.success(scope->net()->request({{"url", base + "/status"}})).value("status").toInt(),
        500);
    const auto redirected = ops.success(scope->net()->request({{"url", base + "/redirect"}}));
    QCOMPARE(redirected.value("url").toString(), base + "/json");
    QCOMPARE(ops.success(scope->net()->request({{"url", base + "/redirect"}, {"maxRedirects", 0}}))
                 .value("status")
                 .toInt(),
             302);
    QCOMPARE(ops.success(scope->net()->request(
                             {{"url", base + "/binary"}, {"responseType", "arrayBuffer"}}))
                 .value("data")
                 .toByteArray(),
             QByteArray("\0\x01\xff", 3));
    host.engine.globalObject().setProperty("__networkScope", host.engine.newQObject(scope));
    const auto binaryRequest = host.engine.evaluate(
        "__networkScope.net.request({url:'" + base +
        "/binary',method:'PUT',responseType:'arrayBuffer',body:new Uint8Array([0,1,255]).buffer})");
    QVERIFY2(!binaryRequest.isError(), qPrintable(binaryRequest.toString()));
    auto *jsOperation = qobject_cast<EchoPluginOperation *>(binaryRequest.toQObject());
    QVERIFY(jsOperation);
    ops.keep(jsOperation);
    QTRY_COMPARE(jsOperation->state(), EchoPluginOperation::Succeeded);
    QVERIFY(wire.last().endsWith(QByteArray("\0\x01\xff", 3)));
    host.engine.globalObject().deleteProperty("__networkScope");
    auto *invalid = ops.keep(scope->net()->request({{"url", base + "/invalid"}}));
    QTRY_VERIFY(invalid->finished());
    QCOMPARE(invalid->errorCode(), "DecodeError");
    auto *large =
        ops.keep(scope->net()->request({{"url", base + "/large"}, {"maxResponseBytes", 20}}));
    QTRY_VERIFY(large->finished());
    QCOMPARE(large->errorCode(), "ResponseTooLarge");
    auto *timeout =
        ops.keep(scope->net()->request({{"url", base + "/pending"}, {"timeoutMs", 100}}));
    QTRY_VERIFY(timeout->finished());
    QCOMPARE(timeout->errorCode(), "Timeout");
    QTRY_VERIFY(!pending);
    auto *cancel = ops.keep(scope->net()->request({{"url", base + "/pending"}}));
    QTRY_VERIFY(pending);
    cancel->cancel();
    QCOMPARE(cancel->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(!pending);
    auto *retire = ops.keep(scope->net()->request({{"url", base + "/pending"}}));
    QTRY_VERIFY(pending);
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(retire->state(), EchoPluginOperation::Canceled);
    QTRY_VERIFY(!pending);
    // No legacy network entry points in the actual public Qt metaobject.
    QCOMPARE(EchoPluginIo::staticMetaObject.indexOfMethod("get(QUrl)"), -1);
    QCOMPARE(EchoPluginIo::staticMetaObject.indexOfMethod("post(QUrl,QByteArray,QString)"), -1);
}
void PluginRuntimeTest::fileGrantLedgerPersistenceAndIsolation()
{
    QTemporaryDir temporary;
    const auto directory = temporary.path() + "/selected";
    QVERIFY(QDir().mkpath(directory));
    write(directory + "/text.txt", "persisted");
    PluginFileOwner owner{"installation", "test-plugin", "Test Plugin", "first",
                          std::make_shared<std::atomic_bool>(false)};
    QVariantMap reference;
    QString temporaryId;
    const auto call = [](PluginFileService &service, const PluginFileOwner &who,
                         const QString &method, const QVariantMap &input) {
        QSignalSpy done(&service, &PluginFileService::completed);
        const auto id = service.request(who, method, input);
        if (!id || !QTest::qWaitFor([&] {
                for (const auto &row : done)
                    if (row[0].toULongLong() == id)
                        return true;
                return false;
            }))
            qFatal("Service request did not complete");
        for (const auto &row : done)
            if (row[0].toULongLong() == id)
                return row;
        return QList<QVariant>();
    };
    {
        PluginFileService service(temporary.path() + "/store");
        QTRY_VERIFY(!service.loading());
        auto result = call(service, owner, "grant",
                           {{"paths", QStringList{directory}},
                            {"kind", "directory"},
                            {"access", "read-write"},
                            {"persist", true}});
        QVERIFY(result[1].toBool());
        reference = result[2].toList().first().toMap().value("file").toMap();
        result = call(service, owner, "grant",
                      {{"paths", QStringList{directory + "/text.txt"}},
                       {"kind", "file"},
                       {"persist", false}});
        temporaryId = result[2].toList().first().toMap().value("id").toString();
        QCOMPARE(service.rowCount(), 2);
        service.closeScope(owner.scope);
        QTRY_COMPARE(service.rowCount(), 1);
        service.shutdown();
        QTRY_VERIFY(service.isClosed());
    }
    {
        PluginFileService service(temporary.path() + "/store");
        QTRY_VERIFY(!service.loading());
        QCOMPARE(service.rowCount(), 1);
        QVERIFY(service.grant(temporaryId).isEmpty());
        owner.scope = "second";
        owner.canceled = std::make_shared<std::atomic_bool>(false);
        auto result = call(service, owner, "readText", {{"file", child(reference, "text.txt")}});
        QVERIFY(result[1].toBool());
        QCOMPARE(result[2].toMap().value("text").toString(), "persisted");
        auto stranger = owner;
        stranger.installation = "another";
        result = call(service, stranger, "readText", {{"file", child(reference, "text.txt")}});
        QVERIFY(!result[1].toBool());
        QCOMPARE(result[3].toString(), "AccessDenied");
        result = call(service, stranger, "revoke", {{"id", reference.value("grantId")}});
        QCOMPARE(result[3].toString(), "AccessDenied");
        service.forgetInstallation(owner.installation);
        QTRY_COMPARE(service.rowCount(), 0);
        QVERIFY(QFileInfo::exists(directory + "/text.txt"));
        service.shutdown();
        QTRY_VERIFY(service.isClosed());
    }
}
void PluginRuntimeTest::mediaLeaseFeedsPlaybackAndRevocationStopsIt()
{
    Host host;
    const auto source = package(host.temporary.path(), R"(
export function activate(c) {
 c.player.audioSource.registerResolver('media',{resolve:function(v){return {url:c.ui.state.url};}});
 c.lyrics.registerResolver('empty',{resolve:function(v){return {format:'lrc',content:''};}});
})",
                                {"files.media", "playback.audioSource", "lyrics.resolvers"},
                                {"localFiles", "audioSource", "lyrics"});
    host.runtime.importDirectory(source);
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    auto *scope = host.runtime.context(host.installation());
    Operations ops{host.engine};
    const auto selected = host.temporary.path() + "/music";
    QVERIFY(QDir().mkpath(selected));
    QByteArray wave;
    QDataStream data(&wave, QIODevice::WriteOnly);
    data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF", 4);
    data << quint32(36 + 320000);
    data.writeRawData("WAVEfmt ", 8);
    data << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2)
         << quint16(16);
    data.writeRawData("data", 4);
    data << quint32(320000);
    wave.append(QByteArray(320000, 0));
    write(selected + "/song.wav", wave);
    const auto grant = chooseDirectory(ops, scope->files(), selected, "read");
    const auto file = child(grant.value("file").toMap(), "song.wav");
    const auto metadata = ops.success(scope->files()->metadata(file));
    QCOMPARE(metadata.value("media").toMap().value("durationMs").toLongLong(), 20000);
    auto *opened = ops.keep(scope->files()->openMedia(file, {{"lifetime", "scope"}}));
    QTRY_COMPARE(opened->state(), EchoPluginOperation::Succeeded);
    auto *lease = opened->result().value<EchoPluginMediaLease *>();
    ops.roots.append(host.engine.newQObject(lease));
    scope->ui()->setState({{"url", lease->url().toString()}});
    PlaybackTrack track;
    track.id = "plugin-stream";
    track.catalogMetadata["plugin_source"] = "test.runtime/media";
    host.playback.playTracks({track}, 0, "Media", "queue:manual");
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.playing(), 15000);
    ops.success(scope->files()->revoke(grant.value("id").toString()));
    QTRY_VERIFY(!lease->active());
    QTRY_VERIFY(!host.playback.playing());
}
void PluginRuntimeTest::fileDownloadTrackRevocationAndAccountCancellation()
{
    Host host;
    auto *scope = installFiles(host);
    QVERIFY(scope);
    Operations ops{host.engine};
    DownloadServer server;
    const auto selected = host.temporary.path() + "/downloads";
    QVERIFY(QDir().mkpath(selected));
    const auto grant = chooseDirectory(ops, scope->files(), selected);
    const auto root = grant.value("file").toMap();
    auto start = [&](const QString &name, const QString &lifetime) {
        auto *op = ops.keep(scope->downloads()->start(
            {{"url", server.url()}, {"target", child(root, name)}, {"lifetime", lifetime}}));
        if (!QTest::qWaitFor([op] { return op->finished(); }) ||
            op->state() != EchoPluginOperation::Succeeded)
            qFatal("Download start failed");
        auto *job = op->result().value<EchoPluginDownload *>();
        ops.roots.append(host.engine.newQObject(job));
        return job;
    };
    auto *track = start("track.bin", "track");
    auto *global = start("scope.bin", "scope");
    QTRY_VERIFY(track->snapshot().value("receivedBytes").toDouble() > 0);
    PlaybackTrack next;
    next.id = "next";
    next.localPath = host.temporary.path() + "/absent.wav";
    host.playback.playTracks({next}, 0, "Next", "queue:manual");
    QTRY_COMPARE(track->snapshot().value("state").toString(), "canceled");
    QVERIFY(!QFileInfo::exists(selected + "/track.bin"));
    ops.success(track->retry());
    QTRY_VERIFY(track->snapshot().value("receivedBytes").toDouble() > 0);
    next.id = "third";
    host.playback.playTracks({next}, 0, "Next again", "queue:manual");
    QTRY_COMPARE(track->snapshot().value("state").toString(), "canceled");
    auto *finished = ops.keep(global->wait());
    QTRY_COMPARE_WITH_TIMEOUT(finished->state(), EchoPluginOperation::Succeeded, 10000);
    auto *revoked = start("revoke.bin", "scope");
    QTRY_VERIFY(revoked->snapshot().value("receivedBytes").toDouble() > 0);
    ops.success(scope->files()->revoke(grant.value("id").toString()));
    QTRY_VERIFY((QStringList{"failed", "interrupted", "canceled"}.contains(
        revoked->snapshot().value("state").toString())));
    QVERIFY(!QFileInfo::exists(selected + "/revoke.bin"));
    const auto privateRoot = ops.success(scope->files()->privateDirectory()).value("file").toMap();
    auto *op = ops.keep(scope->downloads()->start(
        {{"url", server.url()}, {"target", child(privateRoot, "account.bin")}}));
    QTRY_COMPARE(op->state(), EchoPluginOperation::Succeeded);
    auto *account = op->result().value<EchoPluginDownload *>();
    ops.roots.append(host.engine.newQObject(account));
    QTRY_VERIFY(account->snapshot().value("receivedBytes").toDouble() > 0);
    // Signal from the real account transport; no profile or credentials are changed.
    QVERIFY(QMetaObject::invokeMethod(&host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_VERIFY(!account->active());
    QCOMPARE(account->cancel()->errorCode(), "ScopeClosed");
    auto *absent = ops.keep(scope->files()->stat(child(privateRoot, "account.bin")));
    QTRY_VERIFY(absent->finished());
    QCOMPARE(absent->errorCode(), "NotFound");
}
void PluginRuntimeTest::fileGrantManagementDialogUsesRealLedger()
{
    Host host;
    auto *scope = installFiles(host);
    QVERIFY(scope);
    Operations ops{host.engine};
    const auto directory = host.temporary.path() + "/selected";
    QVERIFY(QDir().mkpath(directory));
    const auto grant = chooseDirectory(ops, scope->files(), directory);
    PluginManager manager(host.runtime, host.settings);
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &manager, [&](const QList<QQmlError> &errors) {
        for (const auto &e : errors)
            warnings.append(e.toString());
    });
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Plugins
ApplicationWindow {
 id:root
 required property PluginManager manager
 width:900; height:700; visible:true; color:Theme.main
 PluginGrantsDialog { id:grants; manager:root.manager; Component.onCompleted:open() }
})",
                      QUrl("qrc:/grants-test.qml"));
    std::unique_ptr<QObject> ui(
        component.createWithInitialProperties({{"manager", QVariant::fromValue(&manager)}}));
    QVERIFY2(ui, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(ui.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = ui->findChild<QObject *>("pluginGrantsDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto *list = dialog->findChild<QQuickItem *>("pluginFileGrants");
    QTRY_VERIFY(list && list->height() > 0);
    QCOMPARE(list->property("count").toInt(), 1);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QTRY_COMPARE(dialog->property("opacity").toDouble(), 1.0);
    const auto output = qEnvironmentVariable("ECHO_PLUGIN_UI_OUTPUT");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        for (double scale : {1.0, 1.25}) {
            dialog->setProperty("uiScale", scale);
            for (bool dark : {false, true}) {
                host.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                QTest::qWait(400);
                QVERIFY(dialog->property("width").toDouble() * scale <= window->width());
                const auto name = QString("/grants-%1-%2.png")
                                      .arg(dark ? "dark" : "light")
                                      .arg(scale == 1.0 ? "100" : "125");
                QVERIFY(window->grabWindow().save(output + name));
            }
        }
    }
    auto *revoke =
        visual(window->contentItem(), "pluginGrantRevoke-" + grant.value("id").toString());
    QVERIFY(revoke);
    QVERIFY(QMetaObject::invokeMethod(revoke, "clicked"));
    QTRY_VERIFY(
        manager.fileGrants()->grant(grant.value("id").toString()).value("revoked").toBool());
    auto *reauthorize =
        visual(window->contentItem(), "pluginGrantReauthorize-" + grant.value("id").toString());
    QVERIFY(reauthorize);
    QVERIFY(QMetaObject::invokeMethod(reauthorize, "clicked"));
    QTRY_VERIFY(picker());
    picker()->selectFile(directory);
    QMetaObject::invokeMethod(picker(), "accept");
    QTRY_VERIFY(
        !manager.fileGrants()->grant(grant.value("id").toString()).value("revoked").toBool());
    auto *remove =
        visual(window->contentItem(), "pluginGrantRemove-" + grant.value("id").toString());
    QVERIFY(remove);
    QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
    QTRY_COMPARE(manager.fileGrants()->rowCount(), 0);
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    ui.reset();
    manager.shutdown();
}
