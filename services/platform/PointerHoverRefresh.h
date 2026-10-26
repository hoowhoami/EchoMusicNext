#pragma once
class QWindow;
namespace EchoMusic
{
/// Re-delivers the pointer's current position to the window so Qt re-evaluates
/// what the pointer is hovering.
///
/// Qt updates hover only while the pointer moves. A surface that becomes
/// interactive after the pointer stopped moving - the playback page once its
/// entry transition is done - would otherwise wait for the next mouse twitch
/// before any control under the pointer reacts. The position is re-delivered
/// without touching the physical cursor.
///
/// Returns false when the platform cannot deliver the move.
bool refreshPointerHover(QWindow *window);
} // namespace EchoMusic
