#include "EchoActionCursor.h"
#include <QCursor>

namespace EchoMusic
{
EchoActionCursor::~EchoActionCursor()
{
    if (m_target)
        m_target->unsetCursor();
}
void EchoActionCursor::componentComplete()
{
    if (!m_target)
        setTarget(qobject_cast<QQuickItem *>(parent()));
}
void EchoActionCursor::setTarget(QQuickItem *item)
{
    if (m_target == item)
        return;
    disconnect(m_enabledConnection);
    if (m_target)
        m_target->unsetCursor();
    m_target = item;
    if (m_target)
        m_enabledConnection =
            connect(m_target, &QQuickItem::enabledChanged, this, &EchoActionCursor::updateCursor);
    updateCursor();
    emit targetChanged();
}
void EchoActionCursor::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    updateCursor();
    emit enabledChanged();
}
void EchoActionCursor::setCursorShape(Qt::CursorShape shape)
{
    if (m_shape == shape)
        return;
    m_shape = shape;
    updateCursor();
    emit cursorShapeChanged();
}
void EchoActionCursor::updateCursor()
{
    if (!m_target)
        return;
    if (m_enabled)
        m_target->setCursor(QCursor(m_target->isEnabled() ? m_shape : Qt::ArrowCursor));
    else
        m_target->unsetCursor();
}
} // namespace EchoMusic
