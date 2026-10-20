#include "NetworkBootstrap.h"
#include <QGuiApplication>
#include "PlaybackService.h"
#include "TestProxyStore.h"
#include "PlayerEnginePreferences.h"
#include <QDataStream>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QtTest>
#include <limits>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void stalled(PlaybackService &playback)
    {
        // Internal native-event seam, not an upstream API fixture. The test loads
        // real PCM before delivering a deterministic stall notification.
        playback.onEvent({{"event", "stalled"}, {"track_seq", qint64(playback.m_audioSequence)}});
    }
    static quint64 audioSequence(const PlaybackService &playback)
    {
        return playback.m_audioSequence;
    }
    static bool powerActive(const PlaybackService &playback)
    {
        return playback.m_sleepInhibitor.active();
    }
    static bool ready(const PlaybackService &playback)
    {
        return playback.m_ready;
    }
    static AudioEngine &engine(PlaybackService &playback)
    {
        return playback.m_engine;
    }
    static NativeBridge &native(PlaybackService &playback)
    {
        return playback.m_native;
    }
    static void ownProbe(PlaybackService &playback, quint64 id)
    {
        playback.m_trackProbe = id;
    }
};
} // namespace EchoMusic
class PlayerEngineSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void nativeAudioAndProbeUseUnifiedProxyAndLivePolicy_data()
    {
        QTest::addColumn<bool>("authenticated");
        QTest::newRow("anonymous-proxy") << false;
        QTest::newRow("explicit-basic-proxy") << true;
    }
    void nativeAudioAndProbeUseUnifiedProxyAndLivePolicy()
    {
        QFETCH(bool, authenticated);
        QTemporaryDir directory;
        QSettings settings(directory.filePath("network.ini"), QSettings::IniFormat);
        settings.setValue("playback/volume", 0);
        ApiClient api(DeviceIdentity::temporary());
        TestProxyStore store;
        NetworkPreferences preferences(settings, api, nullptr, &store);
        QTcpServer first, second;
        first.setProxy(QNetworkProxy::NoProxy);
        second.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(first.listen(QHostAddress::LocalHost));
        QVERIFY(second.listen(QHostAddress::LocalHost));
        QByteArray pcm;
        QDataStream stream(&pcm, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        constexpr quint32 dataSize = 16000 * 2 * 10;
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + dataSize);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000)
               << quint16(2) << quint16(16);
        stream.writeRawData("data", 4);
        stream << dataSize;
        const QByteArray silence(dataSize, '\0');
        QCOMPARE(stream.writeRawData(silence.constData(), silence.size()), silence.size());
        QStringList routes;
        const auto serve = [&](QTcpServer &server, const QString &label) {
            connect(&server, &QTcpServer::newConnection, &server, [&, listener = &server, label] {
                while (listener->hasPendingConnections()) {
                    auto *peer = listener->nextPendingConnection();
                    connect(peer, &QTcpSocket::readyRead, peer, [&, peer, label] {
                        const auto header =
                            peer->property("header").toByteArray() + peer->readAll();
                        peer->setProperty("header", header);
                        if (!header.contains("\r\n\r\n") || peer->property("answered").toBool())
                            return;
                        if (authenticated && !header.toLower().contains("proxy-authorization:")) {
                            peer->write(
                                "HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: "
                                "Basic realm=\"consumer\"\r\nContent-Length: "
                                "0\r\nProxy-Connection: keep-alive\r\n\r\n");
                            peer->setProperty("header", QByteArray());
                            return;
                        }
                        if (authenticated)
                            QVERIFY(header.toLower().contains(
                                "proxy-authorization: basic " +
                                QByteArray("proxy-user:proxy-secret").toBase64().toLower()));
                        peer->setProperty("answered", true);
                        routes.append(label + ':' +
                                      QString::fromLatin1(header.left(header.indexOf("\r\n"))));
                        qint64 offset = 0;
                        for (const auto &line : header.split('\n'))
                            if (line.toLower().startsWith("range: bytes="))
                                offset = line.mid(13).split('-').first().trimmed().toLongLong();
                        offset = qBound<qint64>(0, offset, pcm.size());
                        auto reply =
                            offset ? QByteArray(
                                         "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes ") +
                                         QByteArray::number(offset) + '-' +
                                         QByteArray::number(pcm.size() - 1) + '/' +
                                         QByteArray::number(pcm.size()) + "\r\n"
                                   : QByteArray("HTTP/1.1 200 OK\r\n");
                        reply +=
                            "Content-Type: audio/wav\r\nAccept-Ranges: bytes\r\nContent-Length: " +
                            QByteArray::number(pcm.size() - offset) +
                            "\r\nConnection: close\r\n\r\n";
                        peer->write(reply + pcm.mid(offset));
                        peer->disconnectFromHost();
                    });
                    connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
                }
            });
        };
        serve(first, "first");
        serve(second, "second");
        QSignalSpy applied(&preferences, &NetworkPreferences::applied);
        const auto selectProxy = [&](const QTcpServer &server) {
            preferences.apply(
                "fixed_servers", {}, QString("http://127.0.0.1:%1").arg(server.serverPort()),
                authenticated ? QString("proxy-user") : QString(),
                authenticated ? QString("proxy-secret") : QString(), false, "<local>", 0, 60);
        };
        selectProxy(first);
        QTRY_COMPARE(applied.size(), 1);
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"),
                                 &preferences);
        auto &engine = PlaybackServiceTestAccess::engine(playback);
        QSignalSpy events(&engine, &AudioEngine::eventReceived);
        QTRY_VERIFY_WITH_TIMEOUT(PlaybackServiceTestAccess::ready(playback), 15000);
        auto &native = PlaybackServiceTestAccess::native(playback);
        QSignalSpy probes(&native, &NativeBridge::completed);
        QVERIFY(native.submit(NativeBridge::AudioTracks,
                              QString("http://audio-source.invalid/probe.wav"), 991));
        QTRY_COMPARE_WITH_TIMEOUT(probes.size(), 1, 10000);
        QVERIFY(probes[0][2].toBool());
        QVERIFY(routes.contains("first:GET http://audio-source.invalid/probe.wav HTTP/1.1"));
        const auto loaded = [&](quint64 sequence) {
            for (const auto &entry : events) {
                const auto event = entry[0].toJsonObject();
                if (event.value("event") == "file-loaded" &&
                    event.value("track_seq").toInteger() == qint64(sequence))
                    return true;
            }
            return false;
        };
        engine.load("http://audio-source.invalid/first.wav", 992);
        QTRY_VERIFY_WITH_TIMEOUT(loaded(992), 10000);
        QVERIFY(routes.contains("first:GET http://audio-source.invalid/first.wav HTTP/1.1"));
        selectProxy(second);
        QTRY_COMPARE(applied.size(), 2);
        engine.load("http://audio-source.invalid/second.wav", 993);
        QTRY_VERIFY_WITH_TIMEOUT(loaded(993), 10000);
        QVERIFY(routes.contains("second:GET http://audio-source.invalid/second.wav HTTP/1.1"));
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void stoppingCancelsActiveProbeAndSkipsCancelledQueuedProbe()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath("network.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        QVERIFY(preferences.applyTimeouts(0, 300));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int connections = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            ++connections;
            // Hold the response headers: invalid/empty audio must not let the
            // format probe fail before the network deadline or cancellation.
            connect(socket, &QTcpSocket::readyRead, socket, [socket] { socket->readAll(); });
        });
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"),
                                 &preferences);
        QTRY_VERIFY_WITH_TIMEOUT(PlaybackServiceTestAccess::ready(playback), 15000);
        auto &native = PlaybackServiceTestAccess::native(playback);
        QSignalSpy results(&native, &NativeBridge::completed);
        const auto source = QString("http://127.0.0.1:%1/probe.mkv").arg(server.serverPort());
        const auto active = native.submit(NativeBridge::AudioTracks, source, 888);
        QVERIFY(active);
        // Register a real accepted request at the source-ownership seam. This
        // is not an API fixture; public stop must retire the active native I/O.
        PlaybackServiceTestAccess::ownProbe(playback, active);
        QTRY_COMPARE_WITH_TIMEOUT(connections, 1, 5000);
        const auto queued = native.submit(NativeBridge::AudioTracks, source, 889);
        QVERIFY(queued);
        QVERIFY(native.cancel(queued));
        const auto successor = native.submit(NativeBridge::AirplayStatus, QJsonObject{}, 890);
        QVERIFY(successor);
        QElapsedTimer elapsed;
        elapsed.start();
        playback.stop();
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000);
        QCOMPARE(results[0][0].toULongLong(), successor);
        QCOMPARE(results[0][1].toULongLong(), 890);
        QVERIFY(results[0][2].toBool());
        QCOMPARE(connections, 1);
        QVERIFY(elapsed.elapsed() < 5000);
        QVERIFY(!native.cancel(active));
        QVERIFY(!native.cancel(successor));
        QVERIFY(!native.cancel(0));
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QCOMPARE(results.size(), 1);
    }
    void nativeTrackProbeShutdownCancelsLongNetworkDeadline()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath("network.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        QVERIFY(preferences.applyTimeouts(0, 300));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        bool connected = false;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connected = true;
            // Hold the response headers: invalid/empty audio must not let the
            // format probe fail before the network deadline or cancellation.
            connect(socket, &QTcpSocket::readyRead, socket, [socket] { socket->readAll(); });
        });
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"),
                                 &preferences);
        QTRY_VERIFY_WITH_TIMEOUT(PlaybackServiceTestAccess::ready(playback), 15000);
        auto &native = PlaybackServiceTestAccess::native(playback);
        QVERIFY(native.submit(NativeBridge::AudioTracks,
                              QString("http://127.0.0.1:%1/probe.mkv").arg(server.serverPort()),
                              888));
        QTRY_VERIFY_WITH_TIMEOUT(connected, 5000);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        QElapsedTimer elapsed;
        elapsed.start();
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QVERIFY(elapsed.elapsed() < 5000);
    }
    void persistedNetworkDeadlineAppliesBeforeFirstPlayerLoad()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto settingsPath = directory.filePath("network.ini");
        ApiClient api(DeviceIdentity::temporary());
        {
            QSettings initial(settingsPath, QSettings::IniFormat);
            NetworkPreferences saved(initial, api);
            QVERIFY(saved.applyTimeouts(0, 1));
        }
        QSettings settings(settingsPath, QSettings::IniFormat);
        NetworkPreferences preferences(settings, api);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int connections = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            ++connections;
            // Hold the response headers: invalid/empty audio must not let the
            // format probe fail before the network deadline or cancellation.
            connect(socket, &QTcpSocket::readyRead, socket, [socket] { socket->readAll(); });
        });
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"),
                                 &preferences);
        auto &engine = PlaybackServiceTestAccess::engine(playback);
        QSignalSpy events(&engine, &AudioEngine::eventReceived);
        QTRY_VERIFY_WITH_TIMEOUT(PlaybackServiceTestAccess::ready(playback), 15000);
        // Multitrack MKV metadata uses the independent native actor. Its HTTP
        // reader must also capture the saved deadline, not a 60-second default.
        auto &native = PlaybackServiceTestAccess::native(playback);
        QSignalSpy probes(&native, &NativeBridge::completed);
        QElapsedTimer probeElapsed;
        probeElapsed.start();
        const auto probe =
            native.submit(NativeBridge::AudioTracks,
                          QString("http://127.0.0.1:%1/probe.mkv").arg(server.serverPort()), 777);
        QVERIFY(probe);
        const auto probeFinished = [&] {
            for (const auto &entry : probes)
                if (entry[0].toULongLong() == probe && entry[1].toULongLong() == 777)
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(probeFinished(), 6000);
        QVERIFY(probeElapsed.elapsed() >= 500 && probeElapsed.elapsed() < 6000);
        const auto errors = [&] {
            int count = 0;
            for (const auto &entry : events)
                if (entry[0].toJsonObject().value("event") == "error")
                    ++count;
            return count;
        };
        const auto loadAndWait = [&](quint64 sequence) {
            engine.load(QString("http://127.0.0.1:%1/deadline.wav").arg(server.serverPort()),
                        sequence);
        };
        // No direct engine timeout setter: the production playback-ready path
        // must have propagated the persisted preference before this first load.
        QElapsedTimer elapsed;
        elapsed.start();
        loadAndWait(1);
        QTRY_VERIFY_WITH_TIMEOUT(connections > 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(errors(), 1, 6000);
        QVERIFY(elapsed.elapsed() >= 500 && elapsed.elapsed() < 6000);
        // A subsequent applied preference reaches the same engine without
        // rebuilding it; existing readers retain their original snapshot.
        QVERIFY(preferences.applyTimeouts(0, 2));
        elapsed.restart();
        loadAndWait(2);
        QTRY_COMPARE_WITH_TIMEOUT(errors(), 2, 7000);
        QVERIFY(elapsed.elapsed() >= 1500 && elapsed.elapsed() < 7000);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void playerNetworkDeadlineReachesRealDecoderAndRejectsZero()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        bool connected = false;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connected = true;
            // Deliberately stall the response of a transport-only local stream.
            // Hold the response headers: invalid/empty audio must not let the
            // format probe fail before the network deadline or cancellation.
            connect(socket, &QTcpSocket::readyRead, socket, [socket] { socket->readAll(); });
        });
        AudioEngine engine;
        QSignalSpy events(&engine, &AudioEngine::eventReceived),
            closed(&engine, &AudioEngine::closed);
        const auto hasEvent = [&](const QString &kind) {
            for (const auto &entry : events)
                if (entry[0].toJsonObject().value("event") == kind)
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasEvent("ready"), 15000);
        engine.networkTimeout(0);
        QTRY_VERIFY_WITH_TIMEOUT(hasEvent("control-error"), 5000);
        engine.networkTimeout(1);
        QElapsedTimer elapsed;
        elapsed.start();
        engine.load(QString("http://127.0.0.1:%1/deadline.wav").arg(server.serverPort()), 1);
        QTRY_VERIFY_WITH_TIMEOUT(connected, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(hasEvent("error"), 6000);
        QVERIFY2(elapsed.elapsed() >= 500,
                 QJsonDocument(events.last()[0].toJsonObject()).toJson(QJsonDocument::Compact));
        QVERIFY(elapsed.elapsed() < 6000);
        engine.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void nativeSleepLeaseReconcilesSuspendPreferenceAndRelease()
    {
        SystemSleepInhibitor inhibitor;
        QVERIFY(!inhibitor.active());
        inhibitor.setRequired(true);
        QTRY_VERIFY_WITH_TIMEOUT(inhibitor.active(), 10000);
        QVERIFY(inhibitor.error().isEmpty());
        inhibitor.setSystemSuspended(true);
        QVERIFY(!inhibitor.active());
        inhibitor.setRequired(false);
        inhibitor.setSystemSuspended(false);
        QVERIFY(!inhibitor.active());
        inhibitor.setRequired(true);
        QTRY_VERIFY_WITH_TIMEOUT(inhibitor.active(), 10000);
        inhibitor.setSystemSuspended(true);
        QVERIFY(!inhibitor.active());
        inhibitor.setSystemSuspended(false);
        QTRY_VERIFY_WITH_TIMEOUT(inhibitor.active(), 10000);
        inhibitor.setRequired(false);
        QVERIFY(!inhibitor.active());
    }
    void persistedDecimalOptionsAndInvalidInput()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("preferences.ini"), QSettings::IniFormat);
        settings.setValue("playerEngine/audioBufferSecs", "invalid");
        settings.setValue("playerEngine/audioSamplerate", "unsupported");
        PlayerEnginePreferences preferences(settings);
        QCOMPARE(preferences.audioBufferSecs(), .2);
        QCOMPARE(preferences.audioSamplerate(), QString("auto"));
        preferences.setAudioBufferSecs(.35);
        preferences.setCachePauseWaitSecs(.7);
        preferences.setDemuxerMaxMb(256.5);
        preferences.setCachePause(false);
        preferences.setAudioSamplerate("44100");
        preferences.setAudioChannels("mono");
        preferences.setPlaybackStallMaxAttempts(200);
        preferences.setAudioBufferSecs(std::numeric_limits<double>::quiet_NaN());
        preferences.setAudioSamplerate("unsupported");
        settings.sync();
        QSettings second(dir.filePath("preferences.ini"), QSettings::IniFormat);
        PlayerEnginePreferences reopened(second);
        QCOMPARE(reopened.audioBufferSecs(), .35);
        QCOMPARE(reopened.audioSamplerate(), QString("44100"));
        QCOMPARE(reopened.playbackStallMaxAttempts(), 10);
        const auto options = reopened.startupOptions();
        QCOMPARE(options.value("cache_pause_wait_secs").toDouble(), .7);
        QCOMPARE(options.value("demuxer_max_bytes").toDouble(), 256.5 * 1024 * 1024);
        QVERIFY(!options.value("cache_pause").toBool());
        QVERIFY(!options.contains("playResumeTimeout"));
        // Both output and decoder options cross the actual worker/FFI initializer.
        AudioEngine engine(nullptr, options);
        QSignalSpy events(&engine, &AudioEngine::eventReceived),
            closed(&engine, &AudioEngine::closed);
        const auto ready = [&] {
            for (const auto &entry : events)
                if (entry[0].toJsonObject().value("event") == "ready")
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 15000);
        engine.stallTimeout(0);
        engine.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void nativeStallRecoveryPreservesPositionAndHonorsPauseAndLimit()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("recovery.wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        constexpr quint32 bytes = 16000 * 2 * 20;
        stream.writeRawData("RIFF", 4);
        stream << quint32(36 + bytes);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000)
               << quint16(2) << quint16(16);
        stream.writeRawData("data", 4);
        stream << bytes;
        const QByteArray silence(bytes, '\0');
        QCOMPARE(stream.writeRawData(silence.constData(), silence.size()), silence.size());
        file.close();
        QSettings settings(dir.filePath("preferences.ini"), QSettings::IniFormat);
        settings.setValue("playback/volume", 0);
        settings.setValue("playback/volumeFade", false);
        ApiClient api;
        PlaybackService playback(api, settings);
        playback.enginePreferences()->setPlaybackStallMaxAttempts(1);
        PlaybackTrack track;
        track.id = "recovery-local";
        track.localPath = path;
        track.title = "Recovery";
        playback.playTracks({track}, 0, "Local", "recovery-local");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 15000);
        QVERIFY(PlaybackServiceTestAccess::powerActive(playback));
        playback.preferences()->setPreventSleep(false);
        QVERIFY(!PlaybackServiceTestAccess::powerActive(playback));
        playback.preferences()->setPreventSleep(true);
        QVERIFY(PlaybackServiceTestAccess::powerActive(playback));
        playback.seek(4000);
        QTRY_VERIFY_WITH_TIMEOUT(playback.positionMs() >= 3900, 5000);
        auto sequence = PlaybackServiceTestAccess::audioSequence(playback);
        PlaybackServiceTestAccess::stalled(playback);
        QVERIFY(playback.loading());
        // The reload cannot replace the saved point with its initial zero event.
        QVERIFY(playback.positionMs() >= 3900);
        QTRY_VERIFY_WITH_TIMEOUT(
            playback.playing() && !playback.loading() && playback.positionMs() >= 3900, 15000);
        QVERIFY(PlaybackServiceTestAccess::audioSequence(playback) > sequence);
        sequence = PlaybackServiceTestAccess::audioSequence(playback);
        PlaybackServiceTestAccess::stalled(playback);
        QTRY_VERIFY(!playback.playing());
        QVERIFY(!PlaybackServiceTestAccess::powerActive(playback));
        QVERIFY(!playback.errorMessage().isEmpty());
        QCOMPARE(PlaybackServiceTestAccess::audioSequence(playback), sequence);
        PlaybackServiceTestAccess::stalled(playback);
        QVERIFY(!playback.loading());
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
    void actualDecoderErrorsHonorDelayLimitAndUserCancellation()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("preferences.ini"), QSettings::IniFormat);
        settings.setValue("playback/volume", 0);
        ApiClient api;
        PlaybackService playback(api, settings);
        auto *preferences = playback.preferences();
        QVERIFY(!preferences->autoNext());
        QCOMPARE(preferences->autoNextDelaySeconds(), 3);
        QCOMPARE(preferences->autoNextMaxAttempts(), 10);
        preferences->setAutoNext(true);
        preferences->setAutoNextDelaySeconds(1);
        preferences->setAutoNextMaxAttempts(2);
        PlaybackTracks tracks;
        for (int i = 0; i < 3; ++i) {
            PlaybackTrack track;
            track.id = QString::number(i);
            track.localPath = dir.filePath(QString("missing-%1.wav").arg(i));
            track.title = QString::number(i);
            tracks.append(track);
        }
        playback.setPlayMode(PlaybackService::SingleLoop);
        playback.playTracks(tracks, 0, "Missing PCM", "missing-local");
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading() && !playback.errorMessage().isEmpty(), 15000);
        QCOMPARE(playback.queue()->currentIndex(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(playback.queue()->currentIndex(), 2, 5000);
        QTRY_VERIFY2(!playback.loading() && !playback.errorMessage().isEmpty(),
                     qPrintable(QString("loading=%1 playing=%2 index=%3 error=%4")
                                    .arg(playback.loading())
                                    .arg(playback.playing())
                                    .arg(playback.queue()->currentIndex())
                                    .arg(playback.errorMessage())));
        QTest::qWait(1100);
        // Failure traversal advances in single-loop mode but cannot loop forever.
        QCOMPARE(playback.queue()->currentIndex(), 2);

        preferences->setAutoNextDelaySeconds(1);
        playback.playIndex(0); // Explicit intent starts a new failure chain.
        QTRY_VERIFY2(!playback.loading() && !playback.errorMessage().isEmpty(),
                     qPrintable(QString("loading=%1 playing=%2 index=%3 error=%4")
                                    .arg(playback.loading())
                                    .arg(playback.playing())
                                    .arg(playback.queue()->currentIndex())
                                    .arg(playback.errorMessage())));
        playback.pause();
        QTest::qWait(1100);
        QCOMPARE(playback.queue()->currentIndex(), 0);
        QVERIFY(!playback.playing());

        playback.playIndex(0);
        QTRY_VERIFY2(!playback.loading() && !playback.errorMessage().isEmpty(),
                     qPrintable(QString("loading=%1 playing=%2 index=%3 error=%4")
                                    .arg(playback.loading())
                                    .arg(playback.playing())
                                    .arg(playback.queue()->currentIndex())
                                    .arg(playback.errorMessage())));
        preferences->setAutoNext(false); // Cancels an already pending deadline.
        QTest::qWait(1100);
        QCOMPARE(playback.queue()->currentIndex(), 0);
        settings.sync();
        QSettings reopened(dir.filePath("preferences.ini"), QSettings::IniFormat);
        PlaybackPreferences persisted(reopened);
        QCOMPARE(persisted.autoNextDelaySeconds(), 1);
        QCOMPARE(persisted.autoNextMaxAttempts(), 2);
        QVERIFY(!persisted.autoNext());
        preferences->setAutoNext(true);
        playback.playIndex(0);
        QTRY_VERIFY2(!playback.loading() && !playback.errorMessage().isEmpty(),
                     qPrintable(QString("loading=%1 playing=%2 index=%3 error=%4")
                                    .arg(playback.loading())
                                    .arg(playback.playing())
                                    .arg(playback.queue()->currentIndex())
                                    .arg(playback.errorMessage())));
        api.advanceSession();
        const auto indexAfterSession = playback.queue()->currentIndex();
        QTest::qWait(1100);
        QCOMPARE(playback.queue()->currentIndex(), indexAfterSession);
        QVERIFY(!playback.playing());
        QSignalSpy closed(&playback, &PlaybackService::closed), apiClosed(&api, &ApiClient::closed);
        playback.shutdown();
        api.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
    }
};
int main(int argc, char **argv)
{
    initializeNetworkEnvironment();
    QGuiApplication application(argc, argv);
    PlayerEngineSettingsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "PlayerEngineSettingsTest.moc"
