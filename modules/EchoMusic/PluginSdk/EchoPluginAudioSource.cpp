#include "EchoPluginAudioSource.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include <QJSValueIterator>
#include <QTimer>
#include <cmath>
#include <utility>

namespace EchoMusic
{
bool EchoPluginSourceRequest::track(EchoPluginOperation *operation)
{
    if (!operation)
        return false;
    m_operations.removeIf([](const auto &op) { return !op || op->finished(); });
    if (m_canceled || m_operations.size() >= 128) {
        operation->cancel();
        return false;
    }
    if (!operation->finished() && !m_operations.contains(operation))
        m_operations.append(operation);
    return true;
}
void EchoPluginSourceRequest::cancel()
{
    if (std::exchange(m_canceled, true))
        return;
    const auto pending = std::exchange(m_operations, {});
    for (const auto &op : pending)
        if (op)
            op->cancel();
    emit canceledChanged();
}
EchoPluginAudioSource::EchoPluginAudioSource(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope)
{
}
EchoPluginRegistration *EchoPluginAudioSource::registerResolver(const QString &id,
                                                                const QJSValue &definition)
{
    if (m_scope.contextId() != "main" || !m_scope.package().capabilities.contains("audioSource") ||
        !definition.isObject() || definition.isArray())
        return nullptr;
    const QStringList fields{"order",   "position",  "match",
                             "resolve", "transform", "transformStages"};
    QJSValueIterator iterator(definition);
    while (iterator.hasNext()) {
        iterator.next();
        if (!fields.contains(iterator.name()))
            return nullptr;
    }
    PlaybackSourceExtensions::Resolver resolver;
    const auto order = definition.property("order");
    if (!order.isUndefined() &&
        (!order.isNumber() || !std::isfinite(order.toNumber()) ||
         std::floor(order.toNumber()) != order.toNumber() || std::abs(order.toNumber()) > 1000000))
        return nullptr;
    resolver.order = order.isUndefined() ? 1000 : order.toInt();
    const auto position = definition.property("position");
    resolver.position = position.isUndefined() ? "before-catalog" : position.toString();
    if (!QStringList{"before-catalog", "after-catalog", "final-fallback"}.contains(
            resolver.position))
        return nullptr;
    for (const auto &method : {"match", "resolve", "transform"})
        if (!definition.property(method).isUndefined() && !definition.property(method).isCallable())
            return nullptr;
    resolver.match = definition.property("match").isCallable();
    resolver.resolve = definition.property("resolve").isCallable();
    resolver.transform = definition.property("transform").isCallable();
    if (!resolver.resolve && !resolver.transform)
        return nullptr;
    const auto stages = definition.property("transformStages");
    if (stages.isUndefined())
        resolver.stages = PlaybackSourceExtensions::stages();
    else if (stages.isArray()) {
        const auto count = stages.property("length").toUInt();
        if (!count || count > 5)
            return nullptr;
        for (quint32 i = 0; i < count; ++i) {
            const auto value = stages.property(i);
            if (!value.isString() ||
                !PlaybackSourceExtensions::stages().contains(value.toString()) ||
                resolver.stages.contains(value.toString()))
                return nullptr;
            resolver.stages.append(value.toString());
        }
    } else
        return nullptr;
    auto *lease = m_scope.registration("audioSource", id);
    if (!lease)
        return nullptr;
    const QPointer<EchoPluginRegistration> guard(lease);
    const QPointer<EchoPluginContext> scope(&m_scope);
    resolver.key = lease->key();
    resolver.revision = lease->revision();
    resolver.valid = [scope, guard] {
        return scope && scope->active() && scope->committed() && guard && guard->active();
    };
    resolver.report = [scope, id](const QString &message) {
        if (scope && scope->active())
            emit scope->callbackFailed("Audio source " + id + ": " + message);
    };
    resolver.invoke = [scope, guard, definition](const QString &method,
                                                 const QVariantMap &arguments, QObject *lifetime,
                                                 PlaybackSourceExtensions::Reply reply) {
        if (!scope || !guard || !guard->active() || !scope->active()) {
            reply(false, {});
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
            reply(op->state() == EchoPluginOperation::Succeeded, op->result());
        };
        connect(op, &EchoPluginOperation::completed, lifetime, settle);
        if (op->finished())
            settle();
    };
    auto *registry = m_scope.host().playback.sourceExtensions();
    lease->stage([registry, resolver] { return registry->add(resolver); },
                 [registry, key = resolver.key, revision = resolver.revision] {
                     registry->remove(key, revision);
                 });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
} // namespace EchoMusic
