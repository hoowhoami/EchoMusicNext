// SPDX-License-Identifier: GPL-3.0-only
#include "AnalysisService.h"
#include <QJsonDocument>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QtTest>
using namespace EchoMusic::AudioAnalysis;
class ServiceTest final : public QObject
{
    Q_OBJECT
    static QJsonObject facts(int revision = 1)
    {
        return {{"accountRevision", "1"},
                {"trackKey", "actual-track"},
                {"sourceRevision", QString::number(revision)},
                {"trackSequence", "9"},
                {"locator", "internal-not-public"},
                {"audioStreamOrdinal", -1}};
    }
    static Config config(const QString &root)
    {
        return {"/missing/helper", root, root, root + "/cache", root + "/scratch"};
    }
private slots:
    void captureCompletionAlsoPublishesTheCommittedEpochExactlyOnce()
    {
        QTemporaryDir temp;
        QObject owner;
        const auto a = facts();
        QString epoch;
        auto gate = std::make_shared<QSemaphore>();
        auto entered = std::make_shared<std::atomic_bool>(false);
        Service service(
            config(temp.path()), [a] { return a; },
            [gate, entered](auto, auto) {
                entered->store(true);
                gate->acquire();
                return Input{};
            });
        const auto release = qScopeGuard([gate] { gate->release(); });
        service.setSourceAuthority([a](auto) { return a; },
                                   [&](const auto &snapshot) {
                                       return snapshot == a ? (epoch.isEmpty() ? QString("current")
                                                                               : "epoch:" + epoch)
                                                            : QString();
                                   });
        auto *job = service.request(&owner, "user:1", Kind::Beats, Policy::StoredOnly);
        QTRY_VERIFY(entered->load());
        QSignalSpy identities(&service, &Service::authorizationChanged);
        epoch = "33";
        QVERIFY(service.sourceIdentity(&owner).isEmpty());
        // No sourceChanged call here: capture completion first observes the
        // native permission update and commits this scope's sticky authority.
        gate->release();
        QTRY_VERIFY(job->finished());
        QCOMPARE(job->errorCode(), "CacheMiss");
        QTRY_COMPARE(identities.count(), 1);
        QCOMPARE(service.sourceIdentity(&owner),
                 (QJsonObject{{"sourceSequence", "9"}, {"transitionEpoch", "33"}}));
        service.sourceChanged();
        QTest::qWait(10);
        QCOMPARE(identities.count(), 1);
    }
    void publicIdentityCommitsTheRealEpochBeforeNotificationAndNeverLeaksFacts()
    {
        QTemporaryDir temp;
        QObject outgoing, incoming;
        auto a = facts(), b = facts(2);
        b.insert("trackSequence", "9007199254740994");
        a.insert("trackSequence", "9007199254740993");
        auto current = a;
        QString epoch;
        Service service(
            config(temp.path()), [&] { return current; }, [](auto, auto) { return Input{}; });
        service.setSourceAuthority([&](auto) { return current; },
                                   [&](const auto &snapshot) {
                                       if (snapshot == a && !epoch.isEmpty())
                                           return "epoch:" + epoch;
                                       return snapshot == current ? QString("current") : QString();
                                   });
        auto *first = service.request(&outgoing, "user:1", Kind::Beats, Policy::StoredOnly);
        QCOMPARE(service.sourceIdentity(&outgoing),
                 (QJsonObject{{"sourceSequence", "9007199254740993"}, {"transitionEpoch", ""}}));
        const auto sequence = service.sourceIdentity(&outgoing).value("sourceSequence");
        QSignalSpy changed(&service, &Service::authorizationChanged);
        QJsonObject observed;
        connect(&service, &Service::authorizationChanged, this,
                [&](QObject *owner) { observed = service.sourceIdentity(owner); });
        epoch = "18446744073709551615";
        QVERIFY(service.sourceIdentity(&outgoing).isEmpty());
        service.sourceChanged();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(observed, (QJsonObject{{"sourceSequence", sequence}, {"transitionEpoch", epoch}}));
        current = b;
        service.sourceChanged();
        QCOMPARE(service.sourceIdentity(&outgoing), observed);
        QTRY_VERIFY(first->finished());
        QCOMPARE(first->errorCode(), "CacheMiss");
        auto *second = service.request(&incoming, "user:1", Kind::Beats, Policy::StoredOnly);
        QCOMPARE(service.sourceIdentity(&incoming),
                 (QJsonObject{{"sourceSequence", "9007199254740994"}, {"transitionEpoch", ""}}));
        QTRY_VERIFY(second->finished());
        const auto serialized = QJsonDocument(observed).toJson();
        QVERIFY(!serialized.contains("internal-not-public"));
        QCOMPARE(observed.size(), 2);
        epoch.clear();
        QJsonObject atRevoke{{"unexpected", true}};
        connect(&service, &Service::authorizationRevoked, this, [&](QObject *owner) {
            if (owner == &outgoing)
                atRevoke = service.sourceIdentity(owner);
        });
        service.sourceChanged();
        QVERIFY(atRevoke.isEmpty());
        QVERIFY(service.sourceIdentity(&outgoing).isEmpty());
        current = a;
        epoch = "42";
        service.sourceChanged();
        QVERIFY(service.sourceIdentity(&outgoing).isEmpty());
    }
    void sameTrackSequenceWithChangedQualityDoesNotKeepTheOldIdentity()
    {
        QTemporaryDir temp;
        QObject owner;
        auto current = facts();
        Service service(
            config(temp.path()), [&] { return current; }, [](auto, auto) { return Input{}; });
        auto *job = service.request(&owner, "user:1", Kind::Beats, Policy::StoredOnly);
        QTRY_VERIFY(job->finished());
        QCOMPARE(service.sourceIdentity(&owner).value("sourceSequence"), QJsonValue("9"));
        current.insert("resolvedQuality", "flac");
        QVERIFY(service.sourceIdentity(&owner).isEmpty());
        service.sourceChanged();
        QVERIFY(service.sourceIdentity(&owner).isEmpty());
        current.remove("resolvedQuality");
        service.sourceChanged();
        QVERIFY(service.sourceIdentity(&owner).isEmpty());
    }
    void outgoingEpochRetainsExistingAndFreshCapturesButNeverResurrects()
    {
        QTemporaryDir temp;
        QObject existing, fresh, incoming;
        auto a = facts(), b = facts(2);
        b.insert("trackKey", "actual-b");
        b.insert("trackSequence", "10");
        auto current = a;
        QString activeEpoch;
        auto gate = std::make_shared<QSemaphore>();
        auto entered = std::make_shared<std::atomic_int>(0);
        QList<QString> captured;
        QMutex mutex;
        Service service(
            config(temp.path()), [&] { return current; },
            [&](const QJsonObject &source, auto) {
                {
                    QMutexLocker lock(&mutex);
                    captured.append(source.value("trackSequence").toString());
                }
                ++*entered;
                gate->acquire();
                return Input{};
            });
        const auto release = qScopeGuard([gate] { gate->release(3); });
        service.setSourceAuthority(
            [&](const QJsonObject &selector) {
                return selector.isEmpty() ? current
                       : activeEpoch == selector.value("transitionEpoch").toString()
                           ? a
                           : QJsonObject{};
            },
            [&](const QJsonObject &source) {
                if (source == a && !activeEpoch.isEmpty())
                    return "epoch:" + activeEpoch;
                return source == current ? QString("current") : QString();
            });
        auto *old = service.request(&existing, "user:1", Kind::Beats, Policy::StoredOnly);
        QTRY_COMPARE(entered->load(), 1);
        activeEpoch = "41";
        service.sourceChanged();
        current = b;
        service.sourceChanged();
        QVERIFY2(!old->finished(),
                 "An already captured A must survive actual B adoption within the same live epoch");
        auto *tail = service.request(&fresh, "user:1", Kind::DrumHits, Policy::StoredOnly,
                                     {{"sequence", "9"}, {"transitionEpoch", "41"}});
        QTRY_COMPARE(entered->load(), 2);
        {
            QMutexLocker lock(&mutex);
            QCOMPARE(captured, QStringList({"9", "9"}));
        }
        auto *busy = service.request(&incoming, "user:1", Kind::Beats, Policy::StoredOnly);
        QTRY_VERIFY(busy->finished());
        QCOMPARE(busy->errorCode(), "Busy");
        activeEpoch.clear();
        service.sourceChanged();
        QCOMPARE(old->state(), Job::State::Canceled);
        QCOMPARE(tail->state(), Job::State::Canceled);
        current = a;
        activeEpoch = "42";
        service.sourceChanged();
        QCOMPARE(old->state(), Job::State::Canceled);
        gate->release(2);
        QTRY_VERIFY(service.scopeDrained(&existing));
        QTRY_VERIFY(service.scopeDrained(&fresh));
        QVERIFY(old->result().isEmpty());
        QVERIFY(tail->result().isEmpty());
    }
    void captureStartedBeforeAdoptionRetriesOnlyTheSameLiveOutgoingEpoch()
    {
        QTemporaryDir temp;
        QObject owner;
        auto a = facts(), current = a;
        QString epoch;
        auto entered = std::make_shared<std::atomic_int>(0);
        auto gate = std::make_shared<QSemaphore>();
        Service service(
            config(temp.path()), [&] { return current; },
            [entered, gate](const QJsonObject &source, auto) {
                const int attempt = ++*entered;
                if (attempt == 1) {
                    gate->acquire();
                    Input failed;
                    failed.code = "StaleSource";
                    failed.error = "B was adopted";
                    return failed;
                }
                Input output;
                if (source.value("analysisTransitionEpoch") != "71") {
                    output.code = "InvalidResult";
                    output.error = "retry did not capture exact epoch";
                }
                return output;
            });
        const auto release = qScopeGuard([gate] { gate->release(); });
        service.setSourceAuthority([&](auto) { return current; },
                                   [&](const auto &source) {
                                       return source == a && !epoch.isEmpty() ? "epoch:" + epoch
                                              : source == current             ? QString("current")
                                                                              : QString();
                                   });
        auto *job = service.request(&owner, "user:1", Kind::Beats, Policy::StoredOnly);
        QTRY_COMPARE(entered->load(), 1);
        epoch = "71";
        service.sourceChanged();
        current = facts(2);
        service.sourceChanged();
        gate->release();
        QTRY_VERIFY(job->finished());
        QCOMPARE(entered->load(), 2);
        QCOMPARE(job->errorCode(), "CacheMiss");
        QVERIFY(service.scopeDrained(&owner));
    }
    void completedJobStillRevokesItsPublicAuthorizationSynchronously()
    {
        QTemporaryDir temp;
        QObject owner;
        auto a = facts();
        auto current = a;
        QString epoch;
        Service service(
            config(temp.path()), [&] { return current; }, [](auto, auto) { return Input{}; });
        service.setSourceAuthority([&](auto) { return current; },
                                   [&](const auto &snapshot) {
                                       if (snapshot != a)
                                           return QString();
                                       return !epoch.isEmpty() ? "epoch:" + epoch
                                              : current == a   ? QString("current")
                                                               : QString();
                                   });
        auto *job = service.request(&owner, "user:1", Kind::Beats, Policy::StoredOnly);
        QTRY_VERIFY(job->finished());
        QCOMPARE(job->errorCode(), "CacheMiss");
        epoch = "9";
        service.sourceChanged();
        current = facts(2);
        service.sourceChanged();
        QSignalSpy revoked(&service, &Service::authorizationRevoked);
        epoch.clear();
        service.sourceChanged();
        QCOMPARE(revoked.count(), 1);
        QCOMPARE(qvariant_cast<QObject *>(revoked.at(0).at(0)), &owner);
    }
    void sourceChangeCancelsPendingCaptureAndDrains()
    {
        QTemporaryDir temp;
        QObject scope;
        auto current = facts();
        auto gate = std::make_shared<QSemaphore>();
        auto entered = std::make_shared<std::atomic_bool>(false);
        Service service(
            config(temp.path()), [&] { return current; },
            [gate, entered](auto, auto) {
                entered->store(true);
                gate->acquire();
                return Input{};
            });
        auto *job = service.request(&scope, "user:1", Kind::Beats, Policy::GpuOnly);
        QTRY_VERIFY(entered->load());
        QVERIFY(!service.scopeDrained(&scope));
        current = facts(2);
        service.sourceChanged();
        QCOMPARE(job->state(), Job::State::Canceled);
        QVERIFY(!service.scopeDrained(&scope));
        QSignalSpy spy(job, &Job::changed);
        gate->release();
        QTRY_VERIFY(service.scopeDrained(&scope));
        QCOMPARE(spy.count(), 0);
        QVERIFY(job->result().isEmpty());
    }
    void captureBudgetAndScopeRetirementAreReal()
    {
        QTemporaryDir temp;
        QObject one, two, three;
        auto current = facts();
        auto gate = std::make_shared<QSemaphore>();
        auto entered = std::make_shared<std::atomic_int>(0);
        Service service(
            config(temp.path()), [&] { return current; },
            [gate, entered](auto, auto) {
                ++*entered;
                gate->acquire();
                return Input{};
            });
        service.request(&one, "user:1", Kind::Beats, Policy::GpuOnly);
        service.request(&two, "user:1", Kind::Beats, Policy::GpuOnly);
        auto *busy = service.request(&three, "user:1", Kind::Beats, Policy::GpuOnly);
        QTRY_VERIFY(busy->finished());
        QCOMPARE(busy->errorCode(), "Busy");
        QTRY_COMPARE(entered->load(), 2);
        service.retireScope(&one);
        service.retireScope(&two);
        gate->release(2);
        QTRY_VERIFY(service.scopeDrained(&one));
        QTRY_VERIFY(service.scopeDrained(&two));
    }
    void noAcknowledgedSourceDoesNotCapture()
    {
        QTemporaryDir temp;
        QObject scope;
        int captures = 0;
        Service service(
            config(temp.path()), [] { return QJsonObject{}; },
            [&](auto, auto) {
                ++captures;
                return Input{};
            });
        auto *job = service.request(&scope, "user:1", Kind::Beats, Policy::StoredOnly);
        QTRY_VERIFY(job->finished());
        QCOMPARE(job->errorCode(), "Unavailable");
        QCOMPARE(captures, 0);
    }
};
QTEST_GUILESS_MAIN(ServiceTest)
#include "ServiceTest.moc"
