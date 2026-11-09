#include "EchoUiPartRegistry.h"
#include "EchoUiModelAdapter.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiContentFactory.h"
#include "EchoUiLayout.h"
#include "EchoUiStructure.h"
#include "EchoUiScope.h"
#include "EchoUiAnchorGroup.h"
#include "EchoUiFocusOrder.h"
#include "EchoUiValue.h"
#include "EchoUiModelAdapter.h"
#include <QQmlContext>
#include <QUrl>
#include <QJsonDocument>
#include <QColor>
#include <QQuickWindow>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <QTimer>
#include <QMetaMethod>
namespace EchoMusic
{
namespace
{
QVariantMap styleCandidates(QVariantMap selector)
{
    for (const auto *field : {"state", "parentId", "parentOf", "ancestor"})
        selector.remove(QLatin1String(field));
    return selector;
}
bool instancePreference(const QString &identity)
{
    return identity.startsWith('@');
}
} // namespace
EchoUiPartRegistry::EchoUiPartRegistry(QQmlEngine &engine) : QObject(&engine), m_engine(engine) {}
void EchoUiPartRegistry::nativeStateChanged(EchoUiPartRegistration *part)
{
    if (part)
        emit partChanged(part);
    if (!part || m_styles.isEmpty() || m_statePending.contains(part))
        return;
    m_statePending.insert(part);
    const QPointer<EchoUiPartRegistration> guarded(part);
    QTimer::singleShot(0, this, [this, guarded, part] {
        m_statePending.remove(part);
        if (guarded && !guarded->identity().isEmpty()) {
            resolve(guarded);
            if (m_styleDependencySources.contains(guarded->identity()))
                notifyStructure();
        }
    });
}
void EchoUiPartRegistry::notifyStructure()
{
    if (m_styles.isEmpty() && m_moves.isEmpty() && m_moveStates.isEmpty() &&
        m_compositions.isEmpty() &&
        !isSignalConnected(QMetaMethod::fromSignal(&EchoUiPartRegistry::structureChanged)))
        return;
    if (std::exchange(m_structurePending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_structurePending = false;
        // Relational selectors depend on live ancestry, not frame geometry.
        // Ordinary UI creation has neither consumers nor rules and does no work.
        refreshStyleDependencies();
        QSet<EchoUiPartRegistration *> affected;
        for (const auto &style : std::as_const(m_styles)) {
            const auto basic = styleCandidates(style.selector);
            if (basic == style.selector && !basic.contains("address") && !basic.contains("scope") &&
                !basic.contains("resourceKey"))
                continue;
            for (auto *part : query(basic))
                affected.insert(part);
            for (const auto &id : m_styleTargets.value(style.token))
                if (auto *part = find(id))
                    affected.insert(part);
        }
        for (auto *part : affected)
            resolve(part);
        if (!m_moves.isEmpty() || !m_moveStates.isEmpty())
            resolveMoves();
        if (!m_compositions.isEmpty())
            resolveInsertions();
        emit structureChanged();
    });
}
void EchoUiPartRegistry::acquireStructure(QObject *consumer, bool inspector)
{
    acquireStructure(consumer, inspector, true);
}
void EchoUiPartRegistry::acquireStructure(QObject *consumer, bool inspector, bool discovery)
{
    if (!m_structure)
        m_structure = new EchoUiStructure(*this);
    m_structure->acquire(consumer, inspector, true, discovery);
}
bool EchoUiPartRegistry::selectorNeedsDiscovery(const QVariantMap &selector,
                                                const QVariantMap &definition) const
{
    if (definition.contains("destination") &&
        selectorNeedsDiscovery(definition.value("destination").toMap()))
        return true;
    // Relationships must include unnamed native intermediate parents. Custom
    // semantic roles are declarations; the conservative native role families
    // cover every role emitted by EchoUiStructure's native role adapter.
    if (selector.contains("parentOf") || selector.contains("ancestor") ||
        selector.contains("parentId"))
        return true;
    if (selector.contains("id")) {
        const auto *part = find(selector.value("id").toString());
        return !part || part->automatic();
    }
    const auto role = selector.value("role").toString();
    if (role.isEmpty() || role == "*")
        return true;
    for (const auto &family : {"layout", "control", "item", "view", "model"})
        if (role == QLatin1StringView(family) || role.startsWith(QLatin1StringView(family) + '.'))
            return true;
    return false;
}
void EchoUiPartRegistry::acquireStructureFor(QObject *consumer, const QVariantMap &selector,
                                             const QVariantMap &definition)
{
    acquireStructure(consumer, false, selectorNeedsDiscovery(selector, definition));
}
void EchoUiPartRegistry::releaseStructure(QObject *consumer)
{
    if (m_structure)
        m_structure->release(consumer);
    if ((!m_structure || !m_structure->hasConsumers()) && m_styles.isEmpty() &&
        m_compositions.isEmpty() && m_moves.isEmpty()) {
        for (const auto &id : m_rawAddresses.keys()) {
            forgetAddress(id);
            if (find(id))
                m_dirtyAddresses.insert(id);
        }
    }
}
void EchoUiPartRegistry::retainStructure(QObject *consumer, bool discovery)
{
    if (!m_structure)
        m_structure = new EchoUiStructure(*this);
    // References already point at a discovered node. Do not recursively scan
    // half-constructed sibling controls while delivering a registration signal.
    m_structure->acquire(consumer, false, false, discovery);
}
int EchoUiPartRegistry::structureWatchCount() const
{
    return m_structure ? m_structure->watchCount() : 0;
}
bool EchoUiPartRegistry::hasExplicitTarget(QQuickItem *item) const
{
    for (auto part : m_targets.value(item))
        if (part && !part->automatic())
            return true;
    return false;
}
EchoUiPartRegistry *EchoUiPartRegistry::create(QQmlEngine *engine, QJSEngine *)
{
    return engine ? new EchoUiPartRegistry(*engine) : nullptr;
}
EchoUiPartRegistry *EchoUiPartRegistry::forEngine(QQmlEngine &engine)
{
    constexpr auto key = "_echoUiPartRegistryCache";
    const auto cached = engine.property(key).value<QPointer<EchoUiPartRegistry>>();
    if (cached)
        return cached;
    auto *registry =
        engine.singletonInstance<EchoUiPartRegistry *>("EchoMusic.Controls", "EchoUiPartRegistry");
    if (registry)
        engine.setProperty(key, QVariant::fromValue(QPointer<EchoUiPartRegistry>(registry)));
    return registry;
}
QList<QQuickItem *> EchoUiPartRegistry::presentationRoots() const
{
    return m_presentationRoots.values();
}
QList<EchoUiPartRegistration *> EchoUiPartRegistry::declarationsWithin(QQuickItem *root,
                                                                       bool visual) const
{
    QSet<EchoUiPartRegistration *> result;
    QSet<QObject *> seen;
    const auto visit = [&](auto &&self, QObject *object) -> void {
        if (!object || seen.contains(object))
            return;
        seen.insert(object);
        if (auto *item = qobject_cast<QQuickItem *>(object)) {
            for (auto *part : m_targetDeclarations.value(item))
                if (part && part->target() == item)
                    result.insert(part);
            if (visual)
                for (auto *child : item->childItems())
                    self(self, child);
        }
        for (auto *child : object->children())
            self(self, child);
    };
    visit(visit, root);
    return result.values();
}
void EchoUiPartRegistry::addressTreeChanged(QQuickItem *root)
{
    if (m_structure && !m_structure->needsDiscovery())
        m_structure->schedule(root);
    // Uncached declarations are already dirty from attach. With no extension
    // consumer there is no reason to walk the ordinary UI's QObject subtree.
    if (m_rawAddresses.isEmpty())
        return;
    for (auto *part : declarationsWithin(root, false))
        if (!part->identity().isEmpty())
            m_dirtyAddresses.insert(part->identity());
}
void EchoUiPartRegistry::forgetAddress(const QString &id)
{
    m_dirtyAddresses.remove(id);
    m_addresses.remove(id);
    const auto remove = [&](auto &index, const QString &value) {
        auto it = index.find(value);
        if (it == index.end())
            return;
        it->remove(id);
        if (it->isEmpty())
            index.erase(it);
    };
    const auto raw = m_rawAddresses.take(id);
    remove(m_addressMembers, raw);
    if (m_addressMembers.value(raw).size() == 1)
        m_addresses[*m_addressMembers.value(raw).begin()] = raw;
    remove(m_scopeMembers, m_scopes.take(id));
    remove(m_resourceMembers, m_resources.take(id));
}
void EchoUiPartRegistry::scopeChanged(QQuickItem *root)
{
    if (!root)
        return;
    if (!m_presentationRoots.contains(root)) {
        m_presentationRoots.insert(root);
        connect(root, &QObject::destroyed, this,
                [this, root] { m_presentationRoots.remove(root); });
    }
    if ((!m_structure || !m_structure->hasConsumers()) && m_styles.isEmpty() &&
        m_compositions.isEmpty() && m_moves.isEmpty()) {
        for (const auto &id : m_rawAddresses.keys()) {
            forgetAddress(id);
            if (find(id))
                m_dirtyAddresses.insert(id);
        }
        return;
    }
    addressTreeChanged(root);
    QList<QPointer<EchoUiPartRegistration>> declarations;
    for (auto *part : declarationsWithin(root, true))
        declarations.append(part);
    for (auto part : declarations) {
        if (!part)
            continue;
        detach(part);
        if (part && !m_scopePending.contains(part))
            m_scopePending.append(part);
    }
    if (m_structure)
        m_structure->schedule(root);
    if (!std::exchange(m_scopeScheduled, true))
        QTimer::singleShot(0, this, [this] {
            m_scopeScheduled = false;
            flushScopeChanges();
        });
}
void EchoUiPartRegistry::flushScopeChanges()
{
    const auto pending = std::exchange(m_scopePending, {});
    if (pending.isEmpty())
        return;
    for (auto part : pending)
        if (part && (!part->automatic() || (m_structure && m_structure->owns(part))))
            attach(part);
    notifyStructure();
}
void EchoUiPartRegistry::modelDataChanged(QQuickItem *view)
{
    const auto declarations = m_targetDeclarations.value(view);
    QList<QPointer<EchoUiPartRegistration>> parts;
    for (auto *part : declarations)
        parts.append(part);
    for (auto part : parts)
        if (part) {
            emit part->changed();
            if (part)
                emit partChanged(part);
        }
}
void EchoUiPartRegistry::modelContextChanged(QQuickItem *root)
{
    if (!root)
        return;
    QList<QPointer<EchoUiPartRegistration>> targets;
    for (auto *part : declarationsWithin(root, true))
        targets.append(part);
    for (auto part : targets)
        if (part) {
            emit part->changed();
            emit partChanged(part);
        }
}
void EchoUiPartRegistry::rebuildAddresses() const
{
    if (m_dirtyAddresses.isEmpty())
        return;
    const auto dirty = std::exchange(m_dirtyAddresses, {});
    auto *self = const_cast<EchoUiPartRegistry *>(this);
    for (const auto &id : dirty)
        self->forgetAddress(id);
    QSet<QString> affected;
    const auto encode = [](const QString &value) {
        return QString::fromLatin1(QUrl::toPercentEncoding(value));
    };
    for (const auto &id : dirty) {
        auto part = find(id);
        if (!part || part->identity() != id || !part->target())
            continue;
        auto *window = part->target()->window();
        QString windowKey = window ? window->objectName() : QString();
        if (windowKey.isEmpty())
            if (auto *context = window ? QQmlEngine::contextForObject(window) : nullptr)
                windowKey = context->baseUrl().isLocalFile()
                                ? "component:" + context->baseUrl().fileName()
                                : context->baseUrl().toString();
        QStringList names, scopes;
        QString resource, originSurface = part->m_surface;
        bool stableScopes = true;
        for (auto *object = static_cast<QObject *>(part->target()); object;
             object = object->parent()) {
            auto *item = qobject_cast<QQuickItem *>(object);
            if (!item)
                continue;
            if (auto *scope = EchoUiScope::get(item)) {
                stableScopes = stableScopes && !scope->key().startsWith('@');
                if (originSurface.isEmpty() && !scope->surface().isEmpty())
                    originSurface = scope->surface();
                if (!scope->key().isEmpty())
                    scopes.prepend("s=" + encode(scope->key()));
                if (resource.isEmpty() && !scope->resourceKey().isEmpty())
                    resource = scope->resourceKey();
            }
            if (originSurface.isEmpty())
                originSurface = item->property("echoUiSurface").toString();
            if (item == part->target())
                continue;
            QString name;
            for (auto semantic : m_targets.value(item))
                if (semantic && !semantic->automatic() && !semantic->key().isEmpty() &&
                    !semantic->key().startsWith('@')) {
                    name = semantic->role() + ':' + semantic->key();
                    break;
                }
            if (name.isEmpty())
                name = item->objectName();
            if (!name.isEmpty())
                names.prepend(encode(name));
        }
        // Native windows are registered by their presentation's stable scope key.
        if (windowKey.isEmpty())
            windowKey = scopes.isEmpty() ? '@' + part->windowId() : QStringLiteral("native");
        if (originSurface.isEmpty())
            originSurface = windowKey.contains("mini", Qt::CaseInsensitive) ? "mini" : "main";
        const auto prefix = "ui:/" + encode(part->owner()) + '/' + encode(originSurface) + '/' +
                            encode(windowKey) + '/' + scopes.join('/');
        m_scopes[part->identity()] = prefix;
        m_resources[part->identity()] = resource;
        const bool stable = stableScopes && !part->key().isEmpty() &&
                            !part->key().startsWith('@') && !windowKey.startsWith('@') &&
                            !resource.startsWith('@');
        QString value = '@' + part->identity();
        if (stable) {
            if (!resource.isEmpty())
                names.append("r=" + encode(resource));
            names.append(encode(part->role() + ':' + part->key()));
            value = prefix + '/' + names.join('/');
            if (value.size() > 8192)
                value = '@' + part->identity();
        }
        m_rawAddresses[id] = value;
        m_addressMembers[value].insert(id);
        m_scopeMembers[prefix].insert(id);
        m_resourceMembers[resource].insert(id);
        affected.insert(value);
    }
    // Duplicate addresses stay instance-only. Removing/renaming either peer
    // restores the remaining member without invalidating unrelated addresses.
    for (const auto &raw : affected) {
        const auto members = m_addressMembers.value(raw);
        for (const auto &id : members)
            m_addresses[id] = members.size() > 1 ? '@' + id : raw;
    }
}
QString EchoUiPartRegistry::address(const EchoUiPartRegistration *part) const
{
    rebuildAddresses();
    return part ? m_addresses.value(part->identity()) : QString();
}
QString EchoUiPartRegistry::scope(const EchoUiPartRegistration *part) const
{
    rebuildAddresses();
    return part ? m_scopes.value(part->identity()) : QString();
}
QString EchoUiPartRegistry::resourceKey(const EchoUiPartRegistration *part) const
{
    rebuildAddresses();
    return part ? m_resources.value(part->identity()) : QString();
}
void EchoUiPartRegistry::requestSessionReset()
{
    for (auto i = m_parts.begin(); i != m_parts.end(); ++i)
        m_sessionResetParts.insert(i.key());
    if (std::exchange(m_sessionResetPending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_sessionResetPending = false;
        const auto ids = std::exchange(m_sessionResetParts, {});
        for (const auto &id : ids)
            if (auto *part = find(id)) {
                detach(part);
                attach(part);
            }
    });
}
void EchoUiPartRegistry::attach(EchoUiPartRegistration *part)
{
    if (!m_declarations.contains(part)) {
        m_declarations.insert(part);
        connect(part, &QObject::destroyed, this, [this, part] {
            m_declarations.remove(part);
            if (auto *target = m_declarationTargets.take(part)) {
                auto it = m_targetDeclarations.find(target);
                if (it != m_targetDeclarations.end()) {
                    it->remove(part);
                    if (it->isEmpty())
                        m_targetDeclarations.erase(it);
                }
            }
            if (m_structure && !m_structure->needsDiscovery())
                m_structure->schedule();
        });
    }
    auto *declaredTarget = m_declarationTargets.value(part);
    if (declaredTarget != part->target()) {
        if (declaredTarget) {
            auto it = m_targetDeclarations.find(declaredTarget);
            if (it != m_targetDeclarations.end()) {
                it->remove(part);
                if (it->isEmpty())
                    m_targetDeclarations.erase(it);
            }
        }
        m_declarationTargets[part] = part->target();
        if (part->target())
            m_targetDeclarations[part->target()].insert(part);
    }
    if (!part->identity().isEmpty() && find(part->identity()) == part)
        return;
    if (m_structure && !part->automatic())
        m_structure->supersede(part->target());
    if (!part->active() || !part->target() || !EchoUiScope::accepts(part->target()) ||
        !part->target()->window() ||
        (!part->target()->parentItem() &&
         part->target()->window()->contentItem() != part->target()) ||
        part->role().isEmpty())
        return;
    part->m_identity = QString::number(++m_sequence);
    emit part->identityChanged();
    m_parts.insert(part->identity(), part);
    m_keyIndex[part->key()].insert(part->identity());
    m_roleIndex[part->role()].insert(part->identity());
    part->m_indexedKey = part->key();
    part->m_indexedRole = part->role();
    m_dirtyAddresses.insert(part->identity());
    addressTreeChanged(part->target());
    part->m_indexedTarget = part->target();
    m_targets[part->target()].append(part);
    resolve(part);
    resolveComposition(part);
    if (part->surface() != "inspector")
        notifyTree();
    emit added(part);
    notifyStructure();
    if (m_structure && !part->automatic())
        m_structure->schedule(part->target());
}
void EchoUiPartRegistry::detach(EchoUiPartRegistration *part)
{
    addressTreeChanged(part->target());
    const auto id = std::exchange(part->m_identity, {});
    if (id.isEmpty())
        return;
    if (m_focusOrders.contains(id))
        suspendFocusOrders();
    if (auto order = m_focusOrders.take(id)) {
        order->release();
        order->deleteLater();
    }
    if (auto group = m_anchorGroups.take(id)) {
        group->release();
        group->deleteLater();
    }
    if (auto state = m_moveStates.take(id); state.origin && part->target()) {
        part->target()->setParentItem(state.origin);
        if (state.next && state.next->parentItem() == state.origin)
            part->target()->stackBefore(state.next);
    }
    m_parts.remove(id);
    forgetPartStyles(id);
    if (auto it = m_keyIndex.find(part->m_indexedKey); it != m_keyIndex.end()) {
        it->remove(id);
        if (it->isEmpty())
            m_keyIndex.erase(it);
    }
    if (auto it = m_roleIndex.find(part->m_indexedRole); it != m_roleIndex.end()) {
        it->remove(id);
        if (it->isEmpty())
            m_roleIndex.erase(it);
    }
    m_preferences.remove('@' + id);
    forgetAddress(id);
    auto *indexedTarget = std::exchange(part->m_indexedTarget, nullptr);
    if (auto it = m_targets.find(indexedTarget); it != m_targets.end()) {
        it->removeAll(part);
        if (it->isEmpty())
            m_targets.erase(it);
    }
    addressTreeChanged(indexedTarget);
    emit part->identityChanged();
    part->setResolved({});
    part->setReplacement({});
    const auto chains = m_chains.take(id);
    for (auto factory : chains)
        if (auto *layer = qobject_cast<EchoUiLayerContentFactory *>(factory.data()))
            layer->setRegistryActive(false);
    part->setContentFactory(nullptr, {});
    if (part->surface() != "inspector")
        notifyTree();
    emit removed(id);
    notifyStructure();
    if (m_structure && !part->automatic())
        m_structure->schedule(part->target());
}
EchoUiPartRegistration *EchoUiPartRegistry::find(const QString &id) const
{
    return m_parts.value(id);
}
bool EchoUiPartRegistry::validSelector(const QVariantMap &selector)
{
    // Bounded recursion keeps author selectors cheap to validate and execute.
    const auto validate = [](auto &&self, const QVariantMap &value, int depth) -> bool {
        if (depth > 8)
            return false;
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() == "state") {
                if (it.value().metaType().id() != QMetaType::QVariantMap ||
                    it.value().toMap().size() > 32)
                    return false;
                for (auto state = it.value().toMap(); const auto &key : state.keys()) {
                    const auto type = state.value(key).metaType().id();
                    if (key.isEmpty() || key.size() > 128 ||
                        (type != QMetaType::Bool && type != QMetaType::Int &&
                         type != QMetaType::LongLong && type != QMetaType::Double &&
                         type != QMetaType::QString) ||
                        (type == QMetaType::Double &&
                         !std::isfinite(state.value(key).toDouble())) ||
                        state.value(key).toString().size() > 1024)
                        return false;
                }
                continue;
            }
            if (it.key() == "parentOf" || it.key() == "ancestor") {
                if (it.value().metaType().id() != QMetaType::QVariantMap ||
                    !self(self, it.value().toMap(), depth + 1))
                    return false;
                continue;
            }
            if (!QStringList{"id", "role", "key", "surface", "window", "parentId", "owner",
                             "address", "scope", "resourceKey"}
                     .contains(it.key()) ||
                it.value().metaType().id() != QMetaType::QString ||
                it.value().toString().size() >
                    (it.key() == "address" || it.key() == "scope" ? 8192 : 1024))
                return false;
            if (it.key() != "role" && it.value().toString().contains('*'))
                return false;
            const auto role = it.value().toString();
            if (it.key() == "role" && role.contains('*') && role != "*" &&
                (!role.endsWith(".*") || role.count('*') != 1))
                return false;
        }
        return true;
    };
    return validate(validate, selector, 0);
}
bool EchoUiPartRegistry::matches(const EchoUiPartRegistration &part, const QVariantMap &selector)
{
    for (auto it = selector.begin(); it != selector.end(); ++it) {
        if (it.key() == "state") {
            const auto actual = part.state(), expected = it.value().toMap();
            for (auto field = expected.begin(); field != expected.end(); ++field)
                if (!actual.contains(field.key()) || actual.value(field.key()) != field.value())
                    return false;
            continue;
        }
        if (it.key() == "ancestor") {
            bool found = false;
            for (auto *node = part.m_registry ? part.m_registry->find(part.parentId()) : nullptr;
                 node; node = part.m_registry->find(node->parentId()))
                if (matches(*node, it.value().toMap())) {
                    found = true;
                    break;
                }
            if (!found)
                return false;
            continue;
        }
        if (it.key() == "parentOf") {
            if (!part.m_registry)
                return false;
            const auto children = part.m_registry->query(it.value().toMap());
            if (std::none_of(children.cbegin(), children.cend(),
                             [&part](auto *child) { return child->parentId() == part.identity(); }))
                return false;
            continue;
        }
        const auto expected = it.value().toString();
        const auto actual = it.key() == "address"       ? part.address()
                            : it.key() == "scope"       ? part.scope()
                            : it.key() == "resourceKey" ? part.resourceKey()
                            : it.key() == "id"          ? part.identity()
                            : it.key() == "role"        ? part.role()
                            : it.key() == "key"         ? part.key()
                            : it.key() == "surface"     ? part.surface()
                            : it.key() == "owner"       ? part.owner()
                            : it.key() == "parentId"    ? part.parentId()
                                                        : part.windowId();
        if (it.key() == "role" && expected == "*")
            continue;
        if (it.key() == "role" && expected.endsWith(".*")) {
            if (!actual.startsWith(expected.chopped(1)))
                return false;
        } else if (actual != expected)
            return false;
    }
    return true;
}
QList<EchoUiPartRegistration *> EchoUiPartRegistry::query(const QVariantMap &selector) const
{
    QList<EchoUiPartRegistration *> result;
    if (!validSelector(selector))
        return result;
    auto local = selector;
    QSet<QString> parents;
    const bool relational = local.contains("parentOf");
    if (relational)
        for (auto *child : query(local.take("parentOf").toMap()))
            parents.insert(child->parentId());
    QList<QPointer<EchoUiPartRegistration>> candidates;
    if (local.contains("id"))
        candidates.append(find(local.value("id").toString()));
    else if (local.contains("key"))
        for (const auto &id : m_keyIndex.value(local.value("key").toString()))
            candidates.append(find(id));
    else if (local.contains("role") && !local.value("role").toString().contains('*'))
        for (const auto &id : m_roleIndex.value(local.value("role").toString()))
            candidates.append(find(id));
    else if (local.contains("address") || local.contains("scope") ||
             local.contains("resourceKey")) {
        rebuildAddresses();
        EchoUiIndexSet<QString> ids;
        if (local.contains("address")) {
            const auto address = local.value("address").toString();
            if (address.startsWith('@')) {
                const auto id = address.mid(1);
                if (m_addresses.value(id) == address)
                    ids.insert(id);
            } else if (m_addressMembers.value(address).size() == 1)
                ids = m_addressMembers.value(address);
        } else if (local.contains("scope"))
            ids = m_scopeMembers.value(local.value("scope").toString());
        else
            ids = m_resourceMembers.value(local.value("resourceKey").toString());
        for (const auto &id : ids)
            candidates.append(find(id));
    } else
        candidates = m_parts.values();
    for (auto part : candidates)
        if (part && EchoUiScope::accepts(part->target()) &&
            (!relational || parents.contains(part->identity())) && matches(*part, local))
            result.append(part);
    std::sort(result.begin(), result.end(), [](auto *a, auto *b) {
        return a->identity().toULongLong() < b->identity().toULongLong();
    });
    return result;
}
bool EchoUiPartRegistry::normalize(QVariantMap &values, QString &error)
{
    const QStringList flags{"visible", "fillWidth", "fillHeight"};
    const QStringList colors{"foreground", "secondaryForeground", "background", "borderColor",
                             "accent"};
    const QHash<QString, QPair<double, double>> numbers{{"scale", {0, 8}},
                                                        {"padding", {0, 1024}},
                                                        {"margins", {-1024, 1024}},
                                                        {"leftMargin", {-1024, 1024}},
                                                        {"rightMargin", {-1024, 1024}},
                                                        {"topMargin", {-1024, 1024}},
                                                        {"bottomMargin", {-1024, 1024}},
                                                        {"leftPadding", {0, 1024}},
                                                        {"rightPadding", {0, 1024}},
                                                        {"topPadding", {0, 1024}},
                                                        {"bottomPadding", {0, 1024}},
                                                        {"spacing", {0, 1024}},
                                                        {"rowSpacing", {0, 1024}},
                                                        {"columnSpacing", {0, 1024}},
                                                        {"translateX", {-4096, 4096}},
                                                        {"translateY", {-4096, 4096}},
                                                        {"radius", {0, 512}},
                                                        {"borderWidth", {0, 32}},
                                                        {"opacity", {0, 1}},
                                                        {"preferredWidth", {-1, 16384}},
                                                        {"preferredHeight", {-1, 16384}},
                                                        {"minimumWidth", {0, 16384}},
                                                        {"minimumHeight", {0, 16384}},
                                                        {"maximumWidth", {0, 16384}},
                                                        {"maximumHeight", {0, 16384}},
                                                        {"alignment", {0, 511}},
                                                        {"rowSpan", {1, 256}},
                                                        {"columnSpan", {1, 256}},
                                                        {"row", {0, 4096}},
                                                        {"column", {0, 4096}},
                                                        {"fontSize", {1, 256}},
                                                        {"fontWeight", {1, 1000}},
                                                        {"letterSpacing", {-20, 100}}};
    if (values.size() > 32) {
        error = "Too many visual properties";
        return false;
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (QStringList{"text", "glyph", "toolTip"}.contains(it.key())) {
            if (it.value().metaType().id() != QMetaType::QString ||
                it.value().toString().size() > 4096) {
                error = "Expected bounded text visual property: " + it.key();
                return false;
            }
        } else if (flags.contains(it.key())) {
            if (it.value().metaType().id() != QMetaType::Bool) {
                error = "Expected boolean UI property: " + it.key();
                return false;
            }
        } else if (colors.contains(it.key())) {
            QColor color;
            if (it.value().metaType() == QMetaType::fromType<QColor>())
                color = it.value().value<QColor>();
            else if (it.value().metaType().id() == QMetaType::QString)
                color = QColor(it.value().toString());
            if (!color.isValid()) {
                error = "Invalid color: " + it.key();
                return false;
            }
            it.value() = color;
        } else if (numbers.contains(it.key())) {
            const auto type = it.value().metaType().id();
            if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong &&
                type != QMetaType::ULongLong && type != QMetaType::Double &&
                type != QMetaType::Float) {
                error = "Expected numeric visual property: " + it.key();
                return false;
            }
            const auto value = it.value().toDouble();
            if (QStringList{"row", "column", "rowSpan", "columnSpan", "alignment"}.contains(
                    it.key()) &&
                value != std::floor(value)) {
                error = "Expected integral layout property: " + it.key();
                return false;
            }
            const auto bounds = numbers.value(it.key());
            if (!std::isfinite(value) || value < bounds.first || value > bounds.second) {
                error = "Visual property outside supported range: " + it.key();
                return false;
            }
        } else if (it.key() == "font") {
            if (it.value().metaType() != QMetaType::fromType<QFont>()) {
                error = "font must be a Qt font";
                return false;
            }
        } else {
            error = "Unknown visual property: " + it.key();
            return false;
        }
    }
    for (const auto &axis : {QStringLiteral("Width"), QStringLiteral("Height")}) {
        if (values.contains("minimum" + axis) && values.contains("maximum" + axis) &&
            values.value("minimum" + axis).toDouble() > values.value("maximum" + axis).toDouble()) {
            error = "Minimum exceeds maximum layout size";
            return false;
        }
    }
    return true;
}
bool EchoUiPartRegistry::putStyle(const QString &token, const QString &owner, const QString &id,
                                  int priority, const QVariantMap &selector,
                                  const QVariantMap &values)
{
    if (!m_styles.contains(token) && m_styles.size() >= 4096)
        return false;
    const auto previous = m_styles.value(token);
    auto priorTargets = m_styleTargets.value(token);
    for (auto it = m_styles.begin(); it != m_styles.end();) {
        if (it.key() != token && !m_maskedPublications.contains(it.key()) && it->owner == owner &&
            it->id == id && it->selector == selector) {
            priorTargets.unite(m_styleTargets.value(it.key()));
            m_styleBuckets[styleBucket(it->selector)].remove(it.key());
            it = m_styles.erase(it);
        } else
            ++it;
    }
    if (const auto old = m_styles.constFind(token); old != m_styles.cend())
        m_styleBuckets[styleBucket(old->selector)].remove(token);
    m_styleBuckets[styleBucket(selector)].insert(token);
    m_styles.insert(token, {token, owner, id, priority, selector, values});
    refreshStyleDependencies();
    auto currentSelector = styleCandidates(selector),
         oldSelector = styleCandidates(previous.selector);
    auto parts = query(currentSelector);
    if (!previous.token.isEmpty())
        for (auto *part : query(oldSelector))
            if (!parts.contains(part))
                parts.append(part);
    for (const auto &id : priorTargets)
        if (auto *part = find(id); part && !parts.contains(part))
            parts.append(part);
    for (auto *part : parts)
        resolve(part);
    return true;
}
void EchoUiPartRegistry::removeStyle(const QString &token)
{
    const auto removed = m_styles.take(token);
    if (removed.token.isEmpty())
        return;
    const auto bucket = styleBucket(removed.selector);
    m_styleBuckets[bucket].remove(token);
    if (m_styleBuckets[bucket].isEmpty())
        m_styleBuckets.remove(bucket);
    refreshStyleDependencies();
    auto basic = styleCandidates(removed.selector);
    auto parts = query(basic);
    for (const auto &id : m_styleTargets.value(token))
        if (auto *part = find(id); part && !parts.contains(part))
            parts.append(part);
    for (auto *part : parts)
        resolve(part);
}
void EchoUiPartRegistry::refreshStyleDependencies()
{
    if (m_publicationDepth)
        return;
    QSet<QString> next;
    if (m_enabled)
        for (const auto &rule : std::as_const(m_styles)) {
            if (m_maskedPublications.contains(rule.token))
                continue;
            const auto collect = [&](auto &&self, const QVariantMap &selector,
                                     bool nested) -> void {
                if (nested && selector.contains("state"))
                    for (auto *part : query(styleCandidates(selector)))
                        next.insert(part->identity());
                for (const auto *field : {"ancestor", "parentOf"})
                    if (selector.contains(QLatin1String(field)))
                        self(self, selector.value(QLatin1String(field)).toMap(), true);
            };
            collect(collect, rule.selector, false);
        }
    auto changed = m_styleDependencySources;
    changed.unite(next);
    const auto previous = std::exchange(m_styleDependencySources, next);
    for (const auto &id : changed)
        if (previous.contains(id) != m_styleDependencySources.contains(id))
            if (auto *part = find(id))
                matchingStyles(part, true);
}
QString EchoUiPartRegistry::styleBucket(const QVariantMap &selector) const
{
    if (selector.contains("id"))
        return "i:" + selector.value("id").toString();
    if (selector.contains("key"))
        return "k:" + selector.value("key").toString();
    if (selector.contains("role") && !selector.value("role").toString().contains('*'))
        return "r:" + selector.value("role").toString();
    return "*";
}
QList<EchoUiPartRegistry::Style> EchoUiPartRegistry::matchingStyles(EchoUiPartRegistration *part,
                                                                    bool updateObservation) const
{
    QList<Style> rules;
    QSet<QString> tokens = m_styleBuckets.value("*");
    tokens.unite(m_styleBuckets.value("i:" + part->identity()));
    tokens.unite(m_styleBuckets.value("k:" + part->key()));
    tokens.unite(m_styleBuckets.value("r:" + part->role()));
    bool observe = false;
    for (const auto &token : tokens) {
        const auto rule = m_styles.value(token);
        if (!rule.selector.contains("state"))
            continue;
        auto basic = rule.selector;
        basic.remove("state");
        if (matches(*part, basic) && allowed(part, rule.owner, rule.id)) {
            observe = true;
            break;
        }
    }
    if (updateObservation)
        part->setStyleObserved(m_enabled &&
                               (observe || m_styleDependencySources.contains(part->identity())));
    for (const auto &token : tokens) {
        if (m_maskedPublications.contains(token) || !m_styles.contains(token))
            continue;
        const auto rule = m_styles.value(token);
        if (matches(*part, rule.selector) && allowed(part, rule.owner, rule.id))
            rules.append(rule);
    }
    return rules;
}
QVariantMap EchoUiPartRegistry::styleSources(const QString &identity) const
{
    auto *part = find(identity);
    return part ? part->visualSources() : QVariantMap();
}
void EchoUiPartRegistry::forgetPartStyles(const QString &id)
{
    for (const auto &token : m_partStyleTokens.take(id)) {
        auto it = m_styleTargets.find(token);
        if (it != m_styleTargets.end()) {
            it->remove(id);
            if (it->isEmpty())
                m_styleTargets.erase(it);
        }
    }
}
void EchoUiPartRegistry::resolve(EchoUiPartRegistration *part)
{
    if (m_publicationDepth)
        return;
    auto rules = matchingStyles(part, true);
    const auto identity = part->identity();
    forgetPartStyles(identity);
    for (const auto &rule : rules) {
        m_styleTargets[rule.token].insert(identity);
        m_partStyleTokens[identity].insert(rule.token);
    }
    std::sort(rules.begin(), rules.end(), [this, part](const auto &a, const auto &b) {
        return std::tuple(preferenceRank(part, a.owner, a.id), a.priority, a.owner, a.id) <
               std::tuple(preferenceRank(part, b.owner, b.id), b.priority, b.owner, b.id);
    });
    QVariantMap values, sources;
    for (const auto &rule : rules)
        for (auto it = rule.values.begin(); it != rule.values.end(); ++it)
            if (part->properties().contains(it.key())) {
                values.insert(it.key(), it.value());
                sources[it.key()] = QVariantMap{{"owner", rule.owner},
                                                {"id", rule.id},
                                                {"priority", rule.priority},
                                                {"value", it.value()},
                                                {"selector", rule.selector}};
            }
    part->setResolved(values, sources);
    notifyTree();
}
void EchoUiPartRegistry::putReplacement(const QString &token, const QString &part,
                                        const QString &owner, const QString &id, int priority,
                                        const QString &position)
{
    const auto old = m_replacements.constFind(token);
    if (old != m_replacements.cend() && old->priority == priority &&
        old->values.value("position") == position)
        return;
    m_replacements.insert(token,
                          {token, owner, id, priority, {{"id", part}}, {{"position", position}}});
    if (auto *p = find(part)) {
        resolveReplacement(p);
        emit p->mountOrderChanged();
    }
}
void EchoUiPartRegistry::removeReplacement(const QString &token)
{
    const auto rule = m_replacements.take(token);
    if (auto *p = find(rule.selector.value("id").toString())) {
        resolveReplacement(p);
        emit p->mountOrderChanged();
    }
}
void EchoUiPartRegistry::setMountReady(const QString &token, bool ready)
{
    auto it = m_replacements.find(token);
    if (it == m_replacements.end() || it->values.value("ready").toBool() == ready)
        return;
    it->values["ready"] = ready;
    if (auto *part = find(it->selector.value("id").toString()))
        resolveReplacement(part);
}
void EchoUiPartRegistry::resolveReplacement(EchoUiPartRegistration *part)
{
    if (m_publicationDepth)
        return;
    QStringList ordered = mountOrder(part->identity(), "replace");
    QString applied;
    for (const auto &token : ordered)
        if (m_replacements.value(token).values.value("ready").toBool())
            applied = token;
    part->setReplacement(applied, ordered.isEmpty() ? QString() : ordered.last());
}
QStringList EchoUiPartRegistry::mountOrder(const QString &part, const QString &position) const
{
    QList<Style> mounts;
    for (const auto &mount : m_replacements)
        if (auto *p = find(part); p && allowed(p, mount.owner, mount.id) &&
                                  mount.selector.value("id") == part &&
                                  mount.values.value("position") == position)
            mounts.append(mount);
    std::sort(mounts.begin(), mounts.end(), [](const auto &a, const auto &b) {
        return std::tie(a.priority, a.owner, a.id) < std::tie(b.priority, b.owner, b.id);
    });
    QStringList result;
    for (const auto &mount : mounts)
        result.append(mount.token);
    return result;
}
QString EchoUiPartRegistry::windowId(QQuickWindow *window) const
{
    if (!window)
        return {};
    if (!m_windows.contains(window)) {
        m_windows.insert(window, QString::number(++m_windowSequence));
        connect(window, &QObject::destroyed, this, [this, window] { m_windows.remove(window); });
        connect(window, &QObject::objectNameChanged, this, [this, window] {
            auto *self = const_cast<EchoUiPartRegistry *>(this);
            self->addressTreeChanged(window->contentItem());
            self->notifyStructure();
        });
    }
    return m_windows.value(window);
}
} // namespace EchoMusic

