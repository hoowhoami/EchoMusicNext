#include "ImageCapture.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QQmlEngine>
#include <QTemporaryFile>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrentRun>
#include <atomic>
#include <cmath>

namespace EchoMusic
{
namespace
{
class CaptureNotifier : public QObject
{
    Q_OBJECT
signals:
    void activity();
    void finished(std::shared_ptr<EchoMusic::ImageCaptureJob> job,
                  std::shared_ptr<EchoMusic::ImageCaptureArtifact> artifact, QString error);
};
struct CaptureBudget
{
    QMutex mutex;
    ImageCapture::BudgetSnapshot values;
    QList<std::shared_ptr<void>> failedCleanup;
    CaptureNotifier notifier;
    // Destroy/join the pool before the notifier, only at process shutdown.
    QThreadPool pool;
    CaptureBudget()
    {
        pool.setMaxThreadCount(ImageCapture::MaxJobs);
    }
};
CaptureBudget &budget()
{
    static CaptureBudget value;
    return value;
}

struct CaptureState
{
    std::atomic<int> pending{0}, leases{0}, cleanup{0};
    std::atomic<qint64> reserved{0}, encoded{0};
    void notify()
    {
        emit budget().notifier.activity();
    }
};

class BoundedFile final : public QFile
{
public:
    using QFile::QFile;

protected:
    qint64 writeData(const char *data, qint64 size) override
    {
        if (size < 0 || pos() > ImageCapture::MaxEncodedBytes - size) {
            setErrorString("Encoded image exceeds the per-image byte budget");
            return -1;
        }
        return QFile::writeData(data, size);
    }
};
} // namespace

struct ImageCaptureSlot
{
    ~ImageCaptureSlot()
    {
        auto &global = budget();
        QMutexLocker lock(&global.mutex);
        --global.values.leaseCount;
    }
};

struct ImageCaptureArtifact
{
    QString path;
    qint64 bytes = 0;
    std::shared_ptr<CaptureState> state;
    std::shared_ptr<ImageCaptureSlot> slot;
    ~ImageCaptureArtifact()
    {
        ++state->cleanup;
        const auto file = path;
        const auto amount = bytes;
        const auto context = state;
        const auto heldSlot = slot;
        (void)QtConcurrent::run(&budget().pool, [file, amount, context, heldSlot] {
            if (!QFile::remove(file) && QFile::exists(file)) {
                // Fail closed: an I/O failure must not silently refund a file that still exists.
                const auto held = std::make_shared<
                    std::pair<std::shared_ptr<CaptureState>, std::shared_ptr<ImageCaptureSlot>>>(
                    context, heldSlot);
                {
                    auto &global = budget();
                    QMutexLocker lock(&global.mutex);
                    global.failedCleanup.append(held);
                }
                qWarning(
                    "ImageCapture could not remove its private output; budget remains charged");
                context->notify();
                return;
            }
            {
                auto &global = budget();
                QMutexLocker lock(&global.mutex);
                global.values.encodedBytes -= amount;
            }
            context->encoded -= amount;
            --context->cleanup;
            context->notify();
        });
    }
};

struct ImageCaptureJob
{
    std::shared_ptr<CaptureState> state;
    std::shared_ptr<ImageCaptureSlot> slot;
    QPointer<ImageCaptureLease> lease;
    QPointer<QQuickItem> source;
    QPointer<QQuickWindow> window;
    QSharedPointer<QQuickItemGrabResult> grab;
    QPointer<QTimer> timer;
    QMetaObject::Connection readyConnection, sourceConnection, windowConnection;
    QMetaObject::Connection syncConnection, renderedConnection, invalidationConnection;
    std::atomic<bool> cancelled{false}, resourcesReleased{false};
    std::atomic<bool> synchronized{false}, rendered{false};
    bool encoding = false, ownsOutputReservation = true;
    qint64 reservation = 0;
    QSize size;
    QByteArray format;
    int quality = 92;
    qreal dpr = 1;

