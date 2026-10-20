#include "SettingsKeys.h"
#include "LogService.h"
#include "CrashDiagnostics.h"
#include "LogRedaction.h"
#include "LogFiles.h"
#include "echo_ffi.h"
#include <QStandardPaths>
#include <QDebug>
#include <QScopedValueRollback>
#include <QLoggingCategory>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTimeZone>
#include <QLockFile>
#include <QRegularExpression>
#include <QSet>
#include <QCoreApplication>
#include <QDesktopServices>
#include <atomic>
#include <deque>
#include <cstdio>

namespace EchoMusic
{
Q_LOGGING_CATEGORY(loggingLog, "echo.logging")
namespace
{
constexpr size_t capacity = 512;
constexpr qint64 fileLimit = 5 * 1024 * 1024;
constexpr int retainedFiles = 7;
const QStringList levels{QStringLiteral("error"), QStringLiteral("warn"), QStringLiteral("info"),
                         QStringLiteral("debug"), QStringLiteral("verbose")};
std::unique_ptr<QSettings> accessor(const QSettings &settings)
{
    auto result =
        settings.organizationName().isEmpty() && settings.applicationName().isEmpty()
            ? std::make_unique<QSettings>(settings.fileName(), settings.format())
            : std::make_unique<QSettings>(settings.format(), settings.scope(),
                                          settings.organizationName(), settings.applicationName());
    result->setFallbacksEnabled(settings.fallbacksEnabled());
    result->beginGroup(settings.group());
    return result;
}
uint32_t logLevel(QtMsgType type)
{
    return type == QtDebugMsg                              ? 3
           : type == QtWarningMsg                          ? 1
           : (type == QtCriticalMsg || type == QtFatalMsg) ? 0
                                                           : 2;
}
const char *levelName(uint32_t level, bool fatal)
{
    return fatal        ? "FATAL"
           : level == 0 ? "ERROR"
           : level == 1 ? "WARN"
           : level == 3 ? "DEBUG"
                        : "INFO";
}
QString singleLine(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const auto c : text) {
        const auto value = c.unicode();
        if (c == u'\n')
            result += QStringLiteral("\\n");
        else if (c == u'\r')
            result += QStringLiteral("\\r");
        else if (c == u'\t')
            result += QStringLiteral("\\t");
        else if (value < 0x20 || (value >= 0x7f && value <= 0x9f) || value == 0x2028 ||
                 value == 0x2029)
            result += QStringLiteral("\\u%1").arg(quint32(value), 4, 16, QLatin1Char('0'));
        else
            result += c;
    }
    return result;
}
struct Record
{
    qint64 time;
    uint32_t level;
    QString target;
    QString message;
    bool fatal = false;
};
} // namespace
// The QThread object is host-owned, not used as a worker QObject. Only run()
// touches QFile/console; producers use a bounded try-lock queue and wake it.
class LogSink final : public QThread
{
public:
    LogSink(QString directory, int level, bool console, int retentionDays)
        : m_directory(std::move(directory)), m_level(level), m_console(console),
          m_retentionDays(retentionDays)
    {
    }
    void setLevel(int level, qint64 diagnosticUntil)
    {
        m_diagnosticUntil.store(diagnosticUntil, std::memory_order_relaxed);
        m_level.store(level, std::memory_order_relaxed);
    }
    void enqueue(uint32_t level, const QString &target, const QString &message, qint64 time = 0,
                 bool fatal = false)
    {
        if (!accepts(level))
            return;
        if (!m_mutex.tryLock()) {
            m_dropped.fetch_add(1);
            return;
        }
        if (!m_stopping && m_queue.size() < capacity) {
            m_queue.push_back(
                {time ? time : QDateTime::currentMSecsSinceEpoch(), level, target.left(80),
                 message.size() > 8192 ? QStringLiteral("[oversized log omitted]") : message,
                 fatal});
            m_ready.wakeOne();
        } else {
            m_dropped.fetch_add(1);
        }
        m_mutex.unlock();
    }
    void finish()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_stopping = true;
            m_ready.wakeOne();
        }
        wait(); // only after the application event loop/services have stopped
    }

