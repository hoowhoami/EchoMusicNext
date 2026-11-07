#pragma once
#include "EchoPluginRegistration.h"
#include "EchoUiModelAdapter.h"
#include <QPointer>
namespace EchoMusic
{
class EchoPluginUiParts;
class EchoUiContribution;
class EchoUiPartRegistry;
class EchoUiPartRegistration;
class EchoUiPluginModelRecipe : public EchoUiModelRecipe
{
    Q_OBJECT
public:
    EchoUiPluginModelRecipe(EchoPluginUiParts &, EchoUiPartRegistration &, QString key, QObject *);
    bool active() const override;
    bool select(const QVariantMap &) override;
    bool activate(const QVariantMap &, const QVariantMap &) override;
    EchoUiContentFactory *factory(QObject *, const QVariantMap &, const QVariantMap &) override;
    void retire();

private:
    QPointer<EchoPluginUiParts> m_owner;
    QPointer<EchoUiPartRegistration> m_part;
    QString m_identity, m_key;
    bool m_retired = false;
    QVariantMap m_selectedEntry;
    QPointer<EchoUiContribution> m_selectedContent;
};
class EchoUiModelContribution final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.parts.contributeModel")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QVariantList matches READ matches NOTIFY changed)
    Q_PROPERTY(QVariantMap definition READ definition NOTIFY changed)
public:
    EchoUiModelContribution(EchoPluginUiParts &, EchoPluginRegistration &, QString id,
                            QVariantMap selector, QVariantMap definition);
    bool active() const;
    QString errorMessage() const
    {
        return m_error;
    }
    QVariantList matches() const
    {
        return m_matches;
    }
    QVariantMap definition() const
    {
        return m_definition;
    }
    Q_INVOKABLE bool update(const QVariantMap &);
    Q_INVOKABLE void dispose();
    void start();
    void stop();
    static bool validate(EchoPluginUiParts &, const QVariantMap &, QString &);
signals:
    void changed();

private:
    struct Instance
    {
        QPointer<EchoUiModelAdapter> adapter;
        QPointer<EchoUiPluginModelRecipe> recipe;
        QString token;
    };
    void schedule();
    void reconcile();
    QPointer<EchoPluginUiParts> m_owner;
    QPointer<EchoPluginRegistration> m_lease;
    QPointer<EchoUiPartRegistry> m_registry;
    QString m_id, m_error;
    QVariantMap m_selector, m_definition;
    QHash<QString, Instance> m_instances;
    QVariantList m_matches;
    QList<QMetaObject::Connection> m_connections;
    bool m_started = false, m_pending = false, m_applying = false;
};
} // namespace EchoMusic
