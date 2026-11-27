#pragma once
#include "EchoPluginOperation.h"
#include <QHash>
#include <QPointer>
#include <QThread>

namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginWebSocketClient;
class PluginWebSocketClientWorker;
/// Outbound connection. The scope owns it until closed; retained JS handles remain readable.
class EchoPluginWebSocketConnection final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by outbound WebSocket connect")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString url READ url CONSTANT)
    Q_PROPERTY(QString protocol READ protocol CONSTANT)
    Q_PROPERTY(int readyState READ readyState NOTIFY changed)
    Q_PROPERTY(int closeCode READ closeCode NOTIFY changed)
    Q_PROPERTY(QString closeReason READ closeReason NOTIFY changed)
public:
    EchoPluginWebSocketConnection(EchoPluginWebSocketClient &, QString id, QString url,
                                  QString protocol);
    QString id() const
    {
        return m_id;
    }
    QString url() const
    {
        return m_url;
    }
    QString protocol() const
    {
        return m_protocol;
    }
    int readyState() const
    {
        return m_state;
    }
    int closeCode() const
    {
        return m_closeCode;
    }
    QString closeReason() const
    {
        return m_closeReason;
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
    friend class EchoPluginWebSocketClient;
    QPointer<EchoPluginWebSocketClient> m_owner;
    QString m_id, m_url, m_protocol, m_closeReason;
    int m_state = 1, m_closeCode = 0;
};

/// Independent webSocket authorization; transport lives on a lazy worker and drains with net.
class EchoPluginWebSocketClient final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin network")
public:
    explicit EchoPluginWebSocketClient(EchoPluginContext &);
    ~EchoPluginWebSocketClient() override;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *connect(const QString &url,
                                                        const QVariantMap &options = {});
    void shutdown();
    bool isDrained() const
    {
        return m_drained;
    }
    EchoPluginOperation *send(const QString &, const QString &, const QByteArray &);
    void close(const QString &, int, const QString &);
signals:
    void drained();

private:
    friend class PluginWebSocketClientWorker;
    friend class EchoPluginWebSocketConnection;
    void retire();
    void abort(const QString &);
    void result(const QString &, const QVariant &, const QString &code = {},
                const QString &message = {});
    void receive(const QString &, const QString &, const QVariant &, bool binary = false);
    void closed(const QString &, int, const QString &, bool notify = true);
    EchoPluginContext &m_scope;
    QThread m_thread;
    PluginWebSocketClientWorker *m_worker;
    QHash<QString, QPointer<EchoPluginOperation>> m_pending;
    QHash<QString, QPointer<EchoPluginWebSocketConnection>> m_connections;
    qint64 m_queuedBytes = 0;
    bool m_started = false, m_stopping = false, m_drained = false;
};
} // namespace EchoMusic
