#pragma once
#include <QRectF>
#include <algorithm>
#include <cmath>

namespace EchoMusic::MenuBarLyricLayout
{
constexpr double compactWidth = 24.;
// Measure from a compact, laid-out item: an overflowing status window can keep
// a stale origin while its width grows, so its right edge is no longer an anchor.
inline double availableWidth(const QRectF &screen, const QRectF &rightSafeArea,
                             const QRectF &statusWindow, double itemWidth)
{
    if (screen.isEmpty() || statusWindow.isEmpty())
        return compactWidth;
    // Retain the existing half-screen limit on displays without a notch.
    const auto left = rightSafeArea.isEmpty() ? screen.center().x() : rightSafeArea.left();
    const auto decoration = std::max(0., statusWindow.width() - itemWidth);
    return std::max(compactWidth, std::floor(statusWindow.right() - left - decoration - 2.));
}
} // namespace EchoMusic::MenuBarLyricLayout