namespace EchoMusic
{
void EchoUiPartRegistry::notifyTree()
{
    if (!isSignalConnected(QMetaMethod::fromSignal(&EchoUiPartRegistry::treeChanged)))
        return;
    if (std::exchange(m_treePending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_treePending = false;
        emit treeChanged();
    });
}
QString EchoUiPartRegistry::parentOf(const EchoUiPartRegistration *part) const
{
    for (auto *parent = part->target() ? part->target()->parentItem() : nullptr; parent;
         parent = parent->parentItem())
        for (auto other : m_targets.value(parent))
            if (other && other != part)
                return other->identity();
    return {};
}
QVariantList EchoUiPartRegistry::snapshot() const
{
    QVariantList result;
    for (auto *part : query({}))
        if (part->surface() != "inspector")
            result.append(describe(part->identity()));
    return result;
}
QVariantMap EchoUiPartRegistry::describe(const QString &identity) const
{
    auto *part = find(identity);
    if (!part || part->surface() == "inspector")
        return {};
    qsizetype budget = 65536;
    auto result = QVariantMap{{"id", part->identity()},
                              {"parentId", parentOf(part)},
                              {"address", part->address()},
                              {"scope", part->scope()},
                              {"resourceKey", part->resourceKey()},
                              {"persistent", part->persistent()},
                              {"role", part->role()},
                              {"key", part->key()},
                              {"owner", part->owner()},
                              {"window", part->windowId()},
                              {"surface", part->surface()},
                              {"visible", part->presented()},
                              {"geometry", QVariantMap{{"x", part->geometry().x()},
                                                       {"y", part->geometry().y()},
                                                       {"width", part->geometry().width()},
                                                       {"height", part->geometry().height()}}},
                              {"properties", part->properties()},
                              {"actions", part->actions()},
                              {"layoutKind", part->layoutKind()},
                              {"layoutEngine", part->layoutEngine()},
                              {"automatic", part->automatic()},
                              {"insertionPositions", part->insertionPositions()},
                              {"contractVersion", part->contractVersion()},
                              {"composable", part->canCompose()},
                              {"overrides", part->overrides()},
                              {"visualSources", part->visualSources()},
                              {"state", EchoUiValue::json(part->state(), budget).toMap()},
                              {"uniqueSelector", part->persistent()
                                                     ? QVariantMap{{"address", part->address()}}
                                                     : QVariantMap{{"id", part->identity()}}},
                              {"contributions", contributions(part->identity())}};
    result["modelContext"] = part->modelContext();
    if (auto *engine = qmlEngine(part->target()))
        if (auto *adapter = EchoUiModelAdapter::forView(part->target(), *engine, false))
            result["model"] = adapter->diagnostics();
    const bool compose = part->canCompose();
    const auto *target = part->target();
    const bool move =
        target && target->parentItem() && !EchoUiLayout::kind(target->parentItem()).isEmpty();
    const auto layout = EchoUiLayout::itemProperties(target ? target->parentItem() : nullptr);
    const bool composite = target && target->property("_echoUiColumns").value<QObject *>();
    const bool table = target && target->inherits("QQuickTableView");
    const bool rows = target && (table || target->inherits("QQuickItemView"));
    const auto modelKind = composite ? QString("composite")
                           : table   ? QString("columns")
                           : rows    ? QString("rows")
                           : target  ? target->property("_echoUiModelKind").toString()
                                     : QString();
    result["capabilities"] = QVariantMap{
        {"insert", QVariantMap{{"supported", !part->insertionPositions().isEmpty()},
                               {"positions", part->insertionPositions()}}},
        {"wrap",
         QVariantMap{{"supported", compose},
                     {"reason", compose ? "" : "Native content has no composable factory"}}},
        {"replace",
         QVariantMap{{"supported", compose},
                     {"reason", compose ? "" : "Native content has no composable factory"}}},
        {"hide", QVariantMap{{"supported", target != nullptr}}},
        {"layout", QVariantMap{{"supported", !layout.isEmpty()},
                               {"properties", layout},
                               {"engine", part->layoutEngine()}}},
        {"move", QVariantMap{{"supported", move},
                             {"reason",
                              move ? "A unique destination container in the same window is required"
                                   : "The source is not in a native container"}}},
        {"style", QVariantMap{{"supported", !part->properties().isEmpty()},
                              {"properties", part->properties()}}},
        {"model",
         QVariantMap{{"kind", modelKind},
                     {"supported", !modelKind.isEmpty()},
                     {"nativeRows", rows},
                     {"nativeColumns", table || composite},
                     {"relativePlacement", true},
                     {"structureOperations", QStringList{"insert", "hide", "move", "replace"}}}}};
    return result;
}
QVariantList EchoUiPartRegistry::contributions(const QString &part) const
{
    QVariantList result;
    auto *p = find(part);
    if (!p)
        return result;
    for (const auto &rule : m_styles)
        if (matches(*p, rule.selector))
            result.append(QVariantMap{{"owner", rule.owner},
                                      {"id", rule.id},
                                      {"operation", "style"},
                                      {"priority", rule.priority},
                                      {"values", rule.values}});
    for (const auto &c : m_compositions)
        if (c.part == part) {
            const auto effective = compositionState(c.token);
            result.append(
                QVariantMap{{"owner", c.owner},
                            {"id", c.id},
                            {"operation", c.operation},
                            {"priority", c.priority},
                            {"status", effective.value("state", c.status)},
                            {"error", effective.value("error", c.error)},
                            {"ready", c.status == "active" && (!c.factory || c.factory->ready())},
                            {"enabled", allowed(p, c.owner, c.id)}});
        }
    for (const auto &c : m_replacements)
        if (c.selector.value("id") == part)
            result.append(QVariantMap{{"owner", c.owner},
                                      {"id", c.id},
                                      {"operation", c.values.value("position")},
                                      {"priority", c.priority}});
    return result;
}
QVariantMap EchoUiPartRegistry::compositionState(const QString &token) const
{
    const auto record = m_compositions.constFind(token);
    if (record == m_compositions.cend())
        return {};
    const bool adjacent = record->operation == "insert" &&
                          (record->position == "before" || record->position == "after");
    if (auto group = m_anchorGroups.value(record->part);
        group && adjacent && record->status == "active" && group->pending())
        return {{"state", "loading"}, {"error", QString()}};
    if (auto group = m_anchorGroups.value(record->part);
        group && adjacent && record->status == "active" && !group->capacityError().isEmpty())
        return {{"state", "constrained"}, {"error", group->capacityError()}};
    return {{"state", record->status}, {"error", record->error}};
}
void EchoUiPartRegistry::setPreference(const QString &id, const QStringList &order)
{
    if (id.isEmpty() || id.size() > 8192 || order.size() > 128 ||
        !std::any_of(m_parts.cbegin(), m_parts.cend(),
                     [&id](auto part) { return part && part->address() == id; }))
        return;
    if (!order.isEmpty() && !m_preferences.contains(id) && m_preferences.size() >= 1024)
        return;
    if (order.isEmpty())
        m_preferences.remove(id);
    else
        m_preferences.insert(id, order);
    for (auto part : m_parts)
        if (part && part->address() == id) {
            resolve(part);
            resolveComposition(part);
            resolveReplacement(part);
        }
    resolveMoves();
    resolveInsertions();
    emit preferencesChanged();
    notifyTree();
}
void EchoUiPartRegistry::resetPreferences()
{
    m_preferences.clear();
    for (auto part : m_parts)
        if (part) {
            resolve(part);
            resolveComposition(part);
            resolveReplacement(part);
        }
    resolveMoves();
    resolveInsertions();
    emit preferencesChanged();
    notifyTree();
}
bool EchoUiPartRegistry::putComposition(const QString &token, const QString &part,
                                        const QString &owner, const QString &id, int priority,
                                        const QString &operation, EchoUiContentFactory *factory)
{
    if (!find(part) || (!m_compositions.contains(token) && m_compositions.size() >= 4096))
        return false;
    for (auto it = m_compositions.begin(); it != m_compositions.end();) {
        if (it.key() != token && !m_maskedPublications.contains(it.key()) && it->part == part &&
            it->owner == owner && it->id == id)
            it = m_compositions.erase(it);
        else
            ++it;
    }
    m_compositions.insert(token,
                          {token, part, owner, id, operation, priority, factory, {}, {}, {}, {}});
    resolveComposition(find(part));
    return true;
}
void EchoUiPartRegistry::removeComposition(const QString &token)
{
    const auto c = m_compositions.take(token);
    if (auto *p = find(c.part))
        resolveComposition(p);
    resolveInsertions();
}
void EchoUiPartRegistry::reportComposition(const QString &token, const QString &part,
                                           const QString &owner, const QString &id,
                                           const QString &operation, int priority,
                                           const QString &status, const QString &error)
{
    if (!m_compositions.contains(token)) {
        if (!find(part) || m_compositions.size() >= 4096)
            return;
        m_compositions.insert(
            token, {token, part, owner, id, operation, priority, nullptr, {}, {}, {}, {}});
    }
    auto &record = m_compositions[token];
    if (record.status == status && record.error == error)
        return;
    record.status = status;
    record.error = error;
    notifyTree();
}
void EchoUiPartRegistry::setCompositionItem(const QString &token, QQuickItem *item,
                                            const QString &position, const QString &axis, qreal gap)
{
    auto i = m_compositions.find(token);
    if (i == m_compositions.end())
        return;
    i->item = item;
    i->position = position;
    i->axis = axis;
    i->gap = gap;
    resolveInsertions();
}
void EchoUiPartRegistry::suspendFocusOrders()
{
    for (auto it = m_appliedFocusOrders.crbegin(); it != m_appliedFocusOrders.crend(); ++it)
        if (*it)
            (*it)->suspend();
    m_appliedFocusOrders.clear();
}
void EchoUiPartRegistry::resolveInsertions()
{
    if (m_publicationDepth)
        return;
    QList<Composition> records;
    QSet<QQuickItem *> inserted;
    for (const auto &record : m_compositions)
        if (!m_maskedPublications.contains(record.token) && record.item &&
            record.operation == "insert") {
            records.append(record);
            inserted.insert(record.item);
        }
    std::sort(records.begin(), records.end(), [this](const auto &a, const auto &b) {
        return std::tuple(a.part, a.position, preferenceRank(find(a.part), a.owner, a.id),
                          a.priority, a.owner, a.id) <
               std::tuple(b.part, b.position, preferenceRank(find(b.part), b.owner, b.id),
                          b.priority, b.owner, b.id);
    });
    QHash<QString, QPointer<QQuickItem>> tails;
    QHash<QString, QList<Composition>> anchors;
    for (const auto &record : records) {
        auto *part = find(record.part);
        auto *item = record.item.data();
        if (!part || !item || !item->parentItem())
            continue;
        if ((record.position == "before" || record.position == "after") && part->target() &&
            part->target()->parentItem() &&
            EchoUiLayout::kind(part->target()->parentItem()).isEmpty()) {
            anchors[record.part].append(record);
            continue;
        }
        auto *parent = item->parentItem();
        const auto key = record.part + ':' + record.position;
        auto *previous = tails.value(key).data();
        if (previous && previous->parentItem() == parent)
            item->stackAfter(previous);
        else if (record.position == "before" && part->target()->parentItem() == parent)
            item->stackBefore(part->target());
        else if (record.position == "after" && part->target()->parentItem() == parent)
            item->stackAfter(part->target());
        else {
            const auto children = parent->childItems();
            if (record.position == "prepend" || record.position == "background") {
                for (auto *child : children)
                    if (child != item && !inserted.contains(child)) {
                        item->stackBefore(child);
                        break;
                    }
            } else {
                for (auto it = children.crbegin(); it != children.crend(); ++it)
                    if (*it != item && !inserted.contains(*it)) {
                        item->stackAfter(*it);
                        break;
                    }
            }
        }
        tails[key] = item;
    }
    QHash<QString, QList<Composition>> focusGroups;
    for (const auto &record : records)
        if (record.item && (record.position == "before" || record.position == "after"))
            focusGroups[record.part].append(record);
    suspendFocusOrders();
    for (const auto &key : m_focusOrders.keys())
        if (!focusGroups.contains(key))
            if (auto order = m_focusOrders.take(key)) {
                order->release();
                order->deleteLater();
            }
    auto focusKeys = focusGroups.keys();
    std::sort(focusKeys.begin(), focusKeys.end());
    for (const auto &focusKey : focusKeys) {
        const auto it = focusGroups.constFind(focusKey);
        auto *part = find(it.key());
        if (!part || !part->target())
            continue;
        auto &order = m_focusOrders[it.key()];
        if (!order) {
            order = new EchoUiFocusOrder(part->target(), this);
            connect(order, &EchoUiFocusOrder::refreshRequested, this,
                    &EchoUiPartRegistry::resolveInsertions);
        }
        QList<QQuickItem *> before, after;
        for (const auto &record : it.value()) {
            if (record.position == "before")
                before.append(record.item);
            else
                after.append(record.item);
        }
        const auto kind = EchoUiLayout::kind(part->target()->parentItem());
        const auto axis = kind == "column" ? "vertical"
                          : kind.isEmpty() ? it.value().last().axis
                                           : "horizontal";
        order->arrange(before, after, axis);
        m_appliedFocusOrders.append(order);
    }
    for (const auto &key : m_anchorGroups.keys())
        if (!anchors.contains(key))
            if (auto group = m_anchorGroups.take(key)) {
                group->release();
                group->deleteLater();
            }
    QSet<QQuickItem *> anchorPeers;
    for (const auto &key : anchors.keys())
        if (auto *p = find(key); p && p->target())
            anchorPeers.insert(p->target());
    for (auto it = anchors.cbegin(); it != anchors.cend(); ++it) {
        auto *part = find(it.key());
        if (!part || !part->target())
            continue;
        auto group = m_anchorGroups.value(it.key());
        if (!group) {
            group = new EchoUiAnchorGroup(m_engine, part->target(), this);
            m_anchorGroups.insert(it.key(), group);
            connect(group, &EchoUiAnchorGroup::arranged, part, [this, part] {
                emit part->changed();
                emit compositionChanged(part->identity());
                notifyTree();
            });
        }
        QList<QQuickItem *> before, after;
        for (const auto &record : it.value())
            if (record.item)
                (record.position == "before" ? before : after).append(record.item);
        const auto winner =
            *std::max_element(it->cbegin(), it->cend(), [this, part](const auto &a, const auto &b) {
                return std::tuple(preferenceRank(part, a.owner, a.id), a.priority, a.owner, a.id) <
                       std::tuple(preferenceRank(part, b.owner, b.id), b.priority, b.owner, b.id);
            });
        group->setPeers(anchorPeers);
        group->arrange(before, after, winner.axis, winner.gap);
    }
}
void EchoUiPartRegistry::resolveComposition(EchoUiPartRegistration *part)
{
    if (m_publicationDepth)
        return;
    if (!part)
        return;
    if (part->nativeFactory())
        part->nativeFactory()->setOwnerTag(part->owner());
    // Most discovered nodes have no content factory. Do not build all scoped
    // addresses on every attach merely to sort an empty wrapping chain.
    if (!part->nativeFactory() && part->compositionSignature().isEmpty())
        return;
    QList<Composition> records;
    for (const auto &c : m_compositions)
        if (!m_maskedPublications.contains(c.token) && c.part == part->identity() &&
            (c.operation == "replace" || c.operation == "wrap") && allowed(part, c.owner, c.id) &&
            c.factory && c.factory->ready())
            records.append(c);
    const auto preference = records.size() > 1 && !m_preferences.isEmpty()
                                ? m_preferences.value(part->address())
                                : QStringList();
    std::sort(records.begin(), records.end(), [&](const auto &a, const auto &b) {
        return std::tuple(preference.indexOf(a.owner + ":" + a.id), a.priority, a.owner, a.id) <
               std::tuple(preference.indexOf(b.owner + ":" + b.id), b.priority, b.owner, b.id);
    });
    QString signature = QString::number(quintptr(part->nativeFactory()));
    for (const auto &c : records)
        signature += '|' + c.token + ':' + c.operation;
    if (signature == part->compositionSignature()) {
        notifyTree();
        return;
    }
    const auto previous = m_chains.take(part->identity());
    EchoUiContentFactory *current = part->nativeFactory();
    const auto cacheKey = part->identity() + QChar(0) + signature;
    const auto cached = m_chainCache.value(cacheKey);
    const bool reusable = cached.root && !cached.layers.isEmpty() &&
                          std::all_of(cached.layers.begin(), cached.layers.end(),
                                      [](const auto &layer) { return !layer.isNull(); });
    if (reusable) {
        current = cached.root;
        m_chains[part->identity()] = cached.layers;
        for (auto layer : cached.layers)
            if (auto *recipe = qobject_cast<EchoUiLayerContentFactory *>(layer.data()))
                recipe->setRegistryActive(true);
    } else {
        for (const auto &c : records)
            if (c.operation == "replace")
                current = c.factory;
        for (const auto &c : records)
            if (c.operation == "wrap") {
                current = new EchoUiLayerContentFactory(c.factory, current, this);
                m_chains[part->identity()].append(current);
            }
        if (m_chains.contains(part->identity())) {
            m_chainCache[cacheKey] = {current, m_chains.value(part->identity())};
            for (auto layer : m_chains.value(part->identity()))
                connect(layer, &QObject::destroyed, this, [this, cacheKey] {
                    const auto found = m_chainCache.constFind(cacheKey);
                    if (found != m_chainCache.cend() &&
                        std::any_of(found->layers.begin(), found->layers.end(),
                                    [](const auto &item) { return item.isNull(); }))
                        m_chainCache.remove(cacheKey);
                });
        }
    }
    const bool replaced = std::any_of(records.cbegin(), records.cend(), [](const auto &record) {
        return record.operation == "replace";
    });
    part->setContentFactory(current, signature, replaced);
    for (auto factory : previous)
        if (auto *layer = qobject_cast<EchoUiLayerContentFactory *>(factory.data());
            layer && !m_chains.value(part->identity()).contains(factory))
            layer->setRegistryActive(false);
    notifyTree();
}
} // namespace EchoMusic

