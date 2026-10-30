#pragma once
#include <QQuickItem>
#include <QQuickWindow>
#include <functional>
namespace EchoMusic
{
// Observe public Qt item geometry/visibility only. The visual owner destroys
// this observer before its derived members, including self-signal callbacks.
class ItemPresentationObserver final : public QObject
{
public:
    ItemPresentationObserver(QQuickItem &item, std::function<void()> changed)
        : QObject(&item), m_item(item), m_changed(std::move(changed))
    {
        rewire();
    }
    static bool presented(const QQuickItem &item)
    {
        auto rect = item.mapRectToScene(item.boundingRect());
        for (auto *ancestor = &item; ancestor; ancestor = ancestor->parentItem()) {
            if (!ancestor->isVisible() || ancestor->opacity() <= 0)
                return false;
            if (ancestor->clip())
                rect = rect.intersected(ancestor->mapRectToScene(ancestor->boundingRect()));
        }
        if (item.window()) {
            if (item.window()->opacity() <= 0)
                return false;
            rect = rect.intersected(QRectF(QPointF(), item.window()->size()));
        }
        return !rect.isEmpty();
    }

private:
    void rewire()
    {
        delete m_connections;
        m_connections = new QObject(this);
        for (auto *item = &m_item; item; item = item->parentItem()) {
            connect(item, &QQuickItem::parentChanged, m_connections, [this] {
                rewire();
                const auto changed = m_changed;
                changed();
            });
            connect(item, &QQuickItem::visibleChanged, m_connections, m_changed);
            connect(item, &QQuickItem::opacityChanged, m_connections, m_changed);
            connect(item, &QQuickItem::xChanged, m_connections, m_changed);
            connect(item, &QQuickItem::yChanged, m_connections, m_changed);
            connect(item, &QQuickItem::widthChanged, m_connections, m_changed);
            connect(item, &QQuickItem::heightChanged, m_connections, m_changed);
            connect(item, &QQuickItem::clipChanged, m_connections, m_changed);
            connect(item, &QQuickItem::scaleChanged, m_connections, m_changed);
            connect(item, &QQuickItem::rotationChanged, m_connections, m_changed);
        }
    }
    QQuickItem &m_item;
    std::function<void()> m_changed;
    QObject *m_connections = nullptr;
};
} // namespace EchoMusic
