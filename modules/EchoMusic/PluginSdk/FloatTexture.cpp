#include "FloatTexture.h"
#include "EchoPluginContext.h"
#include "ItemPresentationObserver_p.h"
#include <QQuickWindow>
#include <QRunnable>
#include <QSGNode>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QtEndian>
#include <rhi/qrhi.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace EchoMusic
{
struct FloatTexture::Reservation
{
    std::shared_ptr<GpuMemoryBudget> budget;
    qint64 bytes;
    Reservation(std::shared_ptr<GpuMemoryBudget> value, qint64 size)
        : budget(std::move(value)), bytes(size)
    {
    }
    ~Reservation()
    {
        budget->native.release(bytes);
        --budget->floatTextures;
    }
};
struct FloatTexture::Native : std::enable_shared_from_this<Native>
{
    class Texture final : public QSGTexture
    {
    public:
        QByteArray bytes;
        QSize size;
        std::shared_ptr<Reservation> reservation;
        std::unique_ptr<QRhiTexture> value;
        std::function<void(qint64, bool, const QString &)> report;
        bool pending = true;
        qint64 comparisonKey() const override
        {
            return qint64(quintptr(this));
        }
        QRhiTexture *rhiTexture() const override
        {
            return value.get();
        }
        QSize textureSize() const override
        {
            return size;
        }
        bool hasAlphaChannel() const override
        {
            return true;
        }
        bool hasMipmaps() const override
        {
            return false;
        }
        void commitTextureOperations(QRhi *rhi, QRhiResourceUpdateBatch *updates) override
        {
            if (!pending)
                return;
            pending = false;
            if (!rhi->isTextureFormatSupported(QRhiTexture::RGBA32F) ||
                size.width() > rhi->resourceLimit(QRhi::TextureSizeMax) ||
                size.height() > rhi->resourceLimit(QRhi::TextureSizeMax)) {
                bytes.clear();
                report(0, false, "RGBA32F texture or extent unavailable");
                return;
            }
            if (!value) {
                value.reset(rhi->newTexture(QRhiTexture::RGBA32F, size));
                if (!value->create()) {
                    value.reset();
                    bytes.clear();
                    report(0, false, "RGBA32F allocation failed");
                    return;
                }
            }
            QRhiTextureSubresourceUploadDescription payload(bytes);
            payload.setDataStride(size.width() * 16);
            updates->uploadTexture(value.get(), QRhiTextureUploadDescription({{0, 0, payload}}));
            const auto count = bytes.size();
            bytes.clear();
            report(count, true, {});
        }
    };
    class Provider final : public QSGTextureProvider
    {
    public:
        std::unique_ptr<Texture> value;
        quint64 revision = std::numeric_limits<quint64>::max();
        QSGTexture *texture() const override
        {
            return value.get();
        }
        void replace(std::unique_ptr<Texture> next)
        {
            value = std::move(next);
            emit textureChanged();
        }
        void updated()
        {
            emit textureChanged();
        }
    };
    Provider *provider = nullptr; // Render thread only, including lifecycle callbacks.
    QPointer<QQuickWindow> window;
    std::atomic<bool> retired{false};
    QMetaObject::Connection stopping, invalidated;
    ~Native()
    {
        Q_ASSERT(!provider);
    }
    void release()
    {
        const auto keep = shared_from_this();
        delete std::exchange(provider, nullptr);
        if (retired) {
            QObject::disconnect(stopping);
            QObject::disconnect(invalidated);
        }
    }
    Provider *get()
    {
        if (!provider && !retired && window && window->isSceneGraphInitialized())
            provider = new Provider;
        return provider;
    }
    void follow(QQuickWindow *target)
    {
        window = target;
        // Window connections retain the render owner if a queued cleanup job is
        // discarded during shutdown. No native texture is then deleted on GUI.
        const auto self = shared_from_this();
        stopping = QObject::connect(
            target, &QQuickWindow::sceneGraphAboutToStop, target, [self] { self->release(); },
            Qt::DirectConnection);
        invalidated = QObject::connect(
            target, &QQuickWindow::sceneGraphInvalidated, target, [self] { self->release(); },
            Qt::DirectConnection);
    }
};
namespace
{
class ReleaseNative final : public QRunnable
{
    std::function<void()> m_release;

public:
    explicit ReleaseNative(std::function<void()> value) : m_release(std::move(value)) {}
    void run() override
    {
        m_release();
    }
};
} // namespace
FloatTexture::FloatTexture(QQuickItem *parent) : QQuickItem(parent), m_size(0, 0)
{
    setFlag(ItemHasContents, true);
    m_presentation = new ItemPresentationObserver(*this, [this] {
        if (!ownerPresented())
            clear();
    });
    connect(this, &QQuickItem::windowChanged, this, [this] {
        for (const auto &connection : m_windowConnections)
            disconnect(connection);
        m_windowConnections.clear();
        if (window())
            m_windowConnections.append(connect(window(), &QWindow::visibilityChanged, this, [this] {
                if (!window() || !window()->isVisible() ||
                    window()->visibility() == QWindow::Minimized)
                    clear();
            }));
        if (window())
            m_windowConnections.append(connect(window(), &QWindow::opacityChanged, this, [this] {
                if (!window() || window()->opacity() <= 0)
                    clear();
            }));
        releaseResources();
        update();
    });
}
FloatTexture::~FloatTexture()
{
    // QQuickItem detaches from its window after derived members are destroyed.
    // Its notification must not reenter our already-destroyed connection list.
    disconnect(this, &QQuickItem::windowChanged, this, nullptr);
    for (const auto &connection : m_windowConnections)
        disconnect(connection);
    m_windowConnections.clear();
    for (const auto &connection : m_scopeConnections)
        disconnect(connection);
    m_scopeConnections.clear();
    delete m_presentation;
    releaseResources();
}
void FloatTexture::setGraphics(EchoPluginGraphics *value)
{
    if (m_graphics == value)
        return;
    clear();
    for (const auto &connection : m_scopeConnections)
        disconnect(connection);
    m_scopeConnections.clear();
    m_graphics = value;
    if (value) {
        m_scopeConnections.append(connect(&value->scope(), &EchoPluginContext::sessionInvalidated,
                                          this, &FloatTexture::clear));
        m_scopeConnections.append(connect(value, &EchoPluginGraphics::activeChanged, this, [this] {
            if (!m_graphics || !m_graphics->active())
                clear();
        }));
        m_scopeConnections.append(connect(value, &QObject::destroyed, this, &FloatTexture::clear));
    }
    emit graphicsChanged();
}
void FloatTexture::setActive(bool value)
{
    if (m_active == value)
        return;
    m_active = value;
    if (!value)
        clear();
    emit activeChanged();
    update();
}
void FloatTexture::setError(const QString &value)
{
    if (m_error == value)
        return;
    m_error = value;
    emit errorChanged();
    emit renderingChanged();
}
bool FloatTexture::ownerPresented() const
{
    if (!isVisible() || opacity() <= 0)
        return false;
    // This item supplies data and draws no pixels. Its one-pixel source cell
    // may sit outside a cropped preview while its owning renderer is visible.
    // Gate resources on that enclosing presentation rather than the cell's x/y.
    const QQuickItem *owner = parentItem();
    while (owner && owner->boundingRect().isEmpty() && !owner->clip())
        owner = owner->parentItem();
    return ItemPresentationObserver::presented(owner ? *owner : *this);
}
bool FloatTexture::validateExtent(int width, int height)
{
    bool presented = true;
    if (window()) {
        presented = ownerPresented();
    } else {
        // A bounded source may be prepared before attachment; an explicitly
        // transparent/hidden ancestor still must not accumulate new data.
        for (QQuickItem *item = this; item; item = item->parentItem())
            presented = presented && item->isVisible() && item->opacity() > 0;
    }
    if (!m_active || !m_graphics || !m_graphics->active() || !isVisible() || !presented ||
        (window() && (window()->visibility() == QWindow::Hidden ||
                      window()->visibility() == QWindow::Minimized)) ||
        width < 1 || height < 1 || width > MaxDimension || height > MaxDimension ||
        qint64(width) * height * 16 > MaxBytes) {
        setError("FloatTexture requires active graphics and an extent within its budget");
        return false;
    }
    return true;
}
bool FloatTexture::upload(int width, int height, const QJSValue &data)
{
    if (!validateExtent(width, height))
        return false;
    const QVariant value = data.toVariant();
    if (value.metaType() != QMetaType::fromType<QByteArray>()) {
        setError("FloatTexture upload requires an ArrayBuffer");
        return false;
    }
    return acceptBytes(width, height, value.toByteArray());
}
bool FloatTexture::uploadPacked(int width, int height, const QString &words)
{
    if (!validateExtent(width, height))
        return false;
    const auto expected = qint64(width) * height * 16;
    if (qint64(words.size()) * 2 != expected) {
        setError("Packed length must equal width * height * 8");
        return false;
    }
    QByteArray bytes(expected, Qt::Uninitialized);
    for (qsizetype i = 0; i < words.size(); ++i)
        qToLittleEndian<quint16>(words[i].unicode(), bytes.data() + i * 2);
    return acceptBytes(width, height, bytes);
}
bool FloatTexture::uploadBase64(int width, int height, const QString &base64)
{
    if (!validateExtent(width, height))
        return false;
    const qint64 expected = qint64(width) * height * 16;
    if (base64.size() != (expected + 2) / 3 * 4) {
        setError("Base64 length must match width * height * 16 bytes");
        return false;
    }
    // Latin-1 conversion cannot turn a non-ASCII character into a Base64
    // character. The strict decoder and canonical re-encoding also reject
    // whitespace and unused nonzero padding bits before accepting any data.
    const auto encoded = base64.toLatin1();
    const auto decoded =
        QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() != expected || decoded.decoded.toBase64() != encoded) {
        setError("FloatTexture requires canonical ASCII Base64");
        return false;
    }
    return acceptBytes(width, height, decoded.decoded);
}
bool FloatTexture::acceptBytes(int width, int height, const QByteArray &bytes)
{
    const auto expected = qint64(width) * height * 16;
    if (bytes.size() != expected) {
        setError("Byte length must equal width * height * 16");
        return false;
    }
    for (qsizetype offset = 0; offset < bytes.size(); offset += sizeof(float)) {
        float value;
        std::memcpy(&value, bytes.constData() + offset, sizeof(value));
        if (!std::isfinite(value)) {
            setError("FloatTexture values must be finite float32");
            return false;
        }
    }
    if (m_size == QSize(width, height) && m_bytes == bytes) {
        setError({});
        return true;
    }
    auto budget = m_graphics->memoryBudget();
    if (!budget->native.reserve(expected)) {
        setError("Plugin native graphics budget exhausted");
        return false;
    }
    if (budget->floatTextures.fetch_add(1) >= 256) {
        --budget->floatTextures;
        budget->native.release(expected);
        setError("Plugin float texture count exhausted");
        return false;
    }
    m_reservation = std::make_shared<Reservation>(std::move(budget), expected);
    m_size = QSize(width, height);
    m_bytes = bytes;
    ++m_revision;
    setError({});
    emit contentChanged();
    emit renderingChanged();
    update();
    return true;
}
void FloatTexture::clear()
{
    m_bytes.clear();
    m_size = {0, 0};
    ++m_revision;
    ++m_renderEpoch;
    m_gpuBytes.store(0);
    std::shared_ptr<Native> state;
    {
        QMutexLocker lock(&m_nativeMutex);
        state = m_native;
    }
    if (state && state->window) {
        const auto revision = m_revision;
        state->window->scheduleRenderJob(new ReleaseNative([state, revision] {
                                             // Keep the provider stable for ShaderEffect sampler
                                             // bindings. Retire only its data, including when the
                                             // window is not exposed.
                                             if (state->provider &&
                                                 state->provider->revision <= revision) {
                                                 state->provider->revision = revision;
                                                 state->provider->replace({});
                                             }
                                         }),
                                         QQuickWindow::NoStage);
    }
    m_reservation.reset();
    setError({});
    emit contentChanged();
    emit renderingChanged();
    update();
}
std::shared_ptr<FloatTexture::Native> FloatTexture::native() const
{
    QMutexLocker lock(&m_nativeMutex);
    if (!m_native && window()) {
        m_native = std::make_shared<Native>();
        m_native->follow(window());
    }
    return m_native;
}
QSGTextureProvider *FloatTexture::textureProvider() const
{
    const auto state = native();
    return state ? state->get() : nullptr;
}
void FloatTexture::rendered(quint64 epoch, qint64 bytes, bool uploaded, const QString &error)
{
    if (uploaded)
        m_uploads.fetch_add(1);
    QMetaObject::invokeMethod(
        this,
        [this, epoch, bytes, error] {
            // New frame data may already be queued when this upload report
            // reaches the GUI. It still reports a real usable allocation.
            // Retirement, unlike a payload update, invalidates every old report.
            if (epoch != m_renderEpoch || !m_active || !isVisible())
                return;
            m_gpuBytes.store(bytes);
            if (!error.isEmpty())
                setError(error);
            emit renderingChanged();
        },
        Qt::QueuedConnection);
}
QSGNode *FloatTexture::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    if (!node)
        node = new QSGNode;
    const auto state = native();
    auto *provider = state ? state->get() : nullptr;
    if (provider && provider->revision != m_revision) {
        provider->revision = m_revision;
        if (m_active && !m_bytes.isEmpty()) {
            const bool reuse = provider->value && provider->value->size == m_size;
            if (!reuse)
                provider->value = std::make_unique<Native::Texture>();
            auto *texture = provider->value.get();
            texture->bytes = m_bytes;
            texture->size = m_size;
            texture->reservation = m_reservation;
            texture->pending = true;
            texture->setFiltering(QSGTexture::Nearest);
            texture->setMipmapFiltering(QSGTexture::None);
            const QPointer<FloatTexture> guard(this);
            const auto epoch = m_renderEpoch;
            texture->report = [guard, epoch](qint64 bytes, bool uploaded, const QString &error) {
                if (guard)
                    guard->rendered(epoch, bytes, uploaded, error);
            };
            provider->updated();
        } else
            provider->replace({});
    }
    return node;
}
void FloatTexture::releaseResources()
{
    ++m_revision;
    ++m_renderEpoch;
    std::shared_ptr<Native> state;
    {
        QMutexLocker lock(&m_nativeMutex);
        state = std::exchange(m_native, {});
    }
    m_gpuBytes.store(0);
    if (!state)
        return;
    state->retired = true;
    if (state->window)
        state->window->scheduleRenderJob(new ReleaseNative([state] { state->release(); }),
                                         QQuickWindow::BeforeSynchronizingStage);
}
} // namespace EchoMusic
