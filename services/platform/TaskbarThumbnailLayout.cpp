#include "TaskbarThumbnailLayout.h"
#include <QPainter>
namespace EchoMusic
{
QSize taskbarThumbnailRequest(qintptr packed)
{
    return {int((quint32(packed) >> 16) & 0xffff), int(quint32(packed) & 0xffff)};
}
QSize taskbarThumbnailTile(QSize requested)
{
    // A square bitmap keeps the card square, and the shorter requested side is
    // the largest square that still fits inside the bounds DWM named. A request
    // without usable bounds falls back to the largest allowed square.
    if (requested.isEmpty())
        return {MaximumTaskbarTileSide, MaximumTaskbarTileSide};
    const int side = qMin(qMin(requested.width(), requested.height()), MaximumTaskbarTileSide);
    return {side, side};
}
QSize taskbarLivePreviewTile()
{
    return {LivePreviewTileSide, LivePreviewTileSide};
}
QRect taskbarThumbnailCoverRect(QSize tile, QSize coverSize)
{
    if (tile.isEmpty() || coverSize.isEmpty())
        return {};
    // Fit inside without cropping: the whole artwork stays visible, and any
    // leftover space is the band the backdrop pass fills. QSize::scaled
    // truncates, which would collapse a very thin cover to nothing; keep at
    // least one pixel of the short side so the artwork never disappears.
    const QSize fitted = coverSize.scaled(tile, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    const QPoint topLeft((tile.width() - fitted.width()) / 2,
                         (tile.height() - fitted.height()) / 2);
    return QRect(topLeft, fitted);
}
QRect taskbarBackdropSourceRect(QSize tile, QSize coverSize)
{
    if (tile.isEmpty() || coverSize.isEmpty())
        return {};
    const int coverWidth = coverSize.width();
    const int coverHeight = coverSize.height();
    // Comparing the shapes is done cross-multiplied on qint64: dividing would
    // need a zero check and the products stay small.
    const qint64 coverIsWider = qint64(coverWidth) * qint64(tile.height());
    const qint64 tileIsWider = qint64(coverHeight) * qint64(tile.width());
    if (coverIsWider >= tileIsWider) {
        // Wider than the tile: the full height fills it, the sides are trimmed.
        const int width =
            qBound(1, qRound(qreal(coverHeight) * tile.width() / tile.height()), coverWidth);
        return QRect(QPoint((coverWidth - width) / 2, 0), QSize(width, coverHeight));
    }
    const int height =
        qBound(1, qRound(qreal(coverWidth) * tile.height() / tile.width()), coverHeight);
    return QRect(QPoint(0, (coverHeight - height) / 2), QSize(coverWidth, height));
}
QImage renderTaskbarThumbnail(const QImage &cover, QSize tile)
{
    if (cover.isNull() || tile.isEmpty())
        return {};
    QImage image(tile, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // The band behind a non-square cover carries the same artwork enlarged to
    // fill the tile and dimmed, so the edges read as an extension of the
    // picture. Enlarging is what softens it; no separate blur pass runs here,
    // because the tile is small and this is DWM's synchronous request path.
    // The enlarged cover is never materialised: painting it as a source rect
    // avoids a size that a very wide cover would blow past int.
    const QRect backdropSource = taskbarBackdropSourceRect(tile, cover.size());
    if (!backdropSource.isEmpty())
        painter.drawImage(QRect(QPoint(0, 0), tile), cover, backdropSource);
    painter.fillRect(QRect(QPoint(0, 0), tile), QColor(0, 0, 0, 128));
    // The complete cover on top, never cropped.
    painter.drawImage(taskbarThumbnailCoverRect(tile, cover.size()), cover);
    return image;
}
} // namespace EchoMusic
