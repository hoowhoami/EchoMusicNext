#pragma once
#include <QObject>
#include <QSettings>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Taskbar lyric presentation preferences (QSettings-backed). The reference
/// EchoMusic clamps: maxWidth 160-800, fontSize 11-22 (shared/taskbar.ts).
///
/// The cover is always shown and always opens the main window on a click;
/// neither is a preference.
class TaskbarLyricPreferences final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the window controller")
public:
    /// How the strip picks its seat. The declaration order is the settings
    /// picker's order (the combo binds its index to this value), and the stored
    /// key of each mode lives in positionKey() only.
    enum class Position
    {
        AutoLeft,  ///< prefer the left free area, fall back to the right one
        AutoRight, ///< prefer the right free area, fall back to the left one
        FixedLeft, ///< pin to the left edge of the whole taskbar
        FixedRight, ///< pin to the right edge of the whole taskbar
        Drag,      ///< wherever the user dropped the strip (dragOffsetX)
    };
    Q_ENUM(Position)
    Q_PROPERTY(Position position READ position WRITE setPosition NOTIFY changed)
    Q_PROPERTY(int maxWidth READ maxWidth WRITE setMaxWidth NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(bool showTranslation READ showTranslation WRITE setShowTranslation NOTIFY changed)
    Q_PROPERTY(bool wordByWord READ wordByWord WRITE setWordByWord NOTIFY changed)
    Q_PROPERTY(int dragOffsetX READ dragOffsetX WRITE setDragOffsetX NOTIFY changed)
public:
    explicit TaskbarLyricPreferences(QSettings &settings, QObject *parent = nullptr);
    Position position() const
    {
        return m_position;
    }
    void setPosition(Position value);
    /// The on-disk form of a mode ("left", "right", "fixedLeft", "fixedRight",
    /// "drag"). Kept out of the enum so settings files stay readable and a
    /// rename of the enumerators never orphans them.
    static QString positionKey(Position value);
    int maxWidth() const
    {
        return m_maxWidth;
    }
    void setMaxWidth(int value);
    int fontSize() const
    {
        return m_fontSize;
    }
    void setFontSize(int value);
    bool showTranslation() const
    {
        return m_showTranslation;
    }
    void setShowTranslation(bool value);
    bool wordByWord() const
    {
        return m_wordByWord;
    }
    void setWordByWord(bool value);
    /// Left edge of the strip inside the taskbar client area (physical
    /// pixels), remembered for the manual drag mode.
    int dragOffsetX() const
    {
        return m_dragOffsetX;
    }
    void setDragOffsetX(int value);

signals:
    void changed();

private:
    QSettings &m_settings;
    Position m_position = Position::AutoRight;
    int m_maxWidth = 400;
    int m_fontSize = 14;
    bool m_showTranslation = true;
    bool m_wordByWord = true;
    int m_dragOffsetX = 0;
};
} // namespace EchoMusic
