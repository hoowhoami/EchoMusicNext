#pragma once
#include "EchoPluginPlayer.h"
#include "PlaybackTransitionPresentation.h"

namespace EchoMusic
{
/// A host-parsed A or B lyric presentation, never a queue guess or transport.
class EchoPluginTransitionTrack final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginPlaybackTransition")
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(QString sequence READ sequence NOTIFY changed)
    Q_PROPERTY(QString id READ id NOTIFY changed)
    Q_PROPERTY(QString hash READ hash NOTIFY changed)
    Q_PROPERTY(QString songId READ songId NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QUrl coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed)
    Q_PROPERTY(QString lyricRevision READ lyricRevision NOTIFY changed)
    Q_PROPERTY(QVariantMap lyricsInfo READ lyricsInfo NOTIFY changed)
    Q_PROPERTY(EchoMusic::EchoPluginReadModel *lyrics READ lyrics CONSTANT)
public:
    EchoPluginTransitionTrack(EchoPluginContext &, PlaybackTransitionPresentation &,
                              PlaybackTransitionTrack &, QObject *parent);
    QString identity() const;
    QString sequence() const;
    QString id() const;
    QString hash() const;
    QString songId() const;
    QString title() const;
    QString artist() const;
    QString album() const;
    QUrl coverUrl() const;
    qint64 durationMs() const;
    QString lyricRevision() const;
    QVariantMap lyricsInfo() const;
    EchoPluginReadModel *lyrics()
    {
        return &m_rows;
    }
signals:
    void changed();

private:
    bool active() const;
    void sync();
    EchoPluginContext &m_scope;
    PlaybackTransitionPresentation &m_presentation;
    PlaybackTransitionTrack &m_track;
    LyricModel m_empty;
    EchoPluginReadModel m_rows;
};

class EchoPluginPlaybackTransition final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginPlayer")
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(QString epoch READ epoch NOTIFY changed)
    Q_PROPERTY(QString generation READ generation NOTIFY changed)
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    Q_PROPERTY(QString templateName READ templateName NOTIFY changed)
    Q_PROPERTY(bool handedOver READ handedOver NOTIFY changed)
    Q_PROPERTY(bool cueReady READ cueReady NOTIFY changed)
    Q_PROPERTY(QString cueReason READ cueReason NOTIFY changed)
    Q_PROPERTY(double crossover READ crossover NOTIFY changed)
    Q_PROPERTY(bool hasOutgoingPosition READ hasOutgoingPosition NOTIFY positionChanged)
    Q_PROPERTY(bool hasIncomingPosition READ hasIncomingPosition NOTIFY positionChanged)
    Q_PROPERTY(double outgoingPositionMs READ outgoingPositionMs NOTIFY positionChanged)
    Q_PROPERTY(double incomingPositionMs READ incomingPositionMs NOTIFY positionChanged)
    Q_PROPERTY(double outgoingEndMs READ outgoingEndMs NOTIFY changed)
    Q_PROPERTY(double outgoingTempoRatio READ outgoingTempoRatio NOTIFY changed)
    Q_PROPERTY(double outputSpeed READ outputSpeed NOTIFY changed)
    Q_PROPERTY(double cueElapsedMs READ cueElapsedMs NOTIFY positionChanged)
    Q_PROPERTY(double cueDurationMs READ cueDurationMs NOTIFY changed)
    Q_PROPERTY(double overlapProgress READ overlapProgress NOTIFY positionChanged)
    Q_PROPERTY(EchoMusic::EchoPluginTransitionTrack *outgoing READ outgoing CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginTransitionTrack *incoming READ incoming CONSTANT)
public:
    EchoPluginPlaybackTransition(EchoPluginContext &, PlaybackTransitionPresentation &,
                                 QObject *parent);
    bool valid() const;
    QString epoch() const;
    QString generation() const;
    QString requestId() const;
    QString mode() const;
    QString phase() const;
    QString reason() const;
    QString templateName() const;
    bool handedOver() const;
    bool cueReady() const;
    QString cueReason() const;
    double crossover() const;
    bool hasOutgoingPosition() const;
    bool hasIncomingPosition() const;
    double outgoingPositionMs() const;
    double incomingPositionMs() const;
    double outgoingEndMs() const;
    double outgoingTempoRatio() const;
    double outputSpeed() const;
    double cueElapsedMs() const;
    double cueDurationMs() const;
    double overlapProgress() const;
    EchoPluginTransitionTrack *outgoing()
    {
        return &m_outgoing;
    }
    EchoPluginTransitionTrack *incoming()
    {
        return &m_incoming;
    }
signals:
    void changed();
    void positionChanged();

private:
    QJsonValue value(const char *key) const;
    QJsonObject cue() const;
    EchoPluginContext &m_scope;
    PlaybackTransitionPresentation &m_host;
    EchoPluginTransitionTrack m_outgoing, m_incoming;
};
} // namespace EchoMusic
