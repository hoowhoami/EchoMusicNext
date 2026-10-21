#import <Cocoa/Cocoa.h>
#include <QVariantMap>
#include <QRectF>
#include <QPointer>
#include <QWindow>
#include <QGuiApplication>
// A fresh, never-repositioned AppKit window is the size/spacing reference for
// this OS + linked SDK. Do not encode pre-Tahoe (or Tahoe) button dimensions.
QVariantList nativeSystemCaptionFrames(QWindow *window)
{
    auto *content = reinterpret_cast<NSView *>(window->winId());
    auto *reference = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 640, 480)
                  styleMask:content.window.styleMask & ~NSWindowStyleMaskFullScreen
                    backing:NSBackingStoreBuffered
                      defer:NO];
    reference.releasedWhenClosed = NO;
    QVariantList frames;
    for (NSNumber *kind in
         @[ @(NSWindowCloseButton), @(NSWindowMiniaturizeButton), @(NSWindowZoomButton) ]) {
        auto *button = [reference standardWindowButton:NSWindowButton(kind.integerValue)];
        const auto rect = button.frame;
        frames.append(QRectF(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height));
    }
    [reference close];
    [reference release];
    return frames;
}
// Read metadata only; Qt retains ownership of its native window and content view.
QVariantMap nativeWindowAppearance(QWindow *window)
{
    auto *view = reinterpret_cast<NSView *>(window->winId());
    auto *native = view.window;
    // A style bit changes before the Cocoa fullscreen animation completes. Wait
    // for the real did-enter/did-exit notification before issuing the inverse.
    if (!window->property("echoTestFullscreenObserved").toBool()) {
        window->setProperty("echoTestFullscreenObserved", true);
        window->setProperty("echoTestFullscreenSettled", "normal");
        const QPointer<QWindow> guard(window);
        id entered = [NSNotificationCenter.defaultCenter
            addObserverForName:NSWindowDidEnterFullScreenNotification
                        object:native
                         queue:nil
                    usingBlock:^(NSNotification *) {
                      if (guard)
                          guard->setProperty("echoTestFullscreenSettled", "full");
                    }];
        id exited = [NSNotificationCenter.defaultCenter
            addObserverForName:NSWindowDidExitFullScreenNotification
                        object:native
                         queue:nil
                    usingBlock:^(NSNotification *) {
                      if (guard)
                          guard->setProperty("echoTestFullscreenSettled", "normal");
                    }];
        QObject::connect(window, &QObject::destroyed, [entered, exited] {
            [NSNotificationCenter.defaultCenter removeObserver:entered];
            [NSNotificationCenter.defaultCenter removeObserver:exited];
        });
    }
    QVariantList frames, identities, targets, actions;
    for (NSNumber *kind in
         @[ @(NSWindowCloseButton), @(NSWindowMiniaturizeButton), @(NSWindowZoomButton) ]) {
        auto *button = [native standardWindowButton:NSWindowButton(kind.integerValue)];
        const auto rect = [button convertRect:button.bounds toView:view];
        const auto top = view.isFlipped ? rect.origin.y : view.bounds.size.height - NSMaxY(rect);
        frames.append(QRectF(rect.origin.x, top, rect.size.width, rect.size.height));
        identities.append(qulonglong(reinterpret_cast<quintptr>(button)));
        targets.append(qulonglong(reinterpret_cast<quintptr>(button.target)));
        actions.append(QString::fromUtf8(sel_getName(button.action)));
    }
    int backdropCount = 0, applicationBackdropCount = 0;
    for (NSView *sibling in view.superview.subviews) {
        if ([sibling isKindOfClass:NSVisualEffectView.class])
            ++backdropCount;
        if ([sibling isKindOfClass:NSClassFromString(@"EchoWindowMaterial")])
            ++applicationBackdropCount;
    }
    return {{"backdropCount", backdropCount},
            {"applicationBackdropCount", applicationBackdropCount},
            {"windowNumber", qint64(native.windowNumber)},
            {"fullscreenSettled", window->property("echoTestFullscreenSettled")},
            {"buttonFrames", frames},
            {"buttonIdentities", identities},
            {"buttonTargets", targets},
            {"buttonActions", actions},
            {"nativeFullscreen", bool(native.styleMask & NSWindowStyleMaskFullScreen)},
            {"backgroundAlpha", native.backgroundColor.alphaComponent},
            {"opaque", bool(native.opaque)},
            {"shadow", bool(native.hasShadow)},
            {"closeButton", bool([native standardWindowButton:NSWindowCloseButton])},
            {"nativeWindow", qulonglong(reinterpret_cast<quintptr>(native))}};
}

