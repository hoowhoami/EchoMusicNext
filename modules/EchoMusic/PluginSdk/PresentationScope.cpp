#include "PresentationScope.h"
#include <QPolygonF>
#include <QQuickWindow>
#include <cmath>
#include <utility>

namespace EchoMusic
{
namespace
{
QPolygonF scenePolygon(const QQuickItem &item)
{
    auto result = QPolygonF(item.boundingRect());
    for (auto &point : result)
        point = item.mapToScene(point);
    return result;
}
} // namespace
PresentationScope::PresentationScope(QQuickItem *parent) : QQuickItem(parent)
{
    observe();
    refresh();
}
void PresentationScope::observe()
{
    if (m_observing)
        return;
    m_observing = true;
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    const auto changed = [this] { refresh(); };
    const auto reparented = [this] {
        observe();
        refresh();
    };
    for (QQuickItem *item = this; item; item = item->parentItem()) {
        m_connections.push_back(connect(item, &QQuickItem::parentChanged, this, reparented));
        m_connections.push_back(connect(item, &QQuickItem::windowChanged, this, reparented));
        m_connections.push_back(connect(item, &QQuickItem::visibleChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::opacityChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::xChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::yChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::widthChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::heightChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::clipChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::scaleChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::rotationChanged, this, changed));
        m_connections.push_back(connect(item, &QQuickItem::transformOriginChanged, this, changed));
    }
    if (auto *surface = window()) {
        m_connections.push_back(connect(surface, &QWindow::visibilityChanged, this, changed));
        m_connections.push_back(connect(surface, &QWindow::widthChanged, this, changed));
        m_connections.push_back(connect(surface, &QWindow::heightChanged, this, changed));
    }
    m_observing = false;
}
void PresentationScope::refresh()
{
    bool next = false;
    if (auto *surface = window(); surface && surface->isVisible() &&
                                  surface->visibility() != QWindow::Minimized && width() > 0 &&
                                  height() > 0) {
        QPolygonF area = scenePolygon(*this);
        for (QQuickItem *item = this; item && !area.isEmpty(); item = item->parentItem()) {
            if (!item->isVisible() || item->opacity() <= 0 || item->scale() == 0) {
                area.clear();
                break;
            }
            if (item->clip())
                area = area.intersected(scenePolygon(*item));
        }
        area = area.intersected(QPolygonF(QRectF(QPointF(), surface->size())));
        // Polygon clipping may leave an edge with no physical area.
        double twiceArea = 0;
        for (qsizetype i = 0; i < area.size(); ++i) {
            const auto &a = area[i];
            const auto &b = area[(i + 1) % area.size()];
            twiceArea += a.x() * b.y() - b.x() * a.y();
        }
        next = std::isfinite(twiceArea) && std::abs(twiceArea) > 0;
    }
    if (std::exchange(m_presented, next) != next)
        emit presentedChanged();
}
} // namespace EchoMusic
