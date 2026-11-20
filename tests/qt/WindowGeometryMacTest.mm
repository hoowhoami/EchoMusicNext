#import <Cocoa/Cocoa.h>
#include <QQuickWindow>
#include "WindowBackdrop.h"
#include "WindowController.h"
#include <QCoreApplication>
#include <QPointer>
#include <QTimer>
#include <QTemporaryDir>
#include <cstdio>

void zoomNativeWindow(QQuickWindow *window)
{
    auto *view = reinterpret_cast<NSView *>(window->winId());
    [view.window zoom:nil];
}

int runMacBackdropRetirementProbe(bool throughController)
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
    EchoMusic::WindowController controller(settings, true);
    EchoMusic::WindowBackdrop backdrop(true);
    auto *window = new QQuickWindow;
    window->setFlags(EchoMusic::WindowBackdrop::mainWindowFlags() | Qt::WindowDoesNotAcceptFocus);
    window->setGeometry(20, 80, 1100, 650);
    window->setOpacity(.01);
    window->show();
    // Match application startup: apply material before entering the event loop.
    // No caller-owned autorelease pool may mask the platform entry's lifetime.
    if (throughController) {
        controller.setAppearance(true, true, 10);
        controller.attach(window);
        controller.nativeCaptionBounds();
    } else {
        backdrop.attach(window);
        const auto error = backdrop.apply(true, true, 10);
        if (!error.isEmpty()) {
            delete window;
            return 2;
        }
    }
    auto *objects = [[NSHashTable alloc] initWithOptions:NSPointerFunctionsWeakMemory capacity:3];
    @autoreleasepool {
        // Weak observations must not leave autoreleased getter results in the
        // outer pool and accidentally retain the objects being measured.
        auto *view = reinterpret_cast<NSView *>(window->winId());
        [objects addObject:view];
        [objects addObject:view.window];
        [objects addObject:view.layer];
    }
    QPointer<QQuickWindow> retired = window;
    QTimer::singleShot(1500, qApp, [window, &backdrop, &controller, throughController] {
        if (throughController) {
            controller.toggleVisibility();
            controller.detach();
        } else
            backdrop.attach(nullptr);
        window->hide();
        window->deleteLater();
    });
    QTimer::singleShot(3500, qApp, [objects, retired] {
        @autoreleasepool {
            const auto remaining = objects.allObjects.count;
            std::printf(
                "{\"nativeObjectsRemaining\":%lu,\"qtWindowAlive\":%s,\"settingsOk\":true}\n",
                static_cast<unsigned long>(remaining), retired ? "true" : "false");
            std::fflush(stdout);
        }
        [objects release];
        QCoreApplication::quit();
    });
    return QCoreApplication::exec();
}
