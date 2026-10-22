#pragma once
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QThread>
#include <functional>
#include <memory>

namespace EchoMusic
{
class EchoPluginContext;
class PluginDatabaseWorker;
class EchoPluginSqlite;
class EchoPluginDatabase final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by sqlite.open")
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int version READ version CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    EchoPluginDatabase(EchoPluginSqlite &owner, QString id, const QVariantMap &metadata);
    QString name() const
    {
        return m_name;
    }
    int version() const
    {
        return m_version;
    }
    bool readOnly() const
    {
        return m_readOnly;
    }
    bool active() const
    {
        return m_active;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *exec(const QString &sql);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *run(const QString &sql,
                                                    const QVariantList &params = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *get(const QString &sql,
                                                    const QVariantList &params = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    all(const QString &sql, const QVariantList &params = {}, const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *transaction(const QVariantList &statements);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *close();
    void invalidate();
signals:
    void activeChanged();

private:
    EchoPluginOperation *request(const QString &command, const QVariantMap &arguments);
    QPointer<EchoPluginSqlite> m_owner;
    QString m_id, m_name;
    int m_version;
    bool m_readOnly, m_active = true;
};
/// Installation-private QSQLITE, with one lazy serial worker per visible scope.
/// Accepted writes drain before update/retirement; cancellation suppresses old
/// delivery and rolls back work when the worker reaches its next safe boundary.
class EchoPluginSqlite final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    EchoPluginSqlite(EchoPluginContext &scope, QString directory);
    ~EchoPluginSqlite() override;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *open(const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *listDatabases();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *deleteDatabase(const QString &name);
    EchoPluginOperation *request(const QString &command, const QString &id,
                                 const QVariantMap &arguments);
    void shutdown();
    bool isDrained() const
    {
        return m_drained;
    }
signals:
    void drained();

private:
    friend class EchoPluginDatabase;
    void startWorker();
    EchoPluginContext &m_scope;
    QThread m_thread;
    QObject *m_dispatcher;
    std::unique_ptr<PluginDatabaseWorker> m_worker;
    QList<QPointer<EchoPluginDatabase>> m_handles;
    int m_pending = 0;
    qsizetype m_pendingBytes = 0;
    bool m_closed = false, m_drained = false;
};
} // namespace EchoMusic
