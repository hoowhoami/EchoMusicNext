#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QtGui/qscreen_platform.h>
#include <QPoint>
#include <QWindow>
#include <QVariantMap>

namespace EchoMusic::Test
{
void activateTestApplication()
{
    // Command-line QtTest executables have no application bundle activation.
    // Establish the native process policy before requesting a key test window.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activate];
}
QVariantMap nativeWindowInputPolicy(QWindow *window)
{
    auto *view = reinterpret_cast<NSView *>(window->winId());
    return {{"appActive", bool(NSApp.active)},
            {"appForeground", NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier ==
                                  NSProcessInfo.processInfo.processIdentifier},
            {"nativeVisible", bool(view.window.visible)},
            {"nativeMiniaturized", bool(view.window.miniaturized)},
            {"nativeMiniaturizable", bool(view.window.styleMask & NSWindowStyleMaskMiniaturizable)},
            {"nativeOccluded", !(view.window.occlusionState & NSWindowOcclusionStateVisible)},
            {"nativeKey", bool(view.window.keyWindow)},
            {"thermalState", int(NSProcessInfo.processInfo.thermalState)},
            {"underCursor", [NSWindow windowNumberAtPoint:NSEvent.mouseLocation
                                belowWindowWithWindowNumber:0] == view.window.windowNumber},
            {"backgroundMove", bool(view.window.movableByWindowBackground)},
            {"viewCanMove", bool(view.mouseDownCanMoveWindow)},
            {"movable", bool(view.window.movable)},
            {"viewClass", QString::fromUtf8(object_getClassName(view))}};
}
bool systemMousePostingAvailable()
{
    return CGPreflightPostEventAccess();
}
void setNativePointerPosition(QPoint globalPosition)
{
    // Cocoa's QCursor::setPos posts a mouse event. When event posting is
    // unavailable, position the real cursor directly; moveNativePointer
    // delivers the corresponding enter/leave and move events to Qt Quick.
    if (QGuiApplication::platformName() == "cocoa" && !systemMousePostingAvailable()) {
        if (auto *screen = QGuiApplication::screenAt(globalPosition)) {
            auto *native =
                screen->nativeInterface<QNativeInterface::QCocoaScreen>()->nativeScreen();
            const auto origin = screen->geometry().topLeft();
            const auto scale = screen->devicePixelRatio() / native.backingScaleFactor;
            const auto point = origin + QPointF(globalPosition - origin) * scale;
            if (CGWarpMouseCursorPosition(point.toCGPoint()) == kCGErrorSuccess)
                return;
        }
    }
    QCursor::setPos(globalPosition);
}
void postSystemLeftMouse(QPoint globalPosition, QEvent::Type type)
{
    const auto eventType = type == QEvent::MouseButtonPress     ? kCGEventLeftMouseDown
                           : type == QEvent::MouseButtonRelease ? kCGEventLeftMouseUp
                                                                : kCGEventLeftMouseDragged;
    auto *event = CGEventCreateMouseEvent(nullptr, eventType,
                                          CGPointMake(globalPosition.x(), globalPosition.y()),
                                          kCGMouseButtonLeft);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, 1);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}
void postNativeLeftMouse(QWindow *window, QPoint position, QEvent::Type type)
{
    auto *view = reinterpret_cast<NSView *>(window->winId());
    const auto local = NSMakePoint(
        position.x(), view.isFlipped ? position.y() : view.bounds.size.height - position.y());
    const auto location = [view convertPoint:local toView:nil];
    const auto eventType = type == QEvent::MouseButtonPress     ? NSEventTypeLeftMouseDown
                           : type == QEvent::MouseButtonRelease ? NSEventTypeLeftMouseUp
                                                                : NSEventTypeLeftMouseDragged;
    auto *event = [NSEvent mouseEventWithType:eventType
                                     location:location
                                modifierFlags:0
                                    timestamp:NSProcessInfo.processInfo.systemUptime
                                 windowNumber:view.window.windowNumber
                                      context:nil
                                  eventNumber:0
                                   clickCount:1
                                     pressure:type == QEvent::MouseButtonRelease ? 0 : 1];
    [NSApp postEvent:event atStart:NO];
}
} // namespace EchoMusic::Test
