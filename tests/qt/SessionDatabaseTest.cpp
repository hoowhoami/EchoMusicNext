#include "SessionDatabase.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSemaphore>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <optional>
using namespace EchoMusic;
class SessionDatabaseTest : public QObject
{
    Q_OBJECT
    struct Result
    {
        QString error;
        bool worker = false;
        int value = 0;
        QString path;
        QByteArray payload;
        bool sourceOpen = false;
    };
    static Result execute(SessionDatabase &database)
    {
        auto result = std::make_shared<std::optional<Result>>();
        const auto gui = QThread::currentThread();
        if (!database.submit(
                &database,
                [gui](QSqlDatabase &db, const QString &error) {
                    Result result{error, QThread::currentThread() != gui, 0};
                    if (!error.isEmpty())
                        return result;
                    QSqlQuery query(db);
                    if (!query.exec("SELECT 42") || !query.next())
                        result.error = query.lastError().text();
                    else
                        result.value = query.value(0).toInt();
                    return result;
                },
                [result](auto value) { *result = std::move(value); }))
            return {"not admitted", false, 0};
        if (!QTest::qWaitFor([&] { return result->has_value(); }, 5000))
            return {"timeout", false, 0};
        return std::move(**result);
    }
    static void close(SessionDatabase &database)
    {
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
    }
    static Result run(SessionDatabase &database,
                      std::function<Result(QSqlDatabase &, const QString &)> work)
    {
        auto result = std::make_shared<std::optional<Result>>();
        if (!database.submit(&database, std::move(work),
                             [result](Result value) { *result = std::move(value); }))
            return {"not admitted"};
        if (!QTest::qWaitFor([&] { return result->has_value(); }, 5000))
            return {"timeout"};
        return std::move(**result);
    }
    static Result seed(SessionDatabase &database, const QByteArray &payload)
    {
        return run(database, [payload](QSqlDatabase &db, const QString &error) {
            Result result{error};
            if (!error.isEmpty())
                return result;
            QSqlQuery query(db);
            for (const auto *sql :
                 {"CREATE TABLE sample(record INTEGER PRIMARY KEY AUTOINCREMENT,"
                  "payload BLOB NOT NULL)",
                  "CREATE INDEX sample_payload ON sample(payload)", "PRAGMA user_version=17"})
                if (!query.exec(QString::fromLatin1(sql))) {
                    result.error = query.lastError().text();
                    return result;
                }
            query.prepare("INSERT INTO sample VALUES(7,?)");
            query.addBindValue(payload);
            if (!query.exec())
                result.error = query.lastError().text();
            result.path = db.databaseName();
            return result;
        });
    }
    static Result readAndAppend(SessionDatabase &database)
    {
        return run(database, [](QSqlDatabase &db, const QString &error) {
            Result result{error};
            result.sourceOpen = db.isOpen();
            if (!error.isEmpty())
                return result;
            result.path = db.databaseName();
            QSqlQuery query(db);
            if (!query.exec("SELECT payload FROM sample WHERE record=7") || !query.next())
                result.error = query.lastError().text().isEmpty() ? "missing source"
                                                                  : query.lastError().text();
            else
                result.payload = query.value(0).toByteArray();
            query.finish();
            if (result.error.isEmpty() &&
                !query.exec("INSERT INTO sample(payload) VALUES(x'010203')"))
                result.error = query.lastError().text();
            if (result.error.isEmpty())
                result.value = query.lastInsertId().toInt();
            return result;
        });
    }
private slots:
    void truncatedOpenDatabaseFallsBackToSourceInvalidation()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.filePath("session"));
        const auto original = seed(database, QByteArray(6 * 1024 * 1024, 'x'));
        QVERIFY2(original.error.isEmpty(), qPrintable(original.error));
        QFile file(original.path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(0));
        file.close();
        QSignalSpy invalidated(&database, &SessionDatabase::invalidated);
        QVERIFY(database.submit(
            this, [](QSqlDatabase &, const QString &) { return true; }, [](bool) {}));
        QTRY_COMPARE(invalidated.size(), 1);
        QCOMPARE(execute(database).value, 42);
        close(database);
    }
    void totalLossRetiresQueuedWorkAndReloadsOnce()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.filePath("session"));
        QVERIFY(seed(database, "accepted").error.isEmpty());
        QSignalSpy invalidated(&database, &SessionDatabase::invalidated);
        int staleWork = 0, staleDelivery = 0, restored = 0;
        QObject consumer;
        connect(&database, &SessionDatabase::invalidated, &consumer, [&] {
            QCOMPARE(database.epoch(), quint64(1));
            QVERIFY(database.submit(
                &consumer,
                [](QSqlDatabase &db, const QString &error) {
                    if (!error.isEmpty())
                        return false;
                    QSqlQuery query(db);
                    return query.exec("CREATE TABLE reloaded(value INTEGER)") &&
                           query.exec("INSERT INTO reloaded VALUES(42)");
                },
                [&](bool ok) {
                    QVERIFY(ok);
                    ++restored;
                }));
        });
        QSemaphore release;
        QVERIFY(database.submit(
            &consumer,
            [&release](QSqlDatabase &db, const QString &) {
                release.acquire();
                const auto path = db.databaseName();
                db.close();
                return QFile::remove(path);
            },
            [&](bool) { ++staleDelivery; }));
        for (int i = 0; i < 3; ++i)
            QVERIFY(database.submit(
                &consumer,
                [&](QSqlDatabase &, const QString &) {
                    ++staleWork;
                    return true;
                },
                [&](bool) { ++staleDelivery; }));
        release.release();
        QTRY_COMPARE(restored, 1);
        QTRY_COMPARE(database.pendingJobs(), 0);
        QCOMPARE(invalidated.size(), 1);
        QCOMPARE(staleWork, 0);
        QCOMPARE(staleDelivery, 0);
        QCOMPARE(execute(database).value, 42);
        QCOMPARE(invalidated.size(), 1);
        close(database);
    }
    void copyFailureRetainsSourceUntilTransactionCanFinish()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows does not permit this fixture to unlink open handles");
