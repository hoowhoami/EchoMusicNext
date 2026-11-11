// Winsock2 must precede Crashpad headers that include windows.h.
#ifdef _WIN32
#include <winsock2.h>
#endif
#include "CrashDiagnostics.h"
#include "DiagnosticRecords.h"
#include "EchoBuildInfo.h"
#include "LogRedaction.h"
#include "LogFiles.h"
#include <QtLogging>
#include <cstdio>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLockFile>
#include <QSysInfo>
#include <QTimeZone>
#include <client/crashpad_client.h>
#include <client/crash_report_database.h>
#include <client/settings.h>
#include <client/prune_crash_reports.h>
#include <snapshot/minidump/process_snapshot_minidump.h>
#include <util/file/file_reader.h>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef Q_OS_MACOS
#include <mach-o/dyld.h>
#endif
#endif

namespace EchoMusic
{
namespace
{
QtMessageHandler previousEarlyHandler = nullptr;
void earlyMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtFatalMsg)
        CrashDiagnostics::recordQtFatal(message);
    const auto sanitized = safeLogMessage(message);
    if (previousEarlyHandler)
        previousEarlyHandler(type, context, sanitized);
    else {
        const auto bytes = qFormatLogMessage(type, context, sanitized).toLocal8Bit();
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
}
#ifdef Q_OS_WIN
HANDLE emergency = INVALID_HANDLE_VALUE;
#else
int emergency = -1;
#endif
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
void emergencyWrite(const char *bytes, size_t length) noexcept
{
#ifdef Q_OS_WIN
    if (emergency != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(emergency, bytes, DWORD(length), &written, nullptr);
        FlushFileBuffers(emergency);
    }
#else
    if (emergency >= 0) {
        const auto ignored = ::write(emergency, bytes, length);
        (void)ignored;
        ::fsync(emergency);
    }
#endif
}
QString hex(uint64_t value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}
} // namespace
struct CrashDiagnostics::State
{
    QString report;
    QString error;
    QStringList abnormal;
    QStringList summaries;
    bool earlyHandlerInstalled = false;
    std::unique_ptr<QLockFile> lock;
    std::unique_ptr<crashpad::CrashReportDatabase> database;
    crashpad::CrashpadClient client;
};
QJsonObject CrashDiagnostics::buildIdentity()
{
    return {{"version", QStringLiteral(ECHO_RELEASE_VERSION)},
            {"buildId", QStringLiteral(ECHO_BUILD_ID)},
            {"configuration", QStringLiteral(ECHO_BUILD_CONFIGURATION)},
            {"arch", QSysInfo::buildCpuArchitecture()},
            {"qtVersion", QString::fromLatin1(qVersion())},
            {"compiler", QStringLiteral(ECHO_BUILD_COMPILER)}};
}
QString CrashDiagnostics::buildDescription()
{
    return QStringLiteral("version=%1 build=%2 configuration=%3 arch=%4 Qt=%5 compiler=%6")
        .arg(QStringLiteral(ECHO_RELEASE_VERSION), QStringLiteral(ECHO_BUILD_ID),
             QStringLiteral(ECHO_BUILD_CONFIGURATION), QSysInfo::buildCpuArchitecture(),
             QString::fromLatin1(qVersion()), QStringLiteral(ECHO_BUILD_COMPILER));
}
QString CrashDiagnostics::argumentLogDirectory(int argc, char **argv)
{
    QStringList arguments;
#ifdef Q_OS_WIN
    int count = 0;
    auto **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide) {
        for (int i = 1; i < count; ++i)
            arguments.append(QString::fromWCharArray(wide[i]));
        LocalFree(wide);
    }
    Q_UNUSED(argc)
    Q_UNUSED(argv)
#else
    for (int i = 1; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));
