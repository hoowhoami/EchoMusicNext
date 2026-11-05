#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
// A borrowed presentation target keeps its own geometry and other transforms.
// QQuickTransform's public append API attaches this one extra native transform;
// retiring/changing the target removes only our entry from its transform list.
class EchoItemTranslation : public QQuickTransform
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(qreal x READ x WRITE setX NOTIFY xChanged)
    Q_PROPERTY(qreal y READ y WRITE setY NOTIFY yChanged)
public:
    explicit EchoItemTranslation(QObject *parent = nullptr);
    ~EchoItemTranslation() override;
    QQuickItem *target() const
    {
        return m_target;
    }
    qreal x() const
    {
        return m_x;
    }
    qreal y() const
    {
        return m_y;
    }
    void setTarget(QQuickItem *target);
    void setX(qreal x);
    void setY(qreal y);
    void applyTo(QMatrix4x4 *matrix) const override;
signals:
    void targetChanged();
    void xChanged();
    void yChanged();

private:
    void detach();
    QPointer<QQuickItem> m_target;
    qreal m_x = 0, m_y = 0;
};
} // namespace EchoMusic
