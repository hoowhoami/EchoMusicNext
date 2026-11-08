#include "EchoUiScope.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiValue.h"
#include <QJsonDocument>
#include <QQuickItem>
#include <QQmlEngine>

namespace EchoMusic
{
EchoUiScope::EchoUiScope(QObject *object)
    : QObject(object), m_root(qobject_cast<QQuickItem *>(object))
{
}
EchoUiScope::~EchoUiScope() = default;
EchoUiScope *EchoUiScope::qmlAttachedProperties(QObject *object)
{
    return new EchoUiScope(object);
}
EchoUiScope *EchoUiScope::get(QObject *object, bool create)
{
    return object ? qobject_cast<EchoUiScope *>(
                        qmlAttachedPropertiesObject<EchoUiScope>(object, create))
                  : nullptr;
}
void EchoUiScope::mark(QQuickItem *root, const QString &owner, const QString &surface,
                       const QString &key)
{
    if (auto *scope = get(root, true))
        scope->configure(owner, surface, key);
}
bool EchoUiScope::accepts(QQuickItem *item)
{
    for (auto *object = static_cast<QObject *>(item); object; object = object->parent())
        if (auto *scope = get(object); scope && !scope->active())
            return false;
    for (; item; item = item->parentItem())
        if (auto *scope = get(item); scope && !scope->active())
            return false;
    return true;
}
void EchoUiScope::invalidate()
{
    const QPointer<EchoUiScope> self(this);
    ++m_generation;
    if (m_root)
        if (auto *engine = qmlEngine(m_root))
            if (auto *registry = EchoUiPartRegistry::forEngine(*engine))
                registry->scopeChanged(m_root);
    if (self)
        emit changed();
}
void EchoUiScope::renew()
{
    invalidate();
}
void EchoUiScope::configure(const QString &owner, const QString &surface, const QString &key)
{
    if (m_owner == owner && m_surface == surface && m_key == key)
        return;
    m_owner = owner;
    m_surface = surface;
    m_key = key;
    invalidate();
}
#define ECHO_SCOPE_SETTER(Name, Member)                                                            \
    void EchoUiScope::set##Name(const QString &value)                                              \
    {                                                                                              \
        if (Member != value) {                                                                     \
            Member = value;                                                                        \
            invalidate();                                                                          \
        }                                                                                          \
    }
ECHO_SCOPE_SETTER(Owner, m_owner)
ECHO_SCOPE_SETTER(Surface, m_surface)
ECHO_SCOPE_SETTER(Key, m_key)
ECHO_SCOPE_SETTER(ResourceKey, m_resource)
#undef ECHO_SCOPE_SETTER
void EchoUiScope::setActive(bool value)
{
    if (m_active == value)
        return;
    m_active = value;
    invalidate();
}
void EchoUiScope::setModelContext(const QVariantMap &value)
{
    qsizetype budget = 65536;
    const auto normalized = EchoUiValue::json(value, budget).toMap();
    if (QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact).size() > 65536 ||
        normalized != value) {
        qWarning("UI model context requires finite JSON within 64 KiB");
        return;
    }
    if (m_modelContext == normalized)
        return;
    m_modelContext = normalized;
    ++m_contextRevision;
    emit modelContextChanged();
    if (m_root)
        if (auto *engine = qmlEngine(m_root))
            EchoUiPartRegistry::forEngine(*engine)->modelContextChanged(m_root);
}
void EchoUiScope::setContextActive(bool value)
{
    if (m_contextActive == value)
        return;
    m_contextActive = value;
    ++m_contextRevision;
    emit modelContextChanged();
    if (m_root)
        if (auto *engine = qmlEngine(m_root))
            EchoUiPartRegistry::forEngine(*engine)->modelContextChanged(m_root);
}
} // namespace EchoMusic
