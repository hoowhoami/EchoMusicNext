#pragma once
#include <QByteArray>
#include <QJSValue>
#include <QQuickItem>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <atomic>

namespace EchoMusic
{
/// Immutable RGBA32F data for public ShaderEffect samplers. No colour conversion or atlas packing.
class FloatTexture : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int textureWidth READ textureWidth NOTIFY contentChanged)
    Q_PROPERTY(int textureHeight READ textureHeight NOTIFY contentChanged)
    Q_PROPERTY(qint64 sourceBytes READ sourceBytes NOTIFY contentChanged)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY renderingChanged)
    Q_PROPERTY(qulonglong textureUploads READ textureUploads NOTIFY renderingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
public:
    static constexpr int MaxDimension = 8192;
    static constexpr qint64 MaxBytes = 16LL * 1024 * 1024;
    explicit FloatTexture(QQuickItem *parent = nullptr);
    ~FloatTexture() override;
    bool active() const
    {
        return m_active;
    }
    void setActive(bool value);
    int textureWidth() const
    {
        return m_size.width();
    }
    int textureHeight() const
    {
        return m_size.height();
    }
    qint64 sourceBytes() const
    {
        return m_bytes.size();
    }
    qint64 cacheBytes() const
    {
        return m_gpuBytes.load();
    }
    qulonglong textureUploads() const
    {
        return m_uploads.load();
    }
    QString errorString() const
    {
        return m_error;
    }
    Q_INVOKABLE bool upload(int width, int height, const QJSValue &arrayBuffer);
    Q_INVOKABLE bool uploadPacked(int width, int height, const QJSValue &utf16Words);
    Q_INVOKABLE void clear();
    bool isTextureProvider() const override
    {
        return true;
    }
    QSGTextureProvider *textureProvider() const override;
signals:
    void activeChanged();
    void contentChanged();
    void renderingChanged();
    void errorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
    void releaseResources() override;
    void itemChange(ItemChange, const ItemChangeData &) override;

private:
    class Provider;
    mutable Provider *m_provider = nullptr;
    QByteArray m_bytes;
    QSize m_size;
    QString m_error;
    QPointer<QQuickWindow> m_textureWindow;
    QMetaObject::Connection m_invalidation;
    QMetaObject::Connection m_visibility;
    bool m_active = true;
    quint64 m_revision = 0;
    std::atomic<qint64> m_gpuBytes{0};
    std::atomic<quint64> m_uploads{0};
    void setError(const QString &);
    bool acceptBytes(int width, int height, const QByteArray &bytes);
    bool canUpload() const;
    void rendered(quint64 revision, qint64 bytes, bool uploaded, const QString &error = {});
};
} // namespace EchoMusic
