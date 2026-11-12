#ifdef _WIN32
#include <winsock2.h>
#endif
#include "DiagnosticService.h"
#include "LogService.h"
#include "LogFiles.h"
#include "echo_ffi.h"
#include <QAbstractItemModelTester>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QLockFile>
#include <QtTest>
#include <client/crash_report_database.h>
#include <util/misc/metrics.h>

using namespace EchoMusic;
class DiagnosticServiceTest final : public QObject
{
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static QString session(const QString &logs, const QString &id,
                           const QString &kind = "unclean_exit")
    {
        const auto log = id + ".log";
        QJsonObject started{{"event", "session_started"},
                            {"timestamp", "2026-11-10T10:20:30.000Z"},
                            {"build", "version=0.1.3 build=fixture-build arch=arm64 Qt=6.12.0"},
                            {"os", "Fixture OS"},
                            {"runLog", logs + '/' + log}};
        auto bytes = QJsonDocument(started).toJson(QJsonDocument::Compact) + '\n';
        if (kind == "fatal")
            bytes += "{\"event\":\"fatal\",\"reason\":\"qt_fatal\"}\n";
        if (!write(logs + "/crashes/" + id + ".jsonl", bytes))
            return {};
        return id;
    }
    bool unpack(const QString &archive, const QString &target)
    {
        QDir().mkpath(target);
        char error[1024]{};
        const auto ok =
            echo_plugin_extract_zip(archive.toUtf8().constData(), target.toUtf8().constData(),
                                    reinterpret_cast<uint8_t *>(error), sizeof(error));
        if (!ok)
            qWarning() << error;
        return ok;
    }
    bool validManifest(const QString &root)
    {
        const auto manifest = QJsonDocument::fromJson(read(root + "/manifest.json")).object();
        if (manifest.value("formatVersion") != 1)
            return false;
        for (const auto &value : manifest.value("files").toArray()) {
            const auto file = value.toObject();
            const auto bytes = read(root + '/' + file.value("path").toString());
            if (bytes.size() != file.value("size").toInteger() ||
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
                    file.value("sha256").toString().toLatin1())
                return false;
        }
        return true;
    }
private slots:
    void historyDistinguishesLiveFatalAndUncleanSessions()
    {
        QTemporaryDir temporary;
        const auto logs = temporary.filePath("logs");
        session(logs, "echomusicnext-20261110T102030000-11");
        session(logs, "echomusicnext-20261110T102030001-12", "fatal");
        const auto live = session(logs, "echomusicnext-20261110T102030002-13");
        QLockFile lease(logs + "/crashes/" + live + ".running");
        QVERIFY(lease.tryLock(0));
        QVERIFY(write(logs + "/crashes/unrelated.jsonl", "not an app session"));
        DiagnosticService service(logs);
        QAbstractItemModelTester model(&service,
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        service.refresh();
        QTRY_VERIFY(!service.loading());
        QCOMPARE(service.count(), 1);
        QVERIFY(service.record("echomusicnext-20261110T102030000-11").isEmpty());
        QCOMPARE(service.record("echomusicnext-20261110T102030001-12").value("kind").toString(),
                 "fatal");
        QVERIFY(service.record(live).isEmpty());
        QSignalSpy notices(&service, &DiagnosticService::startupNotice);
        service.checkStartupNotice();
        QCOMPARE(notices.size(), 1);
        service.checkStartupNotice();
        QCOMPARE(notices.size(), 1);
        DiagnosticService restart(logs);
        QSignalSpy repeated(&restart, &DiagnosticService::startupNotice);
        restart.checkStartupNotice();
        QTRY_VERIFY(!restart.loading());
        QCOMPARE(repeated.size(), 0);
        // A late native summary upgrades the same fatal event without another notice.
        const auto summary =
            QJsonDocument(
                QJsonObject{
                    {"event", "native_crash"},
                    {"timestamp", "2026-11-10T10:20:30.000Z"},
                    {"build",
                     QJsonObject{{"sessionReport",
                                  logs + "/crashes/echomusicnext-20261110T102030001-12.jsonl"},
                                 {"runLog", logs + "/echomusicnext-20261110T102030001-12.log"}}}})
                .toJson();
        QVERIFY(
            write(logs + "/crashes/11111111-1111-4111-8111-111111111111.summary.json", summary));
        DiagnosticService upgrade(logs);
        QSignalSpy upgradedNotice(&upgrade, &DiagnosticService::startupNotice);
        upgrade.checkStartupNotice();
        QTRY_VERIFY(!upgrade.loading());
        QCOMPARE(upgrade.count(), 1);
        QCOMPARE(upgradedNotice.size(), 0);
        session(logs, "echomusicnext-20261110T102030003-14", "fatal");
        DiagnosticService next(logs);
        QSignalSpy newNotice(&next, &DiagnosticService::startupNotice);
        next.checkStartupNotice();
        QTRY_VERIFY(!next.loading());
        QCOMPARE(newNotice.size(), 1);
    }
    void noCrashNoticeForForcedExit()
    {
        QTemporaryDir temporary;
        session(temporary.path(), "echomusicnext-20261110T102030000-11");
        DiagnosticService service(temporary.path());
        QSignalSpy notices(&service, &DiagnosticService::startupNotice);
        service.checkStartupNotice();
        QTRY_VERIFY(!service.loading());
        QCOMPARE(service.count(), 0);
        QCOMPARE(notices.size(), 0);
        const std::atomic_bool notCancelled{false};
        QCOMPARE(DiagnosticRecords::scan(temporary.path(), notCancelled).records.size(), 1);
        QVERIFY(QFileInfo::exists(
            temporary.filePath("crashes/echomusicnext-20261110T102030000-11.jsonl")));
    }
    void currentExportFlushesAndExcludesPrivateData()
    {
        QTemporaryDir temporary;
        const auto logs = temporary.filePath("logs");
        LogService logging(logs, 2, false);
        auto *service = logging.diagnostics();
        QVERIFY(service);
        QVERIFY(write(temporary.filePath("account.sqlite"), "private-account-data"));
        QVERIFY(write(logs + "/unrelated.log", "unrelated-private-data"));
        qWarning("diagnostic-flush-boundary 中文");
        const auto zip = temporary.filePath("current.zip");
        service->exportCurrent(QUrl::fromLocalFile(zip), "t1=do-not-export");
        QTRY_VERIFY_WITH_TIMEOUT(!service->busy(), 20000);
        QVERIFY2(service->errorString().isEmpty(), qPrintable(service->errorString()));
        QVERIFY(unpack(zip, temporary.filePath("unpacked")));
        QVERIFY(validManifest(temporary.filePath("unpacked")));
        const auto body = read(temporary.filePath("unpacked/logs/") + sessionLogFileName());
        QVERIFY(body.contains("diagnostic-flush-boundary"));
        QVERIFY(body.contains(QStringLiteral("中文").toUtf8()));
        QVERIFY(!QFileInfo::exists(temporary.filePath("unpacked/logs/unrelated.log")));
        QVERIFY(!read(temporary.filePath("unpacked/report.txt")).contains("do-not-export"));
        const auto context = read(temporary.filePath("unpacked/context.json"));
        QVERIFY(!context.contains("do-not-export"));
        QVERIFY(!context.contains("private-account-data"));
    }
    void perEventIncludesRotationsAndReportsMissingEvidence()
    {
        QTemporaryDir temporary;
        const auto logs = temporary.filePath("logs");
        const auto old = session(logs, "echomusicnext-20261110T102030000-11", "fatal");
        QVERIFY(write(logs + '/' + old + ".log", "before-crash\npassword=do-not-export\n"));
        QVERIFY(write(logs + '/' + old + ".1.log", "older-segment\n"));
        QVERIFY(write(logs + "/echomusicnext-20261111T102030000-12.log", "wrong-session\n"));
        DiagnosticService service(logs);
        service.refresh();
        QTRY_VERIFY(!service.loading());
        const auto zip = temporary.filePath("event.zip");
        service.exportCrash(old, QUrl::fromLocalFile(zip), false);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY2(service.errorString().isEmpty(), qPrintable(service.errorString()));
        const auto target = temporary.filePath("event");
        QVERIFY(unpack(zip, target));
        QVERIFY(validManifest(target));
        QVERIFY(read(target + "/logs/" + old + ".log").contains("before-crash"));
        QVERIFY(!read(target + "/logs/" + old + ".log").contains("do-not-export"));
        QVERIFY(read(target + "/logs/" + old + ".1.log").contains("older-segment"));
        QVERIFY(!QFileInfo::exists(target + "/logs/echomusicnext-20261111T102030000-12.log"));
        QVERIFY(QFile::remove(logs + '/' + old + ".log"));
        service.exportCrash(old, QUrl::fromLocalFile(temporary.filePath("missing.zip")), false);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY(service.errorString().isEmpty());
        QVERIFY(unpack(temporary.filePath("missing.zip"), temporary.filePath("missing")));
        QVERIFY(!QJsonDocument::fromJson(read(temporary.filePath("missing/manifest.json")))
                     .object()
                     .value("missing")
                     .toArray()
                     .isEmpty());
    }
    void retainedCrashProtectsOldRotatedLogsAndSidecars()
    {
        QTemporaryDir temporary;
        const auto logs = temporary.filePath("logs");
        const auto id = session(logs, "echomusicnext-2026-11-10_10-20-30-000-11", "fatal");
        for (const auto &suffix : {QString(".log"), QString(".1.log")}) {
            QFile file(logs + '/' + id + suffix);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("old but associated crash evidence") > 0);
            QVERIFY(file.flush());
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-10),
                                     QFileDevice::FileModificationTime));
        }
        {
            LogService logging(logs, 2, false);
        }
        QVERIFY(QFileInfo::exists(logs + '/' + id + ".log"));
        QVERIFY(QFileInfo::exists(logs + '/' + id + ".1.log"));
        // Expired owned sidecars are pruned; unrelated and live evidence survives.
        QFile expired(logs + "/crashes/" + id + ".jsonl");
        QVERIFY(expired.open(QIODevice::ReadWrite));
        QVERIFY(expired.setFileTime(QDateTime::currentDateTime().addDays(-10),
                                    QFileDevice::FileModificationTime));
        expired.close();
        QVERIFY(write(logs + "/crashes/notes.summary.json", "unrelated"));
        QFile unrelated(logs + "/crashes/notes.summary.json");
        QVERIFY(unrelated.open(QIODevice::ReadWrite));
        QVERIFY(unrelated.setFileTime(QDateTime::currentDateTime().addDays(-10),
                                      QFileDevice::FileModificationTime));
        unrelated.close();
        DiagnosticRecords::pruneSidecars(logs);
        QVERIFY(!QFileInfo::exists(expired.fileName()));
        QVERIFY(QFileInfo::exists(unrelated.fileName()));
    }
    void cancellationAndBadDestinationPreserveExistingFile()
    {
        QTemporaryDir temporary;
        const auto logs = temporary.filePath("logs");
        const auto id = session(logs, "echomusicnext-20261110T102030000-11", "fatal");
        QVERIFY(write(logs + '/' + id + ".log", QByteArray(5 * 1024 * 1024, 'x')));
        const auto destination = temporary.filePath("existing.zip");
        QVERIFY(write(destination, "original"));
        DiagnosticService service(logs);
        service.refresh();
        QTRY_VERIFY(!service.loading());
        service.exportCrash(id, QUrl::fromLocalFile(destination), false);
        service.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QCOMPARE(read(destination), "original");
        QVERIFY(service.exportedFile().isEmpty());
        service.exportCrash(id, QUrl::fromLocalFile(logs + '/' + id + ".log"), false);
        QVERIFY(!service.errorString().isEmpty());
        QCOMPARE(QFileInfo(logs + '/' + id + ".log").size(), 5 * 1024 * 1024);
        service.exportCrash(id, QUrl("https://example.invalid/output.zip"), false);
        QVERIFY(!service.errorString().isEmpty());
        service.exportCrash(id, QUrl::fromLocalFile(temporary.filePath("missing/dir/out.zip")),
                            false);
        QVERIFY(!service.errorString().isEmpty());
    }
    void actualCrashExportsDumpAndAttachmentAfterOriginalLogRemoval()
    {
        QTemporaryDir temporary;
        const auto logs = temporary.filePath("logs");
        QProcess probe;
        auto executable = QCoreApplication::applicationDirPath() + "/echo-crash-probe";
#ifdef Q_OS_WIN
        executable += ".exe";
#endif
        probe.start(executable, {"fatal-logged", logs});
        QVERIFY(probe.waitForFinished(25000));
        QVERIFY(probe.exitCode() != 0 || probe.exitStatus() == QProcess::CrashExit);
        DiagnosticService service(logs);
        for (int i = 0; i < 50; ++i) {
            service.refresh();
            QTRY_VERIFY(!service.loading());
            if (service.count() &&
                service.data(service.index(0), Qt::UserRole + 1).toMap().value("hasDump").toBool())
                break;
            QTest::qWait(50);
        }
        QCOMPARE(service.count(), 1);
        const auto record = service.data(service.index(0), Qt::UserRole + 1).toMap();
        QVERIFY(record.value("hasDump").toBool());
        QCOMPARE(record.value("kind").toString(), "native_crash");
        for (const auto &file : QDir(logs).entryList({"*.log"}, QDir::Files))
            QVERIFY(QFile::remove(logs + '/' + file));
        const auto zip = temporary.filePath("native.zip");
        service.exportCrash(record.value("id").toString(), QUrl::fromLocalFile(zip), true);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY2(service.errorString().isEmpty(), qPrintable(service.errorString()));
        const auto target = temporary.filePath("native");
        QVERIFY(unpack(zip, target));
        QVERIFY(validManifest(target));
        QVERIFY(read(target + "/crash/report.dmp").startsWith("MDMP"));
        QVERIFY(!QDir(target + "/logs").entryList({"attached-*.log"}, QDir::Files).isEmpty());
        for (const auto &name : QDir(target + "/logs").entryList(QDir::Files))
            QVERIFY(!read(target + "/logs/" + name).contains("do-not-store"));
        service.exportCrash(record.value("id").toString(),
                            QUrl::fromLocalFile(temporary.filePath("without.zip")), false);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY(service.errorString().isEmpty());
        QVERIFY(unpack(temporary.filePath("without.zip"), temporary.filePath("without")));
        QVERIFY(!QFileInfo::exists(temporary.filePath("without/crash/report.dmp")));
        const auto databasePath = logs + "/crashes/database";
#ifdef Q_OS_WIN
        auto database =
            crashpad::CrashReportDatabase::Initialize(base::FilePath(databasePath.toStdWString()));
#else
        auto database = crashpad::CrashReportDatabase::Initialize(
            base::FilePath(QFile::encodeName(databasePath).toStdString()));
#endif
        QVERIFY(database);
        crashpad::UUID uuid;
        QVERIFY(uuid.InitializeFromString(record.value("id").toString().toStdString()));
        QCOMPARE(database->SkipReportUpload(
                     uuid, crashpad::Metrics::CrashSkippedReason::kUploadsDisabled),
                 crashpad::CrashReportDatabase::kNoError);
        service.refresh();
        QTRY_VERIFY(!service.loading());
        service.exportCrash(record.value("id").toString(),
                            QUrl::fromLocalFile(temporary.filePath("completed.zip")), true);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY2(service.errorString().isEmpty(), qPrintable(service.errorString()));
        QVERIFY(unpack(temporary.filePath("completed.zip"), temporary.filePath("completed")));
        QVERIFY(read(temporary.filePath("completed/crash/report.dmp")).startsWith("MDMP"));
        QVERIFY(!QDir(temporary.filePath("completed/logs"))
                     .entryList({"attached-*.log"}, QDir::Files)
                     .isEmpty());
        crashpad::CrashReportDatabase::Report after;
        QCOMPARE(database->LookUpCrashReport(uuid, &after),
                 crashpad::CrashReportDatabase::kNoError);
        QVERIFY(!after.uploaded);
    }
};
QTEST_GUILESS_MAIN(DiagnosticServiceTest)
#include "DiagnosticServiceTest.moc"
