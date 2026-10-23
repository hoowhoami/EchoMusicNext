#include "TaskbarLyricController.h"
#include <QDateTime>
#include <QDebug>
#include <QLoggingCategory>
#include <optional>
#include "SettingsKeys.h"
#include "TaskbarLyricGeometry.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif
namespace EchoMusic
{
namespace
{
#ifdef Q_OS_WIN
Q_LOGGING_CATEGORY(taskbarLyricLog, "echo.shell.taskbar-lyric")
#endif
} // namespace

TaskbarLyricController::TaskbarLyricController(QSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_settingsStore(settings, this)
{
#ifdef Q_OS_WIN
    connect(&m_session, &WindowsTaskbarLyric::layoutChanged, this,
            &TaskbarLyricController::applyLayout);
    connect(&m_session, &WindowsTaskbarLyric::stripRightClicked, this,
            &TaskbarLyricController::onStripRightClicked);
#endif
    m_fullscreenPoll.setInterval(1000);
    connect(&m_fullscreenPoll, &QTimer::timeout, this, &TaskbarLyricController::refreshFullscreen);
    m_mountWatch.setInterval(150);
    connect(&m_mountWatch, &QTimer::timeout, this, &TaskbarLyricController::watchMount);
    // The re-parented window loses its exposure, and without a paint request the
    // scene graph stops presenting - so the strip freezes. Forcing that request
    // on a fixed 60 Hz schedule instead makes the strip's parent (the shell's
    // own taskbar window) redraw constantly, and the shell then swallows clicks
    // meant for its own buttons: the taskbar's minimize toggle started failing
    // intermittently with the strip enabled (measured 2/4 with the strip, 5/5
    // without). Frames that flow need no help, so the request is only re-kicked
    // once the scene graph has actually gone quiet.
    m_exposure.setInterval(16);
    connect(&m_exposure, &QTimer::timeout, this, [this] {
        if (m_enabled)
            kickExposure();
    });
    connect(&m_settingsStore, &TaskbarLyricPreferences::changed, this,
            &TaskbarLyricController::present);
    connect(&m_settingsStore, &TaskbarLyricPreferences::changed, this,
            &TaskbarLyricController::settingsChanged);
    m_enabled = m_settings.value(SettingsKeys::Taskbar::Enabled, false).toBool();
    // QUNS is only meaningful once mounted; the poll stays tied to the session.
    syncMounted();
}

TaskbarLyricController::~TaskbarLyricController()
{
    detach();
}

bool TaskbarLyricController::available() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void TaskbarLyricController::setEnabled(bool value)
{
    if (m_enabled == value)
        return;
    m_enabled = value;
    m_settings.setValue(SettingsKeys::Taskbar::Enabled, value);
    syncMounted();
    emit enabledChanged();
}

void TaskbarLyricController::attach(QQuickWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    // Frames that do arrive silence the exposure re-kick; see the m_exposure
    // hookup for why the timer must not run unconditionally.
    if (window) {
        m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
        connect(window, &QQuickWindow::frameSwapped, this,
                [this] { m_lastFrameMs = QDateTime::currentMSecsSinceEpoch(); });
    }
    syncMounted();
}

void TaskbarLyricController::detach()
{
    if (!m_window)
        return;
    m_window.clear();
    syncMounted();
}

void TaskbarLyricController::syncMounted()
{
#ifdef Q_OS_WIN
    if (m_enabled && m_window) {
        if (!m_session.mounted()) {
            // EchoMusic: embed only AFTER the first frame - the transparent
            // composition surface must be drawn before SetParent, or the
            // child-form swapchain never presents (blank window).
            if (!m_firstFrameConnected && m_window) {
                m_firstFrameConnected = true;
                // Qt must consider the window visible, otherwise the scene
                // graph stops rendering and the strip freezes on its first
                // frame. The native alpha keeps it invisible until the mount
                // is placed on the taskbar.
                setStripAlpha(0);
                connect(
                    m_window, &QQuickWindow::frameSwapped, this,
                    [this] {
                        if (m_session.mounted())
                            return;
                        qCInfo(taskbarLyricLog)
                            << "首帧就绪，嵌入任务栏:"
                            << (m_session.attach(m_window.data()) ? "成功" : "失败");
                        // Place it on the taskbar first, then reveal:
                        // present() reveals only when a region was found,
                        // so a mount without a slot stays invisible.
                        present();
                    },
                    Qt::SingleShotConnection);
            }
            m_window->show();
        }
        m_fullscreenPoll.start();
        m_mountWatch.start();
        m_exposure.start();
    } else {
        m_fullscreenPoll.stop();
        m_mountWatch.stop();
        m_exposure.stop();
        m_session.detach();
    }
#else
    m_fullscreenPoll.stop();
#endif
    present();
}

void TaskbarLyricController::applyLayout(const TaskbarLayout &layout)
{
    const bool wasDark = m_dark;
    m_layout = layout;
    m_dark = layout.available && layout.reliable && layout.dark;
    present();
    if (m_dark != wasDark)
        emit stateChanged();
}

void TaskbarLyricController::onStripRightClicked(int globalX, int globalY)
{
    // The platform reports the cursor in physical pixels; the menu is placed in
    // Qt's logical coordinates, so the window scale has to be divided out.
    const qreal scale = m_layout.scaleFactor > 0 ? m_layout.scaleFactor : 1.0;
    emit menuRequested(globalX / scale, globalY / scale);
}

void TaskbarLyricController::dragBegin()
{
#ifdef Q_OS_WIN
    POINT cursor{};
    if (!GetCursorPos(&cursor))
        return;
    // The drag compared the pointer with the strip's own position, so both
    // have to be read in the same coordinate space.
    m_dragOriginCursorX = cursor.x;
    m_dragOriginX = m_region.x();
#endif
}

void TaskbarLyricController::dragUpdate()
{
#ifdef Q_OS_WIN
    if (m_settingsStore.position() != TaskbarLyricPreferences::Position::Drag || m_dragOriginX < 0)
        return;
    POINT cursor{};
    if (!GetCursorPos(&cursor))
        return;
    // The strip follows the pointer one to one: both the origin and the delta
    // are physical pixels, so the window moving under the cursor cannot feed
    // back into the calculation.
    const int target = m_dragOriginX + (cursor.x - m_dragOriginCursorX);
    m_session.setStripOffset(target);
    m_dragOffsetX = m_session.stripOffset();
    m_region.moveLeft(m_dragOffsetX);
#endif
}

void TaskbarLyricController::dragEnd()
{
#ifdef Q_OS_WIN
    if (m_settingsStore.position() != TaskbarLyricPreferences::Position::Drag)
        return;
    m_session.setStripOffset(m_dragOffsetX);
    m_settingsStore.setDragOffsetX(m_session.stripOffset());
#endif
}

void TaskbarLyricController::present()
{
#ifdef Q_OS_WIN
    if (!m_window || !m_session.mounted()) {
        m_hiddenByFullscreen = false;
        m_showable = false;
        emit stateChanged();
        return;
    }
    const auto mode = m_settingsStore.position();
    const int maxWidth = m_settingsStore.maxWidth();
    // The modes differ in what they anchor to: the automatic ones prefer a free
    // area and fall back to the other one, the fixed ones pin to a taskbar edge,
    // and the drag mode keeps the offset the user dropped the strip at.
    std::optional<TaskbarLyricGeometry::Region> region;
    using Side = TaskbarLyricGeometry::Side;
    switch (mode) {
    case TaskbarLyricPreferences::Position::Drag:
        // The user's drop point wins; the free areas no longer apply. The
        // layout must not move the strip afterwards either - only a change of
        // the position mode or a narrower taskbar may clamp it.
        if (m_dragOffsetX < 0)
            m_dragOffsetX = m_settingsStore.dragOffsetX();
        region =
            TaskbarLyricGeometry::resolveDragged(m_layout, Side::Left, maxWidth, m_dragOffsetX);
        break;
    case TaskbarLyricPreferences::Position::FixedLeft:
        region = TaskbarLyricGeometry::resolveFixed(m_layout, Side::Left, maxWidth);
        break;
    case TaskbarLyricPreferences::Position::FixedRight:
        region = TaskbarLyricGeometry::resolveFixed(m_layout, Side::Right, maxWidth);
        break;
    case TaskbarLyricPreferences::Position::AutoLeft:
        region = TaskbarLyricGeometry::resolveAuto(m_layout, Side::Left, maxWidth);
        break;
    case TaskbarLyricPreferences::Position::AutoRight:
        region = TaskbarLyricGeometry::resolveAuto(m_layout, Side::Right, maxWidth);
        break;
    }
    const bool showable =
        m_layout.available && m_layout.reliable && region.has_value() && !m_hiddenByFullscreen;
    // applyContentWidth() is reached from the lyric update path as well (the
    // content width drives the window region), so it must know whether the
    // strip is allowed to be on screen at all.
    m_showable = showable;
    if (showable)
        qCDebug(taskbarLyricLog).nospace()
            << "present side="
            << (region->side == TaskbarLyricGeometry::Side::Right ? "right" : "left")
            << " bounds=" << region->bounds << " scale=" << m_layout.scaleFactor;
    else
        qCDebug(taskbarLyricLog).nospace()
            << "hide: available=" << m_layout.available << " reliable=" << m_layout.reliable
            << " fullscreen=" << m_hiddenByFullscreen << " left=" << m_layout.left
            << " right=" << m_layout.right << " scale=" << m_layout.scaleFactor;
    if (!showable) {
        if (m_window->isVisible())
            m_window->hide();
        emit stateChanged();
        return;
    }
    // The mounted window's geometry is parent-relative (taskbar client area),
    // exactly the coordinate space the probe reports.
    // Geometry goes through native SetWindowPos on purpose: after the
    // cross-process SetParent, Windows interprets child coordinates relative
    // to the taskbar client area while Qt still believes the window is a
    // top-level (global DIP). Mixing QWindow::setGeometry here pushes the
    // window off-screen; the explicit parent-relative physical rect plus
    // SWP_FRAMECHANGED is what forces DWM to recompose at the taskbar
    // (exactly the spike-verified path).
    const qreal scale = m_layout.scaleFactor;
    m_anchor = region->side;
    m_region =
        QRect(qRound(region->bounds.x() * scale), qRound(region->bounds.y() * scale),
              qRound(region->bounds.width() * scale), qRound(region->bounds.height() * scale));
    applyContentWidth();
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    qCDebug(taskbarLyricLog).nospace()
        << "native: parent=" << GetParent(hwnd) << " visible=" << IsWindowVisible(hwnd) << " rect=("
        << rc.left << "," << rc.top << "," << rc.right << "," << rc.bottom << ")";
    // The region is valid: the reveal happens here and nowhere else, so the
    // strip never shows up as a misplaced ghost before it is positioned.
    revealStrip();
    emit stateChanged();
#endif
}

void TaskbarLyricController::revealStrip()
{
    setStripAlpha(255);
}

void TaskbarLyricController::setStripAlpha(int alpha)
{
#ifdef Q_OS_WIN
    if (!m_window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd || !IsWindow(hwnd))
        return;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED))
        return;
    // Native alpha instead of QWindow::setOpacity: the toolkit may rebuild the
    // platform window when the opacity changes, which unmounts the strip.
    SetLayeredWindowAttributes(hwnd, 0, BYTE(alpha), LWA_ALPHA);
#endif
}

