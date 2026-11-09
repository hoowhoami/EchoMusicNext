#include "EchoPluginUiParts.h"
#include "EchoPluginContext.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartMount.h"
#include "EchoUiLayout.h"
#include <QTimer>
#include <memory>
#include <algorithm>
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
EchoUiBatch *EchoPluginUiParts::applyBatch(const QString &id, const QVariantList &entries)
{
    QString message;
    if (!EchoUiBatch::validate(*this, entries, message)) {
        error(message);
        return nullptr;
    }
    auto *lease = m_scope.registration("uiBatch", id);
    if (!lease) {
        error("Invalid batch ID or retired activation");
        return nullptr;
    }
    auto *batch = new EchoUiBatch(*this, *lease, id, entries);
    m_scope.engine().newQObject(batch);
    QQmlEngine::setObjectOwnership(batch, QQmlEngine::CppOwnership);
    connect(&m_scope, &EchoPluginContext::sessionInvalidated, lease,
            &EchoPluginRegistration::dispose);
    connect(lease, &EchoPluginRegistration::activeChanged, batch, [batch, lease] {
        if (!lease->active()) {
            batch->stop();
            batch->setParent(nullptr);
            QQmlEngine::setObjectOwnership(batch, QQmlEngine::JavaScriptOwnership);
            lease->deleteLater();
        }
    });
    lease->stage(
        [batch] {
            batch->start();
            return true;
        },
        [batch] { batch->stop(); });
    if (m_scope.committed())
        lease->commit();
    error({});
    return batch;
}
EchoUiModelContribution *EchoPluginUiParts::contributeModel(const QString &id,
                                                            const QVariantMap &selector,
                                                            const QVariantMap &definition,
                                                            const QJSValue &provider)
{
    QString message;
    if ((!provider.isUndefined() && !provider.isNull() && !provider.isCallable()) ||
        !EchoUiPartRegistry::validSelector(selector) ||
        !EchoUiModelContribution::validate(*this, definition, message, provider.isCallable())) {
        error(message.isEmpty() ? "Invalid model selector" : message);
        return nullptr;
    }
    auto *lease = m_scope.registration("uiModel", id);
    if (!lease) {
        error("Invalid model contribution ID or retired scope");
        return nullptr;
    }
    auto *contribution =
        new EchoUiModelContribution(*this, *lease, id, selector, definition, provider);
    m_scope.engine().newQObject(contribution);
    QQmlEngine::setObjectOwnership(contribution, QQmlEngine::CppOwnership);
    connect(&m_scope, &EchoPluginContext::sessionInvalidated, lease,
            &EchoPluginRegistration::dispose);
    connect(lease, &EchoPluginRegistration::activeChanged, contribution, [contribution, lease] {
        if (lease->active())
            return;
        contribution->stop();
        contribution->setParent(nullptr);
        QQmlEngine::setObjectOwnership(contribution, QQmlEngine::JavaScriptOwnership);
        lease->deleteLater();
    });
    lease->stage(
        [contribution] {
            contribution->start();
            return true;
        },
        [contribution] { contribution->stop(); });
    if (m_scope.committed())
        lease->commit();
    error({});
    return contribution;
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
    QObject discovery;
    registry()->acquireStructureFor(&discovery, selector);
    for (auto *part : registry()->query(selector))
        if (part->surface() != "inspector")
            return reference(*part);
    return nullptr;
}
EchoUiPartRef *EchoPluginUiParts::queryUnique(const QVariantMap &selector)
{
    if (!m_scope.active() || !registry())
        return nullptr;
    if (!EchoUiPartRegistry::validSelector(selector)) {
        error("Invalid UI part selector");
        return nullptr;
    }
    QObject discovery;
    registry()->acquireStructureFor(&discovery, selector);
    auto matches = registry()->query(selector);
    matches.removeIf([](auto *part) { return part->surface() == "inspector"; });
    if (matches.size() != 1) {
        error(QString("UI selector matched %1 parts; expected exactly one").arg(matches.size()));
        return nullptr;
    }
    error({});
    return reference(*matches.first());
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
    QObject discovery;
    registry()->acquireStructureFor(&discovery, selector);
    for (auto *part : registry()->query(selector))
        if (part->surface() != "inspector")
            result.append(QVariant::fromValue(reference(*part)));
    return result;
}
QVariantMap EchoPluginUiParts::preflightBatch(const QVariantList &entries, const QString &id)
{
    QString error;
    if (!EchoUiBatch::validate(*this, entries, error))
        return {{"valid", false}, {"error", error}, {"entries", QVariantList()}};
    QVariantList results;
    bool valid = true;
    QObject discovery;
    const bool native = std::any_of(entries.cbegin(), entries.cend(), [this](const auto &value) {
        const auto entry = value.toMap();
        return registry()->selectorNeedsDiscovery(entry.value("selector").toMap(),
                                                  entry.value("definition").toMap());
    });
    registry()->acquireStructure(&discovery, false, native);
    QHash<QQuickItem *, QList<EchoUiModelAdapter::Record>> models;
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        auto result =
            preflight(entry.value("selector").toMap(),
                      entry.contains("model")   ? entry.value("model").toMap()
                      : entry.contains("style") ? QVariantMap{{"style", entry.value("style")}}
                                                : entry.value("definition").toMap());
        if (entry.contains("model")) {
            const auto definition = entry.value("model").toMap();
            for (auto *part : registry()->query(entry.value("selector").toMap())) {
                if (part->surface() == "inspector" || part->owner() == m_scope.id())
                    continue;
                EchoUiModelAdapter::Record record;
                record.id = (id.isEmpty() ? QString() : id + '.') + entry.value("id").toString();
                record.owner = m_scope.id();
                record.token = "@preflight:" + record.id;
                record.kind = definition.value("kind").toString();
                record.priority = definition.value("priority").toInt();
                for (const auto &value : definition.value("entries").toList()) {
                    const auto item = value.toMap();
                    auto values = item.value("values").toMap();
                    for (const auto &key : item.keys())
                        if (key != "values")
                            values[key] = item[key];
                    const auto key = item.value("key").toString();
                    record.entries.append({key, record.owner, record.id, record.token + ':' + key,
                                           values, item.value("enabled", true).toBool()});
                }
                models[part->target()].append(record);
            }
        }
        if (entry.contains("model") && m_scope.active()) {
            result["valid"] =
                result.value("matchCount").toInt() > 0 && result.value("matchCount").toInt() <= 256;
            result["error"] = result.value("valid").toBool()
                                  ? QString()
                                  : QStringLiteral("Expected 1..256 live model targets");
        }
        result["id"] = entry.value("id");
        results.append(result);
        if (!result.value("valid").toBool()) {
            valid = false;
            if (error.isEmpty())
                error = result.value("error").toString();
        }
    }
    for (auto it = models.begin(); it != models.end(); ++it) {
        const auto result =
            EchoUiModelAdapter::inspectRecords(it.key(), m_scope.engine(), it.value());
        for (auto &entryResult : results) {
            auto entry = entryResult.toMap();
            auto targets = entry.value("targets").toList();
            bool matched = false;
            for (auto &targetValue : targets) {
                auto target = targetValue.toMap();
                auto *part = registry()->find(target.value("id").toString());
                if (part && part->target() == it.key() &&
                    !target.value("model").toMap().isEmpty()) {
                    target["model"] = result;
                    target["supported"] = result.value("supported");
                    target["error"] = result.value("error");
                    targetValue = target;
                    matched = true;
                }
            }
            if (matched) {
                entry["targets"] = targets;
                if (!result.value("supported").toBool()) {
                    entry["valid"] = false;
                    entry["error"] = result.value("error");
                }
                entryResult = entry;
            }
        }
        if (!result.value("supported").toBool()) {
            valid = false;
            if (error.isEmpty())
                error = result.value("error").toString();
        }
    }
    return {{"valid", valid}, {"error", error}, {"entries", results}};
}
QVariantMap EchoPluginUiParts::preflight(const QVariantMap &selector, const QVariantMap &definition)
{
    QString message;
    if (!m_scope.active() || !registry() || !EchoUiPartRegistry::validSelector(selector))
        message = "Invalid selector or closed activation";
    else if (definition.contains("kind"))
        EchoUiModelContribution::validate(*this, definition, message);
    else if (definition.contains("style")) {
        auto style = definition.value("style").toMap();
        if (definition.size() != 1 || style.isEmpty() ||
            !EchoUiPartRegistry::normalize(style, message))
            if (message.isEmpty())
                message = "Invalid style preflight definition";
    } else
        EchoUiContribution::validate(m_scope, definition, message);
    if (!message.isEmpty())
        return {{"valid", false}, {"error", message}, {"matchCount", 0}};
    QObject discovery;
    registry()->acquireStructureFor(&discovery, selector, definition);
    auto matches = registry()->query(selector);
    matches.removeIf([this](auto *part) {
        return part->surface() == "inspector" || part->owner() == m_scope.id();
    });
    QVariantList targets;
    const auto operation = definition.value("operation", "insert").toString();
    bool valid = !matches.isEmpty() && matches.size() <= 256;
    if (!valid)
        message = matches.isEmpty() ? "No matching live targets"
                                    : "UI contribution exceeds 256 live targets";
    for (auto *part : matches) {
        QString reason;
        QVariantMap model;
        if (definition.contains("kind")) {
            EchoUiModelAdapter::Record record;
            record.token = "@preflight";
            record.owner = m_scope.id();
            record.id = "preflight";
            record.kind = definition.value("kind").toString();
            for (const auto &value : definition.value("entries").toList()) {
                const auto entry = value.toMap();
                auto values = entry.value("values").toMap();
                for (const auto &key : entry.keys())
                    if (key != "values")
                        values[key] = entry[key];
                const auto key = entry.value("key").toString();
                record.entries.append({key, record.owner, record.id, record.token + ':' + key,
                                       values, entry.value("enabled", true).toBool()});
            }
            model = EchoUiModelAdapter::inspect(part->target(), m_scope.engine(), record);
            if (!model.value("supported").toBool())
                reason = model.value("error").toString();
        } else if (definition.contains("style")) {
            const auto style = definition.value("style").toMap();
            for (const auto &key : style.keys())
                if (!part->properties().contains(key)) {
                    reason = "Unsupported style field: " + key;
                    break;
                }
        } else if ((operation == "wrap" || operation == "replace") && !part->canCompose())
            reason = "Native content factory is unavailable for this item type";
        else if (operation == "insert") {
            const auto position = definition.value("position", "append").toString();
            if (!part->insertionPositions().contains(position))
                reason = "Unsupported insertion position";
            auto *container = position == "before" || position == "after"
                                  ? part->target()->parentItem()
                                  : part->target();
            if (reason.isEmpty() && position != "background" && position != "foreground")
                EchoUiLayout::validateInsertion(container, definition.value("layout").toMap(),
                                                reason);
        } else if (operation == "move") {
            const auto destinations = registry()->query(definition.value("destination").toMap());
            if (destinations.size() != 1 || destinations.first()->layoutKind().isEmpty() ||
                destinations.first()->target()->window() != part->target()->window())
                reason = "Move requires one native container in the same window";
            else {
                for (auto *item = destinations.first()->target(); item; item = item->parentItem())
                    if (item == part->target())
                        reason = "Composition cycle";
            }
        } else
            EchoUiLayout::validate(part->target()->parentItem(), definition.value("layout").toMap(),
                                   reason);
        valid = valid && reason.isEmpty();
        if (message.isEmpty() && !reason.isEmpty())
            message = reason;
        targets.append(QVariantMap{{"id", part->identity()},
                                   {"address", part->address()},
                                   {"persistent", part->persistent()},
                                   {"supported", reason.isEmpty()},
                                   {"error", reason},
                                   {"model", model},
                                   {"layoutEngine", part->layoutEngine()},
                                   {"insertionPositions", part->insertionPositions()}});
    }
    if (valid && definition.contains("component")) {
        QQmlComponent component(&m_scope.engine(), QUrl::fromLocalFile(m_scope.package().resolve(
                                                       definition.value("component").toString())));
        if (component.isError()) {
            valid = false;
            message = component.errorString();
        }
    }
    QVariantMap unique;
    if (matches.size() == 1)
        unique = matches.first()->persistent()
                     ? QVariantMap{{"address", matches.first()->address()}}
                     : QVariantMap{{"id", matches.first()->identity()}};
    return {{"valid", valid},
            {"error", message},
            {"matchCount", matches.size()},
            {"uniqueSelector", unique},
            {"targets", targets}};
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
        if (!part || part->surface() == "inspector" ||
            !EchoUiPartRegistry::matches(*part, selector))
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
    const auto remove = [state, enqueue](const QString &id) {
        for (const auto &connection : state->connections.take(id))
            disconnect(connection);
        const auto ref = state->refs.take(id);
        if (ref.isQObject())
            enqueue("removed", ref);
    };
    lease->stage(
        [this, state, selector, attach, lease, immediate, remove] {
            auto *source = registry();
            if (!source)
                return false;
            source->acquireStructureFor(lease, selector);
            state->registryConnections
                << connect(source, &EchoUiPartRegistry::added, lease,
                           [attach](auto *part) { attach(part, true); })
                << connect(source, &EchoUiPartRegistry::removed, lease, remove)
                << connect(source, &EchoUiPartRegistry::structureChanged, lease,
                           [source, state, selector, attach, remove] {
                               QSet<QString> selected;
                               for (auto *part : source->query(selector)) {
                                   selected.insert(part->identity());
                                   attach(part, true);
                               }
                               for (const auto &id : state->refs.keys())
                                   if (!selected.contains(id))
                                       remove(id);
                           });
            for (auto *part : source->query(selector))
                attach(part, immediate);
            return true;
        },
        [this, state, lease] {
            if (auto *source = registry())
                source->releaseStructure(lease);
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
    if (position == "replace" && part->registration())
        part->registration()->ensureNativeFactory();
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

namespace EchoMusic
{
EchoUiContribution *EchoPluginUiParts::contribute(const QString &id, const QVariantMap &selector,
                                                  const QVariantMap &definition)
{
    QString message;
    if (!EchoUiPartRegistry::validSelector(selector) ||
        !EchoUiContribution::validate(m_scope, definition, message)) {
        error(message.isEmpty() ? QStringLiteral("Invalid UI contribution selector") : message);
        return nullptr;
    }
    auto *lease = m_scope.registration("uiContribution", id);
    if (!lease) {
        error("Invalid contribution ID or retired scope");
        return nullptr;
    }
    auto *contribution = new EchoUiContribution(*this, *lease, id, selector, definition);
    m_scope.engine().newQObject(contribution);
    QQmlEngine::setObjectOwnership(contribution, QQmlEngine::CppOwnership);
    connect(lease, &EchoPluginRegistration::activeChanged, contribution, [contribution, lease] {
        if (lease->active())
            return;
        QTimer::singleShot(
            0, contribution, [contribution, lease = QPointer<EchoPluginRegistration>(lease)] {
                contribution->stop();
                contribution->setParent(nullptr);
                QQmlEngine::setObjectOwnership(contribution, QQmlEngine::JavaScriptOwnership);
                if (lease)
                    lease->deleteLater();
            });
    });
    lease->stage(
        [contribution] {
            contribution->start();
            return true;
        },
        [contribution] { contribution->stop(); });
    if (m_scope.committed())
        lease->commit();
    error({});
    return contribution;
}
} // namespace EchoMusic
