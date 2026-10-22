#pragma once
#include <optional>
#include <QRectF>
#include "WindowsTaskbarLyric.h"

namespace EchoMusic
{
/// Pure geometry of the taskbar lyric strip: turns the platform's free-area
/// layout into the rectangle the strip occupies. Kept free of OS and Qt object
/// calls on purpose, so every position mode is unit tested
/// (tests/qt/TaskbarLyricGeometryTest.cpp).
///
/// Semantics mirror EchoMusic shared/taskbar.ts resolveTaskbarLyricRegion().
namespace TaskbarLyricGeometry
{
/// A taskbar side. It doubles as the edge of the seat the content hugs: the
/// strip never straddles an edge, so a side is all the layout needs to know.
enum class Side
{
    Left,
    Right,
};

/// The other side, for the automatic modes' fallback.
inline constexpr Side opposite(Side side)
{
    return side == Side::Left ? Side::Right : Side::Left;
}

/// A resolved seat for the strip: the side it hugs and the rectangle in
/// device-independent pixels, relative to the taskbar client area.
struct Region
{
    Side side = Side::Left;
    QRectF bounds;
};

/// Gates mirroring resolveTaskbarLyricRegion(): a side below these is unusable
/// and the caller falls back to the other one. Exposed so the boundaries are
/// tested against the same numbers the resolver uses.
inline constexpr int kMinWidth = 160;
inline constexpr int kMinHeight = 28;
inline constexpr int kMaxHeight = 100;
inline constexpr int kEdgeInset = 8;
inline constexpr int kWidthMargin = 16;

/// Seats the strip inside the free area of one side, hugging that same side so
/// it never crowds the icons the area borders. Nothing when the area cannot
/// seat the strip (too narrow, misshapen, or an unknown scale).
std::optional<Region> resolveSide(const TaskbarLayout &layout, Side side, int maxWidth);

/// The automatic modes: the preferred side wins whenever it can seat the strip,
/// otherwise the other free area is used. Whichever area is taken, the seat hugs
/// that area's own edge - aligning to the requested side instead would push the
/// strip against the button cluster the fallback just avoided
/// (shared/taskbar.ts: anchor = position === "auto" ? regionSide : position).
std::optional<Region> resolveAuto(const TaskbarLayout &layout, Side preferred, int maxWidth);

/// Pins the strip to an edge of the whole taskbar, deliberately ignoring the
/// occupied spans (the user asked for the taskbar edges).
std::optional<Region> resolveFixed(const TaskbarLayout &layout, Side side, int maxWidth);

/// Keeps the strip where the user dropped it, hugging the given side. The offset
/// is a physical pixel value inside the taskbar client area, clamped so a
/// narrower taskbar or a different resolution can never push the strip off
/// screen.
std::optional<Region> resolveDragged(const TaskbarLayout &layout, Side side, int maxWidth,
                                     int offsetX);
} // namespace TaskbarLyricGeometry
} // namespace EchoMusic
