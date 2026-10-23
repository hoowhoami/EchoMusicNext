#include "EchoPluginIo.h"
#include "EchoPluginContext.h"
#include <QDir>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <atomic>
namespace EchoMusic
{
namespace
{
struct StorageResult
{
    QVariant value;
    QString error;
};
bool initializeValues(QSqlDatabase &database, QString &error)
{
    QSqlQuery query(database);
    if (!query.exec("PRAGMA user_version") || !query.next()) {
        error = query.lastError().text();
        return false;
    }
    const int version = query.value(0).toInt();
    if (version == 1)
        return true;
    if (version != 0) {
        error = "Unsupported plugin values schema version";
        return false;
    }
    if (!database.transaction()) {
        error = database.lastError().text();
        return false;
    }
    if (!query.exec(
            "CREATE TABLE plugin_values (key TEXT PRIMARY KEY NOT NULL, json BLOB NOT NULL)") ||
        !query.exec("PRAGMA user_version=1") || !database.commit()) {
        error = query.lastError().text();
        if (error.isEmpty())
            error = database.lastError().text();
        database.rollback();
        return false;
    }
    return true;
}
StorageResult accessValues(const QString &directory, const QString &key, const QByteArray &json,
                           int command)
{
    StorageResult result;
    if (!QDir().mkpath(directory))
        return {{}, "Cannot create plugin data directory"};
    const auto connection = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(directory + "/values.sqlite");
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        if (!db.open())
            result.error = db.lastError().text();
        else {
            QSqlQuery query(db);
            if (initializeValues(db, result.error)) {
                query.prepare(command == 0   ? "SELECT json FROM plugin_values WHERE key=?"
                              : command == 1 ? "INSERT INTO plugin_values(key,json) VALUES(?,?) ON "
                                               "CONFLICT(key) DO UPDATE SET json=excluded.json"
                                             : "DELETE FROM plugin_values WHERE key=?");
                query.addBindValue(key);
                if (command == 1)
                    query.addBindValue(json);
                if (!query.exec())
                    result.error = query.lastError().text();
                else if (command == 0 && query.next()) {
                    QJsonParseError error;
                    const auto document =
                        QJsonDocument::fromJson(query.value(0).toByteArray(), &error);
                    if (error.error != QJsonParseError::NoError)
                        result.error = "Invalid stored SDK JSON";
                    else
                        result.value = document.object().value("value").toVariant();
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}
} // namespace
EchoPluginIo::EchoPluginIo(EchoPluginContext &scope, QString dataPath)
    : QObject(&scope), m_scope(scope), m_dataPath(std::move(dataPath)), m_worker(new QObject(this))
{
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, [this] {
        m_drained = true;
        emit drained();
    });
    m_thread.setObjectName("PluginValues-" + scope.installationId());
}
EchoPluginIo::~EchoPluginIo()
{
    // Normal retirement already drained asynchronously. Also join on scope
    // unwinding: QThread must never outlive its containing QObject, and queued
    // SQL writes still precede the worker's quit barrier.
    shutdown();
    m_thread.wait();
}
EchoPluginOperation *EchoPluginIo::storage(const QString &key, const QVariant &value, int command)
{
    if (m_scope.storageFrozen())
        return m_scope.rejected("Busy", "Plugin storage is taking a backup snapshot");
    if (key.isEmpty() || key.size() > 512)
        return m_scope.rejected("InvalidArgument", "Storage key must contain 1..512 characters");
    const auto normalized = value.metaType() == QMetaType::fromType<QJSValue>()
                                ? value.value<QJSValue>().toVariant()
                                : value;
    const auto jsonValue = QJsonValue::fromVariant(normalized);
    if (command == 1 && jsonValue.isUndefined())
        return m_scope.rejected("InvalidArgument", "Only JSON values can be stored");
    const auto bytes =
        QJsonDocument(QJsonObject{{"value", jsonValue}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 1024 * 1024)
        return m_scope.rejected("InvalidArgument", "Stored value exceeds 1 MiB");
    auto *op = m_scope.operation();
    if (op->finished() || m_closed)
        return op;
    auto canceled = std::make_shared<std::atomic_bool>(false);
    op->start([canceled] { canceled->store(true); });
    const QPointer<EchoPluginOperation> guard(op);
    const auto directory = m_dataPath;
    if (!m_thread.isRunning()) {
        m_worker->setParent(nullptr);
        m_worker->moveToThread(&m_thread);
        m_thread.start();
    }
    QMetaObject::invokeMethod(
        m_worker,
        [this, directory, key, bytes, command, guard] {
            const auto result = accessValues(directory, key, bytes, command);
            QMetaObject::invokeMethod(
                this,
                [guard, result] {
                    if (!guard || guard->finished())
                        return;
                    if (result.error.isEmpty())
                        guard->succeed(result.value);
                    else
                        guard->fail("StorageError", result.error);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    return op;
}
EchoPluginOperation *EchoPluginIo::readValue(const QString &key)
{
    return storage(key, {}, 0);
}
EchoPluginOperation *EchoPluginIo::writeValue(const QString &key, const QVariant &value)
{
    return storage(key, value, 1);
}
EchoPluginOperation *EchoPluginIo::removeValue(const QString &key)
{
    return storage(key, {}, 2);
}
void EchoPluginIo::storageBarrier(std::function<void()> ready)
{
    if (m_closed && !m_drained) {
        connect(this, &EchoPluginIo::drained, this, std::move(ready), Qt::SingleShotConnection);
        return;
    }
    if (!m_thread.isRunning()) {
        ready();
        return;
    }
    QMetaObject::invokeMethod(
        m_worker,
        [this, ready = std::move(ready)] {
            QMetaObject::invokeMethod(this, ready, Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
void EchoPluginIo::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    if (!m_thread.isRunning()) {
        m_drained = true;
        emit drained();
        return;
    }
    // The serial barrier follows every queued database write. The GUI never waits.
    QMetaObject::invokeMethod(
        m_worker, [thread = &m_thread] { thread->quit(); }, Qt::QueuedConnection);
}
} // namespace EchoMusic
