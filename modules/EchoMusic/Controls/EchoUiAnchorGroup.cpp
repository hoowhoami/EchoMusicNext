#include "EchoUiAnchorGroup.h"
#include "EchoUiLayout.h"
#include <QQmlComponent>
#include <QTimer>
#include <QTransform>
#include <algorithm>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquickanchors_p.h>

namespace EchoMusic
{
namespace
{
bool publicationHeld(QQuickItem *item)
{
    if (item->property("_echoUiPublicationHeld").toBool())
        return true;
    for (auto *child : item->childItems())
        if (publicationHeld(child))
            return true;
    return false;
}
} // namespace
EchoUiAnchorGroup::EchoUiAnchorGroup(QQmlEngine &engine, QQuickItem *source, QObject *parent)
    : QObject(parent), m_engine(engine), m_source(source), m_translation(this)
{
    m_translation.setTarget(source);
    const auto update = [this] { schedule(); };
    m_connections << connect(source, &QQuickItem::xChanged, this, update)
                  << connect(source, &QQuickItem::yChanged, this, update)
                  << connect(source, &QQuickItem::widthChanged, this, update)
                  << connect(source, &QQuickItem::heightChanged, this, update)
                  << connect(source, &QQuickItem::visibleChanged, this, update)
                  << connect(source, &QQuickItem::scaleChanged, this, update)
                  << connect(source, &QQuickItem::rotationChanged, this, update)
                  << connect(source, &QQuickItem::parentChanged, this, [this] {
                         watchParent(m_source ? m_source->parentItem() : nullptr);
                         schedule();
                     });
    watchParent(source->parentItem());
}
void EchoUiAnchorGroup::watchParent(QQuickItem *parent)
{
    if (m_parent != parent)
        reserve(0);
    for (const auto &connection : m_parentConnections)
        disconnect(connection);
    m_parentConnections.clear();
    m_parent = parent;
    if (parent) {
        m_parentConnections
            << connect(parent, &QQuickItem::widthChanged, this, &EchoUiAnchorGroup::schedule)
            << connect(parent, &QQuickItem::heightChanged, this, &EchoUiAnchorGroup::schedule)
            << connect(parent, &QQuickItem::visibleChanged, this, &EchoUiAnchorGroup::schedule)
            << connect(parent, &QQuickItem::childrenChanged, this, [this] {
                   watchNeighbours();
                   schedule();
               });
    }
    watchNeighbours();
}
void EchoUiAnchorGroup::watchNeighbours()
{
    for (const auto &connection : m_neighbourConnections)
        disconnect(connection);
    m_neighbourConnections.clear();
    if (!m_parent || m_released)
        return;
    for (auto *item : m_parent->childItems())
        if (item != m_source && item != m_flow && !item->property("_echoUiInternal").toBool())
            m_neighbourConnections
                << connect(item, &QQuickItem::xChanged, this, &EchoUiAnchorGroup::schedule)
                << connect(item, &QQuickItem::yChanged, this, &EchoUiAnchorGroup::schedule)
                << connect(item, &QQuickItem::widthChanged, this, &EchoUiAnchorGroup::schedule)
                << connect(item, &QQuickItem::heightChanged, this, &EchoUiAnchorGroup::schedule)
                << connect(item, &QQuickItem::visibleChanged, this, &EchoUiAnchorGroup::schedule);
}
EchoUiAnchorGroup::~EchoUiAnchorGroup()
{
    release();
}
void EchoUiAnchorGroup::release()
{
    if (std::exchange(m_released, true))
        return;
    for (const auto &connection :
         m_connections + m_itemConnections + m_parentConnections + m_neighbourConnections)
        disconnect(connection);
    reserve(0);
    m_translation.setTarget(nullptr);
    for (auto item : m_before + m_after)
        if (item && m_flow && item->parentItem() == m_flow)
            item->setParentItem(nullptr);
    if (m_flow) {
        m_flow->setVisible(false);
        m_flow->setParentItem(nullptr);
        m_flow->deleteLater();
    }
    m_flow = nullptr;
    m_slot = nullptr;
}
void EchoUiAnchorGroup::arrange(const QList<QQuickItem *> &before, const QList<QQuickItem *> &after,
                                const QString &axis, qreal gap)
{
    if (m_released || !m_source || !m_source->parentItem())
        return;
    const auto same = [](const QList<QPointer<QQuickItem>> &current,
                         const QList<QQuickItem *> &next) {
        if (current.size() != next.size())
            return false;
        for (qsizetype index = 0; index < current.size(); ++index)
            if (current[index] != next[index])
                return false;
        return true;
    };
    if (m_flow && m_axis == axis && m_gap == gap && same(m_before, before) && same(m_after, after))
        return;
    for (auto item : m_before + m_after)
        if (item && !before.contains(item) && !after.contains(item) &&
            item->parentItem() == m_flow && !publicationHeld(item))
            item->setParentItem(nullptr);
    if (m_axis != axis || !m_flow) {
        for (auto item : m_before + m_after)
            if (item && item->parentItem() == m_flow)
                item->setParentItem(m_source->parentItem());
        if (m_flow)
            delete m_flow.data();
        QQmlComponent component(&m_engine);
        const auto type = axis == "vertical" ? "ColumnLayout" : "RowLayout";
        component.setData(QString(R"(
import QtQuick
import QtQuick.Layouts
%1 {
    id: flow
    required property Item sourceTarget
    property alias sourceSlot: slot
    spacing: 0
    Item {
        id: slot
        implicitWidth: flow.sourceTarget ? flow.sourceTarget.width : 0
        implicitHeight: flow.sourceTarget ? flow.sourceTarget.height : 0
        Layout.minimumWidth: implicitWidth
        Layout.maximumWidth: implicitWidth
        Layout.minimumHeight: implicitHeight
        Layout.maximumHeight: implicitHeight
        visible: flow.sourceTarget && flow.sourceTarget.visible
    }
})")
                              .arg(type)
                              .toUtf8(),
                          QUrl("qrc:/echo-ui-anchor-flow.qml"));
        m_flow = qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"sourceTarget", QVariant::fromValue(m_source.data())}}));
        if (!m_flow)
            return;
        m_flow->setParent(this);
        m_flow->setProperty("_echoUiInternal", true);
        m_flow->setClip(true);
        EchoUiLayout::setDecoration(m_flow, true);
        m_slot = m_flow->property("sourceSlot").value<QQuickItem *>();
        if (m_slot)
            m_slot->setProperty("_echoUiInternal", true);
        m_axis = axis;
        connect(m_flow, &QQuickItem::implicitWidthChanged, this, &EchoUiAnchorGroup::schedule);
        connect(m_flow, &QQuickItem::implicitHeightChanged, this, &EchoUiAnchorGroup::schedule);
    }
    m_gap = gap;
    m_flow->setProperty("spacing", gap);
    for (const auto &connection : m_itemConnections)
        disconnect(connection);
    m_itemConnections.clear();
    m_before.clear();
    m_after.clear();
    for (auto *item : before)
        m_before.append(item);
    for (auto *item : after)
        m_after.append(item);
    auto place = [this](const QList<QQuickItem *> &items, QQuickItem *anchor, bool prepend) {
        auto *previous = anchor;
        for (auto *item : items) {
            item->setParentItem(m_flow);
            if (prepend)
                item->stackBefore(anchor);
            else {
                item->stackAfter(previous);
                previous = item;
            }
            m_itemConnections << connect(item, &QQuickItem::implicitWidthChanged, this,
                                         &EchoUiAnchorGroup::schedule)
                              << connect(item, &QQuickItem::implicitHeightChanged, this,
                                         &EchoUiAnchorGroup::schedule);
        }
    };
    place(before, m_slot, true);
    place(after, m_slot, false);
    schedule();
}
void EchoUiAnchorGroup::schedule()
{
    if (m_released || std::exchange(m_pending, true))
        return;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        layout();
    });
}
void EchoUiAnchorGroup::layout()
{
    if (m_released || m_updating || !m_source || !m_flow || !m_slot || !m_source->parentItem())
        return;
    m_updating = true;
    auto *parent = m_source->parentItem();
    m_flow->setParentItem(parent);
    m_flow->setVisible(parent->isVisible());
    const bool vertical = m_axis == "vertical";
    m_flow->ensurePolished();
    const auto sourceLength =
        m_source->isVisible() ? (vertical ? m_source->height() : m_source->width()) : 0;
    const auto wanted = vertical ? m_flow->implicitHeight() : m_flow->implicitWidth();
    reserve(std::max(qreal(0), wanted - sourceLength));
    parent->ensurePolished();
    // The free-form box is bounded by the parent and disjoint neighbouring boxes.
    // Paint/background children overlapping the source already are not siblings in its flow.
    const QRectF base(m_source->position(), m_source->size());
    qreal start = 0, end = vertical ? parent->height() : parent->width();
    for (auto *sibling : parent->childItems()) {
        if (sibling == m_source || sibling == m_flow || !sibling->isVisible() ||
            sibling->width() <= 0 || sibling->height() <= 0 ||
            sibling->property("_echoUiInternal").toBool())
            continue;
        const QRectF box(sibling->position(), sibling->size());
        if (box.intersects(base))
            continue;
        const bool crosses = vertical ? box.left() < base.right() && box.right() > base.left()
                                      : box.top() < base.bottom() && box.bottom() > base.top();
        if (!crosses)
            continue;
        const auto low = vertical ? box.top() : box.left();
        const auto high = vertical ? box.bottom() : box.right();
        const auto sourceLow = vertical ? base.top() : base.left();
        const auto sourceHigh = vertical ? base.bottom() : base.right();
        if (high <= sourceLow)
            start = std::max(start, m_peers.contains(sibling) ? (high + sourceLow + m_gap) / 2
                                                              : high + m_gap);
        if (low >= sourceHigh)
            end = std::min(end, m_peers.contains(sibling) ? (low + sourceHigh - m_gap) / 2
                                                          : low - m_gap);
    }
    m_flow->ensurePolished();
    const auto length = std::max(qreal(0), std::min(std::max(sourceLength, wanted), end - start));
    const auto parentCross = vertical ? parent->width() : parent->height();
    const auto cross =
        std::max(qreal(0), std::min(parentCross,
                                    vertical ? std::max(base.width(), m_flow->implicitWidth())
                                             : std::max(base.height(), m_flow->implicitHeight())));
    const auto crossOrigin =
        std::clamp((vertical ? base.center().x() : base.center().y()) - cross / 2, qreal(0),
                   std::max(qreal(0), parentCross - cross));
    const auto flags = QQuickItemPrivate::get(m_source)->anchors()->usedAnchors();
    const auto low = vertical ? base.top() : base.left();
    const auto high = vertical ? base.bottom() : base.right();
    const auto parentLength = vertical ? parent->height() : parent->width();
    const bool trailing =
        flags.testFlag(vertical ? QQuickAnchors::BottomAnchor : QQuickAnchors::RightAnchor) ||
        (high > parentLength / 2 && parentLength - high < sourceLength);
    const bool centered =
        flags.testFlag(vertical ? QQuickAnchors::VCenterAnchor : QQuickAnchors::HCenterAnchor);
    const auto requestedOrigin = trailing   ? high - length
                                 : centered ? (low + high - length) / 2
                                            : low;
    const auto origin = std::clamp(requestedOrigin, start, std::max(start, end - length));
    if (vertical) {
        m_flow->setSize({cross, length});
        m_flow->setPosition({crossOrigin, origin});
    } else {
        m_flow->setSize({length, cross});
        m_flow->setPosition({origin, crossOrigin});
    }
    m_flow->ensurePolished();
    m_capacityError.clear();
    const QRectF viewport(QPointF(), m_flow->size());
    for (auto item : m_before + m_after) {
        if (!item || !item->isVisible())
            continue;
        const QRectF box(item->position(), item->size());
        // Empty cells have not completed natural-size measurement yet. They
        // consume no capacity and must remain presented so their factory can run.
        if (!box.isEmpty() && !viewport.adjusted(-0.5, -0.5, 0.5, 0.5).contains(box)) {
            m_capacityError = QString("Anchored group cannot satisfy its minimum sizes: item "
                                      "(%1,%2 %3x%4), available %5x%6, wanted %7, source %8x%9")
                                  .arg(box.x())
                                  .arg(box.y())
                                  .arg(box.width())
                                  .arg(box.height())
                                  .arg(viewport.width())
                                  .arg(viewport.height())
                                  .arg(wanted)
                                  .arg(base.width())
                                  .arg(base.height());
            break;
        }
    }
    if (m_slot->isVisible() && !viewport.adjusted(-0.5, -0.5, 0.5, 0.5)
                                    .contains(QRectF(m_slot->position(), m_slot->size())))
        m_capacityError = "Anchored group cannot preserve its source in the available area";
    if (!m_capacityError.isEmpty()) {
        // A transaction has not released the old presentations yet. Do not
        // transiently hide their native focus/IME owners while testing capacity.
        if (!publicationHeld(m_flow))
            m_flow->setVisible(false);
        m_translation.setX(0);
        m_translation.setY(0);
        m_updating = false;
        emit arranged();
        return;
    }
    // Solve the native transform in the source's coordinate system, retaining
    // its original scale/rotation/transform entries and all anchor bindings.
    m_translation.setX(0);
    m_translation.setY(0);
    const auto zero = m_source->mapToItem(parent, QPointF());
    m_translation.setX(1);
    const auto x = m_source->mapToItem(parent, QPointF()) - zero;
    m_translation.setX(0);
    m_translation.setY(1);
    const auto y = m_source->mapToItem(parent, QPointF()) - zero;
    m_translation.setY(0);
    const QTransform basis(x.x(), x.y(), y.x(), y.y(), 0, 0);
    bool invertible = false;
    const auto inverse = basis.inverted(&invertible);
    const auto destination = m_slot->mapToItem(parent, QPointF());
    const auto delta = invertible ? inverse.map(destination - zero) : QPointF();
    m_translation.setX(delta.x());
    m_translation.setY(delta.y());
    m_updating = false;
    emit arranged();
}
void EchoUiAnchorGroup::reserve(qreal length)
{
    auto *parent = m_source ? m_source->parentItem() : nullptr;
    if (length > 0 && parent &&
        (parent->inherits("QQuickTextField") || parent->inherits("QQuickTextArea"))) {
        if (!m_padding) {
            m_padding =
                parent->findChild<QObject *>("_echoUiAnchorPadding", Qt::FindDirectChildrenOnly);
            if (!m_padding) {
                QVariantMap bases;
                for (const auto &side : {"left", "right", "top", "bottom"})
                    bases[side] = parent->property((QString(side) + "Padding").toUtf8());
                QQmlComponent component(
                    &m_engine, QUrl("qrc:/qt/qml/EchoMusic/Controls/EchoUiAnchorPadding.qml"));
                m_padding = component.createWithInitialProperties(
                    {{"control", QVariant::fromValue<QObject *>(parent)}, {"bases", bases}});
                if (m_padding) {
                    m_padding->setParent(parent);
                    m_padding->setObjectName("_echoUiAnchorPadding");
                }
            }
        }
    }
    if (!m_padding)
        return;
    const auto token = QString::number(quintptr(this), 16);
    auto leases = m_padding->property("_leases").toMap();
    if (length <= 0 || !parent)
        leases.remove(token);
    else {
        const bool vertical = m_axis == "vertical";
        const bool end = vertical ? m_source->y() + m_source->height() / 2 > parent->height() / 2
                                  : m_source->x() + m_source->width() / 2 > parent->width() / 2;
        const auto side = vertical ? (end ? "bottom" : "top") : (end ? "right" : "left");
        leases[token] = QVariantMap{{"side", side}, {"length", length}};
    }
    QVariantMap extra;
    for (const auto &lease : leases) {
        const auto value = lease.toMap();
        const auto side = value.value("side").toString();
        extra[side] = extra.value(side, 0).toDouble() + value.value("length").toDouble();
    }
    m_padding->setProperty("_leases", leases);
    if (m_padding->property("extra").toMap() != extra)
        m_padding->setProperty("extra", extra);
    if (leases.isEmpty()) {
        delete m_padding.data();
        m_padding = nullptr;
    }
}
} // namespace EchoMusic
