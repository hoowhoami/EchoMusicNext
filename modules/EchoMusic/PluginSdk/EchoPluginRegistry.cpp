#include "EchoPluginRegistry.h"
#include "EchoPluginContext.h"
#include <QJSValueIterator>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>

namespace EchoMusic
{
namespace
{
bool jsonValue(const QVariant &value, int depth, qsizetype &budget)
{
    if (depth > 32 || --budget < 0)
        return false;
    if (!value.isValid() || value.isNull())
        return true;
    switch (value.metaType().id()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return true;
    case QMetaType::Double:
    case QMetaType::Float:
        return std::isfinite(value.toDouble());
    case QMetaType::QString:
        budget -= value.toString().size();
        return budget >= 0;
    case QMetaType::QVariantList:
        for (const auto &item : value.toList())
            if (!jsonValue(item, depth + 1, budget))
                return false;
        return true;
    case QMetaType::QVariantMap: {
        const auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            budget -= it.key().size();
            if (!jsonValue(it.value(), depth + 1, budget))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}
bool jsonValue(const QVariant &value)
{
    qsizetype budget = 1024 * 1024;
    return jsonValue(value, 0, budget);
}
} // namespace
EchoPluginRegistry::EchoPluginRegistry(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
}
void EchoPluginRegistry::setResolver(std::function<EchoPluginContext *(const QString &)> resolver)
{
    m_resolver = std::move(resolver);
}
EchoPluginContext *EchoPluginRegistry::provider(const QString &pluginId) const
{
    if (!m_scope.active() || (!m_resolver && m_scope.activationContext() == &m_scope))
        return nullptr;
    const auto &package = m_scope.package();
    const auto range =
        package.dependencies.value(pluginId, package.optionalDependencies.value(pluginId));
    if (range.isEmpty())
        return nullptr;
    auto *scope = m_scope.activationContext() != &m_scope
                      ? m_scope.activationContext()->plugins()->provider(pluginId)
                      : m_resolver(pluginId);
    return scope && scope->active() && scope->committed() &&
                   PluginPackage::versionMatches(range, scope->package().version)
               ? scope
               : nullptr;
}
bool EchoPluginRegistry::isAvailable(const QString &pluginId) const
{
    return provider(pluginId) != nullptr;
}
QString EchoPluginRegistry::version(const QString &pluginId) const
{
    const auto *scope = provider(pluginId);
    return scope ? scope->package().version : QString();
}
QString EchoPluginRegistry::serviceVersion(const QString &pluginId, const QString &serviceId) const
{
    const auto *scope = provider(pluginId);
    return scope ? scope->plugins()->m_services.value(serviceId).version : QString();
}
EchoPluginRegistration *EchoPluginRegistry::registerService(const QString &id,
                                                            const QString &version,
                                                            const QJSValue &methods)
{
    if (m_scope.contextId() != "main")
        return nullptr;
    if (!PluginPackage::versionMatches("=" + version, version) || !methods.isObject() ||
        methods.isArray())
        return nullptr;
    Service service;
    service.version = version;
    QJSValueIterator iterator(methods);
    while (iterator.hasNext()) {
        iterator.next();
        if (!QRegularExpression("^[a-zA-Z][a-zA-Z0-9_]{0,63}$").match(iterator.name()).hasMatch() ||
            !iterator.value().isCallable() || service.methods.size() >= 64)
            return nullptr;
        service.methods.insert(iterator.name(), iterator.value());
    }
    if (service.methods.isEmpty())
        return nullptr;
    auto *lease = m_scope.registration("service", id);
    if (!lease)
        return nullptr;
    service.lease = lease;
    lease->stage(
        [this, id, service] {
            m_services.insert(id, service);
            return true;
        },
        [this, id] { m_services.remove(id); });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginOperation *EchoPluginRegistry::call(const QString &pluginId, const QString &serviceId,
                                              const QString &versionRange, const QString &method,
                                              const QVariantMap &arguments)
{
    if (!jsonValue(arguments))
        return m_scope.rejected("InvalidArguments",
                                "Plugin service arguments must be bounded JSON values");
    auto *scope = provider(pluginId);
    if (!scope)
        return m_scope.rejected("DependencyUnavailable",
                                "Plugin dependency is not active or not declared: " + pluginId);
    const auto service = scope->plugins()->m_services.value(serviceId);
    if (!service.lease || !service.lease->active() ||
        !PluginPackage::versionMatches(versionRange, service.version) ||
        !service.methods.contains(method))
        return m_scope.rejected("ServiceUnavailable",
                                "Plugin service/version/method is unavailable: " + pluginId + '/' +
                                    serviceId + '/' + method);
    auto *operation = m_scope.operation();
    if (operation->finished())
        return operation;
    operation->start();
    auto *remote = scope->invokeRegistered(service.methods.value(method), service.lease, arguments);
    connect(service.lease.data(), &EchoPluginRegistration::activeChanged, remote,
            [lease = service.lease, remote] {
                if (!lease || !lease->active())
                    remote->cancel();
            });
    connect(service.lease.data(), &QObject::destroyed, remote, &EchoPluginOperation::cancel);
    const auto epoch = m_scope.accountEpoch();
    const auto providerEpoch = scope->accountEpoch();
    const QPointer<EchoPluginContext> providerScope(scope);
    const QPointer<EchoPluginOperation> remoteGuard(remote);
    connect(operation, &EchoPluginOperation::completed, remote, [operation, remote] {
        if (!remote->finished() && operation->state() != EchoPluginOperation::Succeeded)
            remote->cancel();
    });
    const auto deliver = [this, operation, remoteGuard, providerScope, epoch, providerEpoch] {
        if (operation->finished() || !remoteGuard || !remoteGuard->finished())
            return;
        if (!m_scope.active() || epoch != m_scope.accountEpoch() || !providerScope ||
            !providerScope->active() || providerScope->accountEpoch() != providerEpoch ||
            remoteGuard->state() == EchoPluginOperation::Canceled)
            operation->cancel();
        else if (remoteGuard->state() == EchoPluginOperation::Succeeded) {
            if (jsonValue(remoteGuard->result()))
                operation->succeed(remoteGuard->result());
            else
                operation->fail(
                    "InvalidResult",
                    "Plugin service result must contain bounded JSON values, not Qt objects");
        } else
            operation->fail(remoteGuard->errorCode(), remoteGuard->errorMessage());
    };
    connect(remote, &EchoPluginOperation::completed, operation, deliver);
    // Service promises have an independent deadline; neither provider retirement
    // nor a consumer's cancellation may leave a pending foreign call alive.
    QTimer::singleShot(30000, operation, [operation] {
        if (!operation->finished())
            operation->fail("Timeout", "Plugin service call timed out");
    });
    deliver();
    return operation;
}
} // namespace EchoMusic
