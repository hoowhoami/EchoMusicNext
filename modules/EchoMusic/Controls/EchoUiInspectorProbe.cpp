#include "EchoUiInspectorProbe.h"
#include "EchoUiLayout.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiPresentation.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QPolygonF>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <algorithm>
#include <cmath>
#include <QScopedValueRollback>
namespace EchoMusic
{
namespace
{
class Outline final : public QQuickItem
{
public:
    Outline(QQuickItem *parent) : QQuickItem(parent)
    {
        setFlag(ItemHasContents, true);
        setProperty("_echoUiInternal", true);
        EchoUiLayout::setDecoration(this, true);
        setZ(1e9);
    }
    bool contains(const QPointF &) const override
    {
        return false;
    }
    QPolygonF points;
    QColor color;

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override
    {
        auto *node = static_cast<QSGGeometryNode *>(old);
        if (!node) {
            node = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 24);
            geometry->setDrawingMode(QSGGeometry::DrawTriangles);
            node->setGeometry(geometry);
            node->setFlag(QSGNode::OwnsGeometry);
            node->setMaterial(new QSGFlatColorMaterial);
            node->setFlag(QSGNode::OwnsMaterial);
        }
        auto *vertices = node->geometry()->vertexDataAsPoint2D();
        for (int i = 0; i < 4; ++i) {
            const auto a = points.value(i), b = points.value((i + 1) % 4);
            const auto delta = b - a;
            const auto length = std::hypot(delta.x(), delta.y());
            const QPointF offset = length > 0 ? QPointF(-delta.y(), delta.x()) / length : QPointF();
            const QPointF quad[] = {a - offset, b - offset, b + offset, a + offset};
            const int order[] = {0, 1, 2, 0, 2, 3};
            for (int j = 0; j < 6; ++j)
                vertices[i * 6 + j].set(quad[order[j]].x(), quad[order[j]].y());
        }
        static_cast<QSGFlatColorMaterial *>(node->material())->setColor(color);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return node;
    }
};
QList<QQuickItem *> path(QQuickItem *item)
{
    QList<QQuickItem *> result;
    for (; item; item = item->parentItem())
        result.prepend(item);
    return result;
}
bool above(QQuickItem *a, QQuickItem *b)
{
    const auto left = path(a), right = path(b);
    for (qsizetype i = 0; i < qMin(left.size(), right.size()); ++i) {
        if (left[i] == right[i])
            continue;
        if (left[i]->z() != right[i]->z())
            return left[i]->z() > right[i]->z();
        if (i == 0)
            return false;
        const auto children = left[i - 1]->childItems();
        return children.indexOf(left[i]) > children.indexOf(right[i]);
    }
    return left.size() > right.size();
}
} // namespace
EchoUiInspectorProbe::EchoUiInspectorProbe(QObject *parent) : QObject(parent) {}
EchoUiInspectorProbe::~EchoUiInspectorProbe()
{
    setActive(false);
    clearHighlight();
}
EchoUiPartRegistry *EchoUiInspectorProbe::registry() const
{
    auto *engine = qmlEngine(this);
    return engine ? EchoUiPartRegistry::forEngine(*engine) : nullptr;
}
void EchoUiInspectorProbe::setActive(bool value)
{
    if (m_active == value)
        return;
    m_active = value;
    if (m_active) {
        if (auto *owner = registry())
            owner->acquireStructure(this, true);
        qGuiApp->installEventFilter(this);
    } else {
        qGuiApp->removeEventFilter(this);
        if (m_selectedId.isEmpty())
            if (auto *owner = registry())
                owner->releaseStructure(this);
    }
    emit changed();
}
void EchoUiInspectorProbe::clearHighlight()
{
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    delete m_observer;
    m_observer = nullptr;
    delete m_overlay;
    m_overlay = nullptr;
    if (m_part)
        m_part->releaseState();
    m_part = nullptr;
}
void EchoUiInspectorProbe::setSelectedId(const QString &identity)
{
    if (m_selectedId == identity)
        return;
    clearHighlight();
    m_selectedId = identity;
    auto *owner = registry();
    if (owner) {
        if (!identity.isEmpty())
            owner->acquireStructure(this, true);
        else if (!m_active)
            owner->releaseStructure(this);
    }
    m_part = owner ? owner->find(identity) : nullptr;
    if (m_part && m_part->target()) {
        m_part->acquireState();
        m_observer = new EchoUiPresentation(m_part->target(), this);
        m_connections << connect(owner, &EchoUiPartRegistry::removed, this,
                                 [this](const QString &id) {
                                     if (id == m_selectedId)
                                         setSelectedId({});
                                 })
                      << connect(m_observer, &EchoUiPresentation::changed, this,
                                 &EchoUiInspectorProbe::highlight)
                      << connect(m_part, &EchoUiPartRegistration::changed, this,
                                 &EchoUiInspectorProbe::highlight)
                      << connect(m_part, &QObject::destroyed, this, [this] { setSelectedId({}); });
        highlight();
    }
    emit changed();
}
void EchoUiInspectorProbe::setColor(const QColor &value)
{
    if (m_color != value) {
        m_color = value;
        highlight();
        emit changed();
    }
}
void EchoUiInspectorProbe::highlight()
{
    if (m_highlighting)
        return;
    QScopedValueRollback guard(m_highlighting, true);
    auto *item = m_part ? m_part->target() : nullptr;
    auto *window = item ? item->window() : nullptr;
    if (!item || !window || !m_part->presented()) {
        if (m_overlay)
            m_overlay->setVisible(false);
        return;
    }
    if (m_overlay && m_overlay->window() != window) {
        delete m_overlay;
        m_overlay = nullptr;
    }
    if (!m_overlay)
        m_overlay = new Outline(window->contentItem());
    auto *outline = static_cast<Outline *>(m_overlay.data());
    outline->setSize(window->size());
    outline->points.clear();
    for (const auto &corner : {QPointF(), QPointF(item->width(), 0),
                               QPointF(item->width(), item->height()), QPointF(0, item->height())})
        outline->points.append(item->mapToScene(corner));
    outline->color = m_color;
    outline->setVisible(true);
    outline->update();
}
QString EchoUiInspectorProbe::at(QQuickWindow *window, const QPointF &position) const
{
    auto *owner = registry();
    if (!owner)
        return {};
    EchoUiPartRegistration *best = nullptr;
    for (auto *part : owner->query({})) {
        auto *item = part->target();
        if (!item || item->window() != window || part->surface() == "inspector" ||
            !part->presented() || !item->contains(item->mapFromScene(position)))
            continue;
        bool clipped = false;
        for (auto *parent = item->parentItem(); parent; parent = parent->parentItem())
            if (parent->clip() && !parent->contains(parent->mapFromScene(position))) {
                clipped = true;
                break;
            }
        if (!clipped && (!best || above(item, best->target())))
            best = part;
    }
    return best ? best->identity() : QString();
}
bool EchoUiInspectorProbe::eventFilter(QObject *object, QEvent *event)
{
    if (!m_active)
        return false;
    if (event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        setActive(false);
        return true;
    }
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window ||
        (event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonPress))
        return false;
    auto *pointer = static_cast<QMouseEvent *>(event);
    if (event->type() == QEvent::MouseButtonPress && pointer->button() != Qt::LeftButton)
        return false;
    setSelectedId(at(window, pointer->position()));
    if (event->type() == QEvent::MouseButtonPress && !m_selectedId.isEmpty()) {
        setActive(false);
        emit picked(m_selectedId);
    }
    return true;
}
} // namespace EchoMusic
