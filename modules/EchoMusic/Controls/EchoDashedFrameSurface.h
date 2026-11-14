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
    Q_PROPERTY(bool solid MEMBER m_solid NOTIFY appearanceChanged)
    // Per-side device-pixel insets (0 or 1) that pull the stroke band inside
    // the logical rectangle; the fill keeps covering the whole box. Scalars
    // instead of one rect because QRectF::right()/bottom() mean absolute
    // edges (x + width), which an inset value must not be run through.
    Q_PROPERTY(qreal strokeInsetLeft MEMBER m_strokeInsetLeft NOTIFY appearanceChanged)
    Q_PROPERTY(qreal strokeInsetTop MEMBER m_strokeInsetTop NOTIFY appearanceChanged)
    Q_PROPERTY(qreal strokeInsetRight MEMBER m_strokeInsetRight NOTIFY appearanceChanged)
    Q_PROPERTY(qreal strokeInsetBottom MEMBER m_strokeInsetBottom NOTIFY appearanceChanged)
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
    bool m_solid = false;
    qreal m_strokeInsetLeft = 0, m_strokeInsetTop = 0, m_strokeInsetRight = 0,
          m_strokeInsetBottom = 0;
    QSize m_pixelSize;
    quint64 m_revision = 1;
    std::atomic<quint64> m_uploadCount{0}, m_lastSize{0};
};
} // namespace EchoMusic
