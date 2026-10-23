#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include <QJSValue>
#include <QPointer>
#include <QThread>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginWebServer;
class PluginWebServerWorker;
class EchoPluginWebSocket final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Delivered by web server")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QVariantMap request READ request CONSTANT)
    Q_PROPERTY(int readyState READ readyState NOTIFY changed)
public:
    EchoPluginWebSocket(EchoPluginWebServer &, QString id, QVariantMap request);
    QString id() const
    {
        return m_id;
    }
    QVariantMap request() const
    {
        return m_request;
    }
    int readyState() const
    {
        return m_closed ? 3 : 1;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *sendText(const QString &text);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *sendBytes(const QByteArray &bytes);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *ping(const QByteArray &bytes = {});
    Q_INVOKABLE void close(int code = 1000, const QString &reason = {});
signals:
    void changed();
    void message(const QVariant &data, bool binary);
    void closed(int code, const QString &reason);
    void error(const QString &message);
    void pong(double elapsedMs, const QByteArray &payload);

private:
    friend class EchoPluginWebServer;
    QPointer<EchoPluginWebServer> m_owner;
    QString m_id;
    QVariantMap m_request;
    bool m_closed = false;
    QJSValue m_cleanup;
};
class EchoPluginWebServer final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
public:
    explicit EchoPluginWebServer(EchoPluginContext &);
    ~EchoPluginWebServer() override;
    QVariantMap status() const
    {
        return m_status;
    }
    Q_INVOKABLE EchoMusic::EchoPluginOperation *listen(const QVariantMap &options = {});
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *onRequest(const QString &id,
                                                             const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *onConnection(const QString &id,
                                                                const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *onUpgrade(const QString &id,
                                                             const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *close();
    void shutdown();
    bool isDrained() const
    {
        return m_drained;
    }
    EchoPluginOperation *send(const QString &, const QString &, const QByteArray &);
    void closeSocket(const QString &, int, const QString &);
    EchoPluginContext &scope() const
    {
        return m_scope;
    }
signals:
    void changed();
    void drained();

private:
    friend class PluginWebServerWorker;
    void start();
    void stop();
    EchoPluginRegistration *handler(const QString &, const QString &, const QJSValue &);
    void dispatch(const QString &kind, const QString &requestId, const QVariantMap &);
    void deliver(const QString &id, QVariant result, const QString &code = {},
                 const QString &message = {});
    void receive(const QString &id, const QString &kind, const QVariantMap &);
    EchoPluginContext &m_scope;
    PluginWebServerWorker *m_worker;
    QThread m_thread;
    QVariantMap m_status{{"running", false}, {"connections", 0}, {"pendingRequests", 0}};
    QHash<QString, QPointer<EchoPluginOperation>> m_pending, m_handlers;
    QHash<QString, QPointer<EchoPluginWebSocket>> m_sockets;
    QHash<QString, QJSValue> m_callbacks;
    QHash<QString, QPointer<EchoPluginRegistration>> m_leases;
    QPointer<EchoPluginOperation> m_opening;
    QHash<QString, qint64> m_payloads;
    qint64 m_queuedBytes = 0;
    bool m_started = false, m_stopping = false, m_drained = false, m_listening = false;
    quint64 m_revision = 0;
};
} // namespace EchoMusic