#endif
    QString path;
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments[i] == "--")
            break;
        if (arguments[i].startsWith("--log-dir="))
            path = arguments[i].mid(10);
        else if (arguments[i] == "--log-dir" && i + 1 < arguments.size())
            path = arguments[++i];
    }
    return path;
}
QJsonObject CrashDiagnostics::summarizeDump(const QString &path)
{
    crashpad::FileReader reader;
    crashpad::ProcessSnapshotMinidump snapshot;
    if (!reader.Open(nativePath(path)) || !snapshot.Initialize(&reader) || !snapshot.Exception() ||
        !snapshot.Exception()->Context())
        return {};
    const auto *exception = snapshot.Exception();
    const auto ip = exception->Context()->InstructionPointer();
    QJsonObject result{{"event", "native_crash"},
                       {"dump", path},
                       {"pid", qint64(snapshot.ProcessID())},
                       {"thread", qint64(exception->ThreadID())},
                       {"exceptionCode", hex(exception->Exception())},
                       {"exceptionInfo", hex(exception->ExceptionInfo())},
                       {"instructionPointer", hex(ip)},
                       {"faultAddress", hex(exception->ExceptionAddress())},
                       {"threadCount", int(snapshot.Threads().size())}};
    QJsonArray codes;
    for (auto code : exception->Codes())
        codes.append(hex(code));
    result.insert("exceptionParameters", codes);
    qint64 stackBytes = 0;
    for (const auto *thread : snapshot.Threads())
        if (thread->Stack())
            stackBytes += qint64(thread->Stack()->Size());
    result.insert("capturedStackBytes", stackBytes);
    timeval time{};
    snapshot.SnapshotTime(&time);
    result.insert("timestamp", QDateTime::fromMSecsSinceEpoch(
                                   qint64(time.tv_sec) * 1000 + time.tv_usec / 1000, QTimeZone::UTC)
                                   .toString(Qt::ISODateWithMs));
    QJsonObject annotations;
    for (const auto &[key, value] : snapshot.AnnotationsSimpleMap())
        annotations.insert(QString::fromStdString(key), QString::fromStdString(value));
    result.insert("build", annotations);
    for (const auto *module : snapshot.Modules()) {
        if (ip >= module->Address() && ip - module->Address() < module->Size()) {
            result.insert("module", QFileInfo(QString::fromStdString(module->Name())).fileName());
            result.insert("moduleOffset", hex(ip - module->Address()));
            result.insert("moduleBase", hex(module->Address()));
            uint16_t major = 0, minor = 0, build = 0, revision = 0;
            module->FileVersion(&major, &minor, &build, &revision);
            result.insert(
                "moduleVersion",
                QStringLiteral("%1.%2.%3.%4").arg(major).arg(minor).arg(build).arg(revision));
            crashpad::UUID uuid;
            uint32_t age = 0;
            module->UUIDAndAge(&uuid, &age);
            result.insert("debugId", QString::fromStdString(uuid.ToString()));
            result.insert("debugAge", int(age));
            const auto buildId = module->BuildID();
            result.insert("codeId", QString::fromLatin1(
                                        QByteArray(reinterpret_cast<const char *>(buildId.data()),
                                                   qsizetype(buildId.size()))
                                            .toHex()));
            break;
        }
    }
    return result;
}
CrashDiagnostics::CrashDiagnostics(const QString &directory) : m_state(std::make_unique<State>())
{
#ifdef Q_OS_WIN
    if (emergency != INVALID_HANDLE_VALUE) {
#else
    if (emergency >= 0) {
#endif
        m_state->error = QStringLiteral("Crash diagnostics already owned by this process");
        return;
    }
    QDir root(QDir(directory).absoluteFilePath("crashes"));
    if (!root.mkpath(".")) {
        m_state->error =
            QStringLiteral("Cannot create crash report directory: %1").arg(root.path());
        return;
    }
    for (const auto &name : root.entryList({"*.running"}, QDir::Files)) {
        QLockFile old(root.filePath(name));
        old.setStaleLockTime(0);
        if (old.tryLock(0)) {
            const auto report = root.filePath(name.chopped(8) + ".jsonl");
            QFile file(report);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                file.write(
                    "{\"event\":\"previous_unclean_exit\",\"cause\":\"unknown_without_dump\"}\n");
                file.flush();
            }
            m_state->abnormal.append(report);
        }
    }
    const auto stem = QStringLiteral("echomusicnext-%1-%2")
                          .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz"))
                          .arg(QCoreApplication::applicationPid());
    m_state->report = root.filePath(stem + ".jsonl");
    m_state->lock = std::make_unique<QLockFile>(root.filePath(stem + ".running"));
    if (!m_state->lock->tryLock(0)) {
        m_state->error = QStringLiteral("Cannot create crash session lease");
        return;
    }
    const auto runLog = QDir(directory).absoluteFilePath(sessionLogFileName());
    QFile file(m_state->report);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        m_state->error = QStringLiteral("Cannot create emergency crash report");
        return;
    }
    file.write(QJsonDocument(QJsonObject{{"event", "session_started"},
                                         {"timestamp", QDateTime::currentDateTimeUtc().toString(
                                                           Qt::ISODateWithMs)},
                                         {"pid", QCoreApplication::applicationPid()},
                                         {"build", buildDescription()},
                                         {"os", QSysInfo::prettyProductName()},
                                         {"kernel", QSysInfo::kernelVersion()},
                                         {"runLog", runLog}})
                   .toJson(QJsonDocument::Compact));
    file.write("\n");
    file.close();
