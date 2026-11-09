#pragma once
#include <QObject>
#include "SettingsStore.h"
namespace EchoMusic
{
/// Playback-owned text preferences for the centered, lyrics-only built-in skin.
class LyricSimpleAppearance final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString secondaryTextColor READ secondaryTextColor WRITE setSecondaryTextColor NOTIFY
                   changed)
    Q_PROPERTY(bool secondaryUseCoverColor READ secondaryUseCoverColor WRITE
                   setSecondaryUseCoverColor NOTIFY changed)
    Q_PROPERTY(double fontScale READ fontScale WRITE setFontScale NOTIFY changed)
    Q_PROPERTY(int fontWeight READ fontWeight WRITE setFontWeight NOTIFY changed)
    Q_PROPERTY(QString textColor READ textColor WRITE setTextColor NOTIFY changed)
    Q_PROPERTY(
        QString unplayedTextColor READ unplayedTextColor WRITE setUnplayedTextColor NOTIFY changed)
    Q_PROPERTY(bool useCoverColor READ useCoverColor WRITE setUseCoverColor NOTIFY changed)
    Q_PROPERTY(bool unplayedUseCoverColor READ unplayedUseCoverColor WRITE setUnplayedUseCoverColor
                   NOTIFY changed)
    Q_PROPERTY(bool customized READ customized NOTIFY changed)
public:
    QString secondaryTextColor() const
    {
        return m_secondaryTextColor;
    }
    bool secondaryUseCoverColor() const
    {
        return m_secondaryUseCoverColor;
    }
    void setSecondaryTextColor(const QString &value);
    void setSecondaryUseCoverColor(bool value);
    explicit LyricSimpleAppearance(SettingsStore &settings, QObject *parent = nullptr);
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
    bool customized() const;
    Q_INVOKABLE void resetSkin();
signals:
    void changed();

private:
    QString m_secondaryTextColor;
    bool m_secondaryUseCoverColor = false;
    SettingsStore &m_settings;
    double m_fontScale = 1.0;
    int m_fontWeight = 900;
    QString m_textColor = QString();
    QString m_unplayedTextColor = QString();
    bool m_useCoverColor = false;
    bool m_unplayedUseCoverColor = false;
};
} // namespace EchoMusic
