#include "SettingsKeys.h"
#include "PluginRuntime.h"
#include "PluginDataTransaction.h"
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QThread>
#include <QDateTime>
#include <utility>
#include "PluginRuntimeHelpers_p.h"

namespace EchoMusic
{
using namespace PluginRuntimeDetail;

void PluginRuntime::settle(Entry &entry, const QJSValue &value,
                           std::function<void(QString)> complete)
{
    auto *bridge = new PluginSettlement(this);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    entry.settlement = bridge;
    bridge->complete = std::move(complete);
    bridge->timeout.setSingleShot(true);
    bridge->timeout.setInterval(5000);
    connect(&bridge->timeout, &QTimer::timeout, bridge,
            [bridge] { bridge->rejected("Plugin lifecycle Promise timed out (5 s)"); });
    bridge->timeout.start();
    const auto attach = m_engine.evaluate(
        "(function(value, bridge) { Promise.resolve(value).then(function() { try { "
        "bridge.resolved(); } catch(ignored) {} }, "
        "function(error) { try { bridge.rejected(String(error)); } catch(ignored) {} }); })");
    auto result = attach.call({value, m_engine.newQObject(bridge)});
    if (m_engine.hasError()) {
        result = m_engine.catchError();
        if (!result.isError())
            result = m_engine.newErrorObject(QJSValue::GenericError, result.toString());
    }
    if (result.isError())
        bridge->rejected(result.toString());
}
PluginDependencyGraph PluginRuntime::dependencyPlan(const PluginPackage *replacement) const
{
    QList<PluginPackage> packages;
    QSet<QString> enabled;
    bool replaced = false;
    for (const auto &entry : m_entries) {
        const bool replace = replacement && entry->package.id == replacement->id;
        packages.append(replace ? *replacement : entry->package);
        replaced |= replace;
        if (entry->enabled)
            enabled.insert(entry->package.id);
    }
    if (replacement && !replaced) {
        packages.append(*replacement);
        enabled.insert(replacement->id);
    }
    return PluginDependencyGraph::resolve(packages, enabled);
}
bool PluginRuntime::dependsOn(const Entry &consumer, const QString &provider) const
{
    return consumer.package.dependencies.contains(provider) ||
           consumer.package.optionalDependencies.contains(provider);
}
void PluginRuntime::queueReconcile()
{
    if (std::exchange(m_reconcileQueued, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_reconcileQueued = false;
        advanceRetirements();
        reconcile();
        emit loadingChanged();
    });
}
void PluginRuntime::reconcile()
{
    if (m_closing || m_maintenance || m_safeMode || m_imports || m_startScanPending)
        return;
    const auto plan = dependencyPlan();
    for (const auto &entry : m_entries) {
        if (entry->enabled && entry->context && plan.errors.contains(entry->package.id))
            retire(*entry, Blocked, plan.errors.value(entry->package.id));
        if (entry->enabled && !entry->context && entry->state != Failed &&
            entry->state != Invalid && entry->state != Incompatible &&
            plan.errors.contains(entry->package.id)) {
            const auto error = plan.errors.value(entry->package.id);
            if (entry->state != Blocked || entry->error != error) {
                entry->state = Blocked;
                entry->error = error;
                changed(*entry);
            }
            if (entry->mutation != Entry::None && entry->applying)
                finishMutation(*entry);
        }
    }
    for (const auto &id : plan.order) {
        auto *entry = find(installationForId(id));
        if (!entry || !entry->enabled || plan.errors.contains(id) || entry->retirementPending)
            continue;
        if (entry->state == Activating || entry->state == Deactivating)
            return; // Deterministic ordered activation, including async activate.
        if (!entry->context && (entry->state == Disabled || entry->state == Blocked)) {
            activate(*entry);
            if (entry->state == Activating)
                return;
        }
    }
}
void PluginRuntime::activate(Entry &entry)
{
    if (m_closing || m_maintenance || m_safeMode || entry.context || entry.retirementPending)
        return;
    if (m_imports || m_startScanPending) {
        queueReconcile();
        return;
    }
    // All activation requests pass through reconcile's graph preflight.
    QString error;
    for (const auto &id :
         entry.package.dependencies.keys() + entry.package.optionalDependencies.keys()) {
        auto *provider = find(installationForId(id));
        if (provider && (provider->retirementPending || provider->state == Deactivating))
            error = tr("等待依赖插件 %1 完成排空").arg(id);
    }
    auto dependencies = entry.package.dependencies;
    for (auto it = entry.package.optionalDependencies.begin();
         it != entry.package.optionalDependencies.end(); ++it) {
        auto *provider = find(installationForId(it.key()));
        if (provider && provider->enabled)
            dependencies.insert(it.key(), it.value());
    }
    for (auto it = dependencies.begin(); it != dependencies.end() && error.isEmpty(); ++it) {
        auto *provider = find(installationForId(it.key()));
        if (!provider || provider->state != Active || provider->retirementPending)
            error = tr("等待依赖插件 %1 就绪").arg(it.key());
    }
    if (!error.isEmpty()) {
        if (entry.state != Blocked || entry.error != error) {
            entry.state = Blocked;
            entry.error = error;
            changed(entry);
        }
        if (entry.mutation != Entry::None && entry.applying)
            finishMutation(entry);
        return;
    }
    entry.state = Activating;
    entry.error.clear();
    entry.context = new EchoPluginContext(entry.package, ++m_generation, m_engine, m_host,
                                          m_root + "/data/" + entry.package.installationId, this);
    entry.context->plugins()->setResolver([this](const QString &id) -> EchoPluginContext * {
        auto *provider = find(installationForId(id));
        return provider && provider->state == Active && !provider->retirementPending
                   ? provider->context.data()
                   : nullptr;
    });
    connect(this, &PluginRuntime::catalogChanged, entry.context,
            [scope = QPointer<EchoPluginContext>(entry.context)] {
                if (scope && scope->active())
                    emit scope->plugins()->availabilityChanged();
            });
    connect(entry.context, &EchoPluginContext::callbackFailed, this,
            [this, target = &entry](const QString &message) {
                target->failure = {{"message", message},
                                   {"reason", tr("回调异常")},
                                   {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
                m_preferences.setValue(EchoMusic::SettingsKeys::Plugins::FailuresPrefix +
                                           target->package.installationId,
                                       target->failure);
                m_preferences.sync();
                changed(*target);
                emit pluginFailed(target->package.id, message);
            });
    changed(entry);
    entry.module = m_engine.importModule(entry.package.resolve(entry.package.entry));
    if (m_engine.hasError()) {
        entry.module = m_engine.catchError();
        if (!entry.module.isError())
            entry.module = m_engine.newErrorObject(QJSValue::GenericError, entry.module.toString());
    }
    if (entry.module.isError() || !entry.module.property("activate").isCallable()) {
        retire(entry, Failed,
               entry.module.isError() ? entry.module.toString()
                                      : "Module must export activate(context)");
        return;
    }
    auto result = entry.context->call(entry.module.property("activate"),
                                      {m_engine.newQObject(entry.context)});
    if (m_engine.hasError()) {
        result = m_engine.catchError();
        if (!result.isError())
            result = m_engine.newErrorObject(QJSValue::GenericError, result.toString());
    }
    if (result.isError()) {
        retire(entry, Failed, result.toString());
        return;
    }
    if (!entry.context || entry.state != Activating)
        return;
    settle(entry, result, [this, target = &entry](QString error) {
        if (target->state != Activating || !target->context)
            return;
        if (!error.isEmpty() || !target->context->commit()) {
            retire(*target, Failed, error.isEmpty() ? "Contribution commit failed" : error);
            return;
        }
        target->state = Active;
        changed(*target);
        finishMutation(*target);
    });
}
void PluginRuntime::retire(Entry &entry, State terminal, QString error)
{
    if (entry.state == Deactivating) {
        if (terminal == Failed) {
            entry.afterRetire = terminal;
            entry.error = std::move(error);
        }
        return;
    }
    if (!entry.context)
        return;
    // Consumers drain before their provider. Their enabled preferences remain
    // intact, so an updated/re-enabled provider can reactivate the graph.
    entry.retirementPending = true;
    entry.afterRetire = terminal;
    entry.error = error;
    for (const auto &other : m_entries)
        if (other.get() != &entry && other->context && dependsOn(*other, entry.package.id) &&
            other->state != Deactivating && !other->retirementPending)
            retire(*other, Blocked, tr("依赖插件 %1 正在停用").arg(entry.package.id));
    for (const auto &other : m_entries)
        if (other.get() != &entry && other->context && dependsOn(*other, entry.package.id)) {
            changed(entry);
            return;
        }
    entry.retirementPending = false;
    if (entry.settlement)
        entry.settlement->invalidate();
    entry.state = Deactivating;
    entry.afterRetire = terminal;
    entry.error = std::move(error);
    entry.deactivateDone = false;
    changed(entry);
    connect(entry.context, &EchoPluginContext::resourcesDrained, this,
            [this, target = &entry] { finishRetire(*target); });
    entry.context->beginRetire();
    const auto deactivate = entry.module.property("deactivate");
    if (deactivate.isCallable()) {
        auto result = entry.context->call(deactivate, {m_engine.newQObject(entry.context)});
        if (m_engine.hasError()) {
            result = m_engine.catchError();
            if (!result.isError())
                result = m_engine.newErrorObject(QJSValue::GenericError, result.toString());
        }
        if (result.isError()) {
            emit pluginFailed(entry.package.id, result.toString());
            entry.deactivateDone = true;
        } else
            settle(entry, result, [this, target = &entry](QString error) {
                if (!error.isEmpty())
                    emit pluginFailed(target->package.id, error);
                target->deactivateDone = true;
                finishRetire(*target);
            });
    } else
        entry.deactivateDone = true;
    finishRetire(entry);
}
void PluginRuntime::finishRetire(Entry &entry)
{
    if (entry.state != Deactivating || !entry.deactivateDone || !entry.context ||
        !entry.context->drained())
        return;
    // QML Loader delegate removal and queued completions precede context deletion.
    auto *context = entry.context.data();
    entry.context = nullptr;
    entry.module = {};
    context->deleteLater();
    entry.state = entry.afterRetire;
    changed(entry);
    if (entry.state == Failed)
        emit pluginFailed(entry.package.id, entry.error);
    if (!m_closing && entry.mutation != Entry::None) {
        if (!entry.applying)
            applyMutation(entry);
        else
            finishMutation(entry);
    } else if (!m_closing && std::exchange(entry.reloadPending, false))
        load(entry, entry.source);
    queueReconcile();
    checkMaintenance();
    checkClosed();
}
void PluginRuntime::advanceRetirements()
{
    for (const auto &entry : m_entries)
        if (entry->retirementPending) {
            bool consumers = false;
            for (const auto &other : m_entries)
                consumers |= other.get() != entry.get() && other->context &&
                             dependsOn(*other, entry->package.id);
            if (!consumers)
                retire(*entry, entry->afterRetire, entry->error);
        }
}
} // namespace EchoMusic
