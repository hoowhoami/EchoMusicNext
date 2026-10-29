#include "NetworkMediaConnection.h"
#include "NetworkBootstrap.h"
#include "PlaybackService.h"
#include "cast/DlnaSession.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class CastServiceTestAccess
{
public:
    static NativeBridge &native(CastService &service)
    {
        return service.m_native;
    }
    static void add(CastService &service, const QJsonObject &device)
    {
        service.upsert({"test-renderer", "dlna", device.value("friendly_name").toString(),
                        "Protocol fixture", false, true, device});
    }
};
} // namespace EchoMusic
namespace
{
QByteArray response(const QByteArray &body, const QByteArray &type = "text/xml")
{
    return "HTTP/1.1 200 OK\r\nContent-Type: " + type +
           "\r\nContent-Length: " + QByteArray::number(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
}
QByteArray wave()
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 data = 16000 * 2 * 10;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + data);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << data;
    bytes.append(QByteArray(data, '\0'));
    return bytes;
}
/// A UPnP protocol receiver, not a fabricated music-service response. Production
/// Rust fetches its XML and exchanges real SOAP; its media GET uses real TCP.
class Renderer : public QObject
{
public:
    QTcpServer server;
    QNetworkAccessManager reader;
    QString uri, state = "STOPPED", time = "00:00:00", didl;
    QStringList commands;
    QByteArray media;
    int volume = 40;
    bool rejectVolume = false;
    Renderer()
    {
        reader.setProxy(QNetworkProxy::NoProxy);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            auto *peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
            connect(peer, &QTcpSocket::readyRead, peer, [this, peer] {
                auto bytes = peer->property("input").toByteArray() + peer->readAll();
                peer->setProperty("input", bytes);
                const auto end = bytes.indexOf("\r\n\r\n");
                if (end < 0 || peer->property("answered").toBool())
                    return;
                const auto lower = bytes.left(end).toLower();
                const auto lengthAt = lower.indexOf("content-length:");
                const auto length =
                    lengthAt < 0 ? 0
                                 : lower.mid(lengthAt + 15).split('\r').first().trimmed().toInt();
                if (bytes.size() < end + 4 + length)
                    return;
                peer->setProperty("answered", true);
                if (bytes.startsWith("GET /expired ")) {
                    peer->write(
                        "HTTP/1.1 403 Expired\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else if (bytes.startsWith("GET /device.xml ")) {
                    peer->write(response(
                        R"XML(<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType><friendlyName>Qt protocol receiver</friendlyName><manufacturer>Fixture</manufacturer><modelName>Protocol</modelName><UDN>uuid:test-renderer</UDN><serviceList><service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType><serviceId>urn:upnp-org:serviceId:AVTransport</serviceId><controlURL>/av</controlURL><eventSubURL>/events</eventSubURL><SCPDURL>/av.xml</SCPDURL></service><service><serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType><serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId><controlURL>/rc</controlURL><eventSubURL>/events</eventSubURL><SCPDURL>/rc.xml</SCPDURL></service></serviceList></device></root>)XML"));
                } else {
                    const auto header = bytes.left(end).split('\n');
                    QByteArray soap;
                    for (const auto &line : header)
                        if (line.toLower().startsWith("soapaction:"))
                            soap = line.mid(11).trimmed().replace("\"", "");
                    const auto action = QString::fromUtf8(soap.mid(soap.indexOf('#') + 1));
                    const auto service = soap.left(soap.indexOf('#'));
                    commands.append(action);
                    QMap<QString, QString> args;
                    QXmlStreamReader xml(bytes.mid(end + 4, length));
                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isStartElement() && QStringList{"CurrentURI", "CurrentURIMetaData",
                                                                "Target", "DesiredVolume"}
                                                        .contains(xml.name().toString())) {
                            const auto name = xml.name().toString();
                            args.insert(name, xml.readElementText());
                        }
                    }
                    QByteArray values;
                    if (action == "GetVolume")
                        values =
                            "<CurrentVolume>" + QByteArray::number(volume) + "</CurrentVolume>";
                    else if (action == "SetVolume") {
                        if (rejectVolume) {
                            peer->write("HTTP/1.1 500 Rejected\r\nContent-Length: 0\r\nConnection: "
                                        "close\r\n\r\n");
                            peer->disconnectFromHost();
                            return;
                        }
                        volume = args.value("DesiredVolume").toInt();
                    } else if (action == "SetAVTransportURI") {
                        uri = args.value("CurrentURI");
                        didl = args.value("CurrentURIMetaData");
                        time = "00:00:00";
                        state = "STOPPED";
                    } else if (action == "Play") {
                        state = "PLAYING";
                        QNetworkRequest request{QUrl(uri)};
                        request.setRawHeader("Range", "bytes=4-31");
                        auto *reply = reader.get(request);
                        connect(reply, &QNetworkReply::finished, reply, [this, reply] {
                            media = reply->readAll();
                            reply->deleteLater();
                        });
                    } else if (action == "Pause")
                        state = "PAUSED_PLAYBACK";
                    else if (action == "Stop")
                        state = "STOPPED";
                    else if (action == "Seek")
                        time = args.value("Target");
                    else if (action == "GetTransportInfo")
                        values =
                            "<CurrentTransportState>" + state.toUtf8() + "</CurrentTransportState>";
                    else if (action == "GetPositionInfo")
                        values = "<TrackDuration>00:00:10</TrackDuration><RelTime>" +
                                 time.toUtf8() + "</RelTime><TrackURI>" +
                                 uri.toHtmlEscaped().toUtf8() + "</TrackURI>";
                    peer->write(response(
                        "<s:Envelope "
                        "xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><u:" +
                        action.toUtf8() + "Response xmlns:u=\"" + service + "\">" + values +
                        "</u:" + action.toUtf8() + "Response></s:Body></s:Envelope>"));
                }
                peer->disconnectFromHost();
            });
        });
    }
};
} // namespace
class CastMediaTest : public QObject
{
    Q_OBJECT
private slots:
    void queuedNativeDeliveryHonorsCancellationAndShutdown()
    {
        NativeBridge native;
        QSignalSpy closed(&native, &NativeBridge::closed);
        int delivered = 0;
        quint64 first = 0, second = 0;
        connect(&native, &NativeBridge::completed, this,
                [&](quint64 id, quint64, bool, const QByteArray &) {
                    QCOMPARE(QThread::currentThread(), qApp->thread());
                    QCOMPARE(id, first);
                    ++delivered;
                    // The second result may already be in the same GUI batch. Retiring
                    // its Qt ownership must suppress it even after native completion.
                    QVERIFY(native.cancel(second));
                    native.shutdown();
                });
        first = native.submit(NativeBridge::AirplayStatus, QJsonObject{}, 1);
        second = native.submit(NativeBridge::AirplayStatus, QJsonObject{}, 1);
        QVERIFY(first && second);
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QCoreApplication::sendPostedEvents();
        QCOMPARE(delivered, 1);
        QVERIFY(!native.hasPendingWork());
    }
    void nativeCompletionCanSynchronouslyRetireItsOwner()
    {
        for (int iteration = 0; iteration < 8; ++iteration) {
            auto native = std::make_unique<NativeBridge>();
            const QPointer<NativeBridge> guard(native.get());
            int delivered = 0;
            connect(native.get(), &NativeBridge::completed, this,
                    [&](quint64, quint64, bool, const QByteArray &) {
                        ++delivered;
                        native.reset();
                    });
            QVERIFY(native->submit(NativeBridge::AirplayStatus, QJsonObject{}, 1));
            QVERIFY(native->submit(NativeBridge::AirplayStatus, QJsonObject{}, 1));
            QTRY_VERIFY_WITH_TIMEOUT(!guard, 5000);
            QCoreApplication::sendPostedEvents();
            QCOMPARE(delivered, 1);
        }
    }
    void localMediaRangeAndHead_data()
    {
        QTest::addColumn<QByteArray>("method");
        QTest::addColumn<QByteArray>("range");
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("first");
        QTest::addColumn<int>("count");
        QTest::newRow("whole") << QByteArray("GET") << QByteArray() << 200 << 0 << 1024;
        QTest::newRow("prefix") << QByteArray("GET") << QByteArray("bytes=0-31") << 206 << 0 << 32;
        QTest::newRow("suffix") << QByteArray("GET") << QByteArray("bytes=-31") << 206 << 993 << 31;
        QTest::newRow("open") << QByteArray("GET") << QByteArray("bytes=900-") << 206 << 900 << 124;
        QTest::newRow("clamp") << QByteArray("GET") << QByteArray("bytes=1000-9999") << 206 << 1000
                               << 24;
        QTest::newRow("invalid") << QByteArray("GET") << QByteArray("bytes=1024-") << 416 << 0 << 0;
        QTest::newRow("head") << QByteArray("HEAD") << QByteArray("bytes=10-19") << 206 << 0 << 0;
    }
    void localMediaRangeAndHead()
    {
        QFETCH(QByteArray, method);
        QFETCH(QByteArray, range);
        QFETCH(int, status);
        QFETCH(int, first);
        QFETCH(int, count);
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("中文.wav"));
        const QByteArray bytes(1024, 'x');
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QNetworkAccessManager reader;
        reader.setProxy(QNetworkProxy::NoProxy);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            new NetworkMediaConnection(
                server.nextPendingConnection(), reader,
                [&](const QString &token) {
                    return token == "registered" ? QUrl::fromLocalFile(path) : QUrl();
                },
                10000, &server);
        });
        QNetworkRequest request{
            QUrl(QString("http://127.0.0.1:%1/registered").arg(server.serverPort()))};
        if (!range.isEmpty())
            request.setRawHeader("Range", range);
        auto *reply = method == "HEAD" ? reader.head(request) : reader.get(request);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), status);
        QCOMPARE(reply->readAll(), bytes.mid(first, count));
        if (method == "HEAD")
            QCOMPARE(reply->rawHeader("Content-Length"), QByteArray("10"));
        if (status == 416)
            QCOMPARE(reply->rawHeader("Content-Range"), QByteArray("bytes */1024"));
    }
    void timeAndEndClassificationDoesNotAdvanceExternalOrExplicitStops()
    {
        QCOMPARE(DlnaSession::parseTime("01:02:03.5"), qint64(3723500));
        QCOMPARE(DlnaSession::parseTime("00:99:00"), qint64(-1));
        QCOMPARE(DlnaSession::formatTime(3723500), QString("01:02:03"));
        QVERIFY(DlnaSession::naturalEnd("STOPPED", "ours", "ours", 9000, 10000, false));
        QVERIFY(!DlnaSession::naturalEnd("STOPPED", "ours", "other", 9000, 10000, false));
        QVERIFY(!DlnaSession::naturalEnd("STOPPED", "ours", "ours", 9000, 10000, true));
        QVERIFY(!DlnaSession::naturalEnd("STOPPED", "ours", "ours", 4000, 10000, false));
        QVERIFY(!DlnaSession::naturalEnd("STOPPED", "ours", "ours", 9000, 0, false));
    }
    void ignoredOriginRangesRemainBounded_data()
    {
        QTest::addColumn<QByteArray>("range");
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("start");
        QTest::addColumn<int>("count");
        QTest::newRow("late-prefix")
            << QByteArray("bytes=1800000-1800123") << 206 << 1800000 << 124;
        QTest::newRow("suffix") << QByteArray("bytes=-123") << 206 << 2097029 << 123;
        QTest::newRow("open") << QByteArray("bytes=2097000-") << 206 << 2097000 << 152;
        QTest::newRow("clamp") << QByteArray("bytes=2097000-9999999") << 206 << 2097000 << 152;
        QTest::newRow("unsatisfiable") << QByteArray("bytes=2097152-") << 416 << 0 << 0;
        QTest::newRow("empty-suffix") << QByteArray("bytes=-0") << 416 << 0 << 0;
        QTest::newRow("unsupported-multipart")
            << QByteArray("bytes=1-2,4-5") << 200 << 0 << 2097152;
    }
    void ignoredOriginRangesRemainBounded()
    {
        QFETCH(QByteArray, range);
        QFETCH(int, status);
        QFETCH(int, start);
        QFETCH(int, count);
        QByteArray bytes(2 * 1024 * 1024, Qt::Uninitialized);
        for (qsizetype i = 0; i < bytes.size(); ++i)
            bytes[i] = char(i % 251);
        QTcpServer origin, relay;
        QVERIFY(origin.listen(QHostAddress::LocalHost));
        QVERIFY(relay.listen(QHostAddress::LocalHost));
        connect(&origin, &QTcpServer::newConnection, &origin, [&] {
            auto *peer = origin.nextPendingConnection();
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
            connect(peer, &QTcpSocket::readyRead, peer, [&, peer] {
                if (peer->property("answered").toBool())
                    return;
                const auto input = peer->property("input").toByteArray() + peer->readAll();
                peer->setProperty("input", input);
                if (!input.contains("\r\n\r\n"))
                    return;
                peer->setProperty("answered", true);
                const bool head = input.startsWith("HEAD ");
                peer->write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: " +
                            QByteArray::number(bytes.size()) + "\r\nConnection: close\r\n\r\n");
                if (!head)
                    peer->write(bytes);
                peer->disconnectFromHost();
            });
        });
        QNetworkAccessManager reader;
        reader.setProxy(QNetworkProxy::NoProxy);
        connect(&relay, &QTcpServer::newConnection, &relay, [&] {
            new NetworkMediaConnection(
                relay.nextPendingConnection(), reader,
                [&](const QString &) {
                    return QUrl(QString("http://127.0.0.1:%1/media").arg(origin.serverPort()));
                },
                10000, &relay, {}, true);
        });
        QNetworkRequest request{
            QUrl(QString("http://127.0.0.1:%1/registered").arg(relay.serverPort()))};
        request.setRawHeader("Range", range);
        auto *reply = reader.get(request);
        QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), status);
        QCOMPARE(reply->readAll(), bytes.mid(start, count));
        if (status == 206) {
            QCOMPARE(reply->rawHeader("Content-Length"), QByteArray::number(count));
            QCOMPARE(reply->rawHeader("Content-Range"), "bytes " + QByteArray::number(start) + '-' +
                                                            QByteArray::number(start + count - 1) +
                                                            "/2097152");
            auto *head = reader.head(request);
            QSignalSpy headDone(head, &QNetworkReply::finished);
            QTRY_COMPARE(headDone.size(), 1);
            QCOMPARE(head->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
            QCOMPARE(head->rawHeader("Content-Length"), QByteArray::number(count));
            QVERIFY(head->readAll().isEmpty());
        }
        // A stale If-Range validator requests the full representation. The
        // relay must not turn the origin's 200 into a partial response.
        request.setRawHeader("If-Range", "\"stale\"");
        auto *validated = reader.get(request);
        QSignalSpy validatedDone(validated, &QNetworkReply::finished);
        QTRY_COMPARE(validatedDone.size(), 1);
        QCOMPARE(validated->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(validated->readAll(), bytes);
    }
    void nativeSoapMediaRangePlaybackQueueAndLocalHandoff()
    {
        QTemporaryDir directory;
        const auto bytes = wave();
        for (const auto &name : {"a.wav", "b.wav"}) {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        }
        EchoMusic::SettingsStore preferences(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(preferences, api);
        PlaybackService playback(api, preferences, nullptr, directory.filePath("playback.sqlite"),
                                 &network);
        auto *cast = playback.cast();
        playback.setVolume(65);
        auto &native = CastServiceTestAccess::native(*cast);
        Renderer renderer;
        QVERIFY(renderer.server.listen(QHostAddress::LocalHost));
        QSignalSpy nativeReplies(&native, &NativeBridge::completed);
        const auto location =
            QString("http://127.0.0.1:%1/device.xml").arg(renderer.server.serverPort());
        const auto request = native.submit(NativeBridge::UpnpDevice, location, 1);
        QVERIFY(request);
        QTRY_VERIFY_WITH_TIMEOUT(!nativeReplies.isEmpty(), 10000);
        const auto result = nativeReplies.takeFirst();
        QVERIFY2(result[2].toBool(), result[3].toByteArray().constData());
        const auto device = QJsonDocument::fromJson(result[3].toByteArray()).object();
        CastServiceTestAccess::add(*cast, device);
        cast->setEnabled(true);
        cast->connectTarget("test-renderer");
        QTRY_VERIFY_WITH_TIMEOUT(cast->remoteMedia(), 10000);
        QCOMPARE(playback.volume(), 40.);
        QCOMPARE(preferences.value("playback/volume").toDouble(), 65.);
        PlaybackTrack first;
        first.id = "a";
        first.title = "First";
        first.artist = "Singer & More";
        first.localPath = directory.filePath("a.wav");
        first.durationMs = 10000;
        PlaybackTrack second = first;
        second.id = "b";
        second.title = "Second";
        second.localPath = directory.filePath("b.wav");
        playback.playTracks({first, second}, 0, "Protocol queue", "dlna-fixture");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 15000);
        QTRY_COMPARE(renderer.media, bytes.mid(4, 28));
        QVERIFY(renderer.didl.contains("Singer &amp; More"));
        const auto volumeKey =
            "cast/volumes/" +
            QString::fromLatin1(
                QByteArray("test-renderer").toBase64(QByteArray::Base64UrlEncoding));
        const auto priorCommands = renderer.commands.count("SetVolume");
        for (int value = 41; value <= 90; ++value)
            playback.setVolume(value);
        QTRY_COMPARE(renderer.volume, 90);
        QTRY_COMPARE(preferences.value(volumeKey).toInt(), 90);
        QVERIFY(renderer.commands.count("SetVolume") - priorCommands <= 2);
        renderer.rejectVolume = true;
        playback.setVolume(80);
        QTRY_COMPARE(playback.volume(), 90.);
        QCOMPARE(preferences.value(volumeKey).toInt(), 90);
        renderer.rejectVolume = false;
        playback.pause();
        QTRY_VERIFY(!playback.playing());
        playback.play();
        QTRY_VERIFY(playback.playing());
        playback.seek(4500);
        QTRY_VERIFY(playback.positionMs() >= 4000);
        renderer.time = "00:00:10";
        renderer.state = "STOPPED";
        QTRY_VERIFY_WITH_TIMEOUT(playback.title() == "Second" && playback.playing(), 8000);
        QCOMPARE(playback.queue()->currentIndex(), 1);
        // An external renderer taking a new URI must never advance our queue.
        const auto ownedUri = renderer.uri;
        renderer.uri = "http://external.invalid/other";
        QTRY_VERIFY_WITH_TIMEOUT(cast->errorMessage().contains(QStringLiteral("接管")), 4000);
        QCOMPARE(playback.queue()->currentIndex(), 1);
        renderer.uri = ownedUri;
        playback.playIndex(0);
        QTRY_VERIFY(playback.playing() && playback.title() == "First");
        playback.seek(3500);
        QTRY_VERIFY(playback.positionMs() >= 3000);
        playback.setVolume(55);
        QTRY_COMPARE(renderer.volume, 55);
        QCOMPARE(preferences.value("playback/volume").toDouble(), 65.);
        const QUrl revoked(renderer.uri);
        auto unknown = revoked;
        unknown.setPath("/not-registered");
        auto *unknownReply = renderer.reader.get(QNetworkRequest(unknown));
        QSignalSpy unknownDone(unknownReply, &QNetworkReply::finished);
        QTRY_COMPARE(unknownDone.size(), 1);
        QCOMPARE(unknownReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
        cast->useLocal();
        QTRY_VERIFY_WITH_TIMEOUT(!cast->remote(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 15000);
        QVERIFY(playback.positionMs() >= 3000);
        QCOMPARE(playback.volume(), 65.);
        QVERIFY(renderer.commands.contains("Stop"));
        auto *revokedReply = renderer.reader.get(QNetworkRequest(revoked));
        QSignalSpy revokedDone(revokedReply, &QNetworkReply::finished);
        QTRY_COMPARE(revokedDone.size(), 1);
        QVERIFY(revokedReply->error() != QNetworkReply::NoError);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void expiredRemoteMediaUsesOneCurrentResourceNotification()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("preferences.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(settings, api);
        NativeBridge native;
        DlnaSession session(native);
        session.setNetwork(&network);
        Renderer renderer;
        QVERIFY(renderer.server.listen(QHostAddress::LocalHost));
        const auto root = QString("http://127.0.0.1:%1").arg(renderer.server.serverPort());
        QSignalSpy replies(&native, &NativeBridge::completed);
        QVERIFY(native.submit(NativeBridge::UpnpDevice, root + "/device.xml", 1));
        QTRY_VERIFY_WITH_TIMEOUT(!replies.isEmpty(), 10000);
        QVERIFY2(replies.first()[2].toBool(), replies.first()[3].toByteArray().constData());
        QSignalSpy connected(&session, &DlnaSession::connected);
        session.connectDevice(QJsonDocument::fromJson(replies.first()[3].toByteArray()).object());
        QTRY_COMPARE(connected.size(), 1);
        QSignalSpy expired(&session, &DlnaSession::sourceExpired);
        connect(&session, &DlnaSession::outputEvent, &session,
                [&session](const QJsonObject &event) {
                    if (event.value("event") == "file-loaded")
                        session.play();
                });
        PlaybackTrack track;
        track.title = "Expiring source";
        track.durationMs = 10000;
        session.load(root + "/expired", track, 1);
        QTRY_COMPARE_WITH_TIMEOUT(expired.size(), 1, 10000);
        network.mediaRequestRejected("not-current", 403);
        network.mediaRequestRejected(QUrl(renderer.uri).path().mid(1), 403);
        QCOMPARE(expired.size(), 1);
        bool drained = false;
        session.shutdown([&] { drained = true; });
        QTRY_VERIFY(drained);
        QSignalSpy closed(&native, &NativeBridge::closed);
        native.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 10000);
        api.shutdown();
    }
};
int main(int argc, char **argv)
{
    initializeNetworkEnvironment();
    QGuiApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    CastMediaTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "CastMediaTest.moc"
