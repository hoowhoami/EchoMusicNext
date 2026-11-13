#include "EchoInputFocus.h"
#include <QMouseEvent>
#include <QTouchEvent>

namespace EchoMusic
{
EchoInputFocusAttached::EchoInputFocusAttached(QObject *parent)
    : QObject(parent), m_editor(qobject_cast<QQuickItem *>(parent))
{
    if (!m_editor)
        return;
    connect(m_editor, &QQuickItem::activeFocusChanged, this, &EchoInputFocusAttached::syncWindow);
    connect(m_editor, &QQuickItem::windowChanged, this, &EchoInputFocusAttached::syncWindow);
    connect(m_editor, &QQuickItem::enabledChanged, this, &EchoInputFocusAttached::syncWindow);
    connect(m_editor, &QQuickItem::visibleChanged, this, &EchoInputFocusAttached::syncWindow);
    syncWindow();
}
EchoInputFocusAttached::~EchoInputFocusAttached()
{
    if (m_window)
        m_window->removeEventFilter(this);
}
void EchoInputFocusAttached::syncWindow()
{
    auto *window =
        m_editor && m_editor->hasActiveFocus() && m_editor->isEnabled() && m_editor->isVisible()
            ? m_editor->window()
            : nullptr;
    if (window == m_window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
}
bool EchoInputFocusAttached::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || !m_editor)
        return false;
    QPointF point;
    if (event->type() == QEvent::MouseButtonPress) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        // Right-click keeps the editing context menu and selection.
        if (mouse->button() != Qt::LeftButton)
            return false;
        point = mouse->position();
    } else if (event->type() == QEvent::TouchBegin) {
        const auto *touch = static_cast<QTouchEvent *>(event);
        if (touch->points().isEmpty())
            return false;
        point = touch->points().first().position();
    } else {
        return false;
    }
    const auto contains = [&](QQuickItem *item) {
        return item && item->window() == m_window && item->isVisible() &&
               item->contains(item->mapFromScene(point));
    };
    // Popup.Item (including Wayland) delivers menu presses to this window.
    // Keep numeric drafts and selection until the chosen Qt edit action runs.
    if (contains(m_boundary ? m_boundary.data() : m_editor.data()) || contains(m_extension) ||
        contains(m_menuContent))
        return false;
    // Focus loss can commit a draft and retire its editor. Keep no borrowed
    // pointer across that callback. Spin boxes park focus before steppers act.
    const QPointer<QQuickItem> editor = m_editor;
    emit outsidePressed();
    if (editor && editor->hasActiveFocus())
        editor->setFocus(false, Qt::MouseFocusReason);
    return false;
}
} // namespace EchoMusic
