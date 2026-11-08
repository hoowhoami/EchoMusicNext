#pragma once
#include <QColor>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <atomic>

namespace EchoMusic
{
// Small raster border with an explicit physical pixel size. The scene-graph
// node owns its texture on the render thread; translations never repaint it.
class EchoDashedFrameSurface : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor strokeColor MEMBER m_strokeColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY appearanceChanged)
    Q_PROPERTY(qreal radius MEMBER m_radius NOTIFY appearanceChanged)
    Q_PROPERTY(qreal thickness MEMBER m_thickness NOTIFY appearanceChanged)
    Q_PROPERTY(qreal dashLength MEMBER m_dashLength NOTIFY appearanceChanged)
    Q_PROPERTY(QSize pixelSize MEMBER m_pixelSize NOTIFY appearanceChanged)
public:
    explicit EchoDashedFrameSurface(QQuickItem *parent = nullptr);
    // Native diagnostics for verifying reuse, without per-frame GUI signals.
    quint64 textureUploadCount() const
    {
        return m_uploadCount.load();
    }
    QSize lastUploadSize() const;
signals:
    void appearanceChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &next, const QRectF &previous) override;

private:
    QColor m_strokeColor = Qt::black;
    QColor m_backgroundColor = Qt::transparent;
    qreal m_radius = 0, m_thickness = 1, m_dashLength = 3;
    QSize m_pixelSize;
    quint64 m_revision = 1;
    std::atomic<quint64> m_uploadCount{0}, m_lastSize{0};
};
} // namespace EchoMusic
