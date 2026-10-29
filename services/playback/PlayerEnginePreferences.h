#pragma once
#include <QObject>
#include "SettingsStore.h"
#include <QJsonObject>
namespace EchoMusic
{
/// Engine/output preferences use one application-owned state. Startup options
/// are copied to the native worker; output/cache changes apply at next startup.
/// Recovery deadlines and stall detection are live controls.
class PlayerEnginePreferences final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool cachePause READ cachePause WRITE setCachePause NOTIFY changed)
    Q_PROPERTY(double demuxerReadaheadSecs READ demuxerReadaheadSecs WRITE setDemuxerReadaheadSecs
                   NOTIFY changed)
    Q_PROPERTY(double cacheSecs READ cacheSecs WRITE setCacheSecs NOTIFY changed)
    Q_PROPERTY(double cachePauseWaitSecs READ cachePauseWaitSecs WRITE setCachePauseWaitSecs NOTIFY
                   changed)
    Q_PROPERTY(double demuxerMaxMb READ demuxerMaxMb WRITE setDemuxerMaxMb NOTIFY changed)
    Q_PROPERTY(
        double demuxerMaxBackMb READ demuxerMaxBackMb WRITE setDemuxerMaxBackMb NOTIFY changed)
    Q_PROPERTY(double audioBufferSecs READ audioBufferSecs WRITE setAudioBufferSecs NOTIFY changed)
    Q_PROPERTY(
        double playResumeTimeout READ playResumeTimeout WRITE setPlayResumeTimeout NOTIFY changed)
    Q_PROPERTY(double playbackStallTimeout READ playbackStallTimeout WRITE setPlaybackStallTimeout
                   NOTIFY changed)
    Q_PROPERTY(QString cacheMode READ cacheMode WRITE setCacheMode NOTIFY changed)
    Q_PROPERTY(QString audioSamplerate READ audioSamplerate WRITE setAudioSamplerate NOTIFY changed)
    Q_PROPERTY(QString audioChannels READ audioChannels WRITE setAudioChannels NOTIFY changed)
    Q_PROPERTY(QString audioFormat READ audioFormat WRITE setAudioFormat NOTIFY changed)
    Q_PROPERTY(QString gaplessAudio READ gaplessAudio WRITE setGaplessAudio NOTIFY changed)
    Q_PROPERTY(int playbackStallMaxAttempts READ playbackStallMaxAttempts WRITE
                   setPlaybackStallMaxAttempts NOTIFY changed)
public:
    explicit PlayerEnginePreferences(EchoMusic::SettingsStore &settings, QObject *parent = nullptr);
    QJsonObject startupOptions() const;
    bool cachePause() const
    {
        return m_cachePause;
    }
    void setCachePause(bool enabled);
    double demuxerReadaheadSecs() const
    {
        return m_demuxerReadaheadSecs;
    }
    void setDemuxerReadaheadSecs(double value);
    double cacheSecs() const
    {
        return m_cacheSecs;
    }
    void setCacheSecs(double value);
    double cachePauseWaitSecs() const
    {
        return m_cachePauseWaitSecs;
    }
    void setCachePauseWaitSecs(double value);
    double demuxerMaxMb() const
    {
        return m_demuxerMaxMb;
    }
    void setDemuxerMaxMb(double value);
    double demuxerMaxBackMb() const
    {
        return m_demuxerMaxBackMb;
    }
    void setDemuxerMaxBackMb(double value);
    double audioBufferSecs() const
    {
        return m_audioBufferSecs;
    }
    void setAudioBufferSecs(double value);
    double playResumeTimeout() const
    {
        return m_playResumeTimeout;
    }
    void setPlayResumeTimeout(double value);
    double playbackStallTimeout() const
    {
        return m_playbackStallTimeout;
    }
    void setPlaybackStallTimeout(double value);
    QString cacheMode() const
    {
        return m_cacheMode;
    }
    void setCacheMode(const QString &value);
    QString audioSamplerate() const
    {
        return m_audioSamplerate;
    }
    void setAudioSamplerate(const QString &value);
    QString audioChannels() const
    {
        return m_audioChannels;
    }
    void setAudioChannels(const QString &value);
    QString audioFormat() const
    {
        return m_audioFormat;
    }
    void setAudioFormat(const QString &value);
    QString gaplessAudio() const
    {
        return m_gaplessAudio;
    }
    void setGaplessAudio(const QString &value);
    int playbackStallMaxAttempts() const
    {
        return m_attempts;
    }
    void setPlaybackStallMaxAttempts(int value);
signals:
    void changed();
    void stallTimeoutChanged(double value);

private:
    EchoMusic::SettingsStore &m_settings;
    double m_demuxerReadaheadSecs = 1;
    double m_cacheSecs = 3600000;
    double m_cachePauseWaitSecs = 1;
    double m_demuxerMaxMb = 150;
    double m_demuxerMaxBackMb = 50;
    double m_audioBufferSecs = 0.2;
    double m_playResumeTimeout = 5;
    double m_playbackStallTimeout = 8;
    QString m_cacheMode = "auto";
    QString m_audioSamplerate = "auto";
    QString m_audioChannels = "auto-safe";
    QString m_audioFormat = "auto";
    QString m_gaplessAudio = "weak";
    bool m_cachePause = true;
    int m_attempts = 3;
};
} // namespace EchoMusic
