#include "EchoPluginSqlite.h"
#include "EchoPluginContext.h"
#include "PluginDatabaseWorker.h"
#include <QTimer>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
namespace EchoMusic
{
EchoPluginDatabase::EchoPluginDatabase(EchoPluginSqlite &owner, QString id,
                                       const QVariantMap &metadata)
    : QObject(&owner), m_owner(&owner), m_id(std::move(id)),
      m_name(metadata.value("name").toString()), m_version(metadata.value("version").toInt()),
      m_readOnly(metadata.value("readOnly").toBool())
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    owner.m_scope.engine().newQObject(this);
}
void EchoPluginDatabase::invalidate()
{
    if (!m_active)
        return;
    m_active = false;
    emit activeChanged();
}
EchoPluginOperation *EchoPluginDatabase::request(const QString &command,
                                                 const QVariantMap &arguments)
{
    if (!m_owner)
        return nullptr;
    if (!m_active)
        return m_owner->m_scope.rejected("DatabaseClosed", "Database handle is closed");
    return m_owner->request(command, m_id, arguments);
}
EchoPluginOperation *EchoPluginDatabase::exec(const QString &sql)
{
    return request("exec", {{"sql", sql}});
}
EchoPluginOperation *EchoPluginDatabase::run(const QString &sql, const QVariantList &params)
{
    return request("run", {{"sql", sql}, {"params", params}});
}
EchoPluginOperation *EchoPluginDatabase::get(const QString &sql, const QVariantList &params)
{
    return request("get", {{"sql", sql}, {"params", params}});
}
EchoPluginOperation *EchoPluginDatabase::all(const QString &sql, const QVariantList &params,
                                             const QVariantMap &options)
{
    if (!m_owner)
        return nullptr;
    for (auto it = options.begin(); it != options.end(); ++it)
        if (it.key() != "limit")
            return m_owner->m_scope.rejected("InvalidArgument",
                                             "Unknown SQLite query option: " + it.key());
    auto arguments = options;
    arguments.insert("sql", sql);
    arguments.insert("params", params);
    return request("all", arguments);
}
EchoPluginOperation *EchoPluginDatabase::transaction(const QVariantList &statements)
{
    return request("transaction", {{"statements", statements}});
}
EchoPluginOperation *EchoPluginDatabase::close()
{
    if (!m_owner)
        return nullptr;
    if (!m_active) {
        auto *operation = m_owner->m_scope.operation();
        if (!operation->finished())
            operation->succeed(false);
        return operation;
    }
    auto *operation = request("close", {});
    // Only invalidate after enqueue succeeds: a full operation queue must leave
    // the caller a working handle that can still be closed on retry.
    if (operation->state() == EchoPluginOperation::Running) {
        invalidate();
        QTimer::singleShot(0, this, [this] {
            setParent(nullptr);
            QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
        });
    }
    return operation;
}
EchoPluginSqlite::EchoPluginSqlite(EchoPluginContext &scope, QString directory)
    : QObject(&scope), m_scope(scope), m_dispatcher(new QObject(this)),
      m_worker(std::make_unique<PluginDatabaseWorker>(std::move(directory)))
{
    m_thread.setObjectName("PluginSqlite-" + scope.installationId());
    connect(&m_thread, &QThread::finished, m_dispatcher, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, [this] {
        m_drained = true;
        emit drained();
    });
}
EchoPluginSqlite::~EchoPluginSqlite()
{
    shutdown();
    m_thread.wait();
}
void EchoPluginSqlite::startWorker()
{
    if (m_thread.isRunning())
        return;
    m_dispatcher->setParent(nullptr);
    m_dispatcher->moveToThread(&m_thread);
    m_thread.start();
}
EchoPluginOperation *EchoPluginSqlite::open(const QVariantMap &options)
{
    return request("open", QUuid::createUuid().toString(QUuid::WithoutBraces), options);
}
EchoPluginOperation *EchoPluginSqlite::listDatabases()
{
    return request("list", {}, {});
}
EchoPluginOperation *EchoPluginSqlite::deleteDatabase(const QString &name)
{
    return request("delete", {}, {{"name", name}});
}
EchoPluginOperation *EchoPluginSqlite::request(const QString &command, const QString &id,
                                               const QVariantMap &arguments)
{
    if (m_scope.storageFrozen())
        return m_scope.rejected("Busy", "Plugin storage is taking a backup snapshot");
    if (!m_scope.package().capabilities.contains("sqlite"))
        return m_scope.rejected("CapabilityDenied", "Declare the sqlite capability");
    if (m_closed)
        return m_scope.rejected("ScopeClosed", "SQLite scope is closed");
    if (m_pending >= 64)
        return m_scope.rejected("Busy", "At most 64 SQLite operations may be pending");
    // Bound the GUI->worker queue as well as each worker query. QVariant payloads
    // here consist only of the public JSON/typed-BLOB argument grammar.
    const auto argumentBytes =
        QJsonDocument(QJsonObject::fromVariantMap(arguments)).toJson(QJsonDocument::Compact).size();
    if (argumentBytes > 18 * 1024 * 1024 || m_pendingBytes + argumentBytes > 32 * 1024 * 1024)
        return m_scope.rejected("InvalidArgument", "SQLite request exceeds budget");
    auto *operation = m_scope.operation();
    if (operation->finished())
        return operation;
    auto canceled = std::make_shared<std::atomic_bool>(false);
    operation->start([this, canceled, command, id] {
        canceled->store(true);
        if (command == "open" && !m_closed && m_thread.isRunning())
            QMetaObject::invokeMethod(
                m_dispatcher,
                [this, id] {
                    const std::atomic_bool no(false);
                    m_worker->execute("close", id, {}, no);
                },
                Qt::QueuedConnection);
    });
    const QPointer<EchoPluginOperation> guard(operation);
    startWorker();
    ++m_pending;
    m_pendingBytes += argumentBytes;
    QMetaObject::invokeMethod(
        m_dispatcher,
        [this, command, id, arguments, canceled, guard, argumentBytes] {
            // Closing must release the connection even if its result was canceled.
            const std::atomic_bool neverCanceled(false);
            auto result = m_worker->execute(command, id, arguments,
                                            command == "close" ? neverCanceled : *canceled);
            if (command == "open" && result.error.isEmpty() && canceled->load()) {
                m_worker->execute("close", id, {}, neverCanceled);
                result.code = "Canceled";
                result.error = "Open canceled";
            }
            QMetaObject::invokeMethod(
                this,
                [this, command, id, result, guard, argumentBytes] {
                    --m_pending;
                    m_pendingBytes -= argumentBytes;
                    if (!guard || guard->finished()) {
                        if (command == "open" && result.error.isEmpty() && !m_closed)
                            QMetaObject::invokeMethod(
                                m_dispatcher,
                                [this, id] {
                                    const std::atomic_bool no(false);
                                    m_worker->execute("close", id, {}, no);
                                },
                                Qt::QueuedConnection);
                        return;
                    }
                    if (!result.error.isEmpty()) {
                        guard->fail(result.code, result.error);
                        return;
                    }
                    if (command == "open") {
                        auto *handle = new EchoPluginDatabase(*this, id, result.value.toMap());
                        m_handles.erase(std::remove_if(m_handles.begin(), m_handles.end(),
                                                       [](const auto &h) { return !h; }),
                                        m_handles.end());
                        m_handles.append(handle);
                        guard->succeed(QVariant::fromValue(handle));
                    } else
                        guard->succeed(result.value);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    return operation;
}
void EchoPluginSqlite::storageBarrier(std::function<void()> ready)
{
    if (m_closed && !m_drained) {
        connect(this, &EchoPluginSqlite::drained, this, std::move(ready), Qt::SingleShotConnection);
        return;
    }
    if (!m_thread.isRunning()) {
        ready();
        return;
    }
    QMetaObject::invokeMethod(
        m_dispatcher,
        [this, ready = std::move(ready)] {
            QMetaObject::invokeMethod(this, ready, Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
void EchoPluginSqlite::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    for (auto handle : m_handles)
        if (handle)
            handle->invalidate();
    if (!m_thread.isRunning()) {
        m_drained = true;
        emit drained();
        return;
    }
    QMetaObject::invokeMethod(
        m_dispatcher,
        [this] {
            m_worker->closeAll();
            m_thread.quit();
        },
        Qt::QueuedConnection);
}
} // namespace EchoMusic
