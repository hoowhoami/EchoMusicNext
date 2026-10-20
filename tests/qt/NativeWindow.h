#pragma once
#include <QCursor>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QGuiApplication>
#include <QWindow>
#include <QtTest>

namespace EchoMusic::Test
{
#ifdef Q_OS_MACOS
void activateTestApplication();
#endif
/// A CLI test can be a background process. On Cocoa requestActivate() makes a
/// window key but does not foreground the process; raise() supplies that Qt
/// platform step. Keep native input preconditions shared across formal UI tests.
inline bool prepareNativeInput(QWindow *window)
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
        return true;
#ifdef Q_OS_MACOS
    activateTestApplication();
#endif
    if (!QTest::qWaitForWindowExposed(window)) {
        qInfo() << "Native test window not exposed" << window->isVisible() << window->geometry();
        return false;
    }
    window->raise();
    window->requestActivate();
    const bool active = QTest::qWaitForWindowActive(window);
    if (!active)
        qInfo() << "Native test window not active" << window->isExposed()
                << QGuiApplication::applicationState();
    return active;
}
/// QTest's QWindow mouse events do not move the platform cursor. Cocoa can
/// emit a later enter/leave from that cursor when popups or render captures
/// update the window. Keep the real pointer and injected events consistent.
inline void moveNativePointer(QWindow *window, QPoint position)
{
    const auto global = window->mapToGlobal(position);
    if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
        QCursor::setPos(global);
    // QTest sends a QPA mouse event directly, bypassing the platform plugin.
    // Complete its window enter/leave sequence too: a cursor warp alone does
    // not deliver Cocoa tracking events when a short-lived tool disappears.
    static QPointer<QWindow> entered;
    const bool inside = QRect(QPoint(), window->size()).contains(position) &&
                        !window->flags().testFlag(Qt::WindowTransparentForInput);
    if (entered && (entered != window || !inside)) {
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(entered, &leave);
        entered.clear();
    }
    if (inside && entered != window) {
        QEnterEvent enter(position, position, global);
        QCoreApplication::sendEvent(window, &enter);
        entered = window;
    } else if (!inside) {
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(window, &leave);
    }
    QTest::mouseMove(window, position);
    // A platform cursor warp can update QGuiApplication's cached global
    // position before QTest injects the same position. Deliver the matching
    // hover event to the target as well; this keeps Quick's item delivery in
    // sync without changing any item's hover state directly.
    QMouseEvent hover(QEvent::MouseMove, position, global, Qt::NoButton,
                      QGuiApplication::mouseButtons(), QGuiApplication::keyboardModifiers());
    QCoreApplication::sendEvent(window, &hover);
}
} // namespace EchoMusic::Test
