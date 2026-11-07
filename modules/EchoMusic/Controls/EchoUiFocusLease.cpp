#include "EchoUiFocusLease.h"
#include "EchoUiPresentation.h"
namespace EchoMusic
{
EchoUiFocusLease::EchoUiFocusLease(QQuickItem *view, QQuickItem *fallback, QObject *owner)
    : QObject(owner), m_view(view), m_fallback(fallback)
{
    connect(view, &QQuickItem::windowChanged, this, &EchoUiFocusLease::bindWindow);
    connect(view, &QQuickItem::visibleChanged, this, [this] {
        if (m_view && !m_view->isVisible())
            restore();
    });
    bindWindow();
}
bool EchoUiFocusLease::contains(QQuickItem *item) const
{
    for (; item; item = item->parentItem())
        if (item == m_view)
            return true;
    return false;
}
void EchoUiFocusLease::bindWindow()
{
    disconnect(m_focus);
    m_window = m_view ? m_view->window() : nullptr;
    if (!m_window)
        return;
    m_last = m_window->activeFocusItem();
    m_focus = connect(m_window, &QQuickWindow::activeFocusItemChanged, this, [this] {
        const QPointer<QQuickItem> current = m_window ? m_window->activeFocusItem() : nullptr;
        if (contains(current) && !contains(m_last) && m_last && m_last != m_window->contentItem())
            m_previous = m_last;
        if (contains(current))
            m_ownedFocus = true;
        else if (current && current != m_window->contentItem())
            m_ownedFocus = false;
        m_last = current;
    });
}
void EchoUiFocusLease::restore()
{
    if (!m_window)
        return;
    auto *current = m_window->activeFocusItem();
    if (!contains(current) && !(m_ownedFocus && (!current || current == m_window->contentItem())))
        return;
    auto target = m_previous;
    if (!target || target->window() != m_window || !target->isEnabled() ||
        !EchoUiPresentation::presented(target))
        target = m_fallback;
    if (!target || target->window() != m_window || !target->isEnabled() ||
        !EchoUiPresentation::presented(target))
        return;
    target->forceActiveFocus(Qt::OtherFocusReason);
}
} // namespace EchoMusic
