#pragma once
#include <QObject>
#include "SettingsStore.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Per-view preferences only; playback, shared lyric choices and timing stay in PlaybackService.
class FloatingLyricPreferences final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the window controller")
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY changed)
    Q_PROPERTY(bool showUnlockButton READ showUnlockButton WRITE setShowUnlockButton NOTIFY changed)
    Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY changed)
    Q_PROPERTY(QString alignment READ alignment WRITE setAlignment NOTIFY changed)
    Q_PROPERTY(QString shadowStrength READ shadowStrength WRITE setShadowStrength NOTIFY changed)
    Q_PROPERTY(bool bold READ bold WRITE setBold NOTIFY changed)
    Q_PROPERTY(bool showNextLinePreview READ showNextLinePreview WRITE setShowNextLinePreview NOTIFY
                   changed)
    Q_PROPERTY(QString playedColor READ playedColor WRITE setPlayedColor NOTIFY changed)
    Q_PROPERTY(QString unplayedColor READ unplayedColor WRITE setUnplayedColor NOTIFY changed)
    Q_PROPERTY(bool followCoverColor READ followCoverColor WRITE setFollowCoverColor NOTIFY changed)
    Q_PROPERTY(bool unplayedFollowCoverColor READ unplayedFollowCoverColor WRITE
                   setUnplayedFollowCoverColor NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(
        int secondaryFontSize READ secondaryFontSize WRITE setSecondaryFontSize NOTIFY changed)
    Q_PROPERTY(int lineGap READ lineGap WRITE setLineGap NOTIFY changed)
    Q_PROPERTY(int fontWeight READ fontWeight WRITE setFontWeight NOTIFY changed)
    Q_PROPERTY(bool backgroundBlur READ backgroundBlur WRITE setBackgroundBlur NOTIFY changed)
public:
    FloatingLyricPreferences(EchoMusic::SettingsStore &settings, bool desktop,
                             QObject *parent = nullptr);
    bool alwaysOnTop() const
    {
        return m_alwaysOnTop;
    }
    void setAlwaysOnTop(bool value);
    bool showUnlockButton() const
    {
        return m_showUnlockButton;
    }
    void setShowUnlockButton(bool value);
    QString layout() const
    {
        return m_layout;
    }
    void setLayout(QString value);
    QString alignment() const
    {
        return m_alignment;
    }
    void setAlignment(QString value);
    QString shadowStrength() const
    {
        return m_shadowStrength;
    }
    void setShadowStrength(QString value);
    bool bold() const
    {
        return m_bold;
    }
    void setBold(bool value);
    bool showNextLinePreview() const
    {
        return m_showNextLinePreview;
    }
    void setShowNextLinePreview(bool value);
    QString playedColor() const
    {
        return m_playedColor;
    }
    void setPlayedColor(QString value);
    QString unplayedColor() const
    {
        return m_unplayedColor;
    }
    void setUnplayedColor(QString value);
    bool followCoverColor() const
    {
        return m_followCoverColor;
    }
    void setFollowCoverColor(bool value);
    bool unplayedFollowCoverColor() const
    {
        return m_unplayedFollowCoverColor;
    }
    void setUnplayedFollowCoverColor(bool value);
    int fontSize() const
    {
        return m_fontSize;
    }
    void setFontSize(int value);
    int secondaryFontSize() const
    {
        return m_secondaryFontSize;
    }
    void setSecondaryFontSize(int value);
    int lineGap() const
    {
        return m_lineGap;
    }
    void setLineGap(int value);
    int fontWeight() const
    {
        return m_fontWeight;
    }
    void setFontWeight(int value);
    bool backgroundBlur() const
    {
        return m_backgroundBlur;
    }
    void setBackgroundBlur(bool value);
    Q_INVOKABLE void resetColors();
    Q_INVOKABLE void resetLayout();
signals:
    void changed();

private:
    EchoMusic::SettingsStore &m_settings;
    QString m_prefix;
    bool m_mini;
    bool m_alwaysOnTop;
    bool m_showUnlockButton;
    QString m_layout;
    QString m_alignment;
    QString m_shadowStrength;
    bool m_bold;
    bool m_showNextLinePreview;
    QString m_playedColor;
    QString m_unplayedColor;
    bool m_followCoverColor;
    bool m_unplayedFollowCoverColor;
    int m_fontSize;
    int m_secondaryFontSize;
    int m_lineGap;
    int m_fontWeight;
    bool m_backgroundBlur;
};
} // namespace EchoMusic
