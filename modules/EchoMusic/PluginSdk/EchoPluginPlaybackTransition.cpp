#include "EchoPluginPlaybackTransition.h"
#include "EchoPluginContext.h"

namespace EchoMusic
{
EchoPluginTransitionTrack::EchoPluginTransitionTrack(EchoPluginContext &scope,
                                                     PlaybackTransitionPresentation &presentation,
                                                     PlaybackTransitionTrack &track,
                                                     QObject *parent)
    : QObject(parent), m_scope(scope), m_presentation(presentation), m_track(track), m_empty(this),
      m_rows(m_empty, this)
{
    connect(&scope, &EchoPluginContext::activeChanged, this, &EchoPluginTransitionTrack::sync);
    connect(&presentation, &PlaybackTransitionPresentation::changed, this,
            &EchoPluginTransitionTrack::sync);
    connect(&track, &PlaybackTransitionTrack::changed, this, &EchoPluginTransitionTrack::sync);
    connect(&track.lyrics, &LyricModel::currentChanged, this, &EchoPluginTransitionTrack::changed);
    sync();
}
bool EchoPluginTransitionTrack::active() const
{
    return m_scope.active() && m_presentation.valid();
}
void EchoPluginTransitionTrack::sync()
{
    m_rows.setSource(active() ? m_track.lyrics : m_empty);
    emit changed();
}
QString EchoPluginTransitionTrack::sequence() const
{
    return active() ? m_track.sequence : QString();
}
QString EchoPluginTransitionTrack::id() const
{
    return active() ? m_track.track.id : QString();
}
QString EchoPluginTransitionTrack::hash() const
{
    return active() ? m_track.track.hash : QString();
}
QString EchoPluginTransitionTrack::songId() const
{
    return active() ? QString::number(m_track.track.songId) : QString();
}
QString EchoPluginTransitionTrack::title() const
{
    return active() ? m_track.track.title : QString();
}
QString EchoPluginTransitionTrack::artist() const
{
    return active() ? m_track.track.artist : QString();
}
QString EchoPluginTransitionTrack::album() const
{
    return active() ? m_track.track.album : QString();
}
QUrl EchoPluginTransitionTrack::coverUrl() const
{
    return active() ? m_track.cover : QUrl();
}
qint64 EchoPluginTransitionTrack::durationMs() const
{
    return active() ? qint64(m_track.track.durationMs) : 0;
}
QString EchoPluginTransitionTrack::lyricRevision() const
{
    return active() ? m_presentation.snapshot().value("epoch").toString() + ':' + m_track.sequence +
                          ':' + QString::number(m_track.lyricRevision)
                    : QString();
}
QVariantMap EchoPluginTransitionTrack::lyricsInfo() const
{
    return active() ? m_track.lyricsInfo() : QVariantMap();
}
EchoPluginPlaybackTransition::EchoPluginPlaybackTransition(EchoPluginContext &scope,
                                                           PlaybackTransitionPresentation &host,
                                                           QObject *parent)
    : QObject(parent), m_scope(scope), m_host(host), m_outgoing(scope, host, host.outgoing(), this),
      m_incoming(scope, host, host.incoming(), this)
{
    connect(&scope, &EchoPluginContext::activeChanged, this, [this] {
        emit changed();
        emit positionChanged();
    });
    connect(&host, &PlaybackTransitionPresentation::changed, this,
            &EchoPluginPlaybackTransition::changed);
    connect(&host, &PlaybackTransitionPresentation::positionChanged, this,
            &EchoPluginPlaybackTransition::positionChanged);
}
bool EchoPluginPlaybackTransition::valid() const
{
    return m_scope.active() && m_host.valid();
}
QJsonValue EchoPluginPlaybackTransition::value(const char *key) const
{
    return valid() ? m_host.snapshot().value(key) : QJsonValue();
}
QJsonObject EchoPluginPlaybackTransition::cue() const
{
    return value("presentation_cue").toObject();
}
QString EchoPluginPlaybackTransition::epoch() const
{
    return value("epoch").toString();
}
QString EchoPluginPlaybackTransition::generation() const
{
    return value("generation").toString();
}
QString EchoPluginPlaybackTransition::requestId() const
{
    return value("request_id").toString();
}
QString EchoPluginPlaybackTransition::mode() const
{
    return value("mode").toString();
}
QString EchoPluginPlaybackTransition::phase() const
{
    return value("phase").toString();
}
QString EchoPluginPlaybackTransition::reason() const
{
    return m_scope.active() ? m_host.reason() : QString("inactive");
}
QString EchoPluginPlaybackTransition::templateName() const
{
    return value("template").toString();
}
bool EchoPluginPlaybackTransition::handedOver() const
{
    return value("handed_over").toBool();
}
bool EchoPluginPlaybackTransition::cueReady() const
{
    return cue().value("ready").toBool();
}
QString EchoPluginPlaybackTransition::cueReason() const
{
    return cue().value("reason").toString();
}
double EchoPluginPlaybackTransition::crossover() const
{
    return cue().value("crossover").toDouble();
}
bool EchoPluginPlaybackTransition::hasOutgoingPosition() const
{
    return value("outgoing_position_secs").isDouble();
}
bool EchoPluginPlaybackTransition::hasIncomingPosition() const
{
    return value("incoming_position_secs").isDouble();
}
double EchoPluginPlaybackTransition::outgoingPositionMs() const
{
    return value("outgoing_position_secs").toDouble() * 1000;
}
double EchoPluginPlaybackTransition::incomingPositionMs() const
{
    return value("incoming_position_secs").toDouble() * 1000;
}
double EchoPluginPlaybackTransition::outgoingEndMs() const
{
    return value("outgoing_end_secs").toDouble() * 1000;
}
double EchoPluginPlaybackTransition::outgoingTempoRatio() const
{
    return value("outgoing_tempo_ratio").toDouble(1);
}
double EchoPluginPlaybackTransition::outputSpeed() const
{
    return value("output_speed").toDouble(1);
}
double EchoPluginPlaybackTransition::cueElapsedMs() const
{
    return value("cue_elapsed_secs").toDouble() * 1000;
}
double EchoPluginPlaybackTransition::cueDurationMs() const
{
    return value("cue_duration_secs").toDouble() * 1000;
}
double EchoPluginPlaybackTransition::overlapProgress() const
{
    return value("progress").toDouble();
}
} // namespace EchoMusic
