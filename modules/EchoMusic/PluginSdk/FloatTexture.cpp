#include "FloatTexture.h"
#include <QPointer>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGNode>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QtEndian>
#include <rhi/qrhi.h>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>

namespace EchoMusic
{
namespace
{
class FloatDataTexture final : public QSGTexture
{
public:
    QByteArray bytes;
    QSize size;
    QPointer<FloatTexture> owner;
    std::function<void(qint64, bool, const QString &)> report;
    std::unique_ptr<QRhiTexture> texture;
    bool pending = true;
    FloatDataTexture(QByteArray input, QSize extent) : bytes(std::move(input)), size(extent)
    {
        setFiltering(Nearest);
        setMipmapFiltering(None);
    }
    qint64 comparisonKey() const override
    {
        return qint64(quintptr(this));
    }
    QRhiTexture *rhiTexture() const override
    {
        return texture.get();
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
            report(0, false, QStringLiteral("RGBA32F texture format or dimensions unsupported"));
            bytes.clear();
            return;
        }
        texture.reset(rhi->newTexture(QRhiTexture::RGBA32F, size));
        if (!texture->create()) {
            texture.reset();
            report(0, false, QStringLiteral("RGBA32F texture allocation failed"));
            bytes.clear();
            return;
        }
        const qint64 count = bytes.size();
        QRhiTextureSubresourceUploadDescription payload(bytes);
        payload.setDataStride(size.width() * 16);
        updates->uploadTexture(texture.get(), QRhiTextureUploadDescription({{0, 0, payload}}));
        bytes.clear();
        report(count, true, {});
    }
};
class DeleteProvider final : public QRunnable
{
public:
    QObject *provider;
    explicit DeleteProvider(QObject *value) : provider(value) {}
    void run() override
    {
        delete provider;
    }
};
} // namespace
class FloatTexture::Provider final : public QSGTextureProvider
{
public:
    std::unique_ptr<FloatDataTexture> value;
    quint64 revision = std::numeric_limits<quint64>::max();
    QSGTexture *texture() const override
    {
        return value.get();
    }
    void replace(std::unique_ptr<FloatDataTexture> next)
    {
        value = std::move(next);
        emit textureChanged();
    }
};
FloatTexture::FloatTexture(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *value) {
        releaseResources();
        disconnect(m_invalidation);
        disconnect(m_visibility);
        m_textureWindow = value;
        ++m_revision;
        if (!value)
            return;
        m_invalidation = connect(
            value, &QQuickWindow::sceneGraphInvalidated, this,
            [this] {
                delete m_provider;
                m_provider = nullptr;
                m_gpuBytes.store(0);
            },
            Qt::DirectConnection);
        m_visibility = connect(
            value, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
                if (visibility == QWindow::Hidden || visibility == QWindow::Minimized)
                    clear();
            });
    });
}
FloatTexture::~FloatTexture()
{
    disconnect(m_invalidation);
    disconnect(m_visibility);
    releaseResources();
}
bool FloatTexture::canUpload() const
{
    return m_active && isVisible() &&
           (!window() || (window()->visibility() != QWindow::Hidden &&
                          window()->visibility() != QWindow::Minimized));
}
void FloatTexture::setError(const QString &value)
{
    if (m_error == value)
        return;
    m_error = value;
    emit errorChanged();
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
bool FloatTexture::upload(int width, int height, const QJSValue &arrayBuffer)
{
    const qint64 expected = qint64(width) * height * 16;
    if (!canUpload() || width < 1 || height < 1 || width > MaxDimension || height > MaxDimension ||
        expected > MaxBytes) {
        setError(QStringLiteral("FloatTexture inactive or extent exceeds its budget"));
        return false;
    }
    const QVariant input = arrayBuffer.toVariant();
    if (input.metaType() != QMetaType::fromType<QByteArray>()) {
        setError(QStringLiteral("FloatTexture upload requires an ArrayBuffer"));
        return false;
    }
    return acceptBytes(width, height, input.toByteArray());
}
bool FloatTexture::uploadPacked(int width, int height, const QJSValue &utf16Words)
{
    const qint64 expected = qint64(width) * height * 16;
    if (!canUpload() || width < 1 || height < 1 || width > MaxDimension || height > MaxDimension ||
        expected > MaxBytes || !utf16Words.isString()) {
        setError(QStringLiteral("FloatTexture packed upload requires active bounded UTF16 words"));
        return false;
    }
    const QString words = utf16Words.toString();
    if (qint64(words.size()) * 2 != expected) {
        setError(QStringLiteral("FloatTexture packed word length must equal width * height * 8"));
        return false;
    }
    QByteArray bytes(expected, Qt::Uninitialized);
    for (qsizetype i = 0; i < words.size(); ++i)
        qToLittleEndian<quint16>(words[i].unicode(), bytes.data() + i * 2);
    return acceptBytes(width, height, bytes);
}
bool FloatTexture::acceptBytes(int width, int height, const QByteArray &bytes)
{
    const qint64 expected = qint64(width) * height * 16;
    if (bytes.size() != expected) {
        setError(QStringLiteral("FloatTexture byte length must equal width * height * 16"));
        return false;
    }
    for (qsizetype offset = 0; offset < bytes.size(); offset += sizeof(float)) {
        float value;
        std::memcpy(&value, bytes.constData() + offset, sizeof value);
        if (!std::isfinite(value)) {
            setError(QStringLiteral("FloatTexture values must be finite float32"));
            return false;
        }
    }
    setError({});
    if (m_size == QSize(width, height) && m_bytes == bytes)
        return true;
    m_size = QSize(width, height);
    m_bytes = bytes;
    ++m_revision;
    emit contentChanged();
    update();
    return true;
}
void FloatTexture::clear()
{
    m_bytes.clear();
    m_size = {};
    ++m_revision;
    releaseResources();
    setError({});
    emit contentChanged();
    emit renderingChanged();
    update();
}
QSGTextureProvider *FloatTexture::textureProvider() const
{
    if (!m_provider)
        m_provider = new Provider;
    return m_provider;
}
void FloatTexture::rendered(quint64 revision, qint64 bytes, bool uploaded, const QString &error)
{
    if (uploaded)
        m_uploads.fetch_add(1);
    QMetaObject::invokeMethod(
        this,
        [this, revision, bytes, error] {
            if (revision != m_revision || !m_active || !isVisible())
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
    auto *provider = static_cast<Provider *>(textureProvider());
    if (provider->revision != m_revision) {
        provider->revision = m_revision;
        std::unique_ptr<FloatDataTexture> next;
        if (m_active && !m_bytes.isEmpty()) {
            next = std::make_unique<FloatDataTexture>(m_bytes, m_size);
            const QPointer<FloatTexture> guard(this);
            const quint64 revision = m_revision;
            next->report = [guard, revision](qint64 bytes, bool uploaded, const QString &error) {
                if (guard)
                    guard->rendered(revision, bytes, uploaded, error);
            };
        }
        provider->replace(std::move(next));
    }
    return node;
}
void FloatTexture::releaseResources()
{
    auto *provider = m_provider;
    m_provider = nullptr;
    m_gpuBytes.store(0);
    if (!provider)
        return;
    if (m_textureWindow)
        m_textureWindow->scheduleRenderJob(new DeleteProvider(provider),
                                           QQuickWindow::BeforeSynchronizingStage);
    else
        delete provider;
}
void FloatTexture::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged && !value.boolValue)
        clear();
}
} // namespace EchoMusic
