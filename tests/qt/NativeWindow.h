#pragma once
#include <QCursor>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QGuiApplication>
#include <QWindow>
#include <QQuickWindow>
#include <QQuickItem>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QtTest>

namespace EchoMusic::Test
{
/// frameSwapped can be emitted by the render thread. Queue observation onto a
/// GUI-owned receiver so a short-lived waiter cannot be destroyed during a
/// direct meta-call from that thread. Late queued deliveries die with receiver.
class FrameSpy
{
public:
    explicit FrameSpy(QQuickWindow *window)
    {
        QObject::connect(
            window, &QQuickWindow::frameSwapped, &m_receiver,
            [this] {
                ++m_count;
                if (m_waiter)
                    m_waiter->quit();
            },
            Qt::QueuedConnection);
    }
    bool isEmpty() const
    {
        return m_count == 0;
    }
    qsizetype size() const
    {
        return m_count;
    }
    qsizetype count() const
    {
        return m_count;
    }
    void clear()
    {
        m_count = 0;
    }
    bool wait(int timeout = 5000)
    {
        const auto before = m_count;
        QEventLoop loop;
        m_waiter = &loop;
        QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
        loop.exec();
        m_waiter = nullptr;
        return m_count > before;
    }

private:
    qsizetype m_count = 0;
    QEventLoop *m_waiter = nullptr;
    QObject m_receiver;
};
#ifdef Q_OS_MACOS
void activateTestApplication();
QVariantMap nativeWindowInputPolicy(QWindow *window);
bool systemMousePostingAvailable();
void postSystemLeftMouse(QPoint globalPosition, QEvent::Type type);
// Post through AppKit/QPA, including the native expanded-caption delivery path.
void postNativeLeftMouse(QWindow *window, QPoint position, QEvent::Type type);
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
/// Opening can finish before nested Qt layouts have polished. Observe real
/// swapped frames and stable scene geometry before preparing an input point.
inline bool settleNativeItem(QQuickItem *item, int timeout = 5000)
{
    const QPointer<QQuickItem> target(item);
    const QPointer<QQuickWindow> window(item ? item->window() : nullptr);
    if (!target || !window)
        return false;
    QElapsedTimer elapsed;
    elapsed.start();
    QRectF previous;
    int stable = 0;
    while (elapsed.elapsed() < timeout) {
        FrameSpy rendered(window);
        window->update();
        if (rendered.isEmpty() &&
            !rendered.wait(qMax(1, qMin(1000, timeout - int(elapsed.elapsed())))))
            return false;
        if (!target || !window || !target->isVisible())
            return false;
        const auto bounds = target->mapRectToScene(target->boundingRect());
        stable = bounds == previous && !bounds.isEmpty() ? stable + 1 : 0;
        previous = bounds;
        if (stable >= 2)
            return true;
    }
    return false;
}

/// QTest's QWindow mouse events do not move the platform cursor. Cocoa can
/// emit a later enter/leave from that cursor when popups or render captures
/// update the window. Keep the real pointer and injected events consistent.
inline void moveNativePointer(QWindow *window, QPoint position)
{
    const auto global = window->mapToGlobal(position);
    // Offscreen has a virtual cursor too. Quick reconciles hover against it
    // on later frames; leaving it behind can undo an injected hover event.
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