#else
        QTemporaryDir directory;
        const auto root = directory.filePath("session");
        SessionDatabase database(root);
        QVERIFY(seed(database, QByteArray("retained")).error.isEmpty());
        const auto transaction = run(database, [](QSqlDatabase &db, const QString &error) {
            Result result{error};
            if (!error.isEmpty())
                return result;
            QSqlQuery query(db);
            if (!db.transaction() || !query.exec("SELECT count(*) FROM sample") || !query.next())
                result.error = "fixture read transaction failed";
            return result;
        });
        QVERIFY(transaction.error.isEmpty());
        QVERIFY(QDir(root).removeRecursively());
        const auto failed = run(database, [](QSqlDatabase &db, const QString &error) {
            Result result{error};
            result.sourceOpen = db.isOpen();
            QSqlQuery query(db);
            if (query.exec("SELECT count(*) FROM sample") && query.next())
                result.value = query.value(0).toInt();
            query.finish();
            db.rollback();
            return result;
        });
        QVERIFY(!failed.error.isEmpty());
        QVERIFY(!failed.error.contains("VACUUM"));
        QVERIFY(failed.sourceOpen);
        QCOMPARE(failed.value, 1);
        const auto retried = readAndAppend(database);
        QVERIFY2(retried.error.isEmpty(), qPrintable(retried.error));
        QCOMPARE(retried.payload, QByteArray("retained"));
        QCOMPARE(retried.value, 8);
        close(database);
#endif
    }
    void deletionDuringWorkReportsDbMovedAndNextTaskRecovers()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows does not permit this fixture to unlink open handles");
#else
        QTemporaryDir directory;
        SessionDatabase database(directory.filePath("session"));
        QVERIFY(seed(database, QByteArray("accepted")).error.isEmpty());
        const auto failed = run(database, [](QSqlDatabase &db, const QString &error) {
            Result result{error};
            if (!error.isEmpty())
                return result;
            if (!QDir(QFileInfo(db.databaseName()).absolutePath()).removeRecursively())
                return Result{"fixture deletion failed"};
            QSqlQuery query(db);
            if (!query.exec("INSERT INTO sample(payload) VALUES(x'ff')")) {
                result.error = query.lastError().text();
                result.value = query.lastError().nativeErrorCode().toInt();
            }
            return result;
        });
        QVERIFY(!failed.error.isEmpty());
        QCOMPARE(failed.value, 1032); // actual QSQLITE extended READONLY_DBMOVED
        const auto recovered = readAndAppend(database);
        QVERIFY2(recovered.error.isEmpty(), qPrintable(recovered.error));
        QCOMPARE(recovered.payload, QByteArray("accepted"));
        QCOMPARE(recovered.value, 8);
        close(database);
#endif
    }
    void removedFilesRecoverWithoutDroppingAcceptedRows_data()
    {
        QTest::addColumn<int>("removed");
        QTest::newRow("whole-cache-root") << 0;
        QTest::newRow("database-file") << 1;
        QTest::newRow("owner-lock") << 2;
    }
    void removedFilesRecoverWithoutDroppingAcceptedRows()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows does not permit this fixture to unlink open database/lock handles");
