// SPDX-License-Identifier: GPL-3.0-only
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "plugins/PluginRuntimeTestFixture.h"
#include "AudioEngine.h"
#include "NativePcmReader.h"
#include "EchoPluginPlaybackTransition.h"
#include <QtConcurrent/QtConcurrentRun>

namespace
{
struct SourceTrace
{
    QObject listener;
    QElapsedTimer clock;
    QStringList rows;
    explicit SourceTrace(Host &host)
    {
        clock.start();
        const auto remember = [this](const QString &text) {
            if (rows.size() >= 128)
                rows.removeFirst();
            rows.append(QString::number(clock.elapsed()) + "ms " + text);
        };
        QObject::connect(
            &host.playback.analysisEngine(), &AudioEngine::eventReceived, &listener,
            [remember](QJsonObject event) {
                event.remove("url");
                event.remove("source");
                event.remove("locator");
                const auto kind = event.value("event").toString();
                if (kind == "time-update" || kind == "spectrum" || kind.endsWith("stats"))
                    return;
                remember("native " +
                         QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact))
                             .left(2048));
            });
        QObject::connect(&host.playback, &PlaybackService::changed, &listener, [remember, &host] {
            remember(QString("host ready=%1 loading=%2 playing=%3 facts=%4 rev=%5 err=%6")
                         .arg(host.playback.engineReady())
                         .arg(host.playback.loading())
                         .arg(host.playback.playing())
                         .arg(!host.playback.analysisSourceFacts().isEmpty())
                         .arg(host.playback.sourceRevision())
                         .arg(host.playback.errorMessage()));
        });
    }
    QString details() const
    {
        return rows.join('\n');
    }
};
PlaybackTrack fixture(Host &host, const QString &id, int seconds = 4)
{
    constexpr int rate = 44100;
    const int frames = rate * seconds;
    QByteArray pcm;
    QDataStream data(&pcm, QIODevice::WriteOnly);
    data.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < frames; ++i)
        data << qint16(std::sin(i * .06) * (.2 + .8 * std::max(0., 1. - (i % 22050) / 1200.)) *
                       6000);
    QByteArray wav;
    QDataStream header(&wav, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header.writeRawData("RIFF", 4);
    header << quint32(36 + pcm.size());
    header.writeRawData("WAVEfmt ", 8);
    header << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2)
           << quint16(2) << quint16(16);
    header.writeRawData("data", 4);
    header << quint32(pcm.size());
    wav += pcm;
    const auto path = host.temporary.filePath(id + ".wav");
    write(path, wav);
    PlaybackTrack track;
    track.id = id;
    track.title = id;
    track.localPath = path;
    track.durationMs = seconds * 1000;
    return track;
}
} // namespace
class PluginAudioAnalysisTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        configureUiTypography();
    }
    void realAdoptedWholeTrackAndPublicGpuLease()
    {
        Host host;
        SourceTrace trace(host);
        host.runtime.importDirectory(package(host.temporary.path(),
                                             "export function activate(ctx) {}",
                                             {"lifecycle", "audio.analysis"}, {"audioAnalysis"}));
        QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
        auto *scope = host.runtime.context(host.installation());
        auto *analysis = scope->audio()->analysis();
        QVERIFY(analysis);
        QVERIFY(analysis->status().value("helperInstalled").toBool());
        QVERIFY(analysis->status().value("beatsModelInstalled").toBool());
        QVERIFY(!analysis->status().value("gpuVerified").toBool());
        auto *invalid = analysis->request(
            {{"kind", "beats"}, {"policy", "gpuOnly"}, {"locator", "/private/file"}});
        QTRY_VERIFY(invalid->finished());
        QCOMPARE(invalid->errorCode(), "InvalidArgument");
        QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
        const auto before = host.playback.volume();
        auto restore = qScopeGuard([&] { host.playback.setVolume(before); });
        host.playback.setVolume(0);
        host.playback.playTracks({fixture(host, "actual-adopted-source")}, 0, "analysis",
                                 "analysis-test");
        QTRY_VERIFY2_WITH_TIMEOUT(!host.playback.analysisSourceFacts().isEmpty(),
                                  qPrintable(trace.details()), 15000);
        host.playback.pause();
        const auto facts = host.playback.analysisSourceFacts();
        auto native =
            QtConcurrent::run([facts] { return AudioEngine::captureAnalysisSource(facts); });
        QTRY_VERIFY(native.isFinished());
        const auto captured = native.result();
        QVERIFY2(captured.source, qPrintable(captured.error));
        QVERIFY(!captured.source->info().contains("locator"));
        const auto reader = captured.source->reader(AudioAnalysis::Kind::Beats);
        auto decoded = QtConcurrent::run(
            [reader] { return reader(std::make_shared<std::atomic_bool>(false)); });
        QTRY_VERIFY_WITH_TIMEOUT(decoded.isFinished(), 10000);
        const auto pcm = decoded.result();
        QVERIFY2(pcm.error.isEmpty(), qPrintable(pcm.error));
        QVERIFY(pcm.wholeTrack);
        QCOMPARE(pcm.sampleRate, 22050);
        QCOMPARE(pcm.left.size(), 22050 * 4 * 4);
        QVERIFY(pcm.right.isEmpty());
        auto *cached = analysis->request({{"kind", "beats"}, {"policy", "storedOnly"}});
        QTRY_VERIFY_WITH_TIMEOUT(cached->finished(), 10000);
        QVERIFY2(cached->errorCode() == "CacheMiss",
                 qPrintable(cached->errorCode() + ": " + cached->errorMessage() + " current=" +
                            QString::fromUtf8(QJsonDocument(host.playback.analysisSourceFacts())
                                                  .toJson(QJsonDocument::Compact)) +
                            "\n" + trace.details()));
        QVERIFY(cached->result().isNull());
        QCOMPARE(cached->sourceSequence(), "");
        QCOMPARE(cached->transitionEpoch(), "");
        auto *undeclared = analysis->request(
            {{"kind", "beats"},
             {"policy", "storedOnly"},
             {"source", QVariantMap{{"sequence", "1"}, {"transitionEpoch", "1"}}}});
        QTRY_VERIFY(undeclared->finished());
        QCOMPARE(undeclared->errorCode(), "FeatureRequired");
        QPointer<EchoPluginAnalysisLease> gpu =
            analysis->request({{"kind", "beats"}, {"policy", "gpuOnly"}});
        QTRY_VERIFY_WITH_TIMEOUT(gpu->finished(), 60000);
        QVERIFY2(gpu->errorCode().isEmpty(), qPrintable(gpu->errorMessage()));
        const auto result = gpu->result().toMap();
        QCOMPARE(result.value("source").toString(), "beat_this");
        QCOMPARE(result.value("backend").toString(), "gpu");
        QVERIFY(result.value("complete").toBool());
        QCOMPARE(result.value("durationSeconds").toDouble(), 4.);
        const auto serialized = QJsonDocument(QJsonObject::fromVariantMap(result)).toJson();
        QVERIFY(!serialized.contains(facts.value("locator").toString().toUtf8()));
        auto *stored = analysis->request({{"kind", "beats"}, {"policy", "storedOnly"}});
        QTRY_VERIFY_WITH_TIMEOUT(stored->finished(), 10000);
        QCOMPARE(stored->result(), gpu->result());
        // A completed model result is a lease: a source change invalidates it immediately.
        host.playback.playTracks({fixture(host, "new-adopted-source")}, 0, "analysis",
                                 "analysis-test");
        QTRY_VERIFY(!gpu || !gpu->active());
        QVERIFY(host.runtime.setEnabled(host.installation(), false));
        QTRY_COMPARE_WITH_TIMEOUT(host.runtime.state(host.installation()), PluginRuntime::Disabled,
                                  15000);
    }
    void realOutgoingEpochSupportsTailFirstRequestAndIndependentLeases()
    {
        Host host;
        SourceTrace trace(host);
        host.runtime.importDirectory(
            package(host.temporary.path(), "export function activate(ctx) {}",
                    {"lifecycle", "audio.analysis", "audio.analysis.transitionSources"},
                    {"audioAnalysis"}));
        QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
        auto *scope = host.runtime.context(host.installation());
        auto *analysis = scope->audio()->analysis();
        auto *projection = scope->player()->transition();
        QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
        const auto before = host.playback.volume();
        auto restore = qScopeGuard([&] {
            host.playback.stop();
            host.playback.setVolume(before);
        });
        host.playback.setVolume(0);
        const auto a = fixture(host, "outgoing-analysis-A", 40);
        const auto b = fixture(host, "incoming-analysis-B", 40);
        host.playback.transition()->setMode("fade");
        host.playback.transition()->setFadeSeconds(4);
        host.playback.playTracks({a, b}, 0, "analysis handoff", "analysis-handoff-test");
        QTRY_VERIFY2_WITH_TIMEOUT(!host.playback.analysisSourceFacts().isEmpty(),
                                  qPrintable(trace.details()), 15000);
        QVERIFY(!projection->valid());
        const auto aSequence =
            host.playback.analysisSourceFacts().value("trackSequence").toString();
        QPointer<EchoPluginAnalysisLease> beforeAdopt =
            analysis->request({{"kind", "beats"}, {"policy", "storedOnly"}});
        QCOMPARE(beforeAdopt->sourceSequence(), aSequence);
        QCOMPARE(beforeAdopt->transitionEpoch(), "");
        QSignalSpy identities(beforeAdopt, &EchoPluginAnalysisLease::sourceIdentityChanged);
        QSignalSpy payloads(beforeAdopt, &EchoPluginAnalysisLease::changed);
        QTRY_VERIFY_WITH_TIMEOUT(beforeAdopt->finished(), 10000);
        QCOMPARE(beforeAdopt->errorCode(), "CacheMiss");
        const auto payloadNotifications = payloads.count();
        host.playback.seek(30000);
        QTRY_VERIFY2_WITH_TIMEOUT(projection->valid(), qPrintable(trace.details()), 15000);
        const QVariantMap selector{{"sequence", projection->outgoing()->sequence()},
                                   {"transitionEpoch", projection->epoch()}};
        const QJsonObject sourceSelector = QJsonObject::fromVariantMap(selector);
        const auto outgoingFacts = host.playback.analysisSourceFacts(sourceSelector);
        QCOMPARE(outgoingFacts.value("locator").toString(), a.localPath);
        QCOMPARE(beforeAdopt->sourceSequence(), aSequence);
        QCOMPARE(beforeAdopt->transitionEpoch(), projection->epoch());
        QCOMPARE(identities.count(), 1);
        QCOMPARE(payloads.count(), payloadNotifications);
        QTRY_VERIFY2_WITH_TIMEOUT(projection->handedOver(), qPrintable(trace.details()), 15000);
        host.playback.pause();
        QTRY_VERIFY(!host.playback.playing());
        QVERIFY(beforeAdopt && beforeAdopt->active());
        QCOMPARE(beforeAdopt->sourceSequence(), projection->outgoing()->sequence());
        QCOMPARE(beforeAdopt->transitionEpoch(), projection->epoch());
        QCOMPARE(host.playback.analysisSourceFacts().value("locator").toString(), b.localPath);
        QCOMPARE(host.playback.analysisSourceFacts(sourceSelector), outgoingFacts);
        // This is deliberately the first public explicit-A request, after B was adopted.
        QPointer<EchoPluginAnalysisLease> tail =
            analysis->request({{"kind", "beats"}, {"policy", "storedOnly"}, {"source", selector}});
        QTRY_VERIFY_WITH_TIMEOUT(tail->finished(), 10000);
        QCOMPARE(tail->errorCode(), "CacheMiss");
        QPointer<EchoPluginAnalysisLease> incoming =
            analysis->request({{"kind", "beats"}, {"policy", "storedOnly"}});
        QTRY_VERIFY_WITH_TIMEOUT(incoming->finished(), 10000);
        QCOMPARE(incoming->errorCode(), "CacheMiss");
        QCOMPARE(incoming->sourceSequence(), projection->incoming()->sequence());
        QCOMPARE(incoming->transitionEpoch(), "");
        QVERIFY(beforeAdopt && beforeAdopt->active());
        QVERIFY(tail && tail->active());
        const auto captured = QtConcurrent::run(
            [outgoingFacts] { return AudioEngine::captureAnalysisSource(outgoingFacts); });
        QTRY_VERIFY_WITH_TIMEOUT(captured.isFinished(), 10000);
        QVERIFY2(captured.result().source, qPrintable(captured.result().error));
        const auto reader = captured.result().source->reader(AudioAnalysis::Kind::Beats);
        const auto decoded = QtConcurrent::run(
            [reader] { return reader(std::make_shared<std::atomic_bool>(false)); });
        QTRY_VERIFY_WITH_TIMEOUT(decoded.isFinished(), 10000);
        QVERIFY2(decoded.result().error.isEmpty(), qPrintable(decoded.result().error));
        QCOMPARE(decoded.result().left.size(), 22050 * 40 * 4);
        QVERIFY(decoded.result().wholeTrack);
        auto forged = selector;
        forged.insert("transitionEpoch", QString::number(projection->epoch().toULongLong() + 1));
        auto *wrong =
            analysis->request({{"kind", "beats"}, {"policy", "storedOnly"}, {"source", forged}});
        QTRY_VERIFY(wrong->finished());
        QCOMPARE(wrong->errorCode(), "Unavailable");
        auto *path = analysis->request(
            {{"kind", "beats"},
             {"policy", "storedOnly"},
             {"source", QVariantMap{{"sequence", selector.value("sequence")},
                                    {"transitionEpoch", selector.value("transitionEpoch")},
                                    {"locator", a.localPath}}}});
        QTRY_VERIFY(path->finished());
        QCOMPARE(path->errorCode(), "InvalidArgument");
        host.playback.seek(4000);
        QVERIFY(!projection->valid());
        QVERIFY(!beforeAdopt || !beforeAdopt->active());
        QVERIFY(!tail || !tail->active());
        if (beforeAdopt) {
            QCOMPARE(beforeAdopt->sourceSequence(), "");
            QCOMPARE(beforeAdopt->transitionEpoch(), "");
        }
        QVERIFY(host.playback.analysisSourceFacts(sourceSelector).isEmpty());
        auto *expired =
            analysis->request({{"kind", "beats"}, {"policy", "storedOnly"}, {"source", selector}});
        QTRY_VERIFY(expired->finished());
        QCOMPARE(expired->errorCode(), "Unavailable");
        scope->beginRetire();
        QTRY_VERIFY_WITH_TIMEOUT(scope->drained(), 15000);
    }
    void sourceRetirementHasNoLateSuccessfulDelivery()
    {
        Host host;
        SourceTrace trace(host);
        host.runtime.importDirectory(package(host.temporary.path(),
                                             "export function activate(ctx) {}",
                                             {"lifecycle", "audio.analysis"}, {"audioAnalysis"}));
        QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
        auto *scope = host.runtime.context(host.installation());
        QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
        const auto before = host.playback.volume();
        auto restore = qScopeGuard([&] { host.playback.setVolume(before); });
        host.playback.setVolume(0);
        host.playback.playTracks({fixture(host, "retired-source")}, 0, "analysis", "analysis-test");
        QTRY_VERIFY2_WITH_TIMEOUT(!host.playback.analysisSourceFacts().isEmpty(),
                                  qPrintable(trace.details()), 15000);
        QPointer<EchoPluginAnalysisLease> lease =
            scope->audio()->analysis()->request({{"kind", "beats"}, {"policy", "gpuOnly"}});
        int successes = 0;
        connect(lease, &EchoPluginAnalysisLease::completed, this, [lease, &successes] {
            if (lease->state() == "succeeded")
                ++successes;
        });
        QVERIFY(scope->active());
        QVERIFY(lease && lease->active());
        scope->beginRetire();
        // Retirement is synchronous; release/deletion may finish in later event turns.
        QVERIFY(!scope->active());
        QVERIFY(!lease || !lease->active());
        QTRY_VERIFY_WITH_TIMEOUT(scope->drained(), 15000);
        QCOMPARE(successes, 0);
        QVERIFY(!lease || !lease->active());
        QVERIFY(!lease || lease->result().isNull());
        QTRY_VERIFY(lease.isNull());
    }
};
QTEST_MAIN(PluginAudioAnalysisTest)
#include "PluginAudioAnalysisTest.moc"
