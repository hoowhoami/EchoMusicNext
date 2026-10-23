#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginTcp.h"
#include <QPointer>
#include <QThread>
#include <QHash>
namespace EchoMusic
{
class EchoPluginContext;
class PluginNetworkWorker;
/// One HTTP API, without domain/header allowlists or host credentials. Transport
/// and decoding run on a bounded worker and drain with the activation scope.
class EchoPluginNetwork final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
    Q_PROPERTY(EchoMusic::EchoPluginTcp *tcp READ tcp CONSTANT)
public:
    explicit EchoPluginNetwork(EchoPluginContext &scope);
    ~EchoPluginNetwork() override;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *request(const QVariantMap &options);
    EchoPluginTcp *tcp() const
    {
        return m_tcp;
    }
    void shutdown();
    bool isDrained() const
    {
        return m_drained && m_tcp->isDrained();
    }
signals:
    void drained();

private:
    friend class PluginNetworkWorker;
    EchoPluginContext &m_scope;
    EchoPluginTcp *m_tcp;
    QThread m_thread;
    PluginNetworkWorker *m_worker;
    QHash<QString, QPointer<EchoPluginOperation>> m_pending;
    bool m_closed = false, m_drained = false;
};
} // namespace EchoMusic
