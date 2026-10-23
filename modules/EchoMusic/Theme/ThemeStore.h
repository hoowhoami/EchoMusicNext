#pragma once
#include "AppearanceCatalog.h"
#include <QColor>
#include <atomic>
#include <memory>
#include <QFont>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QJsonObject>
#include <QRectF>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
struct ThemePalette
{
    QColor shell, sidebar, main, player, card, elevated, text, secondary, border;
};
/// Application-owned appearance preferences and derived semantic colors. Every
/// window uses the same instance; QML has no independent mutable theme state.
class ThemeStore final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(EchoMusic::AppearanceCatalog *extensions READ extensions CONSTANT)
    Q_PROPERTY(QVariantMap extensionAppearance READ extensionAppearance NOTIFY changed)
    Q_PROPERTY(bool motionEnabled READ motionEnabled NOTIFY changed)
    Q_PROPERTY(QFont applicationFont READ applicationFont NOTIFY fontChanged)
    Q_PROPERTY(QString globalFont READ globalFont WRITE setGlobalFont NOTIFY fontChanged)
    Q_PROPERTY(QString lyricFont READ lyricFont WRITE setLyricFont NOTIFY lyricFontsChanged)
    Q_PROPERTY(QString desktopLyricFont READ desktopLyricFont WRITE setDesktopLyricFont NOTIFY
                   lyricFontsChanged)
    Q_PROPERTY(
        QString miniLyricFont READ miniLyricFont WRITE setMiniLyricFont NOTIFY lyricFontsChanged)
    Q_PROPERTY(QString taskbarLyricFont READ taskbarLyricFont WRITE setTaskbarLyricFont NOTIFY
                   lyricFontsChanged)
    Q_PROPERTY(QFont lyricBaseFont READ lyricBaseFont NOTIFY lyricFontsChanged)
    Q_PROPERTY(QFont desktopLyricBaseFont READ desktopLyricBaseFont NOTIFY lyricFontsChanged)
    Q_PROPERTY(QFont miniLyricBaseFont READ miniLyricBaseFont NOTIFY lyricFontsChanged)
    Q_PROPERTY(QFont taskbarLyricBaseFont READ taskbarLyricBaseFont NOTIFY lyricFontsChanged)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(Mode mode READ mode NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString themeKey READ themeKey NOTIFY changed)
    Q_PROPERTY(QColor seedColor READ seedColor NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor customColor READ customColor NOTIFY changed)
    Q_PROPERTY(bool customAccent READ customAccent NOTIFY changed)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY changed)
    Q_PROPERTY(QColor accentPressed READ accentPressed NOTIFY changed)
    Q_PROPERTY(QColor atmosphere READ atmosphere NOTIFY changed)
    Q_PROPERTY(QColor shell READ shell NOTIFY changed)
    Q_PROPERTY(QColor sidebar READ sidebar NOTIFY changed)
    Q_PROPERTY(QColor main READ main NOTIFY changed)
    Q_PROPERTY(QColor player READ player NOTIFY changed)
    Q_PROPERTY(QColor card READ card NOTIFY changed)
    Q_PROPERTY(QColor elevated READ elevated NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor tone READ tone NOTIFY changed)
    Q_PROPERTY(QColor floatingTone READ floatingTone NOTIFY changed)
    Q_PROPERTY(QColor secondaryText READ secondaryText NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor controlHover READ controlHover NOTIFY changed)
    Q_PROPERTY(QColor mutedSurface READ mutedSurface NOTIFY changed)
    Q_PROPERTY(QColor selectedSurface READ selectedSurface NOTIFY changed)
    Q_PROPERTY(int panelOpacity READ panelOpacity NOTIFY changed)
    Q_PROPERTY(QColor panelColor READ panelColor NOTIFY changed)
    Q_PROPERTY(QColor panelBackdrop READ panelBackdrop NOTIFY changed)
    Q_PROPERTY(QColor floatingText READ floatingText NOTIFY changed)
    Q_PROPERTY(QColor floatingSecondaryText READ floatingSecondaryText NOTIFY changed)
    Q_PROPERTY(QColor floatingCard READ floatingCard NOTIFY changed)
    Q_PROPERTY(QColor floatingBorder READ floatingBorder NOTIFY changed)
    Q_PROPERTY(QColor floatingAccentText READ floatingAccentText NOTIFY changed)
    Q_PROPERTY(QColor floatingAccent READ floatingAccent NOTIFY changed)
    Q_PROPERTY(QString accentSource READ accentSource NOTIFY changed)
    Q_PROPERTY(bool coverAtmosphere READ coverAtmosphere WRITE setCoverAtmosphere NOTIFY changed)
    Q_PROPERTY(int atmosphereHeight READ atmosphereHeight WRITE setAtmosphereHeight NOTIFY changed)
    Q_PROPERTY(
        int atmosphereStrength READ atmosphereStrength WRITE setAtmosphereStrength NOTIFY changed)
    Q_PROPERTY(QColor coverAtmosphereColor READ coverAtmosphereColor NOTIFY changed)
    Q_PROPERTY(int transparency READ transparency WRITE setTransparency NOTIFY changed)
    Q_PROPERTY(bool windowFrosted READ windowFrosted WRITE setWindowFrosted NOTIFY changed)
    Q_PROPERTY(bool floatingFrosted READ floatingFrosted WRITE setFloatingFrosted NOTIFY changed)
    Q_PROPERTY(QUrl backgroundImage READ backgroundImage NOTIFY changed)
    Q_PROPERTY(QSize backgroundSize READ backgroundSize NOTIFY changed)
    Q_PROPERTY(QRectF backgroundCrop READ backgroundCrop NOTIFY changed)
    Q_PROPERTY(QColor backgroundText READ backgroundText NOTIFY changed)
    Q_PROPERTY(QColor backgroundAccent READ backgroundAccent NOTIFY changed)
    Q_PROPERTY(int backgroundShade READ backgroundShade NOTIFY changed)
    Q_PROPERTY(bool backgroundPreview READ backgroundPreview NOTIFY changed)
    Q_PROPERTY(bool imageImporting READ imageImporting NOTIFY changed)
    Q_PROPERTY(QString imageError READ imageError NOTIFY changed)
