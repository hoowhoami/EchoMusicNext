#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include "BackupHost.h"
#include <QPointer>
#include <QJSValue>
Q_MOC_INCLUDE("EchoPluginContext.h")
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginBackupRequest final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Passed to backup storage providers")
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString fileName READ fileName CONSTANT)
    Q_PROPERTY(QByteArray data READ data NOTIFY changed)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(bool canceled READ canceled NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(EchoMusic::EchoPluginContext *context READ context NOTIFY changed)
public:
    EchoPluginBackupRequest(EchoPluginContext &, const QVariantMap &arguments);
    QString id() const
    {
        return m_id;
    }
    QString fileName() const
    {
        return m_fileName;
    }
    QByteArray data() const
    {
        return m_data;
    }
    QVariantMap summary() const
    {
        return m_summary;
    }
    bool canceled() const
    {
        return m_canceled;
    }
    bool active() const
    {
        return m_active;
    }
    EchoPluginContext *context() const;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *track(EchoMusic::EchoPluginOperation *operation);
    void finish(bool canceled);
    void cancel();
signals:
    void changed();

private:
    EchoPluginContext &m_scope;
    QPointer<EchoPluginContext> m_context;
    QObject *m_owner;
    QString m_id, m_fileName;
    QByteArray m_data;
    QVariantMap m_summary;
    QList<QPointer<EchoPluginOperation>> m_linked;
    bool m_active = true, m_canceled = false;
};
class EchoPluginBackups final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginBackups(EchoPluginContext &scope);
    ~EchoPluginBackups() override;
    Q_INVOKABLE EchoMusic::EchoPluginOperation *create(const QVariantMap &scope = {});
    Q_INVOKABLE EchoMusic::EchoPluginOperation *inspect(const QByteArray &data);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *restore(const QString &token,
                                                        const QVariantMap &scope = {});
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerProvider(const QString &id,
                                                                    const QJSValue &definition);

private:
    QString owner() const;
    EchoPluginOperation *request(const QString &method, const QVariantMap &, const QByteArray &,
                                 const QString &token);
    EchoPluginContext &m_scope;
    QPointer<BackupHost> m_host;
};
} // namespace EchoMusic
