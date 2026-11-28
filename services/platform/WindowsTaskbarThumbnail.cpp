#include "WindowsTaskbarThumbnail.h"
#include "WindowsTaskbarInterop.h"
#include <QLoggingCategory>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#endif
namespace EchoMusic
{
#ifdef Q_OS_WIN
namespace
{
Q_LOGGING_CATEGORY(taskbarLog, "echo.taskbar.thumbnail")
/// Subclass identity for the DWM bitmap requests. Distinct from the thumbbar
/// and the taskbar lyric subclasses, which share the same window.
constexpr UINT_PTR ThumbnailSubclassId = 0x45434854;
constexpr UINT WmSendIconicThumbnail = 0x0323;
constexpr UINT WmSendIconicLivePreview = 0x0326;
/// Answers the two DWM bitmap requests on the window thread. Both arrive
/// synchronously and expect the bitmap before the handler returns.
LRESULT CALLBACK thumbnailSubclass(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                   DWORD_PTR ref)
{
    auto *self = reinterpret_cast<WindowsTaskbarThumbnail *>(ref);
    if (message == WM_NCDESTROY) {
        // The window is going away: drop the cached bitmaps before DWM can ask
        // for them again, and remove the subclass while the handle is valid.
        if (self)
            self->clear();
        RemoveWindowSubclass(hwnd, thumbnailSubclass, ThumbnailSubclassId);
    } else if (message == WmSendIconicThumbnail || message == WmSendIconicLivePreview) {
        if (self) {
            self->sendBitmap(message, qintptr(lparam));
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}
bool setIconicFlag(HWND hwnd, DWORD attribute, bool enabled)
{
    const BOOL value = enabled ? TRUE : FALSE;
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value)));
}
} // namespace
WindowsTaskbarThumbnail::WindowsTaskbarThumbnail(QObject *parent)
    : QObject(parent), m_diagnostics(qEnvironmentVariableIntValue("ECHO_TASKBAR_DIAGNOSTICS") == 1)
{
}
WindowsTaskbarThumbnail::~WindowsTaskbarThumbnail()
{
    clear();
}
bool WindowsTaskbarThumbnail::available() const
{
    BOOL compositing = FALSE;
    return SUCCEEDED(DwmIsCompositionEnabled(&compositing)) && compositing;
}
void WindowsTaskbarThumbnail::releaseBitmaps()
{
    if (m_thumbnail.handle)
        DeleteObject(reinterpret_cast<HGDIOBJ>(m_thumbnail.handle));
    if (m_preview.handle)
        DeleteObject(reinterpret_cast<HGDIOBJ>(m_preview.handle));
    m_thumbnail = {};
    m_preview = {};
}
bool WindowsTaskbarThumbnail::canReuse(const Bitmap &cache, QSize tile) const
{
    return cache.handle && cache.size == tile && cache.generation == m_generation;
}
bool WindowsTaskbarThumbnail::apply(quintptr hwnd, const QImage &cover)
{
    if (hwnd == 0 || cover.isNull())
        return false;
    const HWND handle = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(handle))
        return false;
    const bool sameWindow = m_subclassed && m_hwnd == hwnd;
    if (!sameWindow) {
        // A different window: none of the installed state belongs to it.
        clear();
        if (!SetWindowSubclass(handle, thumbnailSubclass, ThumbnailSubclassId,
                               reinterpret_cast<DWORD_PTR>(this)))
            return false;
        m_subclassed = true;
        m_hwnd = hwnd;
        // The DWMWA_* constants are enum members, not macros, so they must be
        // used directly: an #ifndef fallback would silently redefine them (11
        // is DWMWA_DISALLOW_PEEK, seven is the iconic-representation opt-in).
        const bool hasBitmap = setIconicFlag(handle, DWMWA_HAS_ICONIC_BITMAP, true);
        const bool forceIconic = setIconicFlag(handle, DWMWA_FORCE_ICONIC_REPRESENTATION, true);
        if (!hasBitmap || !forceIconic) {
            // A window left in FORCE_ICONIC without a bitmap shows an empty
            // preview; roll the whole opt-in back instead.
            setIconicFlag(handle, DWMWA_HAS_ICONIC_BITMAP, false);
            setIconicFlag(handle, DWMWA_FORCE_ICONIC_REPRESENTATION, false);
            RemoveWindowSubclass(handle, thumbnailSubclass, ThumbnailSubclassId);
            m_subclassed = false;
            m_hwnd = 0;
            m_cover = {};
            return false;
        }
    }
    // A new track reuses the installed subclass and attributes: turning them
    // off and on again makes Explorer fall back to its own live snapshot of a
    // window that is still animating the new track into place.
    m_cover = cover;
    ++m_generation;
    // Render the composite once per artwork change, off DWM's request path: a
    // request that lands right after a switch then only resamples these pixels
    // into a DIB, which keeps the synchronous answer as short as the message
    // queue allows.
    m_composite = renderTaskbarThumbnail(cover, taskbarCompositeTile(cover.size()));
    // DWM caches the last preview until it is told the source changed.
    DwmInvalidateIconicBitmaps(handle);
    // The invalidation empties DWM's cache, and a surface opened before the
    // next request is answered (Task View, Alt+Tab) composes its card from the
    // window's live geometry: the late reply swaps pixels but never re-lays the
    // card out, so a square cover reads letterboxed by blank bands until the
    // following display. Refill the cache right away with the shapes DWM has
    // already accepted, so every later display starts from a cached bitmap.
    if (!m_thumbnail.size.isEmpty())
        supply(reinterpret_cast<quintptr>(handle), m_thumbnail, m_thumbnail.size, false);
    supply(reinterpret_cast<quintptr>(handle), m_preview, taskbarLivePreviewTile(), true);
    return true;
}
void WindowsTaskbarThumbnail::clear()
{
    releaseBitmaps();
    const auto handle = reinterpret_cast<HWND>(m_hwnd);
    if (m_subclassed && handle && IsWindow(handle)) {
        setIconicFlag(handle, DWMWA_FORCE_ICONIC_REPRESENTATION, false);
        setIconicFlag(handle, DWMWA_HAS_ICONIC_BITMAP, false);
        RemoveWindowSubclass(handle, thumbnailSubclass, ThumbnailSubclassId);
        DwmInvalidateIconicBitmaps(handle);
    }
    m_subclassed = false;
    m_hwnd = 0;
    m_cover = {};
    m_composite = {};
}
void WindowsTaskbarThumbnail::sendBitmap(quint32 message, qintptr packedSize)
{
    const auto handle = reinterpret_cast<HWND>(m_hwnd);
    if (!handle || m_cover.isNull())
        return;
    const bool preview = message == WmSendIconicLivePreview;
    // A live preview keeps the square peek window; a thumbnail request packs
    // its maximum width in the high word and its maximum height in the low
    // word, and the tile is the largest square inside those bounds so the card
    // is square and the complete cover fits inside it.
    const QSize tile = preview ? taskbarLivePreviewTile()
                               : taskbarThumbnailTile(taskbarThumbnailRequest(packedSize));
    supply(reinterpret_cast<quintptr>(handle), preview ? m_preview : m_thumbnail, tile, preview);
    if (m_diagnostics) {
        if (preview)
            qCInfo(taskbarLog) << "live-preview" << "tile" << tile << "generation" << m_generation;
        else
            qCInfo(taskbarLog) << "thumbnail" << "tile" << tile << "requested"
                               << taskbarThumbnailRequest(packedSize) << "generation"
                               << m_generation;
    }
}
bool WindowsTaskbarThumbnail::supply(quintptr handle, Bitmap &cache, QSize tile, bool preview)
{
    if (!tile.isEmpty() && !canReuse(cache, tile)) {
        if (cache.handle)
            DeleteObject(reinterpret_cast<HGDIOBJ>(cache.handle));
        // Resample the composite rendered in apply(): both tiles are square, so
        // one smooth scale — down or up, whichever the tile needs — reproduces
        // the direct render without running the painter here.
        const QImage image = tile == m_composite.size()
                                 ? m_composite
                                 : m_composite.scaled(tile, Qt::IgnoreAspectRatio);
        cache.handle = quintptr(createTaskbarDib(image));
        cache.size = tile;
        cache.generation = m_generation;
    }
    if (!cache.handle)
        return false;
    const HBITMAP bitmap = reinterpret_cast<HBITMAP>(cache.handle);
    // DWM documents the calls as answers to its requests, but they set the
    // window's cached representation wherever they run, and supplying them
    // right after an invalidation is what keeps Task View from composing a
    // card around the stale geometry.
    const HRESULT result =
        preview ? DwmSetIconicLivePreviewBitmap(reinterpret_cast<HWND>(handle), bitmap, nullptr, 0)
                : DwmSetIconicThumbnail(reinterpret_cast<HWND>(handle), bitmap, 0);
    if (m_diagnostics)
        qCInfo(taskbarLog) << (preview ? "supply-preview" : "supply-thumbnail") << "tile" << tile
                           << "generation" << m_generation << "result"
                           << QString("0x%1").arg(uint(result), 8, 16, QLatin1Char('0'));
    return SUCCEEDED(result);
}
#else
WindowsTaskbarThumbnail::WindowsTaskbarThumbnail(QObject *parent) : QObject(parent) {}
WindowsTaskbarThumbnail::~WindowsTaskbarThumbnail() = default;
bool WindowsTaskbarThumbnail::available() const
{
    return false;
}
bool WindowsTaskbarThumbnail::canReuse(const Bitmap &, QSize) const
{
    return false;
}
bool WindowsTaskbarThumbnail::apply(quintptr hwnd, const QImage &cover)
{
    Q_UNUSED(hwnd)
    Q_UNUSED(cover)
    return false;
}
void WindowsTaskbarThumbnail::clear() {}
void WindowsTaskbarThumbnail::releaseBitmaps() {}
void WindowsTaskbarThumbnail::sendBitmap(quint32 message, qintptr packedSize)
{
    Q_UNUSED(message)
    Q_UNUSED(packedSize)
}
#endif
} // namespace EchoMusic