public:
    AppearanceCatalog *extensions() const
    {
        return m_extensions;
    }
    QVariantMap extensionAppearance() const
    {
        return m_extensionAppearance;
    }
    enum Mode
    {
        System,
        Light,
        Dark,
        ThemeDefault
    };
    Q_ENUM(Mode)
    Q_INVOKABLE QFont styledFont(const QFont &base, qreal pixelSize, int weight,
                                 qreal letterSpacing, bool italic, bool underline) const;
    explicit ThemeStore(QSettings &settings, QObject *parent = nullptr,
                        const QString &imageDirectory = {});
    ~ThemeStore() override;
    /// Bind before loading QML. The engine borrows this application-owned store;
    /// each engine's binding is immutable and never falls back to global state.
    void attachToEngine(QQmlEngine &engine);
    static ThemeStore *create(QQmlEngine *engine, QJSEngine *);
    QFont applicationFont() const;
    QString globalFont() const
    {
        return m_globalFont;
    }
    QStringList fontFamilies() const;
    void setGlobalFont(const QString &family);
    QString lyricFont() const
    {
        return m_lyricFont;
    }
    QString desktopLyricFont() const
    {
        return m_desktopLyricFont;
    }
    QString miniLyricFont() const
    {
        return m_miniLyricFont;
    }
    QString taskbarLyricFont() const
    {
        return m_taskbarLyricFont;
    }
    void setLyricFont(const QString &family);
    void setDesktopLyricFont(const QString &family);
    void setMiniLyricFont(const QString &family);
    void setTaskbarLyricFont(const QString &family);
    QFont lyricBaseFont() const;
    QFont desktopLyricBaseFont() const;
    QFont miniLyricBaseFont() const;
    QFont taskbarLyricBaseFont() const;
    bool motionEnabled() const
    {
        return m_motion;
    }
    void setMotionEnabled(bool enabled)
    {
        if (m_motion != enabled) {
            m_motion = enabled;
            emit changed();
        }
    }
    Mode mode() const
    {
        return m_mode;
    }
    bool dark() const
    {
        if (m_mode == ThemeDefault) {
            auto entry = m_extensions ? m_extensions->theme() : nullptr;
            if (entry && entry->defaultMode != "system")
                return entry->defaultMode == "dark";
            return m_systemDark;
        }
        return m_mode == Dark || (m_mode == System && m_systemDark);
    }
    QString themeKey() const
    {
        return m_key;
    }
    QColor seedColor() const
    {
        return m_seed;
    }
    QColor accent() const
    {
        return m_accent;
    }
    QColor accentText() const
    {
        return m_accentText;
    }
    QColor customColor() const
    {
        return m_selectedAccent;
    }
    bool customAccent() const
    {
        return m_customAccent;
    }
    QColor onAccent() const
    {
        return m_onAccent;
    }
    QColor accentHover() const
    {
        return m_accentHover;
    }
    QColor accentPressed() const
    {
        return m_accentPressed;
    }
    QColor atmosphere() const
    {
        return m_atmosphere;
    }
    QColor tone() const
    {
        return dark() ? QColor(Qt::white) : QColor(Qt::black);
    }
    QColor floatingTone() const
    {
        return contrast(m_floatingText, Qt::black) >= contrast(m_floatingText, Qt::white)
                   ? QColor(Qt::white)
                   : QColor(Qt::black);
    }
    QColor shell() const
    {
        return extensionSurface("shell", m_palette.shell);
    }
    QColor sidebar() const
    {
        return extensionSurface("sidebar", m_palette.sidebar);
    }
    QColor main() const
    {
        return extensionSurface("main", m_palette.main);
    }
    QColor player() const
    {
        return extensionSurface("player", m_palette.player);
    }
    QColor card() const
    {
        return extensionSurface("card", m_palette.card);
    }
    QColor elevated() const
    {
        return extensionSurface("elevated", m_palette.elevated);
    }
    QColor text() const
    {
        return m_palette.text;
    }
    QColor secondaryText() const
    {
        return m_palette.secondary;
    }
    QColor border() const
    {
        return m_palette.border;
    }
    QColor controlHover() const;
    QColor mutedSurface() const;
    QColor selectedSurface() const;
    int panelOpacity() const
    {
        return m_panelOpacity;
    }
    QColor panelColor() const;
    QColor panelBackdrop() const;
    QColor floatingText() const
    {
        return m_floatingText;
    }
    QColor floatingSecondaryText() const
    {
        return m_floatingSecondary;
    }
    QColor floatingCard() const
    {
        return m_floatingCard;
    }
    QColor floatingBorder() const
    {
        return m_floatingBorder;
    }
    QColor floatingAccent() const
    {
        const QColor value(
            m_extensionAppearance.value("floating").toMap().value("accent").toString());
        return !m_customAccent && !m_coverAccent && value.isValid() ? value : m_accent;
    }
    QColor floatingAccentText() const
    {
        return m_floatingAccentText;
    }
    Q_INVOKABLE void setPanelOpacity(int opacity);
    Q_INVOKABLE void setMode(Mode mode);
    Q_INVOKABLE void selectEcho();
    Q_INVOKABLE void selectSolid(const QColor &seed);
    Q_INVOKABLE void setAccent(const QColor &color);
    Q_INVOKABLE void useThemeAccent();
    Q_INVOKABLE void resetAppearanceAdjustments();
    QString accentSource() const
    {
        return m_coverAccent ? "cover" : m_customAccent ? "custom" : "theme";
    }
    Q_INVOKABLE void useCoverAccent();
    void setCoverColor(const QColor &color);
    /// Composition root calls once after the single-instance gate.
    void pruneImageAssets();
    bool coverAtmosphere() const
    {
        return m_coverAtmosphere;
    }
    int atmosphereHeight() const
    {
        return m_atmosphereHeight;
    }
    int atmosphereStrength() const
    {
        return m_atmosphereStrength;
    }
    QColor coverAtmosphereColor() const;
    int transparency() const
    {
        return m_transparency;
    }
    bool windowFrosted() const
    {
        return m_windowFrosted;
    }
    void setWindowFrosted(bool enabled);
    bool floatingFrosted() const
    {
        return m_floatingFrosted;
    }
    void setCoverAtmosphere(bool enabled);
    void setAtmosphereHeight(int value);
    void setAtmosphereStrength(int value);
    void setTransparency(int value);
    void setFloatingFrosted(bool enabled);
    QUrl backgroundImage() const;
    QSize backgroundSize() const
    {
        return m_imageSize;
    }
    QRectF backgroundCrop() const
    {
        return m_crop;
    }
    QColor backgroundText() const
    {
        return m_imageText;
    }
    QColor backgroundAccent() const
    {
        return m_imageAccent;
    }
    int backgroundShade() const
    {
        return m_imageShade;
    }
    bool backgroundPreview() const
    {
        return !m_beforeBackground.isEmpty();
    }
    bool imageImporting() const
    {
        return m_importing;
    }
    bool pendingImageImports() const
    {
        return m_pendingImports != 0;
    }
    /// Stop pending preview delivery before settings/reset commits. Import
    /// writers are excluded by the maintenance guard; sampling jobs only read.
    void cancelImageWorkForShutdown();
    QString imageError() const
    {
        return m_imageError;
    }
    Q_INVOKABLE void importBackground(const QUrl &file);
    Q_INVOKABLE void previewBackground();
    Q_INVOKABLE void cancelBackground(bool stayOnCustom = false);
    Q_INVOKABLE bool applyBackground();
    Q_INVOKABLE void setBackgroundCrop(const QRectF &crop);
    Q_INVOKABLE void setBackgroundText(const QColor &color);
    Q_INVOKABLE void setBackgroundAccent(const QColor &color);
    Q_INVOKABLE void setBackgroundShade(int value);
    Q_INVOKABLE void resetBackgroundAdjustments();
    Q_INVOKABLE void endBackgroundPreview();
    /// Ignore errors from an older Image URL after switching or cancelling.
    Q_INVOKABLE void reportBackgroundUnavailable(const QUrl &source);
    Q_INVOKABLE QVariantMap previewPalette(bool dark, const QColor &seed, bool solid) const;
    Q_INVOKABLE QColor lyricAccent(const QColor &cover) const;
    Q_INVOKABLE QColor lyricBackground(const QColor &cover) const;
    Q_INVOKABLE QVariantMap lyricPalette(const QColor &background) const;
    static ThemePalette neutral(bool dark);
    static ThemePalette fromSeed(const QColor &seed, bool dark);
    static qreal contrast(const QColor &a, const QColor &b);
    // System events do not save preferences or override a user's pinned mode.
    void adoptSystemScheme(Qt::ColorScheme scheme);
