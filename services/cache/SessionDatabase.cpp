#include "SessionDatabase.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QLockFile>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

namespace EchoMusic
{
Q_LOGGING_CATEGORY(sessionCacheLog, "echo.cache.session")
struct SessionDatabase::State
{
    static constexpr int applicationId = 0x45434e58; // ECNX: disposable cache identity.
    QString directory;
    QString connection = "session-cache-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::unique_ptr<QTemporaryDir> run;
    std::unique_ptr<QLockFile> lock;
    QSqlDatabase db;
    quint64 epoch = 0;
    bool initialized = false;
    static bool lost(const QSqlError &error)
    {
        const auto code = error.nativeErrorCode().toInt() & 255;
        return code == 11 || code == 26; // SQLITE_CORRUPT / SQLITE_NOTADB
    }
    void close()
    {
        db.close();
        db = {};
        if (QSqlDatabase::contains(connection))
            QSqlDatabase::removeDatabase(connection);
        lock.reset();
        run.reset();
    }
    static QString failure(const char *operation, const QSqlError &error)
    {
        qCWarning(sessionCacheLog)
            << "operation=" << operation << "native_code=" << error.nativeErrorCode()
            << "error_type=" << int(error.type());
        return error.text();
    }
    QString createRun(std::unique_ptr<QTemporaryDir> &nextRun, std::unique_ptr<QLockFile> &nextLock)
    {
        if (!QDir().mkpath(directory))
            return QObject::tr("无法创建会话缓存目录");
        // Serialize cleanup and run creation so a new, not-yet-locked directory
        // cannot be mistaken for an abandoned run by another process.
        QLockFile maintenance(QDir(directory).filePath("maintenance.lock"));
        maintenance.setStaleLockTime(0);
        if (!maintenance.tryLock(1000))
            return QObject::tr("会话缓存目录正在维护，请稍后重试");
        const auto old = QDir(directory).entryList({"run-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &name : old) {
            const auto path = QDir(directory).filePath(name);
            // A cleaner may remove only our lock file. The still-open source
            // must survive until its replacement has been copied and checked.
            if (run && path == run->path())
                continue;
            QLockFile owner(path + "/owner.lock");
            owner.setStaleLockTime(0);
            if (owner.tryLock(0)) {
                // The maintenance lock excludes another creator/cleaner. Drop
                // the dead run's file handle before removing its directory so
                // cleanup also works where open lock files cannot be deleted.
                owner.unlock();
                QDir(path).removeRecursively();
            }
        }
        nextRun = std::make_unique<QTemporaryDir>(QDir(directory).filePath("run-XXXXXX"));
        if (!nextRun->isValid())
            return QObject::tr("无法创建会话缓存：%1").arg(nextRun->errorString());
        nextLock = std::make_unique<QLockFile>(nextRun->filePath("owner.lock"));
        if (!nextLock->tryLock(0))
            return QObject::tr("无法锁定会话缓存目录");
        return {};
    }
    static QString configure(QSqlDatabase &database)
    {
        // These settings apply only to disposable session data. Durable queues,
        // history and accounts keep their existing repository settings.
        QSqlQuery query(database);
        for (const auto *sql :
             {"PRAGMA foreign_keys=ON", "PRAGMA journal_mode=TRUNCATE", "PRAGMA synchronous=OFF",
              "PRAGMA cache_size=-4096", "PRAGMA temp_store=FILE"})
            if (!query.exec(QString::fromLatin1(sql)))
                return QObject::tr("初始化会话缓存失败：%1")
                    .arg(failure("configure", query.lastError()));
        return {};
    }
    QString recover()
    {
        QElapsedTimer elapsed;
        elapsed.start();
        std::unique_ptr<QTemporaryDir> nextRun;
        std::unique_ptr<QLockFile> nextLock;
        if (const auto error = createRun(nextRun, nextLock); !error.isEmpty())
            return error;
        const auto path = nextRun->filePath("session.sqlite");
        bool unusable = false;
        {
            // The unlinked source may still be readable. Copy through SQLite,
            // not through its missing pathname, before releasing that handle.
            // All domain identities are explicit keys and survive VACUUM INTO.
            QSqlQuery copy(db);
            if (!copy.prepare("VACUUM main INTO ?"))
                return QObject::tr("无法恢复会话缓存：%1")
                    .arg(failure("recover.prepare", copy.lastError()));
            copy.addBindValue(path);
            if (!copy.exec()) {
                unusable = lost(copy.lastError());
                if (!unusable)
                    return QObject::tr("无法恢复会话缓存：%1")
                        .arg(failure("recover.copy", copy.lastError())
                                 .replace(path, "session.sqlite"));
            }
        }
        if (unusable)
            return fresh();
        const auto nextConnection =
            "session-cache-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto next = QSqlDatabase::addDatabase("QSQLITE", nextConnection);
        next.setDatabaseName(path);
        QString error;
        if (!next.open())
            error = failure("recover.open", next.lastError());
        else
            error = configure(next);
        bool emptyReplacement = false;
        if (error.isEmpty()) {
            QSqlQuery identity(next);
            if (!identity.exec("PRAGMA application_id") || !identity.next())
                error = failure("recover.identity", identity.lastError());
            else
                emptyReplacement = identity.value(0).toInt() != applicationId;
        }
        if (error.isEmpty() && !emptyReplacement) {
            QSqlQuery check(next);
            if (!check.exec("PRAGMA quick_check"))
                error = failure("recover.check", check.lastError());
            else if (!check.next() || check.value(0).toString() != "ok" || check.next())
                error = QObject::tr("恢复后的会话缓存校验失败");
            if (error.isEmpty() && check.lastError().isValid())
                error = failure("recover.check_fetch", check.lastError());
        }
        if (!error.isEmpty() || emptyReplacement) {
            next.close();
            next = {};
            QSqlDatabase::removeDatabase(nextConnection);
            if (emptyReplacement)
                return fresh();
            return QObject::tr("无法恢复会话缓存：%1").arg(error.replace(path, "session.sqlite"));
        }
        close();
        connection = nextConnection;
        db = next;
        run = std::move(nextRun);
        lock = std::move(nextLock);
        qCInfo(sessionCacheLog) << "Recovered missing session cache; accepted records preserved"
                                << "elapsed_ms=" << elapsed.elapsed();
        return {};
    }
    bool backingMissing() const
    {
        if (!db.isOpen() || !run)
            return true;
        const QFileInfo file(db.databaseName());
        return !file.exists() || file.size() < 100 ||
               !QFileInfo::exists(run->filePath("owner.lock"));
    }
    QString initialize()
    {
        if (db.isOpen()) {
            if (backingMissing())
                return recover();
            // A truncated/replaced cache can keep its pathname. Probe SQLite's
            // header before work; transient I/O/lock failures do not retire data.
            QSqlQuery probe(db);
            if (probe.exec("PRAGMA application_id") && probe.next()) {
                if (probe.value(0).toInt() == applicationId)
                    return {};
            } else if (!lost(probe.lastError()))
                return failure("probe", probe.lastError());
        }
        return fresh();
    }
    QString fresh()
    {
        const bool replacing = initialized;
        close();
        if (const auto error = createRun(run, lock); !error.isEmpty()) {
            close();
            return error;
        }
        db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(run->filePath("session.sqlite"));
        QString error;
        if (!db.open())
            error = QObject::tr("无法打开会话缓存：%1").arg(failure("open", db.lastError()));
        else
            error = configure(db);
        if (error.isEmpty()) {
            QSqlQuery identity(db);
            if (!identity.exec(QString("PRAGMA application_id=%1").arg(applicationId)))
                error = failure("initialize.identity", identity.lastError());
        }
        if (!error.isEmpty())
            close();
        else {
            initialized = true;
            if (replacing) {
                ++epoch;
                qCInfo(sessionCacheLog) << "Lost session cache invalidated; reloading sources"
                                        << "epoch=" << epoch;
            }
        }
        return error;
    }
};
SessionDatabase::SessionDatabase(QString directory, QObject *parent)
    : SessionDatabase(std::move(directory), Limits{}, parent)
{
}
SessionDatabase::SessionDatabase(QString directory, Limits limits, QObject *parent)
    : QObject(parent), m_state(std::make_shared<State>()), m_worker(new QObject), m_limits(limits)
{
    m_state->directory = std::move(directory);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, &SessionDatabase::closed);
    m_thread.setObjectName("session-cache");
    m_thread.start();
}
SessionDatabase::~SessionDatabase()
{
    shutdown();
    // Normal exit awaits closed(). The fallback joins a worker that never waits
    // for GUI callbacks, and closes all SQL objects on their owning thread.
    m_thread.wait();
}
bool SessionDatabase::enqueue(QObject *consumer, quint64 cost, Job job)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closing || !consumer || m_pendingJobs >= m_limits.pendingJobs ||
        (m_pendingJobs &&
         (cost > m_limits.pendingBytes || m_pendingBytes > m_limits.pendingBytes - cost)))
        return false;
    ++m_pendingJobs;
    m_pendingBytes += cost;
    const QPointer<QObject> receiver(consumer);
    const auto admittedEpoch = m_epoch;
    QMetaObject::invokeMethod(m_worker, [this, state = m_state, receiver, cost, admittedEpoch,
                                         job = std::move(job)]() mutable {
        const auto error = state->initialize();
        if (!error.isEmpty())
            qCWarning(sessionCacheLog).noquote()
                << "Session cache unavailable:"
                << QString(error).replace(state->directory, "[session-cache]");
        Completion completion;
        if (admittedEpoch == state->epoch)
            completion =
                job(state->db, error.isEmpty() ? QString() : tr("本地数据暂时不可用，请重试"));
        // A cleaner can race the admission check while SQL is executing. Detect
        // total loss before publishing that result, and retire it with its epoch.
        if (error.isEmpty() && state->initialized && state->backingMissing()) {
            const auto recoveryError = state->initialize();
            if (!recoveryError.isEmpty())
                qCWarning(sessionCacheLog).noquote()
                    << "Session cache unavailable:"
                    << QString(recoveryError).replace(state->directory, "[session-cache]");
        }
        const auto epoch = state->epoch;
        QMetaObject::invokeMethod(this, [this, receiver, cost, epoch, admittedEpoch,
                                         retiredJob = std::move(job),
                                         completion = std::move(completion)]() mutable {
            --m_pendingJobs;
            m_pendingBytes -= cost;
            const QPointer<SessionDatabase> guard(this);
            if (!m_closing && epoch > m_epoch) {
                m_epoch = epoch;
                m_retired.clear();
                emit aboutToInvalidate();
                if (!guard)
                    return;
                emit invalidated();
                if (!guard)
                    return;
            }
            if (!m_closing && epoch == m_epoch && admittedEpoch == epoch && receiver && completion)
                completion();
            if (!guard)
                return;
            if (!m_closing) {
                if (!m_retired.isEmpty())
                    discard(m_retired.takeFirst());
                emit capacityAvailable();
            }
        });
    });
    return true;
}
void SessionDatabase::discard(std::function<void(QSqlDatabase &)> cleanup)
{
    if (m_closing)
        return;
    if (!submit(
            this,
            [cleanup](QSqlDatabase &db, const QString &error) {
                if (error.isEmpty())
                    cleanup(db);
                return true;
            },
            [](bool) {}))
        m_retired.append(std::move(cleanup));
}
void SessionDatabase::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    m_retired.clear();
    QMetaObject::invokeMethod(m_worker, [state = m_state] {
        state->close();
        QThread::currentThread()->quit();
    });
}
} // namespace EchoMusic
