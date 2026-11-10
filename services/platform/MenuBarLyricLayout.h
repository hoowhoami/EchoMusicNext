#pragma once
#include <QRectF>
#include <algorithm>

namespace EchoMusic::MenuBarLyricLayout
{
constexpr double compactWidth = 24.;
// A remote/expanded status host's origin is not a menu-bar screen coordinate.
// Request a bounded content width; AppKit owns placement and overflow hosting.
inline double availableWidth(const QRectF &screen, double configuredWidth)
{
    const auto screenLimit = screen.isEmpty() ? 800. : std::max(128., screen.width() - 32.);
    return std::clamp(configuredWidth, 128., std::min(800., screenLimit));
}
} // namespace EchoMusic::MenuBarLyricLayout
