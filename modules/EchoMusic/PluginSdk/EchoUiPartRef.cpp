#include "EchoUiPartRef.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include <QJsonDocument>
namespace EchoMusic
{
EchoUiPartRef::EchoUiPartRef(EchoPluginContext &scope, EchoUiPartRegistration &part,
                             QObject *parent)
    : QObject(parent), m_scope(&scope), m_part(&part), m_id(part.identity()), m_role(part.role()),
      m_key(part.key()), m_surface(part.surface()), m_window(part.windowId()),
      m_properties(part.properties()), m_actions(part.actions()), m_replaceable(part.replaceable())
{
    m_epoch = scope.accountEpoch();
    connect(&part, &EchoUiPartRegistration::changed, this, &EchoUiPartRef::notify);
    connect(&part, &QObject::destroyed, this, &EchoUiPartRef::notify);
    connect(&part, &EchoUiPartRegistration::eventOccurred, this,
            [this](const auto &event, const auto &payload) {
                if (valid())
                    emit eventOccurred(event, payload);
            });
    connect(&scope, &EchoPluginContext::activeChanged, this, &EchoUiPartRef::notify);
    connect(&scope, &QObject::destroyed, this, &EchoUiPartRef::notify);
    m_registry = EchoUiPartRegistry::forEngine(scope.engine());
    if (m_registry)
        connect(m_registry, &EchoUiPartRegistry::removed, this, [this](const auto &id) {
            if (id == m_id)
                notify();
        });
    watchAncestors();
}
bool EchoUiPartRef::valid() const
{
    return m_scope && m_scope->active() && m_scope->accountEpoch() == m_epoch && m_part &&
           m_part->identity() == m_id;
}
bool EchoUiPartRef::belongsTo(const EchoPluginContext *scope) const
{
    return m_scope == scope;
}
EchoUiPartRegistration *EchoUiPartRef::registration() const
{
    return valid() ? m_part.data() : nullptr;
}
bool EchoUiPartRef::visible() const
{
    return valid() && m_part->presented();
}
QRectF EchoUiPartRef::geometry() const
{
    return valid() ? m_part->geometry() : QRectF();
}
QVariantMap EchoUiPartRef::visuals() const
{
    if (!valid())
        return {};
    auto values = m_part->baseValues();
    const auto overrides = m_part->overrides();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        values.insert(it.key(), it.value());
    return values;
}
void EchoUiPartRef::notify()
{
    if (!valid() && !std::exchange(m_invalidated, true)) {
        for (const auto &connection : m_ancestors)
            disconnect(connection);
        m_ancestors.clear();
        if (m_part)
            disconnect(m_part, nullptr, this, nullptr);
        if (m_scope)
            disconnect(m_scope, nullptr, this, nullptr);
        if (m_registry)
            disconnect(m_registry, nullptr, this, nullptr);
        m_part = nullptr;
        emit invalidated();
    }
    emit changed();
}
void EchoUiPartRef::watchAncestors()
{
    for (const auto &c : m_ancestors)
        disconnect(c);
    m_ancestors.clear();
    if (!valid())
        return;
    for (auto *item = m_part->target(); item; item = item->parentItem()) {
        const auto update = [this] { notify(); };
        m_ancestors << connect(item, &QQuickItem::widthChanged, this, update)
                    << connect(item, &QQuickItem::heightChanged, this, update)
                    << connect(item, &QQuickItem::clipChanged, this, update)
                    << connect(item, &QQuickItem::xChanged, this, update)
                    << connect(item, &QQuickItem::yChanged, this, update)
                    << connect(item, &QQuickItem::scaleChanged, this, update)
                    << connect(item, &QQuickItem::rotationChanged, this, update)
                    << connect(item, &QQuickItem::parentChanged, this, [this] {
                           watchAncestors();
                           notify();
                       });
    }
}
EchoPluginOperation *EchoUiPartRef::invoke(const QString &action, const QVariantMap &arguments)
{
    if (!m_scope)
        return nullptr;
    if (!valid() || !m_actions.contains(action))
        return m_scope->rejected("Unavailable", "UI part/action is no longer available");
    if (QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact).size() > 65536)
        return m_scope->rejected("InvalidArguments", "UI part action exceeds 64 KiB");
    // Dispatch through the host's declared action, keeping normal enabled and
    // session checks. No private QObject or slot name is returned to plugins.
    auto *op = m_scope->operation();
    if (op->finished())
        return op;
    if (!m_part->invoke(action, arguments))
        op->fail("Unavailable", "UI part action is disabled or hidden");
    else
        op->succeed();
    return op;
}
} // namespace EchoMusic