void TaskbarLyricController::kickExposure()
{
    // Frames that flow need no help: a forced paint request is exactly what the
    // strip's parent window - the shell's own taskbar - has to redraw, and the
    // shell then swallows clicks meant for its own buttons (the taskbar's
    // minimize toggle started failing intermittently with the strip enabled).
    if (!m_window || !m_session.mounted())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFrameMs > 0 && now - m_lastFrameMs < kFrameQuietMs)
        return;
    m_session.refreshExposure();
}
void TaskbarLyricController::watchMount()
{
#ifdef Q_OS_WIN
    if (!m_window || !m_enabled)
        return;
    const auto before = m_session.mountedHandle();
    if (m_session.ensureMounted(m_window.data()) && m_session.mountedHandle() != before) {
        qCInfo(taskbarLyricLog) << "平台窗口被重建，已重新挂载";
        present();
    }
    // The cross process re-parent drops the toolkit exposure; without it the
    // scene graph stops presenting frames and the strip freezes.
    kickExposure();
    // Native hover tracking: the control buttons grab the mouse, so a QML
    // hover would flip between expanded and collapsed while the pointer rests
    // on a button.
    POINT cursor{};
    GetCursorPos(&cursor);
    const HWND under = WindowFromPoint(cursor);
    const bool over = under && under == reinterpret_cast<HWND>(m_window->winId());
    if (over == m_hovered)
        return;
    m_hovered = over;
    emit stateChanged();
#endif
}

