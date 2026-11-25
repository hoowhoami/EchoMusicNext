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
    // DWM caches the last preview until it is told the source changed.
    DwmInvalidateIconicBitmaps(handle);
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
    Bitmap &cache = preview ? m_preview : m_thumbnail;
    if (!canReuse(cache, tile)) {
        if (cache.handle)
            DeleteObject(reinterpret_cast<HGDIOBJ>(cache.handle));
        const QImage image = renderTaskbarThumbnail(m_cover, tile);
        cache.handle = quintptr(createTaskbarDib(image));
        cache.size = tile;
        cache.generation = m_generation;
    }
    if (!cache.handle)
        return;
    const HBITMAP bitmap = reinterpret_cast<HBITMAP>(cache.handle);
    if (preview)
        DwmSetIconicLivePreviewBitmap(handle, bitmap, nullptr, 0);
    else
        DwmSetIconicThumbnail(handle, bitmap, 0);
    if (m_diagnostics) {
        if (preview)
            qCInfo(taskbarLog) << "live-preview" << "tile" << tile << "generation" << m_generation;
        else
            qCInfo(taskbarLog) << "thumbnail" << "tile" << tile << "requested"
                               << taskbarThumbnailRequest(packedSize) << "generation"
                               << m_generation;
    }
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
