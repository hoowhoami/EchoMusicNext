#include "EchoPluginFavorites.h"
#include "EchoPluginAudioSource.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include <QJSValueIterator>
#include <QRegularExpression>

namespace EchoMusic
{
EchoPluginFavoriteProvider::EchoPluginFavoriteProvider(EchoPluginRegistration &lease,
                                                       PlaybackFavoriteExtensions &registry,
                                                       QObject *parent)
    : QObject(parent), m_lease(&lease), m_registry(registry)
{
    connect(&lease, &EchoPluginRegistration::activeChanged, this,
            &EchoPluginFavoriteProvider::activeChanged);
}
bool EchoPluginFavoriteProvider::active() const
{
    return m_lease && m_lease->active();
}
QString EchoPluginFavoriteProvider::key() const
{
    return m_lease ? m_lease->key() : QString();
}
QString EchoPluginFavoriteProvider::revision() const
{
    return m_lease ? m_lease->revision() : QString();
}
bool EchoPluginFavoriteProvider::invalidate(const QString &sourceId)
{
    return active() && m_registry.invalidate(key(), revision(), sourceId);
}
void EchoPluginFavoriteProvider::dispose()
{
    if (m_lease)
        m_lease->dispose();
}
EchoPluginFavorites::EchoPluginFavorites(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
}
EchoPluginFavoriteProvider *EchoPluginFavorites::registerProvider(const QString &id,
                                                                  const QJSValue &definition)
{
    if (!m_scope.active() || m_scope.contextId() != "main" ||
        !m_scope.package().capabilities.contains("audioSource") || !definition.isObject() ||
        definition.isArray())
        return nullptr;
    QJSValueIterator fields(definition);
    while (fields.hasNext()) {
        fields.next();
        if (!QStringList{"sourcePrefix", "getState", "setState"}.contains(fields.name()))
            return nullptr;
    }
    const auto prefixValue = definition.property("sourcePrefix");
    const auto prefix = prefixValue.toString();
    if (!prefixValue.isString() || !prefix.startsWith(m_scope.id() + "/") ||
        !QRegularExpression("^[a-zA-Z0-9_.:/-]{1,256}$").match(prefix).hasMatch() ||
        !definition.property("getState").isCallable() ||
        !definition.property("setState").isCallable())
        return nullptr;
    auto *lease = m_scope.registration("favoriteProvider", id);
    if (!lease)
        return nullptr;
    const QPointer<EchoPluginRegistration> guard(lease);
    const QPointer<EchoPluginContext> scope(&m_scope);
    PlaybackFavoriteExtensions::Provider provider;
    provider.key = lease->key();
    provider.revision = lease->revision();
    provider.sourcePrefix = prefix;
    provider.valid = [scope, guard] {
        return scope && scope->active() && scope->committed() && guard && guard->active();
    };
    provider.invoke = [scope, guard, definition](const QString &method,
                                                 const QVariantMap &arguments, QObject *lifetime,
                                                 PlaybackFavoriteExtensions::Reply reply) {
        if (!scope || !scope->active() || !guard || !guard->active()) {
            reply(false, {}, QString());
            return;
        }
        auto *request = new EchoPluginSourceRequest(lifetime);
        QQmlEngine::setObjectOwnership(request, QQmlEngine::CppOwnership);
        connect(lifetime, &QObject::destroyed, request, [request] { request->cancel(); });
        auto values = arguments;
        values.insert("request", QVariant::fromValue(request));
        auto *op = scope->invokeRegistered(definition.property(method), guard, values);
        connect(lifetime, &QObject::destroyed, op, &EchoPluginOperation::cancel);
        connect(guard, &EchoPluginRegistration::activeChanged, op, [guard, op] {
            if (!guard || !guard->active())
                op->cancel();
        });
        const auto settle = [scope, op, reply] {
            if (op->state() == EchoPluginOperation::Failed && scope && scope->active())
                emit scope->callbackFailed(op->errorMessage());
            reply(op->state() == EchoPluginOperation::Succeeded, op->result(), op->errorMessage());
        };
        connect(op, &EchoPluginOperation::completed, lifetime, settle);
        if (op->finished())
            settle();
    };
    auto *registry = m_scope.host().playback.favoriteExtensions();
    lease->stage([registry, provider] { return registry->add(provider); },
                 [registry, key = provider.key, revision = provider.revision] {
                     registry->remove(key, revision);
                 });
    auto *handle = new EchoPluginFavoriteProvider(*lease, *registry, &m_scope);
    QQmlEngine::setObjectOwnership(handle, QQmlEngine::CppOwnership);
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return handle;
}
} // namespace EchoMusic