signals:
    void fontChanged();
    void lyricFontsChanged();
    void changed();
    void backgroundUnavailable(const QString &message);

private:
    bool eventFilter(QObject *object, QEvent *event) override;
    QString m_globalFont = QStringLiteral("system-ui");
    QString m_lyricFont = QStringLiteral("follow");
    QString m_desktopLyricFont = QStringLiteral("follow");
    QString m_miniLyricFont = QStringLiteral("follow");
    QString m_taskbarLyricFont = QStringLiteral("follow");
    void setLyricFontPreference(QString &preference, const QString &key, const QString &family);
    QFont resolvedLyricFont(const QString &family) const;
    QColor extensionSurface(const QString &, const QColor &) const;
    void resolve();
    void save();
    QJsonObject backgroundDraft() const;
    void restoreBackgroundDraft(const QJsonObject &draft);
    void beginBackground();
    void scheduleImageSample();
    void cleanImages();
    QSettings &m_settings;
    AppearanceCatalog *m_extensions = nullptr;
    QVariantMap m_extensionAppearance;
    ThemePalette m_palette;
    Mode m_mode = ThemeDefault;
    bool m_motion = true, m_systemDark = false, m_customAccent = false;
    QString m_key = "echo";
    QColor m_seed = QColor("#6b9bd1"), m_selectedAccent = QColor("#0071e3"), m_accent, m_accentText;
    int m_panelOpacity = 50;
    bool m_coverAccent = false, m_coverAtmosphere = false, m_floatingFrosted = false,
         m_windowFrosted = false;
    int m_atmosphereHeight = 70, m_atmosphereStrength = 100, m_transparency = 0;
    QColor m_coverColor = QColor("#0071e3");
    QString m_imageDirectory, m_imageId, m_imageError;
    QSize m_imageSize;
    QRectF m_crop;
    QColor m_imageText = Qt::white, m_imageAccent = QColor("#00cc65"), m_imageSample;
    int m_imageShade = 0, m_pendingImports = 0;
    bool m_importing = false, m_sampleRunning = false;
    quint64 m_importRevision = 0, m_sampleRevision = 0;
    QJsonObject m_beforeBackground;
    QThreadPool m_imageWorkers;
    std::shared_ptr<std::atomic_bool> m_importCancellation;
    QTimer m_sampleTimer;
    QColor m_floatingText, m_floatingSecondary, m_floatingCard, m_floatingBorder,
        m_floatingAccentText;
    QColor m_onAccent, m_accentHover, m_accentPressed, m_atmosphere;
};
} // namespace EchoMusic
