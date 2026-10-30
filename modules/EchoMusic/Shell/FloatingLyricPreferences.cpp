#include <QMetaProperty>
#include <cmath>
#include "SettingsKeys.h"
#include "FloatingLyricPreferences.h"
#include <QColor>
namespace EchoMusic
{
namespace
{
bool validPreference(const QString &name, const QVariant &value, bool mini)
{
    if (name == "layout")
        return QStringList{"horizontal", "vertical"}.contains(value.toString());
    if (name == "alignment")
        return QStringList{"left", "center", "right"}.contains(value.toString()) ||
               (!mini && value == "both");
    if (name == "shadowStrength")
        return QStringList{"none", "soft", "normal", "strong"}.contains(value.toString());
    if (name == "playedColor" || name == "unplayedColor")
        return (mini && value.toString().isEmpty()) || QColor(value.toString()).isValid();
    if (name == "fontWeight")
        return value.toInt() == 400 || value.toInt() == 600 || value.toInt() == 780;
    return true;
}
} // namespace
bool FloatingLyricPreferences::patch(const QVariantMap &values)
{
    const auto meta = metaObject();
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto index = meta->indexOfProperty(it.key().toUtf8());
        if (index < meta->propertyOffset() || !meta->property(index).isWritable())
            return false;
        const auto type = meta->property(index).metaType();
        if (it.value().metaType() != type &&
            !(type.id() == QMetaType::Int && it.value().metaType().id() == QMetaType::Double &&
              std::isfinite(it.value().toDouble()) &&
              it.value().toDouble() == std::floor(it.value().toDouble()) &&
              it.value().toDouble() >= -2147483648. && it.value().toDouble() <= 2147483647.))
            return false;
        if (!validPreference(it.key(), it.value(), m_mini))
            return false;
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        if (!setProperty(it.key().toUtf8(), it.value()))
            return false;
    return true;
}

FloatingLyricPreferences::FloatingLyricPreferences(EchoMusic::SettingsStore &settings, bool desktop,
                                                   QObject *parent)
    : QObject(parent), m_settings(settings),
      m_prefix(desktop ? EchoMusic::SettingsKeys::Floating::DesktopPrefix
                       : EchoMusic::SettingsKeys::Floating::MiniPrefix),
      m_mini(!desktop)
{
    m_alwaysOnTop = true;
    m_showUnlockButton = true;
    m_layout = QStringLiteral("horizontal");
    m_alignment = desktop ? QStringLiteral("both") : QStringLiteral("center");
    m_shadowStrength = QStringLiteral("normal");
    m_bold = false;
    m_showNextLinePreview = true;
    m_playedColor = desktop ? QStringLiteral("#31cfa1") : QString();
    m_unplayedColor = desktop ? QStringLiteral("#7a7a7a") : QString();
    m_followCoverColor = false;
    m_unplayedFollowCoverColor = false;
    m_fontSize = 14;
    m_secondaryFontSize = 11;
    m_lineGap = 2;
    m_fontWeight = 780;
    m_backgroundBlur = true;
    setAlwaysOnTop(
        settings.value(m_prefix + SettingsKeys::FloatingFields::AlwaysOnTop, m_alwaysOnTop)
            .toBool());
    setShowUnlockButton(
        settings
            .value(m_prefix + SettingsKeys::FloatingFields::ShowUnlockButton, m_showUnlockButton)
            .toBool());
    setLayout(settings.value(m_prefix + SettingsKeys::FloatingFields::Layout, m_layout).toString());
    setAlignment(
        settings.value(m_prefix + SettingsKeys::FloatingFields::Alignment, m_alignment).toString());
    setShadowStrength(
        settings.value(m_prefix + SettingsKeys::FloatingFields::ShadowStrength, m_shadowStrength)
            .toString());
    setBold(settings.value(m_prefix + SettingsKeys::FloatingFields::Bold, m_bold).toBool());
    setShowNextLinePreview(settings
                               .value(m_prefix + SettingsKeys::FloatingFields::ShowNextLinePreview,
                                      m_showNextLinePreview)
                               .toBool());
    setPlayedColor(
        settings.value(m_prefix + SettingsKeys::FloatingFields::PlayedColor, m_playedColor)
            .toString());
    setUnplayedColor(
        settings.value(m_prefix + SettingsKeys::FloatingFields::UnplayedColor, m_unplayedColor)
            .toString());
    setFollowCoverColor(
        settings
            .value(m_prefix + SettingsKeys::FloatingFields::FollowCoverColor, m_followCoverColor)
            .toBool());
    setUnplayedFollowCoverColor(
        settings
            .value(m_prefix + SettingsKeys::FloatingFields::UnplayedFollowCoverColor,
                   m_unplayedFollowCoverColor)
            .toBool());
    setFontSize(
        settings.value(m_prefix + SettingsKeys::FloatingFields::FontSize, m_fontSize).toInt());
    setSecondaryFontSize(
        settings
            .value(m_prefix + SettingsKeys::FloatingFields::SecondaryFontSize, m_secondaryFontSize)
            .toInt());
    setLineGap(settings.value(m_prefix + SettingsKeys::FloatingFields::LineGap, m_lineGap).toInt());
    setFontWeight(
        settings.value(m_prefix + SettingsKeys::FloatingFields::FontWeight, m_fontWeight).toInt());
    setBackgroundBlur(
        settings.value(m_prefix + SettingsKeys::FloatingFields::BackgroundBlur, m_backgroundBlur)
            .toBool());
}
void FloatingLyricPreferences::setAlwaysOnTop(bool value)
{
    if (m_alwaysOnTop == value)
        return;
    m_alwaysOnTop = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::AlwaysOnTop, value);
    emit changed();
}
void FloatingLyricPreferences::setShowUnlockButton(bool value)
{
    if (m_showUnlockButton == value)
        return;
    m_showUnlockButton = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::ShowUnlockButton, value);
    emit changed();
}
void FloatingLyricPreferences::setLayout(QString value)
{
    if (!validPreference("layout", value, m_mini))
        return;
    if (m_layout == value)
        return;
    m_layout = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::Layout, value);
    emit changed();
}
void FloatingLyricPreferences::setAlignment(QString value)
{
    if (!validPreference("alignment", value, m_mini))
        return;
    if (m_alignment == value)
        return;
    m_alignment = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::Alignment, value);
    emit changed();
}
void FloatingLyricPreferences::setShadowStrength(QString value)
{
    if (!validPreference("shadowStrength", value, m_mini))
        return;
    if (m_shadowStrength == value)
        return;
    m_shadowStrength = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::ShadowStrength, value);
    emit changed();
}
void FloatingLyricPreferences::setBold(bool value)
{
    if (m_bold == value)
        return;
    m_bold = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::Bold, value);
    emit changed();
}
void FloatingLyricPreferences::setShowNextLinePreview(bool value)
{
    if (m_showNextLinePreview == value)
        return;
    m_showNextLinePreview = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::ShowNextLinePreview, value);
    emit changed();
}
void FloatingLyricPreferences::setPlayedColor(QString value)
{
    if (!validPreference("playedColor", value, m_mini))
        return;
    if (!value.isEmpty())
        value = QColor(value).name(QColor::HexRgb);
    if (m_playedColor == value)
        return;
    m_playedColor = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::PlayedColor, value);
    emit changed();
}
void FloatingLyricPreferences::setUnplayedColor(QString value)
{
    if (!validPreference("unplayedColor", value, m_mini))
        return;
    if (!value.isEmpty())
        value = QColor(value).name(QColor::HexRgb);
    if (m_unplayedColor == value)
        return;
    m_unplayedColor = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::UnplayedColor, value);
    emit changed();
}
void FloatingLyricPreferences::setFollowCoverColor(bool value)
{
    if (m_followCoverColor == value)
        return;
    m_followCoverColor = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::FollowCoverColor, value);
    emit changed();
}
void FloatingLyricPreferences::setUnplayedFollowCoverColor(bool value)
{
    if (m_unplayedFollowCoverColor == value)
        return;
    m_unplayedFollowCoverColor = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::UnplayedFollowCoverColor, value);
    emit changed();
}
void FloatingLyricPreferences::setFontSize(int value)
{
    value = qBound(12, value, 22);
    if (m_fontSize == value)
        return;
    m_fontSize = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::FontSize, value);
    emit changed();
}
void FloatingLyricPreferences::setSecondaryFontSize(int value)
{
    value = qBound(10, value, 18);
    if (m_secondaryFontSize == value)
        return;
    m_secondaryFontSize = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::SecondaryFontSize, value);
    emit changed();
}
void FloatingLyricPreferences::setLineGap(int value)
{
    value = qBound(0, value, 12);
    if (m_lineGap == value)
        return;
    m_lineGap = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::LineGap, value);
    emit changed();
}
void FloatingLyricPreferences::setFontWeight(int value)
{
    if (!validPreference("fontWeight", value, m_mini))
        return;
    if (m_fontWeight == value)
        return;
    m_fontWeight = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::FontWeight, value);
    emit changed();
}
void FloatingLyricPreferences::setBackgroundBlur(bool value)
{
    if (m_backgroundBlur == value)
        return;
    m_backgroundBlur = value;
    m_settings.setValue(m_prefix + SettingsKeys::FloatingFields::BackgroundBlur, value);
    emit changed();
}
void FloatingLyricPreferences::resetColors()
{
    setPlayedColor(m_mini ? "" : "#31cfa1");
    setUnplayedColor(m_mini ? "" : "#7a7a7a");
    setFollowCoverColor(false);
    setUnplayedFollowCoverColor(false);
}
void FloatingLyricPreferences::resetLayout()
{
    setFontSize(14);
    setSecondaryFontSize(11);
    setLineGap(2);
    setFontWeight(780);
    setAlignment(m_mini ? "center" : "both");
    setBackgroundBlur(true);
}
} // namespace EchoMusic
