#pragma once
#include "EchoPluginRegistration.h"
#include "EchoUiContribution.h"
#include "EchoUiModelContribution.h"
#include <QElapsedTimer>
#include <QVariantList>
#include <memory>
namespace EchoMusic
{
class EchoPluginUiParts;
class EchoUiPartRegistry;
// A bounded transaction over the currently resolved target identities. Ordinary
// contribute() remains the persistent selector API for future UI instances.
class EchoUiBatch final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.parts.applyBatch")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QVariantList definition READ definition NOTIFY changed)
    Q_PROPERTY(QVariantList matches READ matches NOTIFY changed)
public:
    EchoUiBatch(EchoPluginUiParts &, EchoPluginRegistration &, QString id, QVariantList);
    ~EchoUiBatch() override;
    bool active() const;
    bool pending() const
    {
        return bool(m_preparing);
    }
    QString state() const
    {
        return !active()           ? "disposed"
               : pending()         ? "preparing"
               : m_current         ? "active"
               : m_error.isEmpty() ? "idle"
                                   : "failed";
    }
    QString revision() const
    {
        return QString::number(m_revision);
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QVariantList definition() const
    {
        return m_definition;
    }
    QVariantList matches() const;
    Q_INVOKABLE bool update(const QVariantList &entries);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dispose();
    void start();
    void stop();
    static bool validate(EchoPluginUiParts &, const QVariantList &, QString &error);
signals:
    void changed();
    void committed();
    void rejected(const QString &message);

private:
    struct Target
    {
        QString entry, logicalId, identity, token;
        QVariantMap selector, definition, style, model;
        int priority = 0;
        QPointer<EchoUiPartRegistration> part;
        QPointer<QQuickItem> item, stage;
        QPointer<EchoUiPluginContentFactory> factory;
        QPointer<EchoPluginRegistration> lease;
        QPointer<EchoUiContribution> contribution;
        QPointer<EchoUiModelContribution> modelContribution;
    };
    struct Revision
    {
        QPointer<QObject> owner;
        QVariantList entries;
        QList<Target> targets;
        QElapsedTimer elapsed;
        quint64 number = 0;
    };
    void schedule();
    void prepare();
    void commit();
    void reject(const QString &);
    void clear(std::unique_ptr<Revision> &);
    QSet<QString> tokens(const Revision *) const;
    QPointer<EchoPluginUiParts> m_owner;
    QPointer<EchoPluginRegistration> m_lease;
    QPointer<EchoUiPartRegistry> m_registry;
    QString m_id, m_error;
    QVariantList m_definition;
    std::unique_ptr<Revision> m_current, m_preparing;
    quint64 m_revision = 0, m_attempt = 0;
    bool m_scheduled = false, m_committing = false;
};
} // namespace EchoMusic
