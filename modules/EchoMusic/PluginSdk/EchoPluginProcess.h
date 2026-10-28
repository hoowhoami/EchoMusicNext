#pragma once
#include "EchoPluginOperation.h"
#include <QPointer>
#include <QHash>
namespace EchoMusic
{
class EchoPluginContext;
class PluginProcessResource;
class EchoPluginProcess final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(QVariantList running READ running NOTIFY changed)
public:
    explicit EchoPluginProcess(EchoPluginContext &);
    ~EchoPluginProcess() override;
    QVariantList running() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *launch(const QVariantMap &options);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *terminate(const QString &id);
    void shutdown();
    bool isDrained() const;
signals:
    void changed();
    void exited(const QVariantMap &process);
    void drained();

private:
    EchoPluginContext &m_scope;
    QHash<QString, PluginProcessResource *> m_processes;
    int m_workers = 0, m_pendingLaunches = 0;
};
} // namespace EchoMusic