namespace EchoMusic
{
QStringList EchoUiPartRegistry::preference(const QString &address) const
{
    return m_preferences.value(address);
}
bool EchoUiPartRegistry::allowed(const EchoUiPartRegistration *part, const QString &owner,
                                 const QString &id) const
{
    if (!m_enabled || !part || part->surface() == "inspector")
        return false;
    if (m_preferences.isEmpty())
        return true;
    const auto order = preference(part->address());
    return !order.contains("@host") && !order.contains('!' + owner + ':' + id);
}
int EchoUiPartRegistry::preferenceRank(const EchoUiPartRegistration *part, const QString &owner,
                                       const QString &id) const
{
    return part && !m_preferences.isEmpty() ? preference(part->address()).indexOf(owner + ':' + id)
                                            : -1;
}
QVariantMap EchoUiPartRegistry::preferences() const
{
    QVariantMap result;
    for (auto it = m_preferences.begin(); it != m_preferences.end(); ++it)
        if (!instancePreference(it.key()))
            result.insert(it.key(), it.value());
    return {{"enabled", m_enabled}, {"order", result}};
}
void EchoUiPartRegistry::setPreferences(const QVariantMap &value)
{
    m_preferences.clear();
    const auto orders = value.value("order").toMap();
    if (orders.size() <= 1024)
        for (auto it = orders.begin(); it != orders.end(); ++it)
            if (it.key().size() <= 8192 && it.key().startsWith("ui:/") &&
                it.value().toStringList().size() <= 128 && !instancePreference(it.key()))
                m_preferences.insert(it.key(), it.value().toStringList());
    m_enabled = value.value("enabled", true).toBool();
    refreshStyleDependencies();
    if (m_structure)
        m_structure->schedule();
    for (auto part : m_parts)
        if (part) {
            resolve(part);
            resolveComposition(part);
            resolveReplacement(part);
        }
    resolveMoves();
    resolveInsertions();
    emit preferencesChanged();
    notifyTree();
}
void EchoUiPartRegistry::setEnabled(bool value)
{
    if (m_enabled == value)
        return;
    auto settings = preferences();
    settings["enabled"] = value;
    setPreferences(settings);
    if (m_structure)
        m_structure->schedule();
}
bool EchoUiPartRegistry::putMove(const QString &token, const QString &part,
                                 const QString &destination, const QString &position,
                                 const QString &owner, const QString &id, int priority)
{
    if (!m_moves.contains(token) && m_moves.size() >= 4096)
        return false;
    auto *source = find(part), *dest = find(destination);
    if (!source || !dest || !source->target() || !dest->target() || dest->layoutKind().isEmpty() ||
        source->target()->window() != dest->target()->window() || !source->target()->parentItem() ||
        EchoUiLayout::kind(source->target()->parentItem()).isEmpty())
        return false;
    for (auto *ancestor = dest->target(); ancestor; ancestor = ancestor->parentItem())
        if (ancestor == source->target())
            return false;
    for (auto it = m_moves.begin(); it != m_moves.end();) {
        if (it.key() != token && !m_maskedPublications.contains(it.key()) &&
            it->value("part") == part && it->value("owner") == owner && it->value("id") == id)
            it = m_moves.erase(it);
        else
            ++it;
    }
    m_moves[token] = {
        {"part", part}, {"destination", destination}, {"position", position}, {"owner", owner},
        {"id", id},     {"priority", priority}};
    resolveMoves();
    notifyTree();
    return true;
}
void EchoUiPartRegistry::removeMove(const QString &token)
{
    m_moves.remove(token);
    resolveMoves();
    notifyTree();
}
QString EchoUiPartRegistry::moveWinner(const QString &part) const
{
    return m_moveStates.value(part).token;
}
void EchoUiPartRegistry::resolveMoves()
{
    if (m_publicationDepth)
        return;
    if (std::exchange(m_moving, true)) {
        // Scoped retirement can remove another move synchronously. Replay
        // once after the current parent changes, rather than losing revocation.
        auto *retry = findChild<QTimer *>(QStringLiteral("moveReplay"), Qt::FindDirectChildrenOnly);
        if (!retry) {
            retry = new QTimer(this);
            retry->setObjectName("moveReplay");
            retry->setSingleShot(true);
            connect(retry, &QTimer::timeout, this, &EchoUiPartRegistry::resolveMoves);
        }
        if (!retry->isActive())
            retry->start(0);
        return;
    }
    QSet<QString> parts;
    for (const auto &move : m_moves)
        parts.insert(move.value("part").toString());
    for (auto it = m_moveStates.begin(); it != m_moveStates.end(); ++it)
        parts.insert(it.key());
    for (const auto &identity : parts) {
        auto *part = find(identity);
        if (!part || !part->target()) {
            m_moveStates.remove(identity);
            continue;
        }
        QList<QString> tokens;
        for (auto it = m_moves.begin(); it != m_moves.end(); ++it)
            if (!m_maskedPublications.contains(it.key()) && it->value("part") == identity &&
                allowed(part, it->value("owner").toString(), it->value("id").toString()) &&
                find(it->value("destination").toString()))
                tokens.append(it.key());
        std::sort(tokens.begin(), tokens.end(), [&](const auto &a, const auto &b) {
            const auto x = m_moves.value(a), y = m_moves.value(b);
            return std::tuple(
                       preferenceRank(part, x.value("owner").toString(), x.value("id").toString()),
                       x.value("priority").toInt(), x.value("owner").toString(),
                       x.value("id").toString()) <
                   std::tuple(
                       preferenceRank(part, y.value("owner").toString(), y.value("id").toString()),
                       y.value("priority").toInt(), y.value("owner").toString(),
                       y.value("id").toString());
        });
        const auto winner = tokens.isEmpty() ? QString() : tokens.last();
        if (m_moveStates.contains(identity) && m_moveStates.value(identity).token == winner)
            continue;
        auto *target = part->target();
        auto &state = m_moveStates[identity];
        if (!state.origin) {
            state.origin = target->parentItem();
            if (state.origin) {
                const auto siblings = state.origin->childItems();
                const auto i = siblings.indexOf(target);
                if (i + 1 < siblings.size())
                    state.next = siblings[i + 1];
            }
        }
        if (state.origin) {
            target->setParentItem(state.origin);
            if (state.next && state.next->parentItem() == state.origin)
                target->stackBefore(state.next);
        }
        state.token.clear();
        if (!winner.isEmpty()) {
            const auto move = m_moves.value(winner);
            auto *dest = find(move.value("destination").toString());
            bool cycle = false;
            for (auto *ancestor = dest->target(); ancestor; ancestor = ancestor->parentItem())
                if (ancestor == target)
                    cycle = true;
            if (!cycle) {
                target->setParentItem(dest->target());
                if (move.value("position") == "prepend") {
                    const auto children = dest->target()->childItems();
                    if (!children.isEmpty() && children.first() != target)
                        target->stackBefore(children.first());
                }
                state.token = winner;
            }
        } else
            m_moveStates.remove(identity);
    }
    m_moving = false;
}
void EchoUiPartRegistry::beginPublication()
{
    ++m_publicationDepth;
}
void EchoUiPartRegistry::endPublication()
{
    if (m_publicationDepth <= 0 || --m_publicationDepth)
        return;
    // One GUI turn resolves the complete record set. Pending tree/SDK callbacks
    // run afterwards; no frame is rendered between these field publications.
    refreshStyleDependencies();
    const auto parts = query({});
    for (auto *part : parts) {
        resolve(part);
        resolveReplacement(part);
        resolveComposition(part);
    }
    resolveMoves();
    resolveInsertions();
}
void EchoUiPartRegistry::maskPublication(const QSet<QString> &tokens, bool masked)
{
    for (const auto &token : tokens) {
        if (masked)
            m_maskedPublications.insert(token);
        else
            m_maskedPublications.remove(token);
        const auto record = m_compositions.value(token);
        if (record.item)
            EchoUiLayout::setDecoration(record.item, masked || record.position == "background" ||
                                                         record.position == "foreground");
    }
}
QString EchoUiPartRegistry::settleInsertions(const QSet<QString> &targets)
{
    for (auto group = m_anchorGroups.cbegin(); group != m_anchorGroups.cend(); ++group) {
        if (!group.value() || (!targets.isEmpty() && !targets.contains(group.key())))
            continue;
        group.value()->settle();
        if (!group.value()->capacityError().isEmpty())
            return group.value()->capacityError();
        if (group.value()->pending())
            return "Native insertion layout did not settle";
    }
    return {};
}
bool EchoUiPartRegistry::canPublish(int styles, int compositions, int moves) const
{
    return styles >= 0 && compositions >= 0 && moves >= 0 && m_styles.size() + styles <= 4096 &&
           m_compositions.size() + compositions <= 4096 && m_moves.size() + moves <= 4096;
}
} // namespace EchoMusic
