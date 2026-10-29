// SPDX-License-Identifier: GPL-3.0-only
#include "AnalysisBroker.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <cstring>
using namespace EchoMusic::AudioAnalysis;
class BrokerTest final : public QObject
{
    Q_OBJECT
    static Source source(quint64 revision = 1)
    {
        return {"account-a", "song-a", "representation-a", revision, 1};
    }
    static Config config(const QString &root)
    {
        return {QStringLiteral(ANALYSIS_HELPER), QStringLiteral(ANALYSIS_MODEL_DIRECTORY),
                QStringLiteral(ANALYSIS_RUNTIME_DIRECTORY), root + "/cache", root + "/scratch"};
    }
    static Reader pcm(std::atomic_int *reads)
    {
        return [reads](const auto &cancelled) {
            ++*reads;
            Pcm value;
            value.sampleRate = 22050;
            value.wholeTrack = true;
            value.left.resize(22050 * 4 * 4);
            for (int i = 0; i < 22050 * 4; ++i) {
                const float sample = float(.1 * std::sin(i * .06) *
                                           (.2 + .8 * std::max(0., 1. - (i % 11025) / 1200.)));
                std::memcpy(value.left.data() + i * 4, &sample, 4);
            }
            if (cancelled->load())
                value.error = "canceled";
            return value;
        };
    }
private slots:
    void probeReportsRealGpuAndMissingModelsExplicitly()
    {
        QTemporaryDir empty;
        const auto run = [](const QString &models) {
            QProcess process;
            process.start(QStringLiteral(ANALYSIS_HELPER),
                          {"--probe", "--models", models, "--runtime",
                           QStringLiteral(ANALYSIS_RUNTIME_DIRECTORY)});
            if (!process.waitForFinished(30000))
                return QJsonObject{};
            return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
        };
        const auto missing = run(empty.path());
        QVERIFY(!missing.value("available").toBool());
        QVERIFY2(missing.value("error").toString().startsWith("ModelUnavailable"),
                 qPrintable(QJsonDocument(missing).toJson()));
        const auto actual = run(QStringLiteral(ANALYSIS_MODEL_DIRECTORY));
        QVERIFY2(actual.value("available").toBool(), qPrintable(QJsonDocument(actual).toJson()));
        QCOMPARE(actual.value("backend").toString(), "Metal");
        QVERIFY(!actual.value("adapter").toString().isEmpty());
        QVERIFY(!actual.value("cpuFallback").toBool());
        QVERIFY(QFile::link(QStringLiteral(ANALYSIS_MODEL_DIRECTORY "/beat_this-wgsl.bin"),
                            empty.filePath("beat_this-wgsl.bin")));
        const auto beatsOnly = run(empty.path());
        QVERIFY2(beatsOnly.value("available").toBool(),
                 qPrintable(QJsonDocument(beatsOnly).toJson()));
        const auto profiles = beatsOnly.value("profiles").toArray();
        QCOMPARE(profiles.size(), 2);
        QVERIFY(profiles[0].toObject().value("modelAvailable").toBool());
        QVERIFY(!profiles[1].toObject().value("modelAvailable").toBool());
    }
    void cacheMissDoesNotDecodeOrSpawn()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QObject scope;
        Broker broker(config(directory.path()));
        broker.setSource(&scope, source());
        std::atomic_int reads{0};
        auto *job = broker.request(&scope, source(), Kind::Beats, Policy::StoredOnly, pcm(&reads));
        QTRY_VERIFY(job->finished());
        QCOMPARE(job->errorCode(), "CacheMiss");
        QCOMPARE(reads.load(), 0);
        QVERIFY(broker.drained());
        QVERIFY(!QDir(directory.path() + "/scratch").exists());
    }
    void cpuCannotImpersonateGpu()
    {
        QTemporaryDir directory;
        QObject scope;
        auto settings = config(directory.path());
        settings.helper = "/missing/helper";
        Broker broker(settings);
        broker.setSource(&scope, source());
        std::atomic_int reads{0};
        auto *job = broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, pcm(&reads));
        QTRY_VERIFY(job->finished());
        QCOMPARE(job->errorCode(), "Unavailable");
        QCOMPARE(reads.load(), 0);
    }
    void missingModelIsRejectedBeforeWholeTrackRead()
    {
        QTemporaryDir directory;
        QObject scope;
        auto settings = config(directory.path());
        settings.modelDirectory = directory.path() + "/no-models";
        Broker broker(settings);
        broker.setSource(&scope, source());
        std::atomic_int reads{0};
        auto *job = broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, pcm(&reads));
        QTRY_VERIFY_WITH_TIMEOUT(job->finished(), 10000);
        QCOMPARE(job->errorCode(), "ModelUnavailable");
        QCOMPARE(reads.load(), 0);
    }
    void realGpuThenPersistentStoredOnly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QObject scope;
        std::atomic_int reads{0};
        const auto cacheDirectory = directory.path() + "/cache";
        QVERIFY(QDir().mkpath(cacheDirectory));
        for (int index = 0; index < 128; ++index) {
            QFile old(cacheDirectory + '/' +
                      QStringLiteral("%1.json").arg(index, 64, 16, QChar('0')));
            QVERIFY(old.open(QIODevice::WriteOnly));
            QCOMPARE(old.write("{}"), 2);
        }
        const auto hugeName = cacheDirectory + '/' + QString(64, QChar('f')) + ".json";
        QFile huge(hugeName);
        QVERIFY(huge.open(QIODevice::WriteOnly));
        QVERIFY(huge.resize(70 * 1024 * 1024));
        huge.close(); // sparse quota fixture, not a 70MiB payload
        QFile foreignFile(cacheDirectory + "/foreign.txt");
        QVERIFY(foreignFile.open(QIODevice::WriteOnly));
        QCOMPARE(foreignFile.write("preserve"), 8);
        foreignFile.close();
        {
            Broker broker(config(directory.path()));
            broker.setSource(&scope, source());
            auto *job = broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, pcm(&reads));
            QTRY_VERIFY_WITH_TIMEOUT(job->finished(), 30000);
            QVERIFY2(job->state() == Job::State::Succeeded, qPrintable(job->errorMessage()));
            QVERIFY(job->result().value("complete").toBool());
            QVERIFY(job->result().value("beats").toArray().size() > 2);
            QCOMPARE(reads.load(), 1);
            QVERIFY(broker.drained());
            QVERIFY(!QFile::exists(hugeName));
            QVERIFY(QFile::exists(foreignFile.fileName()));
            const auto entryNames = QDir(cacheDirectory).entryList({"*.json"}, QDir::Files);
            QVERIFY(entryNames.size() <= 128);
        }
        Broker restored(config(directory.path()));
        restored.setSource(&scope, source(2));
        auto *cached =
            restored.request(&scope, source(2), Kind::Beats, Policy::StoredOnly, pcm(&reads));
        QTRY_VERIFY(cached->finished());
        QCOMPARE(cached->state(), Job::State::Succeeded);
        QCOMPARE(reads.load(), 1);
        auto foreign = source(3);
        foreign.accountNamespace = "account-b";
        restored.setSource(&scope, foreign);
        auto *foreignMiss =
            restored.request(&scope, foreign, Kind::Beats, Policy::StoredOnly, pcm(&reads));
        QTRY_VERIFY(foreignMiss->finished());
        QCOMPARE(foreignMiss->errorCode(), "CacheMiss");
        QCOMPARE(reads.load(), 1);
        const auto names = QDir(directory.path() + "/cache").entryList({"*.json"}, QDir::Files);
        QString activeName;
        for (const auto &name : names) {
            QFile candidate(directory.path() + "/cache/" + name);
            QVERIFY(candidate.open(QIODevice::ReadOnly));
            if (QJsonDocument::fromJson(candidate.readAll())
                    .object()
                    .value("result")
                    .toObject()
                    .value("complete")
                    .toBool()) {
                QVERIFY(activeName.isEmpty());
                activeName = name;
            }
        }
        QVERIFY(!activeName.isEmpty());
        QFile file(directory.path() + "/cache/" + activeName);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto entry = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto wrong = entry.value("result").toObject();
        wrong.insert("source", "heuristic-beats");
        entry.insert("result", wrong);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(entry).toJson());
        file.close();
        restored.setSource(&scope, source(4));
        auto *corrupt =
            restored.request(&scope, source(4), Kind::Beats, Policy::StoredOnly, pcm(&reads));
        QTRY_VERIFY(corrupt->finished());
        QCOMPARE(corrupt->errorCode(), "CacheMiss");
        QCOMPARE(reads.load(), 1);
    }
    void staleSourceAndRetirementCannotPublish()
    {
        QTemporaryDir directory;
        QObject scope;
        Broker broker(config(directory.path()));
        broker.setSource(&scope, source());
        std::atomic_int reads{0};
        auto *job = broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, pcm(&reads));
        QSignalSpy completed(job, &Job::completed);
        broker.setSource(&scope, source(2));
        QTRY_VERIFY_WITH_TIMEOUT(broker.drained(), 5000);
        QCOMPARE(job->state(), Job::State::Canceled);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(QDir(directory.path() + "/cache").entryList({"*.json"}, QDir::Files).size(), 0);
        auto *stale = broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, pcm(&reads));
        QTRY_VERIFY(stale->finished());
        QCOMPARE(stale->errorCode(), "StaleSource");
    }
    void twoPendingCoalesceAndLastLeaseCancelsReader()
    {
        struct Gate
        {
            std::atomic_int reads{0}, canceled{0};
        };
        auto gate = std::make_shared<Gate>();
        Reader blocked = [gate](const auto &cancel) {
            ++gate->reads;
            while (!cancel->load())
                QThread::msleep(1);
            ++gate->canceled;
            Pcm result;
            result.error = "canceled";
            return result;
        };
        QTemporaryDir directory;
        QObject one, shared, two, three;
        Broker broker(config(directory.path()));
        auto second = source();
        second.trackKey = "song-b";
        auto third = source();
        third.trackKey = "song-c";
        broker.setSource(&one, source());
        broker.setSource(&shared, source());
        broker.setSource(&two, second);
        broker.setSource(&three, third);
        auto *first = broker.request(&one, source(), Kind::Beats, Policy::GpuOnly, blocked);
        auto *joined = broker.request(&shared, source(), Kind::Beats, Policy::GpuOnly, blocked);
        auto *other = broker.request(&two, second, Kind::Beats, Policy::GpuOnly, blocked);
        auto *busy = broker.request(&three, third, Kind::Beats, Policy::GpuOnly, blocked);
        QTRY_COMPARE(gate->reads.load(), 2);
        QTRY_VERIFY(busy->finished());
        QCOMPARE(busy->errorCode(), "Busy");
        QCOMPARE(broker.pendingCount(), 2);
        first->cancel();
        QCOMPARE(first->state(), Job::State::Canceled);
        QTest::qWait(10);
        QCOMPARE(gate->canceled.load(), 0);
        QVERIFY(!joined->finished());
        joined->cancel();
        other->cancel();
        QTRY_VERIFY(broker.drained());
        QCOMPARE(gate->canceled.load(), 2);
        QVERIFY(QDir(directory.path() + "/cache").entryList({"*.json"}, QDir::Files).isEmpty());
    }
    void cachedBeatJoinsExistingButCachedDrumsNeverStartOrJoin()
    {
        auto canceled = std::make_shared<std::atomic_int>(0);
        Reader blocked = [canceled](const auto &cancel) {
            while (!cancel->load())
                QThread::msleep(1);
            ++*canceled;
            Pcm result;
            result.error = "canceled";
            return result;
        };
        QTemporaryDir directory;
        QObject owner, cached;
        Broker broker(config(directory.path()));
        broker.setSource(&owner, source());
        broker.setSource(&cached, source());
        auto *beat = broker.request(&owner, source(), Kind::Beats, Policy::GpuOnly, blocked);
        auto *joined = broker.request(&cached, source(), Kind::Beats, Policy::StoredOnly);
        auto *drum = broker.request(&owner, source(), Kind::DrumHits, Policy::GpuOnly, blocked);
        auto *notJoined = broker.request(&cached, source(), Kind::DrumHits, Policy::StoredOnly);
        QTRY_VERIFY(notJoined->finished());
        QCOMPARE(notJoined->errorCode(), "CacheMiss");
        QVERIFY(!joined->finished());
        QCOMPARE(broker.pendingCount(), 2);
        broker.retireScope(&owner);
        QVERIFY(!joined->finished());
        joined->cancel();
        QTRY_VERIFY(broker.drained());
        QCOMPARE(beat->state(), Job::State::Canceled);
        QCOMPARE(drum->state(), Job::State::Canceled);
    }
    void newGenerationCanRequestWhileRetiredReaderDrains()
    {
        auto reads = std::make_shared<std::atomic_int>(0);
        auto release = std::make_shared<std::atomic_bool>(false);
        Reader reader = [reads, release](const auto &cancel) {
            const auto call = ++*reads;
            if (call == 1)
                while (!release->load())
                    QThread::msleep(1);
            Pcm result;
            result.wholeTrack = true;
            result.sampleRate = 1;
            return result;
        };
        QTemporaryDir directory;
        QObject scope;
        Broker broker(config(directory.path()));
        broker.setSource(&scope, source());
        auto *old = broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, reader);
        QTRY_COMPARE(reads->load(), 1);
        broker.setSource(&scope, source(2));
        auto *fresh = broker.request(&scope, source(2), Kind::Beats, Policy::GpuOnly, reader);
        release->store(true);
        QTRY_VERIFY(fresh->finished());
        QTRY_VERIFY(broker.drained());
        QCOMPARE(fresh->errorCode(), "InvalidPcm");
        QCOMPARE(reads->load(), 2);
        QCOMPARE(old->state(), Job::State::Canceled);
    }
    void pendingReaderDestructionDoesNotBlockOrTouchRetiredObjects()
    {
        struct Gate
        {
            std::atomic_bool entered{false}, release{false}, done{false};
        };
        auto gate = std::make_shared<Gate>();
        Reader blocked = [gate](const auto &) {
            gate->entered = true;
            while (!gate->release)
                QThread::msleep(1);
            gate->done = true;
            return Pcm{};
        };
        QTemporaryDir directory;
        QObject scope;
        auto *broker = new Broker(config(directory.path()));
        broker->setSource(&scope, source());
        auto *job = broker->request(&scope, source(), Kind::Beats, Policy::GpuOnly, blocked);
        QSignalSpy completed(job, &Job::completed);
        QTRY_VERIFY(gate->entered.load());
        QElapsedTimer timer;
        timer.start();
        delete broker;
        QVERIFY2(timer.elapsed() < 500,
                 "destruction waited for the deliberately blocked authorized reader");
        QCOMPARE(job->state(), Job::State::Canceled);
        QCOMPARE(completed.count(), 0);
        gate->release = true;
        QTRY_VERIFY(gate->done.load());
        QTest::qWait(10);
        QCOMPARE(completed.count(), 0);
        QVERIFY(QDir(directory.path() + "/cache").entryList({"*.json"}, QDir::Files).isEmpty());
    }
    void activeGpuRetirementAndReentrantDeletionCannotPublish()
    {
        QTemporaryDir directory;
        QObject scope;
        std::atomic_int reads{0};
        auto *broker = new Broker(config(directory.path()));
        QPointer<Broker> guard(broker);
        broker->setSource(&scope, source());
        auto *job = broker->request(&scope, source(), Kind::Beats, Policy::GpuOnly, pcm(&reads));
        QSignalSpy completed(job, &Job::completed);
        bool progressed = false;
        connect(job, &Job::progress, &scope, [&] {
            progressed = true;
            delete broker;
            broker = nullptr;
        });
        QTRY_VERIFY_WITH_TIMEOUT(progressed, 30000);
        QVERIFY(!guard);
        QCOMPARE(job->state(), Job::State::Canceled);
        QCOMPARE(completed.count(), 0);
        QTest::qWait(50);
        QVERIFY(QDir(directory.path() + "/cache").entryList({"*.json"}, QDir::Files).isEmpty());
    }
    void invalidDecoderContractNeverStartsGpu()
    {
        QTemporaryDir directory;
        QObject scope;
        Broker broker(config(directory.path()));
        broker.setSource(&scope, source());
        auto *partial =
            broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, [](const auto &) {
                Pcm value;
                value.sampleRate = 22050;
                value.left.resize(4096);
                return value;
            });
        QTRY_VERIFY(partial->finished());
        QCOMPARE(partial->errorCode(), "IncompleteSource");
        auto *rate =
            broker.request(&scope, source(), Kind::Beats, Policy::GpuOnly, [](const auto &) {
                Pcm value;
                value.wholeTrack = true;
                value.sampleRate = 44100;
                value.left.resize(4096);
                return value;
            });
        QTRY_VERIFY(rate->finished());
        QCOMPARE(rate->errorCode(), "InvalidPcm");
        auto *stereo =
            broker.request(&scope, source(), Kind::DrumHits, Policy::GpuOnly, [](const auto &) {
                Pcm value;
                value.wholeTrack = true;
                value.sampleRate = 44100;
                value.left.resize(4096);
                return value;
            });
        QTRY_VERIFY(stereo->finished());
        QCOMPARE(stereo->errorCode(), "InvalidPcm");
        QVERIFY(broker.drained());
        QVERIFY(!QDir(directory.path() + "/scratch").exists());
    }
    void realDrumPartialIsScopedAndNeverPersistedAsComplete()
    {
        QTemporaryDir directory;
        QObject scope;
        Broker broker(config(directory.path()));
        broker.setSource(&scope, source());
        auto *job =
            broker.request(&scope, source(), Kind::DrumHits, Policy::GpuOnly, [](const auto &) {
                Pcm value;
                value.sampleRate = 44100;
                value.wholeTrack = true;
                value.left.resize(44100 * 13 * 4);
                for (int i = 0; i < 44100 * 13; ++i) {
                    const float sample =
                        float(.22 * std::sin(2 * 3.141592653589793 * 80 * i / 44100) *
                                  std::exp(-(i % 22050) / 882.) +
                              .015 * std::sin(2 * 3.141592653589793 * 660 * i / 44100));
                    std::memcpy(value.left.data() + i * 4, &sample, 4);
                }
                value.right = value.left;
                return value;
            });
        QSignalSpy completed(job, &Job::completed);
        bool partial = false;
        connect(job, &Job::changed, &scope, [&] {
            if (job->result().isEmpty() || job->result().value("complete").toBool())
                return;
            partial = true;
            const auto hits = job->result().value("hits").toObject();
            QVERIFY(hits.value("until").toDouble() >= 10);
            QVERIFY(!hits.value("times").toArray().isEmpty());
            broker.retireScope(&scope);
        });
        QTRY_VERIFY_WITH_TIMEOUT(partial, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(broker.drained(), 5000);
        QCOMPARE(job->state(), Job::State::Canceled);
        QCOMPARE(completed.count(), 0);
        QVERIFY(job->result().isEmpty());
        QVERIFY(QDir(directory.path() + "/cache").entryList({"*.json"}, QDir::Files).isEmpty());
    }
};
QTEST_GUILESS_MAIN(BrokerTest)
#include "BrokerTest.moc"
