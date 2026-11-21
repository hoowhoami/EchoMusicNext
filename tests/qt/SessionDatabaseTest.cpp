#include "SessionDatabase.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
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
private slots:
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
