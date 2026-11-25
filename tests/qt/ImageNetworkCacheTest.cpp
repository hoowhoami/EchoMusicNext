#include "QmlImageNetworkFactory.h"
#include "PlaybackArtwork.h"
#include <QImage>
#include <QAbstractNetworkCache>
#include <QBuffer>
#include <QDateTime>
#include <QEventLoop>
#include <QFuture>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent>
#include <QtTest>

using namespace EchoMusic;
namespace
{
struct Result
{
    QByteArray bytes;
    bool cached = false;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    qint64 cacheSize = 0;
};
QFuture<Result> fetch(QmlImageNetworkFactory &factory, QUrl url)
{
    return QtConcurrent::run([&factory, url] {
        std::unique_ptr<QNetworkAccessManager> manager(factory.create(nullptr));
        auto *reply = manager->get(QNetworkRequest(url));
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(10000);
        loop.exec();
        return Result{reply->readAll(),
                      reply->attribute(QNetworkRequest::SourceIsFromCacheAttribute).toBool(),
                      reply->error(), manager->cache()->cacheSize()};
    });
}
class Server : public QTcpServer
{
public:
    Server()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n") || socket->property("served").toBool())
                        return;
                    socket->setProperty("served", true);
                    const auto path = request.split(' ').value(1);
                    const auto count = ++requests[path];
                    if (path == "/slow")
                        return;
                    const auto date =
                        QLocale::c()
                            .toString(QDateTime::currentDateTimeUtc().addSecs(
                                          path == "/revalidate" && count == 1 ? -60 : 0),
                                      "ddd, dd MMM yyyy HH:mm:ss 'GMT'")
                            .toLatin1();
                    QByteArray headers = "Date: " + date + "\r\nETag: \"image-v1\"\r\n";
                    QByteArray body = path == "/json"                ? QByteArray("{\"ok\":true}")
                                      : path.startsWith("/pressure") ? QByteArray(8192, 'x')
                                                                     : QByteArray("encoded-image");
                    if (path == "/artwork") {
                        QImage image(64, 64, QImage::Format_RGB32);
                        image.fill(QColor(24, 48, 72));
                        body.clear();
                        QBuffer buffer(&body);
                        buffer.open(QIODevice::WriteOnly);
                        image.save(&buffer, "PNG");
                    }
                    headers += path == "/json" ? "Content-Type: application/json\r\n"
                                               : "Content-Type: image/png\r\n";
                    headers +=
                        path == "/private" ? "Cache-Control: no-store\r\n"
                        : path == "/revalidate" && count == 1
                            ? "Cache-Control: public\r\nExpires: Wed, 01 Jan 2020 00:00:00 GMT\r\n"
                            : "Cache-Control: public, max-age=3600\r\n";
                    if (path == "/revalidate" && count > 1) {
                        conditional = request.toLower().contains("if-none-match: \"image-v1\"");
                        socket->write("HTTP/1.1 304 Not Modified\r\n" + headers +
                                      "Connection: close\r\n\r\n");
                    } else {
                        socket->write("HTTP/1.1 200 OK\r\n" + headers +
                                      "Content-Length: " + QByteArray::number(body.size()) +
                                      "\r\nConnection: close\r\n\r\n" + body);
                    }
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl url(const QString &path) const
    {
        return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }
    QHash<QByteArray, int> requests;
    bool conditional = false;
};
} // namespace
class ImageNetworkCacheTest final : public QObject
{
    Q_OBJECT
private slots:
    void guiClientsShareBackgroundCacheAndCancelWithoutLateResults()
    {
        QTemporaryDir dir;
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QmlImageNetworkFactory factory(dir.path());
        auto warm = fetch(factory, server.url("/shared"));
        QTRY_VERIFY(warm.isFinished());
        std::unique_ptr<QNetworkAccessManager> media(
            factory.createImageClient(nullptr, 2 * 1024 * 1024));
        std::unique_ptr<QNetworkAccessManager> plugin(
            factory.createImageClient(nullptr, 8 * 1024 * 1024));
        for (auto *client : {media.get(), plugin.get()}) {
            auto *reply = client->get(QNetworkRequest(server.url("/shared")));
            QSignalSpy done(reply, &QNetworkReply::finished);
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(reply->error(), QNetworkReply::NoError);
            QCOMPARE(reply->readAll(), "encoded-image");
            QVERIFY(reply->attribute(QNetworkRequest::SourceIsFromCacheAttribute).toBool());
            delete reply;
        }
        QCOMPARE(server.requests["/shared"], 1);
        auto *cold = media->get(QNetworkRequest(server.url("/media-first")));
        QSignalSpy coldDone(cold, &QNetworkReply::finished);
        QTRY_COMPARE(coldDone.size(), 1);
        QCOMPARE(cold->error(), QNetworkReply::NoError);
        delete cold;
        auto readBack = fetch(factory, server.url("/media-first"));
        QTRY_VERIFY(readBack.isFinished());
        QVERIFY(readBack.result().cached);
        QCOMPARE(server.requests["/media-first"], 1);
        auto *cancelled = plugin->get(QNetworkRequest(server.url("/cancelled")));
        QSignalSpy cancelledDone(cancelled, &QNetworkReply::finished);
        cancelled->abort();
        QCOMPARE(cancelledDone.size(), 1);
        QCOMPARE(cancelled->error(), QNetworkReply::OperationCanceledError);
        QTest::qWait(30);
        QCOMPARE(cancelledDone.size(), 1);
        QCOMPARE(server.requests["/cancelled"], 0);
        delete cancelled;
        auto *slow = plugin->get(QNetworkRequest(server.url("/slow")));
        QSignalSpy slowDone(slow, &QNetworkReply::finished);
        QTRY_COMPARE(server.requests["/slow"], 1);
        slow->abort();
        QCOMPARE(slowDone.size(), 1);
        QTest::qWait(30);
        QCOMPARE(slowDone.size(), 1);
        delete slow;
        auto cachedLarge = fetch(factory, server.url("/pressure-large"));
        QTRY_VERIFY(cachedLarge.isFinished());
        std::unique_ptr<QNetworkAccessManager> tiny(factory.createImageClient(nullptr, 1024));
        auto *large = tiny->get(QNetworkRequest(server.url("/pressure-large")));
        QSignalSpy largeDone(large, &QNetworkReply::finished);
        QTRY_COMPARE(largeDone.size(), 1);
        QVERIFY(large->readAll().size() <= 1025);
        QVERIFY(large->error() != QNetworkReply::NoError);
        delete large;
    }
    void mediaArtworkReusesDownloadedBytesAndKeepsProcessing()
    {
        QTemporaryDir dir;
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QmlImageNetworkFactory factory(dir.path());
        auto warm = fetch(factory, server.url("/artwork"));
        QTRY_VERIFY(warm.isFinished());
        PlaybackArtwork artwork(nullptr, &factory);
        QSignalSpy ready(&artwork, &PlaybackArtwork::ready);
        for (quint64 sequence : {42ULL, 43ULL}) {
            artwork.load(server.url("/artwork").toString(), sequence);
            QTRY_COMPARE(ready.size(), int(sequence - 41));
            const auto value = ready.last();
            QCOMPARE(value[0].toULongLong(), sequence);
            const auto png = QImage::fromData(value[1].toByteArray());
            QCOMPARE(png.size(), QSize(256, 256));
            QCOMPARE(png.pixelColor(100, 100), QColor(24, 48, 72));
            QCOMPARE(value[2].value<QColor>(), QColor(24, 48, 72));
        }
        QCOMPARE(server.requests["/artwork"], 1);
        artwork.load(server.url("/slow").toString(), 44);
        QTRY_COMPARE(server.requests["/slow"], 1);
        artwork.cancel();
        QTest::qWait(30);
        QCOMPARE(ready.size(), 2);
    }
    void capacityEvictsEncodedFiles()
    {
        QTemporaryDir dir;
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        constexpr qint64 budget = 64 * 1024;
        QmlImageNetworkFactory factory(dir.path(), budget);
        for (int i = 0; i < 12; ++i) {
            auto future = fetch(factory, server.url(QString("/pressure-%1").arg(i)));
            QTRY_VERIFY(future.isFinished());
            QCOMPARE(future.result().error, QNetworkReply::NoError);
            QVERIFY(future.result().cacheSize <= budget);
        }
        auto last = fetch(factory, server.url("/pressure-11"));
        QTRY_VERIFY(last.isFinished());
        QVERIFY(last.result().cached);
        auto first = fetch(factory, server.url("/pressure-0"));
        QTRY_VERIFY(first.isFinished());
        QVERIFY(!first.result().cached);
        QCOMPARE(server.requests["/pressure-0"], 2);
    }
    void survivesManagersAndRestartWithoutCachingOtherResponses()
    {
        QTemporaryDir dir;
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        {
            QmlImageNetworkFactory factory(dir.path());
            std::unique_ptr<QNetworkAccessManager> gui(factory.create(nullptr));
            QVERIFY(!gui->cache());
            for (const auto &path : {"/fresh", "/private", "/json"})
                for (int visit = 0; visit < 2; ++visit) {
                    auto future = fetch(factory, server.url(path));
                    QTRY_VERIFY(future.isFinished());
                    const auto result = future.result();
                    QCOMPARE(result.error, QNetworkReply::NoError);
                    QVERIFY(!result.bytes.isEmpty());
                    QCOMPARE(result.cached, QByteArray(path) == "/fresh" && visit == 1);
                }
            QCOMPARE(server.requests["/fresh"], 1);
            QCOMPARE(server.requests["/private"], 2);
            QCOMPARE(server.requests["/json"], 2);
        }
        {
            QmlImageNetworkFactory reopened(dir.path());
            auto future = fetch(reopened, server.url("/fresh"));
            QTRY_VERIFY(future.isFinished());
            QCOMPARE(future.result().error, QNetworkReply::NoError);
            QVERIFY(future.result().cached);
            QCOMPARE(future.result().bytes, "encoded-image");
            QCOMPARE(server.requests["/fresh"], 1);
        }
    }
    void concurrentManagersAndConditionalRevalidation()
    {
        QTemporaryDir dir;
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QmlImageNetworkFactory factory(dir.path());
        QList<QFuture<Result>> futures;
        for (int i = 0; i < 8; ++i)
            futures.append(fetch(factory, server.url(QString("/image-%1").arg(i))));
        for (auto &future : futures) {
            QTRY_VERIFY(future.isFinished());
            QCOMPARE(future.result().error, QNetworkReply::NoError);
        }
        for (int i = 0; i < 8; ++i) {
            auto future = fetch(factory, server.url(QString("/image-%1").arg(i)));
            QTRY_VERIFY(future.isFinished());
            QVERIFY(future.result().cached);
        }
        for (int visit = 0; visit < 3; ++visit) {
            auto future = fetch(factory, server.url("/revalidate"));
            QTRY_VERIFY(future.isFinished());
            QCOMPARE(future.result().error, QNetworkReply::NoError);
            QCOMPARE(future.result().bytes, "encoded-image");
        }
        QVERIFY(server.conditional);
        QCOMPARE(server.requests["/revalidate"], 2);
    }
    void cancelledAndOversizedStagingIsReleased()
    {
        QTemporaryDir dir;
        QmlImageNetworkFactory factory(dir.path());
        auto future = QtConcurrent::run([&] {
            std::unique_ptr<QNetworkAccessManager> manager(factory.create(nullptr));
            auto *cache = manager->cache();
            QNetworkCacheMetaData metadata;
            metadata.setUrl(QUrl("https://example.test/image.png"));
            metadata.setRawHeaders({{"Content-Type", "image/png"}});
            QPointer<QIODevice> cancelled = cache->prepare(metadata);
            if (!cancelled)
                return false;
            cancelled->write("partial");
            cache->remove(metadata.url());
            if (cancelled)
                return false;
            QPointer<QIODevice> oversized = cache->prepare(metadata);
            if (!oversized || oversized->write(QByteArray(8 * 1024 * 1024 + 1, 'x')) != -1)
                return false;
            cache->insert(oversized);
            return !oversized && !cache->metaData(metadata.url()).isValid();
        });
        QTRY_VERIFY(future.isFinished());
        QVERIFY(future.result());
    }
};
QTEST_GUILESS_MAIN(ImageNetworkCacheTest)
#include "ImageNetworkCacheTest.moc"
