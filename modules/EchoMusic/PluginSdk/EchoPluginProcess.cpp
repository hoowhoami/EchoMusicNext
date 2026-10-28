#include "EchoPluginProcess.h"
#include "EchoPluginContext.h"
#include "ApiClient.h"
#include "AccountService.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QtConcurrentRun>
#include <atomic>
#include <memory>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <cerrno>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace EchoMusic
{
namespace
{
QString inside(const QString &root, const QString &path, bool directory)
{
    if (path.contains(QChar::Null) || QDir::isAbsolutePath(path))
        return {};
    const QFileInfo info(QDir(root).absoluteFilePath(path));
    const auto result = info.canonicalFilePath();
    if (!result.startsWith(root + '/') && !(directory && result == root))
        return {};
    return (directory ? info.isDir() : info.isFile()) && !info.isSymLink() ? result : QString();
}
QByteArray digest(const QString &path, const std::shared_ptr<std::atomic_bool> &cancelled)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024)
        return {};
    qint64 total = 0;
    while (!file.atEnd()) {
        if (cancelled->load(std::memory_order_relaxed))
            return {};
        const auto bytes = file.read(64 * 1024);
        total += bytes.size();
        if (bytes.isEmpty() || total > 128 * 1024 * 1024)
            return {};
        hash.addData(bytes);
    }
    return hash.result().toHex();
}

bool blocked(const QString &key)
{
    const auto name = key.toUpper();
    return name.startsWith("DYLD_") || name.startsWith("LD_") ||
           QStringList{
               "ECHOMUSIC_PLUGIN_ID",      "ECHOMUSIC_PLUGIN_DIR", "ECHOMUSICNEXT_PLUGIN_ID",
               "ECHOMUSICNEXT_PLUGIN_DIR", "NODE_OPTIONS",         "ELECTRON_RUN_AS_NODE"}
               .contains(name);
}
} // namespace
/// A tracked process group/job is never detached. Normal parent exit also closes
/// its descendants. No shell, inherited input or unbounded output buffers.
class PluginProcessResource final : public QObject
{
public:
    QProcess process;
    QTimer deadline;
    QTimer reap;
    QString id, executable, cwd, grant;
    qint64 pid = 0;
    quint64 epoch = 0;
    bool stopping = false;
    QVariantMap info;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
    STARTUPINFOEXW startup{};
    QByteArray attributes;
#endif
    explicit PluginProcessResource(QObject *p) : QObject(p)
    {
        reap.setInterval(10);
        deadline.setSingleShot(true);
        deadline.setInterval(2000);
        connect(&deadline, &QTimer::timeout, this, [this] { killTree(true); });
        process.setStandardInputFile(QProcess::nullDevice());
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_UNIX
        process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession |
                                         QProcess::UnixProcessFlag::CloseFileDescriptors |
                                         QProcess::UnixProcessFlag::ResetSignalHandlers);
#endif
    }
    ~PluginProcessResource() override
    {
#ifdef Q_OS_WIN
        if (startup.lpAttributeList)
            DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (job)
            CloseHandle(job);
#endif
    }
    bool prepare()
    {
#ifdef Q_OS_WIN
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                             sizeof(limits)))
            return false;
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attributes.resize(qsizetype(size));
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) {
            startup.lpAttributeList = nullptr;
            return false;
        }
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                       &job, sizeof(job), nullptr, nullptr))
            return false;
        process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args) {
            startup.StartupInfo = *args->startupInfo;
            startup.StartupInfo.cb = sizeof(startup);
            args->startupInfo = &startup.StartupInfo;
            args->flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
        });
#endif
        return true;
    }
    void killTree(bool force)
    {
        if (!pid)
            return;
#ifdef Q_OS_UNIX
        ::kill(-pid_t(pid), force ? SIGKILL : SIGTERM);
#elif defined(Q_OS_WIN)
        // Console control events are unsafe for GUI/foreign console helpers.
        // A job termination is the atomic, bounded Windows cancellation primitive.
        if (job)
            TerminateJobObject(job, 1);
#else
        if (force)
            process.kill();
        else
            process.terminate();
#endif
    }
    bool treeStopped() const
    {
#ifdef Q_OS_WIN
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        return !job || (QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info,
                                                  sizeof(info), nullptr) &&
                        info.ActiveProcesses == 0);
#else
        return true;
