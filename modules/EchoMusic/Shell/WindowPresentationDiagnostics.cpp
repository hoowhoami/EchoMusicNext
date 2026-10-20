#include "WindowPresentationDiagnostics.h"
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QFontInfo>
#include <atomic>
#include <chrono>
#include <utility>

namespace EchoMusic
{
namespace
{
Q_LOGGING_CATEGORY(presentationLog, "echo.presentation")
qint64 monotonicUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
void keepMaximum(std::atomic<qint64> &maximum, qint64 duration)
{
    auto previous = maximum.load(std::memory_order_relaxed);
    while (duration > previous &&
           !maximum.compare_exchange_weak(previous, duration, std::memory_order_relaxed)) {
    }
}
} // namespace
struct WindowPresentationDiagnostics::Counters
{
    std::atomic<qint64> frameStart{0}, maximumFrame{0}, maximumAcquire{0};
    std::atomic<quint64> frames{0};
};
WindowPresentationDiagnostics::WindowPresentationDiagnostics(QQuickWindow *window)
    : QObject(window), m_counters(std::make_shared<Counters>())
{
    const auto counters = m_counters;
    connect(
        window, &QQuickWindow::beforeFrameBegin, this,
        [counters] { counters->frameStart.store(monotonicUs(), std::memory_order_relaxed); },
        Qt::DirectConnection);
    connect(
        window, &QQuickWindow::beforeRendering, this,
        [counters] {
            const auto now = monotonicUs();
            const auto start = counters->frameStart.load(std::memory_order_relaxed);
            if (start)
                keepMaximum(counters->maximumAcquire, now - start);
        },
        Qt::DirectConnection);
    connect(
        window, &QQuickWindow::afterFrameEnd, this,
        [counters] {
            const auto start = counters->frameStart.load(std::memory_order_relaxed);
            if (start)
                keepMaximum(counters->maximumFrame, monotonicUs() - start);
            counters->frames.fetch_add(1, std::memory_order_relaxed);
        },
        Qt::DirectConnection);
    // A 50ms heartbeat does not call update() and leaves idle rendering idle.
    m_poll.setInterval(50);
    m_poll.setTimerType(Qt::PreciseTimer);
    connect(&m_poll, &QTimer::timeout, this,
            [window, counters, previous = monotonicUs(), reportAt = monotonicUs(),
             maximumGui = qint64(0)]() mutable {
                const auto now = monotonicUs();
                const auto gui = now - std::exchange(previous, now);
                maximumGui = qMax(maximumGui, gui);
                const auto frame = counters->maximumFrame.exchange(0);
                const auto acquire = counters->maximumAcquire.exchange(0);
                // AppKit coalesces the 50ms timer to ~250ms in the background
                // (observed during lock). Do not label that as a render stall;
                // preserve it in the summary while catching the reported ~1s
                // GUI stalls and any >200ms rendered frame.
                if (gui > 500000 || frame > 200000) {
                    qCWarning(presentationLog)
                        << "Presentation stall: gui-heartbeat-ms=" << gui / 1000.0
                        << "frame-ms=" << frame / 1000.0
                        << "before-rendering-ms=" << acquire / 1000.0
                        << "visible=" << window->isVisible() << "exposed=" << window->isExposed()
                        << "active=" << window->isActive() << "visibility=" << window->visibility()
                        << "app-state=" << QGuiApplication::applicationState();
                }
                if (now - reportAt >= 5000000) {
                    qCInfo(presentationLog)
                        << "Presentation heartbeat: frames=" << counters->frames.load()
                        << "max-gui-interval-ms=" << maximumGui / 1000.0
                        << "graphics-api=" << window->rendererInterface()->graphicsApi()
                        << "qpa=" << QGuiApplication::platformName()
                        << "effective-dpr=" << window->effectiveDevicePixelRatio()
                        << "ui-scale=" << window->property("uiScale")
                        << "visible=" << window->isVisible() << "exposed=" << window->isExposed()
                        << "active=" << window->isActive()
                        << "app-state=" << QGuiApplication::applicationState();
                    reportAt = now;
                    maximumGui = 0;
                }
            });
    connect(window, &QWindow::visibilityChanged, this, [window] {
        qCInfo(presentationLog) << "Window visibility=" << window->visibility()
                                << "exposed=" << window->isExposed();
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [](Qt::ApplicationState state) {
                qCInfo(presentationLog) << "Application state=" << state;
            });
    qCInfo(presentationLog) << "Presentation diagnostics enabled; no backend/frame policy changes";
    const auto *screen = window->screen();
    qCInfo(presentationLog) << "Display configuration: qpa=" << QGuiApplication::platformName()
                            << "session=" << qEnvironmentVariable("XDG_SESSION_TYPE")
                            << "screen=" << (screen ? screen->name() : QString())
                            << "geometry=" << (screen ? screen->geometry() : QRect())
                            << "screen-dpr=" << (screen ? screen->devicePixelRatio() : 0.)
                            << "effective-dpr=" << window->effectiveDevicePixelRatio()
                            << "logical-dpi=" << (screen ? screen->logicalDotsPerInch() : 0.)
                            << "ui-scale=" << window->property("uiScale")
                            << "default-text-renderer=" << QQuickWindow::textRenderType()
                            << "font=" << QFontInfo(QGuiApplication::font()).family()
                            << "QT_SCALE_FACTOR=" << qEnvironmentVariable("QT_SCALE_FACTOR")
                            << "QT_SCREEN_SCALE_FACTORS="
                            << qEnvironmentVariable("QT_SCREEN_SCALE_FACTORS")
                            << "QT_FONT_DPI=" << qEnvironmentVariable("QT_FONT_DPI");
    m_poll.start();
}
} // namespace EchoMusic
