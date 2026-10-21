#include "EchoUiPartMount.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include <QTimer>
namespace EchoMusic
{
EchoUiPartMount::EchoUiPartMount(EchoPluginUiParts &owner, EchoPluginRegistration &lease,
                                 EchoUiPartRegistration &part, QString position, QString path,
                                 QString id, int priority)
    : QObject(&lease), m_owner(&owner), m_lease(&lease), m_part(&part),
      m_ref(new EchoUiPartRef(owner.scope(), part, this)), m_position(std::move(position)),
      m_path(std::move(path)), m_id(std::move(id)), m_identity(part.identity()),
      m_token(lease.key() + ':' + owner.scope().activationGeneration() + ':' + lease.revision()),
      m_priority(priority)
{
    m_registry = owner.registry();
    QQmlEngine::setObjectOwnership(m_ref, QQmlEngine::CppOwnership);
    connect(m_ref, &EchoUiPartRef::changed, this, [this] {
        reconcile();
        emit changed();
    });
    connect(m_ref, &EchoUiPartRef::invalidated, this, &EchoUiPartMount::dispose);
    connect(&part, &EchoUiPartRegistration::replacementChanged, this, &EchoUiPartMount::reconcile);
    connect(&part, &EchoUiPartRegistration::mountOrderChanged, this, [this] {
        if (m_item)
            m_item->setProperty("_echoPartOrderDirty", true);
        reconcile();
    });
    connect(&lease, &EchoPluginRegistration::activeChanged, this, &EchoUiPartMount::changed);
}
EchoUiPartMount::~EchoUiPartMount()
{
    release();
}
bool EchoUiPartMount::active() const
{
    return m_started && m_lease && m_lease->active() && m_owner && m_owner->scope().active() &&
           m_ref->valid();
}
void EchoUiPartMount::start()
{
    if (m_started)
        return;
    m_started = true;
    reconcile();
    emit changed();
}
void EchoUiPartMount::stop()
{
    m_started = false;
    disconnect(m_ref, nullptr, this, nullptr);
    if (m_part)
        disconnect(m_part, nullptr, this, nullptr);
    if (auto *registry = m_registry.data())
        registry->removeReplacement(m_token);
    release();
    emit changed();
}
void EchoUiPartMount::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
void EchoUiPartMount::release()
{
    ++m_loadGeneration;
    // Destroy the complete QML component and render-owned item tree. Never
    // retain hidden shaders, images, timers or popup recipes on an inactive part.
    if (m_item) {
        auto *item = m_item.data();
        m_item = nullptr;
        if (m_registry)
            m_registry->setMountReady(m_token, false);
        item->setParentItem(nullptr);
        item->deleteLater();
    }
    if (m_component) {
        auto *component = m_component.data();
        m_component = nullptr;
        component->deleteLater();
    }
}
void EchoUiPartMount::fail(const QString &message)
{
    m_error = message;
    if (auto *registry = m_registry.data())
        registry->removeReplacement(m_token);
    release();
    emit changed();
    if (m_owner && m_owner->scope().active())
        emit m_owner->scope().callbackFailed("ui.parts mount " + m_id + ": " + message);
}
void EchoUiPartMount::reconcile()
{
    if (m_constructing)
        return;
    if (!active() || !m_ref->visible() || !m_error.isEmpty()) {
        if (auto *registry = m_registry.data())
            registry->removeReplacement(m_token);
        if (m_item || m_component) {
            release();
            emit changed();
        }
        return;
    }
    {
        auto *registry = m_registry.data();
        if (!registry)
            return;
        m_constructing = true;
        registry->putReplacement(m_token, m_identity, m_owner->scope().id(),
                                 m_owner->scope().contextId() + ":" + m_id, m_priority, m_position);
        m_constructing = false;
        if (m_position == "replace" && m_part->replacementCandidate() != m_token &&
            m_part->replacement() != m_token) {
            release();
            emit changed();
            return;
        }
    }
    if (!m_item && !m_component)
        construct();
    if (m_item)
        place();
}
void EchoUiPartMount::construct()
{
    const auto generation = ++m_loadGeneration;
    m_component = new QQmlComponent(&m_owner->scope().engine(), this);
    connect(m_component, &QQmlComponent::statusChanged, this,
            [this, generation](QQmlComponent::Status status) {
                if (generation != m_loadGeneration || !m_component)
                    return;
                if (status == QQmlComponent::Error) {
                    const auto errors = m_component->errorString();
                    // QQmlComponent may be delivering its signal; destroy at the next
                    // queued turn, guarded by the mount's lease and identity.
                    QTimer::singleShot(0, this, [this, errors, generation] {
                        if (generation == m_loadGeneration && active())
                            fail(errors);
                    });
                } else if (status == QQmlComponent::Ready) {
                    QTimer::singleShot(0, this, [this, generation] {
                        if (generation != m_loadGeneration)
                            return;
                        if (!active() || !m_ref->visible() || !m_component || m_item) {
                            reconcile();
                            return;
                        }
                        if (m_position == "replace" && m_part->replacementCandidate() != m_token) {
                            reconcile();
                            return;
                        }
                        m_constructing = true;
                        const auto keepAlive = m_owner->scope().engine().newQObject(this);
                        const QPointer<QQmlComponent> component = m_component;
                        auto *object = component->createWithInitialProperties(
                            {{"plugin", QVariant::fromValue(&m_owner->scope())},
                             {"part", QVariant::fromValue(m_ref)}});
                        auto *item = qobject_cast<QQuickItem *>(object);
                        m_constructing = false;
                        if (generation != m_loadGeneration || !active() || !m_ref->visible()) {
                            if (object)
                                object->deleteLater();
                            return;
                        }
                        if (!item) {
                            const auto message = component ? component->errorString() : QString();
                            delete object;
                            fail(message.isEmpty() ? "Part component must have an Item root and "
                                                     "typed plugin/part properties"
                                                   : message);
                            return;
                        }
                        QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
                        item->setParent(this);
                        item->setProperty("_echoPartMountToken", m_token);
                        m_item = item;
                        place();
                        if (auto *registry = m_registry.data())
                            registry->setMountReady(m_token, true);
                        emit changed();
                        reconcile();
                    });
                }
            });
    m_component->loadUrl(QUrl::fromLocalFile(m_path), QQmlComponent::Asynchronous);
}
void EchoUiPartMount::place()
{
    if (!m_item || !m_part || !m_part->target())
        return;
    auto *target = m_part->target();
    const bool sibling = m_position == "before" || m_position == "after";
    auto *parent = sibling ? target->parentItem() : target;
    if (!parent)
        return;
    const bool sameParent = m_item->parentItem() == parent;
    const bool fill =
        m_position == "background" || m_position == "foreground" || m_position == "replace";
    if (fill) {
        m_item->setPosition({0, 0});
        m_item->setSize(target->size());
        // Scrolling/animated ancestor geometry needs no child reordering.
        if (sameParent && !m_item->property("_echoPartOrderDirty").toBool())
            return;
    }
    m_item->setParentItem(parent);
    m_item->setProperty("_echoPartOrderDirty", false);
    auto *registry = m_registry.data();
    if (!registry)
        return;
    const auto order = registry->mountOrder(m_identity, m_position);
    QList<QQuickItem *> items;
    const auto children = parent->childItems();
    for (const auto &token : order)
        for (auto *child : children)
            if (child->property("_echoPartMountToken").toString() == token)
                items.append(child);
    if (sibling) {
        if (m_position == "before")
            for (auto *item : items)
                item->stackBefore(target);
        else
            for (auto it = items.crbegin(); it != items.crend(); ++it)
                (*it)->stackAfter(target);
    } else if (m_position == "prepend") {
        QQuickItem *first = nullptr;
        for (auto *child : children)
            if (!items.contains(child)) {
                first = child;
                break;
            }
        if (first)
            for (auto *item : items)
                item->stackBefore(first);
    } else if (m_position == "append") {
        if (!items.isEmpty())
            for (auto *item : items) {
                const auto siblings = parent->childItems();
                if (!siblings.isEmpty() && siblings.last() != item)
                    item->stackAfter(siblings.last());
            }
    } else if (m_position == "background") {
        // A negative-z child lies behind the target's own Rectangle paint and
        // disappears under an opaque Mini/control. Stack above that paint but
        // before its ordinary child content, preserving native layout ownership.
        for (auto *item : items)
            item->setZ(0);
        QQuickItem *first = nullptr;
        for (auto *child : children)
            if (!items.contains(child) && child->z() >= 0) {
                first = child;
                break;
            }
        if (first)
            for (auto *item : items)
                item->stackBefore(first);
        else
            for (int i = 1; i < items.size(); ++i)
                items[i]->stackAfter(items[i - 1]);
    } else {
        const auto rank = order.indexOf(m_token);
        m_item->setZ(10000 + rank);
    }
}
} // namespace EchoMusic
