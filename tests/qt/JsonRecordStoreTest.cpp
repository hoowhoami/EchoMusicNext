#include "JsonRecordStore.h"
#include <QSignalSpy>
#include <QSemaphore>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>
using namespace EchoMusic;
class JsonRecordStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void capacityWaitResumesAndConsumerDestructionCancelsRead()
    {
        QTemporaryDir dir;
        SessionDatabase db(dir.filePath("session"), {8 * 1024 * 1024, 1});
        JsonRecordStore store(db);
        QSemaphore release;
        QVERIFY(db.submit(
            this,
            [&release](QSqlDatabase &, const QString &) {
                release.acquire();
                return true;
            },
            [](bool) {}));
        auto snapshot = store.create({QJsonObject{{"id", "kept"}}});
        QTest::qWait(10);
        QVERIFY(!store.persisted(snapshot));
        QCOMPARE(db.pendingJobs(), 1);
        release.release();
        QTRY_VERIFY(store.persisted(snapshot));
        QVERIFY(db.submit(
            this,
            [&release](QSqlDatabase &, const QString &) {
                release.acquire();
                return true;
            },
            [](bool) {}));
        auto cancelled = std::make_unique<QObject>();
        int cancelledCalls = 0;
        store.read({snapshot, 0}, cancelled.get(), [&](QJsonArray, QString) { ++cancelledCalls; });
        bool done = false;
        store.readAll(snapshot, this, [&](QJsonArray rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows.first().toObject().value("id").toString(), QString("kept"));
            done = true;
        });
        cancelled.reset();
        release.release();
        QTRY_VERIFY(done);
        QCOMPARE(cancelledCalls, 0);
    }
    void viewportByteBudgetReturnsPrefixAndExplicitSnapshotReadsEverything()
    {
        QTemporaryDir dir;
        SessionDatabase db(dir.filePath("session"));
        JsonRecordStore store(db);
        QJsonArray source;
        for (int i = 0; i < 4; ++i)
            source.append(QJsonObject{{"id", i}, {"text", QString(350000, QChar('x'))}});
        auto snapshot = store.create(source);
        QTRY_VERIFY(store.persisted(snapshot));
        bool done = false;
        store.read(
            {snapshot, 0}, this,
            [&](QJsonArray rows, QString error) {
                QVERIFY(error.isEmpty());
                QCOMPARE(rows.size(), 1);
                QCOMPARE(rows.first(), source.first());
                done = true;
            },
            64);
        QTRY_VERIFY(done);
        done = false;
        store.readAll(snapshot, this, [&](QJsonArray rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows, source);
            done = true;
        });
        QTRY_VERIFY(done);
    }
    void coldReadsPreserveFieldsUseIndexAndLeaseRetirement()
    {
        QTemporaryDir dir;
        SessionDatabase db(dir.filePath("session"));
        JsonRecordStore store(db);
        QSignalSpy errors(&store, &JsonRecordStore::cacheFailed);
        QJsonArray source;
        for (int i = 0; i < 1500; ++i)
            source.append(QJsonObject{{"id", QString::number(i)},
                                      {"text", "中文😀"},
                                      {"optional", QJsonValue::Null},
                                      {"unknown", QJsonObject{{"array", QJsonArray{false, 7}}}}});
        auto snapshot = store.create(source);
        QTRY_VERIFY_WITH_TIMEOUT(store.persisted(snapshot), 10000);
        QVERIFY(errors.isEmpty());
        bool done = false;
        store.read({snapshot, 1499}, this, [&](QJsonArray rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows, QJsonArray{source.last()});
            done = true;
        });
        QVERIFY(!done);   // The cold path is asynchronous, never GUI-thread SQL.
        snapshot.reset(); // The outstanding read owns the generation lease.
        QTRY_VERIFY(done);
        bool inspected = false;
        QVERIFY(db.submit(
            this,
            [](QSqlDatabase &sql, const QString &) {
                QSqlQuery q(sql);
                q.exec("EXPLAIN QUERY PLAN SELECT payload FROM json_record WHERE generation='x' "
                       "AND ordinal>=10 AND ordinal<74 ORDER BY ordinal");
                q.next();
                return q.value(3).toString();
            },
            [&](QString plan) {
                QVERIFY2(plan.contains("SEARCH") && plan.contains("PRIMARY KEY"), qPrintable(plan));
                inspected = true;
            }));
        QTRY_VERIFY(inspected);
        bool empty = false;
        QVERIFY(db.submit(
            this,
            [](QSqlDatabase &sql, const QString &) {
                QSqlQuery q(sql);
                q.exec("SELECT count(*) FROM json_record");
                q.next();
                return q.value(0).toInt();
            },
            [&](int count) {
                QCOMPARE(count, 0);
                empty = true;
            }));
        QTRY_VERIFY(empty);
    }
    void fullSnapshotAndWriteFailureKeepAcceptedRecords()
    {
        QTemporaryDir dir;
        SessionDatabase db(dir.filePath("good"));
        JsonRecordStore store(db);
        const QJsonArray source{QJsonObject{{"n", 1}}, QJsonObject{{"n", 2}}};
        auto snapshot = store.create(source);
        QTRY_VERIFY(store.persisted(snapshot));
        bool done = false;
        store.readAll(snapshot, this, [&](QJsonArray rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows, source);
            done = true;
        });
        QTRY_VERIFY(done);
        QFile blocked(dir.filePath("blocked"));
        QVERIFY(blocked.open(QIODevice::WriteOnly));
        blocked.close();
        SessionDatabase failed(dir.filePath("blocked/child"));
        JsonRecordStore fallback(failed);
        QSignalSpy errors(&fallback, &JsonRecordStore::cacheFailed);
        auto kept = fallback.create(source);
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!fallback.persisted(kept));
        fallback.readAll(kept, this, [&](QJsonArray rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows, source);
        });
    }
};
QTEST_GUILESS_MAIN(JsonRecordStoreTest)
#include "JsonRecordStoreTest.moc"
