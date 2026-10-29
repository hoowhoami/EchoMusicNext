#include "EchoGpuSurface.h"
#include "EchoPluginContext.h"
#include "graphics/GpuRhi_p.h"
#include <QQuickWindow>
#include <QRunnable>
#include <QGuiApplication>
#include <QEvent>
#include <QPlatformSurfaceEvent>
#include <QQmlEngine>
#include <cmath>
namespace EchoMusic
{
namespace
{
class ReleaseGpu final : public QRunnable
{
public:
    explicit ReleaseGpu(std::shared_ptr<GpuRenderState> state) : m_state(std::move(state)) {}
    void run() override
    {
        if (!m_state->enabled)
            m_state->release();
    }

private:
    std::shared_ptr<GpuRenderState> m_state;
};
class GraphicsWindowPolicy final : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::PlatformSurface &&
            static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
                QPlatformSurfaceEvent::SurfaceCreated)
            if (auto *window = qobject_cast<QQuickWindow *>(object))
                EchoGpuSurface::prepareWindow(window);
        return false;
    }
};
} // namespace
void EchoGpuSurface::installWindowPolicy()
{
    if (!qGuiApp || qGuiApp->property("echoGraphicsWindowPolicy").toBool())
        return;
    qGuiApp->setProperty("echoGraphicsWindowPolicy", true);
    qGuiApp->installEventFilter(new GraphicsWindowPolicy(qGuiApp));
}
EchoGpuSurface::EchoGpuSurface(QQuickItem *parent) : QQuickRhiItem(parent)
{
    setAlphaBlending(true);
    m_status = {{"available", false}, {"compute", false}, {"hdr", false}, {"state", "waiting"}};
    m_bridge = std::shared_ptr<GpuBridge>(new GpuBridge, [](GpuBridge *p) { p->deleteLater(); });
    m_bridge->surface = this;
    connect(&m_clock, &QTimer::timeout, this, [this] {
        if (live())
            update();
    });
    m_clock.setTimerType(Qt::PreciseTimer);
    connect(this, &QQuickItem::visibleChanged, this, &EchoGpuSurface::syncActivity);
    connect(this, &QQuickItem::widthChanged, this, &EchoGpuSurface::syncActivity);
    connect(this, &QQuickItem::heightChanged, this, &EchoGpuSurface::syncActivity);
    connect(this, &QQuickRhiItem::fixedColorBufferWidthChanged, this,
            &EchoGpuSurface::syncActivity);
    connect(this, &QQuickRhiItem::fixedColorBufferHeightChanged, this,
            &EchoGpuSurface::syncActivity);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        for (const auto &c : m_windowConnections)
            disconnect(c);
        m_windowConnections.clear();
        releaseNative();
        if (window) {
            prepareWindow(window);
            m_windowConnections.append(connect(window, &QQuickWindow::visibleChanged, this,
                                               &EchoGpuSurface::syncActivity));
            m_windowConnections.append(connect(window, &QQuickWindow::visibilityChanged, this,
                                               &EchoGpuSurface::syncActivity));
            m_windowConnections.append(connect(
                window, &QQuickWindow::sceneGraphInitialized, this, [this] { syncActivity(); },
                Qt::QueuedConnection));
        }
        syncActivity();
    });
}
EchoGpuSurface::~EchoGpuSurface()
{
    m_bridge->surface = nullptr;
    releaseTarget();
    releaseNative();
    if (m_graphics) {
        m_graphics->retainState(std::atomic_load(&m_renderState));
        m_graphics->detach(this);
    }
}
void EchoGpuSurface::setGraphics(EchoPluginGraphics *value)
{
    if (m_graphics == value)
        return;
    releaseTarget();
    releaseNative();
    if (m_graphics) {
        disconnect(m_graphics, nullptr, this, nullptr);
        m_graphics->retainState(std::atomic_load(&m_renderState));
        m_graphics->detach(this);
    }
    m_graphics = value;
    m_bridge->graphics = value;
    m_retired = false;
    m_frame.clear();
    ++m_revision;
    if (value) {
        if (!value->attach(this)) {
            m_graphics = nullptr;
            m_bridge->graphics = nullptr;
        } else {
            connect(value, &EchoPluginGraphics::resourcesChanged, this,
                    &EchoGpuSurface::requestFrame);
            connect(&value->scope(), &EchoPluginContext::activeChanged, this,
                    &EchoGpuSurface::syncActivity);
            connect(value, &QObject::destroyed, this, [this] {
                m_graphics = nullptr;
                retire();
                emit graphicsChanged();
            });
        }
    }
    emit graphicsChanged();
    syncActivity();
}
void EchoGpuSurface::setPaused(bool v)
{
    if (m_paused == v)
        return;
    m_paused = v;
    emit configurationChanged();
    syncActivity();
}
void EchoGpuSurface::setContinuous(bool v)
{
    if (m_continuous == v)
        return;
    m_continuous = v;
    emit configurationChanged();
    syncActivity();
}
void EchoGpuSurface::setFps(int v)
{
    v = qBound(1, v, 120);
    if (m_fps == v)
        return;
    m_fps = v;
    emit configurationChanged();
    syncActivity();
}
void EchoGpuSurface::setDynamicRange(const QString &v)
{
    if ((v != "auto" && v != "sdr" && v != "hdr") || v == m_dynamicRange)
        return;
    m_dynamicRange = v;
    if (m_status.value("hdrSwapChain").toBool() && m_status.value("floatTextures").toBool())
        setColorBufferFormat(v == "sdr" ? TextureFormat::RGBA8 : TextureFormat::RGBA16F);
    emit configurationChanged();
    requestFrame();
}
bool EchoGpuSurface::live() const
{
    return m_sizeAccepted && !m_retired && !m_paused && m_graphics && m_graphics->active() &&
           isVisible() && width() > 0 && height() > 0 && window() && window()->isVisible() &&
           window()->visibility() != QWindow::Minimized;
}
void EchoGpuSurface::syncActivity()
{
    if (m_sizing)
        return;
    QPointer<EchoGpuSurface> alive(this);
    m_sizing = true;
    const bool wants = !m_retired && !m_paused && m_graphics && m_graphics->active() &&
                       isVisible() && width() > 0 && height() > 0 && window() &&
                       window()->isVisible() && window()->visibility() != QWindow::Minimized;
    m_sizeAccepted = true;
    if (wants) {
        if (m_limited) {
            setFixedColorBufferWidth(0);
            if (!alive)
                return;
            setFixedColorBufferHeight(0);
            if (!alive)
                return;
            m_limited = false;
        }
        const auto dpr = window()->effectiveDevicePixelRatio();
        const double w =
            fixedColorBufferWidth() > 0 ? fixedColorBufferWidth() : std::ceil(width() * dpr);
        const double h =
            fixedColorBufferHeight() > 0 ? fixedColorBufferHeight() : std::ceil(height() * dpr);
        const bool valid = std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0 && w <= 4096 &&
                           h <= 4096 && w * h <= 8 * 1024 * 1024;
        const qint64 bytes = valid ? qint64(w * h) * 32 * qMax(1, sampleCount()) : 0;
        const auto budget = m_graphics->memoryBudget();
        if (valid && (bytes <= m_targetBytes || budget->targets.reserve(bytes - m_targetBytes))) {
            if (m_targetBytes > bytes)
                budget->targets.release(m_targetBytes - bytes);
            m_targetBudget = budget;
            m_targetBytes = bytes;
        } else {
            releaseTarget();
            m_sizeAccepted = false;
            m_limited = true;
            setFixedColorBufferWidth(1);
            if (!alive)
                return;
            setFixedColorBufferHeight(1);
            if (!alive)
                return;
            m_status["state"] = "error";
            m_status["available"] = false;
            m_status["reason"] = valid ? "Plugin render-target budget exceeded"
                                       : "GPU surface exceeds 4096 pixels per axis or 8 megapixels";
        }
    } else
        releaseTarget();
    const bool running = live();
    if (running && m_continuous) {
        m_clock.start(qMax(8, 1000 / m_fps));
    } else
        m_clock.stop();
    if (running)
        requestFrame();
    else {
        releaseNative();
        if (!alive)
            return;
        if (m_sizeAccepted)
            m_status["state"] = "suspended";
    }
    m_sizing = false;
    if (!running)
        emit statusChanged();
}
void EchoGpuSurface::releaseTarget()
{
    if (m_targetBudget && m_targetBytes)
        m_targetBudget->targets.release(m_targetBytes);
    m_targetBytes = 0;
    m_targetBudget.reset();
}
void EchoGpuSurface::releaseNative()
{
    const auto requests = std::exchange(m_readbacks, {});
    const auto state = std::atomic_load(&m_renderState);
    for (const auto &request : requests)
        if (request.second)
            request.second->cancel();
    if (!state)
        return;
    state->enabled = false;
    if (state->objects > 0 && state->window) {
        state->window->scheduleRenderJob(new ReleaseGpu(state), QQuickWindow::NoStage);
        state->window->update();
    }
}
void EchoGpuSurface::releaseResources()
{
    releaseNative();
    QQuickRhiItem::releaseResources();
}
bool EchoGpuSurface::drained() const
{
    const auto state = std::atomic_load(&m_renderState);
    return !state || state->objects == 0;
}
void EchoGpuSurface::retire()
{
    if (m_retired)
        return;
    m_retired = true;
    m_frame.clear();
    m_status["state"] = "suspended";
    m_status["available"] = false;
    releaseTarget();
    m_clock.stop();
    QPointer<EchoGpuSurface> alive(this);
    setVisible(false);
    if (!alive)
        return;
    releaseNative();
    if (alive)
        emit alive->statusChanged();
}
bool EchoGpuSurface::submit(const QVariantMap &frame)
{
    if (!m_graphics)
        return false;
    QString error;
    if (!m_graphics->validateFrame(frame, error))
        return m_graphics->fail(error);
    if (m_frame != frame) {
        m_frame = frame;
        ++m_revision;
    }
    requestFrame();
    return true;
}
void EchoGpuSurface::requestFrame()
{
    if (live() && (!m_continuous || !m_clock.isActive()))
        update();
}
EchoPluginOperation *EchoGpuSurface::readback(EchoGpuResource *resource)
{
    if (!m_graphics)
        return nullptr;
    auto *op = m_graphics->scope().operation();
    if (op->finished())
        return op;
    op->start();
    if (!live() || m_readbacks.size() >= 8 ||
        (resource && (!resource->active() || !m_graphics->descriptions().contains(resource->id()) ||
                      (resource->kind() != "buffer" && resource->kind() != "texture")))) {
        op->fail("InvalidResource", "Surface is inactive or resource is unavailable");
        return op;
    }
    m_readbacks.append({resource ? resource->id() : QString(), op});
    QTimer::singleShot(5000, op, [op] {
        if (!op->finished())
            op->fail("GraphicsTimeout", "GPU readback timed out");
    });
    requestFrame();
    return op;
}
EchoGpuSurface::Snapshot EchoGpuSurface::takeSnapshot()
{
    Snapshot result;
    result.live = live();
    result.hdr = m_dynamicRange != "sdr";
    result.pixelRatio = window() ? window()->effectiveDevicePixelRatio() : 1;
    result.frame = m_frame;
    result.frameRevision = m_revision;
    if (m_graphics) {
        result.resources = m_graphics->descriptions();
        result.budget = m_graphics->memoryBudget();
    }
    result.readbacks = std::exchange(m_readbacks, {});
    return result;
}
void EchoGpuSurface::acceptReport(const QVariantMap &report)
{
    if (m_retired || !m_graphics || !m_graphics->active())
        return;
    if (report.value("generation").toULongLong() < m_status.value("generation").toULongLong())
        return;
    QPointer<EchoGpuSurface> alive(this);
    const auto desired = report.value("hdrSwapChain").toBool() && m_dynamicRange != "sdr" &&
                                 report.value("floatTextures").toBool()
                             ? TextureFormat::RGBA16F
                             : TextureFormat::RGBA8;
    if (colorBufferFormat() != desired)
        setColorBufferFormat(desired);
    if (!alive)
        return;
    for (auto i = report.begin(); i != report.end(); ++i)
        m_status.insert(i.key(), i.value());
    m_graphics->report(m_status);
    if (alive)
        emit alive->statusChanged();
}
QQuickRhiItemRenderer *EchoGpuSurface::createRenderer()
{
    return new GpuRenderer(m_bridge);
}
} // namespace EchoMusic
