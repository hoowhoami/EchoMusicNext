#include "TaskbarLyricGeometry.h"
#include <QtGlobal>

namespace EchoMusic
{
namespace TaskbarLyricGeometry
{
namespace
{
/// Scale factor of a layout, or nothing when it is unknown. Every resolver
/// divides by it, so a zero (an uninitialised layout) must never get through:
/// the division would produce an infinite width instead of a fallback.
std::optional<qreal> usableScale(const TaskbarLayout &layout)
{
    const qreal scale = layout.scaleFactor;
    if (scale <= 0)
        return std::nullopt;
    return scale;
}

/// Every mode applies the same gate once it knows the candidate size: the seat
/// has to clear the minimum width and be exactly a taskbar row tall.
bool seatFits(qreal width, qreal height)
{
    return width >= kMinWidth && height >= kMinHeight && height <= kMaxHeight;
}

/// The free area a side reports, in physical pixels. Both sides always exist as
/// rects; an unusable one is empty rather than absent.
const QRect &sideRect(const TaskbarLayout &layout, Side side)
{
    return side == Side::Left ? layout.left : layout.right;
}

/// A seat inside `rect` hugging `side`, in DIP. Nothing when the rect cannot
/// seat the strip.
std::optional<Region> seatIn(const QRect &rect, Side side, qreal scale, qreal width)
{
    const qreal height = rect.height() / scale;
    if (!seatFits(width, height))
        return std::nullopt;
    const qreal areaWidth = rect.width() / scale;
    Region region;
    region.side = side;
    const qreal inset = side == Side::Right ? areaWidth - width - kEdgeInset : kEdgeInset;
    region.bounds = QRectF(rect.x() / scale + inset, rect.y() / scale, width, height);
    return region;
}

/// Shared preamble of the two taskbar-wide modes: the bar geometry the strip
/// anchors to, in DIP. Nothing when the bar is unknown or too small to seat
/// the strip.
struct BarMetrics
{
    qreal scale = 1;
    qreal barWidth = 0;
    qreal width = 0;
    qreal height = 0;
};

std::optional<BarMetrics> barMetrics(const TaskbarLayout &layout, int maxWidth)
{
    if (layout.barWidth <= 0)
        return std::nullopt;
    const auto scale = usableScale(layout);
    if (!scale)
        return std::nullopt;
    // Either side reports the taskbar's own height; both come from the same
    // taskbar, so the first one that knows it is authoritative.
    const qreal barWidth = layout.barWidth / *scale;
    const qreal width = qMin<qreal>(maxWidth, barWidth - kWidthMargin);
    const qreal height =
        layout.left.height() > 0 ? layout.left.height() / *scale : layout.right.height() / *scale;
    if (!seatFits(width, height))
        return std::nullopt;
    return BarMetrics{*scale, barWidth, width, height};
}
} // namespace

std::optional<Region> resolveSide(const TaskbarLayout &layout, Side side, int maxWidth)
{
    const QRect &rect = sideRect(layout, side);
    if (!rect.width() || !rect.height())
        return std::nullopt;
    const auto scale = usableScale(layout);
    if (!scale)
        return std::nullopt;
    const qreal width = qMin<qreal>(maxWidth, rect.width() / *scale - kWidthMargin);
    return seatIn(rect, side, *scale, width);
}

std::optional<Region> resolveAuto(const TaskbarLayout &layout, Side preferred, int maxWidth)
{
    if (const auto primary = resolveSide(layout, preferred, maxWidth))
        return primary;
    // The seat hugs the area it landed in, not the requested one: right-aligning
    // inside a left-hand gap would park the strip against the button cluster the
    // fallback exists to avoid.
    return resolveSide(layout, opposite(preferred), maxWidth);
}

std::optional<Region> resolveFixed(const TaskbarLayout &layout, Side side, int maxWidth)
{
    const auto bar = barMetrics(layout, maxWidth);
    if (!bar)
        return std::nullopt;
    Region region;
    region.side = side;
    const qreal x = side == Side::Right ? bar->barWidth - bar->width - kEdgeInset : kEdgeInset;
    region.bounds = QRectF(x, 0, bar->width, bar->height);
    return region;
}

std::optional<Region> resolveDragged(const TaskbarLayout &layout, Side side, int maxWidth,
                                     int offsetX)
{
    const auto bar = barMetrics(layout, maxWidth);
    if (!bar)
        return std::nullopt;
    const qreal maxOffset = qMax<qreal>(0, bar->barWidth - bar->width);
    const qreal offset = qBound<qreal>(0, offsetX / bar->scale, maxOffset);
    Region region;
    region.side = side;
    region.bounds = QRectF(offset, 0, bar->width, bar->height);
    return region;
}
} // namespace TaskbarLyricGeometry
} // namespace EchoMusic
