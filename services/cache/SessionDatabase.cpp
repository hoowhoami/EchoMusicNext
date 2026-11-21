#include "SessionDatabase.h"
#include <QDir>
#include <QLockFile>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

namespace EchoMusic
{
struct SessionDatabase::State
{
    QString directory;
    QString connection = "session-cache-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::unique_ptr<QTemporaryDir> run;
    std::unique_ptr<QLockFile> lock;
    QSqlDatabase db;
    void close()
    {
        db.close();
        db = {};
        if (QSqlDatabase::contains(connection))
            QSqlDatabase::removeDatabase(connection);
        lock.reset();
        run.reset();
    }
    QString initialize()
    {
        if (db.isOpen())
            return {};
        close();
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
        run = std::make_unique<QTemporaryDir>(QDir(directory).filePath("run-XXXXXX"));
        if (!run->isValid()) {
            const auto error = QObject::tr("无法创建会话缓存：%1").arg(run->errorString());
            close();
            return error;
        }
        lock = std::make_unique<QLockFile>(run->filePath("owner.lock"));
        if (!lock->tryLock(0)) {
            close();
            return QObject::tr("无法锁定会话缓存目录");
        }
        db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(run->filePath("session.sqlite"));
        if (!db.open()) {
            const auto error = QObject::tr("无法打开会话缓存：%1").arg(db.lastError().text());
            close();
            return error;
        }
        // These settings apply only to disposable session data. Durable queues,
        // history and accounts keep their existing repository settings.
        QSqlQuery query(db);
        for (const auto *sql :
             {"PRAGMA foreign_keys=ON", "PRAGMA journal_mode=TRUNCATE", "PRAGMA synchronous=OFF",
              "PRAGMA cache_size=-4096", "PRAGMA temp_store=FILE"})
            if (!query.exec(QString::fromLatin1(sql))) {
                const auto error =
                    QObject::tr("初始化会话缓存失败：%1").arg(query.lastError().text());
                query.finish();
                query = QSqlQuery();
                close();
                return error;
            }
        return {};
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
    QMetaObject::invokeMethod(
        m_worker, [this, state = m_state, receiver, cost, job = std::move(job)]() mutable {
            const auto error = state->initialize();
            auto completion = job(state->db, error);
            job = {};
            QMetaObject::invokeMethod(
                this, [this, receiver, cost, completion = std::move(completion)]() mutable {
                    --m_pendingJobs;
                    m_pendingBytes -= cost;
                    const QPointer<SessionDatabase> guard(this);
                    if (!m_closing && receiver)
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
