#include "EchoPluginUiParts.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartMount.h"
#include <QTimer>
#include <memory>
namespace EchoMusic
{
EchoPluginUiParts::EchoPluginUiParts(EchoPluginContext &scope) : QObject(&scope), m_scope(scope) {}
EchoUiPartRegistry *EchoPluginUiParts::registry() const
{
    return EchoUiPartRegistry::forEngine(m_scope.engine());
}
void EchoPluginUiParts::error(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}
EchoUiPartRef *EchoPluginUiParts::reference(EchoUiPartRegistration &part)
{
    if (auto ref = m_refs.value(part.identity()); ref && ref->valid())
        return ref;
    for (auto it = m_refs.begin(); it != m_refs.end();)
        if (!it.value() || !it.value()->valid())
            it = m_refs.erase(it);
        else
            ++it;
    auto *ref = new EchoUiPartRef(m_scope, part);
    m_scope.engine().newQObject(ref);
    QQmlEngine::setObjectOwnership(ref, QQmlEngine::JavaScriptOwnership);
    m_refs.insert(part.identity(), ref);
    return ref;
}
EchoUiPartRef *EchoPluginUiParts::query(const QVariantMap &selector)
{
    if (!m_scope.active() || !registry())
        return nullptr;
    if (!EchoUiPartRegistry::validSelector(selector)) {
        error("Invalid UI part selector");
        return nullptr;
    }
    error({});
    const auto parts = registry()->query(selector);
    return parts.isEmpty() ? nullptr : reference(*parts.first());
}
QVariantList EchoPluginUiParts::queryAll(const QVariantMap &selector)
{
    QVariantList result;
    if (!m_scope.active() || !registry())
        return result;
    if (!EchoUiPartRegistry::validSelector(selector)) {
        error("Invalid UI part selector");
        return result;
    }
    error({});
    for (auto *part : registry()->query(selector))
        result.append(QVariant::fromValue(reference(*part)));
    return result;
}
EchoPluginRegistration *EchoPluginUiParts::observe(const QString &id, const QVariantMap &selector,
                                                   const QJSValue &callback, bool immediate)
{
    if (!callback.isCallable() || !EchoUiPartRegistry::validSelector(selector) || !registry()) {
        error("Invalid UI part observer");
        return nullptr;
    }
    auto *lease = m_scope.registration("uiPartObserver", id);
    if (!lease)
        return nullptr;
    // All delivery is queued and coalesced per instance. No tree scanning,
    // frame timers or retained visual delegates; removal still carries the old ref.
    struct State
    {
        struct Event
        {
            QString type;
            QJSValue part;
            QString event;
            QVariantMap payload;
            quint64 epoch;
        };
        QList<Event> events;
        QHash<QString, QList<QMetaObject::Connection>> connections;
        QHash<QString, QJSValue> refs;
        QList<QMetaObject::Connection> registryConnections;
        bool scheduled = false;
    };
    m_scope.engine().newQObject(lease);
    connect(lease, &EchoPluginRegistration::activeChanged, lease, [lease] {
        if (!lease->active()) {
            lease->setParent(nullptr);
            QQmlEngine::setObjectOwnership(lease, QQmlEngine::JavaScriptOwnership);
        }
    });
    auto state = std::make_shared<State>();
    auto enqueue = [this, state, lease, callback](const QString &type, const QJSValue &ref,
                                                  const QString &event = QString(),
                                                  const QVariantMap &payload = QVariantMap()) {
        if (!m_scope.active() || !lease->active())
            return;
        if (type == "changed")
            for (const auto &pending : state->events)
                if (pending.type == type && pending.part.strictlyEquals(ref))
                    return;
        state->events.append({type, ref, event, payload, m_scope.accountEpoch()});
        if (std::exchange(state->scheduled, true))
            return;
        QTimer::singleShot(0, lease, [this, state, lease, callback] {
            state->scheduled = false;
            const auto events = std::exchange(state->events, {});
            for (const auto &event : events) {
                if (!m_scope.active() || !m_scope.committed() || !lease->active())
                    break;
                if (event.epoch != m_scope.accountEpoch())
                    continue;
                auto value = m_scope.engine().newObject();
                value.setProperty("type", event.type);
                value.setProperty("part", event.part);
                value.setProperty("event", event.event);
                value.setProperty("payload", m_scope.engine().toScriptValue(event.payload));
                m_scope.call(callback, {value});
            }
        });
    };
    auto attach = [this, state, lease, selector, enqueue](EchoUiPartRegistration *part,
                                                          bool announce) {
        if (!part || !EchoUiPartRegistry::matches(*part, selector))
            return;
        const auto identity = part->identity();
        if (state->connections.contains(identity))
            return;
        const auto ref = m_scope.engine().newQObject(reference(*part));
        state->refs.insert(identity, ref);
        state->connections[identity] = {
            connect(part, &EchoUiPartRegistration::changed, lease,
                    [ref, enqueue] { enqueue("changed", ref); }),
            connect(part, &EchoUiPartRegistration::eventOccurred, lease,
                    [ref, enqueue](const auto &event, const auto &payload) {
                        enqueue("event", ref, event, payload);
                    })};
        if (announce)
            enqueue("added", ref);
    };
    lease->stage(
        [this, state, selector, attach, lease, immediate, enqueue] {
            auto *source = registry();
            if (!source)
                return false;
            state->registryConnections
                << connect(source, &EchoUiPartRegistry::added, lease,
                           [attach](auto *part) { attach(part, true); })
                << connect(source, &EchoUiPartRegistry::removed, lease,
                           [state, enqueue](const auto &id) {
                               const auto connections = state->connections.take(id);
                               for (const auto &c : connections)
                                   disconnect(c);
                               const auto ref = state->refs.take(id);
                               if (ref.isQObject())
                                   enqueue("removed", ref);
                           });
            for (auto *part : source->query(selector))
                attach(part, immediate);
            return true;
        },
        [state] {
            for (const auto &c : state->registryConnections)
                QObject::disconnect(c);
            for (const auto &list : state->connections)
                for (const auto &c : list)
                    QObject::disconnect(c);
            state->connections.clear();
            state->refs.clear();
            state->registryConnections.clear();
            state->events.clear();
        });
    if (m_scope.committed())
        lease->commit();
    error({});
    return lease;
}
EchoUiPartMount *EchoPluginUiParts::mount(const QString &id, EchoUiPartRef *part,
                                          const QString &position, const QString &component,
                                          int priority)
{
    if (!part || !part->belongsTo(&m_scope) || !part->valid() || priority < -10000 ||
        priority > 10000 ||
        !QStringList{"append", "prepend", "before", "after", "replace", "background", "foreground"}
             .contains(position)) {
        error("Invalid or stale UI part mount");
        return nullptr;
    }
    if (position == "replace" && !part->replaceable()) {
        error("This public part does not support structural replacement");
        return nullptr;
    }
    const auto path = m_scope.package().resolve(component);
    if (path.isEmpty() || !path.endsWith(".qml")) {
        error("Mount component must be a package QML file");
        return nullptr;
    }
    auto *lease = m_scope.registration("uiPartMount", id);
    if (!lease)
        return nullptr;
    auto *mount =
        new EchoUiPartMount(*this, *lease, *part->registration(), position, path, id, priority);
    m_scope.engine().newQObject(mount);
    QQmlEngine::setObjectOwnership(mount, QQmlEngine::CppOwnership);
    connect(lease, &EchoPluginRegistration::activeChanged, mount, [mount, lease] {
        if (lease->active())
            return;
        mount->setParent(nullptr);
        QQmlEngine::setObjectOwnership(mount, QQmlEngine::JavaScriptOwnership);
        lease->deleteLater();
    });
    lease->stage(
        [mount] {
            mount->start();
            return true;
        },
        [mount] { mount->stop(); });
    if (m_scope.committed())
        lease->commit();
    error({});
    return mount;
}
} // namespace EchoMusic
