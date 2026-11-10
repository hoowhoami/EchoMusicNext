#pragma once
#include <QObject>
#include "SettingsStore.h"
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
/// Taskbar lyric presentation preferences (EchoMusic::SettingsStore-backed). The reference
/// EchoMusic clamps: maxWidth 160-800, fontSize 11-22 (shared/taskbar.ts).
///
/// The cover is always shown and always opens the main window on a click;
/// neither is a preference.
class TaskbarLyricPreferences final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString secondaryColor READ secondaryColor WRITE setSecondaryColor NOTIFY changed)
    Q_PROPERTY(bool secondaryFollowCoverColor READ secondaryFollowCoverColor WRITE
                   setSecondaryFollowCoverColor NOTIFY changed)
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the window controller")
public:
    /// Half the 12 DIP seek area in TaskbarLyricWindow.
    static constexpr int InfoOffsetMax = 6;
    QString secondaryColor() const
    {
        return m_secondaryColor;
    }
    bool secondaryFollowCoverColor() const
    {
        return m_secondaryFollowCoverColor;
    }
    void setSecondaryColor(const QString &value);
    void setSecondaryFollowCoverColor(bool value);
    /// How the strip picks its seat. The declaration order is the settings
    /// picker's order (the combo binds its index to this value), and the stored
    /// key of each mode lives in positionKey() only.
    enum class Position
    {
        AutoLeft,   ///< prefer the left free area, fall back to the right one
        AutoRight,  ///< prefer the right free area, fall back to the left one
        FixedLeft,  ///< pin to the left edge of the whole taskbar
        FixedRight, ///< pin to the right edge of the whole taskbar
        Drag,       ///< wherever the user dropped the strip (dragOffsetX)
    };
    Q_ENUM(Position)
    Q_PROPERTY(Position position READ position WRITE setPosition NOTIFY changed)
    Q_PROPERTY(int maxWidth READ maxWidth WRITE setMaxWidth NOTIFY changed)
    Q_PROPERTY(int infoOffset READ infoOffset WRITE setInfoOffset NOTIFY changed)
    Q_PROPERTY(int infoOffsetMax READ infoOffsetMax CONSTANT)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(bool showTranslation READ showTranslation WRITE setShowTranslation NOTIFY changed)
    Q_PROPERTY(bool wordByWord READ wordByWord WRITE setWordByWord NOTIFY changed)
    Q_PROPERTY(int dragOffsetX READ dragOffsetX WRITE setDragOffsetX NOTIFY changed)
public:
    explicit TaskbarLyricPreferences(EchoMusic::SettingsStore &settings, QObject *parent = nullptr);
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
    /// Vertical lift (DIP) of the expanded song info above the strip's seek
    /// area. 0 keeps the row vertically centered; the ceiling is
    /// InfoOffsetMax, half the seek area's height (TaskbarLyricWindow's
    /// TaskbarLyricSeekBar, 12 DIP).
    int infoOffset() const
    {
        return m_infoOffset;
    }
    int infoOffsetMax() const
    {
        return InfoOffsetMax;
    }
    void setInfoOffset(int value);
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
    QString m_secondaryColor;
    bool m_secondaryFollowCoverColor = false;
    EchoMusic::SettingsStore &m_settings;
    Position m_position = Position::AutoRight;
    int m_maxWidth = 400;
    /// Defaults to InfoOffsetMax: keeps the shift the strip shipped with
    /// before this preference existed.
    int m_infoOffset = InfoOffsetMax;
    int m_fontSize = 14;
    bool m_showTranslation = true;
    bool m_wordByWord = true;
    int m_dragOffsetX = 0;
};
} // namespace EchoMusic