    void disconnectReadback()
    {
        QObject::disconnect(readyConnection);
        QObject::disconnect(sourceConnection);
        QObject::disconnect(windowConnection);
        QObject::disconnect(syncConnection);
        QObject::disconnect(renderedConnection);
        QObject::disconnect(invalidationConnection);
        readyConnection = {};
        sourceConnection = {};
        windowConnection = {};
        syncConnection = {};
        renderedConnection = {};
        invalidationConnection = {};
        if (timer) {
            timer->stop();
            QObject::disconnect(timer, nullptr, nullptr, nullptr);
            timer->deleteLater();
            timer = nullptr;
        }
        grab.reset();
    }
    void releaseResources()
    {
        if (resourcesReleased.exchange(true))
            return;
        {
            auto &global = budget();
            QMutexLocker lock(&global.mutex);
            --global.values.pendingJobs;
            global.values.reservedBytes -= reservation;
            if (ownsOutputReservation)
                global.values.outputReservedBytes -= ImageCapture::MaxEncodedBytes;
        }
        --state->pending;
        state->reserved -= reservation;
        slot.reset();
        state->notify();
    }
    ~ImageCaptureJob()
    {
        releaseResources();
    }
};

struct ImageCapture::Data
{
    bool active = true;
    qulonglong readbacks = 0, encodes = 0;
    std::shared_ptr<CaptureState> state = std::make_shared<CaptureState>();
    QList<std::shared_ptr<ImageCaptureJob>> jobs;
};

ImageCaptureLease::ImageCaptureLease(ImageCapture *owner, int width, int height)
    : QObject(owner), m_owner(owner), m_width(width), m_height(height)
{
}
ImageCaptureLease::~ImageCaptureLease()
{
    dispose();
}
qint64 ImageCaptureLease::encodedBytes() const
{
    return m_artifact ? m_artifact->bytes : 0;
}
void ImageCaptureLease::dispose()
{
    if (m_status == "disposed")
        return;
    m_status = "disposed";
    m_url = QUrl();
    m_error.clear();
    m_artifact.reset();
    if (m_owner)
        m_owner->releaseLease(this);
    m_slot.reset();
    emit changed();
    deleteLater();
}

ImageCapture::ImageCapture(QObject *parent) : QObject(parent), d(std::make_unique<Data>())
{
    auto &notifier = budget().notifier;
    connect(&notifier, &CaptureNotifier::activity, this, &ImageCapture::changed,
            Qt::QueuedConnection);
    connect(
        &notifier, &CaptureNotifier::finished, this,
        [this](const std::shared_ptr<ImageCaptureJob> &job,
               const std::shared_ptr<ImageCaptureArtifact> &artifact, const QString &error) {
            if (job->state == d->state)
                complete(job, artifact, error);
        },
        Qt::QueuedConnection);
}
ImageCapture::~ImageCapture()
{
    setActive(false);
}
bool ImageCapture::active() const
{
    return d->active;
}
int ImageCapture::pendingJobs() const
{
    return d->state->pending.load();
}
int ImageCapture::leaseCount() const
{
    return d->state->leases.load();
}
qint64 ImageCapture::reservedBytes() const
{
    return d->state->reserved.load();
}
qint64 ImageCapture::encodedBytes() const
{
    return d->state->encoded.load();
}
qulonglong ImageCapture::readbacks() const
{
    return d->readbacks;
}
qulonglong ImageCapture::encodes() const
{
    return d->encodes;
}
bool ImageCapture::isDrained() const
{
    return pendingJobs() == 0 && leaseCount() == 0 && d->state->cleanup.load() == 0;
}
ImageCapture::BudgetSnapshot ImageCapture::budgetSnapshot()
{
    auto &global = budget();
    QMutexLocker lock(&global.mutex);
    return global.values;
}
void ImageCapture::setActive(bool value)
{
    if (value == d->active)
        return;
    d->active = value;
    if (!value) {
        const auto leases = findChildren<ImageCaptureLease *>(Qt::FindDirectChildrenOnly);
        for (auto *lease : leases)
            lease->dispose();
        const auto jobs = d->jobs;
        for (const auto &job : jobs)
            cancel(job, "Image capture is inactive");
    }
    emit activeChanged();
    emit changed();
}

void ImageCapture::releaseLease(ImageCaptureLease *lease)
{
    if (lease->m_counted) {
        lease->m_counted = false;
        --d->state->leases;
    }
    if (const auto job = lease->m_job.lock())
        cancel(job, {});
    emit changed();
}

void ImageCapture::cancel(const std::shared_ptr<ImageCaptureJob> &job, const QString &error)
{
    job->cancelled = true;
    const bool readbackCanRelease = !job->synchronized || job->rendered;
    if (job->encoding || readbackCanRelease)
        job->disconnectReadback();
    if (job->lease && job->lease->m_status == "pending" && !error.isEmpty()) {
        job->lease->m_status = "error";
        job->lease->m_error = error;
        emit job->lease->changed();
    }
    if (!job->encoding && readbackCanRelease) {
        job->releaseResources();
        d->jobs.removeOne(job);
    }
    emit changed();
}

void ImageCapture::complete(const std::shared_ptr<ImageCaptureJob> &job,
                            const std::shared_ptr<ImageCaptureArtifact> &artifact,
                            const QString &error)
{
    job->disconnectReadback();
    if (!job->cancelled && d->active && job->lease && job->lease->m_status == "pending") {
        auto *lease = job->lease.data();
        if (artifact) {
            lease->m_artifact = artifact;
            lease->m_url = QUrl::fromLocalFile(artifact->path);
            lease->m_status = "ready";
        } else {
            lease->m_error = error.isEmpty() ? "Image encoding failed" : error;
            lease->m_status = "error";
        }
        emit lease->changed();
    }
    d->jobs.removeOne(job);
    emit changed();
}

ImageCaptureLease *ImageCapture::encode(QQuickItem *source, int width, int height,
                                        const QString &requestedFormat, int quality)
{
    auto *lease = new ImageCaptureLease(this, width, height);
    QQmlEngine::setObjectOwnership(lease, QQmlEngine::CppOwnership);
    const auto reject = [&](const QString &message) {
        lease->m_error = message;
        lease->m_status = "error";
        return lease;
    };
    if (!d->active)
        return reject("Image capture is inactive");
    if (!source || source->thread() != thread())
        return reject("Image capture needs a source item on its GUI thread");
    if (width < 1 || height < 1 || width > MaxDimension || height > MaxDimension ||
        qint64(width) * height > MaxPixels)
        return reject("Image capture pixel dimensions exceed the budget");
    const QByteArray format = requestedFormat.toLatin1().toLower();
    if (format != "jpeg" && format != "png")
        return reject("Unsupported image format");
    if (quality < 0 || quality > 100)
        return reject("Image quality must be between 0 and 100");
    auto *window = source->window();
    if (!window || !window->isVisible() || !window->isExposed() || source->width() <= 0 ||
        source->height() <= 0)
        return reject("Image capture source needs an exposed window and nonempty bounds");
    const qreal dpr = window->effectiveDevicePixelRatio();
    if (!std::isfinite(dpr) || dpr <= 0 || dpr > 8)
        return reject("Unsupported image capture device pixel ratio");
    // Allow one logical pixel of DPR rounding while reserving for all three image buffers.
    const qint64 reservation = qint64(width + qCeil(dpr)) * (height + qCeil(dpr)) * 4 * 3;
    auto &global = budget();
    {
        QMutexLocker lock(&global.mutex);
        const auto &used = global.values;
        if (used.pendingJobs >= MaxJobs || used.leaseCount >= MaxLeases ||
            used.reservedBytes + reservation > MaxReservedBytes ||
            used.encodedBytes + used.outputReservedBytes + MaxEncodedBytes > MaxTotalEncodedBytes)
            return reject("Image capture global budget is exhausted");
        ++global.values.pendingJobs;
        ++global.values.leaseCount;
        global.values.reservedBytes += reservation;
        global.values.outputReservedBytes += MaxEncodedBytes;
    }
    auto job = std::make_shared<ImageCaptureJob>();
    job->state = d->state;
    job->slot = std::make_shared<ImageCaptureSlot>();
    job->lease = lease;
    job->source = source;
    job->window = window;
    job->size = {width, height};
    job->reservation = reservation;
    job->dpr = dpr;
    job->format = format;
    job->quality = quality;
    lease->m_job = job;
    lease->m_slot = job->slot;
    lease->m_counted = true;
    ++d->state->leases;
    ++d->state->pending;
    d->state->reserved += reservation;
    d->jobs.append(job);
    job->grab = source->grabToImage({qMax(1, qRound(width / dpr)), qMax(1, qRound(height / dpr))});
    if (!job->grab) {
        cancel(job, "Scenegraph image capture could not start");
        return lease;
    }
    job->sourceConnection = connect(source, &QObject::destroyed, this, [this, job] {
        cancel(job, "Image capture source was destroyed");
    });
    job->windowConnection = connect(source, &QQuickItem::windowChanged, this, [this, job] {
        cancel(job, "Image capture source changed window");
    });
    job->timer = new QTimer(this);
    job->timer->setSingleShot(true);
    connect(job->timer, &QTimer::timeout, this,
            [this, job] { cancel(job, "Image capture timed out"); });
    job->timer->start(10000);
    const QPointer<ImageCapture> owner(this);
    auto *notifier = &budget().notifier;
    // GUI cannot run cancellation during Qt's synchronization. These markers distinguish
    // a not-yet-started weak grab from a live render/readback that must retain its ticket.
    job->syncConnection = connect(
        window, &QQuickWindow::beforeSynchronizing, notifier, [job] { job->synchronized = true; },
        Qt::DirectConnection);
    job->renderedConnection = connect(
        window, &QQuickWindow::afterRendering, notifier, [job] { job->rendered = true; },
        Qt::DirectConnection);
    job->invalidationConnection = connect(
        window, &QQuickWindow::sceneGraphInvalidated, notifier,
        [job, owner, notifier] {
            job->rendered = true;
            QMetaObject::invokeMethod(
                notifier,
                [job, owner] {
                    if (owner)
                        owner->cancel(job, "Image capture scenegraph was invalidated");
                    else {
                        job->cancelled = true;
                        job->disconnectReadback();
                        if (!job->encoding)
                            job->releaseResources();
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::DirectConnection);
    job->readyConnection =
        connect(job->grab.data(), &QQuickItemGrabResult::ready, notifier, [job, owner] {
            job->rendered = true;
            if (owner)
                owner->readbackReady(job);
            else {
                job->cancelled = true;
                job->disconnectReadback();
                job->releaseResources();
            }
        });
    emit changed();
    return lease;
}
void ImageCapture::readbackReady(const std::shared_ptr<ImageCaptureJob> &job)
{
    ++d->readbacks;
    if (job->cancelled || !d->active || !job->source || job->source->window() != job->window) {
        cancel(job, "Image capture source is no longer current");
        return;
    }
    QImage image = job->grab->image();
    if (image.isNull() || std::abs(image.width() - job->size.width()) > qCeil(job->dpr) ||
        std::abs(image.height() - job->size.height()) > qCeil(job->dpr)) {
        cancel(job, "Scenegraph readback returned unexpected physical dimensions");
        return;
    }
    // Readback already completed. The worker owns the immutable QImage from here.
    QObject::disconnect(job->readyConnection);
    job->grab.reset();
    job->encoding = true;
    ++d->encodes;
    emit changed();
    (void)QtConcurrent::run(&budget().pool, [job, image = std::move(image)]() mutable {
        Q_ASSERT(QThread::currentThread() != budget().notifier.thread());
        QString error;
        std::shared_ptr<ImageCaptureArtifact> artifact;
        if (!job->cancelled) {
            image.setDevicePixelRatio(1);
            if (image.size() != job->size)
                image = image.scaled(job->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QTemporaryFile temporary(QDir::tempPath() + "/echo-plugin-capture-XXXXXX." +
                                     QString::fromLatin1(job->format));
            if (!temporary.open()) {
                error = "Could not create private image capture file";
            } else {
                const auto path = temporary.fileName();
                temporary.close();
                BoundedFile output(path);
                if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    error = "Could not open image capture output";
                } else {
                    QImageWriter writer(&output, job->format);
                    if (job->format == "jpeg")
                        writer.setQuality(job->quality);
                    const bool ok = writer.write(image) && output.flush();
                    const qint64 bytes = output.size();
                    output.close();
                    if (!ok || bytes <= 0 || bytes > MaxEncodedBytes) {
                        error = "Image encoding failed or exceeded the byte budget: " +
                                writer.errorString();
                    } else if (!job->cancelled) {
                        artifact = std::make_shared<ImageCaptureArtifact>();
                        artifact->path = path;
                        artifact->bytes = bytes;
                        artifact->state = job->state;
                        artifact->slot = job->slot;
                        temporary.setAutoRemove(false);
                        {
                            auto &global = budget();
                            QMutexLocker lock(&global.mutex);
                            global.values.outputReservedBytes -= MaxEncodedBytes;
                            global.values.encodedBytes += bytes;
                            job->ownsOutputReservation = false;
                        }
                        job->state->encoded += bytes;
                    }
                }
            }
        }
        // A cancelled output is never published; its file stays charged until unlink.
        if (job->cancelled)
            artifact.reset();
        image = QImage();
        job->releaseResources();
        emit budget().notifier.finished(job, artifact, error);
    });
}

} // namespace EchoMusic
#include "ImageCapture.moc"
