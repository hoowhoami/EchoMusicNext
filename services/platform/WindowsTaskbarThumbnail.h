#pragma once
#include <QImage>
#include <QObject>
#include "TaskbarThumbnailLayout.h"
namespace EchoMusic
{
/// Windows taskbar hover preview (DWM iconic bitmaps). The preview normally
/// shows a live copy of the window; this adapter replaces it with the album
/// cover while a track is loaded.
///
/// DWM asks for the bitmap with a window message, so the window is subclassed
/// and the request answered synchronously with a DIB section fitted to the
/// bounds DWM names (EchoMusic native taskbar/thumbnail.rs). An artwork change
/// invalidates DWM's cache and immediately re-supplies the current bitmaps:
/// with an empty cache, a surface opened before the next request is answered
/// (Task View, Alt+Tab) composes its card from the window's live geometry and
/// the late reply never re-lays it out.
class WindowsTaskbarThumbnail final : public QObject
{
    Q_OBJECT
public:
    explicit WindowsTaskbarThumbnail(QObject *parent = nullptr);
    ~WindowsTaskbarThumbnail() override;
    /// False when the DWM interface is unavailable (composition disabled).
    bool available() const;
    /// Shows the cover as the preview. Re-applying to the same window keeps the
    /// subclass and the iconic opt-in installed, invalidates DWM's cached
    /// bitmaps and immediately re-supplies them: a preview displayed while the
    /// cache is empty is composed from the window's live geometry, and the late
    /// reply swaps pixels without re-laying the card out — a square cover would
    /// read letterboxed by blank bands until the following display.
    ///
    /// A null cover is a no-op that keeps whatever preview is already showing:
    /// callers substitute their own placeholder, and tearing the opt-in down
    /// here would leave the window with an empty preview until the next apply.
    bool apply(quintptr hwnd, const QImage &cover);
    /// Restores the live preview and releases the window subclass.
    void clear();
    /// Answers a WM_DWMSENDICONIC* request. Called by the window subclass.
    void sendBitmap(quint32 message, qintptr packedSize);

private:
    void releaseBitmaps();
    /// Cached HBITMAP for one of the two DWM requests. DWM copies the pixels
    /// when it is handed the handle, so the cache is not needed for correctness;
    /// it exists because DWM may ask again for the same size (the peek window
    /// and the card repeat), and rebuilding would mean another DIB section and
    /// blit for pixels that did not change. The generation makes a new cover
    /// with the same tile size still rebuild: a stale cover would otherwise be
    /// handed back.
    struct Bitmap
    {
        quintptr handle = 0;
        QSize size;
        quint64 generation = 0;
    };
    /// Rebuilds the slot's DIB from the composite when needed and hands it to
    /// DWM as the window's iconic thumbnail or live preview. Used both to
    /// answer a request and to refill the cache after an invalidation.
    bool supply(quintptr handle, Bitmap &cache, QSize tile, bool preview);
    bool canReuse(const Bitmap &cache, QSize tile) const;
    quintptr m_hwnd = 0;
    QImage m_cover;
    /// The current cover composited into a square no larger than the cover's
    /// own long side (capped at the live-preview tile), rendered in apply() so
    /// a DWM request only resamples and copies these pixels into a DIB instead
    /// of running the painter on the synchronous request path.
    QImage m_composite;
    quint64 m_generation = 0;
    /// DWM calls sendBitmap synchronously, so the environment is read once here
    /// rather than on every request.
    bool m_diagnostics = false;
    Bitmap m_thumbnail;
    Bitmap m_preview;
    bool m_subclassed = false;
};
} // namespace EchoMusic
