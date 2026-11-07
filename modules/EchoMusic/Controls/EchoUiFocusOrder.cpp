#include "EchoUiFocusOrder.h"
#include <QQmlProperty>
#include <QTimer>
#include <QSet>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQml/private/qqmlanybinding_p.h>
#include <QtQml/private/qqmlproperty_p.h>

namespace EchoMusic
{
namespace
{
QQuickKeyNavigationAttached *navigation(QQuickItem *item)
{
    return qobject_cast<QQuickKeyNavigationAttached *>(
        qmlAttachedPropertiesObject<QQuickKeyNavigationAttached>(item, true));
}
QQuickKeyNavigationAttachedPrivate *details(QQuickKeyNavigationAttached *item)
{
    return static_cast<QQuickKeyNavigationAttachedPrivate *>(QObjectPrivate::get(item));
}
void explicitLinks(QQuickKeyNavigationAttachedPrivate *data)
{
    data->leftSet = data->rightSet = data->upSet = data->downSet = data->tabSet = data->backtabSet =
        true;
}
} // namespace
struct EchoUiFocusOrder::Data
{
    struct Link
    {
        QQmlProperty property;
        QQmlAnyBinding binding;
        QPointer<QQuickItem> value;
    };
    struct Node
    {
        QPointer<QQuickKeyNavigationAttached> navigation;
        QList<Link> links;
        bool left, right, up, down, tab, backtab;
    };
    QList<Node> nodes;
    void restore()
    {
        for (auto &node : nodes)
            if (node.navigation)
                explicitLinks(details(node.navigation));
        for (auto &node : nodes)
            if (node.navigation) {
                for (auto &link : node.links) {
                    QQmlPropertyPrivate::write(
                        link.property, QVariant::fromValue(link.value.data()),
                        QQmlPropertyData::DontRemoveBinding | QQmlPropertyData::BypassInterceptor);
                    if (link.binding)
                        link.binding.installOn(link.property);
                }
            }
        for (auto &node : nodes)
            if (node.navigation) {
                auto *data = details(node.navigation);
                data->leftSet = node.left;
                data->rightSet = node.right;
                data->upSet = node.up;
                data->downSet = node.down;
                data->tabSet = node.tab;
                data->backtabSet = node.backtab;
            }
        nodes.clear();
    }
    QQuickKeyNavigationAttached *capture(QQuickItem *item)
    {
        auto *nav = navigation(item);
        auto *data = details(nav);
        Node node{nav,           {},           data->leftSet,   data->rightSet, data->upSet,
                  data->downSet, data->tabSet, data->backtabSet};
        for (const auto *name : {"tab", "backtab", "left", "right", "up", "down"}) {
            QQmlProperty property(nav, QString::fromLatin1(name));
            node.links.append({property, QQmlAnyBinding::takeFrom(property),
                               property.read().value<QQuickItem *>()});
        }
        explicitLinks(data);
        nodes.append(node);
        return nav;
    }
};
EchoUiFocusOrder::EchoUiFocusOrder(QQuickItem *source, QObject *parent)
    : QObject(parent), m_data(std::make_unique<Data>()), m_source(source)
{
}
EchoUiFocusOrder::~EchoUiFocusOrder()
{
    release();
}
void EchoUiFocusOrder::release()
{
    m_released = true;
    suspend();
}
void EchoUiFocusOrder::suspend()
{
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_data->restore();
}
void EchoUiFocusOrder::arrange(const QList<QQuickItem *> &before, const QList<QQuickItem *> &after,
                               const QString &axis)
{
    if (m_released)
        return;
    QList<QPointer<QQuickItem>> b, a;
    for (auto *item : before)
        b.append(item);
    for (auto *item : after)
        a.append(item);
    if (b == m_before && a == m_after && axis == m_axis && !m_data->nodes.isEmpty())
        return;
    m_before = b;
    m_after = a;
    m_axis = axis;
    refresh();
}
void EchoUiFocusOrder::schedule()
{
    if (m_released || m_updating || std::exchange(m_pending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        emit refreshRequested();
    });
}
void EchoUiFocusOrder::refresh()
{
    if (m_released || !m_source || m_updating)
        return;
    m_updating = true;
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_data->restore();
    QList<QQuickItem *> ordered;
    QSet<QQuickItem *> seen;
    const auto collect = [&](auto &&self, QQuickItem *item, bool native) -> void {
        if (!item || seen.contains(item))
            return;
        seen.insert(item);
        m_connections << connect(item, &QQuickItem::childrenChanged, this,
                                 &EchoUiFocusOrder::schedule)
                      << connect(item, &QQuickItem::activeFocusOnTabChanged, this,
                                 &EchoUiFocusOrder::schedule)
                      << connect(item, &QObject::destroyed, this, &EchoUiFocusOrder::schedule);
        bool owned = !native;
        if (native)
            for (auto *object = static_cast<QObject *>(item); object; object = object->parent()) {
                if (object == m_source) {
                    owned = true;
                    break;
                }
                if (object->property("_echoPluginOwner").isValid())
                    break;
            }
        if (owned && item->activeFocusOnTab())
            ordered.append(item);
        for (auto *child : item->childItems())
            self(self, child, native);
    };
    for (auto item : m_before)
        collect(collect, item, false);
    const qsizetype firstNative = ordered.size();
    collect(collect, m_source, true);
    const qsizetype lastNative = ordered.size();
    for (auto item : m_after)
        collect(collect, item, false);
    if (ordered.isEmpty()) {
        m_updating = false;
        return;
    }
    const QSet<QQuickItem *> group(ordered.begin(), ordered.end());
    const auto outside = [&](QQuickItem *start, bool forward) -> QQuickItem * {
        QSet<QQuickItem *> visited;
        auto *item = start;
        while (item && !visited.contains(item)) {
            visited.insert(item);
            item = item->nextItemInFocusChain(forward);
            if (item && !group.contains(item) && item->activeFocusOnTab() && item->isVisible() &&
                item->isEnabled())
                return item;
        }
        return nullptr;
    };
    auto *previous =
        outside(firstNative < lastNative ? ordered[firstNative] : m_source.data(), false);
    auto *next =
        outside(firstNative < lastNative ? ordered[lastNative - 1] : m_source.data(), true);
    if (firstNative < lastNative) {
        if (auto *original = navigation(ordered[firstNative])->backtab();
            original && !group.contains(original))
            previous = original;
        if (auto *original = navigation(ordered[lastNative - 1])->tab();
            original && !group.contains(original))
            next = original;
    }
    QHash<QQuickItem *, QQuickKeyNavigationAttached *> nav;
    // Capture every old binding/explicitness before any link is written. Qt
    // setters can otherwise install implicit reciprocal links on other nodes.
    for (auto *item : ordered)
        nav[item] = m_data->capture(item);
    if (previous)
        nav[previous] = m_data->capture(previous);
    if (next && next != previous)
        nav[next] = m_data->capture(next);
    for (qsizetype i = 0; i < ordered.size(); ++i) {
        auto *before = i ? ordered[i - 1] : previous ? previous : ordered.last();
        auto *after = i + 1 < ordered.size() ? ordered[i + 1] : next ? next : ordered.first();
        nav[ordered[i]]->setBacktab(before);
        nav[ordered[i]]->setTab(after);
        if (m_axis == "vertical") {
            nav[ordered[i]]->setUp(before);
            nav[ordered[i]]->setDown(after);
        } else {
            nav[ordered[i]]->setLeft(before);
            nav[ordered[i]]->setRight(after);
        }
    }
    if (previous)
        nav[previous]->setTab(ordered.first());
    if (next)
        nav[next]->setBacktab(ordered.last());
    m_updating = false;
}
} // namespace EchoMusic
