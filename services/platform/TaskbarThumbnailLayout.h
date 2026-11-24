#pragma once
#include <QImage>
#include <QRect>
#include <QSize>
namespace EchoMusic
{
/// DWM rejects a taskbar bitmap that exceeds the maximum it names, and Explorer
/// lays the hover preview out from the bitmap's own shape. The bitmap handed to
/// DWM is therefore square, which makes the card square and lets the artwork be
/// shown complete instead of cropped to the card's ratio.
constexpr int MaximumTaskbarTileSide = 600;
/// A live preview names no bounds of its own and is shown in a square peek
/// window, so it always asks for this square. The window's client area is not a
/// substitute: a window parked in the background reports a sliver of a client
/// area, which collapses the peek window into a strip around a cover that
/// cannot fill it.
constexpr int LivePreviewTileSide = MaximumTaskbarTileSide;
/// Splits WM_DWMSENDICONICTHUMBNAIL's lParam: the high word is the maximum
/// width and the low word the maximum height.
QSize taskbarThumbnailRequest(qintptr packed);
/// The square bitmap size for a thumbnail request: the shorter requested side,
/// so the bitmap fits inside both bounds, capped at MaximumTaskbarTileSide.
/// A request that names no usable size yields the largest allowed square.
QSize taskbarThumbnailTile(QSize requested);
/// The square bitmap size for a live preview, which names no bounds at all.
QSize taskbarLivePreviewTile();
/// Where the whole cover sits inside tile: scaled to fit without cropping and
/// centred, so the artwork is never cut off.
QRect taskbarThumbnailCoverRect(QSize tile, QSize coverSize);
/// The part of the cover that fills tile once the artwork is enlarged until it
/// covers the whole tile, in the cover's own coordinates and clipped to it. The
/// enlarged size is deliberately never built: it is coverSize scaled up by
/// tileSide/shortSide, which overflows int for a very wide cover and would turn
/// the whole tile black.
QRect taskbarBackdropSourceRect(QSize tile, QSize coverSize);
/// The tile handed to DwmSetIconicThumbnail/DwmSetIconicLivePreviewBitmap: the
/// complete cover centred on an enlargement of the same artwork, dimmed so the
/// bands beside a non-square cover read as part of the picture rather than as
/// black bars.
QImage renderTaskbarThumbnail(const QImage &cover, QSize tile);
} // namespace EchoMusic
