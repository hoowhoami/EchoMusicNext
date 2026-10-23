#pragma once
#include <QImage>
#include <QObject>
namespace EchoMusic
{
/// Windows taskbar hover preview (DWM iconic bitmaps). The preview normally
/// shows a live copy of the window; this adapter replaces it with the album
/// cover while a track is loaded.
///
/// DWM asks for the bitmap with a window message, so the window is subclassed
/// and the request answered synchronously with a DIB section fitted to the
/// bounds DWM names (EchoMusic native taskbar/thumbnail.rs).
class WindowsTaskbarThumbnail final : public QObject
{
    Q_OBJECT
public:
    explicit WindowsTaskbarThumbnail(QObject *parent = nullptr);
    ~WindowsTaskbarThumbnail() override;
    /// False when the DWM interface is unavailable (composition disabled).
    bool available() const;
    /// Shows the cover as the preview. An empty image restores the system's
    /// live preview so a failed decode never leaves a blank thumbnail.
    bool apply(quintptr hwnd, const QImage &cover);
    /// Restores the live preview and releases the window subclass.
    void clear();
    /// Answers a WM_DWMSENDICONIC* request. Called by the window subclass.
    void sendBitmap(quint32 message, qintptr packedSize);

private:
    void releaseBitmaps();
    /// Cached HBITMAP for one of the two DWM requests, kept alive because DWM
    /// reads it after the call returns.
    struct Bitmap
    {
        quintptr handle = 0;
        int width = 0;
        int height = 0;
    };
    quintptr m_hwnd = 0;
    QImage m_cover;
    Bitmap m_thumbnail;
    Bitmap m_preview;
    bool m_subclassed = false;
};
} // namespace EchoMusic
