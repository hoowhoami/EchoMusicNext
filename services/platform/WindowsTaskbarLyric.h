#pragma once
#include <QObject>
#include <QRect>
#include <QWindow>
#include <memory>
#include <QtQml/qqmlregistration.h>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace EchoMusic
{
/// Free-area layout of the primary taskbar, in physical pixels relative to the
/// taskbar client area (mirrors EchoMusic's NativeTaskbarLayout semantics).
struct TaskbarLayout
{
    QRect left;
    QRect right;
    /// Width of the whole taskbar client area in physical pixels. The fixed
    /// position modes anchor to the taskbar edges, not to a free area.
    int barWidth = 0;
    qreal scaleFactor = 1;
    bool dark = false;
    bool available = false;
    /// False when the UIA enumeration is untrustworthy (provider missing,
    /// truncated, ...); consumers must treat the layout as unknown.
    bool reliable = false;
};

/// Mounts a top-level window as a child of the taskbar (Shell_TrayWnd) and
/// reports the taskbar's free areas. Windows only; other platforms are no-ops.
///
/// Mount order is load-bearing (PeriTray taskbar_widget.rs): restyle to
/// WS_CHILD *before* SetParent, then double-check the parent. Detach must hide
/// and SetParent(NULL) *before* the window is destroyed (EchoMusic
/// taskbar/lyric.rs) - letting Explorer destroy a foreign child crashes shells.
class WindowsTaskbarLyric final : public QObject
{
    Q_OBJECT
public:
    explicit WindowsTaskbarLyric(QObject *parent = nullptr);
    ~WindowsTaskbarLyric() override;
    /// Mounts the window and starts layout probing. Re-attach after detach.
    bool attach(QWindow *window);
    /// Keeps the mount valid: the toolkit may recreate the platform window
    /// behind our back, which silently unmounts the strip.
    bool ensureMounted(QWindow *window);
    /// Native handle of the mounted window (0 when unmounted).
    quintptr mountedHandle() const
    {
        return m_mounted;
    }
    /// Re-asserts the expose state of the mounted window. A cross process
    /// SetParent drops the toolkit's exposure, and without it the scene graph
    /// stops presenting frames.
    void refreshExposure();
    /// Sets the strip's left edge inside the taskbar client area (physical
    /// pixels). Used by the drag handling and by the fixed position modes.
    void setStripOffset(int x);
    /// Current left edge of the strip inside the taskbar client area.
    int stripOffset() const
    {
        return m_stripOffset;
    }
    /// Re-runs the mount for the current window (self-healing after the
    /// toolkit recreates the native window underneath us).
    void remount();
    /// Detaches and stops probing. Call before the window is destroyed.
    void detach();
    bool mounted() const
    {
        return m_window != nullptr;
    }

signals:
    void layoutChanged(const EchoMusic::TaskbarLayout &layout);
    /// The strip was right-clicked. The shell shows the context menu at the
    /// given screen position (physical pixels).
    void stripRightClicked(int globalX, int globalY);

private:
    /// Mounts (or re-mounts) the window without touching the probe worker.
    bool mountWindow(QWindow *window);
    /// Releases the mounted window (no worker teardown).
    void unmountWindow();
    void ensureWorker();
    void stopWorker();
    QWindow *m_window = nullptr;
    /// Native window handle of the mount, 0 when unmounted. An opaque value
    /// rather than a pointer: the platform handle type does not exist off
    /// Windows, and no code may dereference it.
    quintptr m_mounted = 0;
    struct Worker;
    // Shared ownership: the probe thread keeps the worker alive even when the
    // session dies first, so it can never touch freed memory.
    std::shared_ptr<Worker> m_worker;
    int m_stripOffset = 0;
    int m_clientWidth = 0;
};
} // namespace EchoMusic

Q_DECLARE_METATYPE(EchoMusic::TaskbarLayout)
