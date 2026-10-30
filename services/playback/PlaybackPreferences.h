#pragma once
#include <QObject>
#include "SettingsStore.h"

namespace EchoMusic
{
/// Application playback preferences; UI controls and all entry points use this
/// single host-owned state. A per-track quality override never changes default.
class PlaybackPreferences final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool mvAudioFallbackEnabled READ mvAudioFallbackEnabled WRITE
                   setMvAudioFallbackEnabled NOTIFY mvPolicyChanged)
    Q_PROPERTY(QString queueMode READ queueMode WRITE setQueueMode NOTIFY changed)
    Q_PROPERTY(QString defaultQuality READ defaultQuality WRITE setDefaultQuality NOTIFY changed)
    Q_PROPERTY(bool autoPlayOnLaunch READ autoPlayOnLaunch WRITE setAutoPlayOnLaunch NOTIFY changed)
    Q_PROPERTY(bool compatibilityMode READ compatibilityMode WRITE setCompatibilityMode NOTIFY
                   sourcePolicyChanged)
    Q_PROPERTY(bool volumeFade READ volumeFade WRITE setVolumeFade NOTIFY changed)
    Q_PROPERTY(int volumeFadeMs READ volumeFadeMs WRITE setVolumeFadeMs NOTIFY changed)
    Q_PROPERTY(bool viperQualityEnabled READ viperQualityEnabled WRITE setViperQualityEnabled NOTIFY
                   changed)
    Q_PROPERTY(bool autoNext READ autoNext WRITE setAutoNext NOTIFY failurePolicyChanged)
    Q_PROPERTY(bool preventSleep READ preventSleep WRITE setPreventSleep NOTIFY changed)
    Q_PROPERTY(int autoNextDelaySeconds READ autoNextDelaySeconds WRITE setAutoNextDelaySeconds
                   NOTIFY failurePolicyChanged)
    Q_PROPERTY(int autoNextMaxAttempts READ autoNextMaxAttempts WRITE setAutoNextMaxAttempts NOTIFY
                   failurePolicyChanged)
public:
    explicit PlaybackPreferences(EchoMusic::SettingsStore &settings, QObject *parent = nullptr);
    bool mvAudioFallbackEnabled() const
    {
        return m_mvFallback;
    }
    void setMvAudioFallbackEnabled(bool enabled);
    QString queueMode() const
    {
        return m_queueMode;
    }
    QString defaultQuality() const
    {
        return m_quality;
    }
    bool autoPlayOnLaunch() const
    {
        return m_autoPlay;
    }
    bool volumeFade() const
    {
        return m_fade;
    }
    int volumeFadeMs() const
    {
        return m_fadeMs;
    }
    bool viperQualityEnabled() const
    {
        return m_viper;
    }
    void setQueueMode(const QString &mode);
    void setDefaultQuality(const QString &quality);
    void setAutoPlayOnLaunch(bool enabled);
    void setVolumeFade(bool enabled);
    void setVolumeFadeMs(int ms);
    void setViperQualityEnabled(bool enabled);
    bool compatibilityMode() const
    {
        return m_compatibilityMode;
    }
    void setCompatibilityMode(bool enabled);
    bool autoNext() const
    {
        return m_autoNext;
    }
    int autoNextDelaySeconds() const
    {
        return m_autoNextDelaySeconds;
    }
    int autoNextMaxAttempts() const
    {
        return m_autoNextMaxAttempts;
    }
    void setAutoNext(bool enabled);
    void setAutoNextDelaySeconds(int seconds);
    void setAutoNextMaxAttempts(int attempts);
    bool preventSleep() const
    {
        return m_preventSleep;
    }
    void setPreventSleep(bool enabled);
signals:
    void changed();
    void defaultQualityChanged();
    void failurePolicyChanged();
    void sourcePolicyChanged();
    void mvPolicyChanged();

private:
    EchoMusic::SettingsStore &m_settings;
    QString m_queueMode = "context", m_quality = "hi_res";
    bool m_autoPlay = false, m_fade = true, m_viper = false;
    bool m_compatibilityMode = true;
    bool m_mvFallback = true;
    int m_fadeMs = 1000;
    bool m_autoNext = true;
    bool m_preventSleep = true;
    int m_autoNextDelaySeconds = 3;
    int m_autoNextMaxAttempts = 10;
};
} // namespace EchoMusic
