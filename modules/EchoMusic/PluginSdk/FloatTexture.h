#pragma once
#include "EchoPluginGraphics.h"
#include <QQuickItem>
#include <QPointer>
#include <QMutex>
#include <QtQml/qqmlregistration.h>
#include <atomic>

namespace EchoMusic
{
class ItemPresentationObserver;
/// Read-only RGBA32F ShaderEffect source; bytes never pass through image/color conversion.
class FloatTexture : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EchoMusic::EchoPluginGraphics *graphics READ graphics WRITE setGraphics NOTIFY
                   graphicsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int textureWidth READ textureWidth NOTIFY contentChanged)
    Q_PROPERTY(int textureHeight READ textureHeight NOTIFY contentChanged)
    Q_PROPERTY(qint64 sourceBytes READ sourceBytes NOTIFY contentChanged)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY renderingChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY renderingChanged)
    Q_PROPERTY(qulonglong textureUploads READ textureUploads NOTIFY renderingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
public:
    static constexpr int MaxDimension = 8192;
    static constexpr qint64 MaxBytes = 16LL * 1024 * 1024;
    explicit FloatTexture(QQuickItem *parent = nullptr);
    ~FloatTexture() override;
    EchoPluginGraphics *graphics() const
    {
        return m_graphics;
    }
    void setGraphics(EchoPluginGraphics *);
    bool active() const
    {
        return m_active;
    }
    void setActive(bool);
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
    bool ready() const
    {
        return cacheBytes() > 0 && m_error.isEmpty();
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
    Q_INVOKABLE bool uploadPacked(int width, int height, const QString &utf16Words);
    /// Canonical ASCII Base64 for exactly width * height RGBA32F pixels.
    Q_INVOKABLE bool uploadBase64(int width, int height, const QString &base64);
    Q_INVOKABLE void clear();
    bool isTextureProvider() const override
    {
        return true;
    }
    QSGTextureProvider *textureProvider() const override;
signals:
    void graphicsChanged();
    void activeChanged();
    void contentChanged();
    void renderingChanged();
    void errorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
    void releaseResources() override;

private:
    struct Native;
    struct Reservation;
    QPointer<EchoPluginGraphics> m_graphics;
    QList<QMetaObject::Connection> m_scopeConnections, m_windowConnections;
    ItemPresentationObserver *m_presentation;
    std::shared_ptr<Reservation> m_reservation;
    // Upload callbacks may outlive this item on the render thread. Only the
    // independent GUI relay may inspect the item's weak reference.
    std::shared_ptr<QObject> m_uploadRelay;
    mutable QMutex m_nativeMutex;
    mutable std::shared_ptr<Native> m_native;
    QByteArray m_bytes;
    QSize m_size;
    QString m_error;
    bool m_active = true;
    quint64 m_revision = 0;
    quint64 m_renderEpoch = 0;
    std::atomic<qint64> m_gpuBytes{0};
    std::atomic<quint64> m_uploads{0};
    std::shared_ptr<Native> native() const;
    void setError(const QString &);
    bool validateExtent(int, int);
    bool ownerPresented() const;
    bool acceptBytes(int, int, const QByteArray &);
    void rendered(quint64, qint64, bool, const QString &);
};
} // namespace EchoMusic
