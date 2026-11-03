#pragma once
#include <QPointer>
#include <QPointF>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace EchoMusic
{
// Tracked scene position and device pixel ratio of the attached item. Qt
// Quick exposes neither as a notified property (QML mapToItem is not a tracked
// binding), which is what kept device-pixel alignment a per-element patch.
// Attach it to the OUTER item and bind an INNER surface's x/y/width/height
// from it; writing back to the same item's geometry would feed back.
class EchoPixelSnapAttached final : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QPointF scenePosition READ scenePosition NOTIFY scenePositionChanged)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY devicePixelRatioChanged)
public:
    explicit EchoPixelSnapAttached(QObject *parent);
    static bool snapEnabled();
    QPointF scenePosition() const
    {
        return snapEnabled() ? m_scenePosition : QPointF();
    }
    qreal devicePixelRatio() const
    {
        return snapEnabled() ? m_devicePixelRatio : 1.0;
    }
    void refresh();
signals:
    void scenePositionChanged();
    void devicePixelRatioChanged();
private:
    void watchChain();
    void disconnectChain();
    QPointer<QQuickItem> m_item;
    QPointF m_scenePosition;
    qreal m_devicePixelRatio = 1.0;
    std::vector<QMetaObject::Connection> m_connections;
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