protected:
    void run() override
    {
        QDir directory(m_directory);
        bool fileAlive = directory.mkpath(QStringLiteral("."));
        const auto path = directory.filePath(sessionLogFileName());
        QLockFile active(path + QStringLiteral(".lock"));
        active.setStaleLockTime(0);
        fileAlive = fileAlive && active.tryLock(0);
        int removed = 0, cleanupFailures = 0;
        if (fileAlive) {
            // Releases using PID-only filenames have a crash-session lease but
            // no log lease. Read it without acquiring/removing crash artifacts.
            QSet<qint64> livePids;
            const QDir crashes(directory.filePath("crashes"));
            for (const auto &name : crashes.entryList({"*.running"}, QDir::Files)) {
                QLockFile lease(crashes.filePath(name));
                qint64 pid = 0;
                QString host, application;
                if (lease.getLockInfo(&pid, &host, &application))
                    livePids.insert(pid);
            }
            // Only this logger's top-level files, including old PID filenames
            // and rotated segments. A session lease protects every segment of
            // another live process even if it has been quiet past the cutoff.
            const QRegularExpression owned(
                R"(^(echomusicnext-(?:\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}-\d{3}-)?(\d+))(?:\.\d+)?\.log$)");
            const auto cutoff = QDateTime::currentDateTime().addDays(-m_retentionDays);
            const auto files = directory.entryInfoList({QStringLiteral("echomusicnext-*.log")},
                                                       QDir::Files | QDir::NoSymLinks);
            for (const auto &file : files) {
                const auto match = owned.match(file.fileName());
                if (!match.hasMatch() || file.lastModified() >= cutoff ||
                    livePids.contains(match.captured(2).toLongLong()))
                    continue;
                QLockFile lease(directory.filePath(match.captured(1) + ".log.lock"));
                lease.setStaleLockTime(0);
                if (!lease.tryLock(0))
                    continue;
                if (QFile::remove(file.absoluteFilePath()))
                    ++removed;
                else if (QFile::exists(file.absoluteFilePath()))
                    ++cleanupFailures;
            }
        }
        QFile file(path);
        fileAlive = fileAlive && file.open(QIODevice::WriteOnly | QIODevice::Append);
        bool consoleAlive = m_console;
        const auto failure = [&](const char *text) {
            if (consoleAlive) {
                if (std::fputs(text, stderr) < 0)
                    consoleAlive = false;
                std::fflush(stderr);
            }
        };
        if (!fileAlive)
            failure("EchoMusicNext: log file unavailable; console logging continues\n");
        const auto write = [&](const Record &record) {
            const auto stamp = QDateTime::fromMSecsSinceEpoch(record.time)
                                   .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            // One physical UTF-8 line in both destinations. Redact before escaping
            // control characters so multiline messages cannot forge log records
            // or inject terminal escape sequences.
            auto bytes =
                (stamp + ' ' +
                 QString::fromLatin1(levelName(record.level, record.fatal)).leftJustified(5) +
                 " [" + singleLine(record.target) + "] " +
                 singleLine(safeLogMessage(record.message)))
                    .toUtf8();
            bytes.append('\n');
            if (fileAlive && file.size() > 0 && file.size() + bytes.size() > fileLimit) {
                file.flush();
                // Backups are closed. Copy/truncate the active file on this
                // thread rather than rename an open handle on Windows.
                const auto segment = [&](int n) {
                    return path.left(path.size() - 4) + QStringLiteral(".%1.log").arg(n);
                };
                QFile::remove(segment(retainedFiles - 1));
                for (int n = retainedFiles - 2; n >= 1; --n) {
                    if (QFile::exists(segment(n)) && !QFile::rename(segment(n), segment(n + 1)))
                        fileAlive = false;
                }
                if (fileAlive &&
                    (!QFile::copy(path, segment(1)) || !file.resize(0) || !file.seek(0)))
                    fileAlive = false;
                if (!fileAlive)
                    failure("EchoMusicNext: log rotation failed; console logging continues\n");
            }
            if (fileAlive && file.write(bytes) != bytes.size()) {
                fileAlive = false;
                failure("EchoMusicNext: log write failed; console logging continues\n");
            }
            if (consoleAlive && std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr) !=
                                    size_t(bytes.size()))
                consoleAlive = false;
            if (consoleAlive)
                std::fflush(stderr);
        };
        if (removed && accepts(2))
            write({QDateTime::currentMSecsSinceEpoch(), 2, QStringLiteral("echo.logging"),
                   QStringLiteral("Removed %1 expired log files (retention_days=%2)")
                       .arg(removed)
                       .arg(m_retentionDays)});
        if (cleanupFailures && accepts(1))
            write({QDateTime::currentMSecsSinceEpoch(), 1, QStringLiteral("echo.logging"),
                   QStringLiteral("Unable to remove %1 expired log files").arg(cleanupFailures)});
        for (;;) {
            std::deque<Record> batch;
            bool stopping;
            {
                QMutexLocker lock(&m_mutex);
                if (m_queue.empty() && !m_stopping)
                    m_ready.wait(&m_mutex, 100);
                batch.swap(m_queue);
                stopping = m_stopping;
            }
            const auto dropped = m_dropped.exchange(0);
            if (dropped)
                write({QDateTime::currentMSecsSinceEpoch(), 1, QStringLiteral("echo.logging"),
                       QStringLiteral("Dropped %1 log entries (queue capacity or contention)")
                           .arg(dropped)});
            for (const auto &record : batch)
                write(record);
            if (fileAlive)
                file.flush();
            if (stopping)
                break;
        }
    }

