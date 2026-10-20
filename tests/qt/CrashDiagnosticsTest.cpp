#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include "CrashDiagnostics.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QtTest>
#include "client/crash_report_database.h"
#include <vector>
class CrashDiagnosticsTest : public QObject
{
    Q_OBJECT
    QString probe() const
    {
        return QCoreApplication::applicationDirPath() + "/echo-crash-probe"
#ifdef Q_OS_WIN
                                                        ".exe"
#endif
            ;
    }
    QByteArray reports(const QString &directory)
    {
        QByteArray bytes;
        const QDir root(directory + "/crashes");
        for (const auto &file : root.entryList({"*.jsonl"}, QDir::Files)) {
            QFile report(root.filePath(file));
            if (!report.open(QIODevice::ReadOnly))
                return {};
            const auto content = report.readAll();
            for (const auto &line : content.split('\n'))
                if (!line.isEmpty() && !QJsonDocument::fromJson(line).isObject())
                    return {};
            bytes += content;
        }
        return bytes;
    }
private slots:
    void fatalArtifactsSurviveWithoutEventLoop_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto *name : {"fatal", "early-fatal", "late-fatal", "terminate", "abort",
                                 "access", "worker", "startup", "teardown"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void fatalArtifactsSurviveWithoutEventLoop()
    {
        QFETCH(QString, mode);
        QTemporaryDir directory;
        QProcess child;
        child.start(probe(), {mode, directory.path()});
        QVERIFY(child.waitForStarted());
        QVERIFY2(child.waitForFinished(25000), qPrintable(child.errorString()));
        QVERIFY(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0);
        auto bytes = reports(directory.path());
        QVERIFY(bytes.contains("session_started"));
        QVERIFY(bytes.contains("build="));
        QVERIFY(!bytes.contains("do-not-store"));
        if (mode.endsWith("fatal")) {
            QVERIFY(bytes.contains("qt_fatal_message"));
            QVERIFY(bytes.contains("redacted"));
        }
        // Crashpad writes a dump in its staging directory before registering
        // it as pending. File existence alone is not a completed capture.
        const auto databaseDirectory = directory.path() + "/crashes/database";
#ifdef Q_OS_WIN
        const base::FilePath databasePath(databaseDirectory.toStdWString());
#else
        const base::FilePath databasePath(QFile::encodeName(databaseDirectory).toStdString());
#endif
        auto database = crashpad::CrashReportDatabase::Initialize(databasePath);
        QVERIFY(database);
        QTRY_VERIFY(([&] {
            std::vector<crashpad::CrashReportDatabase::Report> pending, completed;
            return database->GetPendingReports(&pending) ==
                       crashpad::CrashReportDatabase::kNoError &&
                   database->GetCompletedReports(&completed) ==
                       crashpad::CrashReportDatabase::kNoError &&
                   pending.size() + completed.size() == 1;
        })());
        QStringList dumps;
        QDirIterator iterator(directory.path() + "/crashes", {"*.dmp"}, QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            dumps.append(iterator.next());
        QVERIFY2(dumps.size() == 1, child.readAllStandardError().constData());
        QFile dump(dumps.first());
        QVERIFY(dump.open(QIODevice::ReadOnly));
        auto memory = dump.readAll();
        QVERIFY(memory.startsWith("MDMP"));
        const auto summary = EchoMusic::CrashDiagnostics::summarizeDump(dumps.first());
        QVERIFY(!summary.isEmpty());
        QVERIFY(summary["thread"].toInteger() != 0);
        QVERIFY(summary["threadCount"].toInt() > 0);
        QVERIFY(summary["capturedStackBytes"].toInteger() > 0);
        QVERIFY(summary["instructionPointer"].toString() != "0x0");
        QVERIFY(summary["build"].toObject().contains("buildId"));
        const auto runLog = summary["build"].toObject().value("runLog").toString();
        QVERIFY(QRegularExpression(
                    R"(^echomusicnext-\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}-\d{3}-\d+\.log$)")
                    .match(QFileInfo(runLog).fileName())
                    .hasMatch());
        for (const auto &line : bytes.split('\n')) {
            const auto record = QJsonDocument::fromJson(line).object();
            if (record.value("event") == "session_started")
                QCOMPARE(record.value("runLog").toString(), runLog);
        }
#ifdef Q_OS_WIN
        if (mode == "access" || mode == "worker" || mode == "startup" || mode == "teardown")
            QCOMPARE(summary["exceptionCode"].toString(), QString("0xc0000005"));
#endif
        QProcess restart;
        restart.start(probe(), {"clean", directory.path()});
        QVERIFY(restart.waitForFinished(10000));
        QCOMPARE(restart.exitCode(), 0);
        // QProcess clears its PID after exit; inspect all run logs instead.
        QByteArray runLogs;
        for (const auto &name : QDir(directory.path()).entryList({"*.log"}, QDir::Files)) {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            runLogs += file.readAll();
        }
        QVERIFY(runLogs.contains("Previous native crash:"));
        QVERIFY(runLogs.contains("exceptionCode"));
        QVERIFY(reports(directory.path()).contains("previous_unclean_exit"));
        QVERIFY(
            QDir(directory.path() + "/crashes").entryList({"*.running"}, QDir::Files).isEmpty());
    }
    void cleanExitAndForcedExitAreDistinguished()
    {
        QTemporaryDir directory;
        for (const auto *mode : {"clean", "unclean", "clean"}) {
            QProcess child;
            child.start(probe(), {QString::fromLatin1(mode), directory.path()});
            QVERIFY(child.waitForFinished(10000));
            QCOMPARE(child.exitCode(), QByteArray(mode) == "unclean" ? 77 : 0);
            if (QByteArray(mode) == "clean" && reports(directory.path()).isEmpty())
                QVERIFY(QDir(directory.path() + "/crashes")
                            .entryList({"*.jsonl", "*.running"}, QDir::Files)
                            .isEmpty());
        }
        const auto bytes = reports(directory.path());
        QVERIFY(bytes.contains("previous_unclean_exit"));
        QVERIFY(!bytes.contains("native_crash"));
    }
    void concurrentLiveSessionIsNotReportedAsDead()
    {
        QTemporaryDir directory;
        QProcess live;
        live.start(probe(), {"hold", directory.path()});
        QVERIFY(live.waitForReadyRead(10000));
        QVERIFY(live.readAllStandardOutput().contains("ready"));
        QProcess other;
        other.start(probe(), {"clean", directory.path()});
        QVERIFY(other.waitForFinished(10000));
        QCOMPARE(other.exitCode(), 0);
        QCOMPARE(live.state(), QProcess::Running);
        QVERIFY(!reports(directory.path()).contains("previous_unclean_exit"));
        live.kill();
        QVERIFY(live.waitForFinished(10000));
        QProcess restart;
        restart.start(probe(), {"clean", directory.path()});
        QVERIFY(restart.waitForFinished(10000));
        QCOMPARE(restart.exitCode(), 0);
        QVERIFY(reports(directory.path()).contains("previous_unclean_exit"));
    }
};
QTEST_GUILESS_MAIN(CrashDiagnosticsTest)
#include "CrashDiagnosticsTest.moc"
