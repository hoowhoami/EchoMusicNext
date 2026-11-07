#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
namespace EchoMusic
{
// Native focus return for a disposable contribution. Never steals focus from
// a different popup/window and never exposes a host item to the author.
class EchoUiFocusLease final : public QObject
{
    Q_OBJECT
public:
    EchoUiFocusLease(QQuickItem *view, QQuickItem *fallback, QObject *owner);
    void restore();

private:
    void bindWindow();
    bool contains(QQuickItem *) const;
    QPointer<QQuickItem> m_view, m_fallback, m_previous, m_last;
    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_focus;
    bool m_ownedFocus = false;
};
} // namespace EchoMusic