QVariantMap nativeFloatingAppearance(QWindow *window)
{
    if (!window || QGuiApplication::platformName() != "cocoa")
        return {};
    auto *view = reinterpret_cast<NSView *>(window->winId());
    auto *native = view.window;
    // QT_SCALE_FACTOR maps Qt DIP to AppKit points in addition to Retina's
    // device pixels. Report these hints in the same DIP as QWindow's contract.
    const auto pointScale = window->width() > 0 ? view.bounds.size.width / window->width() : 1.;
    return {
        {"windowNumber", qint64(native.windowNumber)},
        {"minimumContentWidth", native.contentMinSize.width / pointScale},
        {"maximumContentWidth", native.contentMaxSize.width / pointScale},
        {"ignoresMouseEvents", bool(native.ignoresMouseEvents)},
        {"titled", bool(native.styleMask & NSWindowStyleMaskTitled)},
        {"opaque", bool(native.opaque)},
        {"level", qint64(native.level)},
        {"panel", bool([native isKindOfClass:NSPanel.class])},
        {"nonactivating", bool(native.styleMask & NSWindowStyleMaskNonactivatingPanel)},
        {"fullscreenOverlayLevel", native.level == NSScreenSaverWindowLevel},
        {"allSpaces", bool(native.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces)},
        {"fullscreenAuxiliary",
         bool(native.collectionBehavior & NSWindowCollectionBehaviorFullScreenAuxiliary)},
        {"allApplications",
         bool(native.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllApplications)},
        {"hidesOnDeactivate", bool(native.hidesOnDeactivate)}};
}

QVariantMap nativeMacApplicationPresentation()
{
    return {{"activationPolicy", qint64(NSApp.activationPolicy)},
            {"active", bool(NSApp.active)},
            {"frontmostPid",
             qint64(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier)},
            {"runningApplicationPolicy",
             qint64(NSRunningApplication.currentApplication.activationPolicy)}};
}

QVariantMap nativeMacComposition(QWindow *window, qint64 hostWindow)
{
    auto *view = reinterpret_cast<NSView *>(window->winId());
    auto *native = view.window;
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    NSArray *windows = (NSArray *)list;
    NSInteger overlayIndex = -1, hostIndex = -1;
    for (NSUInteger i = 0; i < windows.count; ++i) {
        NSDictionary *entry = windows[i];
        const auto number = [entry[(id)kCGWindowNumber] longLongValue];
        if (number == native.windowNumber)
            overlayIndex = NSInteger(i);
        if (number == hostWindow)
            hostIndex = NSInteger(i);
    }
    if (list)
        CFRelease(list);
    return {{"onActiveSpace", bool(native.isOnActiveSpace)},
            {"overlayOnScreen", overlayIndex >= 0},
            {"hostOnScreen", hostIndex >= 0},
            {"aboveHost", overlayIndex >= 0 && hostIndex >= 0 && overlayIndex < hostIndex}};
}

// Read the actual MPNowPlayingInfoCenter populated through the Rust media bridge.
// Resolve framework symbols already loaded by the host rather than linking a
// second media-controls implementation into the test.
#import <objc/message.h>
#include <dlfcn.h>
QVariantMap nativeSystemMediaInfo()
{
    const auto cls = NSClassFromString(@"MPNowPlayingInfoCenter");
    if (!cls)
        return {};
    id center = ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("defaultCenter"));
    NSDictionary *info =
        ((id (*)(id, SEL))objc_msgSend)(center, sel_registerName("nowPlayingInfo"));
    const auto value = [info](const char *symbol) -> id {
        auto key = reinterpret_cast<NSString **>(dlsym(RTLD_DEFAULT, symbol));
        return key ? info[*key] : nil;
    };
    const auto text = [&value](const char *symbol) {
        id item = value(symbol);
        return [item isKindOfClass:[NSString class]] ? QString::fromUtf8([item UTF8String])
                                                     : QString();
    };
    return {{"title", text("MPMediaItemPropertyTitle")},
            {"artist", text("MPMediaItemPropertyArtist")},
            {"album", text("MPMediaItemPropertyAlbumTitle")},
            {"artwork", value("MPMediaItemPropertyArtwork") != nil}};
}
