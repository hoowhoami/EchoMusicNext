#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "EchoPluginPlaybackTransition.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include "../../services/settings/SettingsKeys.h"
#include <QCryptographicHash>
#include <cmath>

namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static AudioEngine &engine(PlaybackService &player)
    {
        return player.m_engine;
    }
};
} // namespace EchoMusic

namespace
{
bool awaitEvent(const std::function<bool()> &ready, int milliseconds)
{
    if (ready())
        return true;
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(5);
    poll.setTimerType(Qt::PreciseTimer);
    deadline.setSingleShot(true);
    bool result = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if ((result = ready()))
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(milliseconds);
    loop.exec();
    return result;
}
void waitNaturally(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
QByteArray synchsafe(quint32 value)
{
    QByteArray result;
    for (const auto shift : {21, 14, 7, 0})
        result.append(char((value >> shift) & 127));
    return result;
}
QByteArray wave(const QByteArray &lyrics, double frequency)
{
    // Native Fade deliberately refuses tracks shorter than 30 seconds.
    constexpr int rate = 16000, seconds = 40;
    QByteArray samples;
    QDataStream pcm(&samples, QIODevice::WriteOnly);
    pcm.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < rate * seconds; ++i)
        pcm << qint16(std::sin(i * frequency * 2 * 3.141592653589793 / rate) * 1200);
    const QByteArray lyricPayload = QByteArray(1, char(3)) + "eng" + char(0) + lyrics;
    const QByteArray frames =
        "USLT" + synchsafe(lyricPayload.size()) + QByteArray(2, char(0)) + lyricPayload;
    const QByteArray tag = QByteArray("ID3\x04\x00\x00", 6) + synchsafe(frames.size()) + frames;
    QByteArray body("WAVE");
    QDataStream stream(&body, QIODevice::Append);
    stream.setByteOrder(QDataStream::LittleEndian);
    const auto chunk = [&](const char *name, const QByteArray &bytes) {
        stream.writeRawData(name, 4);
        stream << quint32(bytes.size());
        stream.writeRawData(bytes.data(), bytes.size());
        if (bytes.size() % 2)
            stream << quint8(0);
    };
    QByteArray format;
    QDataStream fmt(&format, QIODevice::WriteOnly);
    fmt.setByteOrder(QDataStream::LittleEndian);
    fmt << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2)
        << quint16(16);
    chunk("fmt ", format);
    chunk("id3 ", tag);
    chunk("data", samples);
    QByteArray result("RIFF");
    QDataStream header(&result, QIODevice::Append);
    header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(body.size());
    result += body;
    return result;
}
PlaybackTrack installTrack(Host &host, const QString &name, double frequency)
{
    PlaybackTrack track;
    track.id = name;
    track.title = name;
    track.localPath = host.temporary.filePath(name + ".wav");
    track.durationMs = 40000;
    write(
        track.localPath,
        wave(("[00:00.00]" + name + " first\n[00:20.00]" + name + " second").toUtf8(), frequency));
    const auto key = SettingsKeys::Lyrics::SourcesPrefix +
                     QString::fromLatin1(QCryptographicHash::hash(track.stableKey().toUtf8(),
                                                                  QCryptographicHash::Sha256)
                                             .toHex());
    host.settings.setValue(key, "embedded");
    return track;
}
QString state(const PlaybackService &player)
{
    return QString("playing=%1 loading=%2 source=%3 position=%4 engine=%5 error=%6")
        .arg(player.playing())
        .arg(player.loading())
        .arg(player.sourceKind())
        .arg(player.positionMs())
        .arg(player.engineInitializationError(), player.errorMessage());
}
} // namespace
class PlaybackTransitionPresentationTest final : public QObject
{
    Q_OBJECT
private slots:
    void actualQueueRetainsAUntilTheNativeCueAndClearsOnSeekAndStop()
    {
        Host host;
        const auto originalVolume = host.playback.volume();
        host.playback.setVolume(0);
        const auto restoreVolume = qScopeGuard([&] {
            host.playback.stop();
            host.playback.setVolume(originalVolume);
        });
        host.runtime.importDirectory(
            package(host.temporary.path(), "export function activate(ctx) {}\n"));
        QVERIFY(awaitEvent(
            [&] { return !host.runtime.installationForId("test.runtime").isEmpty(); }, 7000));
        const auto installation = host.runtime.installationForId("test.runtime");
        QVERIFY(awaitEvent(
            [&] { return host.runtime.state(installation) == PluginRuntime::Active; }, 7000));
        auto *scope = host.runtime.context(installation);
        auto *projection = scope->player()->transition();
        QJsonArray nativeEvents;
        connect(&PlaybackServiceTestAccess::engine(host.playback), &AudioEngine::eventReceived,
                this, [&](const QJsonObject &event) {
                    const auto kind = event.value("event").toString();
                    if (nativeEvents.size() < 160 &&
                        (kind.startsWith("next-source-") || kind == "file-loaded" ||
                         kind == "seeked" || event.value("transition_playback").isObject()))
                        nativeEvents.append(QJsonObject{
                            {"event", event},
                            {"currentTitle", host.playback.title()},
                            {"valid", projection->valid()},
                            {"reason", projection->reason()},
                            {"preparedIncoming",
                             host.playback.transitionPresentation().incoming().sequence},
                            {"presentation", host.playback.transitionPresentation().snapshot()}});
                });
        QVERIFY(projection);
        QVERIFY(!projection->valid());
        QCOMPARE(projection->incoming()->lyrics()->rowCount(), 0);
        QCOMPARE(projection->incoming()->metaObject()->indexOfMethod("seek(qint64)"), -1);
        const auto a = installTrack(host, "outgoing-A", 220),
                   b = installTrack(host, "incoming-B", 330);
        host.playback.transition()->setMode("fade");
        host.playback.transition()->setFadeSeconds(2);
        host.playback.playTracks({a, b}, 0, "Real handoff", "real-handoff");
        QVERIFY2(awaitEvent(
                     [&] {
                         return host.playback.playing() && !host.playback.loading() &&
                                host.playback.lyrics()->rowCount() == 2;
                     },
                     12000),
                 qPrintable(state(host.playback)));
        QCOMPARE(host.playback.currentTrack().stableKey(), a.stableKey());
        host.playback.seek(34000);
        const auto projected = awaitEvent([&] { return projection->valid(); }, 10000);
        if (!projected)
            qWarning().noquote() << QJsonDocument(nativeEvents).toJson(QJsonDocument::Compact);
        QVERIFY2(projected, qPrintable(state(host.playback)));
        const auto epoch = projection->epoch();
        QVERIFY(!epoch.isEmpty());
        QCOMPARE(projection->outgoing()->title(), a.title);
        QCOMPARE(projection->incoming()->title(), b.title);
        QCOMPARE(projection->outgoing()->id(), a.id);
        QCOMPARE(projection->incoming()->id(), b.id);
        QCOMPARE(projection->outgoing()->hash(), a.hash);
        QCOMPARE(projection->incoming()->songId(), QString::number(b.songId));
        QVERIFY(!projection->outgoing()->sequence().isEmpty());
        QVERIFY(!projection->incoming()->sequence().isEmpty());
        QVERIFY(projection->outgoing()->sequence() != projection->incoming()->sequence());
        QCOMPARE(projection->outgoing()->lyrics()->rowCount(), 2);
        QVERIFY(projection->cueReady());
        QCOMPARE(projection->cueReason(), QString("scheduled-gain-envelope"));
        QVERIFY(projection->crossover() > 0 && projection->crossover() < 1);
        QVERIFY(projection->cueDurationMs() > 0);
        QVERIFY2(awaitEvent(
                     [&] {
                         return projection->handedOver() && projection->cueElapsedMs() > 100 &&
                                projection->cueElapsedMs() < projection->cueDurationMs() * .8;
                     },
                     7000),
                 qPrintable(state(host.playback)));
        QCOMPARE(host.playback.currentTrack().stableKey(), b.stableKey());
        QVERIFY(projection->hasOutgoingPosition());
        QVERIFY(projection->hasIncomingPosition());
        QVERIFY(projection->outgoingPositionMs() > 36000);
        QVERIFY(projection->incomingPositionMs() >= 0);
        host.playback.pause();
        QVERIFY(awaitEvent([&] { return !host.playback.playing(); }, 3000));
        waitNaturally(150);
        const auto frozen = projection->cueElapsedMs();
        waitNaturally(350);
        QVERIFY(qAbs(projection->cueElapsedMs() - frozen) < 1);
        QCOMPARE(projection->epoch(), epoch);
        QVERIFY2(
            awaitEvent([&] { return projection->incoming()->lyrics()->rowCount() == 2; }, 3000),
            qPrintable(state(host.playback)));
        QVERIFY(projection->incoming()
                    ->lyrics()
                    ->row(0)
                    .value("lineText")
                    .toString()
                    .contains("incoming-B"));
        QVERIFY(projection->outgoing()
                    ->lyrics()
                    ->row(0)
                    .value("lineText")
                    .toString()
                    .contains("outgoing-A"));
        QCOMPARE(host.playback.currentTrack().stableKey(), b.stableKey());
        host.playback.seek(4000);
        QVERIFY(!projection->valid());
        QVERIFY(projection->outgoing()->id().isEmpty());
        QVERIFY(projection->incoming()->hash().isEmpty());
        QVERIFY(projection->incoming()->songId().isEmpty());
        QCOMPARE(projection->outgoing()->lyrics()->rowCount(), 0);
        QCOMPARE(projection->incoming()->lyrics()->rowCount(), 0);
        host.playback.stop();
        waitNaturally(200);
        QVERIFY(!projection->valid());
    }
};
QTEST_MAIN(PlaybackTransitionPresentationTest)
#include "PlaybackTransitionPresentationTest.moc"
