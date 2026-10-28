#import <Cocoa/Cocoa.h>
#include <QQuickWindow>
#include <QRectF>
#include <QScopeGuard>

namespace EchoMusic
{
void setMacDockHidden(bool hidden)
{
    [NSApp setActivationPolicy:hidden ? NSApplicationActivationPolicyAccessory
                                      : NSApplicationActivationPolicyRegular];
}
void configureMacWindow(QQuickWindow *window)
{
    if (!window)
        return;
    // Qt owns the native window and its standard buttons. Keep their default
    // AppKit layout; only suppress the redundant title over the custom chrome.
    auto *content = reinterpret_cast<NSView *>(window->winId());
    content.window.titleVisibility = NSWindowTitleHidden;
    // Expanded titlebar content is still an AppKit drag surface even when
    // Quick accepts its controls. Only the explicit controller move may enable
    // native movement; do not replace Qt's view/class or native button targets.
    content.window.movableByWindowBackground = NO;
    content.window.movable = NO;
}
bool startMacWindowMove(QQuickWindow *window)
{
    auto *content = reinterpret_cast<NSView *>(window->winId());
    auto *native = [content.window retain];
    if (!native)
        return false;
    native.movable = YES;
    // The system loop can process closing events. Keep NSWindow alive until
    // Qt's modal drag returns, then revoke permission before the next press.
    const auto restore = qScopeGuard([native] {
        native.movable = NO;
        [native release];
    });
    return window->startSystemMove();
}
QRectF macCaptionBounds(QQuickWindow *window)
{
    if (!window || !window->handle())
        return {};
    auto *content = reinterpret_cast<NSView *>(window->winId());
    auto *native = content.window;
    if (!native || (native.styleMask & NSWindowStyleMaskFullScreen))
        return {};
    QRectF bounds;
    const NSWindowButton kinds[]{NSWindowCloseButton, NSWindowMiniaturizeButton,
                                 NSWindowZoomButton};
    for (const auto kind : kinds) {
        auto *button = [native standardWindowButton:kind];
        if (!button.superview)
            return {};
        const auto rect = [button convertRect:button.bounds toView:content];
        const auto top =
            content.isFlipped ? rect.origin.y : content.bounds.size.height - NSMaxY(rect);
        bounds = bounds.united(QRectF(rect.origin.x, top, rect.size.width, rect.size.height));
    }
    // QWindow geometry is in Qt DIP; AppKit coordinates are window points.
    // Application UI zoom is applied later by QML, independently of this scale.
    const auto pointScale = window->width() > 0 ? content.bounds.size.width / window->width() : 1.;
    return pointScale > 0 ? QRectF(bounds.topLeft() / pointScale, bounds.size() / pointScale)
                          : QRectF{};
}
} // namespace EchoMusic
