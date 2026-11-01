#pragma once
#include "PlaybackService.h"
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Wheel-only input over a volume button. Leaves pointer/hover input to its
/// parent and keeps the complete native mouse/touchpad wheel event available.
class PlayerVolumeWheelArea : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        EchoMusic::PlaybackService *playback READ playback WRITE setPlayback NOTIFY playbackChanged)
    Q_PROPERTY(qreal wheelStep READ wheelStep WRITE setWheelStep NOTIFY wheelStepChanged)
public:
    explicit PlayerVolumeWheelArea(QQuickItem *parent = nullptr) : QQuickItem(parent)
    {
        setAcceptedMouseButtons(Qt::NoButton);
    }
    PlaybackService *playback() const
    {
        return m_playback;
    }
    void setPlayback(PlaybackService *playback);
    qreal wheelStep() const
    {
        return m_wheelStep;
    }
    void setWheelStep(qreal step);
signals:
    void playbackChanged();
    void wheelStepChanged();

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    QPointer<PlaybackService> m_playback;
    qreal m_wheelStep = 1;
};
} // namespace EchoMusic
