#include "EchoPluginRegistry.h"
#include "EchoPluginContext.h"
#include <QJSValueIterator>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>
#include <memory>

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
    connect(&scope, &EchoPluginContext::sessionInvalidated, this,
            &EchoPluginRegistry::scheduleRefresh);
    connect(&scope, &EchoPluginContext::activeChanged, this, &EchoPluginRegistry::scheduleRefresh);
    if (scope.activationContext() != &scope)
        connect(scope.activationContext()->plugins(), &EchoPluginRegistry::availabilityChanged,
                this, &EchoPluginRegistry::scheduleRefresh);
}
void EchoPluginRegistry::setResolver(std::function<EchoPluginContext *(const QString &)> resolver)
{
    m_resolver = std::move(resolver);
    scheduleRefresh();
}
void EchoPluginRegistry::refresh()
{
    scheduleRefresh();
}
void EchoPluginRegistry::scheduleRefresh()
{
    if (m_refreshQueued || !m_scope.active())
        return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshQueued = false;
        refreshNow();
    });
}
void EchoPluginRegistry::refreshNow()
{
    if (!m_scope.active())
        return;
    for (const auto &connection : m_providerConnections)
        disconnect(connection);
    m_providerConnections.clear();
    if (m_scope.activationContext() == &m_scope) {
        auto dependencies = m_scope.package().dependencies;
        for (auto it = m_scope.package().optionalDependencies.cbegin();
             it != m_scope.package().optionalDependencies.cend(); ++it)
            dependencies.insert(it.key(), it.value());
        for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it)
            if (auto *scope = provider(it.key())) {
                m_providerConnections.append(connect(scope->plugins(),
                                                     &EchoPluginRegistry::exportsChanged, this,
                                                     &EchoPluginRegistry::scheduleRefresh));
                m_providerConnections.append(connect(scope, &EchoPluginContext::activeChanged, this,
                                                     &EchoPluginRegistry::scheduleRefresh));
                m_providerConnections.append(connect(scope, &EchoPluginContext::sessionInvalidated,
                                                     this, &EchoPluginRegistry::scheduleRefresh));
            }
    }
    const auto value = snapshot();
    if (value != m_lastSnapshot) {
        m_lastSnapshot = value;
        emit availabilityChanged();
    }
}
QVariantMap EchoPluginRegistry::describe(const EchoPluginContext *scope) const
{
    if (!scope || !scope->active() || !scope->committed())
        return {{"id", scope ? scope->id() : QString()},
                {"available", false},
                {"version", ""},
                {"generation", ""},
                {"name", ""},
                {"services", QVariantList{}},
                {"commands", QVariantList{}}};
    const auto *registry = scope->plugins();
    QVariantList services, commands;
    auto keys = registry->m_services.keys();
    keys.sort();
    for (const auto &id : keys) {
        const auto &service = registry->m_services[id];
        if (!service.lease || !service.lease->active())
            continue;
        auto methods = service.methods.keys();
        methods.sort();
        services.append(QVariantMap{{"id", id},
                                    {"version", service.version},
                                    {"methods", methods},
                                    {"revision", service.lease->revision()}});
    }
    keys = registry->m_commands.keys();
    keys.sort();
    for (const auto &id : keys) {
        const auto &command = registry->m_commands[id];
        if (command.lease && command.lease->active())
            commands.append(QVariantMap{{"id", id},
                                        {"version", command.version},
                                        {"title", command.title},
                                        {"description", command.description},
                                        {"revision", command.lease->revision()}});
    }
    return {{"id", scope->id()},
            {"name", scope->package().name},
            {"version", scope->package().version},
            {"generation", scope->activationGeneration()},
            {"available", true},
            {"services", services},
            {"commands", commands}};
}
QVariantMap EchoPluginRegistry::snapshot() const
{
    if (!m_scope.active())
        return {{"self", QVariant()}, {"dependencies", QVariantList{}}, {"accountEpoch", ""}};
    QVariantList dependencies;
    const auto &package = m_scope.package();
    auto ids = package.dependencies.keys();
    ids.append(package.optionalDependencies.keys());
    ids.removeDuplicates();
    ids.sort();
    for (const auto &id : ids) {
        auto entry = describe(provider(id));
        entry["id"] = id;
        entry["required"] = package.dependencies.contains(id);
        entry["range"] = package.dependencies.value(id, package.optionalDependencies.value(id));
        dependencies.append(entry);
    }
    return {{"self", describe(m_scope.activationContext())},
            {"dependencies", dependencies},
            {"accountEpoch", QString::number(m_scope.accountEpoch())}};
}
EchoPluginRegistration *EchoPluginRegistry::onChanged(const QString &id, const QJSValue &callback,
                                                      bool immediate)
{
    if (!callback.isCallable())
        return nullptr;
    auto *lease = m_scope.registration("pluginCatalog", id);
    if (!lease)
        return nullptr;
    connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
        if (!lease->active())
            lease->deleteLater();
    });
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto previous = std::make_shared<QVariantMap>(snapshot());
    auto delivered = std::make_shared<bool>(!immediate);
    const auto deliver = [this, callback, lease, previous, delivered] {
        if (!m_scope.active() || !m_scope.committed() || !lease->active())
            return;
        const auto value = snapshot();
        if (*delivered && *previous == value)
            return;
        *previous = value;
        *delivered = true;
        m_scope.call(callback, {m_scope.engine().toScriptValue(value)});
    };
    lease->stage(
        [this, lease, deliver, connection, immediate] {
            *connection = connect(this, &EchoPluginRegistry::availabilityChanged, lease, deliver);
            if (immediate) {
                const auto epoch = m_scope.accountEpoch();
                QTimer::singleShot(0, lease, [this, deliver, epoch] {
                    if (epoch == m_scope.accountEpoch())
                        deliver();
                });
            }
            return true;
        },
        [connection] { disconnect(*connection); });
    if (m_scope.committed())
        lease->commit();
    return lease;
}
EchoPluginRegistration *EchoPluginRegistry::registerCommand(const QString &id,
                                                            const QString &version,
                                                            const QJSValue &callback,
                                                            const QVariantMap &metadata)
{
    if (m_scope.contextId() != "main" || !callback.isCallable() ||
        !PluginPackage::versionMatches("=" + version, version) ||
        (m_commands.size() >= 128 && !m_commands.contains(id)))
        return nullptr;
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it)
        if ((it.key() != "title" && it.key() != "description") ||
            it.value().metaType().id() != QMetaType::QString)
            return nullptr;
    if (metadata.value("title").toString().size() > 128 ||
        metadata.value("description").toString().size() > 2048)
        return nullptr;
    auto *lease = m_scope.registration("publicCommand", id);
    if (!lease)
        return nullptr;
    const Command command{version, metadata.value("title", id).toString(),
                          metadata.value("description").toString(), callback, lease};
    lease->stage(
        [this, id, command] {
            // Activation stages all declarations before committing any of them.
            // Enforce the export budget at publication as well as registration.
            if (m_commands.size() >= 128 && !m_commands.contains(id))
                return false;
            m_commands.insert(id, command);
            emit exportsChanged();
            scheduleRefresh();
            return true;
        },
        [this, id] {
            m_commands.remove(id);
            emit exportsChanged();
            scheduleRefresh();
        });
    if (m_scope.committed() && !lease->commit())
        lease->dispose();
    return lease;
}
EchoPluginOperation *EchoPluginRegistry::callCommand(const QString &pluginId,
                                                     const QString &commandId,
                                                     const QString &versionRange,
                                                     const QVariantMap &arguments)
{
    if (!jsonValue(arguments))
        return m_scope.rejected("InvalidArguments",
                                "Plugin command arguments must be bounded JSON values");
    auto *scope = provider(pluginId);
    if (!scope)
        return m_scope.rejected("DependencyUnavailable",
                                "Plugin dependency is not active or not declared: " + pluginId);
    const auto command = scope->plugins()->m_commands.value(commandId);
    if (!command.lease || !command.lease->active() ||
        !PluginPackage::versionMatches(versionRange, command.version))
        return m_scope.rejected("CommandUnavailable",
                                "Public plugin command/version is unavailable: " + pluginId + '/' +
                                    commandId);
    return forward(*scope, command.callback, command.lease, arguments);
}
EchoPluginContext *EchoPluginRegistry::provider(const QString &pluginId) const
{
    if (!m_scope.active())
        return nullptr;
    auto *activation = m_scope.activationContext();
    if (pluginId == m_scope.id())
        return activation->active() && activation->committed() ? activation : nullptr;
    if (!m_resolver && activation == &m_scope)
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
            emit exportsChanged();
            scheduleRefresh();
            return true;
        },
        [this, id] {
            m_services.remove(id);
            emit exportsChanged();
            scheduleRefresh();
        });
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
    return forward(*scope, service.methods.value(method), service.lease, arguments);
}
EchoPluginOperation *EchoPluginRegistry::forward(EchoPluginContext &provider,
                                                 const QJSValue &callback,
                                                 QPointer<EchoPluginRegistration> lease,
                                                 const QVariantMap &arguments)
{
    auto *scope = &provider;
    auto *operation = m_scope.operation();
    if (operation->finished())
        return operation;
    operation->start();
    auto *remote = scope->invokeRegistered(callback, lease, arguments);
    connect(lease.data(), &EchoPluginRegistration::activeChanged, remote, [lease, remote] {
        if (!lease || !lease->active())
            remote->cancel();
    });
    connect(lease.data(), &QObject::destroyed, remote, &EchoPluginOperation::cancel);
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
                    "Cross-plugin results must contain bounded JSON values, not Qt objects");
        } else
            operation->fail(remoteGuard->errorCode(), remoteGuard->errorMessage());
    };
    connect(remote, &EchoPluginOperation::completed, operation, deliver);
    // Foreign promises have an independent deadline; neither provider retirement
    // nor a consumer's cancellation may leave a pending foreign call alive.
    QTimer::singleShot(30000, operation, [operation] {
        if (!operation->finished())
            operation->fail("Timeout", "Cross-plugin call timed out");
    });
    deliver();
    return operation;
}
} // namespace EchoMusic
