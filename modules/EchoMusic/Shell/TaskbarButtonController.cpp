#include "TaskbarButtonController.h"
#include "EchoIconSource.h"
#include "PlaybackLibraryActions.h"
#include "PlaybackService.h"
#include "SettingsKeys.h"
#include <QPainter>
#include <QQuickWindow>
#include <QSvgRenderer>
#include <cmath>
namespace EchoMusic
{
namespace
{
/// Icon glyphs. The controls module already ships these silhouettes, so the
/// taskbar buttons match the in-window transport instead of a second icon set.
constexpr auto GlyphPrevious = "player-skip-back";
constexpr auto GlyphNext = "player-skip-forward";
constexpr auto GlyphPlay = "play";
constexpr auto GlyphPause = "pause";
constexpr auto GlyphFavorite = "heart";
constexpr auto GlyphFavoriteOn = "heart-filled";
/// Fallback preview when a track has no artwork: the same placeholder cover the
/// library uses, rendered natively for the taskbar.
constexpr int PlaceholderCoverSize = 512;
constexpr int PlaceholderGlyphRatio = 34;
constexpr auto GlyphPlaceholder = "music";
/// A favourited track keeps the accent red of the original toolbar instead of
/// the neutral silhouette colour.
const QColor FavoriteColor = QColor(0xe8, 0x2b, 0x5b);
/// Upper bound on the mirrored favourite state: a failed or unacknowledged
/// write must not leave the button showing the opposite of the library.
constexpr qint64 FavoriteIntentMs = 4000;
/// Thumbnail toolbar command ids. The platform layer only checks the range, so
/// the mapping stays here where the actions are wired.
enum ThumbarButton
{
    ThumbarPrevious = 0xec00,
    ThumbarToggle,
    ThumbarNext,
    ThumbarFavorite
};
} // namespace
TaskbarButtonController::TaskbarButtonController(QSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_progressEnabled = m_settings.value(SettingsKeys::Window::TaskbarProgress, false).toBool();
    m_coverEnabled = m_settings.value(SettingsKeys::Window::TaskbarCoverPreview, false).toBool();
    m_clock.start();
    // Progress and thumbbar share the same shell object; one probe decides
    // whether the whole section is offered.
    m_available = m_progress.available();
    connect(&m_thumbar, &WindowsTaskbarThumbar::buttonClicked, this, [this](int id) {
        if (!m_playback)
            return;
        switch (id) {
        case ThumbarPrevious:
            m_playback->previous();
            break;
        case ThumbarToggle:
            m_playback->toggle();
            break;
        case ThumbarNext:
            m_playback->next();
            break;
        case ThumbarFavorite:
            if (auto *actions = m_playback->libraryActions()) {
                // The library write is asynchronous. Mirror the click so the
                // button flips with it, and let the resolved state (or the
                // timeout) correct the mirror.
                const bool current = m_favoriteIntent.value_or(actions->favorite());
                m_favoriteIntent = !current;
                m_favoriteIntentTrack = m_playback->trackId();
                m_favoriteIntentMs = m_clock.elapsed();
                actions->toggleFavorite();
                refreshThumbar();
            }
            break;
        default:
            break;
        }
    });
}
void TaskbarButtonController::setProgressEnabled(bool enabled)
{
    if (m_progressEnabled == enabled)
        return;
    m_progressEnabled = enabled;
    m_settings.setValue(SettingsKeys::Window::TaskbarProgress, enabled);
    m_progressCleared = false;
    refreshProgress();
    emit changed();
}
void TaskbarButtonController::setCoverPreviewEnabled(bool enabled)
{
    if (m_coverEnabled == enabled)
        return;
    m_coverEnabled = enabled;
    m_settings.setValue(SettingsKeys::Window::TaskbarCoverPreview, enabled);
    refreshCover();
    emit changed();
}
void TaskbarButtonController::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    m_icons.clear();
    m_thumbarBuilt = false;
    refreshThumbar();
}
void TaskbarButtonController::attach(QQuickWindow *window)
{
    // Every visibility change re-arms the three features. Hiding to the tray
    // hands the taskbar button back to Explorer, which returns a fresh one on
    // show, and Qt may also recreate the native window behind us; either way
    // the cached handle, bitmaps and toolbar registration are stale.
    if (window && m_window != window) {
        connect(window, &QWindow::visibleChanged, this, [this] {
            // Hiding to the tray takes the taskbar button with it, and Explorer
            // discards the thumbnail toolbar rather than hiding it. The window
            // subclass stays installed so the platform layer can re-register
            // the buttons when Explorer reports the new button; every other
            // cache belongs to the old handle and is dropped here.
            m_previewApplied = false;
            m_coverKey.clear();
            m_thumbarBuilt = false;
            m_progressCleared = true;
            m_gate.reset();
            refreshAll();
        });
        connect(window, &QWindow::screenChanged, this, [this] { refreshAll(); });
    }
    // A re-attach means a new native window: every cached handle and bitmap
    // belongs to the old one.
    m_window = window;
    m_icons.clear();
    m_gate.reset();
    m_progressCleared = true;
    m_previewApplied = false;
    m_coverKey.clear();
    m_thumbarBuilt = false;
    refreshAll();
}
void TaskbarButtonController::refreshAll()
{
    refreshProgress();
    refreshCover();
    refreshThumbar();
}
void TaskbarButtonController::attachPlayback(PlaybackService &playback)
{
    m_playback = &playback;
    connect(&playback, &PlaybackService::positionChanged, this,
            &TaskbarButtonController::refreshProgress);
    connect(&playback, &PlaybackService::changed, this, &TaskbarButtonController::refreshAll);
    // The library action hub is connected lazily: it is created after sign-in,
    // which is later than this attach.
    refreshAll();
}
quintptr TaskbarButtonController::handle() const
{
    // A hidden window has no taskbar button, and asking for its winId() would
    // recreate the native window behind the shell's back.
    if (!m_window || !m_window->isVisible())
        return 0;
    return quintptr(m_window->winId());
}
QImage TaskbarButtonController::icon(const QString &glyph, const QColor &tint, int size)
{
    const qreal ratio = m_window ? m_window->devicePixelRatio() : 1.0;
    const int pixels = size > 0 ? size : qMax(16, int(std::lround(16.0 * ratio)));
    // The toolbar has no theme of its own, so the silhouette follows the
    // resolved appearance unless the state carries its own colour.
    const QColor color =
        tint.isValid() ? tint : (m_dark ? QColor(Qt::white) : QColor(0x20, 0x20, 0x20));
    const QString key =
        glyph + QLatin1Char('@') + QString::number(pixels) + QLatin1Char('#') + color.name();
    const auto cached = m_icons.constFind(key);
    if (cached != m_icons.constEnd())
        return cached.value();
    // The bundled silhouettes are the single icon contract; resolve them through
    // the same path helper the QML image provider uses.
    const QString path = EchoIconSource::iconResourcePath(glyph);
    QSvgRenderer renderer((path));
    if (path.isEmpty() || !renderer.isValid())
        return {};
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    // Tint the silhouette: multiplying the SVG's own RGB would leave the
    // black glyphs black.
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    painter.end();
    m_icons.insert(key, image);
    return image;
}
QImage TaskbarButtonController::placeholderCover()
{
    // Mirrors EchoCover's placeholder (a hover-tinted surface with a
    // low-opacity music glyph) so a track without artwork still reads as a
    // cover instead of the application icon.
    QImage tile(PlaceholderCoverSize, PlaceholderCoverSize, QImage::Format_ARGB32_Premultiplied);
    tile.fill(m_dark ? QColor(0x2f, 0x2f, 0x2f) : QColor(0xea, 0xea, 0xea));
    const QColor glyphColor = m_dark ? QColor(Qt::white) : QColor(Qt::black);
    const int glyphSize = PlaceholderCoverSize * PlaceholderGlyphRatio / 100;
    const QImage glyph = icon(GlyphPlaceholder, glyphColor, glyphSize);
    if (glyph.isNull())
        return tile;
    QPainter painter(&tile);
    // EchoCover draws its placeholder glyph at placeholderOpacity.
    painter.setOpacity(0.3);
    painter.drawImage(
        QPoint((PlaceholderCoverSize - glyphSize) / 2, (PlaceholderCoverSize - glyphSize) / 2),
        glyph);
    return tile;
}
void TaskbarButtonController::refreshProgress()
{
    const auto hwnd = handle();
    if (!hwnd)
        return;
    if (!m_progressEnabled) {
        if (!m_progressCleared) {
            m_progress.clear(hwnd);
            m_gate.reset();
            m_progressCleared = true;
        }
        return;
    }
    if (!m_playback)
        return;
    m_progressCleared = false;
    const auto value = resolveTaskbarProgress(m_playback->hasTrack(), m_playback->playing(),
                                              m_playback->positionMs(), m_playback->durationMs());
    if (!m_gate.accept(value, m_clock.elapsed()))
        return;
    m_progress.apply(hwnd, value);
}
void TaskbarButtonController::refreshCover()
{
    const auto hwnd = handle();
    if (!hwnd)
        return;
    if (!m_coverEnabled) {
        if (m_previewApplied) {
            m_thumbnail.clear();
            m_previewApplied = false;
            m_coverKey.clear();
        }
        return;
    }
    if (!m_playback)
        return;
    const QByteArray png = m_playback->coverPng();
    const QString key = m_playback->trackId() + QLatin1Char('|') + m_playback->cover() +
                        QLatin1Char('|') + QString::number(png.size());
    if (m_previewApplied && key == m_coverKey)
        return;
    // A cover that cannot be decoded falls back to the placeholder cover, so
    // the preview never shows an empty tile.
    QImage cover;
    if (!png.isEmpty())
        cover.loadFromData(png);
    if (cover.isNull())
        cover = placeholderCover();
    if (cover.isNull())
        return;
    if (!m_thumbnail.apply(hwnd, cover))
        return;
    m_previewApplied = true;
    m_coverKey = key;
}
void TaskbarButtonController::refreshThumbar()
{
    const auto hwnd = handle();
    if (!hwnd || !m_playback)
        return;
    const bool hasTrack = m_playback->hasTrack();
    const bool playing = hasTrack && m_playback->playing();
    auto *actions = m_playback->libraryActions();
    if (actions && actions != m_actions) {
        // The action hub appears only once the library is attached, which is
        // later than the playback service; connect when it shows up.
        m_actions = actions;
        connect(actions, &PlaybackLibraryActions::changed, this,
                &TaskbarButtonController::refreshThumbar);
    }
    bool favorite = hasTrack && actions && actions->favorite();
    if (m_favoriteIntent.has_value()) {
        const bool settled = favorite == *m_favoriteIntent;
        const bool expired = m_clock.elapsed() - m_favoriteIntentMs > FavoriteIntentMs;
        if (settled || expired || m_favoriteIntentTrack != m_playback->trackId())
            m_favoriteIntent.reset();
        else
            favorite = *m_favoriteIntent;
    }
    if (m_thumbarBuilt && hasTrack == m_thumbarHasTrack && playing == m_thumbarPlaying &&
        favorite == m_thumbarFavorite)
        return;
    m_thumbarHasTrack = hasTrack;
    m_thumbarPlaying = playing;
    m_thumbarFavorite = favorite;
    m_thumbarBuilt = true;
    const QList<TaskbarThumbarButton> buttons{
        {icon(GlyphPrevious), tr("上一曲"), hasTrack, ThumbarPrevious},
        {icon(playing ? GlyphPause : GlyphPlay), playing ? tr("暂停") : tr("播放"), hasTrack,
         ThumbarToggle},
        {icon(GlyphNext), tr("下一曲"), hasTrack, ThumbarNext},
        {icon(favorite ? GlyphFavoriteOn : GlyphFavorite, favorite ? FavoriteColor : QColor()),
         favorite ? tr("取消收藏") : tr("收藏"), hasTrack, ThumbarFavorite}};
    m_thumbar.apply(hwnd, buttons);
}
} // namespace EchoMusic
