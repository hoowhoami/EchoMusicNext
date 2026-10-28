#pragma once
#include <QObject>
#include <QSettings>
namespace EchoMusic
{
/// Independent portrait skin settings; global lyric preferences stay in LyricAppearance.
class LyricPortraitAppearance final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int backdropOpacity READ backdropOpacity WRITE setBackdropOpacity NOTIFY changed)
    Q_PROPERTY(bool carouselEnabled READ carouselEnabled WRITE setCarouselEnabled NOTIFY changed)
    Q_PROPERTY(int carouselInterval READ carouselInterval WRITE setCarouselInterval NOTIFY changed)
    Q_PROPERTY(bool fallbackCover READ fallbackCover WRITE setFallbackCover NOTIFY changed)
    Q_PROPERTY(bool autoCollapse READ autoCollapse WRITE setAutoCollapse NOTIFY changed)
    Q_PROPERTY(int collapseDelay READ collapseDelay WRITE setCollapseDelay NOTIFY changed)
    Q_PROPERTY(bool hideControls READ hideControls WRITE setHideControls NOTIFY changed)
    Q_PROPERTY(double fontScale READ fontScale WRITE setFontScale NOTIFY changed)
    Q_PROPERTY(int fontWeight READ fontWeight WRITE setFontWeight NOTIFY changed)
    Q_PROPERTY(QString textColor READ textColor WRITE setTextColor NOTIFY changed)
    Q_PROPERTY(
        QString unplayedTextColor READ unplayedTextColor WRITE setUnplayedTextColor NOTIFY changed)
    Q_PROPERTY(bool useCoverColor READ useCoverColor WRITE setUseCoverColor NOTIFY changed)
    Q_PROPERTY(bool unplayedUseCoverColor READ unplayedUseCoverColor WRITE setUnplayedUseCoverColor
                   NOTIFY changed)
    Q_PROPERTY(bool customized READ customized NOTIFY changed)
    Q_PROPERTY(bool enableSpring READ enableSpring WRITE setEnableSpring NOTIFY changed)
    Q_PROPERTY(bool enableBlur READ enableBlur WRITE setEnableBlur NOTIFY changed)
    Q_PROPERTY(bool enableScale READ enableScale WRITE setEnableScale NOTIFY changed)
public:
    explicit LyricPortraitAppearance(QSettings &settings, QObject *parent = nullptr);
    int backdropOpacity() const
    {
        return m_backdropOpacity;
    }
    void setBackdropOpacity(int value);
    bool carouselEnabled() const
    {
        return m_carouselEnabled;
    }
    void setCarouselEnabled(bool value);
    int carouselInterval() const
    {
        return m_carouselInterval;
    }
    void setCarouselInterval(int value);
    bool fallbackCover() const
    {
        return m_fallbackCover;
    }
    void setFallbackCover(bool value);
    bool autoCollapse() const
    {
        return m_autoCollapse;
    }
    void setAutoCollapse(bool value);
    int collapseDelay() const
    {
        return m_collapseDelay;
    }
    void setCollapseDelay(int value);
    bool hideControls() const
    {
        return m_hideControls;
    }
    void setHideControls(bool value);
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
    QString textColor() const
    {
        return m_textColor;
    }
    void setTextColor(const QString &value);
    QString unplayedTextColor() const
    {
        return m_unplayedTextColor;
    }
    void setUnplayedTextColor(const QString &value);
    bool useCoverColor() const
    {
        return m_useCoverColor;
    }
    void setUseCoverColor(bool value);
    bool unplayedUseCoverColor() const
    {
        return m_unplayedUseCoverColor;
    }
    void setUnplayedUseCoverColor(bool value);
    bool enableSpring() const
    {
        return m_enableSpring;
    }
    void setEnableSpring(bool value);
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
    bool customized() const;
    Q_INVOKABLE void reset();
signals:
    void changed();

private:
    bool m_enableSpring = true;
    bool m_enableBlur = true;
    bool m_enableScale = true;
    QSettings &m_settings;
    int m_backdropOpacity = 50;
    bool m_carouselEnabled = true;
    int m_carouselInterval = 15;
    bool m_fallbackCover = false;
    bool m_autoCollapse = true;
    int m_collapseDelay = 5;
    bool m_hideControls = false;
    double m_fontScale = 1.0;
    int m_fontWeight = 700;
    QString m_textColor = QString();
    QString m_unplayedTextColor = QString();
    bool m_useCoverColor = false;
    bool m_unplayedUseCoverColor = false;
};
} // namespace EchoMusic
