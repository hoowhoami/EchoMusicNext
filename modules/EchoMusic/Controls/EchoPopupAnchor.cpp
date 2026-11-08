#include "EchoPopupAnchor.h"
#include "EchoItemObserver.h"
#include <QSet>
#include <QQuickWindow>
#include <QScreen>
namespace EchoMusic
{
void EchoPopupAnchor::setItem(QQuickItem *item)
{
    if (m_item == item)
        return;
    m_item = item;
    rebuild();
    emit itemChanged();
}
void EchoPopupAnchor::setCoordinateItem(QQuickItem *item)
{
    if (m_coordinateItem == item)
        return;
    m_coordinateItem = item;
    rebuild();
    emit coordinateItemChanged();
}
bool EchoPopupAnchor::available() const
{
    return m_available;
}
QRectF EchoPopupAnchor::rect() const
{
    return m_rect;
}
void EchoPopupAnchor::updateGeometry()
{
    const bool available = m_item && m_coordinateItem && m_item->window() &&
                           m_item->window() == m_coordinateItem->window() && m_item->isVisible();
    const auto rect =
        available ? m_item->mapRectToItem(m_coordinateItem, m_item->boundingRect()) : QRectF();
    QRectF screenRect;
    if (available && m_item->window()->screen()) {
        const auto area = m_item->window()->screen()->availableGeometry();
        screenRect = QRectF(
            m_coordinateItem->mapFromGlobal(area.topLeft()),
            m_coordinateItem->mapFromGlobal(area.topLeft() + QPoint(area.width(), area.height())));
    }
    // Showing a Popup toggles Overlay visibility without moving the trigger.
    // Notify only after a real change, using a stable snapshot, so dependent
    // popup visibility/size bindings cannot re-enter during their own opening.
    if (m_available == available && m_rect == rect && m_screenRect == screenRect)
        return;
    m_available = available;
    m_rect = rect;
    m_screenRect = screenRect;
    emit geometryChanged();
}
void EchoPopupAnchor::rebuild()
{
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    m_sceneObserver = observeItem(m_item);
    m_coordinateObserver = observeItem(m_coordinateItem);
    for (const auto &observer : {m_sceneObserver, m_coordinateObserver})
        if (observer)
            m_connections << connect(observer.data(), &EchoItemObserver::sceneChanged, this,
                                     &EchoPopupAnchor::updateGeometry);
    QSet<QQuickItem *> seen;
    for (auto *root : {m_item.data(), m_coordinateItem.data()}) {
        for (auto *item = root; item && !seen.contains(item); item = item->parentItem()) {
            seen.insert(item);
            const auto changed = [this] { updateGeometry(); };
            m_connections << connect(item, &QQuickItem::xChanged, this, changed)
                          << connect(item, &QQuickItem::yChanged, this, changed)
                          << connect(item, &QQuickItem::widthChanged, this, changed)
                          << connect(item, &QQuickItem::heightChanged, this, changed)
                          << connect(item, &QQuickItem::scaleChanged, this, changed)
                          << connect(item, &QQuickItem::rotationChanged, this, changed)
                          << connect(item, &QQuickItem::transformOriginChanged, this, changed)
                          << connect(item, &QQuickItem::visibleChanged, this, changed)
                          << connect(item, &QQuickItem::windowChanged, this,
                                     &EchoPopupAnchor::rebuild)
                          << connect(item, &QQuickItem::parentChanged, this,
                                     &EchoPopupAnchor::rebuild)
                          << connect(item, &QObject::destroyed, this, &EchoPopupAnchor::rebuild);
        }
    }
    if (m_item && m_item->window()) {
        auto *window = m_item->window();
        m_connections << connect(window, &QWindow::xChanged, this, &EchoPopupAnchor::updateGeometry)
                      << connect(window, &QWindow::yChanged, this, &EchoPopupAnchor::updateGeometry)
                      << connect(window, &QWindow::screenChanged, this, &EchoPopupAnchor::rebuild);
        if (window->screen())
            m_connections << connect(window->screen(), &QScreen::availableGeometryChanged, this,
                                     &EchoPopupAnchor::updateGeometry);
    }
    updateGeometry();
}
} // namespace EchoMusic
