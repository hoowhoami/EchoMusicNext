#import <Cocoa/Cocoa.h>
#include "WindowBackdrop.h"
#include <QWindow>
// This sibling is visual material only; Qt keeps all pointer/keyboard routing.
@interface EchoWindowMaterial : NSVisualEffectView
@end
@implementation EchoWindowMaterial
- (NSView *)hitTest:(NSPoint)point
{
    return nil;
}
@end
namespace EchoMusic
{
void releaseMacBackdrop(void *&effect)
{
    if (!effect)
        return;
    auto *view = static_cast<EchoWindowMaterial *>(effect);
    [view removeFromSuperview];
    [view release];
    effect = nullptr;
}
QString applyMacBackdrop(QWindow *window, void *&effect, bool enabled, bool dark, int transparency)
{
    // QWindow's Cocoa handle is NSView*. Keep Qt's content view, Metal layer,
    // delegate and NSWindow ownership intact: insert below it in its existing
    // superview rather than replacing the native content view or reparenting Qt.
    auto *content = reinterpret_cast<NSView *>(window->winId());
    auto *host = content.superview;
    if (!host || !content.window)
        return QObject::tr("窗口效果未能应用，请重试");
    // Expanded-client-area windows retain their native frame/traffic lights.
    // Qt's framed QNSWindow keeps the system background even with an alpha
    // framebuffer; clear that backing explicitly instead of fading the window
    // (which would also fade text), changing flags or replacing its content view.
    const bool backgroundEnabled = enabled || transparency > 0;
    content.window.backgroundColor = backgroundEnabled
                                         ? NSColor.clearColor
                                         : (dark ? [NSColor colorWithSRGBRed:38.0 / 255
                                                                       green:38.0 / 255
                                                                        blue:42.0 / 255
                                                                       alpha:1]
                                                 : [NSColor colorWithSRGBRed:245.0 / 255
                                                                       green:245.0 / 255
                                                                        blue:247.0 / 255
                                                                       alpha:1]);
    // AppKit can derive alpha-window shadows from individual glyphs and popup
    // surfaces instead of the frame. Only native material or an opaque full
    // background supplies a reliable window-sized silhouette.
    const bool shadow = enabled || transparency == 0;
    if (content.window.hasShadow != shadow) {
        content.window.hasShadow = shadow;
        [content.window invalidateShadow];
    }
    if (!enabled) {
        releaseMacBackdrop(effect);
        return {};
    }
    if (!effect) {
        auto *view = [[EchoWindowMaterial alloc] initWithFrame:content.frame];
        view.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        view.state = NSVisualEffectStateFollowsWindowActiveState;
        [host addSubview:view positioned:NSWindowBelow relativeTo:content];
        effect = view;
    }
    auto *view = static_cast<EchoWindowMaterial *>(effect);
    if (view.superview != host) {
        [view removeFromSuperview];
        [host addSubview:view positioned:NSWindowBelow relativeTo:content];
    }
    view.frame = content.frame;
    view.autoresizingMask = content.autoresizingMask;
    view.material =
        dark ? NSVisualEffectMaterialHUDWindow : NSVisualEffectMaterialUnderWindowBackground;
    return {};
}
} // namespace EchoMusic
