#pragma once
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
// Engine-owned render recipe. SDK facades expose only scoped content hosts,
// never the native component, its creation context or a private item pointer.
class EchoUiContentFactory : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    using QObject::QObject;
    virtual void setOwnerTag(const QString &) {}
    virtual bool suspended() const
    {
        return false;
    }
    virtual bool ready() const
    {
        return true;
    }
    virtual QSizeF naturalSizeHint() const
    {
        return {};
    }
    virtual QQuickItem *create(QQuickItem *parent, EchoUiContentFactory *content = nullptr) = 0;
signals:
    void changed();
    void presentationChanged();
};
class EchoUiNativeContentFactory : public EchoUiContentFactory
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQmlComponent *component READ component WRITE setComponent NOTIFY changed)
    Q_PROPERTY(QVariantMap initialProperties READ initialProperties WRITE setInitialProperties
                   NOTIFY changed)
public:
    using EchoUiContentFactory::EchoUiContentFactory;
    QQmlComponent *component() const
    {
        return m_component;
    }
    QVariantMap initialProperties() const
    {
        return m_properties;
    }
    void setComponent(QQmlComponent *);
    void setOwnerTag(const QString &owner) override
    {
        m_ownerTag = owner;
    }
    void setInitialProperties(const QVariantMap &);
    bool ready() const override;
    QQuickItem *create(QQuickItem *, EchoUiContentFactory * = nullptr) override;

private:
    QPointer<QQmlComponent> m_component;
    QVariantMap m_properties;
    QString m_ownerTag = "@host";
};
// Borrows the live native subtree without exposing it to the plugin. QObject
// ownership stays with the host; changing wrappers preserves UI/business state.
class EchoUiRetainedContentFactory : public EchoUiContentFactory
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool fallbackVisible READ fallbackVisible WRITE setFallbackVisible NOTIFY changed)
    Q_PROPERTY(QQuickItem *content READ content WRITE setContent NOTIFY changed)
public:
    using EchoUiContentFactory::EchoUiContentFactory;
    QQuickItem *content() const
    {
        return m_content;
    }
    void setContent(QQuickItem *);
    void setOwnerTag(const QString &) override;
    bool fallbackVisible() const
    {
        return m_fallbackVisible;
    }
    void setFallbackVisible(bool);
    void restore(QQuickItem *holder);
    void restoreBeforeRetire(QQuickItem *holder);
    void setNativeVisible(bool visible)
    {
        m_nativeVisible = visible;
    }
    Q_INVOKABLE void conceal();
    bool ready() const override
    {
        return m_content;
    }
    QQuickItem *create(QQuickItem *, EchoUiContentFactory * = nullptr) override;

private:
    QPointer<QQuickItem> m_content;
    bool m_fallbackVisible = true, m_nativeVisible = true;
    QString m_ownerTag = "@host";
};
class EchoUiLayerContentFactory final : public EchoUiContentFactory
{
    Q_OBJECT
public:
    EchoUiLayerContentFactory(EchoUiContentFactory *provider, EchoUiContentFactory *content,
                              QObject *parent);
    bool ready() const override;
    bool suspended() const override;
    QQuickItem *create(QQuickItem *, EchoUiContentFactory * = nullptr) override;
    void setRegistryActive(bool);

private:
    void retain(QQuickItem *);
    void collect();
    int m_presentations = 0;
    bool m_registryActive = true, m_collectionPending = false;
    QPointer<EchoUiContentFactory> m_provider, m_content;
};
class EchoUiContentHost : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EchoMusic::EchoUiContentFactory *factory READ factory WRITE setFactory NOTIFY
                   factoryChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QQuickItem *item READ item NOTIFY itemChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY itemChanged)
public:
    explicit EchoUiContentHost(QQuickItem *parent = nullptr);
    ~EchoUiContentHost() override;
    EchoUiContentFactory *factory() const
    {
        return m_factory;
    }
    bool active() const
    {
        return m_active;
    }
    QQuickItem *item() const
    {
        return m_item;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    void setFactory(EchoUiContentFactory *);
    void setActive(bool);
    void beginPublicationHold();
    void endPublicationHold(bool commit);
    void flush();
    void finishCreation()
    {
        componentComplete();
    }
signals:
    void factoryChanged();
    void activeChanged();
    void itemChanged();
    void restoring(QQuickItem *item);
    void retiring(QQuickItem *item);

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &, const QRectF &) override;

private:
    void reconcile();
    void requestReconcile(bool rebuild = true);
    void release();
    void sizeContent();
    QPointer<EchoUiContentFactory> m_factory;
    QPointer<QQuickItem> m_item;
    QList<QMetaObject::Connection> m_factoryConnections, m_itemConnections;
    QString m_error;
    int m_publicationHolds = 0;
    quint64 m_reconcileRevision = 0;
    QPointer<EchoUiContentFactory> m_heldFactory;
    bool m_heldActive = false, m_heldPending = false;
    bool m_active = true, m_complete = false, m_constructing = false, m_pending = false;
};
} // namespace EchoMusic