void TaskbarLyricController::setContentWidth(int widthDip)
{
    m_contentWidth = qMax(0, widthDip);
    applyContentWidth();
}

void TaskbarLyricController::applyContentWidth()
{
#ifdef Q_OS_WIN
    if (!m_window || !m_session.mounted() || m_region.isEmpty())
        return;
    const qreal scale = m_layout.scaleFactor;
    const int windowWidth = m_region.width();
    const int windowHeight = m_region.height();
    // The window keeps the full region size; only the visible content is
    // clipped. Resizing the window itself would move its edge under the
    // pointer and make the hover state oscillate (EchoMusic keeps the window
    // and applies a window region instead).
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    // Follow present()'s verdict instead of revealing unconditionally: a lyric
    // update also lands here, and showing the window on that path brought the
    // strip back over a fullscreen game (or an unreliable layout) the moment
    // the next line changed.
    const DWORD reveal = m_showable ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
    SetWindowPos(hwnd, HWND_TOP, m_region.x(), m_region.y(), windowWidth, windowHeight,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED | reveal);
    // Qt has to see the window as visible or the scene graph stops producing
    // frames and the strip freezes on its first frame.
    if (m_showable && !m_window->isVisible())
        m_window->show();
    int contentPhysical = qRound(qMax(0, m_contentWidth) * scale);
    if (contentPhysical <= 0)
        contentPhysical = windowWidth;
    contentPhysical = qBound(0, qMin(contentPhysical, windowWidth), windowWidth);
    RECT clip{0, 0, contentPhysical, windowHeight};
    if (m_anchor == TaskbarLyricGeometry::Side::Right)
        clip = RECT{windowWidth - contentPhysical, 0, windowWidth, windowHeight};
    // SetWindowRgn takes ownership of the region.
    SetWindowRgn(hwnd, CreateRectRgn(clip.left, clip.top, clip.right, clip.bottom), TRUE);
    const auto widthDip = qreal(windowWidth) / (scale > 0 ? scale : 1);
    const auto heightDip = qreal(windowHeight) / (scale > 0 ? scale : 1);
    if (!qFuzzyCompare(m_currentWidth, widthDip) || !qFuzzyCompare(m_stripHeight, heightDip)) {
        m_currentWidth = widthDip;
        m_stripHeight = heightDip;
        emit stateChanged();
    }
#endif
}
void TaskbarLyricController::refreshFullscreen()
{
#ifdef Q_OS_WIN
    QUERY_USER_NOTIFICATION_STATE state{};
    bool hidden = false;
    // Exclusive fullscreen games and presentation mode cover the taskbar; the
    // lyric must not float over them (PeriTray's hide criteria).
    if (SUCCEEDED(SHQueryUserNotificationState(&state)))
        hidden = state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
    if (hidden == m_hiddenByFullscreen)
        return;
    m_hiddenByFullscreen = hidden;
    present();
    emit stateChanged();
#else
    // No Win32 notification state off Windows, so nothing can report a
    // fullscreen game and the strip is never hidden for one.
    Q_UNUSED(this);
#endif
}
} // namespace EchoMusic
