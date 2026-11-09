#include "EchoUiStructure.h"
#include <QSignalMapper>
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiLayout.h"
#include "EchoUiScope.h"
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickWindow>
#include <QEvent>
#include <algorithm>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QAbstractItemModel>
#include <QtQuick/private/qquicklistview_p.h>
#include <QtQuick/private/qquickgridview_p.h>
#include <QtQuick/private/qquicktableview_p.h>

namespace EchoMusic
{
namespace
{
QString roleFor(QQuickItem *item)
{
    if (item->property("_echoUiModelKind") == "settings")
        return "model.settings";
    if (item->property("_echoUiModelKind") == "tabs")
        return "model.tabs";
    if (item->property("_echoUiModelKind") == "menu")
        return "model.menu";
    const auto layout = EchoUiLayout::kind(item);
    if (!layout.isEmpty())
        return "layout." + layout;
    if (item->inherits("QQuickAbstractButton"))
        return "control.button";
    if (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
        return "control.input";
    if (item->inherits("QQuickText"))
        return "item.text";
    if (item->inherits("QQuickImage"))
        return "item.image";
    if (item->inherits("QQuickRectangle"))
        return "item.rectangle";
    if (item->inherits("QQuickItemView"))
        return "view.items";
    if (item->inherits("QQuickFlickable"))
        return "view.scroll";
    return "item.region";
}
} // namespace
EchoUiStructure::EchoUiStructure(EchoUiPartRegistry &registry)
    : QObject(&registry), m_registry(registry)
{
    m_update.setSingleShot(true);
    connect(&m_update, &QTimer::timeout, this, &EchoUiStructure::synchronize);
}
bool EchoUiStructure::needsDiscovery() const
{
    return std::any_of(m_consumers.cbegin(), m_consumers.cend(),
                       [](const auto &c) { return c.inspector || c.discovery; });
}
void EchoUiStructure::acquire(QObject *consumer, bool inspector, bool immediate, bool discovery)
{
    if (!consumer)
        return;
    if (!m_consumers.contains(consumer)) {
        m_consumers.insert(consumer, {inspector, discovery,
                                      connect(consumer, &QObject::destroyed, this,
                                              [this, consumer] { release(consumer); })});
    } else {
        auto &state = m_consumers[consumer];
        state.inspector = state.inspector || inspector;
        state.discovery = discovery;
    }
    if (immediate && !m_building)
        synchronize();
    else if (!m_update.isActive())
        m_update.start(0);
}
void EchoUiStructure::release(QObject *consumer)
{
    const auto it = m_consumers.find(consumer);
    if (it == m_consumers.end())
        return;
    disconnect(it->destroyed);
    m_consumers.erase(it);
    // Releasing an SDK reference can happen inside a node's removal signal.
    // Retire the remaining structural declarations after that delivery unwinds.
    if (!m_update.isActive())
        m_update.start(0);
}
void EchoUiStructure::schedule(QQuickItem *root)
{
    if (!needsDiscovery()) {
        // A later general consumer discovers the current tree from scratch.
        // Explicit declarations already publish their own lifecycle changes.
        m_full = true;
        m_declarationsDirty = true;
        if (hasConsumers() && !m_update.isActive())
            m_update.start(0);
        return;
    }
    if (root)
        m_dirty.insert(root, root);
    else if (!m_building)
        m_full = true;
    if (!m_update.isActive())
        m_update.start(0);
}
void EchoUiStructure::supersede(QQuickItem *item)
{
    auto node = m_nodes.take(item);
    if (node) {
        m_registry.detach(node);
        node->deleteLater();
    }
}
bool EchoUiStructure::owns(EchoUiPartRegistration *part) const
{
    return part && m_nodes.value(part->target()) == part;
}
void EchoUiStructure::clear()
{
    clearDeclarationAncestors();
    releaseDelegates();
    clearDiscovery();
}
void EchoUiStructure::clearDiscovery()
{
    if (m_filterInstalled) {
        qGuiApp->removeEventFilter(this);
        m_filterInstalled = false;
    }
    for (const auto &connections : std::as_const(m_watches))
        for (const auto &connection : connections)
            disconnect(connection);
    m_watches.clear();
    m_parents.clear();
    m_children.clear();
    m_dirty.clear();
    m_windows.clear();
    m_full = true;
    m_declarationsDirty = true;
    const auto nodes = std::exchange(m_nodes, {});
    for (auto node : nodes)
        if (node) {
            m_registry.detach(node);
            node->deleteLater();
        }
}
bool EchoUiStructure::eventFilter(QObject *object, QEvent *event)
{
    // New independent QML windows need not contain an explicit public part.
    // Only window show/polish events enter discovery, never paint/input events.
    if ((event->type() == QEvent::Show || event->type() == QEvent::Polish) &&
        qobject_cast<QQuickWindow *>(object))
        schedule(static_cast<QQuickWindow *>(object)->contentItem());
    return false;
}
void EchoUiStructure::synchronize()
{
    if (m_building)
        return;
    m_update.stop();
    const bool forced = std::any_of(m_consumers.cbegin(), m_consumers.cend(),
                                    [](const auto &c) { return c.inspector; });
    m_building = true;
    if (!hasConsumers() || (!m_registry.enabled() && !forced)) {
        clear();
        m_building = false;
        return;
    }
    if (!needsDiscovery()) {
        if (m_filterInstalled || !m_watches.isEmpty() || !m_nodes.isEmpty())
            clearDiscovery();
        if (std::exchange(m_declarationsDirty, false))
            synchronizeDeclarations();
        m_building = false;
        return;
    }
    clearDeclarationAncestors();
    // Consumer acquire/release does not invalidate a structural subtree.
    if (!m_full && m_dirty.isEmpty()) {
        m_building = false;
        return;
    }
    if (!m_filterInstalled) {
        qGuiApp->installEventFilter(this);
        m_filterInstalled = true;
    }
    const auto pending = std::exchange(m_dirty, {});
    QSet<QQuickItem *> roots;
    for (auto root : pending)
        if (root)
            roots.insert(root);
    // Known windows stay indexed. A local delegate/layout mutation must not
    // query every public node just to rediscover the same few native windows.
    const auto allWindows = QGuiApplication::allWindows();
    QSet<QQuickWindow *> windows;
    for (auto *window : m_windows)
        if (allWindows.contains(window))
            windows.insert(window);
    for (auto *window : allWindows)
        if (auto *quick = qobject_cast<QQuickWindow *>(window))
            if (auto *context = QQmlEngine::contextForObject(quick);
                context && context->engine() == &m_registry.engine())
                windows.insert(quick);
    bool discover = m_full;
    for (auto *root : roots)
        if (root->window() && !windows.contains(root->window()))
            discover = true;
    if (discover) {
        for (auto *root : m_registry.presentationRoots())
            if (root && root->window())
                windows.insert(root->window());
        for (auto *part : m_registry.query({}))
            if (!part->automatic() && part->target() && part->target()->window())
                windows.insert(part->target()->window());
    }
    const bool full = m_full;
    for (auto *window : windows)
        if (m_full || !m_windows.contains(window))
            roots.insert(window->contentItem());
    m_full = false;
    m_windows = windows;
    // Prune nested invalidations; a burst of delegate creation visits each
    // affected subtree once, leaving unrelated windows and pages untouched.
    for (auto *root : QList<QQuickItem *>(roots.begin(), roots.end()))
        for (auto *parent = root->parentItem(); parent; parent = parent->parentItem())
            if (roots.contains(parent)) {
                roots.remove(root);
                break;
            }
    QSet<QQuickItem *> affected;
    const auto mark = [&](auto &&self, QQuickItem *item) -> void {
        if (!item || affected.contains(item))
            return;
        affected.insert(item);
        for (auto *child : m_children.value(item))
            self(self, child);
    };
    for (auto *root : roots)
        mark(mark, root);
    if (full)
        for (auto *item : m_watches.keys())
            if (!item->window() || !windows.contains(item->window()))
                mark(mark, item);
    QSet<QQuickItem *> seen;
    const auto visit = [&](auto &&self, QQuickItem *item) -> void {
        if (!item || seen.contains(item) || item->property("echoUiSurface") == "inspector")
            return;
        seen.insert(item);
        watchView(item);
        watchDelegate(item);
        if (!EchoUiScope::accepts(item))
            return;
        const bool internal = item->property("_echoUiInternal").toBool() ||
                              (item->window() && item == item->window()->contentItem()) ||
                              item->inherits("QQuickOverlay") || item->inherits("QQuickPopupItem");
        if (!m_watches.contains(item)) {
            indexParent(item);
            m_watches[item] = {
                connect(item, &QQuickItem::childrenChanged, this, [this, item] { schedule(item); }),
                connect(item, &QQuickItem::parentChanged, this,
                        [this, item] {
                            indexParent(item);
                            schedule(item);
                        }),
                connect(item, &QQuickItem::windowChanged, this, [this, item] { schedule(item); }),
                connect(item, &QObject::objectNameChanged, this,
                        [this, item] {
                            m_registry.addressTreeChanged(item);
                            schedule(item);
                        }),
                connect(item, &QObject::destroyed, this, [this, item] {
                    m_watches.remove(item);
                    forgetIndex(item);
                    if (auto node = m_nodes.take(item)) {
                        m_registry.detach(node);
                        node->deleteLater();
                    }
                    if (!m_update.isActive())
                        m_update.start(0);
                })};
        }
        if (!internal && !m_registry.hasExplicitTarget(item)) {
            auto node = m_nodes.value(item);
            if (!node) {
                node = new EchoUiPartRegistration(this);
                m_nodes.insert(item, node);
                const auto key = item->objectName().isEmpty() ? QString("@%1").arg(++m_sequence)
                                                              : item->objectName();
                node->initializeAutomatic(m_registry, item, roleFor(item), key);
            } else if (!item->objectName().isEmpty() && node->key() != item->objectName()) {
                node->setKey(item->objectName());
            }
            if (node && node->identity().isEmpty())
                m_registry.attach(node);
        }
        const auto children = item->childItems();
        for (auto *child : children)
            self(self, child);
    };
    for (auto *root : roots)
        if (root->window() && windows.contains(root->window()))
            visit(visit, root);
    for (auto *item : affected) {
        if (m_nodes.contains(item) && (!seen.contains(item) || m_registry.hasExplicitTarget(item)))
            supersede(item);
        if (!seen.contains(item)) {
            for (const auto &connection : m_watches.take(item))
                disconnect(connection);
            forgetIndex(item);
        }
    }
    m_building = false;
}
void EchoUiStructure::forgetIndex(QQuickItem *item)
{
    if (m_parents.contains(item)) {
        auto *parent = m_parents.take(item);
        if (auto index = m_children.find(parent); index != m_children.end()) {
            index->remove(item);
            if (index->isEmpty())
                m_children.erase(index);
        }
    }
    m_children.remove(item);
}
void EchoUiStructure::indexParent(QQuickItem *item)
{
    auto *parent = item->parentItem();
    if (m_parents.contains(item) && m_parents.value(item) == parent)
        return;
    if (m_parents.contains(item)) {
        auto *previous = m_parents.value(item);
        if (auto index = m_children.find(previous); index != m_children.end()) {
            index->remove(item);
            if (index->isEmpty())
                m_children.erase(index);
        }
    }
    m_parents[item] = parent;
    m_children[parent].insert(item);
}
void EchoUiStructure::watchDelegate(QQuickItem *item)
{
    if (m_delegates.contains(item) || !item->parentItem())
        return;
    auto *view = item->parentItem()->parentItem();
    if (!view)
        return;
    QObject *attached = nullptr;
    if (auto *list = qobject_cast<QQuickListView *>(view);
        list && list->contentItem() == item->parentItem())
        attached = qmlAttachedPropertiesObject<QQuickListView>(item, true);
    else if (auto *grid = qobject_cast<QQuickGridView *>(view);
             grid && grid->contentItem() == item->parentItem())
        attached = qmlAttachedPropertiesObject<QQuickGridView>(item, true);
    else if (auto *table = qobject_cast<QQuickTableView *>(view);
             table && table->contentItem() == item->parentItem())
        attached = qmlAttachedPropertiesObject<QQuickTableView>(item, true);
    if (!attached || attached->property("view").value<QObject *>() != view)
        return;
    auto *scope = EchoUiScope::get(item, true);
    Delegate guard;
    guard.attached = attached;
    guard.view = view;
    guard.managedResource = scope->resourceKey().isEmpty();
    const QPointer<QQuickItem> root(item);
    const auto update = [this, root](bool reused) {
        if (!root || !m_delegates.contains(root))
            return;
        auto *scope = EchoUiScope::get(root, true);
        if (reused)
            scope->setActive(true);
        scope->renew();
        const auto resource = root->property("resourceId").toString();
        if (m_delegates.value(root).managedResource)
            scope->setResourceKey(resource.isEmpty() ? "@delegate" : resource);
        schedule(root);
    };
    const auto pooled = [this, root] {
        if (!root || !m_delegates.contains(root))
            return;
        EchoUiScope::get(root, true)->setActive(false);
        schedule(root);
    };
    if (auto *native = qobject_cast<QQuickItemViewAttached *>(attached)) {
        guard.connections << connect(native, &QQuickItemViewAttached::pooled, this, pooled)
                          << connect(native, &QQuickItemViewAttached::reused, this,
                                     [update] { update(true); });
    } else if (auto *native = qobject_cast<QQuickTableViewAttached *>(attached)) {
        guard.connections << connect(native, &QQuickTableViewAttached::pooled, this, pooled)
                          << connect(native, &QQuickTableViewAttached::reused, this,
                                     [update] { update(true); });
    }
    // Arbitrary delegate notify signals need a dynamic Qt connection. Give
    // each delegate a tiny receiver: QObject::sender() on the tree observer
    // otherwise searches thousands of unrelated incoming connections per reuse.
    auto *relay = new QSignalMapper(this);
    guard.relay = relay;
    relay->setMapping(item, 0);
    guard.connections << connect(relay, &QSignalMapper::mappedInt, this,
                                 [update] { update(false); });
    const auto slot = relay->metaObject()->method(relay->metaObject()->indexOfSlot("map()"));
    for (const auto &name : {"index", "row", "column", "resourceId"}) {
        const auto index = item->metaObject()->indexOfProperty(name);
        if (index >= 0) {
            const auto signal = item->metaObject()->property(index).notifySignal();
            if (signal.isValid())
                guard.connections << connect(item, signal, relay, slot);
        }
    }
    guard.connections.append(connect(item, &QObject::destroyed, this, [this, item] {
        const auto state = m_delegates.take(item);
        for (const auto &connection : state.connections)
            disconnect(connection);
        if (state.relay)
            state.relay->deleteLater();
    }));
    m_delegates.insert(item, guard);
    const auto resource = item->property("resourceId").toString();
    if (guard.managedResource)
        scope->setResourceKey(resource.isEmpty() ? "@delegate" : resource);
}
void EchoUiStructure::releaseDelegates()
{
    for (auto *view : m_views.keys())
        releaseView(view);
    for (auto *delegate : m_delegates.keys())
        releaseDelegate(delegate);
}
void EchoUiStructure::releaseView(QQuickItem *view)
{
    const auto state = m_views.take(view);
    for (const auto &connection : state.connections + state.modelConnections)
        disconnect(connection);
}
void EchoUiStructure::releaseDelegate(QQuickItem *item)
{
    const auto state = m_delegates.take(item);
    for (const auto &connection : state.connections)
        disconnect(connection);
    if (state.relay)
        state.relay->deleteLater();
    if (auto *scope = EchoUiScope::get(item)) {
        scope->setActive(true);
        if (state.managedResource)
            scope->setResourceKey({});
    }
}
void EchoUiStructure::synchronizeDeclarations()
{
    // An explicit part inside a virtual delegate still needs pooled/reused
    // and model-generation protection. Follow declaration ancestors only;
    // do not publish or observe unrelated native siblings as automatic parts.
    QSet<QQuickItem *> ancestors;
    QSet<QQuickItem *> targets;
    for (auto *part : m_registry.m_declarations)
        if (!part->automatic()) {
            targets.insert(part->target());
            // A visual move preserves QObject ownership. Keep the original
            // delegate's identity guard as well as the current visual ancestry.
            for (auto *object = static_cast<QObject *>(part->target()); object;
                 object = object->parent())
                if (auto *item = qobject_cast<QQuickItem *>(object))
                    ancestors.insert(item);
            for (auto *item = part->target(); item; item = item->parentItem()) {
                ancestors.insert(item);
            }
        }
    for (auto *item : ancestors) {
        watchView(item);
        watchDelegate(item);
        if (!targets.contains(item) && !m_declarationAncestors.contains(item)) {
            const auto changed = [this, item] {
                m_registry.addressTreeChanged(item);
                schedule(item);
                m_registry.notifyStructure();
            };
            m_declarationAncestors[item] = {
                connect(item, &QQuickItem::parentChanged, this, changed),
                connect(item, &QQuickItem::windowChanged, this, changed),
                connect(item, &QObject::objectNameChanged, this, changed),
                connect(item, &QObject::destroyed, this, [this, item] {
                    m_declarationAncestors.remove(item);
                    schedule();
                })};
        }
    }
    for (auto *item : m_declarationAncestors.keys())
        if (!ancestors.contains(item) || targets.contains(item))
            for (const auto &connection : m_declarationAncestors.take(item))
                disconnect(connection);
    for (auto *view : m_views.keys())
        if (!ancestors.contains(view))
            releaseView(view);
    for (auto *delegate : m_delegates.keys())
        if (!ancestors.contains(delegate))
            releaseDelegate(delegate);
    // Installing a delegate guard can retire the old declaration identity.
    // Publish its new identity before a synchronous query returns, so a
    // temporary discovery lease can produce and retain a valid reference.
    m_registry.flushScopeChanges();
}
void EchoUiStructure::clearDeclarationAncestors()
{
    const auto watchers = std::exchange(m_declarationAncestors, {});
    for (const auto &connections : watchers)
        for (const auto &connection : connections)
            disconnect(connection);
}
void EchoUiStructure::watchView(QQuickItem *view)
{
    if (m_views.contains(view) ||
        (!qobject_cast<QQuickItemView *>(view) && !qobject_cast<QQuickTableView *>(view)))
        return;
    View state;
    const auto model = view->metaObject()->indexOfProperty("model");
    if (model < 0)
        return;
    const auto property = view->metaObject()->property(model);
    if (property.hasNotifySignal())
        state.connections.append(
            connect(view, property.notifySignal(), this,
                    metaObject()->method(metaObject()->indexOfSlot("viewModelChanged()"))));
    state.connections.append(connect(view, &QObject::destroyed, this, [this, view] {
        const auto state = m_views.take(view);
        for (const auto &connection : state.connections + state.modelConnections)
            disconnect(connection);
    }));
    m_views.insert(view, state);
    bindModel(view);
}
void EchoUiStructure::viewModelChanged()
{
    if (auto *view = qobject_cast<QQuickItem *>(sender())) {
        renewView(view);
        bindModel(view);
        schedule(view);
    }
}
void EchoUiStructure::bindModel(QQuickItem *view)
{
    if (!m_views.contains(view))
        return;
    auto &state = m_views[view];
    for (const auto &connection : state.modelConnections)
        disconnect(connection);
    state.modelConnections.clear();
    auto *model = qobject_cast<QAbstractItemModel *>(view->property("model").value<QObject *>());
    if (!model)
        return;
    state.modelConnections << connect(model, &QAbstractItemModel::modelAboutToBeReset, this,
                                      [this, view] { renewView(view); })
                           << connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this,
                                      [this, view] { renewView(view); })
                           << connect(model, &QAbstractItemModel::rowsAboutToBeMoved, this,
                                      [this, view] { renewView(view); })
                           << connect(model, &QAbstractItemModel::columnsAboutToBeMoved, this,
                                      [this, view] { renewView(view); })
                           << connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this,
                                      [this, view] { renewView(view); })
                           << connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                                      [this, view](const QModelIndex &, int first, int last) {
                                          renewView(view, first, last, false, true);
                                      })
                           << connect(
                                  model, &QAbstractItemModel::dataChanged, this,
                                  [this, view](const QModelIndex &first, const QModelIndex &last) {
                                      renewView(view, first.row(), last.row(), true);
                                  });
}
void EchoUiStructure::renewView(QQuickItem *view, int first, int last, bool unknownOnly,
                                bool deactivate)
{
    QList<QPointer<QQuickItem>> roots;
    for (auto it = m_delegates.cbegin(); it != m_delegates.cend(); ++it)
        if (it->view == view)
            roots.append(it.key());
    for (auto root : roots) {
        if (!root)
            continue;
        auto *scope = EchoUiScope::get(root, true);
        if (unknownOnly && !scope->resourceKey().isEmpty() && !scope->resourceKey().startsWith('@'))
            continue;
        auto row = root->property("row");
        if (!row.isValid())
            row = root->property("index");
        if (first >= 0 && row.isValid() && (row.toInt() < first || row.toInt() > last))
            continue;
        if (deactivate)
            scope->setActive(false);
        else
            scope->renew();
    }
    schedule(view);
}
} // namespace EchoMusic
