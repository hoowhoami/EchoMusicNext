#pragma once
#include <QObject>
#include <QSettings>
namespace EchoMusic
{
/// GUI-thread, application-owned cover lyric preferences. Only the implemented
/// cover skin is exposed; no old provider IDs, schema import or mutable QML copy.
///
/// Colors have two layers: page colors are the global baseline and the active
/// skin may override either slot. Resolution is per slot, so an unset skin
/// value keeps inheriting the global one instead of disabling the whole layer.
/// Without any explicit unplayed color the unplayed slot follows the resolved
/// played color at 20% alpha.
class LyricAppearance final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString skinKey READ skinKey CONSTANT)
    Q_PROPERTY(bool customized READ customized NOTIFY changed)
    Q_PROPERTY(double alignPosition READ alignPosition WRITE setAlignPosition NOTIFY changed)
    Q_PROPERTY(bool enableSpring READ enableSpring WRITE setEnableSpring NOTIFY changed)
    Q_PROPERTY(bool highRefreshRate READ highRefreshRate WRITE setHighRefreshRate NOTIFY changed)
    Q_PROPERTY(bool enableBlur READ enableBlur WRITE setEnableBlur NOTIFY changed)
    Q_PROPERTY(bool enableScale READ enableScale WRITE setEnableScale NOTIFY changed)
    Q_PROPERTY(bool hidePassedLines READ hidePassedLines WRITE setHidePassedLines NOTIFY changed)
    Q_PROPERTY(double wordFadeWidth READ wordFadeWidth WRITE setWordFadeWidth NOTIFY changed)
    Q_PROPERTY(
        bool dynamicAlbumCover READ dynamicAlbumCover WRITE setDynamicAlbumCover NOTIFY changed)
    Q_PROPERTY(QString textColor READ textColor WRITE setTextColor NOTIFY changed)
    Q_PROPERTY(bool useCoverColor READ useCoverColor WRITE setUseCoverColor NOTIFY changed)
    Q_PROPERTY(double fontScale READ fontScale WRITE setFontScale NOTIFY changed)
    Q_PROPERTY(int fontWeight READ fontWeight WRITE setFontWeight NOTIFY changed)
    Q_PROPERTY(bool backgroundBlur READ backgroundBlur WRITE setBackgroundBlur NOTIFY changed)
    Q_PROPERTY(bool backgroundRhythm READ backgroundRhythm WRITE setBackgroundRhythm NOTIFY changed)
    Q_PROPERTY(QString playedColor READ playedColor WRITE setPlayedColor NOTIFY changed)
    Q_PROPERTY(QString unplayedColor READ unplayedColor WRITE setUnplayedColor NOTIFY changed)
    Q_PROPERTY(bool followCoverColor READ followCoverColor WRITE setFollowCoverColor NOTIFY changed)
    Q_PROPERTY(bool unplayedFollowCoverColor READ unplayedFollowCoverColor WRITE
                   setUnplayedFollowCoverColor NOTIFY changed)
    Q_PROPERTY(
        QString unplayedTextColor READ unplayedTextColor WRITE setUnplayedTextColor NOTIFY changed)
    Q_PROPERTY(bool unplayedUseCoverColor READ unplayedUseCoverColor WRITE setUnplayedUseCoverColor
                   NOTIFY changed)
public:
    QString playedColor() const
    {
        return m_playedColor;
    }
    QString unplayedColor() const
    {
        return m_unplayedColor;
    }
    bool followCoverColor() const
    {
        return m_followCoverColor;
    }
    bool unplayedFollowCoverColor() const
    {
        return m_unplayedFollowCoverColor;
    }
    QString unplayedTextColor() const
    {
        return m_unplayedTextColor;
    }
    bool unplayedUseCoverColor() const
    {
        return m_unplayedUseCoverColor;
    }
    void setPlayedColor(const QString &);
    void setUnplayedColor(const QString &);
    void setFollowCoverColor(bool);
    void setUnplayedFollowCoverColor(bool);
    void setUnplayedTextColor(const QString &);
    void setUnplayedUseCoverColor(bool);
    Q_INVOKABLE void resetColors();
    Q_INVOKABLE void resetSkinColors();
    explicit LyricAppearance(QSettings &settings, QObject *parent = nullptr);
    QString skinKey() const
    {
        return QStringLiteral("cover");
    }
    bool customized() const;
    Q_INVOKABLE void resetSkin();
    double alignPosition() const
    {
        return m_alignPosition;
    }
    void setAlignPosition(double value);
    bool enableSpring() const
    {
        return m_enableSpring;
    }
    void setEnableSpring(bool value);
    bool highRefreshRate() const
    {
        return m_highRefreshRate;
    }
    void setHighRefreshRate(bool value);
    bool enableBlur() const
    {
        return m_enableBlur;
    }
    void setEnableBlur(bool value);
    bool enableScale() const
    {
        return m_enableScale;
    }
    void setEnableScale(bool value);
    bool hidePassedLines() const
    {
        return m_hidePassedLines;
    }
    void setHidePassedLines(bool value);
    double wordFadeWidth() const
    {
        return m_wordFadeWidth;
    }
    void setWordFadeWidth(double value);
    bool dynamicAlbumCover() const
    {
        return m_dynamicAlbumCover;
    }
    void setDynamicAlbumCover(bool value);
    QString textColor() const
    {
        return m_textColor;
    }
    void setTextColor(const QString &value);
    bool useCoverColor() const
    {
        return m_useCoverColor;
    }
    void setUseCoverColor(bool value);
    double fontScale() const
    {
        return m_fontScale;
    }
    void setFontScale(double value);
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
    bool backgroundRhythm() const
    {
        return m_backgroundRhythm;
    }
    void setBackgroundRhythm(bool value);
signals:
    void changed();

private:
    QSettings &m_settings;
    QString m_playedColor, m_unplayedColor;
    bool m_followCoverColor = false, m_unplayedFollowCoverColor = false;
    QString m_unplayedTextColor;
    bool m_unplayedUseCoverColor = false;
    double m_alignPosition = 0.5;
    bool m_enableSpring = true;
    bool m_highRefreshRate = false;
    bool m_enableBlur = true;
    bool m_enableScale = true;
    bool m_hidePassedLines = false;
    double m_wordFadeWidth = 0.5;
    bool m_dynamicAlbumCover = false;
    QString m_textColor = {};
    bool m_useCoverColor = false;
    double m_fontScale = 1.;
    int m_fontWeight = 900;
    bool m_backgroundBlur = false;
    bool m_backgroundRhythm = false;
};
} // namespace EchoMusic
