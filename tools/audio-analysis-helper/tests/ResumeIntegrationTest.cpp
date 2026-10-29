// SPDX-License-Identifier: GPL-3.0-only
#include "AnalysisService.h"
#include <QDir>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <cstring>
using namespace EchoMusic::AudioAnalysis;
class ResumeIntegrationTest final : public QObject
{
    Q_OBJECT
    static Config config(const QString &root)
    {
        return {QStringLiteral(ANALYSIS_HELPER), QStringLiteral(ANALYSIS_MODEL_DIRECTORY),
                QStringLiteral(ANALYSIS_RUNTIME_DIRECTORY), root + "/cache", root + "/scratch"};
    }
    static QJsonObject facts(QString track = "a", int sequence = 1)
    {
        return {{"accountRevision", "1"},
                {"trackKey", track},
                {"sourceRevision", "1"},
                {"trackSequence", QString::number(sequence)},
                {"sourceKind", "local"}};
    }
    static Reader pcm(std::atomic_int *reads = nullptr, bool changed = false)
    {
        return [reads, changed](const auto &) {
            if (reads)
                ++*reads;
            Pcm value;
            value.sampleRate = 44100;
            value.wholeTrack = true;
            value.left.resize(44100 * 4 * 4);
            value.right.resize(value.left.size());
            for (int i = 0; i < 44100 * 4; ++i) {
                const float a = float(.1 * std::sin(i * .031)), b = float(.08 * std::sin(i * .041));
                std::memcpy(value.left.data() + i * 4, &a, 4);
                std::memcpy(value.right.data() + i * 4, &b, 4);
            }
            if (changed)
                value.left[0] = char(value.left[0] ^ 1);
            return value;
        };
    }
    static void stopAtFirstSegment(Service &service, QObject &owner, const QString &account = "a")
    {
        auto *job = service.request(&owner, account, Kind::DrumHits, Policy::GpuOnly);
        int first = 0;
        const auto connection = connect(job, &Job::progress, &owner, [&](int done, int, double) {
            if (!first)
                first = done;
            job->cancel();
        });
        QTRY_VERIFY_WITH_TIMEOUT(job->finished(), 30000);
        QCOMPARE(job->state(), Job::State::Canceled);
        QTRY_VERIFY_WITH_TIMEOUT(service.scopeDrained(&owner), 10000);
        disconnect(connection);
        QCOMPARE(first, 1);
        QVERIFY(job->result().isEmpty());
    }
private slots:
    void fourSessionEntriesEvictOldestAndStoredOnlyNeverUsesPartial()
    {
        QTemporaryDir temp;
        QObject owner;
        auto current = facts();
        std::atomic_int reads{0}, captures{0};
        Service service(
            config(temp.path()), [&] { return current; },
            [&](auto, auto) {
                ++captures;
                return Input{pcm(&reads), "physical-local-file", {}, {}};
            });
        for (int index = 0; index < 5; ++index) {
            current = facts(QString(QChar('a' + index)), index + 1);
            service.sourceChanged();
            stopAtFirstSegment(service, owner);
        }
        QCOMPARE(reads.load(), 5);
        QVERIFY(QDir(temp.path() + "/cache").entryList({"*.json"}, QDir::Files).isEmpty());
        current = facts("b", 2);
        service.sourceChanged();
        auto *stored = service.request(&owner, "a", Kind::DrumHits, Policy::StoredOnly);
        QTRY_VERIFY(stored->finished());
        QCOMPARE(stored->errorCode(), "CacheMiss");
        QCOMPARE(reads.load(), 5);
        auto *resumed = service.request(&owner, "a", Kind::DrumHits, Policy::GpuOnly);
        QList<int> progress;
        bool covered = false;
        connect(resumed, &Job::changed, &owner, [&] {
            if (!resumed->result().isEmpty() && !resumed->result().value("complete").toBool()) {
                covered = true;
                QVERIFY(progress.isEmpty());
                QVERIFY(resumed->result().value("hits").toObject().value("until").toDouble() > 0);
            }
        });
        connect(resumed, &Job::progress, &owner,
                [&](int done, int, double) { progress.append(done); });
        QTRY_VERIFY_WITH_TIMEOUT(resumed->finished(), 30000);
        QCOMPARE(resumed->state(), Job::State::Succeeded);
        QCOMPARE(progress, QList<int>{2});
        QVERIFY(covered);
        QCOMPARE(reads.load(), 6);
        // A was the fifth-oldest stop and must genuinely recompute its first segment.
        current = facts("a", 1);
        service.sourceChanged();
        stopAtFirstSegment(service, owner);
        QCOMPARE(reads.load(), 7);
        const auto before = reads.load();
        current = facts("b", 2);
        service.sourceChanged();
        auto *complete = service.request(&owner, "a", Kind::DrumHits, Policy::StoredOnly);
        QTRY_VERIFY(complete->finished());
        QCOMPARE(complete->state(), Job::State::Succeeded);
        QVERIFY(complete->result().value("complete").toBool());
        QCOMPARE(reads.load(), before);
    }
    void changedAccountRepresentationAndPcmCannotReanimateSavedWork()
    {
        QTemporaryDir temp;
        QObject owner;
        auto current = facts();
        bool changed = false;
        std::atomic_int reads{0};
        Service service(
            config(temp.path()), [&] { return current; },
            [&](auto, auto) { return Input{pcm(&reads, changed), "physical-local-file", {}, {}}; });
        stopAtFirstSegment(service, owner);
        stopAtFirstSegment(service, owner, "other-account");
        current.insert("resolvedQuality", "another-representation");
        service.sourceChanged();
        stopAtFirstSegment(service, owner);
        current.remove("resolvedQuality");
        service.sourceChanged();
        changed = true;
        auto *job = service.request(&owner, "a", Kind::DrumHits, Policy::GpuOnly);
        QList<int> progress;
        connect(job, &Job::progress, &owner, [&](int done, int, double) { progress.append(done); });
        QTRY_VERIFY_WITH_TIMEOUT(job->finished(), 30000);
        QCOMPARE(job->state(), Job::State::Succeeded);
        QCOMPARE(progress, (QList<int>{1, 2}));
        // Reuse the already-authorized staged decode for fallback, rather than capture/redecode
        // again.
        QCOMPARE(reads.load(), 4);
        QVERIFY(job->result().value("complete").toBool());
    }
    void expiredOutgoingEpochCancelsResumeAndCannotReviveScopeOrPartial()
    {
        QTemporaryDir temp;
        QObject owner, fresh;
        auto a = facts(), current = a;
        QString epoch;
        std::atomic_int reads{0}, captures{0};
        Service service(
            config(temp.path()), [&] { return current; },
            [&](auto, auto) {
                ++captures;
                return Input{pcm(&reads), "physical-local-file", {}, {}};
            });
        service.setSourceAuthority(
            [&](const auto &selector) {
                if (selector.isEmpty())
                    return current;
                return !epoch.isEmpty() && selector.value("transitionEpoch") == epoch
                           ? a
                           : QJsonObject{};
            },
            [&](const auto &snapshot) {
                if (snapshot == a && !epoch.isEmpty())
                    return "epoch:" + epoch;
                return snapshot == current ? QString("current") : QString();
            });
        stopAtFirstSegment(service, owner);
        epoch = "41";
        service.sourceChanged();
        current = facts("b", 2);
        service.sourceChanged();
        auto *resumed = service.request(&fresh, "a", Kind::DrumHits, Policy::GpuOnly,
                                        {{"sequence", "1"}, {"transitionEpoch", "41"}});
        bool covered = false;
        connect(resumed, &Job::changed, &fresh, [&] {
            if (!resumed->result().isEmpty() && !resumed->result().value("complete").toBool()) {
                covered = true;
                epoch.clear();
                service.sourceChanged();
            }
        });
        QTRY_VERIFY_WITH_TIMEOUT(covered, 30000);
        QCOMPARE(resumed->state(), Job::State::Canceled);
        QVERIFY(resumed->result().isEmpty());
        QVERIFY(service.sourceIdentity(&fresh).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(service.scopeDrained(&fresh), 10000);
        const auto before = captures.load();
        auto *expired = service.request(&fresh, "a", Kind::DrumHits, Policy::GpuOnly,
                                        {{"sequence", "1"}, {"transitionEpoch", "41"}});
        QTRY_VERIFY(expired->finished());
        QCOMPARE(expired->errorCode(), "Unavailable");
        QCOMPARE(captures.load(), before);
        current = a;
        epoch = "42";
        service.sourceChanged();
        QVERIFY(service.sourceIdentity(&fresh).isEmpty());
        QVERIFY(resumed->result().isEmpty());
        // Only a new authorized request/capture may use the saved data again.
        auto *authorized = service.request(&fresh, "a", Kind::DrumHits, Policy::GpuOnly);
        QList<int> progress;
        connect(authorized, &Job::progress, &fresh,
                [&](int done, int, double) { progress.append(done); });
        QTRY_VERIFY_WITH_TIMEOUT(authorized->finished(), 30000);
        QCOMPARE(authorized->state(), Job::State::Succeeded);
        QCOMPARE(progress, QList<int>{2});
        QCOMPARE(reads.load(), 3);
    }
};
QTEST_GUILESS_MAIN(ResumeIntegrationTest)
#include "ResumeIntegrationTest.moc"
