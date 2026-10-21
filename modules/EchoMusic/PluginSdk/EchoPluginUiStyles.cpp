#include "EchoPluginUiStyles.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistry.h"
namespace EchoMusic
{
EchoPluginUiStyles::EchoPluginUiStyles(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
}
EchoUiPartRegistry *EchoPluginUiStyles::registry() const
{
    return EchoUiPartRegistry::forEngine(m_scope.engine());
}
void EchoPluginUiStyles::error(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}
bool EchoPluginUiStyles::validate(QVariantMap &values, int priority)
{
    QString message;
    if (priority < -10000 || priority > 10000)
        message = "Style priority must be between -10000 and 10000";
    else if (EchoUiPartRegistry::normalize(values, message)) {
        error({});
        return true;
    }
    error(message);
    return false;
}
EchoUiStyleRule *EchoPluginUiStyles::registerRule(const QString &id, const QVariantMap &selector,
                                                  const QVariantMap &input, int priority)
{
    auto values = input;
    if (!EchoUiPartRegistry::validSelector(selector)) {
        error("Invalid UI part selector");
        return nullptr;
    }
    if (selector.contains("id") &&
        (!registry() || !registry()->find(selector.value("id").toString()))) {
        error("UI part instance has expired");
        return nullptr;
    }
    if (!validate(values, priority))
        return nullptr;
    auto *lease = m_scope.registration("uiStyle", id);
    if (!lease) {
        error("Invalid rule ID or closed activation");
        return nullptr;
    }
    const auto token =
        lease->key() + ':' + m_scope.activationGeneration() + ':' + lease->revision();
    auto *rule = new EchoUiStyleRule(*this, *lease, token, id, selector, values, priority);
    m_scope.engine().newQObject(rule);
    QQmlEngine::setObjectOwnership(rule, QQmlEngine::CppOwnership);
    connect(lease, &EchoPluginRegistration::activeChanged, rule, [rule, lease] {
        if (lease->active())
            return;
        // A retained public handle stays readable; discarded inactive handles
        // and their private leases must not accumulate during delegate reuse.
        rule->setParent(nullptr);
        QQmlEngine::setObjectOwnership(rule, QQmlEngine::JavaScriptOwnership);
        lease->deleteLater();
    });
    if (selector.contains("id")) {
        const auto identity = selector.value("id").toString();
        connect(registry(), &EchoUiPartRegistry::removed, lease,
                [identity, lease](const auto &removed) {
                    if (identity == removed)
                        lease->dispose();
                });
    }
    lease->stage([rule] { return rule->publish(); },
                 [registry = QPointer(registry()), token] {
                     if (registry)
                         registry->removeStyle(token);
                 });
    if (m_scope.committed() && !lease->commit()) {
        lease->dispose();
        error("UI style registry is full");
        return nullptr;
    }
    return rule;
}
EchoUiStyleRule::EchoUiStyleRule(EchoPluginUiStyles &owner, EchoPluginRegistration &lease,
                                 QString token, QString id, QVariantMap selector,
                                 QVariantMap values, int priority)
    : QObject(&lease), m_owner(&owner), m_lease(&lease), m_token(std::move(token)),
      m_id(std::move(id)), m_selector(std::move(selector)), m_values(std::move(values)),
      m_priority(priority)
{
    m_key = lease.key();
    m_revision = lease.revision();
    connect(&lease, &EchoPluginRegistration::activeChanged, this, &EchoUiStyleRule::changed);
}
QString EchoUiStyleRule::key() const
{
    return m_key;
}
QString EchoUiStyleRule::revision() const
{
    return m_revision;
}
bool EchoUiStyleRule::active() const
{
    return m_lease && m_lease->active() && m_owner && m_owner->scope().active();
}
bool EchoUiStyleRule::publish()
{
    if (!active())
        return false;
    auto *registry = m_owner->registry();
    return active() && registry &&
           registry->putStyle(m_token, m_owner->scope().id(),
                              m_owner->scope().contextId() + ":" + m_id, m_priority, m_selector,
                              m_values);
}
bool EchoUiStyleRule::update(const QVariantMap &input)
{
    auto values = input;
    if (!active() || !m_owner->validate(values, m_priority))
        return false;
    m_values = values;
    if (m_owner->scope().committed() && !publish())
        return false;
    emit changed();
    return true;
}
bool EchoUiStyleRule::setPriority(int priority)
{
    auto values = m_values;
    if (!active() || !m_owner->validate(values, priority))
        return false;
    m_priority = priority;
    if (m_owner->scope().committed() && !publish())
        return false;
    emit changed();
    return true;
}
void EchoUiStyleRule::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
} // namespace EchoMusic
