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
        // The fill covers the whole snapped box (CSS border-box semantics) so
        // a translucent stroke over it never exposes the surface behind the
        // frame edge.
        QPainterPath fillPath;
        fillPath.addRoundedRect(QRectF(0, 0, pixels.width(), pixels.height()),
                                qMax(0., m_radius * scaleX), qMax(0., m_radius * scaleY));
        painter.fillPath(fillPath, m_backgroundColor);
        QPainterPath path;
        // The dash centerline mirrors the solid ring: inset by stroke/2 for
        // the pen band, plus the stroke insets that keep the band inside the
        // logical rectangle under fractional container clips.
        path.addRoundedRect(QRectF(m_strokeInsetLeft + inset, m_strokeInsetTop + inset,
                                   qMax(0., pixels.width() - m_strokeInsetLeft
                                            - m_strokeInsetRight - stroke),
                                   qMax(0., pixels.height() - m_strokeInsetTop
                                            - m_strokeInsetBottom - stroke)),
                            qMax(0., m_radius * scaleX), qMax(0., m_radius * scaleY));
        if (stroke > 0 && m_solid) {
            // A solid stroke as an even-odd ring using Qt border semantics:
            // the outer edge IS the shape boundary (radius R at the box edge)
            // and the inner edge is radius R - stroke, so the ring stays
            // inside the fill while both edges keep their integer pixel
            // bounds and straight runs stay crisp without stroker AA. The
            // band pulls inside the logical rectangle (stroke insets, at most
            // one device pixel per side) so container clipping can never cut
            // a column that straddles its rounded clip edge.
            const qreal bandInset = qMax(m_strokeInsetLeft,
                                         qMax(m_strokeInsetTop,
                                              qMax(m_strokeInsetRight, m_strokeInsetBottom)));
            QPainterPath ring;
            ring.setFillRule(Qt::OddEvenFill);
            ring.addRoundedRect(QRectF(m_strokeInsetLeft, m_strokeInsetTop,
                                       qMax(0., pixels.width() - m_strokeInsetLeft
                                                - m_strokeInsetRight),
                                       qMax(0., pixels.height() - m_strokeInsetTop
                                                - m_strokeInsetBottom)),
                                qMax(0., m_radius * scaleX - bandInset),
                                qMax(0., m_radius * scaleY - bandInset));
            ring.addRoundedRect(
                QRectF(m_strokeInsetLeft + stroke, m_strokeInsetTop + stroke,
                       qMax(0., pixels.width() - m_strokeInsetLeft - m_strokeInsetRight
                                - 2 * stroke),
                       qMax(0., pixels.height() - m_strokeInsetTop - m_strokeInsetBottom
                                - 2 * stroke)),
                qMax(0., m_radius * scaleX - bandInset - stroke),
                qMax(0., m_radius * scaleY - bandInset - stroke));
            painter.fillPath(ring, m_strokeColor);
        } else if (stroke > 0 && m_dashLength > 0) {
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
