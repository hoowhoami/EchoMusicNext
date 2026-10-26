#include "WindowsTaskbarThumbnail.h"
#include "WindowsTaskbarInterop.h"
#include <QPainter>
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
/// Subclass identity for the DWM bitmap requests. Distinct from the thumbbar
/// and the taskbar lyric subclasses, which share the same window.
constexpr UINT_PTR ThumbnailSubclassId = 0x45434854;
constexpr UINT WmSendIconicThumbnail = 0x0323;
constexpr UINT WmSendIconicLivePreview = 0x0326;
/// DWM never asks for more than this; the cover is scaled to the request anyway
/// (EchoMusic MAX_SIZE).
constexpr int MaxPreviewSize = 600;
/// DWM returns the requested size in HIWORD/LOWORD of LPARAM.
inline int requestWidth(qintptr packed)
{
    return int((quint32(packed) >> 16) & 0xffff);
}
inline int requestHeight(qintptr packed)
{
    return int(quint32(packed) & 0xffff);
}
/// Renders the artwork into a square tile: the cover is shown whole, centred,
/// without cropping, so the preview keeps the artwork's own shape. DWM rejects
/// a bitmap larger than the requested maximum in either dimension, so the side
/// is the smaller of the two.
QImage renderPreview(const QImage &cover, int side)
{
    if (cover.isNull())
        return {};
    side = qBound(1, side, MaxPreviewSize);
    QImage tile(side, side, QImage::Format_ARGB32_Premultiplied);
    // Only a non-square cover can leave a band; a defined background keeps the
    // tile opaque for the compositor.
    tile.fill(Qt::black);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage fitted = cover.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawImage(QPoint((side - fitted.width()) / 2, (side - fitted.height()) / 2), fitted);
    return tile;
}
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
WindowsTaskbarThumbnail::WindowsTaskbarThumbnail(QObject *parent) : QObject(parent) {}
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
bool WindowsTaskbarThumbnail::apply(quintptr hwnd, const QImage &cover)
{
    if (hwnd == 0 || cover.isNull())
        return false;
    clear();
    const HWND handle = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(handle))
        return false;
    if (!SetWindowSubclass(handle, thumbnailSubclass, ThumbnailSubclassId,
                           reinterpret_cast<DWORD_PTR>(this)))
        return false;
    m_subclassed = true;
    m_hwnd = hwnd;
    m_cover = cover;
    // The DWMWA_* constants are enum members, not macros, so they must be used
    // directly: an #ifndef fallback would silently redefine them (11 is
    // DWMWA_DISALLOW_PEEK, seven is the iconic-representation opt-in).
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
    // A peek names no size; a thumbnail request packs its maximum width in the
    // high word and its maximum height in the low word.
    const int limitWidth = preview ? MaxPreviewSize : requestWidth(packedSize);
    const int limitHeight = preview ? MaxPreviewSize : requestHeight(packedSize);
    Bitmap &cache = preview ? m_preview : m_thumbnail;
    const QImage tile = renderPreview(m_cover, qMin(limitWidth, limitHeight));
    if (cache.handle == 0 || cache.width != tile.width() || cache.height != tile.height()) {
        if (cache.handle)
            DeleteObject(reinterpret_cast<HGDIOBJ>(cache.handle));
        cache.handle = quintptr(createTaskbarDib(tile));
        cache.width = tile.width();
        cache.height = tile.height();
    }
    if (!cache.handle)
        return;
    const HBITMAP bitmap = reinterpret_cast<HBITMAP>(cache.handle);
    if (preview)
        DwmSetIconicLivePreviewBitmap(handle, bitmap, nullptr, 0);
    else
        DwmSetIconicThumbnail(handle, bitmap, 0);
}
#else
WindowsTaskbarThumbnail::WindowsTaskbarThumbnail(QObject *parent) : QObject(parent) {}
WindowsTaskbarThumbnail::~WindowsTaskbarThumbnail() = default;
bool WindowsTaskbarThumbnail::available() const
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
