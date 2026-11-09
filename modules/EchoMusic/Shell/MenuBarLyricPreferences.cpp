#include "MenuBarLyricPreferences.h"
#include "SettingsKeys.h"
namespace EchoMusic
{
MenuBarLyricPreferences::MenuBarLyricPreferences(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    const auto values = settings.value(SettingsKeys::MenuBarLyric::Preferences).toMap();
    const auto number = [&](const char *key, int fallback, int low, int high) {
        bool ok = false;
        const int value = values.value(QLatin1String(key)).toInt(&ok);
        return ok ? qBound(low, value, high) : fallback;
    };
    m_enabled = values.value("enabled", false).toBool();
    m_secondaryContent = number("secondaryContent", 2, 0, 2);
    m_style.displayMode = number("displayMode", 0, 0, 3);
    m_style.maxWidth = number("maxWidth", 280, 128, 800);
    m_style.fontSize = number("fontSize", 13, 10, 22);
    m_style.fontWeight = number("fontWeight", 500, 400, 700);
    m_style.colorMode = number("colorMode", 0, 0, 2);
    m_style.secondaryColorMode = number("secondaryColorMode", m_style.colorMode, 0, 2);
    m_style.opacity = number("opacity", 100, 20, 100);
    m_style.wordByWord = values.value("wordByWord", true).toBool();
    m_style.scrolling = values.value("scrolling", false).toBool();
    m_pauseBehavior = number("pauseBehavior", 0, 0, 2);
    m_idleBehavior = number("idleBehavior", 1, 0, 2);
    const QColor primary(values.value("primaryColor").toString());
    const QColor secondary(values.value("secondaryColor").toString());
    if (primary.isValid())
        m_style.primaryColor = primary;
    if (secondary.isValid())
        m_style.secondaryColor = secondary;
}
void MenuBarLyricPreferences::save()
{
    m_settings.setValue(
        SettingsKeys::MenuBarLyric::Preferences,
        QVariantMap{{"enabled", m_enabled},
                    {"secondaryContent", m_secondaryContent},
                    {"displayMode", m_style.displayMode},
                    {"maxWidth", m_style.maxWidth},
                    {"fontSize", m_style.fontSize},
                    {"fontWeight", m_style.fontWeight},
                    {"secondaryColorMode", m_style.secondaryColorMode},
                    {"colorMode", m_style.colorMode},
                    {"primaryColor", m_style.primaryColor.name(QColor::HexArgb)},
                    {"secondaryColor", m_style.secondaryColor.name(QColor::HexArgb)},
                    {"opacity", m_style.opacity},
                    {"wordByWord", m_style.wordByWord},
                    {"scrolling", m_style.scrolling},
                    {"pauseBehavior", m_pauseBehavior},
                    {"idleBehavior", m_idleBehavior}});
    emit changed();
}
void MenuBarLyricPreferences::setEnabled(bool value)
{
    if (m_enabled != value) {
        m_enabled = value;
        save();
    }
}
void MenuBarLyricPreferences::setSecondaryContent(int value)
{
    value = qBound(0, value, 2);
    if (m_secondaryContent != value) {
        m_secondaryContent = value;
        save();
    }
}
void MenuBarLyricPreferences::setDisplayMode(int value)
{
    value = qBound(0, value, 3);
    if (m_style.displayMode != value) {
        m_style.displayMode = value;
        save();
    }
}
void MenuBarLyricPreferences::setMaxWidth(int value)
{
    value = qBound(128, value, 800);
    if (m_style.maxWidth != value) {
        m_style.maxWidth = value;
        save();
    }
}
void MenuBarLyricPreferences::setFontSize(int value)
{
    value = qBound(10, value, 22);
    if (m_style.fontSize != value) {
        m_style.fontSize = value;
        save();
    }
}
void MenuBarLyricPreferences::setFontWeight(int value)
{
    value = qBound(400, value, 700);
    if (m_style.fontWeight != value) {
        m_style.fontWeight = value;
        save();
    }
}
void MenuBarLyricPreferences::setSecondaryColorMode(int value)
{
    value = qBound(0, value, 2);
    if (m_style.secondaryColorMode != value) {
        m_style.secondaryColorMode = value;
        save();
    }
}
void MenuBarLyricPreferences::setColorMode(int value)
{
    value = qBound(0, value, 2);
    if (m_style.colorMode != value) {
        m_style.colorMode = value;
        save();
    }
}
void MenuBarLyricPreferences::setPrimaryColor(const QColor &value)
{
    if (value.isValid() && m_style.primaryColor != value) {
        m_style.primaryColor = value;
        save();
    }
}
void MenuBarLyricPreferences::setSecondaryColor(const QColor &value)
{
    if (value.isValid() && m_style.secondaryColor != value) {
        m_style.secondaryColor = value;
        save();
    }
}
void MenuBarLyricPreferences::setOpacity(int value)
{
    value = qBound(20, value, 100);
    if (m_style.opacity != value) {
        m_style.opacity = value;
        save();
    }
}
void MenuBarLyricPreferences::setWordByWord(bool value)
{
    if (m_style.wordByWord != value) {
        m_style.wordByWord = value;
        save();
    }
}
void MenuBarLyricPreferences::setScrolling(bool value)
{
    if (m_style.scrolling != value) {
        m_style.scrolling = value;
        save();
    }
}
void MenuBarLyricPreferences::setPauseBehavior(int value)
{
    value = qBound(0, value, 2);
    if (m_pauseBehavior != value) {
        m_pauseBehavior = value;
        save();
    }
}
void MenuBarLyricPreferences::setIdleBehavior(int value)
{
    value = qBound(0, value, 2);
    if (m_idleBehavior != value) {
        m_idleBehavior = value;
        save();
    }
}
} // namespace EchoMusic
