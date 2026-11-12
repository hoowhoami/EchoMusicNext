#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
// Own only the local item's cursor. No pointer events, grabs or tree discovery.
class EchoActionCursor : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(
        Qt::CursorShape cursorShape READ cursorShape WRITE setCursorShape NOTIFY cursorShapeChanged)
public:
    explicit EchoActionCursor(QObject *parent = nullptr) : QObject(parent) {}
    ~EchoActionCursor() override;
    void classBegin() override {}
    void componentComplete() override;
    QQuickItem *target() const
    {
        return m_target;
    }
    void setTarget(QQuickItem *item);
    bool enabled() const
    {
        return m_enabled;
    }
    void setEnabled(bool enabled);
    Qt::CursorShape cursorShape() const
    {
        return m_shape;
    }
    void setCursorShape(Qt::CursorShape shape);
signals:
    void targetChanged();
    void enabledChanged();
    void cursorShapeChanged();

private:
    void updateCursor();
    QPointer<QQuickItem> m_target;
    QMetaObject::Connection m_enabledConnection;
    Qt::CursorShape m_shape = Qt::PointingHandCursor;
    bool m_enabled = true;
};
} // namespace EchoMusic
