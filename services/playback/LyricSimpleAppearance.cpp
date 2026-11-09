#include "LyricSimpleAppearance.h"
#include "SettingsKeys.h"
#include <QColor>
#include <cmath>
namespace EchoMusic
{
LyricSimpleAppearance::LyricSimpleAppearance(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    setSecondaryTextColor(
        settings.value(SettingsKeys::Lyrics::SimpleSecondaryTextColor, QString()).toString());
    setSecondaryUseCoverColor(
        settings.value(SettingsKeys::Lyrics::SimpleSecondaryUseCoverColor, false).toBool());
    setFontScale(settings.value(SettingsKeys::Lyrics::SimpleFontScale, 1.0).toDouble());
    setFontWeight(settings.value(SettingsKeys::Lyrics::SimpleFontWeight, 900).toInt());
    setTextColor(settings.value(SettingsKeys::Lyrics::SimpleTextColor, QString()).toString());
    setUnplayedTextColor(
        settings.value(SettingsKeys::Lyrics::SimpleUnplayedTextColor, QString()).toString());
    setUseCoverColor(settings.value(SettingsKeys::Lyrics::SimpleUseCoverColor, false).toBool());
    setUnplayedUseCoverColor(
        settings.value(SettingsKeys::Lyrics::SimpleUnplayedUseCoverColor, false).toBool());
}
void LyricSimpleAppearance::setFontScale(double value)
{
    if (!std::isfinite(value))
        return;
    value = qBound(.7, value, 1.4);
    if (m_fontScale == value)
        return;
    m_fontScale = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleFontScale, value);
    emit changed();
}
void LyricSimpleAppearance::setFontWeight(int value)
{
    value = qBound(100, value, 900);
    value = qRound(value / 100.) * 100;
    if (m_fontWeight == value)
        return;
    m_fontWeight = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleFontWeight, value);
    emit changed();
}
void LyricSimpleAppearance::setTextColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    if (m_textColor == value)
        return;
    m_textColor = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleTextColor, value);
    emit changed();
}
void LyricSimpleAppearance::setUnplayedTextColor(const QString &value)
{
    if (!value.isEmpty() && !QColor(value).isValid())
        return;
    if (m_unplayedTextColor == value)
        return;
    m_unplayedTextColor = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleUnplayedTextColor, value);
    emit changed();
}
void LyricSimpleAppearance::setUseCoverColor(bool value)
{
    if (m_useCoverColor == value)
        return;
    m_useCoverColor = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleUseCoverColor, value);
    emit changed();
}
void LyricSimpleAppearance::setUnplayedUseCoverColor(bool value)
{
    if (m_unplayedUseCoverColor == value)
        return;
    m_unplayedUseCoverColor = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleUnplayedUseCoverColor, value);
    emit changed();
}
bool LyricSimpleAppearance::customized() const
{
    return !m_secondaryTextColor.isEmpty() || m_secondaryUseCoverColor || m_fontScale != 1.0 ||
           m_fontWeight != 900 || m_textColor != QString() || m_unplayedTextColor != QString() ||
           m_useCoverColor != false || m_unplayedUseCoverColor != false;
}
void LyricSimpleAppearance::resetSkin()
{
    setSecondaryTextColor("");
    setSecondaryUseCoverColor(false);
    setFontScale(1.0);
    setFontWeight(900);
    setTextColor(QString());
    setUnplayedTextColor(QString());
    setUseCoverColor(false);
    setUnplayedUseCoverColor(false);
}

void LyricSimpleAppearance::setSecondaryTextColor(const QString &value)
{
    if ((!value.isEmpty() && !QColor(value).isValid()) || m_secondaryTextColor == value)
        return;
    m_secondaryTextColor = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleSecondaryTextColor, value);
    emit changed();
}
void LyricSimpleAppearance::setSecondaryUseCoverColor(bool value)
{
    if (m_secondaryUseCoverColor == value)
        return;
    m_secondaryUseCoverColor = value;
    m_settings.setValue(SettingsKeys::Lyrics::SimpleSecondaryUseCoverColor, value);
    emit changed();
}
} // namespace EchoMusic