#endif
    }
    void stop()
    {
        if (stopping)
            return;
        stopping = true;
        killTree(false);
        deadline.start();
    }
};
EchoPluginProcess::EchoPluginProcess(EchoPluginContext &s) : QObject(&s), m_scope(s)
{
    const auto reset = [this] {
        shutdown();
        // Account observers finish invalidating their epochs before the snapshot.
        QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection);
    };
    connect(&s.host().api, &ApiClient::sessionChanged, this, reset);
    connect(&s.host().account, &AccountService::sessionAdopted, this, reset);
    if (s.host().permissions)
        connect(s.host().permissions, &PluginPermissionStore::revoked, this,
                [this](const QString &installation, const QString &cap) {
                    if (installation == m_scope.installationId() && cap == "process")
                        shutdown();
                });
}
EchoPluginProcess::~EchoPluginProcess()
{
    Q_ASSERT(isDrained());
}
QVariantList EchoPluginProcess::running() const
{
    QVariantList out;
    for (auto *r : m_processes)
        if (r->pid && r->epoch == m_scope.accountEpoch())
            out.append(r->info);
    return out;
}
bool EchoPluginProcess::isDrained() const
{
    return m_processes.isEmpty() && m_workers == 0;
}
void EchoPluginProcess::shutdown()
{
    for (auto *r : m_processes)
        r->stop();
}
EchoPluginOperation *EchoPluginProcess::launch(const QVariantMap &options)
{
    QPointer<EchoPluginOperation> op = m_scope.operation();
    if (!op || op->finished())
        return op;
    if (!m_scope.package().capabilities.contains("process") || !m_scope.host().permissions) {
        op->fail("CapabilityDenied", "Declare process capability");
        return op;
    }
    if (m_processes.size() + m_pendingLaunches >= 8) {
        op->fail("LimitExceeded", "At most eight processes or pending launches per scope");
        return op;
    }
    const auto root = QFileInfo(m_scope.package().root).canonicalFilePath();
    const auto exe = inside(root, options.value("executable").toString(), false);
    const auto cwd = inside(root, options.value("cwd", ".").toString(), true);
    const auto args = options.value("args").toStringList();
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &key : environment.keys())
        if (blocked(key))
            environment.remove(key);
    const auto custom = options.value("env").toMap();
    bool shape = true;
    for (auto it = options.begin(); it != options.end(); ++it)
        shape = shape && QStringList{"executable", "args", "cwd", "env"}.contains(it.key());
    if (options.contains("args")) {
        shape = shape && (options.value("args").metaType() == QMetaType::fromType<QStringList>() ||
                          options.value("args").metaType() == QMetaType::fromType<QVariantList>());
        if (options.value("args").metaType() == QMetaType::fromType<QVariantList>())
            for (const auto &arg : options.value("args").toList())
                shape = shape && arg.metaType() == QMetaType::fromType<QString>();
    }
    if (options.contains("env"))
        shape = shape && options.value("env").metaType() == QMetaType::fromType<QVariantMap>();
    bool valid =
        shape && !exe.isEmpty() && !cwd.isEmpty() && args.size() <= 128 && custom.size() <= 64;
#ifdef Q_OS_WIN
    valid = valid && QStringList{"exe", "com"}.contains(QFileInfo(exe).suffix().toLower());
#else
    valid = valid && QFileInfo(exe).isExecutable();
