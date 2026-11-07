#pragma once

class QQuickWindow;

namespace EchoMusic
{
/// Attach the Qt 6.12 empty-animation-queue workaround after window creation.
void installQuickAnimationDriverGuard(QQuickWindow &window);
} // namespace EchoMusic
