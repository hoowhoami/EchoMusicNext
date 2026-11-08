#include "EchoDashedFrameSurface.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtMath>

namespace EchoMusic
{
namespace
{
class FrameNode final : public QSGSimpleTextureNode
{
public:
    quint64 revision = 0;
    QSize pixels;
};
} // namespace
EchoDashedFrameSurface::EchoDashedFrameSurface(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    connect(this, &EchoDashedFrameSurface::appearanceChanged, this, [this] {
        ++m_revision;
        update();
    });
}
QSize EchoDashedFrameSurface::lastUploadSize() const
{
    const auto packed = m_lastSize.load();
    return {int(packed >> 32), int(quint32(packed))};
}
void EchoDashedFrameSurface::geometryChange(const QRectF &next, const QRectF &previous)
{
    QQuickItem::geometryChange(next, previous);
    if (next.size() != previous.size()) {
        ++m_revision;
        update();
    }
}
QSGNode *EchoDashedFrameSurface::updatePaintNode(QSGNode *previous, UpdatePaintNodeData *)
{
    if (!window() || width() <= 0 || height() <= 0) {
        delete previous;
        return nullptr;
    }
    // An explicit size avoids Canvas's intermediate integer-DIP rounding.
    // The unsnapped fallback retains the window-DPR rasterization policy.
    const QSize pixels = m_pixelSize.isEmpty()
                             ? QSize(qCeil(width() * window()->effectiveDevicePixelRatio()),
                                     qCeil(height() * window()->effectiveDevicePixelRatio()))
                             : m_pixelSize;
    auto *node = static_cast<FrameNode *>(previous);
    if (!node) {
        node = new FrameNode;
        node->setOwnsTexture(true);
    }
    // The snapped image is already at the final device resolution. Nearest
    // sampling prevents interpolation from reintroducing fractional coverage
    // (including finite-precision UV offsets) along its one-pixel edges.
    node->setFiltering(m_pixelSize.isEmpty() ? QSGTexture::Linear : QSGTexture::Nearest);
    if (node->revision != m_revision || node->pixels != pixels) {
        QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) {
            delete node;
            return nullptr;
        }
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal scaleX = pixels.width() / width(), scaleY = pixels.height() / height();
        const qreal nominalStroke = qMax(0., m_thickness * scaleY);
        // Quantize in the final physical space too, avoiding floating-point
        // residue from the logical-size/density round trip.
        const qreal stroke = !m_pixelSize.isEmpty() && nominalStroke > 0
                                 ? qreal(qMax(1, qRound(nominalStroke)))
                                 : nominalStroke;
        const qreal inset = stroke / 2;
        QPainterPath path;
        path.addRoundedRect(QRectF(inset, inset, qMax(0., pixels.width() - stroke),
                                   qMax(0., pixels.height() - stroke)),
                            qMax(0., m_radius * scaleX), qMax(0., m_radius * scaleY));
        painter.fillPath(path, m_backgroundColor);
        if (stroke > 0 && m_dashLength > 0) {
            QPen pen(m_strokeColor, stroke, Qt::SolidLine, Qt::FlatCap);
            pen.setCosmetic(true);
            // Qt's dashed stroker can perturb an axis-aligned segment after
            // flattening a curve. Trim the contour by arc length instead, so
            // straight dash segments keep their exact physical coordinates.
            path.setCachingEnabled(true);
            const qreal length = path.length();
            const qreal dash = m_dashLength * scaleY;
            for (qreal offset = 0; offset < length; offset += dash * 2) {
                const auto segment =
                    path.trimmed(offset / length, qMin(length, offset + dash) / length);
                const auto bounds = segment.boundingRect();
                // Fill the straight dashes directly. A path stroker may leave
                // fractional coverage even at an exact half-pixel center;
                // integer perpendicular bounds preserve a solid pixel band,
                // while fractional dash ends and curved segments retain AA.
                if (!m_pixelSize.isEmpty() && qFuzzyIsNull(bounds.height()))
                    painter.fillRect(
                        QRectF(bounds.x(), qRound(bounds.y() - inset), bounds.width(), stroke),
                        m_strokeColor);
                else if (!m_pixelSize.isEmpty() && qFuzzyIsNull(bounds.width()))
                    painter.fillRect(
                        QRectF(qRound(bounds.x() - inset), bounds.y(), stroke, bounds.height()),
                        m_strokeColor);
                else
                    painter.strokePath(segment, pen);
            }
        }
        painter.end();
        auto *texture =
            window()->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel);
        if (!texture) {
            delete node;
            return nullptr;
        }
        // setTexture retires the previous texture because this node owns it.
        node->setTexture(texture);
        node->revision = m_revision;
        node->pixels = pixels;
        const auto actualSize = texture->textureSize();
        m_lastSize.store((quint64(quint32(actualSize.width())) << 32) |
                         quint32(actualSize.height()));
        m_uploadCount.fetch_add(1);
    }
    node->setRect(boundingRect());
    return node;
}
} // namespace EchoMusic
