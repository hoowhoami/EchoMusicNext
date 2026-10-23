#include "SettingsKeys.h"
#include "ThemeStore.h"
#include "ThemeImageStore.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include "UiTypography.h"
#include <QFontDatabase>
#include <QEvent>
#include "AccentPalette.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleHints>
#include <QPointer>
#include <cmath>

namespace EchoMusic
{
// Engine-owned typed binding; QPointer prevents a stale borrowed pointer if a
// caller violates the normal store-outlives-engine teardown order.
class ThemeEngineBinding final : public QObject
{
    Q_OBJECT
public:
    ThemeEngineBinding(QQmlEngine &engine, ThemeStore &store) : QObject(&engine), store(&store) {}
    QPointer<ThemeStore> store;
};
namespace
{
QColor mix(const QColor &a, const QColor &b, qreal amount)
{
    return QColor(qRound(a.red() + (b.red() - a.red()) * amount),
                  qRound(a.green() + (b.green() - a.green()) * amount),
                  qRound(a.blue() + (b.blue() - a.blue()) * amount));
}
qreal luminance(const QColor &color)
{
    const auto linear = [](qreal value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return linear(color.red() / 255.0) * 0.2126 + linear(color.green() / 255.0) * 0.7152 +
           linear(color.blue() / 255.0) * 0.0722;
}
QColor readableSurface(QColor surface, const QColor &text, bool dark)
{
    for (int i = 0; i < 24 && ThemeStore::contrast(text, surface) < 7; ++i)
        surface = mix(surface, dark ? QColor("#111114") : QColor("#ffffff"), 0.12);
    return surface;
}
bool opaque(const QColor &color)
{
    return color.isValid() && color.alpha() == 255;
}
QColor readableText(const QColor &preferred, const QList<QColor> &surfaces,
                    const QColor &target = {})
{
    const auto score = [&surfaces](const QColor &color) {
        qreal result = 100;
        for (const auto &surface : surfaces)
            result = qMin(result, ThemeStore::contrast(color, surface));
        return result;
    };
    if (score(preferred) >= 4.5)
        return preferred;
    const QColor foreground = target.isValid()                       ? target
                              : score(Qt::white) >= score(Qt::black) ? Qt::white
                                                                     : Qt::black;
    if (score(foreground) < 4.5)
        return preferred;
    qreal low = 0, high = 1;
    for (int i = 0; i < 16; ++i) {
        const auto amount = (low + high) / 2;
        if (score(mix(preferred, foreground, amount)) >= 4.5)
            high = amount;
        else
            low = amount;
    }
    return mix(preferred, foreground, high);
}
} // namespace
QColor ThemeStore::lyricAccent(const QColor &cover) const
{
    return normalizeAccent(cover.isValid() ? cover : QColor("#0071e3"), true);
}
QColor ThemeStore::lyricBackground(const QColor &cover) const
{
    if (!cover.isValid())
        return QColor("#1a1d22");
    const auto normalized = normalizeAccent(cover, true);
    return QColor::fromHslF(normalized.hslHueF(), qMin(.6, normalized.hslSaturationF() * .8),
                            qBound(.12, normalized.lightnessF() * .45, .22));
}
QVariantMap ThemeStore::lyricPalette(const QColor &background) const
{
    const auto n = neutral(true);
    const QList<QColor> surfaces{background, mix(background, n.text, .1)};
    return {{"text", n.text},
            {"secondary", readableText(n.secondary, surfaces, n.text)},
            {"border", n.border},
            {"accentText", readableText(m_accent, surfaces)}};
}
qreal ThemeStore::contrast(const QColor &a, const QColor &b)
{
    const auto x = luminance(a), y = luminance(b);
    return (qMax(x, y) + 0.05) / (qMin(x, y) + 0.05);
}
ThemePalette ThemeStore::neutral(bool dark)
{
    // Theme palette derived from EchoMusic src/shared/themePalette.ts, GPL-3.0-only.
    if (dark)
        return {QColor("#171718"), QColor("#171718"), QColor("#29292b"),
                QColor("#29292b"), QColor("#343436"), QColor("#29292b"),
                QColor("#ffffff"), QColor("#bcbcbc"), QColor("#444446")};
    return {QColor("#f6f6f6"), QColor("#f6f6f6"), QColor("#ffffff"),
            QColor("#ffffff"), QColor("#f7f7f7"), QColor("#ffffff"),
            QColor("#000000"), QColor("#666666"), QColor("#dedede")};
}
ThemePalette ThemeStore::fromSeed(const QColor &seed, bool dark)
{
    // Derive readable skin surfaces independently of the Qt palette.
    // Simple HSV darkening does not preserve the required contrast.
    const auto text = neutral(dark).text;
    const auto main = readableSurface(dark ? mix(seed, QColor("#111114"), 0.62) : seed, text, dark);
    const auto shell = mix(main, dark ? QColor("#111114") : QColor("#ffffff"), 0.12);
    const auto card = readableSurface(mix(main, QColor("#ffffff"), dark ? 0.06 : 0.08), text, dark);
    auto secondary = mix(text, main, 0.34);
    for (int i = 0; i < 24 && (contrast(secondary, shell) < 4.5 ||
                               contrast(secondary, main) < 4.5 || contrast(secondary, card) < 4.5);
         ++i) {
        const auto next = mix(secondary, text, 0.3);
        if (next == secondary) {
            secondary = text;
            break;
        }
        secondary = next;
    }
    return {shell, shell, main, main, card, card, text, secondary, mix(main, text, 0.18)};
}
ThemeStore::ThemeStore(QSettings &settings, QObject *parent, const QString &imageDirectory)
    : QObject(parent), m_settings(settings),
      m_imageDirectory(
          !imageDirectory.isEmpty() ? imageDirectory
          : settings.format() == QSettings::IniFormat
              ? QFileInfo(settings.fileName()).absolutePath() + "/theme-images"
              : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                    "/theme-images")
{
    m_imageWorkers.setMaxThreadCount(1);
    m_sampleTimer.setSingleShot(true);
    m_sampleTimer.setInterval(100);
    connect(&m_sampleTimer, &QTimer::timeout, this, &ThemeStore::scheduleImageSample);
    const auto object =
        QJsonDocument::fromJson(
            settings.value(EchoMusic::SettingsKeys::Appearance::Preferences).toByteArray())
            .object();
    const auto version = object.value("version").toInt();
    if (version >= 1 && version <= 3) {
        const auto mode = object.value("mode").toInt();
        if (mode >= System && mode <= ThemeDefault)
            m_mode = Mode(mode);
        if (object.value("theme").toString() == "solid")
            m_key = "solid";
        const QColor seed(object.value("seed").toString());
        if (opaque(seed))
            m_seed = seed;
        const QColor accent(object.value("accent").toString());
        if (opaque(accent))
            m_selectedAccent = accent;
        m_customAccent = object.value("customAccent").toBool();
        // Upgrade only this application's own preferences.
        m_panelOpacity = qBound(0, object.value("panelOpacity").toInt(50), 100);
        m_coverAccent = object.value("accentSource").toString() == "cover";
        m_coverAtmosphere = object.value("coverAtmosphere").toBool();
        m_windowFrosted = object.value("windowFrosted").toBool();
        m_floatingFrosted = object.value("floatingFrosted").toBool();
        m_atmosphereHeight = qBound(20, object.value("atmosphereHeight").toInt(70), 100);
        m_atmosphereStrength = qBound(20, object.value("atmosphereStrength").toInt(100), 200);
        m_transparency = qBound(0, object.value("transparency").toInt(), 100);
        if (version == 3)
            restoreBackgroundDraft(object.value("draft").toObject());
    }
    const auto savedFont =
        settings.value(EchoMusic::SettingsKeys::Typography::GlobalFont, "system-ui").toString();
    if (savedFont == "system-ui" || QFontDatabase::hasFamily(savedFont))
        m_globalFont = savedFont;
    configureUiTypography(m_globalFont);
    const auto loadLyricFont = [&settings](const QString &key) {
        const auto family = settings.value(key, "follow").toString();
        return family == "follow" || family == "system-ui" || QFontDatabase::hasFamily(family)
                   ? family
                   : QStringLiteral("follow");
    };
    m_lyricFont = loadLyricFont(EchoMusic::SettingsKeys::Typography::LyricFont);
    m_desktopLyricFont = loadLyricFont(EchoMusic::SettingsKeys::Typography::DesktopLyricFont);
    m_miniLyricFont = loadLyricFont(EchoMusic::SettingsKeys::Typography::MiniLyricFont);
    m_taskbarLyricFont = loadLyricFont(EchoMusic::SettingsKeys::Typography::TaskbarLyricFont);
    connect(this, &ThemeStore::fontChanged, this, &ThemeStore::lyricFontsChanged);
    qApp->installEventFilter(this);
    auto *hints = QGuiApplication::styleHints();
    m_systemDark = hints->colorScheme() == Qt::ColorScheme::Dark;
    connect(hints, &QStyleHints::colorSchemeChanged, this, &ThemeStore::adoptSystemScheme);
    m_extensions = new AppearanceCatalog(m_settings, this);
    connect(m_extensions, &AppearanceCatalog::themeChanged, this, &ThemeStore::resolve);
    resolve();
    if (!m_imageId.isEmpty())
        m_sampleTimer.start();
}
ThemeStore::~ThemeStore()
{
    // Jobs capture values only. No decoder/file worker can dereference the
    // application store during teardown; managed writes finish before exit.
    cancelImageWorkForShutdown();
    m_imageWorkers.waitForDone();
}
void ThemeStore::cancelImageWorkForShutdown()
{
    ++m_importRevision;
    ++m_sampleRevision;
    m_sampleTimer.stop();
    if (m_importCancellation)
        m_importCancellation->store(true);
}
void ThemeStore::attachToEngine(QQmlEngine &engine)
{
    if (thread() != engine.thread() || QThread::currentThread() != engine.thread())
        qFatal("ThemeStore and QQmlEngine must be bound on their owning thread");
    if (auto *binding = engine.findChild<ThemeEngineBinding *>(Qt::FindDirectChildrenOnly)) {
        if (binding->store != this)
            qFatal("An engine's ThemeStore binding cannot be replaced");
        return;
    }
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    new ThemeEngineBinding(engine, *this);
}
ThemeStore *ThemeStore::create(QQmlEngine *engine, QJSEngine *)
{
    auto *binding = engine->findChild<ThemeEngineBinding *>(Qt::FindDirectChildrenOnly);
    if (!binding || !binding->store) {
        engine->throwError(QStringLiteral("ThemeStore must be attached before loading QML"));
        return nullptr;
    }
    return binding->store;
}
QFont ThemeStore::applicationFont() const
{
    return QGuiApplication::font();
}
QStringList ThemeStore::fontFamilies() const
{
    auto families = QFontDatabase::families();
    std::sort(families.begin(), families.end(), [](const QString &a, const QString &b) {
        if (a.startsWith(b))
            return false;
        if (b.startsWith(a))
            return true;
        return QString::localeAwareCompare(a, b) < 0;
    });
    return families;
}
QFont ThemeStore::styledFont(const QFont &base, qreal pixelSize, int weight, qreal letterSpacing,
                             bool italic, bool underline) const
{
    return styledUiFont(base, pixelSize, weight, letterSpacing, italic, underline);
}
void ThemeStore::setGlobalFont(const QString &family)
{
    if (family == m_globalFont || (family != "system-ui" && !QFontDatabase::hasFamily(family)))
        return;
    m_globalFont = family;
    m_settings.setValue(EchoMusic::SettingsKeys::Typography::GlobalFont, family);
    configureUiTypography(family);
    // QGuiApplication does not deliver ApplicationFontChange to itself on all
    // platforms. Own writes notify explicitly, including an equal resolved font.
    emit fontChanged();
}
void ThemeStore::setLyricFontPreference(QString &preference, const QString &key,
                                        const QString &family)
{
    if (family == preference ||
        (family != "follow" && family != "system-ui" && !QFontDatabase::hasFamily(family)))
        return;
    preference = family;
    m_settings.setValue(key, family);
    emit lyricFontsChanged();
}
void ThemeStore::setLyricFont(const QString &family)
{
    setLyricFontPreference(m_lyricFont, EchoMusic::SettingsKeys::Typography::LyricFont, family);
}
void ThemeStore::setDesktopLyricFont(const QString &family)
{
    setLyricFontPreference(m_desktopLyricFont,
                           EchoMusic::SettingsKeys::Typography::DesktopLyricFont, family);
}
void ThemeStore::setMiniLyricFont(const QString &family)
{
    setLyricFontPreference(m_miniLyricFont, EchoMusic::SettingsKeys::Typography::MiniLyricFont,
                           family);
}
void ThemeStore::setTaskbarLyricFont(const QString &family)
{
    setLyricFontPreference(m_taskbarLyricFont,
                           EchoMusic::SettingsKeys::Typography::TaskbarLyricFont, family);
}
QFont ThemeStore::resolvedLyricFont(const QString &family) const
{
    return family == "follow" ? applicationFont() : uiFont(family);
}
QFont ThemeStore::lyricBaseFont() const
{
    return resolvedLyricFont(m_lyricFont);
}
QFont ThemeStore::desktopLyricBaseFont() const
{
    return resolvedLyricFont(m_desktopLyricFont);
}
QFont ThemeStore::miniLyricBaseFont() const
{
    return resolvedLyricFont(m_miniLyricFont);
}
QFont ThemeStore::taskbarLyricBaseFont() const
{
    return resolvedLyricFont(m_taskbarLyricFont);
}
bool ThemeStore::eventFilter(QObject *object, QEvent *event)
{
    // Qt 6 deprecates QGuiApplication::fontChanged. Notify Theme.bodyFont via
    // the actual ApplicationFontChange event so Text and Controls update together.
    if (object == qApp && event->type() == QEvent::ApplicationFontChange)
        emit fontChanged();
    return QObject::eventFilter(object, event);
}
QColor ThemeStore::controlHover() const
{
    auto c = tone();
    c.setAlphaF(0.10);
    return c;
}
QColor ThemeStore::mutedSurface() const
{
    auto c = tone();
    c.setAlphaF(0.05);
    return c;
}
QColor ThemeStore::selectedSurface() const
{
    auto c = m_accent;
    c.setAlphaF(0.12);
    return c;
}
QColor ThemeStore::panelColor() const
{
    auto color = m_palette.main;
    color.setAlphaF(m_panelOpacity / 100.0);
    return color;
}
QColor ThemeStore::panelBackdrop() const
{
    const auto artwork = (!m_extensions || !m_extensions->theme()) && m_key == "custom" &&
                                 !m_imageId.isEmpty() && m_imageSample.isValid()
                             ? mix(m_imageSample, m_palette.shell, m_imageShade / 100.0)
                             : m_palette.shell;
    return mix(artwork, m_palette.main, m_panelOpacity / 100.0);
}
void ThemeStore::setPanelOpacity(int opacity)
{
    opacity = qBound(0, opacity, 100);
    if (m_panelOpacity == opacity)
        return;
    m_panelOpacity = opacity;
    save();
    resolve();
}
QColor ThemeStore::extensionSurface(const QString &name, const QColor &base) const
{
    auto color = base;
    const auto values = m_extensionAppearance.value("surface").toMap();
    if (values.contains(name))
        color.setAlphaF(base.alphaF() * values.value(name).toDouble());
    return color;
}
void ThemeStore::resolve()
{
    const bool extensionSelected = m_extensions && m_extensions->theme();
    m_palette = !extensionSelected && m_key == "solid" ? fromSeed(m_seed, dark()) : neutral(dark());
    if (!extensionSelected && m_key == "custom") {
        if (m_imageSample.isValid()) {
            const auto imagePalette = fromSeed(m_imageSample, dark());
            m_palette.shell = imagePalette.shell;
            m_palette.main = imagePalette.main;
            m_palette.player = imagePalette.player;
        }
        m_palette.text = m_imageText;
        m_palette.secondary = mix(m_imageText, neutral(dark()).secondary, 0.25);
    }
    m_extensionAppearance =
        m_extensions ? m_extensions->resolveTheme(
                           {{"dark", dark()}, {"coverColor", m_coverColor.name(QColor::HexArgb)}})
                     : QVariantMap();
    const auto palette = m_extensionAppearance.value("palette").toMap();
    auto applyColor = [](QColor &target, const QVariantMap &values, const QString &key) {
        const QColor value(values.value(key).toString());
        if (value.isValid())
            target = value;
    };
    applyColor(m_palette.shell, palette, "shell");
    applyColor(m_palette.sidebar, palette, "sidebar");
    applyColor(m_palette.main, palette, "main");
    applyColor(m_palette.player, palette, "player");
    applyColor(m_palette.card, palette, "card");
    applyColor(m_palette.elevated, palette, "elevated");
    applyColor(m_palette.text, palette, "text");
    applyColor(m_palette.secondary, palette, "secondaryText");
    applyColor(m_palette.border, palette, "border");
    const bool artworkReady =
        !extensionSelected && m_key == "custom" && !m_imageId.isEmpty() && m_imageSample.isValid();
    const auto artwork =
        artworkReady ? mix(m_imageSample, m_palette.shell, m_imageShade / 100.0) : m_palette.shell;
    const bool accentDark =
        artworkReady ? contrast(artwork, Qt::black) < contrast(artwork, Qt::white)
                     : (!extensionSelected && m_key == "custom" && !m_imageId.isEmpty()
                            ? contrast(m_imageText, Qt::black) >= contrast(m_imageText, Qt::white)
                            : dark());
    // Keep the chosen hex unchanged in preferences/swatch selection. Normalize
    // only custom accent rendering, never the raw theme seed.
    m_accent =
        m_coverAccent ? normalizeAccent(m_coverColor, accentDark)
        : m_customAccent
            ? (m_key == "custom" ? m_selectedAccent : normalizeAccent(m_selectedAccent, accentDark))
        : !extensionSelected && m_key == "custom" ? m_imageAccent
        : !extensionSelected && m_key == "solid"  ? m_seed
                                                  : QColor("#00cc65");
    if (!m_customAccent && !m_coverAccent)
        applyColor(m_accent, palette, "accent");
    applyColor(m_palette.elevated, m_extensionAppearance.value("floating").toMap(), "background");
    // Floating colors form a separate readable pair;
    // artwork text colors must not leak into menus or search popovers.
    const auto neutralColors = neutral(dark());
    m_floatingText =
        contrast(neutralColors.text, m_palette.elevated) >= 4.5 ? neutralColors.text
        : contrast(Qt::white, m_palette.elevated) >= contrast(Qt::black, m_palette.elevated)
            ? Qt::white
            : Qt::black;
    const QColor floatingTone =
        contrast(m_floatingText, Qt::black) >= contrast(m_floatingText, Qt::white) ? Qt::white
                                                                                   : Qt::black;
    m_floatingCard = mix(m_palette.elevated, floatingTone, 0.06);
    if (contrast(m_floatingText, m_floatingCard) < 4.5)
        m_floatingCard = m_palette.elevated;
    m_floatingSecondary = mix(m_floatingText, m_palette.elevated, 0.25);
    if (contrast(m_floatingSecondary, m_palette.elevated) < 4.5 ||
        contrast(m_floatingSecondary, m_floatingCard) < 4.5)
        m_floatingSecondary = m_floatingText;
    m_floatingBorder = mix(m_palette.elevated, m_floatingText, 0.2);
    m_floatingAccentText = readableText(floatingAccent(), {m_palette.elevated, m_floatingCard});
    const auto floating = m_extensionAppearance.value("floating").toMap();
    applyColor(m_floatingText, floating, "text");
    applyColor(m_floatingSecondary, floating, "secondaryText");
    applyColor(m_floatingCard, floating, "card");
    applyColor(m_floatingBorder, floating, "border");
    applyColor(m_floatingAccentText, floating, "accentText");
    const QColor tone = dark() ? Qt::white : Qt::black;
    QList<QColor> bases{panelBackdrop(), artwork};
    if (m_coverAtmosphere) {
        const auto atmosphere = coverAtmosphereColor();
        const auto strength = qMin(1.0, (dark() ? .30 : .32) * m_atmosphereStrength / 100.0);
        bases.append(mix(bases[0], atmosphere, strength));
        bases.append(mix(bases[1], atmosphere, strength));
    }
    QList<QColor> contentSurfaces;
    for (const auto &base : bases) {
        contentSurfaces.append(base);
        contentSurfaces.append(mix(base, tone, .1));
    }
    const auto accentColors = accentPalette(m_accent, accentDark, contentSurfaces);
    m_accentText = accentColors.primaryText;
    m_onAccent = accentColors.onPrimary;
    m_accentHover = accentColors.hover;
    m_accentPressed = accentColors.pressed;
    m_atmosphere = accentColors.atmosphere;
    m_palette.secondary = readableText(m_palette.secondary, contentSurfaces, m_palette.text);
    emit changed();
}
QVariantMap ThemeStore::previewPalette(bool dark, const QColor &seed, bool solid) const
{
    const auto p = solid && opaque(seed) ? fromSeed(seed, dark) : neutral(dark);
    return {{"shell", p.shell},         {"main", p.main},
            {"player", p.player},       {"text", p.text},
            {"secondary", p.secondary}, {"accent", solid ? seed : QColor("#00cc65")}};
}
void ThemeStore::save()
{
    const QJsonObject object{
        {"version", 3},
        {"mode", int(m_mode)},
        {"theme", m_key},
        {"seed", m_seed.name()},
        {"accent", m_selectedAccent.name()},
        {"customAccent", m_customAccent},
        {"panelOpacity", m_panelOpacity},
        {"accentSource", accentSource()},
        {"coverAtmosphere", m_coverAtmosphere},
        {"atmosphereHeight", m_atmosphereHeight},
        {"atmosphereStrength", m_atmosphereStrength},
        {"transparency", m_transparency},
        {"windowFrosted", m_windowFrosted},
        {"floatingFrosted", m_floatingFrosted},
        {"draft", backgroundPreview() ? m_beforeBackground : backgroundDraft()}};
    m_settings.setValue(EchoMusic::SettingsKeys::Appearance::Preferences,
                        QJsonDocument(object).toJson(QJsonDocument::Compact));
}
void ThemeStore::adoptSystemScheme(Qt::ColorScheme scheme)
{
    const bool dark = scheme == Qt::ColorScheme::Dark;
    if (m_systemDark == dark)
        return;
    m_systemDark = dark;
    if (m_mode == System || m_mode == ThemeDefault)
        resolve();
}
void ThemeStore::setMode(Mode mode)
{
    if (mode < System || mode > ThemeDefault || mode == m_mode)
        return;
    m_mode = mode;
    save();
    resolve();
}
void ThemeStore::selectEcho()
{
    if (m_extensions)
        m_extensions->clearTheme();
    endBackgroundPreview();
    if (m_key == "echo")
        return;
    m_key = "echo";
    save();
    resolve();
}
void ThemeStore::selectSolid(const QColor &seed)
{
    if (!opaque(seed))
        return;
    if (m_extensions)
        m_extensions->clearTheme();
    endBackgroundPreview();
    if (!opaque(seed) || (m_key == "solid" && m_seed == seed))
        return;
    m_key = "solid";
    m_seed = seed;
    save();
    resolve();
}
void ThemeStore::setAccent(const QColor &color)
{
    if (!opaque(color))
        return;
    m_customAccent = true;
    m_coverAccent = false;
    m_selectedAccent = color;
    save();
    resolve();
}
void ThemeStore::useThemeAccent()
{
    if (!m_customAccent && !m_coverAccent)
        return;
    m_customAccent = false;
    m_coverAccent = false;
    save();
    resolve();
}
void ThemeStore::resetAppearanceAdjustments()
{
    // General appearance reset preserves the selected skin and managed image.
    m_customAccent = false;
    m_coverAccent = false;
    m_selectedAccent = QColor("#0071e3");
    m_panelOpacity = 50;
    m_coverAtmosphere = false;
    m_atmosphereHeight = 70;
    m_atmosphereStrength = 100;
    m_transparency = 0;
    m_floatingFrosted = false;
    m_windowFrosted = false;
    save();
    resolve();
}
void ThemeStore::useCoverAccent()
{
    if (m_coverAccent)
        return;
    m_coverAccent = true;
    m_customAccent = false;
    save();
    resolve();
}
void ThemeStore::setCoverColor(const QColor &color)
{
    if (!opaque(color) || color == m_coverColor)
        return;
    m_coverColor = color;
    resolve();
}
QColor ThemeStore::coverAtmosphereColor() const
{
    return accentPalette(m_coverColor, dark(), {}).atmosphere;
}
void ThemeStore::setCoverAtmosphere(bool enabled)
{
    if (m_coverAtmosphere == enabled)
        return;
    m_coverAtmosphere = enabled;
    save();
    resolve();
}
void ThemeStore::setAtmosphereHeight(int value)
{
    value = qBound(20, value, 100);
    if (value == m_atmosphereHeight)
        return;
    m_atmosphereHeight = value;
    save();
    resolve();
}
void ThemeStore::setAtmosphereStrength(int value)
{
    value = qBound(20, value, 200);
    if (value == m_atmosphereStrength)
        return;
    m_atmosphereStrength = value;
    save();
    resolve();
}
void ThemeStore::setTransparency(int value)
{
    value = qBound(0, value, 100);
    if (value == m_transparency)
        return;
    m_transparency = value;
    save();
    resolve();
}
void ThemeStore::setWindowFrosted(bool enabled)
{
    if (m_windowFrosted == enabled)
        return;
    m_windowFrosted = enabled;
    save();
    emit changed();
}
void ThemeStore::setFloatingFrosted(bool enabled)
{
    if (m_floatingFrosted == enabled)
        return;
    m_floatingFrosted = enabled;
    save();
    resolve();
}
QUrl ThemeStore::backgroundImage() const
{
    return ThemeImageStore::validId(m_imageId)
               ? QUrl::fromLocalFile(QDir(m_imageDirectory).filePath(m_imageId))
               : QUrl();
}
QJsonObject ThemeStore::backgroundDraft() const
{
    return {{"theme", m_key},
            {"seed", m_seed.name()},
            {"image", m_imageId},
            {"width", m_imageSize.width()},
            {"height", m_imageSize.height()},
            {"cropX", m_crop.x()},
            {"cropY", m_crop.y()},
            {"cropWidth", m_crop.width()},
            {"cropHeight", m_crop.height()},
            {"text", m_imageText.name()},
            {"accent", m_imageAccent.name()},
            {"shade", m_imageShade}};
}
void ThemeStore::restoreBackgroundDraft(const QJsonObject &draft)
{
    const auto key = draft.value("theme").toString();
    if (key == "echo" || key == "solid" || key == "custom")
        m_key = key;
    const QColor seed(draft.value("seed").toString());
    if (opaque(seed))
        m_seed = seed;
    const auto id = draft.value("image").toString();
    m_imageId = ThemeImageStore::validId(id) ? id : QString();
    m_imageSize = QSize(qBound(0, draft.value("width").toInt(), 2560),
                        qBound(0, draft.value("height").toInt(), 2560));
    m_crop = QRectF(draft.value("cropX").toDouble(), draft.value("cropY").toDouble(),
                    draft.value("cropWidth").toDouble(), draft.value("cropHeight").toDouble());
    if (!QRectF(QPointF(), QSizeF(m_imageSize)).contains(m_crop) || m_crop.isEmpty())
        m_crop = {};
    const QColor text(draft.value("text").toString()), accent(draft.value("accent").toString());
    m_imageText = opaque(text) ? text : QColor(Qt::white);
    m_imageAccent = opaque(accent) ? accent : QColor("#00cc65");
    m_imageShade = qBound(0, draft.value("shade").toInt(), 85);
    m_imageSample = {};
}
void ThemeStore::beginBackground()
{
    if (!backgroundPreview())
        m_beforeBackground = backgroundDraft();
    m_key = "custom";
}
void ThemeStore::previewBackground()
{
    if (m_imageId.isEmpty() || m_key == "custom")
        return;
    beginBackground();
    resolve();
}
void ThemeStore::importBackground(const QUrl &file)
{
    if (!file.isLocalFile() || m_pendingImports >= 2) {
        m_imageError = tr("请等待当前图片处理完成后再选择本地图片");
        emit changed();
        return;
    }
    const auto revision = ++m_importRevision;
    if (m_importCancellation)
        m_importCancellation->store(true);
    m_importCancellation = std::make_shared<std::atomic_bool>(false);
    m_importing = true;
    m_imageError.clear();
    ++m_pendingImports;
    emit changed();
    auto *watcher = new QFutureWatcher<ThemeImageResult>(this);
    connect(watcher, &QFutureWatcher<ThemeImageResult>::finished, this, [this, watcher, revision] {
        const auto result = watcher->result();
        watcher->deleteLater();
        --m_pendingImports;
        if (revision != m_importRevision) {
            cleanImages();
            return;
        }
        m_importing = false;
        m_imageError = result.error;
        if (result.error.isEmpty()) {
            beginBackground();
            m_imageId = result.id;
            m_imageSize = result.size;
            m_crop = {};
            m_imageSample = result.sample;
            ++m_sampleRevision;
        }
        cleanImages();
        resolve();
    });
    watcher->setFuture(QtConcurrent::run(&m_imageWorkers, &ThemeImageStore::importImage,
                                         file.toLocalFile(), m_imageDirectory,
                                         m_importCancellation));
}
void ThemeStore::cancelBackground(bool stayOnCustom)
{
    if (m_importCancellation)
        m_importCancellation->store(true);
    ++m_importRevision;
    ++m_sampleRevision;
    m_importing = false;
    m_imageError.clear();
    if (backgroundPreview()) {
        const auto original = m_beforeBackground;
        restoreBackgroundDraft(original);
        if (stayOnCustom && !m_imageId.isEmpty())
            m_key = "custom";
        else
            m_beforeBackground = {};
        if (!m_imageId.isEmpty())
            m_sampleTimer.start();
    }
    cleanImages();
    resolve();
}
void ThemeStore::endBackgroundPreview()
{
    // Also cancel imports that have not reached beginBackground yet. Late
    // decoder completions cannot resurrect a page's cancelled preview.
    cancelBackground(false);
}
bool ThemeStore::applyBackground()
{
    if (m_importing || m_imageId.isEmpty()) {
        m_imageError = tr("请先选择图片");
        emit changed();
        return false;
    }
    m_key = "custom";
    m_beforeBackground = {};
    save();
    cleanImages();
    resolve();
    return true;
}
void ThemeStore::setBackgroundCrop(const QRectF &crop)
{
    if (m_imageId.isEmpty() || crop.isEmpty() ||
        !QRectF(QPointF(), QSizeF(m_imageSize)).contains(crop) || crop == m_crop)
        return;
    beginBackground();
    m_crop = crop;
    ++m_sampleRevision;
    m_sampleTimer.start();
    resolve();
}
void ThemeStore::setBackgroundText(const QColor &color)
{
    if (!opaque(color) || color == m_imageText || m_imageId.isEmpty())
        return;
    beginBackground();
    m_imageText = color;
    resolve();
}
void ThemeStore::setBackgroundAccent(const QColor &color)
{
    if (!opaque(color) || color == m_imageAccent || m_imageId.isEmpty())
        return;
    beginBackground();
    m_imageAccent = color;
    resolve();
}
void ThemeStore::setBackgroundShade(int value)
{
    value = qBound(0, value, 85);
    if (value == m_imageShade || m_imageId.isEmpty())
        return;
    beginBackground();
    m_imageShade = value;
    resolve();
}
void ThemeStore::resetBackgroundAdjustments()
{
    if (m_imageId.isEmpty())
        return;
    beginBackground();
    m_crop = {};
    m_imageText = Qt::white;
    m_imageAccent = QColor("#00cc65");
    m_imageShade = 0;
    ++m_sampleRevision;
    m_sampleTimer.start();
    resolve();
}
void ThemeStore::scheduleImageSample()
{
    if (m_imageId.isEmpty())
        return;
    if (m_sampleRunning) {
        m_sampleTimer.start();
        return;
    }
    m_sampleRunning = true;
    const auto revision = ++m_sampleRevision;
    auto *watcher = new QFutureWatcher<QColor>(this);
    connect(watcher, &QFutureWatcher<QColor>::finished, this, [this, watcher, revision] {
        const auto color = watcher->result();
        watcher->deleteLater();
        m_sampleRunning = false;
        if (revision != m_sampleRevision)
            return;
        if (color.isValid())
            m_imageSample = color;
        else
            reportBackgroundUnavailable(backgroundImage());
        resolve();
    });
    watcher->setFuture(QtConcurrent::run(&m_imageWorkers, &ThemeImageStore::sample,
                                         m_imageDirectory, m_imageId, m_crop));
}
void ThemeStore::reportBackgroundUnavailable(const QUrl &source)
{
    if (source.isEmpty() || source != backgroundImage())
        return;
    m_imageSample = {};
    const auto error = tr("背景图片不可用，请重新选择");
    if (m_imageError == error)
        return;
    m_imageError = error;
    emit backgroundUnavailable(tr("背景图片不可用，已显示主题默认底色"));
    resolve();
}
void ThemeStore::pruneImageAssets()
{
    cleanImages();
}
void ThemeStore::cleanImages()
{
    if (m_pendingImports)
        return;
    QStringList retained{m_imageId, m_beforeBackground.value("image").toString()};
    // Same worker serializes normalization and cleanup. A late import may be
    // discarded, but cleanup never races an atomic write or removes a draft.
    m_imageWorkers.start(
        [directory = m_imageDirectory, retained] { ThemeImageStore::clean(directory, retained); });
}
} // namespace EchoMusic

#include "ThemeStore.moc"