#endif
    for (const auto &arg : args)
        valid = valid && arg.size() <= 8192 && !arg.contains(QChar::Null);
    for (auto it = custom.begin(); it != custom.end(); ++it) {
        const auto type = it.value().metaType();
        const bool scalar =
            it.value().isNull() || type == QMetaType::fromType<QString>() ||
            type == QMetaType::fromType<bool>() || type == QMetaType::fromType<double>() ||
            type == QMetaType::fromType<int>() || type == QMetaType::fromType<qint64>();
        valid = valid && scalar && !blocked(it.key()) &&
                QRegularExpression("^[A-Za-z_][A-Za-z0-9_]{0,127}$").match(it.key()).hasMatch() &&
                it.value().toString().size() <= 8192 &&
                !it.value().toString().contains(QChar::Null);
        if (it.value().isNull())
            environment.remove(it.key());
        else
            environment.insert(it.key(), it.value().toString());
    }
    if (!valid) {
        op->fail("InvalidArgument",
                 "Executable and cwd must be inside the package; invalid args/env");
        return op;
    }
    environment.insert("ECHOMUSICNEXT_PLUGIN_ID", m_scope.id());
    environment.insert("ECHOMUSICNEXT_PLUGIN_DIR", root);
    const auto epoch = m_scope.accountEpoch();
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    op->start([cancelled] { cancelled->store(true, std::memory_order_relaxed); });
    ++m_pendingLaunches;
    connect(op.data(), &EchoPluginOperation::completed, this, [this] { --m_pendingLaunches; });
    ++m_workers;
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(
        watcher, &QFutureWatcher<QByteArray>::finished, this,
        [this, watcher, op, epoch, exe, cwd, args, environment, root, cancelled] {
            const auto hash = watcher->result();
            watcher->deleteLater();
            --m_workers;
            emit drained();
            if (!op || op->finished())
                return;
            if (!m_scope.active() || epoch != m_scope.accountEpoch()) {
                op->cancel();
                return;
            }
            if (hash.isEmpty()) {
                op->fail("IoError", "Cannot hash executable");
                return;
            }
            const auto resource =
                QDir(root).relativeFilePath(exe) + ':' + QString::fromLatin1(hash);
            m_scope.host().permissions->authorize(
                m_scope, "process",
                tr("程序：%1\n工作目录：%2\n此程序以当前用户的系统权限运行。").arg(exe, cwd), op,
                [this, op, epoch, exe, cwd, args, environment, root, hash, resource, cancelled] {
                    // Rehash on a worker after consent; file changes require new consent.
                    ++m_workers;
                    auto *verify = new QFutureWatcher<QByteArray>(this);
                    connect(
                        verify, &QFutureWatcher<QByteArray>::finished, this,
                        [this, verify, op, epoch, exe, cwd, args, environment, root, hash,
                         resource] {
                            const auto current = verify->result();
                            verify->deleteLater();
                            --m_workers;
                            emit drained();
                            if (!op || op->finished())
                                return;
                            if (!m_scope.active() || epoch != m_scope.accountEpoch() ||
                                !m_scope.host().permissions->granted(m_scope, "process",
                                                                     resource)) {
                                op->cancel();
                                return;
                            }
                            if (current != hash ||
                                inside(root, QDir(root).relativeFilePath(exe), false) != exe ||
                                inside(root, QDir(root).relativeFilePath(cwd), true) != cwd) {
                                op->fail("ContentChanged",
                                         "Executable changed after authorization");
                                return;
                            }
                            auto *r = new PluginProcessResource(this);
                            if (!r->prepare()) {
                                delete r;
                                op->fail("Unavailable", "Cannot establish process tree ownership");
                                return;
                            }
                            r->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                            r->executable = exe;
                            r->cwd = cwd;
                            r->epoch = epoch;
                            r->grant = resource;
                            m_processes.insert(r->id, r);
                            connect(op.data(), &EchoPluginOperation::completed, r, [op, r] {
                                if (op && op->state() == EchoPluginOperation::Canceled)
                                    r->stop();
                            });
                            connect(&r->process, &QProcess::started, r, [this, r, op, epoch] {
                                r->pid = r->process.processId();
                                r->info = {{"id", r->id},
                                           {"pid", r->pid},
                                           {"executable", r->executable},
                                           {"cwd", r->cwd},
                                           {"startedAt", QDateTime::currentMSecsSinceEpoch()}};
                                if (!op || op->finished() || !m_scope.active() ||
                                    epoch != m_scope.accountEpoch() || r->stopping)
                                    r->killTree(true);
                                else
                                    op->succeed(r->info);
                                emit changed();
                            });
                            const auto finish = [this, r, op, epoch] {
                                r->deadline.stop();
                                r->killTree(true); // Parent exit cannot leave an orphan helper.
                                // Windows job termination is asynchronous: keep the scope
                                // owned until every descendant has released its resources.
                                if (!r->treeStopped()) {
                                    r->reap.start();
                                    return;
                                }
                                r->reap.stop();
                                r->info["exitCode"] = r->process.exitCode();
                                r->info["crashed"] = r->process.exitStatus() == QProcess::CrashExit;
                                if (op && !op->finished())
                                    op->fail("ProcessError", r->process.errorString());
                                m_processes.remove(r->id);
                                if (r->pid && m_scope.active() && m_scope.accountEpoch() == epoch)
                                    emit exited(r->info);
                                emit changed();
                                r->deleteLater();
                                emit drained();
                            };
                            connect(&r->reap, &QTimer::timeout, r, finish);
                            connect(&r->process, &QProcess::finished, r, finish);
                            connect(&r->process, &QProcess::errorOccurred, r, [finish](auto error) {
                                if (error == QProcess::FailedToStart)
                                    finish();
                            });
                            r->process.setWorkingDirectory(cwd);
                            r->process.setProcessEnvironment(environment);
                            r->process.start(exe, args);
                        });
                    verify->setFuture(
                        QtConcurrent::run([exe, cancelled] { return digest(exe, cancelled); }));
                },
                resource);
        });
    watcher->setFuture(QtConcurrent::run([exe, cancelled] { return digest(exe, cancelled); }));
    return op;
}
EchoPluginOperation *EchoPluginProcess::terminate(const QString &id)
{
    auto *op = m_scope.operation();
    if (!op || op->finished())
        return op;
    auto *r = m_processes.value(id);
    if (!r) {
        op->fail("NotFound", "Process is not owned by this scope");
        return op;
    }
    op->start();
    connect(r, &QObject::destroyed, op, [op] {
        if (op && !op->finished())
            op->succeed(true);
    });
    r->stop(); // Cancelling this wait must never undo a termination already requested.
    return op;
}
} // namespace EchoMusic
