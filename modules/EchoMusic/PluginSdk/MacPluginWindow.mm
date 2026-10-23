#include <QQuickWindow>
#include <QGuiApplication>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
namespace EchoMusic
{
namespace
{
BOOL firstMouse(id self, SEL, NSEvent *)
{
    auto view = static_cast<NSView *>(self);
    return [objc_getAssociatedObject(view, @selector(acceptsFirstMouse:)) boolValue];
}
} // namespace
void configureMacPluginWindow(QQuickWindow *window, bool accepts, bool onTop)
{
    if (!window || QGuiApplication::platformName() != "cocoa")
        return;
    auto *view = reinterpret_cast<NSView *>(window->winId());
    if (!view || !view.window)
        return;
    // Keep Qt's NSView implementation and dispatch; customize only this instance's first click.
    Class base = object_getClass(view);
    const QByteArray suffix("_EchoPluginFirstMouse");
    const QByteArray current(class_getName(base));
    const auto name = current.endsWith(suffix) ? current : current + suffix;
    Class adapted = objc_lookUpClass(name.constData());
    if (!adapted) {
        adapted = objc_allocateClassPair(base, name.constData(), 0);
        if (adapted) {
            class_addMethod(adapted, @selector(acceptsFirstMouse:),
                            reinterpret_cast<IMP>(firstMouse), "c@:@");
            objc_registerClassPair(adapted);
        }
    }
    objc_setAssociatedObject(view, @selector(acceptsFirstMouse:), @(accepts),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if (adapted)
        object_setClass(view, adapted);
    auto *native = view.window;
    if ([native isKindOfClass:NSPanel.class]) {
        native.styleMask = onTop ? native.styleMask | NSWindowStyleMaskNonactivatingPanel
                                 : native.styleMask & ~NSWindowStyleMaskNonactivatingPanel;
        static_cast<NSPanel *>(native).becomesKeyOnlyIfNeeded = onTop;
    }
    native.hidesOnDeactivate = NO;
    native.collectionBehavior = onTop ? NSWindowCollectionBehaviorCanJoinAllSpaces |
                                            NSWindowCollectionBehaviorFullScreenAuxiliary |
                                            NSWindowCollectionBehaviorCanJoinAllApplications
                                      : NSWindowCollectionBehaviorDefault;
    native.level = onTop ? NSScreenSaverWindowLevel : NSNormalWindowLevel;
}
} // namespace EchoMusic
