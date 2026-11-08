#pragma once
#include "EchoPluginRegistration.h"
#include "EchoUiModelAdapter.h"
#include <QPointer>
#include <optional>
#include <algorithm>
namespace EchoMusic
{
class EchoPluginUiParts;
class EchoUiContribution;
class EchoUiPartRegistry;
class EchoUiPartRegistration;
class EchoPluginOperation;
class EchoUiPluginContentFactory;
class EchoUiPluginModelRecipe : public EchoUiModelRecipe
{
    Q_OBJECT
public:
    EchoUiPluginModelRecipe(EchoPluginUiParts &, EchoUiPartRegistration &, QString key, QObject *);
    bool active() const override;
    bool select(const QVariantMap &) override;
    bool activate(const QVariantMap &, const QVariantMap &) override;
    bool arm(const QVariantMap &) override;
    EchoUiContentFactory *factory(QObject *, const QVariantMap &, const QVariantMap &) override;
    void retire(bool preserveAcceptedIntents = false);
    void setContext(const QVariantMap &);
    bool prepare(const QVariantMap &entry, QString &error);
    void commitPrepared();
    void discardPrepared();
signals:
    void constructionFailed(const QString &key, const QString &error);

private:
    QPointer<EchoPluginUiParts> m_owner;
    QPointer<EchoUiPartRegistration> m_part;
    QString m_identity, m_key;
    bool m_retired = false;
    QVariantMap m_selectedEntry;
    QPointer<EchoUiContribution> m_selectedContent;
    QVariantMap m_context, m_armedContext;
    QString m_armedKey;
    QList<QPointer<EchoPluginOperation>> m_invocations;
    QHash<QString, QPointer<EchoUiPluginContentFactory>> m_prepared;
    QPointer<QQuickItem> m_stage;
};
class EchoUiModelContribution final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.parts.contributeModel")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QVariantList matches READ matches NOTIFY changed)
    Q_PROPERTY(QVariantMap definition READ definition NOTIFY changed)
public:
    EchoUiModelContribution(EchoPluginUiParts &, EchoPluginRegistration &, QString id,
                            QVariantMap selector, QVariantMap definition, QJSValue provider = {});
    bool active() const;
    bool pending() const
    {
        return m_requested.has_value() ||
               std::any_of(m_instances.begin(), m_instances.end(),
                           [](const auto &instance) { return bool(instance.providerRequest); });
    }
    QString revision() const
    {
        return QString::number(m_revision);
    }
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
    Q_INVOKABLE void cancel();
    void start();
    void stop();
    static bool validate(EchoPluginUiParts &, const QVariantMap &, QString &,
                         bool allowEmpty = false);
signals:
    void changed();

private:
    struct Instance
    {
        QPointer<EchoUiModelAdapter> adapter;
        QPointer<EchoUiPluginModelRecipe> recipe;
        QString token;
        QVariantMap context, providedDefinition, priorDefinition;
        QPointer<EchoPluginOperation> providerRequest;
        QString providerError, runtimeError;
        bool resolved = false, needsProvider = true;
    };
    void schedule();
    void reconcile();
    void ensureAdapter(const QString &, EchoUiPartRegistration &, Instance &);
    void requestContext(const QString &, EchoUiPartRegistration &, Instance &, const QVariantMap &);
    friend class EchoUiBatch;
    QPointer<EchoPluginUiParts> m_owner;
    QPointer<EchoPluginRegistration> m_lease;
    QPointer<EchoUiPartRegistry> m_registry;
    QString m_id, m_error;
    QVariantMap m_selector, m_definition;
    std::optional<QVariantMap> m_requested;
    quint64 m_revision = 0;
    QHash<QString, Instance> m_instances;
    QVariantList m_matches;
    QList<QMetaObject::Connection> m_connections;
    bool m_prepareOnly = false;
    bool m_started = false, m_pending = false, m_applying = false;
    QJSValue m_provider;
};
} // namespace EchoMusic
