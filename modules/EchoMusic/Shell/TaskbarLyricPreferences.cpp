#include "SettingsKeys.h"
#include "TaskbarLyricPreferences.h"
namespace EchoMusic
{
namespace
{
/// The stored vocabulary of the position modes. One table, so the settings
/// file, the parser and the writer can never drift apart.
struct PositionKey
{
    TaskbarLyricPreferences::Position value;
    QLatin1StringView key;
};

constexpr PositionKey kPositionKeys[] = {
    {TaskbarLyricPreferences::Position::AutoLeft, QLatin1StringView("left")},
    {TaskbarLyricPreferences::Position::AutoRight, QLatin1StringView("right")},
    {TaskbarLyricPreferences::Position::FixedLeft, QLatin1StringView("fixedLeft")},
    {TaskbarLyricPreferences::Position::FixedRight, QLatin1StringView("fixedRight")},
    {TaskbarLyricPreferences::Position::Drag, QLatin1StringView("drag")},
};

/// Parses a stored key. "auto" is what older builds wrote and anything unknown
/// can come from a hand-edited file; both land on the shipped default.
TaskbarLyricPreferences::Position positionFromKey(QStringView key)
{
    for (const auto &entry : kPositionKeys) {
        if (key == entry.key)
            return entry.value;
    }
    return TaskbarLyricPreferences::Position::AutoRight;
}
} // namespace

QString TaskbarLyricPreferences::positionKey(Position value)
{
    for (const auto &entry : kPositionKeys) {
        if (entry.value == value)
            return QString::fromLatin1(entry.key);
    }
    return QString::fromLatin1(kPositionKeys[1].key);
}

TaskbarLyricPreferences::TaskbarLyricPreferences(QSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_position = positionFromKey(
        settings.value(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::Position).toString());
    setMaxWidth(settings
                    .value(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::MaxWidth,
                           m_maxWidth)
                    .toInt());
    setFontSize(settings
                    .value(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::FontSize,
                           m_fontSize)
                    .toInt());
    setShowTranslation(
        settings
            .value(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::ShowTranslation,
                   m_showTranslation)
            .toBool());
    setWordByWord(
        settings
            .value(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::WordByWord,
                   m_wordByWord)
            .toBool());
    setDragOffsetX(
        settings
            .value(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::DragOffsetX,
                   m_dragOffsetX)
            .toInt());
}

void TaskbarLyricPreferences::setPosition(Position value)
{
    if (m_position == value)
        return;
    m_position = value;
    m_settings.setValue(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::Position,
                        positionKey(value));
    emit changed();
}

void TaskbarLyricPreferences::setMaxWidth(int value)
{
    // The range mirrors the settings spin box (160 - 800) and the reference
    // EchoMusic shared/taskbar.ts clamp. The QML side owns the picker range;
    // this clamp is the storage-side guard for values written by an older
    // build or a hand-edited settings file.
    value = qBound(160, value, 800);
    if (m_maxWidth == value)
        return;
    m_maxWidth = value;
    m_settings.setValue(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::MaxWidth,
                        value);
    emit changed();
}

void TaskbarLyricPreferences::setFontSize(int value)
{
    // The range mirrors the settings spin box (11 - 22) and the reference
    // EchoMusic shared/taskbar.ts clamp; see setMaxWidth for the split of
    // responsibilities between the picker and the storage guard.
    value = qBound(11, value, 22);
    if (m_fontSize == value)
        return;
    m_fontSize = value;
    m_settings.setValue(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::FontSize,
                        value);
    emit changed();
}

void TaskbarLyricPreferences::setShowTranslation(bool value)
{
    if (m_showTranslation == value)
        return;
    m_showTranslation = value;
    m_settings.setValue(
        SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::ShowTranslation, value);
    emit changed();
}

void TaskbarLyricPreferences::setWordByWord(bool value)
{
    if (m_wordByWord == value)
        return;
    m_wordByWord = value;
    m_settings.setValue(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::WordByWord,
                        value);
    emit changed();
}

void TaskbarLyricPreferences::setDragOffsetX(int value)
{
    if (m_dragOffsetX == value)
        return;
    m_dragOffsetX = qMax(0, value);
    m_settings.setValue(SettingsKeys::Taskbar::Prefix + SettingsKeys::TaskbarFields::DragOffsetX,
                        m_dragOffsetX);
    emit changed();
}
} // namespace EchoMusic