private:
    bool accepts(uint32_t level) const
    {
        // Enforce the diagnostic deadline here even while the GUI timer is busy.
        const auto base = m_level.load(std::memory_order_relaxed);
        const auto threshold =
            QDateTime::currentMSecsSinceEpoch() < m_diagnosticUntil.load(std::memory_order_relaxed)
                ? qMax(3, base)
                : base;
        return int(level) <= threshold;
    }
    QString m_directory;
    std::atomic_int m_level;
    std::atomic<qint64> m_diagnosticUntil{0};
    bool m_console;
    int m_retentionDays;
    QMutex m_mutex;
    QWaitCondition m_ready;
    std::deque<Record> m_queue;
    std::atomic<quint64> m_dropped{0};
    bool m_stopping = false;
};
namespace
{
thread_local qint64 rustEmissionTime = 0;
QMutex sinkMutex;
std::shared_ptr<LogSink> sink;
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtFatalMsg)
        CrashDiagnostics::recordQtFatal(message);
    if (!sinkMutex.tryLock())
        return;
    auto writer = sink;
    sinkMutex.unlock();
    if (writer)
        writer->enqueue(logLevel(type),
                        QString::fromLatin1(context.category ? context.category : "qt"), message,
                        rustEmissionTime, type == QtFatalMsg);
}
void rustRecord(void *, const EchoLogEntry *entry)
{
    const auto target =
        QByteArray(reinterpret_cast<const char *>(entry->target), qsizetype(entry->target_len));
    const auto message = QString::fromUtf8(reinterpret_cast<const char *>(entry->message),
                                           qsizetype(entry->message_len));
    // Enter Qt's category/message-handler path, never from a Rust producer.
    // Keep the Rust emission time; GUI polling must not shift the recorded
    // event time when the window is busy. No pointer/context survives this call.
    QScopedValueRollback<qint64> emission(rustEmissionTime, qint64(entry->timestamp_ms));
    QLoggingCategory category(target.constData());
    switch (entry->level) {
    case 0:
        qCCritical(category).noquote() << message;
        break;
    case 1:
        qCWarning(category).noquote() << message;
        break;
    case 3:
    case 4:
        qCDebug(category).noquote() << message;
        break;
    default:
        qCInfo(category).noquote() << message;
        break;
    }
}
} // namespace
LogService::LogService(const QString &directory, int level, bool console, QSettings *preferences,
                       bool commandLineLevel)
    : m_directory(directory.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                            QStringLiteral("/logs")
                      : directory)
{
    m_level = qBound(0, level, 4);
    int retentionDays = 7;
    if (preferences) {
        m_preferences = accessor(*preferences);
        const auto savedLevel = levels.indexOf(
            m_preferences->value(EchoMusic::SettingsKeys::Logging::Level, "info").toString());
        if (!commandLineLevel && savedLevel >= 0)
            m_level = savedLevel;
        m_responseBody =
            m_preferences->value(EchoMusic::SettingsKeys::Logging::ApiResponseBody, false).toBool();
        bool validRetention = false;
        const auto savedRetention =
            m_preferences->value(EchoMusic::SettingsKeys::Logging::RetentionDays, 7)
                .toInt(&validRetention);
        if (validRetention && savedRetention >= 1 && savedRetention <= 365)
            retentionDays = savedRetention;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const auto savedUntil =
            m_preferences->value(EchoMusic::SettingsKeys::Logging::DiagnosticUntil, 0).toLongLong();
        if (savedUntil > now)
            m_diagnosticUntil = qMin(savedUntil, now + 10 * 60000);
    }
    const auto effective = diagnosticActive() ? qMax(3, m_level) : m_level;
    if (!echo_log_start(uint32_t(effective))) {
        qWarning("Unable to start EchoMusicNext logging");
        return;
    }
    m_sink = std::make_shared<LogSink>(m_directory, m_level, console, retentionDays);
    m_sink->setLevel(m_level, m_diagnosticUntil);
    {
        QMutexLocker lock(&sinkMutex);
        sink = m_sink;
    }
    m_sink->start();
    m_previous = qInstallMessageHandler(messageHandler);
    m_active = true;
    echo_log_api_responses(m_responseBody);
    QLoggingCategory::setFilterRules(
        QStringLiteral("echo.*.debug=true\nqml.debug=true\nqml.*.debug=true"));
    connect(&m_poll, &QTimer::timeout, this, [] { echo_log_drain(rustRecord, nullptr); });
    m_poll.start(50);
    m_diagnostics.setInterval(1000);
    connect(&m_diagnostics, &QTimer::timeout, this, [this] {
        if (!diagnosticActive()) {
            m_diagnosticUntil = 0;
            persist(EchoMusic::SettingsKeys::Logging::DiagnosticUntil, 0);
            m_diagnostics.stop();
            applyThreshold();
        }
        emit changed();
    });
    if (diagnosticActive())
        m_diagnostics.start();
    qCInfo(loggingLog).noquote().nospace()
        << "Logging started pid=" << QCoreApplication::applicationPid()
        << " level=" << this->level() << " timezone=" << QTimeZone::systemTimeZoneId()
        << " retention_days=" << retentionDays
        << " file=" << QDir(m_directory).absoluteFilePath(sessionLogFileName());
    qCInfo(loggingLog).noquote() << "Session build:" << CrashDiagnostics::buildDescription();
}
QString LogService::level() const
{
    return levels.at(m_level);
}
bool LogService::diagnosticActive() const
{
    return m_diagnosticUntil > QDateTime::currentMSecsSinceEpoch();
}
int LogService::diagnosticMinutes() const
{
    return int(qMax<qint64>(0, m_diagnosticUntil - QDateTime::currentMSecsSinceEpoch() + 59999) /
               60000);
}
bool LogService::persist(const QString &key, const QVariant &value)
{
    if (!m_preferences)
        return true;
    auto writer = accessor(*m_preferences);
    const auto previous = writer->value(key);
    writer->setValue(key, value);
    writer->sync();
    if (writer->status() == QSettings::NoError)
        return true;
    if (previous.isValid())
        writer->setValue(key, previous);
    else
        writer->remove(key);
    writer->sync();
    emit notice(tr("日志偏好保存失败"));
    return false;
}
void LogService::applyThreshold()
{
    const auto effective = diagnosticActive() ? qMax(3, m_level) : m_level;
    if (m_sink)
        m_sink->setLevel(m_level, m_diagnosticUntil);
    echo_log_set_level(uint32_t(effective));
    echo_log_api_responses(m_responseBody);
}
void LogService::setLevel(const QString &value)
{
    const auto next = levels.indexOf(value);
    if (next < 0 || next == m_level || !persist(EchoMusic::SettingsKeys::Logging::Level, value))
        return;
    m_level = next;
    applyThreshold();
    emit changed();
}
void LogService::setApiResponseBody(bool enabled)
{
    if (enabled == m_responseBody ||
        !persist(EchoMusic::SettingsKeys::Logging::ApiResponseBody, enabled))
        return;
    m_responseBody = enabled;
    applyThreshold();
    emit changed();
}
bool LogService::enableTemporaryDiagnostics()
{
    const auto until = QDateTime::currentMSecsSinceEpoch() + 10 * 60000;
    if (!persist(EchoMusic::SettingsKeys::Logging::DiagnosticUntil, until))
        return false;
    m_diagnosticUntil = until;
    m_diagnostics.start();
    applyThreshold();
    emit changed();
    return true;
}
void LogService::disableTemporaryDiagnostics()
{
    if (!persist(EchoMusic::SettingsKeys::Logging::DiagnosticUntil, 0))
        return;
    m_diagnosticUntil = 0;
    m_diagnostics.stop();
    applyThreshold();
    emit changed();
}
bool LogService::openDirectory()
{
    const bool opened =
        QDir().mkpath(m_directory) && QDesktopServices::openUrl(QUrl::fromLocalFile(m_directory));
    if (!opened)
        emit notice(tr("无法打开运行日志目录"));
    return opened;
}
LogService::~LogService()
{
    if (!m_active)
        return;
    m_poll.stop();
    m_diagnostics.stop();
    echo_log_api_responses(false);
    while (echo_log_drain(rustRecord, nullptr) > 0) {
    }
    echo_log_stop();
    qCInfo(loggingLog, "Logging stopped; draining accepted entries");
    qInstallMessageHandler(m_previous);
    {
        QMutexLocker lock(&sinkMutex);
        sink.reset();
    }
    m_sink->finish();
}
} // namespace EchoMusic
