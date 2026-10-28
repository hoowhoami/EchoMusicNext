#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <QEvent>
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
    return {{"backgroundMove", bool(view.window.movableByWindowBackground)},
            {"viewCanMove", bool(view.mouseDownCanMoveWindow)},
            {"movable", bool(view.window.movable)},
            {"viewClass", QString::fromUtf8(object_getClassName(view))}};
}
bool systemMousePostingAvailable()
{
    return CGPreflightPostEventAccess();
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