#else
        QFETCH(int, removed);
        QTemporaryDir directory;
        const auto root = directory.filePath("会话缓存");
        SessionDatabase database(root);
        const auto payload = QByteArray("full\0payload", 12) + QByteArray(65536, 'x');
        const auto original = seed(database, payload);
        QVERIFY2(original.error.isEmpty(), qPrintable(original.error));
        if (removed == 0)
            QVERIFY(QDir(root).removeRecursively());
        else
            QVERIFY(QFile::remove(removed == 1
                                      ? original.path
                                      : QFileInfo(original.path).dir().filePath("owner.lock")));
        const auto result = readAndAppend(database);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.payload, payload);
        QCOMPARE(result.value, 8);
        QVERIFY(result.path != original.path);
        QVERIFY(QFileInfo::exists(result.path));
        const auto settings = run(database, [](QSqlDatabase &db, const QString &error) {
            Result result{error};
            QSqlQuery query(db);
            for (const auto &sql :
                 {QStringLiteral("PRAGMA user_version"), QStringLiteral("PRAGMA foreign_keys"),
                  QStringLiteral("PRAGMA cache_size"),
                  QStringLiteral("SELECT count(*) FROM sample"),
                  QStringLiteral(
                      "SELECT count(*) FROM sqlite_master WHERE name='sample_payload'")}) {
                if (!query.exec(sql) || !query.next()) {
                    result.error = query.lastError().text();
                    return result;
                }
                result.payload += query.value(0).toByteArray() + ',';
                query.finish();
            }
            return result;
        });
        QVERIFY(settings.error.isEmpty());
        QCOMPARE(settings.payload, QByteArray("17,1,-4096,2,1,"));
        close(database);
        QVERIFY(QDir(root).entryList({"run-*"}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
#endif
    }
    void failedRecoveryKeepsReadableSourceAndLaterRetryDoesNotReplayWrite()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows does not permit this fixture to unlink open handles");
#else
        QTemporaryDir directory;
        const auto root = directory.filePath("session");
        SessionDatabase database(root);
        const auto original = seed(database, QByteArray("retained"));
        QVERIFY(original.error.isEmpty());
        QVERIFY(QDir(root).removeRecursively());
        QFile blocker(root);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        const auto failed = readAndAppend(database);
        QVERIFY(!failed.error.isEmpty());
        QVERIFY(!failed.error.contains("readonly"));
        QVERIFY(failed.sourceOpen);
        const auto retained = run(database, [](QSqlDatabase &db, const QString &) {
            QSqlQuery query(db);
            Result result;
            if (!query.exec("SELECT count(*) FROM sample") || !query.next())
                result.error = query.lastError().text();
            else
                result.value = query.value(0).toInt();
            return result;
        });
        QVERIFY(retained.error.isEmpty());
        QCOMPARE(retained.value, 1);
        QVERIFY(blocker.remove());
        const auto retried = readAndAppend(database);
        QVERIFY2(retried.error.isEmpty(), qPrintable(retried.error));
        QCOMPARE(retried.payload, QByteArray("retained"));
        QCOMPARE(retried.value, 8);
        close(database);
#endif
    }
    void executesOffGuiBoundsAdmissionAndDropsDestroyedReceiver()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.filePath("会话缓存"), {100, 1});
        auto consumer = std::make_unique<QObject>();
        int callbacks = 0;
        QVERIFY(database.submit(
            consumer.get(), [](QSqlDatabase &, const QString &) { return 1; },
            [&](int) { ++callbacks; }, 1000));
        QCOMPARE(database.pendingBytes(), quint64(1000));
        QVERIFY(!database.submit(
            consumer.get(), [](QSqlDatabase &, const QString &) { return 2; },
            [&](int) { ++callbacks; }, 1));
        consumer.reset();
        QTRY_COMPARE(database.pendingJobs(), 0);
        QCOMPARE(database.pendingBytes(), quint64(0));
        QCOMPARE(callbacks, 0);
        const auto result = execute(database);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.worker);
        QCOMPARE(result.value, 42);
        close(database);
        QVERIFY(QDir(directory.filePath("会话缓存"))
                    .entryList({"run-*"}, QDir::Dirs | QDir::NoDotAndDotDot)
                    .isEmpty());
    }
    void liveDirectoriesStayAndAbandonedDirectoriesAreRemoved()
    {
        QTemporaryDir directory;
        SessionDatabase first(directory.path()), second(directory.path());
        QVERIFY(execute(first).error.isEmpty());
        QVERIFY(QDir().mkpath(directory.filePath("run-abandoned")));
        QVERIFY(execute(second).error.isEmpty());
        QVERIFY(!QDir(directory.filePath("run-abandoned")).exists());
        QCOMPARE(
            QDir(directory.path()).entryList({"run-*"}, QDir::Dirs | QDir::NoDotAndDotDot).size(),
            2);
        close(first);
        QCOMPARE(
            QDir(directory.path()).entryList({"run-*"}, QDir::Dirs | QDir::NoDotAndDotDot).size(),
            1);
        QVERIFY(execute(second).error.isEmpty());
        close(second);
    }
    void failuresAreDeliveredAndShutdownDrainsAcceptedWork()
    {
        QTemporaryDir directory;
        QFile blocked(directory.filePath("blocked"));
        QVERIFY(blocked.open(QIODevice::WriteOnly));
        blocked.close();
        SessionDatabase database(blocked.fileName());
        const auto result = execute(database);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.worker);
        close(database);
        QVERIFY(
            !database.submit(this, [](QSqlDatabase &, const QString &) { return 1; }, [](int) {}));
    }
};
QTEST_GUILESS_MAIN(SessionDatabaseTest)
#include "SessionDatabaseTest.moc"
