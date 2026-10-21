#include "WindowPresentationDiagnostics.h"
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QFontInfo>
#include <QFontDatabase>
#include <QTextLayout>
#include <QGlyphRun>
#include <QRawFont>
#include <QSet>
#include <atomic>
#include <chrono>
#include <utility>

namespace EchoMusic
{
namespace
{
Q_LOGGING_CATEGORY(presentationLog, "echo.presentation")
const char *graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software:
        return "Software";
    case QSGRendererInterface::OpenGL:
        return "OpenGL";
    case QSGRendererInterface::OpenVG:
        return "OpenVG";
    case QSGRendererInterface::Vulkan:
        return "Vulkan";
    case QSGRendererInterface::Metal:
        return "Metal";
    case QSGRendererInterface::Direct3D11:
        return "Direct3D11";
    case QSGRendererInterface::Direct3D12:
        return "Direct3D12";
    case QSGRendererInterface::Null:
        return "Null";
    default:
        return "Unknown";
    }
}
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
void reportTextFonts(QQuickWindow *window)
{
    // ApplicationWindow may use a saved UI font independently of qGuiApp's
    // default. Shape a fixed sample to observe the actual script fallbacks.
    const auto property = window->property("font");
    const auto font =
        property.canConvert<QFont>() ? property.value<QFont>() : QGuiApplication::font();
    qCInfo(presentationLog) << "UI font: family=" << QFontInfo(font).family()
                            << "pixel-size=" << QFontInfo(font).pixelSize()
                            << "weight=" << font.weight();
    QTextLayout layout(QStringLiteral("播放队列 文字清晰度 EchoMusicNext 0123456789"), font);
    layout.beginLayout();
    auto line = layout.createLine();
    if (line.isValid())
        line.setLineWidth(1000);
    layout.endLayout();
    QSet<QString> reported;
    for (const auto &run : layout.glyphRuns()) {
        const auto raw = run.rawFont();
        if (!raw.isValid())
            continue;
        const auto key = raw.familyName() + QChar(0x1f) + raw.styleName();
        if (reported.contains(key))
            continue;
        reported.insert(key);
        qCInfo(presentationLog) << "Text sample font: family=" << raw.familyName()
                                << "style=" << raw.styleName() << "pixel-size=" << raw.pixelSize()
                                << "smoothly-scalable="
                                << QFontDatabase::isSmoothlyScalable(raw.familyName(),
                                                                     raw.styleName());
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
    const auto reportBackend = [window] {
        qCInfo(presentationLog) << "Presentation backend ready: graphics-api="
                                << graphicsApiName(window->rendererInterface()->graphicsApi())
                                << "qpa=" << QGuiApplication::platformName()
                                << "effective-dpr=" << window->effectiveDevicePixelRatio()
                                << "ui-scale=" << window->property("uiScale")
                                << "default-text-renderer=" << QQuickWindow::textRenderType();
        reportTextFonts(window);
    };
    connect(window, &QQuickWindow::sceneGraphInitialized, this, reportBackend,
            Qt::QueuedConnection);
    if (window->isSceneGraphInitialized())
        reportBackend();
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
                        << "max-gui-interval-ms=" << maximumGui / 1000.0 << "graphics-api="
                        << graphicsApiName(window->rendererInterface()->graphicsApi())
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
    qCInfo(presentationLog) << "Renderer overrides: QT_QUICK_BACKEND="
                            << qEnvironmentVariable("QT_QUICK_BACKEND")
                            << "QSG_RHI_BACKEND=" << qEnvironmentVariable("QSG_RHI_BACKEND");
    m_poll.start();
}
} // namespace EchoMusic
