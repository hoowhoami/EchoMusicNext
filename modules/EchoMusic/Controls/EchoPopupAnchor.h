#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QSharedPointer>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoItemObserver;
/// Tracks the complete ancestor transform chain, including animated sidebar
/// width/zoom and reparenting. Popups stay anchored to the real trigger.
class EchoPopupAnchor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *item READ item WRITE setItem NOTIFY itemChanged)
    Q_PROPERTY(QQuickItem *coordinateItem READ coordinateItem WRITE setCoordinateItem NOTIFY
                   coordinateItemChanged)
    Q_PROPERTY(QRectF rect READ rect NOTIFY geometryChanged)
    Q_PROPERTY(QRectF screenRect READ screenRect NOTIFY geometryChanged)
    Q_PROPERTY(bool available READ available NOTIFY geometryChanged)
public:
    explicit EchoPopupAnchor(QObject *parent = nullptr) : QObject(parent) {}
    QQuickItem *item() const
    {
        return m_item;
    }
    QQuickItem *coordinateItem() const
    {
        return m_coordinateItem;
    }
    void setItem(QQuickItem *item);
    void setCoordinateItem(QQuickItem *item);
    QRectF rect() const;
    QRectF screenRect() const
    {
        return m_screenRect;
    }
    bool available() const;
signals:
    void itemChanged();
    void coordinateItemChanged();
    void geometryChanged();

private:
    void rebuild();
    void updateGeometry();
    QPointer<QQuickItem> m_item, m_coordinateItem;
    QSharedPointer<EchoItemObserver> m_sceneObserver, m_coordinateObserver;
    QList<QMetaObject::Connection> m_connections;
    QRectF m_rect, m_screenRect;
    bool m_available = false;
};
} // namespace EchoMusic
