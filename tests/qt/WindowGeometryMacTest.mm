#import <Cocoa/Cocoa.h>
#include <QQuickWindow>

void zoomNativeWindow(QQuickWindow *window)
{
    auto *view = reinterpret_cast<NSView *>(window->winId());
    [view.window zoom:nil];
}
