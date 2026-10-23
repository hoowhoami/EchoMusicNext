#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include "TaskbarLyricGeometry.h"
#include "TaskbarLyricPreferences.h"
#include "WindowsTaskbarLyric.h"

class QSettings;

namespace EchoMusic
{
/// Owns the taskbar lyric surface: mounts the QML window into the taskbar,
/// resolves the free-area region per preferences and keeps it positioned.
///
/// Layout semantics mirror EchoMusic shared/taskbar.ts
/// resolveTaskbarLyricRegion(): the strip prefers the chosen side, one side
/// falling below the minimum falls back to the other, and neither side
/// fitting hides the window.
class TaskbarLyricController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the window controller")
    Q_PROPERTY(EchoMusic::TaskbarLyricPreferences *settings READ settings CONSTANT)
    // The preference object only has one generic changed() signal, so a QML
    // binding on its plain properties would never re-evaluate. These mirror the
    // values the strip needs with a proper notify signal.
    Q_PROPERTY(int maxWidth READ maxWidth NOTIFY settingsChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY settingsChanged)
    Q_PROPERTY(bool showTranslation READ showTranslation NOTIFY settingsChanged)
    Q_PROPERTY(bool wordByWord READ wordByWord NOTIFY settingsChanged)
    /// The platform can mount into the taskbar (Windows only, today).
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY stateChanged)
    /// The strip's content hugs the right edge of its seat (left otherwise).
    /// Drives the mirrored layout, the clip rectangle and the scroll direction.
    Q_PROPERTY(bool anchorRight READ anchorRight NOTIFY stateChanged)
    /// Width of the mounted strip in device-independent pixels.
    Q_PROPERTY(qreal currentWidth READ currentWidth NOTIFY stateChanged)
    /// Size of the strip surface in device-independent pixels. The QML window
    /// must follow it: the toolkit's own size is stale for a window that is
    /// mounted into the taskbar by hand.
    Q_PROPERTY(qreal stripHeight READ stripHeight NOTIFY stateChanged)
    /// Pointer over the visible strip. Tracked natively: the control buttons
    /// grab the mouse, which would otherwise steal a QML hover and make the
    /// strip oscillate between expanded and collapsed.
    Q_PROPERTY(bool hovered READ hovered NOTIFY stateChanged)
    /// The user may drag the strip to any place on the taskbar.
    Q_PROPERTY(bool dragEnabled READ dragEnabled NOTIFY stateChanged)
public:
    TaskbarLyricController(QSettings &settings, QObject *parent = nullptr);
    ~TaskbarLyricController() override;
    TaskbarLyricPreferences *settings()
    {
        return &m_settingsStore;
    }
    int maxWidth() const
    {
        return m_settingsStore.maxWidth();
    }
    int fontSize() const
    {
        return m_settingsStore.fontSize();
    }
    bool showTranslation() const
    {
        return m_settingsStore.showTranslation();
    }
    bool wordByWord() const
    {
        return m_settingsStore.wordByWord();
    }
    bool available() const;
    bool enabled() const
    {
        return m_enabled;
    }
    void setEnabled(bool value);
    bool dark() const
    {
        return m_dark;
    }
    /// The edge of the resolved seat the content hugs. The geometry resolves it
    /// as a side; QML only ever needs the mirror bit.
    bool anchorRight() const
    {
        return m_anchor == TaskbarLyricGeometry::Side::Right;
    }
    qreal currentWidth() const
    {
        return m_currentWidth;
    }
    qreal stripHeight() const
    {
        return m_stripHeight;
    }
    bool hovered() const
    {
        return m_hovered;
    }
    /// Dragging is only offered in the manual drag position mode.
    bool dragEnabled() const
    {
        return m_settingsStore.position() == TaskbarLyricPreferences::Position::Drag;
    }
    /// The strip shrinks to its content; the native side resizes and clips the
    /// mounted window so only the content reacts to the pointer (EchoMusic
    /// lyricWidth.ts + taskbarMediaBar applyShape).
    Q_INVOKABLE void setContentWidth(int widthDip);
    /// Called by the QML window on creation/destruction.
    Q_INVOKABLE void attach(QQuickWindow *window);
    Q_INVOKABLE void detach();
    /// Manual drag (the "drag" position mode). The QML cover reports the
    /// press, the pointer movement and the release; the controller moves the
    /// platform window. The movement is measured with the cursor's SCREEN
    /// position: the window moves under the cursor while dragging, so window
    /// coordinates would drift with it and the strip would fight the pointer.
    Q_INVOKABLE void dragBegin();
    Q_INVOKABLE void dragUpdate();
    Q_INVOKABLE void dragEnd();
    /// The strip was right-clicked; the shell shows the context menu at the
    /// cursor. The argument is the screen position in PHYSICAL pixels as the
    /// platform reports it.
    void onStripRightClicked(int globalX, int globalY);

signals:
    void enabledChanged();
    void stateChanged();
    /// Emitted when any taskbar lyric preference changes.
    void settingsChanged();
    /// The lyric surface was clicked; the shell decides what "open main" is.
    void openMainRequested();
    /// The strip was right-clicked at the given global position; the shell
    /// shows its context menu.
    void menuRequested(qreal globalX, qreal globalY);

private:
    void applyLayout(const TaskbarLayout &layout);
    void present();
    void refreshFullscreen();
    void syncMounted();
    /// Re-kicks the paint request that keeps the re-parented strip presenting,
    /// but only when the scene graph has actually gone quiet.
    void kickExposure();
    QSettings &m_settings;
    TaskbarLyricPreferences m_settingsStore;
    WindowsTaskbarLyric m_session;
    QPointer<QQuickWindow> m_window;
    TaskbarLayout m_layout;
    QTimer m_fullscreenPoll;
    QTimer m_mountWatch;
    QTimer m_exposure;
    /// Timestamp of the last presented frame. The exposure re-kick exists for
    /// frames that stopped arriving, so a frame that did arrive silences it.
    static constexpr qint64 kFrameQuietMs = 120;
    qint64 m_lastFrameMs = 0;
    bool m_enabled = false;
    bool m_dark = false;
    bool m_hiddenByFullscreen = false;
    /// present() found a seat and no hide reason. The lyric update path also
    /// reaches applyContentWidth(), which must not reveal the strip without it.
    bool m_showable = false;
    bool m_firstFrameConnected = false;
    TaskbarLyricGeometry::Side m_anchor = TaskbarLyricGeometry::Side::Left;
    qreal m_currentWidth = 0;
    qreal m_stripHeight = 0;
    bool m_hovered = false;
    /// Left edge of the strip in the taskbar client area (physical pixels) in
    /// the manual drag mode; -1 means "not placed yet".
    int m_dragOffsetX = -1;
    /// Left edge when the current drag started (physical pixels).
    int m_dragOriginX = -1;
    /// Cursor screen position when the current drag started.
    int m_dragOriginCursorX = 0;
    /// Resolved region in taskbar-client physical pixels.
    QRect m_region;
    void applyContentWidth();
    /// Re-mounts the strip when the toolkit recreated the platform window and
    /// keeps its geometry in sync with the taskbar region.
    void watchMount();
    /// Reveals the mounted strip through the native layered alpha.
    void revealStrip();
    /// Native layered alpha of the strip (0 hides it without stopping the
    /// scene graph, 255 shows it).
    void setStripAlpha(int alpha);
    int m_contentWidth = 0;
};
} // namespace EchoMusic
