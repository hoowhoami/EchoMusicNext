#include "SettingsKeys.h"
#include "PlaybackPreferences.h"
#include <cmath>
namespace EchoMusic
{
namespace
{
const QStringList qualities{"standard",   "higher",      "flac",       "hi_res",
                            "viper_tape", "viper_clear", "viper_atmos"};
}
PlaybackPreferences::PlaybackPreferences(EchoMusic::SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_mvFallback =
        settings.value(EchoMusic::SettingsKeys::Playback::MvAudioFallbackEnabled, true).toBool();
    m_queueMode =
        settings.value(EchoMusic::SettingsKeys::Playback::QueueMode, "context").toString() ==
                "single"
            ? "single"
            : "context";
    const auto quality =
        settings.value(EchoMusic::SettingsKeys::Playback::DefaultQuality, m_quality).toString();
    if (qualities.contains(quality))
        m_quality = quality;
    m_autoPlay =
        settings.value(EchoMusic::SettingsKeys::Playback::AutoPlayOnLaunch, false).toBool();
    m_fade = settings.value(EchoMusic::SettingsKeys::Playback::VolumeFade, true).toBool();
    m_fadeMs = qBound(
        500, settings.value(EchoMusic::SettingsKeys::Playback::VolumeFadeMs, 1000).toInt(), 3000);
    m_viper = settings.value(EchoMusic::SettingsKeys::Playback::ViperQualityEnabled, false).toBool();
    m_compatibilityMode =
        settings.value(EchoMusic::SettingsKeys::Playback::CompatibilityMode, true).toBool();
    if (!m_viper && m_quality.startsWith("viper_"))
        m_quality = "hi_res";
    m_autoNext = settings.value(EchoMusic::SettingsKeys::Playback::AutoNext, true).toBool();
    m_preventSleep = settings.value(EchoMusic::SettingsKeys::Playback::PreventSleep, true).toBool();
    bool valid = false;
    const auto delay =
        settings.value(EchoMusic::SettingsKeys::Playback::AutoNextDelaySeconds, 3).toInt(&valid);
    if (valid)
        m_autoNextDelaySeconds = qBound(0, delay, 600);
    m_autoNextMaxAttempts = qBound(
        1, settings.value(EchoMusic::SettingsKeys::Playback::AutoNextMaxAttempts, 10).toInt(), 999);
}
void PlaybackPreferences::setQueueMode(const QString &mode)
{
    if ((mode != "single" && mode != "context") || mode == m_queueMode)
        return;
    m_queueMode = mode;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::QueueMode, mode);
    emit changed();
}
void PlaybackPreferences::setDefaultQuality(const QString &quality)
{
    if (!qualities.contains(quality) || (!m_viper && quality.startsWith("viper_")) ||
        quality == m_quality)
        return;
    m_quality = quality;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::DefaultQuality, quality);
    emit changed();
}
void PlaybackPreferences::setAutoPlayOnLaunch(bool enabled)
{
    if (enabled == m_autoPlay)
        return;
    m_autoPlay = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::AutoPlayOnLaunch, enabled);
    emit changed();
}
void PlaybackPreferences::setVolumeFade(bool enabled)
{
    if (enabled == m_fade)
        return;
    m_fade = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::VolumeFade, enabled);
    emit changed();
}
void PlaybackPreferences::setVolumeFadeMs(int ms)
{
    ms = qBound(500, ms, 3000);
    if (ms == m_fadeMs)
        return;
    m_fadeMs = ms;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::VolumeFadeMs, ms);
    emit changed();
}
void PlaybackPreferences::setViperQualityEnabled(bool enabled)
{
    if (enabled == m_viper)
        return;
    m_viper = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::ViperQualityEnabled, enabled);
    if (!enabled && m_quality.startsWith("viper_")) {
        m_quality = "hi_res";
        m_settings.setValue(EchoMusic::SettingsKeys::Playback::DefaultQuality, m_quality);
    }
    emit changed();
}
void PlaybackPreferences::setAutoNext(bool enabled)
{
    if (enabled == m_autoNext)
        return;
    m_autoNext = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::AutoNext, enabled);
    emit failurePolicyChanged();
}
void PlaybackPreferences::setCompatibilityMode(bool enabled)
{
    if (enabled == m_compatibilityMode)
        return;
    m_compatibilityMode = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::CompatibilityMode, enabled);
    emit sourcePolicyChanged();
    emit changed();
}
void PlaybackPreferences::setAutoNextDelaySeconds(int seconds)
{
    seconds = qBound(0, seconds, 600);
    if (seconds == m_autoNextDelaySeconds)
        return;
    m_autoNextDelaySeconds = seconds;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::AutoNextDelaySeconds, seconds);
    emit failurePolicyChanged();
}
void PlaybackPreferences::setAutoNextMaxAttempts(int attempts)
{
    attempts = qBound(1, attempts, 999);
    if (attempts == m_autoNextMaxAttempts)
        return;
    m_autoNextMaxAttempts = attempts;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::AutoNextMaxAttempts, attempts);
    emit failurePolicyChanged();
}
} // namespace EchoMusic
void EchoMusic::PlaybackPreferences::setPreventSleep(bool enabled)
{
    if (enabled == m_preventSleep)
        return;
    m_preventSleep = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::PreventSleep, enabled);
    emit changed();
}

void EchoMusic::PlaybackPreferences::setMvAudioFallbackEnabled(bool enabled)
{
    if (m_mvFallback == enabled)
        return;
    m_mvFallback = enabled;
    m_settings.setValue(EchoMusic::SettingsKeys::Playback::MvAudioFallbackEnabled, enabled);
    emit mvPolicyChanged();
    emit changed();
}
