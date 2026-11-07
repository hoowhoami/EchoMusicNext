#include "EchoUiContentFactory.h"
#include "EchoUiLayout.h"
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>
namespace EchoMusic
{
void EchoUiNativeContentFactory::setComponent(QQmlComponent *component)
{
    if (m_component == component)
        return;
    if (m_component)
        disconnect(m_component, nullptr, this, nullptr);
    m_component = component;
    if (component)
        connect(component, &QQmlComponent::statusChanged, this, &EchoUiContentFactory::changed);
    emit changed();
}
void EchoUiNativeContentFactory::setInitialProperties(const QVariantMap &properties)
{
    if (m_properties == properties)
        return;
    m_properties = properties;
    emit changed();
}
bool EchoUiNativeContentFactory::ready() const
{
    return m_component && m_component->isReady();
}
QQuickItem *EchoUiNativeContentFactory::create(QQuickItem *parent, EchoUiContentFactory *)
{
    if (!ready())
        return nullptr;
    auto *object = m_component->beginCreate(m_component->creationContext());
    auto *item = qobject_cast<QQuickItem *>(object);
    if (item) {
        item->setProperty("_echoPluginOwner", m_ownerTag);
        item->setParent(parent);
        item->setParentItem(parent);
        item->setSize(parent->size());
        m_component->setInitialProperties(item, m_properties);
    }
    m_component->completeCreate();
    if (!item || m_component->isError()) {
        delete object;
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
    return item;
}
namespace
{
class RetainedContent final : public QQuickItem
{
public:
    RetainedContent(EchoUiRetainedContentFactory *factory, QQuickItem *body, QQuickItem *parent)
        : QQuickItem(parent), m_factory(factory), m_body(body)
    {
        // Reclaim borrowed host UI before any plugin scope is invalidated or
        // hidden. This is independent of QML binding evaluation order.
        for (auto *ancestor = parent; ancestor; ancestor = ancestor->parentItem())
            if (auto *host = qobject_cast<EchoUiContentHost *>(ancestor))
                connect(host, &EchoUiContentHost::restoring, this, [this](QQuickItem *view) {
                    for (auto *ancestor = static_cast<QQuickItem *>(this); ancestor;
                         ancestor = ancestor->parentItem())
                        if (ancestor == view) {
                            if (m_factory)
                                m_factory->restoreBeforeRetire(this);
                            break;
                        }
                });
        setSize(parent->size().isEmpty() ? body->size() : parent->size());
        body->setParentItem(this);
        body->setVisible(true);
        body->setSize(size());
        setImplicitWidth(body->implicitWidth());
        setImplicitHeight(body->implicitHeight());
        connect(body, &QQuickItem::implicitWidthChanged, this, [this] {
            if (m_body)
                setImplicitWidth(m_body->implicitWidth());
        });
        connect(body, &QQuickItem::implicitHeightChanged, this, [this] {
            if (m_body)
                setImplicitHeight(m_body->implicitHeight());
        });
    }
    ~RetainedContent() override
    {
        if (m_factory)
            m_factory->restore(this);
    }

protected:
    void geometryChange(const QRectF &n, const QRectF &o) override
    {
        QQuickItem::geometryChange(n, o);
        if (m_body && m_body->parentItem() == this)
            m_body->setSize(n.size());
    }

private:
    QPointer<EchoUiRetainedContentFactory> m_factory;
    QPointer<QQuickItem> m_body;
};
} // namespace
void EchoUiRetainedContentFactory::setContent(QQuickItem *content)
{
    if (m_content == content)
        return;
    m_content = content;
    emit changed();
}
void EchoUiRetainedContentFactory::setOwnerTag(const QString &owner)
{
    m_ownerTag = owner;
    if (m_content)
        m_content->setProperty("_echoPluginOwner", owner);
}
void EchoUiRetainedContentFactory::setFallbackVisible(bool visible)
{
    if (m_fallbackVisible == visible)
        return;
    m_fallbackVisible = visible;
    if (visible && m_content) {
        m_content->setParentItem(qobject_cast<QQuickItem *>(m_content->parent()));
        m_content->setVisible(true);
    }
    emit changed();
}
void EchoUiRetainedContentFactory::restore(QQuickItem *holder)
{
    if (m_content && m_content->parentItem() == holder) {
        m_content->setParentItem(qobject_cast<QQuickItem *>(m_content->parent()));
        m_content->setVisible(m_fallbackVisible);
    }
}
void EchoUiRetainedContentFactory::restoreBeforeRetire(QQuickItem *holder)
{
    if (m_content && m_content->parentItem() == holder) {
        m_content->setParentItem(qobject_cast<QQuickItem *>(m_content->parent()));
        m_content->setVisible(m_nativeVisible);
    }
}
void EchoUiRetainedContentFactory::conceal()
{
    if (m_content && m_content->parentItem() == qobject_cast<QQuickItem *>(m_content->parent()) &&
        !m_fallbackVisible)
        m_content->setVisible(false);
}
QQuickItem *EchoUiRetainedContentFactory::create(QQuickItem *parent, EchoUiContentFactory *)
{
    return m_content ? new RetainedContent(this, m_content, parent) : nullptr;
}
EchoUiLayerContentFactory::EchoUiLayerContentFactory(EchoUiContentFactory *provider,
                                                     EchoUiContentFactory *content, QObject *parent)
    : EchoUiContentFactory(parent), m_provider(provider), m_content(content)
{
    if (provider) {
        connect(provider, &EchoUiContentFactory::changed, this, &EchoUiContentFactory::changed);
        connect(provider, &EchoUiContentFactory::presentationChanged, this,
                &EchoUiContentFactory::presentationChanged);
    }
    if (content)
        connect(content, &EchoUiContentFactory::changed, this, &EchoUiContentFactory::changed);
}
bool EchoUiLayerContentFactory::suspended() const
{
    return m_provider && m_provider->suspended();
}
bool EchoUiLayerContentFactory::ready() const
{
    return m_provider && m_provider->ready();
}
QQuickItem *EchoUiLayerContentFactory::create(QQuickItem *parent, EchoUiContentFactory *)
{
    auto *item = ready() ? m_provider->create(parent, m_content) : nullptr;
    if (item)
        retain(item);
    return item;
}
void EchoUiLayerContentFactory::retain(QQuickItem *item)
{
    ++m_presentations;
    connect(item, &QObject::destroyed, this, [this] {
        --m_presentations;
        collect();
    });
    if (auto *content = qobject_cast<EchoUiLayerContentFactory *>(m_content.data()))
        content->retain(item);
}
void EchoUiLayerContentFactory::setRegistryActive(bool active)
{
    m_registryActive = active;
    collect();
}
void EchoUiLayerContentFactory::collect()
{
    if (m_registryActive || m_presentations || std::exchange(m_collectionPending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_collectionPending = false;
        if (!m_registryActive && !m_presentations)
            delete this;
    });
}
EchoUiContentHost::EchoUiContentHost(QQuickItem *parent) : QQuickItem(parent) {}
EchoUiContentHost::~EchoUiContentHost()
{
    release();
}
void EchoUiContentHost::setFactory(EchoUiContentFactory *factory)
{
    if (m_factory == factory)
        return;
    for (auto connection : m_factoryConnections)
        disconnect(connection);
    m_factoryConnections.clear();
    m_factory = factory;
    if (factory) {
        m_factoryConnections << connect(factory, &EchoUiContentFactory::presentationChanged, this,
                                        [this] {
                                            if (m_factory && m_factory->suspended() &&
                                                !m_constructing && !m_publicationHolds) {
                                                const QPointer<EchoUiContentHost> self(this);
                                                release();
                                                if (!self)
                                                    return;
                                                emit itemChanged();
                                            }
                                            if (m_factory && (m_factory->suspended() ||
                                                              (!m_item && !m_constructing)))
                                                requestReconcile();
                                        })
                             << connect(factory, &EchoUiContentFactory::changed, this,
                                        [this] { requestReconcile(); })
                             << connect(factory, &QObject::destroyed, this, [this] {
                                    m_factory = nullptr;
                                    requestReconcile();
                                });
    }
    if (!m_item && factory) {
        const auto hint = factory->naturalSizeHint();
        if (hint.isValid())
            setImplicitSize(hint.width(), hint.height());
    }
    emit factoryChanged();
    requestReconcile(false);
}
void EchoUiContentHost::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    if (!active && !m_constructing && !m_publicationHolds) {
        const QPointer<EchoUiContentHost> self(this);
        release();
        if (!self)
            return;
        emit itemChanged();
    }
    requestReconcile();
}
void EchoUiContentHost::componentComplete()
{
    QQuickItem::componentComplete();
    m_complete = true;
    requestReconcile();
}
void EchoUiContentHost::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickItem::geometryChange(n, o);
    sizeContent();
}
void EchoUiContentHost::sizeContent()
{
    if (m_item)
        m_item->setSize(size());
}
void EchoUiContentHost::release()
{
    for (auto connection : m_itemConnections)
        disconnect(connection);
    m_itemConnections.clear();
    if (QPointer<QQuickItem> item = m_item) {
        m_item = nullptr;
        // Cancel the scoped work before detaching. QObject deletion is deferred
        // so a click/binding callback may safely finish on its own view.
        emit restoring(item);
        emit retiring(item);
        if (item) {
            item->setVisible(false);
            item->setParentItem(nullptr);
            item->deleteLater();
        }
    }
    // Keep measured geometry while suspended so layout can restore the view.
}
void EchoUiContentHost::beginPublicationHold()
{
    if (m_publicationHolds++ == 0) {
        setProperty("_echoUiPublicationHeld", true);
        m_heldFactory = m_factory;
        m_heldActive = m_active;
        m_heldPending = m_pending;
    }
}
void EchoUiContentHost::endPublicationHold(bool commit)
{
    if (m_publicationHolds <= 0 || --m_publicationHolds > 0)
        return;
    setProperty("_echoUiPublicationHeld", false);
    if (!commit && m_factory == m_heldFactory && m_active == m_heldActive && m_item) {
        ++m_reconcileRevision;
        m_pending = false;
        if (m_heldPending)
            requestReconcile();
    } else if (m_factory == m_heldFactory && m_active == m_heldActive && m_item && !m_heldPending) {
        ++m_reconcileRevision;
        m_pending = false;
    } else {
        m_pending = true;
        flush();
    }
    m_heldFactory = nullptr;
}
void EchoUiContentHost::flush()
{
    if (m_publicationHolds || !m_pending)
        return;
    ++m_reconcileRevision;
    m_pending = false;
    reconcile();
}
void EchoUiContentHost::requestReconcile(bool)
{
    if (m_publicationHolds)
        return;
    if (std::exchange(m_pending, true))
        return;
    const auto revision = ++m_reconcileRevision;
    QTimer::singleShot(0, this, [this, revision] {
        if (revision != m_reconcileRevision)
            return;
        m_pending = false;
        reconcile();
    });
}
void EchoUiContentHost::reconcile()
{
    if (!m_complete || m_constructing || m_publicationHolds)
        return;
    if (!m_active || !m_factory || m_factory->suspended()) {
        const QPointer<EchoUiContentHost> self(this);
        release();
        if (self)
            emit itemChanged();
        return;
    }
    if (!m_factory->ready())
        return;
    // A newly incubated wrapper can contain a native Qt layout which has not
    // polished its children yet. Settle that layout before borrowing a live
    // subtree, so its existing skins/GPU views never see a transient 0x0 size.
    for (auto *parent = parentItem(); parent; parent = parent->parentItem())
        if (!EchoUiLayout::kind(parent).isEmpty())
            parent->ensurePolished();
    if (!m_factory || !m_factory->ready() || m_factory->suspended() || !m_active)
        return;
    m_constructing = true;
    const QPointer<EchoUiContentFactory> factory = m_factory;
    auto *next = factory->create(this);
    m_constructing = false;
    if (!next) {
        m_error = QStringLiteral("Unable to construct UI contribution");
        emit itemChanged();
        return;
    }
    if (!m_active || factory != m_factory) {
        delete next;
        requestReconcile();
        return;
    }
    const QPointer<EchoUiContentHost> self(this);
    release();
    if (!self) {
        next->deleteLater();
        return;
    }
    m_item = next;
    m_error.clear();
    auto measure = [this] {
        if (m_item) {
            setImplicitWidth(m_item->implicitWidth());
            setImplicitHeight(m_item->implicitHeight());
        }
    };
    m_itemConnections << connect(next, &QQuickItem::implicitWidthChanged, this, measure)
                      << connect(next, &QQuickItem::implicitHeightChanged, this, measure);
    measure();
    sizeContent();
    emit itemChanged();
}
} // namespace EchoMusic