#ifdef Q_OS_WIN
    emergency = CreateFileW(reinterpret_cast<LPCWSTR>(m_state->report.utf16()), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    emergency = ::open(QFile::encodeName(m_state->report).constData(),
                       O_WRONLY | O_APPEND | O_CLOEXEC, 0600);
    if (emergency >= 0)
        ::fchmod(emergency, S_IRUSR | S_IWUSR);
#endif
    previousEarlyHandler = qInstallMessageHandler(earlyMessageHandler);
    m_state->earlyHandlerInstalled = true;
    const auto databasePath = nativePath(root.filePath("database"));
    m_state->database = crashpad::CrashReportDatabase::Initialize(databasePath);
    if (!m_state->database || !m_state->database->GetSettings()->SetUploadsEnabled(false)) {
        m_state->error = QStringLiteral("Cannot initialize local Crashpad database");
        return;
    }
    std::vector<crashpad::CrashReportDatabase::Report> pending, completed;
    m_state->database->GetPendingReports(&pending);
    m_state->database->GetCompletedReports(&completed);
    pending.insert(pending.end(), completed.begin(), completed.end());
    for (const auto &report : pending) {
        const auto summary =
            root.filePath(QString::fromStdString(report.uuid.ToString()) + ".summary.json");
        if (QFile::exists(summary))
            continue;
        const auto json = summarizeDump(qtPath(report.file_path));
        if (json.isEmpty())
            continue;
        const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QFile output(summary);
        if (output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            output.write(bytes);
            output.write("\n");
            output.flush();
            m_state->summaries.append(QString::fromUtf8(bytes));
        }
    }
    QLockFile diagnosticsGate(root.filePath("diagnostics.lock"));
    diagnosticsGate.setStaleLockTime(0);
    const bool mayPrune = diagnosticsGate.tryLock(0);
    // Use Crashpad's locked database operations for pruning; never remove an
    // active report file behind the handler's back.
    crashpad::BinaryPruneCondition retention(crashpad::BinaryPruneCondition::OR,
                                             new crashpad::AgePruneCondition(7),
                                             new crashpad::DatabaseSizePruneCondition(200 * 1024));
    if (mayPrune) {
        crashpad::PruneCrashReportDatabase(m_state->database.get(), &retention);
        DiagnosticRecords::pruneSidecars(directory);
        diagnosticsGate.unlock();
    }
    QString executable;
#ifdef Q_OS_WIN
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    executable = QString::fromWCharArray(path);
#elif defined(Q_OS_MACOS)
    // _NSGetExecutablePath is usable before QApplication exists.
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    QByteArray path(int(length), '\0');
    if (_NSGetExecutablePath(path.data(), &length) == 0)
        executable = QFile::decodeName(path.constData());
#else
executable = QFileInfo("/proc/self/exe").symLinkTarget();
#endif
    QDir executableDirectory = QFileInfo(executable).dir();
#ifdef Q_OS_MACOS
    // Installed bundles put helper executables outside the visible MacOS entry.
    auto handler = executableDirectory.filePath("../Helpers/crashpad_handler");
    if (!QFileInfo::exists(handler))
        handler = executableDirectory.filePath("crashpad_handler");
#elif defined(Q_OS_WIN)
    const auto handler = executableDirectory.filePath("crashpad_handler.exe");
#else
const auto handler = executableDirectory.filePath("crashpad_handler");
#endif
    const std::map<std::string, std::string> annotations{
        {"product", "EchoMusicNext"},
        {"version", ECHO_RELEASE_VERSION},
        {"buildId", ECHO_BUILD_ID},
        {"configuration", ECHO_BUILD_CONFIGURATION},
        {"arch", QSysInfo::buildCpuArchitecture().toStdString()},
        {"qtVersion", qVersion()},
        {"runLog", runLog.toUtf8().toStdString()},
        {"sessionReport", m_state->report.toUtf8().toStdString()}};
    if (!m_state->client.StartHandler(nativePath(QDir::cleanPath(handler)), databasePath,
                                      base::FilePath(), "", "", annotations, {"--no-rate-limit"},
                                      true, false,
                                      {nativePath(m_state->report), nativePath(runLog)}))
        m_state->error = QStringLiteral("Crashpad handler could not start: %1").arg(handler);
#ifdef Q_OS_WIN
    else if (!m_state->client.RegisterWerModule(
                 nativePath(executableDirectory.filePath("crashpad_wer.dll")).value()))
        m_state->error =
            QStringLiteral("Crashpad active, but Windows fail-fast WER integration unavailable");
#endif
}
QString CrashDiagnostics::errorString() const
{
    return m_state->error;
}
QStringList CrashDiagnostics::previousAbnormalSessions() const
{
    return m_state->abnormal;
}
QStringList CrashDiagnostics::previousCrashSummaries() const
{
    return m_state->summaries;
}
void CrashDiagnostics::recordFatal(const char *reason) noexcept
{
    char line[256] = "{\"event\":\"fatal\",\"reason\":\"";
    size_t used = sizeof("{\"event\":\"fatal\",\"reason\":\"") - 1;
    for (size_t i = 0; reason[i] && used < sizeof(line) - 5; ++i)
        line[used++] = reason[i];
    line[used++] = '"';
    line[used++] = '}';
    line[used++] = '\n';
    emergencyWrite(line, used);
}
void CrashDiagnostics::recordFatalMessage(const QByteArray &message) noexcept
{
    emergencyWrite(message.constData(), size_t(message.size()));
}
void CrashDiagnostics::recordQtFatal(const QString &message)
{
    recordFatal("qt_fatal");
    auto bytes = QJsonDocument(QJsonObject{{"event", "qt_fatal_message"},
                                           {"message", safeLogMessage(message)}})
                     .toJson(QJsonDocument::Compact);
    bytes.append('\n');
    recordFatalMessage(bytes);
}
CrashDiagnostics::~CrashDiagnostics()
{
    if (!m_state->lock || !m_state->lock->isLocked())
        return;
    if (m_state->earlyHandlerInstalled)
        qInstallMessageHandler(previousEarlyHandler);
#ifdef Q_OS_WIN
    if (emergency != INVALID_HANDLE_VALUE)
        CloseHandle(emergency);
    emergency = INVALID_HANDLE_VALUE;
#else
    if (emergency >= 0)
        ::close(emergency);
    emergency = -1;
#endif
    QFile::remove(m_state->report);
}
} // namespace EchoMusic
