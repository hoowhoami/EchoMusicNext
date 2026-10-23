#pragma once
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QThread>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginTcp;
class PluginTcpWorker;
class EchoPluginTcpConnection final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by TCP connect")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(bool closed READ closed NOTIFY changed)
public:
    EchoPluginTcpConnection(EchoPluginTcp &, QString id);
    QString id() const
    {
        return m_id;
    }
    bool closed() const
    {
        return m_closed;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *read();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *write(const QByteArray &data);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *end();
    Q_INVOKABLE void close();
signals:
    void changed();

private:
    friend class EchoPluginTcp;
    QPointer<EchoPluginTcp> m_owner;
    QString m_id;
    bool m_closed = false;
};
/// Raw TCP bypasses the HTTP proxy. Every operation is cancelable; cancel closes its connection.
class EchoPluginTcp final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin network")
public:
    explicit EchoPluginTcp(EchoPluginContext &);
    ~EchoPluginTcp() override;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *connect(const QVariantMap &options);
    void close(const QString &id);
    void closeAll();
    void shutdown();
    bool isDrained() const
    {
        return m_drained;
    }
    EchoPluginOperation *operation(const QString &, const QString &, const QByteArray &data = {});
    EchoPluginContext &scope() const
    {
        return m_scope;
    }
signals:
    void drained();

private:
    friend class PluginTcpWorker;
    void start();
    void result(const QString &op, QVariant value, const QString &code = {},
                const QString &message = {});
    void closed(const QString &id);
    EchoPluginContext &m_scope;
    QThread m_thread;
    PluginTcpWorker *m_worker;
    QHash<QString, QPointer<EchoPluginOperation>> m_pending;
    QHash<QString, QPointer<EchoPluginTcpConnection>> m_connections;
    QHash<QString, qint64> m_payloads;
    qint64 m_queuedBytes = 0;
    bool m_started = false, m_stopping = false, m_drained = false;
};
} // namespace EchoMusic
