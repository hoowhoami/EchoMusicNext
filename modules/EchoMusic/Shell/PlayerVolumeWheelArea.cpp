#include "PlayerVolumeWheelArea.h"
#include <QWheelEvent>
#include <cmath>

namespace EchoMusic
{
void PlayerVolumeWheelArea::setPlayback(PlaybackService *playback)
{
    if (m_playback == playback)
        return;
    m_playback = playback;
    emit playbackChanged();
}
void PlayerVolumeWheelArea::setWheelStep(qreal step)
{
    if (!std::isfinite(step) || step <= 0 || m_wheelStep == step)
        return;
    m_wheelStep = step;
    emit wheelStepChanged();
}
void PlayerVolumeWheelArea::wheelEvent(QWheelEvent *event)
{
    event->ignore();
    if (!m_playback || !isVisible() || !isEnabled())
        return;
    const auto pixels = event->pixelDelta();
    const bool hasPixels = !pixels.isNull();
    const auto delta = hasPixels ? pixels : event->angleDelta();
    if (qAbs(delta.y()) <= qAbs(delta.x()))
        return;
    // Prefer logical pixel deltas when both representations exist. Twenty
    // pixels adjust one percent, independent of UI scaling and mouse step.
    // Volume follows physical up/down, not the content-scroll direction.
    // Undo the system's natural-scrolling inversion for this numeric control.
    const qreal change = hasPixels ? delta.y() / 20. : delta.y() / 120. * m_wheelStep;
    m_playback->setVolume(m_playback->volume() + (event->inverted() ? -change : change));
    event->accept();
}
} // namespace EchoMusic
