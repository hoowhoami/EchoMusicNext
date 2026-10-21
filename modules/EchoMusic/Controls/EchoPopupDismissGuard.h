#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Managed dialogs need an outside notification before deciding whether to
/// discard a draft. Overlay.pressed has no point and fires for inside controls;
/// modal overlay filtering can prevent a QML passive handler seeing outside.
/// Native popup owners can opt into dismissOnWindowPress to consume the host's
/// press before Overlay dispatch. That synchronous handler must only close the
/// existing surface; confirmation dialogs keep the default queued dimmer path.
class EchoPopupDismissGuard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *overlay READ overlay WRITE setOverlay NOTIFY changed)
    Q_PROPERTY(QQuickItem *dimmer READ dimmer WRITE setDimmer NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool dismissOnWindowPress READ dismissOnWindowPress WRITE setDismissOnWindowPress
                   NOTIFY changed)
public:
    explicit EchoPopupDismissGuard(QObject *parent = nullptr) : QObject(parent) {}
    ~EchoPopupDismissGuard() override;
    QQuickItem *overlay() const
    {
        return m_overlay;
    }
    QQuickItem *dimmer() const
    {
        return m_dimmer;
    }
    bool enabled() const
    {
        return m_enabled;
    }
    void setOverlay(QQuickItem *item);
    void setDimmer(QQuickItem *item);
    void setEnabled(bool enabled);
    bool dismissOnWindowPress() const
    {
        return m_dismissOnWindowPress;
    }
    void setDismissOnWindowPress(bool value);
signals:
    void changed();
    void outsidePressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void attachWindow();
    void syncApplicationFilter();
    QPointer<QQuickItem> m_overlay, m_dimmer;
    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_windowChanged;
    quint64 m_generation = 0;
    bool m_enabled = false;
    bool m_dismissOnWindowPress = false;
};
} // namespace EchoMusic
