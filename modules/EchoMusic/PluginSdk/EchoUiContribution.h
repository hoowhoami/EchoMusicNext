#pragma once
#include "EchoUiPartRef.h"
#include "EchoPluginRegistration.h"
#include "EchoUiContentFactory.h"
#include <QHash>
namespace EchoMusic
{
class EchoPluginUiParts;
class EchoPluginContext;
class EchoPluginViewContext;
class EchoUiBatch;
class EchoUiPartRegistration;
class EchoUiPartRegistry;
class EchoUiPluginContentFactory final : public EchoUiContentFactory
{
    Q_OBJECT
public:
    EchoUiPluginContentFactory(EchoPluginContext &, EchoUiPartRegistration &, const QString &,
                               QObject *, bool sibling = false, QString key = {},
                               bool preparing = false);
    ~EchoUiPluginContentFactory() override;
    bool prepare(QQuickItem *);
    bool prepared() const
    {
        return m_preparedView != nullptr;
    }
    void publishPrepared()
    {
        m_preparing = false;
    }
    void discardPrepared();
    QSizeF naturalSizeHint() const override;
    bool ready() const override;
    void setModelData(const QVariantMap &row, const QVariantMap &entry);
    bool updateModelData(const QVariantMap &row, const QVariantMap &entry) override;
    bool suspended() const override;
    bool constructed() const
    {
        return m_constructed;
    }
    bool failed() const
    {
        return m_failed;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QQuickItem *create(QQuickItem *, EchoUiContentFactory * = nullptr) override;
signals:
    void realized();

private:
    void fail(const QString &);
    void bindRetirement(QQuickItem *, QQuickItem *, EchoPluginContext *, EchoPluginViewContext *);
    QPointer<QQuickItem> m_preparedView;
    QPointer<EchoUiPartRef> m_preparedPart;
    QPointer<EchoPluginContext> m_preparedScope;
    QPointer<EchoPluginViewContext> m_preparedPage;
    EchoUiPartRef *m_observer = nullptr;
    QPointer<QQuickItem> m_view;
    QPointer<EchoPluginContext> m_scope;
    QPointer<EchoUiPartRegistration> m_part;
    QQmlComponent m_component;
    QString m_error;
    QString m_key;
    QVariantMap m_modelRow, m_modelEntry;
    bool m_hasModelData = false;
    bool m_failed = false, m_constructed = false, m_sibling = false, m_preparing = false;
};
class EchoUiHostContent : public EchoUiContentHost
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EchoMusic::EchoUiPartRef *part READ part WRITE setPart NOTIFY partChanged)
public:
    using EchoUiContentHost::EchoUiContentHost;
    EchoUiPartRef *part() const
    {
        return m_part;
    }
    void setPart(EchoUiPartRef *);
signals:
    void partChanged();

private:
    QPointer<EchoUiPartRef> m_part;
    QObject m_callbacks;
};
class EchoUiContribution final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by ui.parts.contribute")
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QVariantList matches READ matches NOTIFY changed)
    Q_PROPERTY(QVariantMap definition READ definition NOTIFY changed)
public:
    EchoUiContribution(EchoPluginUiParts &, EchoPluginRegistration &, QString, QVariantMap,
                       QVariantMap);
    ~EchoUiContribution() override;
    bool active() const;
    QString errorMessage() const
    {
        return m_error;
    }
    QVariantList matches() const;
    QVariantMap definition() const
    {
        return m_definition;
    }
    Q_INVOKABLE bool update(const QVariantMap &);
    Q_INVOKABLE void dispose();
    void start();
    void stop();
    static bool validate(EchoPluginContext &, const QVariantMap &, QString &);
signals:
    void changed();

private:
    friend class EchoUiBatch;
    QHash<QString, QPointer<EchoUiPluginContentFactory>> m_preparedFactories;
    struct Instance
    {
        QPointer<EchoUiPartRegistration> part;
        QPointer<EchoUiPluginContentFactory> factory;
        QPointer<QQuickItem> item;
        QString token, state, error;
        QMetaObject::Connection stateConnection;
        bool observing = false;
    };
    void schedule();
    void refreshStructureDemand();
    void reconcile();
    void apply(Instance &);
    void remove(Instance &, bool retire = true);
    QPointer<EchoPluginUiParts> m_owner;
    QPointer<EchoPluginRegistration> m_lease;
    QPointer<EchoUiPartRegistry> m_registry;
    QString m_id, m_error;
    QVariantMap m_selector, m_definition;
    QHash<QString, Instance> m_instances, m_previous;
    QVariantMap m_previousDefinition;
    QPointer<QQmlComponent> m_preflight;
    quint64 m_updateGeneration = 0;
    void commitUpdate(const QVariantMap &);
    void finishUpdate();
    QList<QMetaObject::Connection> m_connections;
    bool m_started = false, m_pending = false, m_applying = false;
    quint64 m_revision = 0;
};
} // namespace EchoMusic
