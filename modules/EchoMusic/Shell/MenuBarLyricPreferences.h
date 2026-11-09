#pragma once
#include "MenuBarLyric.h"
#include "SettingsStore.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class MenuBarLyricPreferences final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the menu bar lyric controller")
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int secondaryContent READ secondaryContent WRITE setSecondaryContent NOTIFY changed)
    Q_PROPERTY(int displayMode READ displayMode WRITE setDisplayMode NOTIFY changed)
    Q_PROPERTY(int maxWidth READ maxWidth WRITE setMaxWidth NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(int fontWeight READ fontWeight WRITE setFontWeight NOTIFY changed)
    Q_PROPERTY(
        int secondaryColorMode READ secondaryColorMode WRITE setSecondaryColorMode NOTIFY changed)
    Q_PROPERTY(int colorMode READ colorMode WRITE setColorMode NOTIFY changed)
    Q_PROPERTY(QColor primaryColor READ primaryColor WRITE setPrimaryColor NOTIFY changed)
    Q_PROPERTY(QColor secondaryColor READ secondaryColor WRITE setSecondaryColor NOTIFY changed)
    Q_PROPERTY(int opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(bool wordByWord READ wordByWord WRITE setWordByWord NOTIFY changed)
    Q_PROPERTY(bool scrolling READ scrolling WRITE setScrolling NOTIFY changed)
    Q_PROPERTY(int pauseBehavior READ pauseBehavior WRITE setPauseBehavior NOTIFY changed)
    Q_PROPERTY(int idleBehavior READ idleBehavior WRITE setIdleBehavior NOTIFY changed)
public:
    explicit MenuBarLyricPreferences(SettingsStore &settings, QObject *parent = nullptr);
    bool enabled() const
    {
        return m_enabled;
    }
    int secondaryContent() const
    {
        return m_secondaryContent;
    }
    void setSecondaryContent(int value);
    int displayMode() const
    {
        return m_style.displayMode;
    }
    int maxWidth() const
    {
        return m_style.maxWidth;
    }
    int fontSize() const
    {
        return m_style.fontSize;
    }
    int fontWeight() const
    {
        return m_style.fontWeight;
    }
    int secondaryColorMode() const
    {
        return m_style.secondaryColorMode;
    }
    void setSecondaryColorMode(int value);
    int colorMode() const
    {
        return m_style.colorMode;
    }
    QColor primaryColor() const
    {
        return m_style.primaryColor;
    }
    QColor secondaryColor() const
    {
        return m_style.secondaryColor;
    }
    int opacity() const
    {
        return m_style.opacity;
    }
    bool wordByWord() const
    {
        return m_style.wordByWord;
    }
    bool scrolling() const
    {
        return m_style.scrolling;
    }
    int pauseBehavior() const
    {
        return m_pauseBehavior;
    }
    int idleBehavior() const
    {
        return m_idleBehavior;
    }
    MenuBarLyricStyle style() const
    {
        return m_style;
    }
    void setEnabled(bool value);
    void setDisplayMode(int value);
    void setMaxWidth(int value);
    void setFontSize(int value);
    void setFontWeight(int value);
    void setColorMode(int value);
    void setPrimaryColor(const QColor &value);
    void setSecondaryColor(const QColor &value);
    void setOpacity(int value);
    void setWordByWord(bool value);
    void setScrolling(bool value);
    void setPauseBehavior(int value);
    void setIdleBehavior(int value);
signals:
    void changed();

private:
    void save();
    SettingsStore &m_settings;
    MenuBarLyricStyle m_style;
    bool m_enabled = false;
    int m_secondaryContent = 2; // translation, romanization, both
    int m_pauseBehavior = 0;    // keep, dim, hide
    int m_idleBehavior = 1;     // title, previous visible line, hide
};
} // namespace EchoMusic
