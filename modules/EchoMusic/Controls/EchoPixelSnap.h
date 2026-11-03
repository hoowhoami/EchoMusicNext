#pragma once
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QSharedPointer>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class EchoItemObserver;
// Tracked scene position and device-pixel density of the attached item. Qt
// Quick exposes neither as a notified property (QML mapToItem is not a tracked
// binding), which is what kept device-pixel alignment a per-element patch.
// Attach it to the OUTER item and bind an INNER surface's x/y/width/height
// from it; writing back to the same item's geometry would feed back. Density
// includes the ancestor scale chain, so the compensation stays in the item's
// local coordinates when the application UI scale is not 100%.
class EchoPixelSnapAttached final : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QPointF scenePosition READ scenePosition NOTIFY scenePositionChanged)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY devicePixelRatioChanged)
    Q_PROPERTY(qreal densityX READ densityX NOTIFY densityChanged)
    Q_PROPERTY(qreal densityY READ densityY NOTIFY densityChanged)
    Q_PROPERTY(bool enabled READ enabled CONSTANT)
public:
    explicit EchoPixelSnapAttached(QObject *parent);
    static bool snapEnabled();
    QPointF scenePosition() const
    {
        return m_scenePosition;
    }
    qreal devicePixelRatio() const
    {
        return m_devicePixelRatio;
    }
    qreal densityX() const
    {
        return m_densityX;
    }
    qreal densityY() const
    {
        return m_densityY;
    }
    bool enabled() const
    {
        return snapEnabled();
    }
    void refresh();
signals:
    void scenePositionChanged();
    void devicePixelRatioChanged();
    void densityChanged();

private:
    QPointer<QQuickItem> m_item;
    QSharedPointer<EchoItemObserver> m_observer;
    QPointF m_scenePosition;
    qreal m_devicePixelRatio = 1.0;
    qreal m_densityX = 1.0;
    qreal m_densityY = 1.0;
};

class EchoPixelSnap final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Read the EchoPixelSnap attached scene position")
    QML_ATTACHED(EchoPixelSnapAttached)
public:
    static EchoPixelSnapAttached *qmlAttachedProperties(QObject *object)
    {
        return new EchoPixelSnapAttached(object);
    }
};
} // namespace EchoMusic
QML_DECLARE_TYPEINFO(EchoMusic::EchoPixelSnap, QML_HAS_ATTACHED_PROPERTIES)
