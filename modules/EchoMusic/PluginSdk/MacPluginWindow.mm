#include <QQuickWindow>
#include <QGuiApplication>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
namespace EchoMusic
{
namespace
{
char firstMouseOverrideKey, firstMouseOriginalKey;
BOOL firstMouse(id self, SEL selector, NSEvent *event)
{
    if (NSNumber *value = objc_getAssociatedObject(self, &firstMouseOverrideKey))
        return [value boolValue];
    for (Class type = object_getClass(self); type; type = class_getSuperclass(type)) {
        if (auto *original =
                static_cast<NSValue *>(objc_getAssociatedObject(type, &firstMouseOriginalKey))) {
            const auto call = reinterpret_cast<BOOL (*)(id, SEL, NSEvent *)>(original.pointerValue);
            return call(self, selector, event);
        }
    }
    return NO;
}
void setFirstMouse(NSView *view, bool accepts)
{
    // Never replace isa: AppKit/Qt's KVO subclass owns observer registration and teardown.
    // Untagged views keep the original Qt behavior; only this view receives an override.
    Class type = object_getClass(view);
    while (QByteArray(class_getName(type)).startsWith("NSKVONotifying_"))
        type = class_getSuperclass(type);
    const SEL selector = @selector(acceptsFirstMouse:);
    if (!objc_getAssociatedObject(type, &firstMouseOriginalKey)) {
        Method method = class_getInstanceMethod(type, selector);
        const IMP original = method_getImplementation(method);
        if (original != reinterpret_cast<IMP>(firstMouse)) {
            objc_setAssociatedObject(type, &firstMouseOriginalKey,
                                     [NSValue valueWithPointer:reinterpret_cast<void *>(original)],
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            if (!class_addMethod(type, selector, reinterpret_cast<IMP>(firstMouse),
                                 method_getTypeEncoding(method)))
                method_setImplementation(method, reinterpret_cast<IMP>(firstMouse));
        }
    }
    objc_setAssociatedObject(view, &firstMouseOverrideKey, @(accepts),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
} // namespace
void configureMacPluginWindow(QQuickWindow *window, bool accepts, bool onTop)
{
    if (!window || QGuiApplication::platformName() != "cocoa")
        return;
    auto *view = reinterpret_cast<NSView *>(window->winId());
    if (!view || !view.window)
        return;
    setFirstMouse(view, accepts);
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
