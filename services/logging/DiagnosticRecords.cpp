#ifdef _WIN32
#include <winsock2.h>
#endif
#include "DiagnosticRecords.h"
#include "CrashDiagnostics.h"
#include "LogRedaction.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSet>
#include <QObject>
#include <QTimeZone>
#include <client/crash_report_database.h>
#include <util/file/file_reader.h>
#include <algorithm>

namespace EchoMusic
{
namespace
{
base::FilePath nativePath(const QString &path)
{
#ifdef Q_OS_WIN
    return base::FilePath(QDir::toNativeSeparators(path).toStdWString());
#else
    return base::FilePath(QFile::encodeName(path).toStdString());
#endif
}
QString qtPath(const base::FilePath &path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.value());
#else
    return QFile::decodeName(path.value().c_str());
#endif
}
QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool liveSession(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    QLockFile lease(path);
    lease.setStaleLockTime(0);
    return !lease.tryLock(0);
}
QString ownedName(const QString &value, const QString &suffix)
{
    const auto name = QFileInfo(value).fileName();
    static const QRegularExpression identity(
        "^echomusicnext-[0-9T_-]+(?:\\.[0-9]+)?\\.(log|jsonl)$");
    return name.endsWith(suffix) && identity.match(name).hasMatch() ? name : QString();
}
void adoptNative(DiagnosticRecord &record, const QJsonObject &json)
{
    const auto build = json.value("build").toObject();
    record.kind = "native_crash";
    record.timestamp = json.value("timestamp").toString();
    record.version = build.value("version").toString();
    record.buildId = build.value("buildId").toString();
    record.arch = build.value("arch").toString();
    record.session = ownedName(build.value("sessionReport").toString(), ".jsonl");
    record.runLog = ownedName(build.value("runLog").toString(), ".log");
    // Do not expose absolute paths, annotations, environment or command lines.
    for (const auto *key : {"exceptionCode", "exceptionInfo", "module", "moduleOffset",
                            "moduleVersion", "debugId", "debugAge", "codeId", "faultAddress",
                            "instructionPointer", "threadCount", "capturedStackBytes"})
        if (json.contains(key))
            record.exception.insert(key, json.value(key));
}
void adoptSession(DiagnosticRecord &record, const QString &directory)
{
    if (record.session.isEmpty())
        return;
    QFile file(QDir(directory).filePath("crashes/" + record.session));
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly))
        return;
    qint64 read = 0;
    while (!file.atEnd() && read < 64 * 1024) {
        const auto line = file.readLine(8193);
        read += line.size();
        const auto event = QJsonDocument::fromJson(line).object();
        if (event.value("event") == "session_started") {
            if (record.timestamp.isEmpty())
                record.timestamp = event.value("timestamp").toString();
            if (record.runLog.isEmpty())
                record.runLog = ownedName(event.value("runLog").toString(), ".log");
            record.os = event.value("os").toString();
            const auto build = event.value("build").toString();
            const auto field = [&](const QString &key) {
                return QRegularExpression("(?:^| )" + key + "=([^ ]+)").match(build).captured(1);
            };
            if (record.version.isEmpty())
                record.version = field("version");
            if (record.buildId.isEmpty())
                record.buildId = field("build");
            if (record.arch.isEmpty())
                record.arch = field("arch");
        }
        if (event.value("event") == "fatal" && record.kind != "native_crash")
            record.kind = "fatal";
    }
}
using Database = crashpad::CrashReportDatabase;
std::vector<Database::Report> reports(Database &database, QString &error)
{
    std::vector<Database::Report> pending, completed;
    if (database.GetPendingReports(&pending) != Database::kNoError ||
        database.GetCompletedReports(&completed) != Database::kNoError)
        error = QObject::tr("无法读取部分崩溃转储，仍可查看会话记录");
    pending.insert(pending.end(), completed.begin(), completed.end());
    return pending;
}
} // namespace
DiagnosticScan DiagnosticRecords::scan(const QString &directory, const std::atomic_bool &cancel)
{
    DiagnosticScan result;
    const QDir root(QDir(directory).filePath("crashes"));
    QSet<QString> sessions, ids;
    if (QFileInfo::exists(root.filePath("database"))) {
        auto database = Database::Initialize(nativePath(root.filePath("database")));
        if (!database)
            result.error = QObject::tr("崩溃转储数据库不可用，仍可查看会话记录");
        else
            for (const auto &report : reports(*database, result.error)) {
                if (cancel)
                    return result;
                DiagnosticRecord record;
                record.id = QString::fromStdString(report.uuid.ToString());
                record.dumpPath = qtPath(report.file_path);
                record.bytes = report.total_size;
                record.hasDump = QFileInfo(record.dumpPath).isFile();
                auto json = readJson(root.filePath(record.id + ".summary.json"));
                if (json.isEmpty())
                    json = CrashDiagnostics::summarizeDump(record.dumpPath);
                adoptNative(record, json);
                if (record.timestamp.isEmpty())
                    record.timestamp =
                        QDateTime::fromSecsSinceEpoch(report.creation_time, QTimeZone::UTC)
                            .toString(Qt::ISODateWithMs);
                adoptSession(record, directory);
                if (!record.session.isEmpty())
                    sessions.insert(record.session);
                ids.insert(record.id);
                result.records.append(record);
            }
    }
    // Summaries survive a missing/pruned dump until the event retention expires.
    for (const auto &name : root.entryList({"*.summary.json"}, QDir::Files | QDir::NoSymLinks)) {
        if (cancel)
            return result;
        const auto id = name.chopped(13);
        crashpad::UUID uuid;
        if (!uuid.InitializeFromString(id.toStdString()))
            continue;
        if (ids.contains(id))
            continue;
        const auto json = readJson(root.filePath(name));
        if (json.value("event") != "native_crash")
            continue;
        DiagnosticRecord record;
        record.id = id;
        adoptNative(record, json);
        adoptSession(record, directory);
        if (!record.session.isEmpty())
            sessions.insert(record.session);
        result.records.append(record);
    }
    for (const auto &name :
         root.entryList({"echomusicnext-*.jsonl"}, QDir::Files | QDir::NoSymLinks)) {
        if (cancel)
            return result;
        if (sessions.contains(name) || liveSession(root.filePath(name.chopped(6) + ".running")))
            continue;
        DiagnosticRecord record;
        record.id = name.chopped(6);
        record.session = ownedName(name, ".jsonl");
        if (record.session.isEmpty())
            continue;
        record.kind = "unclean_exit";
        adoptSession(record, directory);
        if (record.timestamp.isEmpty())
            continue;
        result.records.append(record);
    }
    std::sort(result.records.begin(), result.records.end(), [](const auto &a, const auto &b) {
        return a.timestamp == b.timestamp ? a.id > b.id : a.timestamp > b.timestamp;
    });
    return result;
}
QJsonObject DiagnosticRecords::summary(const DiagnosticRecord &record)
{
    return {{"id", record.id},
            {"kind", record.kind},
            {"timestamp", record.timestamp},
            {"version", record.version},
            {"buildId", record.buildId},
            {"os", record.os},
            {"arch", record.arch},
            {"exception", record.exception},
            {"hasDump", record.hasDump}};
}
QStringList DiagnosticRecords::captureNative(const QString &directory,
                                             const DiagnosticRecord &record, const QString &staging,
                                             bool dump, const std::atomic_bool &cancel,
                                             QStringList &missing)
{
    QStringList captured;
    auto database = Database::Initialize(nativePath(QDir(directory).filePath("crashes/database")));
    crashpad::UUID id;
    std::unique_ptr<const Database::UploadReport> lease;
    if (!database || !id.InitializeFromString(record.id.toStdString()) ||
        database->GetReportForUploading(id, &lease, false) != Database::kNoError) {
        // Completed reports cannot be checked out as pending. Do not move them
        // back to pending or request an upload just to obtain local evidence.
        std::vector<Database::Report> completed;
        bool available = false;
        if (database && database->GetCompletedReports(&completed) == Database::kNoError)
            for (const auto &report : completed)
                if (report.uuid == id)
                    available = true;
        const auto copyFile = [&](const QString &source, const QString &name) {
            const auto canonical = QFileInfo(source).canonicalFilePath();
            const auto databaseRoot =
                QFileInfo(QDir(directory).filePath("crashes/database")).canonicalFilePath();
            QFile input(source);
            QFile output(QDir(staging).filePath(name));
            if (!available || canonical.isEmpty() || !canonical.startsWith(databaseRoot + '/') ||
                QFileInfo(source).isSymLink() || !input.open(QIODevice::ReadOnly) ||
                input.size() > 256 * 1024 * 1024 ||
                !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                return false;
            output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            while (!input.atEnd()) {
                if (cancel)
                    return false;
                const auto bytes = input.read(65536);
                if (bytes.isEmpty() || output.write(bytes) != bytes.size())
                    return false;
            }
            return output.flush();
        };
        if (dump) {
            if (copyFile(record.dumpPath, "crash/report.dmp"))
                captured.append("crash/report.dmp");
            else {
                QFile::remove(QDir(staging).filePath("crash/report.dmp"));
                missing.append(QObject::tr("崩溃转储不存在或正被占用"));
            }
        }
        // The pinned Crashpad database maps both completed and pending report
        // attachments to database/attachments/<uuid>/<name>. Only these two
        // allowlisted names may be read; no directory traversal or DB mutation.
        for (const auto &name : {record.runLog, record.session}) {
            if (name.isEmpty())
                continue;
            const auto target =
                name.endsWith(".log") ? "logs/attached-" + name : "crash/session.jsonl";
            if (copyFile(QDir(directory).filePath("crashes/database/attachments/" + record.id +
                                                  '/' + name),
                         target))
                captured.append(target);
            else
                QFile::remove(QDir(staging).filePath(target));
        }
        if (captured.isEmpty())
            missing.append(QObject::tr("Crashpad 附件不可用，将尝试原会话日志"));
        return captured;
    }
    const auto copy = [&](crashpad::FileReader *reader, const QString &name) {
        QFile file(QDir(staging).filePath(name));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        char bytes[65536];
        qint64 total = 0;
        for (;;) {
            if (cancel)
                return false;
            const auto length = reader->Read(bytes, sizeof(bytes));
            if (length < 0 || (total += length) > 256 * 1024 * 1024)
                return false;
            if (!length)
                return true;
            if (file.write(bytes, length) != length)
                return false;
        }
    };
    if (dump) {
        if (copy(lease->Reader(), "crash/report.dmp"))
            captured.append("crash/report.dmp");
        else {
            QFile::remove(QDir(staging).filePath("crash/report.dmp"));
            missing.append(QObject::tr("无法读取完整崩溃转储"));
        }
    }
    for (const auto &[name, reader] : lease->GetAttachments()) {
        const auto attachment = QString::fromStdString(name);
        if (attachment != record.runLog && attachment != record.session)
            continue;
        const auto target =
            attachment.endsWith(".log") ? "logs/attached-" + attachment : "crash/session.jsonl";
        if (copy(reader, target))
            captured.append(target);
        else {
            QFile::remove(QDir(staging).filePath(target));
            missing.append(QObject::tr("无法读取附件：%1").arg(attachment));
        }
    }
    return captured;
}
void DiagnosticRecords::pruneSidecars(const QString &directory)
{
    const QDir root(QDir(directory).filePath("crashes"));
    const auto cutoff = QDateTime::currentDateTimeUtc().addDays(-7);
    for (const auto &info : root.entryInfoList({"*.summary.json", "echomusicnext-*.jsonl"},
                                               QDir::Files | QDir::NoSymLinks)) {
        if (info.lastModified() >= cutoff)
            continue;
        if (info.fileName().endsWith(".summary.json")) {
            crashpad::UUID id;
            if (!id.InitializeFromString(info.fileName().chopped(13).toStdString()) ||
                readJson(info.absoluteFilePath()).value("event") != "native_crash")
                continue;
        } else if (ownedName(info.fileName(), ".jsonl").isEmpty())
            continue;
        if (info.fileName().endsWith(".jsonl") &&
            liveSession(root.filePath(info.completeBaseName() + ".running")))
            continue;
        // Only sidecars owned by this collector; Crashpad owns dump/attachment deletion.
        QFile::remove(info.absoluteFilePath());
    }
}
} // namespace EchoMusic
