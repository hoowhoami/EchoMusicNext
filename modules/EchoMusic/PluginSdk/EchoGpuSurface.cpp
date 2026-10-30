#include "EchoGpuSurface.h"
#include "ItemPresentationObserver_p.h"
#include "EchoPluginContext.h"
#include "graphics/GpuRhi_p.h"
#include <QQuickWindow>
#include <QRunnable>
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
} // namespace
EchoGpuSurface::EchoGpuSurface(QQuickItem *parent) : QQuickRhiItem(parent)
{
    setAlphaBlending(true);
    m_visibilityObserver = new ItemPresentationObserver(*this, [this] { syncActivity(); });
    m_status = {{"available", false},    {"compute", false},           {"hdr", false},
                {"hdrRequested", false}, {"localHdrSupported", false}, {"dynamicRange", "sdr"},
                {"state", "waiting"}};
    m_bridge = std::shared_ptr<GpuBridge>(new GpuBridge, [](GpuBridge *p) { p->deleteLater(); });
    m_bridge->surface = this;
    connect(&m_clock, &QTimer::timeout, this, [this] {
        if (live())
            update();
    });
    m_clock.setTimerType(Qt::PreciseTimer);
    const auto follow = [this](QMetaObject::Connection connection) {
        m_itemConnections.append(connection);
    };
    follow(connect(this, &QQuickItem::visibleChanged, this, &EchoGpuSurface::syncActivity));
    follow(connect(this, &QQuickItem::widthChanged, this, &EchoGpuSurface::syncActivity));
    follow(connect(this, &QQuickItem::heightChanged, this, &EchoGpuSurface::syncActivity));
    follow(connect(this, &QQuickRhiItem::fixedColorBufferWidthChanged, this,
                   &EchoGpuSurface::syncActivity));
    follow(connect(this, &QQuickRhiItem::fixedColorBufferHeightChanged, this,
                   &EchoGpuSurface::syncActivity));
    follow(connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        for (const auto &c : m_windowConnections)
            disconnect(c);
        m_windowConnections.clear();
        releaseNative();
        if (window) {
            m_windowConnections.append(connect(window, &QQuickWindow::afterAnimating, this, [this] {
                if (live() != m_lastLive ||
                    (this->window() &&
                     this->window()->effectiveDevicePixelRatio() != m_lastPixelRatio))
                    syncActivity();
            }));
            m_windowConnections.append(
                connect(window, &QWindow::opacityChanged, this, &EchoGpuSurface::syncActivity));
            m_windowConnections.append(connect(window, &QQuickWindow::visibleChanged, this,
                                               &EchoGpuSurface::syncActivity));
            m_windowConnections.append(connect(window, &QQuickWindow::visibilityChanged, this,
                                               &EchoGpuSurface::syncActivity));
            m_windowConnections.append(connect(
                window, &QQuickWindow::sceneGraphInitialized, this, [this] { syncActivity(); },
                Qt::QueuedConnection));
        }
        syncActivity();
    }));
}
EchoGpuSurface::~EchoGpuSurface()
{
    retireTextureSources();
    // QQuickItem detaches from its window after derived members are destroyed.
    // Its signals must no longer reach callbacks which access those members.
    for (const auto &connection : m_itemConnections)
        disconnect(connection);
    for (const auto &connection : m_windowConnections)
        disconnect(connection);
    disconnect(m_scopeConnection);
    delete m_visibilityObserver;
    m_clock.stop();
    if (m_graphics)
        disconnect(m_graphics, nullptr, this, nullptr);
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
    retireTextureSources();
    releaseTarget();
    releaseNative();
    if (m_graphics) {
        disconnect(m_graphics, nullptr, this, nullptr);
        m_graphics->retainState(std::atomic_load(&m_renderState));
        m_graphics->detach(this);
    }
    disconnect(m_scopeConnection);
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
            m_scopeConnection = connect(&value->scope(), &EchoPluginContext::activeChanged, this,
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
    emit configurationChanged();
    requestFrame();
}
bool EchoGpuSurface::live() const
{
    return m_sizeAccepted && !m_retired && !m_paused && m_graphics && m_graphics->active() &&
           ItemPresentationObserver::presented(*this) && width() > 0 && height() > 0 && window() &&
           window()->isVisible() && window()->visibility() != QWindow::Minimized &&
           window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
}
void EchoGpuSurface::syncActivity()
{
    if (m_sizing)
        return;
    QPointer<EchoGpuSurface> alive(this);
    if (window() &&
        window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        releaseTarget();
        releaseNative();
        m_clock.stop();
        m_lastLive = false;
        m_lastPixelRatio = window()->effectiveDevicePixelRatio();
        m_status = {{"state", "unavailable"},
                    {"available", false},
                    {"compute", false},
                    {"hdr", false},
                    {"hdrRequested", m_dynamicRange != "sdr"},
                    {"localHdrSupported", false},
                    {"dynamicRange", "sdr"},
                    {"reason", "Qt software renderer has no RHI"}};
        if (m_graphics)
            m_graphics->report(m_status);
        if (alive)
            emit statusChanged();
        return;
    }
    m_sizing = true;
    const bool wants = !m_retired && !m_paused && m_graphics && m_graphics->active() &&
                       ItemPresentationObserver::presented(*this) && width() > 0 && height() > 0 &&
                       window() && window()->isVisible() &&
                       window()->visibility() != QWindow::Minimized;
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
    m_lastLive = running;
    m_lastPixelRatio = window() ? window()->effectiveDevicePixelRatio() : 0;
    if (running && m_continuous) {
        const auto interval = qMax(8, 1000 / m_fps);
        if (!m_clock.isActive() || m_clock.interval() != interval)
            m_clock.start(interval);
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
    m_readbacks.clear();
    const auto requests = m_activeReadbacks;
    const auto submissions = m_inflight;
    for (const auto &weak : submissions)
        if (auto pending = weak.lock()) {
            pending->canceled = true;
            if (pending->operation)
                pending->operation->cancel();
        }
    m_inflight.clear();
    const auto queued = std::exchange(m_submissions, {});
    for (const auto &submission : queued) {
        submission->canceled = true;
        if (submission->operation)
            submission->operation->cancel();
    }
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
    retireTextureSources();
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
EchoPluginOperation *EchoGpuSurface::enqueue(const QVariantMap &commands)
{
    if (!m_graphics)
        return nullptr;
    auto *op = m_graphics->scope().operation();
    if (op->finished())
        return op;
    if (!live() || m_status.value("state") == "unavailable") {
        op->fail("Unavailable", "GPU submissions require a visible, unpaused RHI surface");
        return op;
    }
    auto submission = std::make_shared<GpuSubmission>();
    QString error;
    if (!m_graphics->prepareSubmission(commands, submission->frame, error)) {
        op->fail("InvalidArgument", error);
        return op;
    }
    submission->resources = m_graphics->descriptions();
    qint64 bytes = 0;
    QSet<GpuSharedResourceState *> sharedSnapshots;
    for (const auto &resource : submission->resources) {
        if (!resource.shared || !sharedSnapshots.contains(resource.shared.get()))
            bytes += resource.allocationBytes;
        if (resource.shared)
            sharedSnapshots.insert(resource.shared.get());
    }
    for (const auto &value : submission->frame.value("passes").toList())
        bytes += value.toMap().value("data").toByteArray().size();
    auto budget = m_graphics->memoryBudget();
    if (m_submissions.size() >= 8 || budget->pendingSubmissions >= 32 ||
        !budget->submissions.reserve(bytes)) {
        op->fail("Busy", "GPU submission queue or snapshot budget exceeded");
        return op;
    }
    submission->bytes = bytes;
    submission->budget = budget;
    ++budget->pendingSubmissions;
    submission->retired = [sink = m_bridge] { sink->report({}); };
    submission->operation = op;
    submission->serial = ++m_submissionSequence;
    const auto weak = std::weak_ptr<GpuSubmission>(submission);
    op->start([weak] {
        if (auto pending = weak.lock())
            pending->canceled = true;
    });
    connect(op, &EchoPluginOperation::completed, op, [weak, op] {
        if (op->state() != EchoPluginOperation::Succeeded)
            if (auto pending = weak.lock())
                pending->canceled = true;
    });
    QTimer::singleShot(5000, op, [op] {
        if (!op->finished())
            op->fail("GraphicsTimeout", "GPU submission timed out");
    });
    m_inflight.removeIf([](const auto &pending) { return pending.expired(); });
    m_inflight.append(submission);
    m_submissions.append(std::move(submission));
    emit m_graphics->changed();
    requestFrame();
    return op;
}
void EchoGpuSurface::retireResource(const QString &id)
{
    const auto submissions = m_inflight;
    for (const auto &weak : submissions)
        if (auto pending = weak.lock(); pending && pending->resources.contains(id)) {
            pending->canceled = true;
            if (pending->operation && !pending->operation->finished())
                pending->operation->cancel();
        }
    const auto reads = m_activeReadbacks;
    for (const auto &request : reads)
        if (request.first == id && request.second && !request.second->finished())
            request.second->cancel();
    requestFrame();
}
void EchoGpuSurface::requestFrame()
{
    if (live() && (!m_continuous || !m_clock.isActive()))
        update();
}
EchoPluginOperation *EchoGpuSurface::readback(EchoGpuResource *resource, const QVariantMap &options)
{
    if (!m_graphics)
        return nullptr;
    auto *op = m_graphics->scope().operation();
    if (op->finished())
        return op;
    op->start();
    if (!live() || m_activeReadbacks.size() >= 8 ||
        (resource && resource->m_description.textureSource) ||
        (resource && (!resource->active() || !m_graphics->descriptions().contains(resource->id()) ||
                      (resource->kind() != "buffer" && resource->kind() != "texture")))) {
        op->fail("InvalidResource", "Surface is inactive or resource is unavailable");
        return op;
    }
    const auto allowed = resource && resource->kind() == "buffer"
                             ? QStringList{"offset", "size"}
                             : QStringList{"level", "layer", "x", "y", "width", "height"};
    for (auto it = options.begin(); it != options.end(); ++it) {
        bool ok = false;
        const auto n = it.value().toDouble(&ok);
        if (!allowed.contains(it.key()) || !ok || it.value().metaType().id() == QMetaType::Bool ||
            !std::isfinite(n) || n != std::floor(n) || n < 0 || n > 16 * 1024 * 1024) {
            op->fail("InvalidArgument", "Invalid GPU readback region");
            return op;
        }
    }
    auto request = options;
    request["resource"] = resource ? resource->id() : QString();
    m_activeReadbacks.append({request.value("resource").toString(), op});
    const auto removeRead = [this, op] {
        m_activeReadbacks.removeIf(
            [op](const auto &read) { return !read.second || read.second == op; });
        m_readbacks.removeIf([op](const auto &read) { return !read.second || read.second == op; });
    };
    connect(op, &EchoPluginOperation::completed, this, removeRead);
    connect(op, &QObject::destroyed, this, removeRead);
    m_readbacks.append({request, op});
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
    while (!m_submissions.isEmpty() && m_submissions.first()->canceled)
        m_submissions.removeFirst();
    if (!m_submissions.isEmpty()) {
        result.submission = m_submissions.takeFirst();
        result.resources = result.submission->resources;
        result.frame = result.submission->frame;
        // A one-shot graph cannot alias the persistent graph's pipeline revision.
        result.frameRevision = (quint64(1) << 63) | result.submission->serial;
    }
    if (m_submissions.isEmpty())
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
