#include "EchoUiControlContentFactory.h"
#include "EchoUiLayout.h"
#include <QMetaMethod>
#include <algorithm>
#include <utility>
#include <QMetaProperty>
#include <QQmlEngine>
#include <QQuickWindow>

namespace EchoMusic
{
namespace
{
class ControlBody final : public QQuickItem
{
public:
    ControlBody(EchoUiControlContentFactory *factory, QQuickItem *parent)
        : QQuickItem(parent), m_factory(factory)
    {
        setSize(parent->size());
    }
    ~ControlBody() override
    {
        if (m_factory)
            m_factory->restore(this);
    }

private:
    QPointer<EchoUiControlContentFactory> m_factory;
};
} // namespace
bool EchoUiControlContentFactory::supports(QQuickItem *item)
{
    return item && item->inherits("QQuickControl") &&
           item->metaObject()->indexOfProperty("contentItem") >= 0;
}
EchoUiControlContentFactory::EchoUiControlContentFactory(QQuickItem *target, QQmlEngine &engine,
                                                         QObject *parent)
    : EchoUiContentFactory(parent), m_target(target), m_engine(engine)
{
    const auto refresh = metaObject()->method(metaObject()->indexOfSlot("refreshSlots()"));
    for (const auto *name : {"contentItem", "background", "indicator"}) {
        const auto index = target->metaObject()->indexOfProperty(name);
        if (index >= 0) {
            const auto property = target->metaObject()->property(index);
            if (property.hasNotifySignal())
                connect(target, property.notifySignal(), this, refresh);
        }
    }
    refreshSlots();
    QQmlComponent adapter(
        &engine, QUrl("qrc:/qt/qml/EchoMusic/Controls/EchoUiControlContentVisibility.qml"));
    m_visibility = adapter.createWithInitialProperties({{"factory", QVariant::fromValue(this)}});
    if (m_visibility)
        m_visibility->setParent(this);
}
EchoUiControlContentFactory::~EchoUiControlContentFactory()
{
    present(nullptr);
    delete m_host;
    delete m_visibility;
}
QVariantList EchoUiControlContentFactory::renderSlots() const
{
    QVariantList result;
    for (const auto &slot : m_slots)
        if (slot.item && slot.origin)
            result.append(QVariantMap{{"item", QVariant::fromValue(slot.item.data())},
                                      {"origin", QVariant::fromValue(slot.origin.data())}});
    return result;
}
void EchoUiControlContentFactory::refreshSlots()
{
    if (!m_target)
        return;
    QList<Slot> next;
    for (const auto *name : {"background", "contentItem", "indicator"}) {
        auto *item = m_target->property(name).value<QQuickItem *>();
        if (!item || item == m_target ||
            std::any_of(next.cbegin(), next.cend(),
                        [item](const auto &slot) { return slot.item == item; }))
            continue;
        auto previous = std::find_if(m_slots.cbegin(), m_slots.cend(),
                                     [item](const auto &slot) { return slot.item == item; });
        if (previous != m_slots.cend())
            next.append(*previous);
        else {
            const auto children =
                item->parentItem() ? item->parentItem()->childItems() : QList<QQuickItem *>();
            const int at = children.indexOf(item);
            next.append({item, item->parentItem(),
                         at >= 0 && at + 1 < children.size() ? children[at + 1] : nullptr});
        }
    }
    m_slots = next;
    emit slotsChanged();
    emit changed();
}
void EchoUiControlContentFactory::restore(QQuickItem *holder)
{
    for (const auto &slot : std::as_const(m_slots))
        if (slot.item && slot.origin && slot.item->parentItem() == holder) {
            const bool focused = slot.item->hasActiveFocus();
            slot.item->setParentItem(slot.origin);
            if (slot.next && slot.next->parentItem() == slot.origin)
                slot.item->stackBefore(slot.next);
            if (focused && slot.item->window())
                slot.item->forceActiveFocus(Qt::OtherFocusReason);
        }
}
QQuickItem *EchoUiControlContentFactory::create(QQuickItem *parent, EchoUiContentFactory *)
{
    if (!m_target)
        return nullptr;
    auto *body = new ControlBody(this, parent);
    body->setImplicitWidth(m_target->implicitWidth());
    body->setImplicitHeight(m_target->implicitHeight());
    connect(m_target, &QQuickItem::implicitWidthChanged, body, [this, body] {
        if (m_target)
            body->setImplicitWidth(m_target->implicitWidth());
    });
    connect(m_target, &QQuickItem::implicitHeightChanged, body, [this, body] {
        if (m_target)
            body->setImplicitHeight(m_target->implicitHeight());
    });
    for (const auto &slot : std::as_const(m_slots))
        if (slot.item) {
            const bool focused = slot.item->hasActiveFocus();
            slot.item->setParentItem(body);
            if (focused && slot.item->window())
                slot.item->forceActiveFocus(Qt::OtherFocusReason);
        }
    return body;
}
void EchoUiControlContentFactory::present(EchoUiContentFactory *factory)
{
    if (!m_target)
        return;
    if (!factory || factory == this) {
        if (m_host) {
            m_host->setActive(false);
            m_host->setFactory(nullptr);
        }
        if (std::exchange(m_covered, false))
            emit coverageChanged();
        return;
    }
    if (!m_host) {
        m_host = new EchoUiContentHost(m_target);
        m_host->setProperty("_echoUiInternal", true);
        EchoUiLayout::setDecoration(m_host, true);
        m_host->setSize(m_target->size());
        m_host->setZ(100);
        connect(m_target, &QQuickItem::widthChanged, m_host, [this] {
            if (m_target && m_host)
                m_host->setWidth(m_target->width());
        });
        connect(m_target, &QQuickItem::heightChanged, m_host, [this] {
            if (m_target && m_host)
                m_host->setHeight(m_target->height());
        });
        // This host is C++-created, so its QML completion hook is not invoked.
        m_host->finishCreation();
    }
    if (!std::exchange(m_covered, true))
        emit coverageChanged();
    m_host->setFactory(factory);
    m_host->setActive(true);
}
} // namespace EchoMusic
