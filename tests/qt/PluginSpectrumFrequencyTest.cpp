#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "plugins/PluginRuntimeTestFixture.h"
#include <algorithm>

namespace
{
PlaybackTrack tone(Host &host, const QString &id, int frequency, int afterSeekFrequency = 0)
{
    constexpr int rate = 48000, seconds = 20;
    QByteArray pcm;
    QDataStream data(&pcm, QIODevice::WriteOnly);
    data.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < rate * seconds; ++i) {
        const int hz =
            afterSeekFrequency && i >= rate * seconds / 2 ? afterSeekFrequency : frequency;
        data << qint16(std::sin(i * 2.0 * 3.141592653589793 * hz / rate) * 10000);
    }
    QByteArray wave;
    QDataStream header(&wave, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header.writeRawData("RIFF", 4);
    header << quint32(36 + pcm.size());
    header.writeRawData("WAVEfmt ", 8);
    header << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2)
           << quint16(2) << quint16(16);
    header.writeRawData("data", 4);
    header << quint32(pcm.size());
    wave.append(pcm);
    const auto path = host.temporary.path() + '/' + id + ".wav";
    write(path, wave);
    PlaybackTrack track;
    track.id = id;
    track.title = id;
    track.localPath = path;
    track.durationMs = seconds * 1000;
    return track;
}

double strongestFrequency(const QVariantMap &frame)
{
    const auto values = frame.value("frequencyData").toList();
    if (values.size() != 1024)
        return -1;
    const auto maximum = std::max_element(values.cbegin(), values.cend(),
                                          [](auto a, auto b) { return a.toInt() < b.toInt(); });
    return std::distance(values.cbegin(), maximum) *
           frame.value("frequencyDataInfo").toMap().value("binWidthHz").toDouble();
}
} // namespace

class PluginSpectrumFrequencyTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        configureUiTypography();
    }
    void explicitOptInPreservesLegacyDefaults();
    void realPcmFixedProfileAndScopeLifetime();
};

void PluginSpectrumFrequencyTest::explicitOptInPreservesLegacyDefaults()
{
    QVERIFY(!PlayerSpectrum::normalize({}).value("includeFrequencyData").toBool());
    QCOMPARE(PlayerSpectrum::normalize(
                 {{"includeFrequencyData", true}, {"fftSize", 256}, {"smoothing", .95}})
                 .value("includeFrequencyData")
                 .toBool(),
             true);
    QCOMPARE(PlayerSpectrum::normalize({}).value("fftSize").toInt(), 2048);
    QCOMPARE(PlayerSpectrum::normalize({}).value("smoothing").toDouble(), .65);
}

