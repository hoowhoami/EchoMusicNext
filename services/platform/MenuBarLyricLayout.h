#pragma once
#include <QRectF>
#include <algorithm>
#include <cmath>

namespace EchoMusic::MenuBarLyricLayout
{
constexpr double compactWidth = 24.;
// AppKit may keep either edge fixed while resizing a status window. Neither
// form is a relocation of the item among its neighbours.
inline bool relocated(const QRectF &previous, double previousItemWidth, const QRectF &current,
                      double itemWidth)
{
    return !previous.isEmpty() && std::abs(previousItemWidth - itemWidth) < .5 &&
           std::abs(previous.width() - current.width()) < .5 &&
           std::abs(previous.right() - current.right()) > .5;
}
// Measure a laid-out item. Initial/screen probes use a compact item because an
// overflowing window may retain a stale origin while its requested width grows.
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
