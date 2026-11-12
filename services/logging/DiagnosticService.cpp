#include "DiagnosticService.h"
#include "CrashDiagnostics.h"
#include "LogFiles.h"
#include "LogRedaction.h"
#include "echo_ffi.h"
#include <QtConcurrentRun>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QSet>
#include <QRegularExpression>
#include <QThread>

namespace EchoMusic
{
namespace
{
constexpr qint64 exportLimit = 512 * 1024 * 1024;
QString kindText(const QString &kind)
{
    return kind == "native_crash" ? QObject::tr("程序崩溃")
           : kind == "fatal"      ? QObject::tr("致命错误")
                                  : QObject::tr("异常退出（原因未知）");
}
QVariantMap view(const DiagnosticRecord &record)
{
    auto value = DiagnosticRecords::summary(record).toVariantMap();
    value.insert("kindText", kindText(record.kind));
    value.insert("timeText", QDateTime::fromString(record.timestamp, Qt::ISODateWithMs)
                                 .toLocalTime()
                                 .toString("yyyy-MM-dd HH:mm:ss"));
    value.insert("platformText", QStringList{record.os, record.arch}.join(' ').trimmed());
    value.insert("estimatedBytes", record.bytes);
    value.insert(
        "detailText",
        QString::fromUtf8(QJsonDocument(record.exception).toJson(QJsonDocument::Indented)));
    return value;
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) &&
           file.write(bytes) == bytes.size() && file.flush();
}
QString sanitizedLine(QString line)
{
    static const QRegularExpression sensitive(
        R"((?:^|[\s,;{\"&])(?:t1|mid|dfid|uuid|guid|mac|sid|edt|device_id|userid|user_id)[\"\s]*[:=])",
        QRegularExpression::CaseInsensitiveOption);
    if (sensitive.match(line).hasMatch())
        return QStringLiteral("[redacted sensitive diagnostic line]");
    line = safeLogMessage(line);
    // Strip machine/user directories in free text, in addition to credential redaction.
    const auto home = QDir::homePath();
    if (!home.isEmpty())
        line.replace(home, "<home>", Qt::CaseInsensitive);
    return line;
}
bool sanitizeFile(const QString &source, const QString &target, const std::atomic_bool &cancel)
{
    QFile input(source);
    QSaveFile output(target);
    if (QFileInfo(source).isSymLink() || !input.open(QIODevice::ReadOnly) ||
        input.size() > 40 * 1024 * 1024 || !output.open(QIODevice::WriteOnly))
        return false;
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto size = input.size();
    qint64 remaining = size;
    while (remaining > 0) {
        if (cancel)
            return false;
        QByteArray bytes = input.readLine(qMin<qint64>(remaining + 1, 8193));
        if (bytes.isEmpty())
            return false;
        remaining -= bytes.size();
        bool oversized = !bytes.endsWith('\n') && bytes.size() >= 8192;
        if (oversized) {
            while (remaining > 0 && !bytes.endsWith('\n')) {
                if (cancel)
                    return false;
                bytes = input.readLine(qMin<qint64>(remaining + 1, 8193));
                if (bytes.isEmpty())
                    return false;
                remaining -= bytes.size();
            }
            bytes = "[oversized diagnostic line omitted]\n";
        } else
            bytes = sanitizedLine(QString::fromUtf8(bytes)).toUtf8();
        if (!bytes.endsWith('\n'))
            bytes.append('\n');
        if (output.write(bytes) != bytes.size())
            return false;
    }
    input.close(); // Source and target may be the same private staging file.
    return !cancel && output.commit();
}
struct ZipProgress
{
    std::atomic_bool &cancel;
    std::function<void(int)> publish;
    quint64 total;
    int last = -1;
};
bool zipProgress(void *context, uint64_t bytes)
{
    auto &state = *static_cast<ZipProgress *>(context);
    const int percent = 45 + int(qMin<quint64>(bytes * 45 / qMax<quint64>(1, state.total), 45));
    if (percent != state.last) {
        state.last = percent;
        state.publish(percent);
    }
    return !state.cancel;
}
DiagnosticExportResult createPackage(const QString &directory, DiagnosticRecord record,
                                     bool current, const QString &destination, bool dump,
                                     const QString &notes, const DiagnosticService::Flush &flush,
                                     std::atomic_bool &cancel,
                                     const std::function<void(int)> &progress)
{
    DiagnosticExportResult result;
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return {QObject::tr("无法创建诊断临时目录"), false};
    const auto staging = temporary.filePath("content");
    if (!QDir().mkpath(staging + "/logs") || !QDir().mkpath(staging + "/crash"))
        return {QObject::tr("无法创建诊断临时目录"), false};
    QStringList missing, captured;
    progress(5);
    // Serialize our scan/export/pruning across processes. The handler's pending
    // report lease additionally protects native copies from its own prune task.
    if (!QDir().mkpath(QDir(directory).filePath("crashes")))
        return {QObject::tr("无法访问本地诊断目录"), false};
    QLockFile gate(QDir(directory).filePath("crashes/diagnostics.lock"));
    gate.setStaleLockTime(0);
    while (!gate.tryLock(0)) {
        if (gate.error() != QLockFile::LockFailedError)
            return {QObject::tr("无法锁定本地诊断记录"), false};
        if (cancel)
            return {{}, true};
        QThread::msleep(25);
    }
    const auto snapshot = temporary.filePath("log-snapshot");
    if (current && flush && (!QDir().mkpath(snapshot) || !flush(snapshot, cancel)))
        missing.append(QObject::tr("部分运行日志尚未写入或日志快照不可用"));
    if (!current && record.kind == "native_crash")
        captured =
            DiagnosticRecords::captureNative(directory, record, staging, dump, cancel, missing);
    if (!current && record.hasDump && !dump)
        missing.append(QObject::tr("用户选择不包含崩溃转储"));
    progress(20);
    const auto root = QDir(current && flush ? snapshot : directory);
    if (record.runLog.isEmpty())
        missing.append(QObject::tr("未找到该次运行的日志关联信息"));
    else {
        QStringList names;
        if (!captured.contains("logs/attached-" + record.runLog))
            names.append(record.runLog);
        const auto stem = record.runLog.chopped(4);
        // The sink retains seven segments, newest base and .1 .. .7.
        for (int i = 1; i <= 7; ++i)
            if (QFileInfo::exists(root.filePath(stem + QStringLiteral(".%1.log").arg(i))))
                names.append(stem + QStringLiteral(".%1.log").arg(i));
        for (const auto &name : names) {
            if (cancel)
                return {{}, true};
            const auto target = "logs/" + name;
            if (sanitizeFile(root.filePath(name), staging + '/' + target, cancel))
                captured.append(target);
            else if (name == record.runLog)
                missing.append(QObject::tr("原会话日志不可用，可能已被清理；如有附件则保留附件"));
            else
                missing.append(QObject::tr("轮转日志不可用：%1").arg(name));
        }
    }
    if (!current && !record.session.isEmpty() && !captured.contains("crash/session.jsonl")) {
        if (sanitizeFile(root.filePath("crashes/" + record.session),
                         staging + "/crash/session.jsonl", cancel))
            captured.append("crash/session.jsonl");
        else
            missing.append(QObject::tr("会话记录不可用，可能已被清理"));
    }
    // Crashpad attachments are original text: sanitize them before packaging.
    for (const auto &name : std::as_const(captured)) {
        if (name.endsWith(".dmp"))
            continue;
        if (!sanitizeFile(staging + '/' + name, staging + '/' + name, cancel))
            return cancel ? DiagnosticExportResult{{}, true}
                          : DiagnosticExportResult{QObject::tr("无法脱敏诊断文本"), false};
    }
    gate.unlock();
    progress(40);
    QJsonObject context{{"exporterBuild", CrashDiagnostics::buildIdentity()},
                        {"exporterOs", QSysInfo::prettyProductName()},
                        {"exporterKernel", QSysInfo::kernelVersion()},
                        {"exporterArch", QSysInfo::currentCpuArchitecture()},
                        {"notes", sanitizedLine(notes.left(2000))}};
    auto event = DiagnosticRecords::summary(record);
    if (!writeFile(staging + "/context.json", QJsonDocument(context).toJson()) ||
        !writeFile(staging + "/crash/summary.json", QJsonDocument(event).toJson()))
        return {QObject::tr("无法写入诊断摘要"), false};
    captured.append({"context.json", "crash/summary.json"});
    const auto text =
        QObject::tr("EchoMusicNext "
                    "诊断包\n报告编号：%1\n类型：%2\n事件时间（UTC）：%3\n事件版本：%4\n事件构建：%"
                    "5\n系统：%6 "
                    "%7\n\n问题描述：\n%8\n\n材料说明：\n%"
                    "9\n\n崩溃转储可能含内存中的个人信息，请仅发送给可信的排查人员。\n故障模块不代"
                    "表根本原因；源码定位取决于匹配构建的符号是否可用。\n")
            .arg(record.id, current ? QObject::tr("当前会话") : kindText(record.kind),
                 record.timestamp, record.version, record.buildId, record.os, record.arch,
                 sanitizedLine(notes.left(2000)),
                 missing.isEmpty() ? QObject::tr("所选材料已收集") : missing.join('\n'));
    if (!writeFile(staging + "/report.txt", text.toUtf8()))
        return {QObject::tr("无法写入诊断说明"), false};
    captured.append("report.txt");
    QJsonArray files;
    quint64 total = 0;
    for (const auto &name : std::as_const(captured)) {
        if (cancel)
            return {{}, true};
        QFile file(staging + '/' + name);
        if (!file.open(QIODevice::ReadOnly))
            return {QObject::tr("无法读取诊断临时文件"), false};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd()) {
            if (cancel)
                return {{}, true};
            const auto bytes = file.read(65536);
            if (bytes.isEmpty() && file.error() != QFile::NoError)
                return {QObject::tr("无法校验诊断临时文件"), false};
            hash.addData(bytes);
        }
        total += quint64(file.size());
        if (total > exportLimit)
            return {QObject::tr("诊断包超过 512 MiB，请取消包含崩溃转储后重试"), false};
        files.append(QJsonObject{{"path", name},
                                 {"size", file.size()},
                                 {"sha256", QString::fromLatin1(hash.result().toHex())}});
    }
    QJsonObject manifest{{"format", "echomusicnext-diagnostics"},
                         {"formatVersion", 1},
                         {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                         {"event", event},
                         {"includesDump", captured.contains("crash/report.dmp")},
                         {"missing", QJsonArray::fromStringList(missing)},
                         {"files", files}};
    if (!writeFile(staging + "/manifest.json", QJsonDocument(manifest).toJson()))
        return {QObject::tr("无法写入诊断清单"), false};
    ZipProgress state{cancel, progress, total};
    char error[1024]{};
    const auto archive = temporary.filePath("diagnostics.zip");
    if (!echo_diagnostic_zip(staging.toUtf8().constData(), archive.toUtf8().constData(),
                             zipProgress, &state, reinterpret_cast<uint8_t *>(error),
                             sizeof(error)))
        return cancel ? DiagnosticExportResult{{}, true}
                      : DiagnosticExportResult{
                            QObject::tr("压缩诊断包失败：%1").arg(QString::fromUtf8(error)), false};
    progress(95);
    QFile input(archive);
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return {QObject::tr("无法保存诊断包，请检查目标位置及可用空间"), false};
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    while (!input.atEnd()) {
        if (cancel)
            return {{}, true};
        const auto bytes = input.read(65536);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            return {QObject::tr("保存诊断包失败，原目标文件已保留"), false};
    }
    if (cancel)
        return {{}, true};
    if (!output.commit())
        return {QObject::tr("保存诊断包失败，原目标文件已保留"), false};
    return result;
}
} // namespace
DiagnosticService::DiagnosticService(QString directory, Flush flush, QObject *parent)
    : QAbstractListModel(parent), m_directory(std::move(directory)), m_flush(std::move(flush))
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(0);
    connect(&m_scan, &QFutureWatcher<DiagnosticScan>::finished, this, [this] {
        const auto result = m_scan.result();
        beginResetModel();
        m_records = result.records;
        // An unfinished session alone is not actionable crash evidence. Keep
        // those sessions in the collector for logs/retention, outside this UI.
        m_records.removeIf([](const DiagnosticRecord &record) {
            return record.kind != "native_crash" && record.kind != "fatal";
        });
        endResetModel();
        m_error = result.error;
        m_loaded = true;
        m_loading = false;
        emit changed();
        publishStartupNotice();
    });
    connect(&m_export, &QFutureWatcher<DiagnosticExportResult>::finished, this, [this] {
        const auto result = m_export.result();
        m_busy = false;
        m_error = result.error;
        m_progress = result.error.isEmpty() && !result.cancelled ? 100 : 0;
        m_status = result.cancelled          ? tr("已取消导出")
                   : !result.error.isEmpty() ? result.error
                                             : tr("诊断包已保存");
        if (result.cancelled || !result.error.isEmpty())
            m_exported = QUrl{};
        emit changed();
        if (!result.cancelled)
            emit notice(m_status, !result.error.isEmpty());
    });
}
DiagnosticService::~DiagnosticService()
{
    cancel();
    if (m_cancel)
        *m_cancel = true;
    m_pool.waitForDone();
}
int DiagnosticService::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}
QHash<int, QByteArray> DiagnosticService::roleNames() const
{
    return {{Qt::UserRole + 1, "entry"}};
}
QVariant DiagnosticService::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || role != Qt::UserRole + 1)
        return {};
    return view(m_records[index.row()]);
}
QVariantMap DiagnosticService::record(const QString &id) const
{
    for (const auto &record : m_records)
        if (record.id == id)
            return view(record);
    return {};
}
void DiagnosticService::refresh()
{
    if (m_busy || m_loading)
        return;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const auto directory = m_directory;
    m_loading = true;
    m_scan.setFuture(QtConcurrent::run(
        &m_pool, [directory, cancel] { return DiagnosticRecords::scan(directory, *cancel); }));
    emit changed();
}
QString DiagnosticService::suggestedFileName(const QString &id) const
{
    return "EchoMusicNext-diagnostics-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
           '-' + (id.isEmpty() ? "current-session" : id) + ".zip";
}
void DiagnosticService::exportCurrent(const QUrl &destination, const QString &notes)
{
    DiagnosticRecord record;
    record.id = sessionLogFileName().chopped(4);
    record.kind = "current_session";
    record.runLog = sessionLogFileName();
    record.timestamp = sessionStartedAt().toString(Qt::ISODateWithMs);
    const auto build = CrashDiagnostics::buildIdentity();
    record.version = build.value("version").toString();
    record.buildId = build.value("buildId").toString();
    record.os = QSysInfo::prettyProductName();
    record.arch = QSysInfo::buildCpuArchitecture();
    startExport(record, true, destination, false, notes);
}
void DiagnosticService::exportCrash(const QString &id, const QUrl &destination, bool dump,
                                    const QString &notes)
{
    for (const auto &record : m_records)
        if (record.id == id) {
            startExport(record, false, destination, dump, notes);
            return;
        }
    m_error = tr("该崩溃记录已不可用，请刷新记录列表");
    emit changed();
}
void DiagnosticService::startExport(DiagnosticRecord record, bool current, const QUrl &destination,
                                    bool dump, const QString &notes)
{
    if (m_busy || m_loading)
        return;
    const auto path = destination.toLocalFile();
    const auto canonical = QFileInfo(path).absoluteFilePath();
    const auto root = QFileInfo(m_directory).canonicalFilePath();
    const auto parent = QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath();
    // Never overwrite diagnostic inputs or write into the collector database.
    if (!destination.isLocalFile() || path.isEmpty() || parent.isEmpty() ||
        QFileInfo(path).isSymLink() || canonical.startsWith(root + '/', Qt::CaseInsensitive) ||
        parent == root || parent.startsWith(root + '/', Qt::CaseInsensitive)) {
        m_error = tr("请选择日志目录以外的本地 ZIP 保存位置");
        emit changed();
        return;
    }
    m_busy = true;
    m_progress = 0;
    m_error.clear();
    m_status = tr("正在收集诊断材料…");
    m_exported = destination;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const auto directory = m_directory;
    const auto flush = m_flush;
    m_export.setFuture(QtConcurrent::run(
        &m_pool, [this, directory, record, current, path, dump, notes, flush, cancel] {
            const auto progress = [this](int value) {
                QMetaObject::invokeMethod(
                    this,
                    [this, value] {
                        m_progress = value;
                        emit changed();
                    },
                    Qt::QueuedConnection);
            };
            return createPackage(directory, record, current, path, dump, notes, flush, *cancel,
                                 progress);
        }));
    emit changed();
}
void DiagnosticService::cancel()
{
    if (m_busy && m_cancel) {
        *m_cancel = true;
        m_status = tr("正在取消…");
        emit changed();
    }
}
void DiagnosticService::resetExport()
{
    if (m_busy)
        return;
    m_status.clear();
    m_error.clear();
    m_progress = 0;
    m_exported = QUrl{};
    emit changed();
}
void DiagnosticService::revealExport() const
{
    if (!m_busy && !m_exported.isEmpty())
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(m_exported.toLocalFile()).absolutePath()));
}
void DiagnosticService::checkStartupNotice()
{
    if (m_startupChecked)
        return;
    m_startupChecked = true;
    m_startupRequested = true;
    if (m_loaded)
        publishStartupNotice();
    else
        refresh();
}
void DiagnosticService::publishStartupNotice()
{
    if (!m_startupRequested || !m_loaded)
        return;
    m_startupRequested = false;
    const QDir root(QDir(m_directory).filePath("crashes"));
    QLockFile gate(root.filePath("notification.lock"));
    if (!gate.tryLock(0))
        return;
    QFile input(root.filePath("notified.json"));
    QJsonArray notified;
    if (input.open(QIODevice::ReadOnly) && input.size() < 128 * 1024)
        notified = QJsonDocument::fromJson(input.readAll()).array();
    QSet<QString> seen;
    for (const auto &id : notified)
        seen.insert(id.toString());
    bool fresh = false;
    QJsonArray retained;
    for (const auto &record : m_records) {
        if (record.kind != "native_crash" && record.kind != "fatal")
            continue;
        const auto key = record.session.isEmpty() ? record.id : record.session;
        if (!seen.contains(key))
            fresh = true;
        retained.append(key);
    }
    if (!fresh)
        return;
    input.close();
    QSaveFile output(root.filePath("notified.json"));
    const auto bytes = QJsonDocument(retained).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit()) {
        m_error = tr("无法保存崩溃提示状态");
        emit changed();
    }
    emit startupNotice(tr("检测到上次运行的崩溃记录，可导出诊断包协助排查"));
}
} // namespace EchoMusic