void PluginSpectrumFrequencyTest::realPcmFixedProfileAndScopeLifetime()
{
    Host host;
    host.runtime.importDirectory(package(host.temporary.path(), "export function activate(ctx) {}",
                                         {"lifecycle", "audio.spectrum"}, {"audioSpectrum"}));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    auto *scope = host.runtime.context(host.installation());
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
    const auto originalVolume = host.playback.volume();
    auto restore = qScopeGuard([&] { host.playback.setVolume(originalVolume); });
    host.playback.setVolume(8);
    QPointer<EchoPluginSpectrumSubscription> legacy = scope->audio()->spectrum()->subscribe(
        {{"fps", 60}, {"fftSize", 8192}, {"smoothing", 0.0}, {"includeWaveform", true}});
    QPointer<EchoPluginSpectrumSubscription> frequency = scope->audio()->spectrum()->subscribe(
        {{"fps", 30}, {"fftSize", 256}, {"smoothing", .95}, {"includeFrequencyData", true}});
    QVERIFY(legacy && frequency && legacy->active() && frequency->active());
    QSignalSpy frequencyFrames(frequency, &EchoPluginSpectrumSubscription::frameChanged);
    host.playback.playTracks({tone(host, "one-khz", 1000, 3000)}, 0, "frequency test",
                             "frequency-test");
    QTRY_VERIFY2_WITH_TIMEOUT(host.playback.playing() && !host.playback.loading(),
                              qPrintable(host.playback.errorMessage()), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(strongestFrequency(frequency->frame().toMap()) - 1000) < 50,
                             10000);
    const auto frame = frequency->frame().toMap();
    const auto info = frame.value("frequencyDataInfo").toMap();
    QCOMPARE(info.value("fftSize").toInt(), 2048);
    QCOMPARE(info.value("frequencyBinCount").toInt(), 1024);
    QCOMPARE(info.value("sampleRate").toInt(), frame.value("sampleRate").toInt());
    QCOMPARE(info.value("binWidthHz").toDouble(), frame.value("sampleRate").toDouble() / 2048);
    QCOMPARE(info.value("window").toString(), "blackman");
    QCOMPARE(info.value("fftNormalization").toString(), "1/N");
    QCOMPARE(info.value("smoothingTimeConstant").toDouble(), .6);
    QCOMPARE(info.value("minDecibels").toDouble(), -100.);
    QCOMPARE(info.value("maxDecibels").toDouble(), -30.);
    QCOMPARE(info.value("channelInterpretation").toString(), "speakers");
    QCOMPARE(info.value("framesPerSecond").toInt(), 30);
    for (const auto &value : frame.value("frequencyData").toList()) {
        QVERIFY(value.toDouble() >= 0 && value.toDouble() <= 255);
        QCOMPARE(value.toDouble(), double(value.toInt()));
    }
    QTRY_COMPARE(legacy->frame().toMap().value("waveform").toList().size(), 8192);
    const auto legacyFrame = legacy->frame().toMap();
    QCOMPARE(legacyFrame.value("fftSize").toInt(), 8192);
    QCOMPARE(legacyFrame.value("waveform").toList().size(), 8192);
    QVERIFY(!legacyFrame.contains("frequencyData"));
    QVERIFY(!legacyFrame.contains("frequencyDataInfo"));
    // Removing the 8192/.0 subscriber leaves the canonical profile unchanged.
    legacy->dispose();
    QTRY_VERIFY(legacy.isNull());
    QTRY_COMPARE(frequency->frame().toMap().value("fftSize").toInt(), 256);
    QCOMPARE(frequency->frame().toMap().value("frequencyDataInfo").toMap().value("fftSize").toInt(),
             2048);
    const auto sameSource = host.playback.sourceRevision();
    host.playback.seek(12000);
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.positionMs() >= 11900, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(strongestFrequency(frequency->frame().toMap()) - 3000) < 50,
                             10000);
    QCOMPARE(host.playback.sourceRevision(), sameSource);
    frequency->setPaused(true);
    QTRY_VERIFY(!host.playback.spectrum()->status().value("running").toBool());
    frequencyFrames.clear();
    QTest::qWait(150);
    QCOMPARE(frequencyFrames.count(), 0);
    frequency->setPaused(false);
    QTRY_VERIFY(frequencyFrames.count() > 0);
    host.playback.pause();
    QTRY_COMPARE(frequency->frame().toMap().value("source").toString(), "silence");
    QVERIFY(frequency->frame().toMap().value("frequencyData").toList().isEmpty());
    host.playback.playTracks({tone(host, "two-khz", 2000)}, 0, "frequency test", "frequency-test");
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(strongestFrequency(frequency->frame().toMap()) - 2000) < 50,
                             10000);
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(frequency.isNull());
    QCOMPARE(host.playback.spectrum()->subscriberCount(), 0);
    QVERIFY(!host.playback.spectrum()->status().value("running").toBool());
    QSignalSpy retiredFrames(host.playback.spectrum(), &PlayerSpectrum::frameReady);
    QTest::qWait(150);
    QCOMPARE(retiredFrames.count(), 0);
}

QTEST_MAIN(PluginSpectrumFrequencyTest)
#include "PluginSpectrumFrequencyTest.moc"
