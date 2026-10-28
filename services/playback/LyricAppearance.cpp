#include "SettingsKeys.h"
#include "LyricAppearance.h"
#include <QColor>
#include <cmath>
namespace EchoMusic
{
LyricAppearance::LyricAppearance(QSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_portrait(settings, this)
{
    setPlayedColor(settings.value(EchoMusic::SettingsKeys::Lyrics::PagePlayedColor, "").toString());
    setUnplayedColor(
        settings.value(EchoMusic::SettingsKeys::Lyrics::PageUnplayedColor, "").toString());
    setFollowCoverColor(
        settings.value(EchoMusic::SettingsKeys::Lyrics::PageFollowCoverColor, false).toBool());
    setUnplayedFollowCoverColor(
        settings.value(EchoMusic::SettingsKeys::Lyrics::PageUnplayedFollowCoverColor, false)
            .toBool());
    setAlignPosition(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceAlignPosition, 0.5).toDouble());
    setEnableSpring(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceEnableSpring, true).toBool());
    setHighRefreshRate(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceHighRefreshRate, false).toBool());
    setEnableBlur(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceEnableBlur, true).toBool());
    setEnableScale(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceEnableScale, true).toBool());
    setHidePassedLines(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceHidePassedLines, false).toBool());
    setWordFadeWidth(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceWordFadeWidth, 0.5).toDouble());
    setDynamicAlbumCover(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceDynamicAlbumCover, false)
            .toBool());
    setTextColor(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceTextColor, QString()).toString());
    setUnplayedTextColor(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceUnplayedTextColor, QString())
            .toString());
    setUnplayedUseCoverColor(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceUnplayedUseCoverColor, false)
            .toBool());
    setUseCoverColor(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceUseCoverColor, false).toBool());
    setFontScale(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceFontScale, 1.).toDouble());
    setFontWeight(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceFontWeight, 900).toInt());
    setBackgroundBlur(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceBackgroundBlur, false).toBool());
    setBackgroundRhythm(
        settings.value(EchoMusic::SettingsKeys::Lyrics::AppearanceBackgroundRhythm, false)
            .toBool());
}
void LyricAppearance::setPlayedColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    if (m_playedColor == value)
        return;
    m_playedColor = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::PagePlayedColor, value);
    emit changed();
}
void LyricAppearance::setUnplayedColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    if (m_unplayedColor == value)
        return;
    m_unplayedColor = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::PageUnplayedColor, value);
    emit changed();
}
void LyricAppearance::setFollowCoverColor(bool value)
{
    if (m_followCoverColor == value)
        return;
    m_followCoverColor = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::PageFollowCoverColor, value);
    emit changed();
}
void LyricAppearance::setUnplayedFollowCoverColor(bool value)
{
    if (m_unplayedFollowCoverColor == value)
        return;
    m_unplayedFollowCoverColor = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::PageUnplayedFollowCoverColor, value);
    emit changed();
}
void LyricAppearance::setUnplayedTextColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    if (m_unplayedTextColor == value)
        return;
    m_unplayedTextColor = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceUnplayedTextColor, value);
    emit changed();
}
void LyricAppearance::setUnplayedUseCoverColor(bool value)
{
    if (m_unplayedUseCoverColor == value)
        return;
    m_unplayedUseCoverColor = value;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceUnplayedUseCoverColor, value);
    emit changed();
}
void LyricAppearance::resetColors()
{
    setPlayedColor("");
    setUnplayedColor("");
    setFollowCoverColor(false);
    setUnplayedFollowCoverColor(false);
}
void LyricAppearance::resetSkinColors()
{
    setTextColor(QString());
    setUseCoverColor(false);
    setUnplayedTextColor(QString());
    setUnplayedUseCoverColor(false);
}
bool LyricAppearance::customized() const
{
    return m_alignPosition != 0.5 || m_enableSpring != true || m_highRefreshRate != false ||
           m_enableBlur != true || m_enableScale != true || m_hidePassedLines != false ||
           m_wordFadeWidth != 0.5 || m_dynamicAlbumCover != false || m_textColor != QString() ||
           m_useCoverColor != false || m_unplayedTextColor != QString() ||
           m_unplayedUseCoverColor != false || m_fontScale != 1. || m_fontWeight != 900;
}
void LyricAppearance::resetSkin()
{
    setAlignPosition(0.5);
    setEnableSpring(true);
    setHighRefreshRate(false);
    setEnableBlur(true);
    setEnableScale(true);
    setHidePassedLines(false);
    setWordFadeWidth(0.5);
    setDynamicAlbumCover(false);
    resetSkinColors();
    setFontScale(1.);
    setFontWeight(900);
}
void LyricAppearance::setAlignPosition(double value)
{
    if (!std::isfinite(value))
        return;
    const auto normalized = qBound(0., value, 1.);
    if (m_alignPosition == normalized)
        return;
    m_alignPosition = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceAlignPosition, normalized);
    emit changed();
}
void LyricAppearance::setEnableSpring(bool value)
{
    const auto normalized = value;
    if (m_enableSpring == normalized)
        return;
    m_enableSpring = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceEnableSpring, normalized);
    emit changed();
}
void LyricAppearance::setHighRefreshRate(bool value)
{
    const auto normalized = value;
    if (m_highRefreshRate == normalized)
        return;
    m_highRefreshRate = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceHighRefreshRate, normalized);
    emit changed();
}
void LyricAppearance::setEnableBlur(bool value)
{
    const auto normalized = value;
    if (m_enableBlur == normalized)
        return;
    m_enableBlur = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceEnableBlur, normalized);
    emit changed();
}
void LyricAppearance::setEnableScale(bool value)
{
    const auto normalized = value;
    if (m_enableScale == normalized)
        return;
    m_enableScale = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceEnableScale, normalized);
    emit changed();
}
void LyricAppearance::setHidePassedLines(bool value)
{
    const auto normalized = value;
    if (m_hidePassedLines == normalized)
        return;
    m_hidePassedLines = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceHidePassedLines, normalized);
    emit changed();
}
void LyricAppearance::setWordFadeWidth(double value)
{
    if (!std::isfinite(value))
        return;
    const auto normalized = qBound(.05, value, 1.);
    if (m_wordFadeWidth == normalized)
        return;
    m_wordFadeWidth = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceWordFadeWidth, normalized);
    emit changed();
}
void LyricAppearance::setDynamicAlbumCover(bool value)
{
    const auto normalized = value;
    if (m_dynamicAlbumCover == normalized)
        return;
    m_dynamicAlbumCover = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceDynamicAlbumCover, normalized);
    emit changed();
}
void LyricAppearance::setTextColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    const auto normalized = value;
    if (m_textColor == normalized)
        return;
    m_textColor = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceTextColor, normalized);
    emit changed();
}
void LyricAppearance::setUseCoverColor(bool value)
{
    const auto normalized = value;
    if (m_useCoverColor == normalized)
        return;
    m_useCoverColor = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceUseCoverColor, normalized);
    emit changed();
}
void LyricAppearance::setFontScale(double value)
{
    if (!std::isfinite(value))
        return;
    const auto normalized = qBound(.7, value, 1.4);
    if (m_fontScale == normalized)
        return;
    m_fontScale = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceFontScale, normalized);
    emit changed();
}
void LyricAppearance::setFontWeight(int value)
{
    const auto normalized = qBound(100, qRound(value / 100.) * 100, 900);
    if (m_fontWeight == normalized)
        return;
    m_fontWeight = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceFontWeight, normalized);
    emit changed();
}
void LyricAppearance::setBackgroundBlur(bool value)
{
    const auto normalized = value;
    if (m_backgroundBlur == normalized)
        return;
    m_backgroundBlur = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceBackgroundBlur, normalized);
    emit changed();
}
void LyricAppearance::setBackgroundRhythm(bool value)
{
    const auto normalized = value;
    if (m_backgroundRhythm == normalized)
        return;
    m_backgroundRhythm = normalized;
    m_settings.setValue(EchoMusic::SettingsKeys::Lyrics::AppearanceBackgroundRhythm, normalized);
    emit changed();
}
} // namespace EchoMusic
