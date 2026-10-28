#include "LyricPortraitAppearance.h"
#include "SettingsKeys.h"
#include <QColor>
#include <cmath>
namespace EchoMusic
{
LyricPortraitAppearance::LyricPortraitAppearance(QSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    setEnableSpring(settings.value(SettingsKeys::Lyrics::PortraitEnableSpring, true).toBool());
    setEnableBlur(settings.value(SettingsKeys::Lyrics::PortraitEnableBlur, true).toBool());
    setEnableScale(settings.value(SettingsKeys::Lyrics::PortraitEnableScale, true).toBool());
    setBackdropOpacity(settings.value(SettingsKeys::Lyrics::PortraitBackdropOpacity, 50).toInt());
    setCarouselEnabled(
        settings.value(SettingsKeys::Lyrics::PortraitCarouselEnabled, true).toBool());
    setCarouselInterval(settings.value(SettingsKeys::Lyrics::PortraitCarouselInterval, 15).toInt());
    setFallbackCover(settings.value(SettingsKeys::Lyrics::PortraitFallbackCover, false).toBool());
    setAutoCollapse(settings.value(SettingsKeys::Lyrics::PortraitAutoCollapse, true).toBool());
    setCollapseDelay(settings.value(SettingsKeys::Lyrics::PortraitCollapseDelay, 5).toInt());
    setHideControls(settings.value(SettingsKeys::Lyrics::PortraitHideControls, false).toBool());
    setFontScale(settings.value(SettingsKeys::Lyrics::PortraitFontScale, 1.0).toDouble());
    setFontWeight(settings.value(SettingsKeys::Lyrics::PortraitFontWeight, 700).toInt());
    setTextColor(settings.value(SettingsKeys::Lyrics::PortraitTextColor, QString()).toString());
    setUnplayedTextColor(
        settings.value(SettingsKeys::Lyrics::PortraitUnplayedTextColor, QString()).toString());
    setUseCoverColor(settings.value(SettingsKeys::Lyrics::PortraitUseCoverColor, false).toBool());
    setUnplayedUseCoverColor(
        settings.value(SettingsKeys::Lyrics::PortraitUnplayedUseCoverColor, false).toBool());
}
void LyricPortraitAppearance::setBackdropOpacity(int value)
{
    const auto normalized = qBound(10, value, 100);
    if (m_backdropOpacity == normalized)
        return;
    m_backdropOpacity = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitBackdropOpacity, normalized);
    emit changed();
}
void LyricPortraitAppearance::setCarouselEnabled(bool value)
{
    const auto normalized = value;
    if (m_carouselEnabled == normalized)
        return;
    m_carouselEnabled = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitCarouselEnabled, normalized);
    emit changed();
}
void LyricPortraitAppearance::setCarouselInterval(int value)
{
    const auto normalized = qBound(5, value, 60);
    if (m_carouselInterval == normalized)
        return;
    m_carouselInterval = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitCarouselInterval, normalized);
    emit changed();
}
void LyricPortraitAppearance::setFallbackCover(bool value)
{
    const auto normalized = value;
    if (m_fallbackCover == normalized)
        return;
    m_fallbackCover = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitFallbackCover, normalized);
    emit changed();
}
void LyricPortraitAppearance::setAutoCollapse(bool value)
{
    const auto normalized = value;
    if (m_autoCollapse == normalized)
        return;
    m_autoCollapse = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitAutoCollapse, normalized);
    emit changed();
}
void LyricPortraitAppearance::setCollapseDelay(int value)
{
    const auto normalized = qBound(5, value, 60);
    if (m_collapseDelay == normalized)
        return;
    m_collapseDelay = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitCollapseDelay, normalized);
    emit changed();
}
void LyricPortraitAppearance::setHideControls(bool value)
{
    const auto normalized = value;
    if (m_hideControls == normalized)
        return;
    m_hideControls = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitHideControls, normalized);
    emit changed();
}
void LyricPortraitAppearance::setFontScale(double value)
{
    if (!std::isfinite(value))
        return;
    const auto normalized = qBound(.7, value, 1.4);
    if (m_fontScale == normalized)
        return;
    m_fontScale = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitFontScale, normalized);
    emit changed();
}
void LyricPortraitAppearance::setFontWeight(int value)
{
    const auto normalized = qBound(100, qRound(value / 100.) * 100, 900);
    if (m_fontWeight == normalized)
        return;
    m_fontWeight = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitFontWeight, normalized);
    emit changed();
}
void LyricPortraitAppearance::setTextColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    const auto normalized = value;
    if (m_textColor == normalized)
        return;
    m_textColor = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitTextColor, normalized);
    emit changed();
}
void LyricPortraitAppearance::setUnplayedTextColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    const auto normalized = value;
    if (m_unplayedTextColor == normalized)
        return;
    m_unplayedTextColor = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitUnplayedTextColor, normalized);
    emit changed();
}
void LyricPortraitAppearance::setUseCoverColor(bool value)
{
    const auto normalized = value;
    if (m_useCoverColor == normalized)
        return;
    m_useCoverColor = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitUseCoverColor, normalized);
    emit changed();
}
void LyricPortraitAppearance::setUnplayedUseCoverColor(bool value)
{
    const auto normalized = value;
    if (m_unplayedUseCoverColor == normalized)
        return;
    m_unplayedUseCoverColor = normalized;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitUnplayedUseCoverColor, normalized);
    emit changed();
}
void LyricPortraitAppearance::setEnableSpring(bool value)
{
    if (m_enableSpring == value)
        return;
    m_enableSpring = value;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitEnableSpring, value);
    emit changed();
}
void LyricPortraitAppearance::setEnableBlur(bool value)
{
    if (m_enableBlur == value)
        return;
    m_enableBlur = value;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitEnableBlur, value);
    emit changed();
}
void LyricPortraitAppearance::setEnableScale(bool value)
{
    if (m_enableScale == value)
        return;
    m_enableScale = value;
    m_settings.setValue(SettingsKeys::Lyrics::PortraitEnableScale, value);
    emit changed();
}
bool LyricPortraitAppearance::customized() const
{
    return !m_enableSpring || !m_enableBlur || !m_enableScale || m_backdropOpacity != 50 ||
           m_carouselEnabled != true || m_carouselInterval != 15 || m_fallbackCover != false ||
           m_autoCollapse != true || m_collapseDelay != 5 || m_hideControls != false ||
           m_fontScale != 1.0 || m_fontWeight != 700 || m_textColor != QString() ||
           m_unplayedTextColor != QString() || m_useCoverColor != false ||
           m_unplayedUseCoverColor != false;
}
void LyricPortraitAppearance::reset()
{
    setEnableSpring(true);
    setEnableBlur(true);
    setEnableScale(true);

    setBackdropOpacity(50);
    setCarouselEnabled(true);
    setCarouselInterval(15);
    setFallbackCover(false);
    setAutoCollapse(true);
    setCollapseDelay(5);
    setHideControls(false);
    setFontScale(1.0);
    setFontWeight(700);
    setTextColor(QString());
    setUnplayedTextColor(QString());
    setUseCoverColor(false);
    setUnplayedUseCoverColor(false);
}
} // namespace EchoMusic
