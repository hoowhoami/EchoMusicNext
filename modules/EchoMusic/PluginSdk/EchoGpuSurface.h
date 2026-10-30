#pragma once
#include "EchoPluginGraphics.h"
#include "EchoPluginOperation.h"
#include <QQuickRhiItem>
#include <QTimer>
namespace EchoMusic
{
struct GpuRenderState;
struct GpuBridge;
/// A scene-graph item backed by the host's RHI, never a JS render-thread callback.
class EchoGpuSurface : public QQuickRhiItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EchoMusic::EchoPluginGraphics *graphics READ graphics WRITE setGraphics NOTIFY
                   graphicsChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY configurationChanged)
    Q_PROPERTY(bool continuous READ continuous WRITE setContinuous NOTIFY configurationChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY configurationChanged)
    Q_PROPERTY(
        QString dynamicRange READ dynamicRange WRITE setDynamicRange NOTIFY configurationChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
public:
    explicit EchoGpuSurface(QQuickItem *parent = nullptr);
    ~EchoGpuSurface() override;
    EchoPluginGraphics *graphics() const
    {
        return m_graphics;
    }
    void setGraphics(EchoPluginGraphics *graphics);
    bool paused() const
    {
        return m_paused;
    }
    void setPaused(bool value);
    bool continuous() const
    {
        return m_continuous;
    }
    void setContinuous(bool value);
    int fps() const
    {
        return m_fps;
    }
    void setFps(int value);
    QString dynamicRange() const
    {
        return m_dynamicRange;
    }
    void setDynamicRange(const QString &value);
    QVariantMap status() const
    {
        return m_status;
    }
    Q_INVOKABLE bool submit(const QVariantMap &frame);
    Q_INVOKABLE void requestFrame();
    Q_INVOKABLE EchoMusic::EchoPluginOperation *enqueue(const QVariantMap &commands);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *
    readback(EchoMusic::EchoGpuResource *resource = nullptr, const QVariantMap &options = {});
    bool live() const;
    bool drained() const;
    void retire();
    struct Snapshot
    {
        QHash<QString, GpuResourceDescription> resources;
        QVariantMap frame;
        std::shared_ptr<GpuSubmission> submission;
        bool live = false;
        bool hdr = false;
        float pixelRatio = 1;
        std::shared_ptr<GpuMemoryBudget> budget;
        quint64 frameRevision = 0;
        QList<QPair<QVariantMap, QPointer<EchoPluginOperation>>> readbacks;
    };
    Snapshot takeSnapshot();
    void acceptReport(const QVariantMap &report);

signals:
    void graphicsChanged();
    void configurationChanged();
    void statusChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void releaseResources() override;

private:
    friend class GpuRenderer;
    void syncActivity();
    void releaseNative();
    void releaseTarget();
    QPointer<EchoPluginGraphics> m_graphics;
    std::shared_ptr<GpuRenderState> m_renderState;
    std::shared_ptr<GpuBridge> m_bridge;
    QList<QMetaObject::Connection> m_windowConnections, m_itemConnections;
    QMetaObject::Connection m_scopeConnection;
    QObject *m_visibilityObserver = nullptr;
    QTimer m_clock;
    QVariantMap m_frame, m_status;
    quint64 m_revision = 0, m_submissionSequence = 0;
    QList<std::shared_ptr<GpuSubmission>> m_submissions;
    QList<std::weak_ptr<GpuSubmission>> m_inflight;
    QList<QPair<QVariantMap, QPointer<EchoPluginOperation>>> m_readbacks;
    std::shared_ptr<GpuMemoryBudget> m_targetBudget;
    qint64 m_targetBytes = 0;
    bool m_lastLive = false;
    qreal m_lastPixelRatio = 0;
    bool m_sizing = false, m_sizeAccepted = true, m_limited = false;
    bool m_paused = false, m_continuous = false, m_retired = false;
    int m_fps = 60;
    QString m_dynamicRange = "sdr";
};
} // namespace EchoMusic
