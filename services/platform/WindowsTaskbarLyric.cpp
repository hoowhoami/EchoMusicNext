// Windows implementation of the taskbar lyric mount + free-area probe.
//
// Sources of the load-bearing rules (do not "simplify" them away):
//  * EchoMusic native/taskbar/lyric.rs - Shell_TrayWnd must belong to
//    explorer.exe; UIA probing lives on its own thread and never on the UI
//    thread; probe cooldown after Explorer re-arranges the taskbar; detach
//    must happen before the mounted window is destroyed.
//  * PeriTray src-tauri/taskbar_widget.rs - restyle to WS_CHILD *before*
//    SetParent (reversed order fails with err=87); SetParent's return value is
//    ambiguous (the previous parent is NULL on success) so validate via
//    GetLastError + GetParent.
//  * PeriTray src-tauri/taskbar_layout.rs - ask UI Automation for the
//    *elements'* bounding rectangles (start button, pinned/running tasks,
//    tray, clock) instead of guessing empty pixels; any uncertainty (zero
//    elements, truncated enumeration, provider missing) marks the layout
//    unreliable and consumers must fall back to hiding.
#include "WindowsTaskbarLyric.h"
#include <QDebug>
#include <QLoggingCategory>
#include <QMetaObject>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <uiautomationclient.h>
// commctrl.h must follow windows.h: the subclassing API is guarded by
// NTDDI_VERSION, which windows.h settles.
#include <commctrl.h>
#endif

namespace EchoMusic
{
namespace
{
#ifdef Q_OS_WIN

Q_LOGGING_CATEGORY(taskbarLyricLog, "echo.platform.taskbar-lyric")
constexpr auto kShellClass = L"Shell_TrayWnd";
/// EchoMusic lyric.rs: cover the whole "Explorer destroyed + rebuilt taskbar"
/// window before probing again after a failed probe.
constexpr DWORD kRearrangeCooldownMs = 1200;
/// Coalesce bursts of UIA structure changes into one probe.
constexpr DWORD kDebounceMs = 300;
/// Safety net for layout changes UIA events do not report (DPI, displays).
constexpr DWORD kIdlePollMs = 10000;
/// An enumeration above this is truncated; refuse to guess (EchoMusic gate).
constexpr int kElementLimit = 2048;
/// EchoMusic free_intervals: every occupied span widens by ±6 px so a lyric
/// seat never abuts icons and icon-group gaps never count as seats.
constexpr int kSlotMarginPx = 6;
/// Window procedure of the mounted strip.
///
/// The strip is a child of Shell_TrayWnd. Left clicks reach it through the
/// normal hit test, but a RIGHT click on the taskbar area is claimed by the
/// shell's XAML island before Qt ever sees it, so the context menu request has
/// to be taken here. Every other message goes straight back to Qt.
LRESULT CALLBACK StripMouseProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR subclassId, DWORD_PTR referenceData)
{
    Q_UNUSED(referenceData);
    if (message == WM_RBUTTONUP) {
        if (auto *owner =
                reinterpret_cast<WindowsTaskbarLyric *>(reinterpret_cast<void *>(subclassId))) {
            POINT cursor{};
            if (GetCursorPos(&cursor))
                emit owner->stripRightClicked(cursor.x, cursor.y);
        }
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

/// Moves the strip to a left edge inside the taskbar client area.
void moveStrip(HWND strip, int offset)
{
    if (!strip)
        return;
    // The window keeps its size; only the left edge moves.
    SetWindowPos(strip, nullptr, offset, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/// Reads AppsUseLightTheme: the taskbar follows the app theme setting.
bool systemTaskbarDark()
{
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

HWND findShellTrayWnd()
{
    const HWND shell = FindWindowW(kShellClass, nullptr);
    if (!shell)
        return nullptr;
    // EchoMusic: refuse same-named third-party windows - the host must be shell.
    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    WCHAR path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    const BOOL got = proc && QueryFullProcessImageNameW(proc, 0, path, &size);
    if (proc)
        CloseHandle(proc);
    if (!got)
        return nullptr;
    return _wcsicmp(PathFindFileNameW(path), L"explorer.exe") == 0 ? shell : nullptr;
}

/// Taskbar-client-relative horizontal span, physical pixels.
struct Interval
{
    int start;
    int end;
};

/// The identity scan (mirrors EchoMusic lyric.rs Window::layout()): enumerate
/// ONLY the taskbar's XAML islands and name the real controls by identity - the
/// Start pill, search, task view, widgets, and the Windows 11 "TaskListButton"
/// app buttons.
/// Containers, flyout content and odd overlays never match the identity set,
/// so the occupied set needs no post-filtering. The mounted strip is a plain
/// Win32 child of Shell_TrayWnd and lives outside the islands, so it can never
/// survey itself (EchoMusic counts on the same property instead of filtering).
/// Everything is collected taskbar-client-relative, physical pixels.
bool identityOccupied(IUIAutomation *automation, HWND shell, const POINT &origin, int barWidth,
                  QList<Interval> &occupied, Interval &content, bool &hasStart, bool &complete)
{
    if (!automation || !shell)
        return false;
    HWND bridge = nullptr, previous = nullptr;
    while ((bridge =
                FindWindowExW(shell, previous, L"Windows.UI.Composition.DesktopWindowContentBridge",
                              nullptr)) != nullptr) {
        previous = bridge;
        IUIAutomationElement *root = nullptr;
        if (FAILED(automation->ElementFromHandle(bridge, &root)) || !root)
            return false;
        IUIAutomationElementArray *elements = nullptr;
        IUIAutomationCondition *all = nullptr;
        bool got = SUCCEEDED(automation->CreateTrueCondition(&all)) && all &&
                   SUCCEEDED(root->FindAll(TreeScope_Descendants, all, &elements));
        if (all)
            all->Release();
        root->Release();
        if (!got)
            return false;
        int count = 0;
        elements->get_Length(&count);
        if (count > kElementLimit) {
            // EchoMusic: list too big means the query ran wild; refuse anyway.
            elements->Release();
            complete = false;
            return true;
        }
        for (int i = 0; i < count; ++i) {
            IUIAutomationElement *element = nullptr;
            if (FAILED(elements->GetElement(i, &element)) || !element)
                continue;
            BOOL offscreen = TRUE;
            const bool hasOffscreen = SUCCEEDED(element->get_CurrentIsOffscreen(&offscreen));
            BSTR id = nullptr, className = nullptr;
            CONTROLTYPEID controlType = 0;
            RECT rect{};
            const bool hasId = SUCCEEDED(element->get_CurrentAutomationId(&id)) && id;
            SUCCEEDED(element->get_CurrentClassName(&className));
            const bool hasType = SUCCEEDED(element->get_CurrentControlType(&controlType));
            const bool hasRect = SUCCEEDED(element->get_CurrentBoundingRectangle(&rect));
            element->Release();
            const QString automationId = hasId ? QString::fromWCharArray(id) : QString();
            const QString typeClass = className ? QString::fromWCharArray(className) : QString();
            if (id)
                SysFreeString(id);
            if (className)
                SysFreeString(className);
            // Overflown tray items keep stale rects after hiding.
            if (hasOffscreen && offscreen)
                continue;
            const bool isButton = hasType && controlType == 50000; // UIA_ButtonControlTypeId
            const bool isContent = typeClass.contains(QStringLiteral("TaskListButton")) ||
                                   automationId == QStringLiteral("StartButton") ||
                                   automationId == QStringLiteral("SearchButton") ||
                                   automationId == QStringLiteral("SearchBoxTextBlock") ||
                                   automationId == QStringLiteral("TaskViewButton");
            if (!isButton && !isContent && automationId != QStringLiteral("WidgetsButton"))
                continue;
            if (!hasRect || rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0)
                continue;
            const Interval span{qBound(0, int(rect.left - origin.x), barWidth),
                                qBound(0, int(rect.right - origin.x), barWidth)};
            occupied.append(span);
            if (isContent) {
                content.start = qMin(content.start, span.start);
                content.end = qMax(content.end, span.end);
            }
            if (automationId == QStringLiteral("StartButton"))
                hasStart = true;
        }
        elements->Release();
    }
    // The classic tray: from TrayNotifyWnd's left edge to the bar end is
    // occupied. Win10 and Win11 keep this child window, and drawing the line
    // here keeps the overflow-flyout area from ever reading as free space.
    const HWND tray = FindWindowExW(shell, nullptr, L"TrayNotifyWnd", nullptr);
    if (!tray)
        return false;
    RECT trayRect{};
    if (!GetWindowRect(tray, &trayRect) || trayRect.right <= trayRect.left)
        return false;
    occupied.append(Interval{int(trayRect.left - origin.x), barWidth});
    return true;
}

/// EchoMusic geometry.rs taskbar_spaces: gaps between icons must never read
/// as a lyric seat, so a slot qualifies only by lying entirely before the
/// app-button cluster starts or entirely after it ends. Every occupied span
/// widens by ±6 px first so the strip keeps a visual margin from neighbours.
void taskbarSpaces(int barWidth, const Interval &content, QList<Interval> occupied, Interval &left,
                   Interval &right)
{
    occupied.append(content); // EchoMusic pushes the cluster hull itself too
    QList<Interval> spans;
    for (const auto &span : occupied) {
        if (span.end <= span.start)
            continue;
        spans.append(Interval{qBound(0, span.start - kSlotMarginPx, barWidth),
                              qBound(0, span.end + kSlotMarginPx, barWidth)});
    }
    std::sort(spans.begin(), spans.end(), [](const Interval &a, const Interval &b) {
        return a.start != b.start ? a.start < b.start : a.end < b.end;
    });
    QList<Interval> gaps;
    int cursor = 0;
    for (const auto &span : spans) {
        if (span.start > cursor)
            gaps.append(Interval{cursor, span.start});
        cursor = qMax(cursor, span.end);
    }
    if (cursor < barWidth)
        gaps.append(Interval{cursor, barWidth});
    for (const auto &gap : gaps) {
        const int size = gap.end - gap.start;
        if (size <= 0)
            continue;
        if (gap.end <= content.start && size > left.end - left.start)
            left = gap;
        else if (gap.start >= content.end && size > right.end - right.start)
            right = gap;
    }
}

/// Scheme A (this repo's earlier PeriTray-style layout, kept from commit
/// e50c9b6 as the fallback): enumerate interactive elements by control type
/// with a four-way OR condition, exclude the host process by pid (a rect test
/// would swallow icons sitting under the mounted strip), keep only innermost
/// rects to drop UIA containers, then take the widest cluster gaps on each
/// half of the taskbar. Works on Win10 taskbars and survives automation-id
/// renames that would break the identity scan above.
bool legacyOccupied(IUIAutomation *automation, HWND shell, QList<RECT> &blocks, bool &truncated)
{
    if (!automation || !shell)
        return false;
    IUIAutomationElement *taskbar = nullptr;
    bool ok = SUCCEEDED(automation->ElementFromHandle(shell, &taskbar)) && taskbar;
    IUIAutomationElementArray *elements = nullptr;
    if (ok) {
        IUIAutomationCondition *focusable = nullptr, *orAll = nullptr;
        IUIAutomationCondition *button = nullptr, *listItem = nullptr, *tabItem = nullptr;
        VARIANT variant{};
        variant.vt = VT_I4;
        variant.lVal = UIA_ButtonControlTypeId;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, variant, &button);
        variant.lVal = UIA_ListItemControlTypeId;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, variant, &listItem);
        variant.lVal = UIA_TabItemControlTypeId;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, variant, &tabItem);
        variant.vt = VT_BOOL;
        variant.boolVal = VARIANT_TRUE;
        automation->CreatePropertyCondition(UIA_IsKeyboardFocusablePropertyId, variant, &focusable);
        if (button && listItem) {
            IUIAutomationCondition *orFirst = nullptr;
            if (SUCCEEDED(automation->CreateOrCondition(button, listItem, &orFirst)) && orFirst) {
                IUIAutomationCondition *orMiddle = nullptr;
                if (tabItem &&
                    SUCCEEDED(automation->CreateOrCondition(orFirst, tabItem, &orMiddle)) &&
                    orMiddle) {
                    if (focusable)
                        automation->CreateOrCondition(orMiddle, focusable, &orAll);
                    orMiddle->Release();
                }
                orFirst->Release();
            }
        }
        IUIAutomationCondition *finalCondition = orAll ? orAll : focusable;
        if (finalCondition)
            ok = SUCCEEDED(taskbar->FindAll(TreeScope_Descendants, finalCondition, &elements));
        if (button)
            button->Release();
        if (listItem)
            listItem->Release();
        if (tabItem)
            tabItem->Release();
        if (focusable)
            focusable->Release();
        if (orAll)
            orAll->Release();
    }
    if (ok && elements) {
        int count = 0;
        elements->get_Length(&count);
        truncated = count > kElementLimit;
        const DWORD ownPid = GetCurrentProcessId();
        for (int i = 0; i < count && !truncated; ++i) {
            IUIAutomationElement *element = nullptr;
            if (FAILED(elements->GetElement(i, &element)) || !element)
                continue;
            int pid = -1;
            const bool hasPid = SUCCEEDED(element->get_CurrentProcessId(&pid));
            BOOL offscreen = TRUE;
            const bool hasOffscreen = SUCCEEDED(element->get_CurrentIsOffscreen(&offscreen));
            RECT rect{};
            const bool hasRect = SUCCEEDED(element->get_CurrentBoundingRectangle(&rect));
            element->Release();
            if (hasPid && pid > 0 && DWORD(pid) == ownPid)
                continue;
            if (hasOffscreen && offscreen)
                continue;
            if (!hasRect || rect.right - rect.left <= 0 || rect.bottom - rect.top <= 0)
                continue;
            blocks.append(rect);
        }
    }
    if (elements)
        elements->Release();
    if (taskbar)
        taskbar->Release();
    return ok;
}

inline bool sameRect(const RECT &a, const RECT &b)
{
    return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
}

void legacyKeepInnermost(QList<RECT> &blocks)
{
    const QList<RECT> all = blocks;
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                [&all](const RECT &outer) {
                                    for (const auto &inner : all) {
                                        if (sameRect(outer, inner))
                                            continue; // duplicates are not containers
                                        if (outer.left <= inner.left &&
                                            outer.right >= inner.right && outer.top <= inner.top &&
                                            outer.bottom >= inner.bottom)
                                            return true;
                                    }
                                    return false;
                                }),
                 blocks.end());
}

/// The parts both scans read from the taskbar window itself: client rect,
/// origin, and the layout fields that follow from them. Nothing when the shell
/// window reports no usable client area (the taskbar is being rebuilt).
struct ShellBar
{
    RECT client{};
    POINT origin{};
};

std::optional<ShellBar> readShellBar(HWND shell, TaskbarLayout &layout)
{
    RECT client{};
    if (!GetClientRect(shell, &client) || client.right - client.left <= 0 ||
        client.bottom - client.top <= 0)
        return std::nullopt;
    POINT origin{client.left, client.top};
    MapWindowPoints(shell, nullptr, &origin, 1);
    const UINT dpi = GetDpiForWindow(shell);
    layout.scaleFactor = dpi ? qreal(dpi) / 96.0 : 1.0;
    layout.barWidth = client.right - client.left;
    layout.dark = systemTaskbarDark();
    layout.available = true;
    return ShellBar{client, origin};
}

/// Win10/11 desktop taskbars are horizontal rows; a vertical dock has no
/// left/right lyric slot this feature can address.
bool horizontalRow(const RECT &client)
{
    return client.bottom - client.top <= client.right - client.left;
}

/// The taskbar row height the strip has to cover. QRect built from two points
/// counts the bottom edge, so the closed range keeps the strip flush with the
/// row exactly like the gap rects do.
int rowHeight(const RECT &client)
{
    return client.bottom - client.top + 1;
}

bool legacyLayout(IUIAutomation *automation, HWND shell, TaskbarLayout &layout, QString *reason)
{
    layout = {};
    const auto bar = readShellBar(shell, layout);
    if (!bar) {
        if (reason)
            *reason = QStringLiteral("taskbar client rect unavailable");
        return false;
    }
    if (!horizontalRow(bar->client)) {
        layout.reliable = false;
        if (reason)
            *reason = QStringLiteral("vertical taskbar has no lyric slot");
        return false;
    }

    bool truncated = false;
    QList<RECT> blocks;
    if (!legacyOccupied(automation, shell, blocks, truncated)) {
        if (reason)
            *reason = QStringLiteral("UIA block probe failed");
        layout.reliable = false;
        return false;
    }
    if (truncated || blocks.isEmpty()) {
        if (reason)
            *reason = truncated ? QStringLiteral("UIA block probe truncated the element list")
                                : QStringLiteral("the taskbar reports no occupied elements");
        layout.reliable = false;
        return false;
    }
    for (auto &block : blocks) {
        block.left -= bar->origin.x;
        block.right -= bar->origin.x;
        block.top -= bar->origin.y;
        block.bottom -= bar->origin.y;
    }
    legacyKeepInnermost(blocks);
    std::sort(blocks.begin(), blocks.end(),
              [](const RECT &a, const RECT &b) { return a.left < b.left; });
    QList<RECT> clusters;
    for (const auto &block : blocks) {
        if (!clusters.isEmpty() && block.left <= clusters.last().right)
            clusters.last().right = qMax(clusters.last().right, block.right);
        else
            clusters.append(block);
    }
    const LONG clientRight = bar->client.right;
    const LONG clientBottom = bar->client.bottom;
    const LONG mid = clientRight / 2;
    QList<RECT> gaps;
    LONG previousRight = 0;
    for (const auto &cluster : clusters) {
        if (cluster.left > previousRight)
            gaps.append(RECT{previousRight, 0, cluster.left, clientBottom});
        previousRight = qMax(previousRight, cluster.right);
    }
    if (clientRight > previousRight)
        gaps.append(RECT{previousRight, 0, clientRight, clientBottom});
    const auto widest = [&](bool leftHalf) {
        RECT best{0, 0, 0, 0};
        for (const auto &gap : gaps) {
            const bool candidate = leftHalf ? gap.left < mid : gap.right > mid;
            if (candidate && gap.right - gap.left > best.right - best.left)
                best = gap;
        }
        return best;
    };
    const RECT leftGap = widest(true);
    const RECT rightGap = widest(false);
    const int height = rowHeight(bar->client);
    layout.left = QRect(QPoint(leftGap.left, 0), QPoint(leftGap.right, height));
    layout.right = QRect(QPoint(rightGap.left, 0), QPoint(rightGap.right, height));
    layout.reliable = layout.left.width() > 0 || layout.right.width() > 0;
    if (!layout.reliable && reason)
        *reason = QStringLiteral("no free gap on either side of the taskbar");
    return layout.reliable;
}

/// Computes left/right free intervals inside the taskbar client area.
/// Returns false when the layout must be treated as unknown.
/// EchoMusic identity scan first, the control-type fallback second.
bool computeLayout(IUIAutomation *automation, HWND shell, TaskbarLayout &layout, QString *reason)
{
    layout = {};
    const auto bar = readShellBar(shell, layout);
    if (!bar) {
        if (reason)
            *reason = QStringLiteral("taskbar client rect unavailable");
        return false;
    }
    if (!horizontalRow(bar->client)) {
        layout.reliable = false;
        if (reason)
            *reason = QStringLiteral("vertical taskbar has no lyric slot");
        return false;
    }
    const int barWidth = bar->client.right - bar->client.left;
    QList<Interval> occupied;
    Interval content{INT_MAX, INT_MIN};
    bool hasStart = false;
    bool complete = true;
    const bool scanned =
        identityOccupied(automation, shell, bar->origin, barWidth, occupied, content, hasStart,
                         complete);
    const bool identityOk = scanned && complete && hasStart && content.start <= content.end;
    if (!identityOk) {
        // The identity scan needs the Win11 XAML islands (a classic Win10
        // taskbar has no DesktopWindowContentBridge and cannot answer with the
        // Start pill); a renamed automation id or a half-built tree fails it
        // just the same. The earlier control-type scan (this repo's original
        // PeriTray-style layout) covers exactly these misses, so the row never
        // distinguishes "no seat" from "unauditable".
        const QString hint = !scanned    ? QStringLiteral("bridges/tray not enumerable")
                             : !complete ? QStringLiteral("enumeration truncated")
                             : !hasStart ? QStringLiteral("no StartButton")
                                         : QStringLiteral("no app-button hull");
        qCInfo(taskbarLyricLog).nospace()
            << "identity scan unavailable (" << hint << "), falling back to the control-type scan";
        return legacyLayout(automation, shell, layout, reason);
    }
    Interval left{0, 0}, right{0, 0};
    taskbarSpaces(barWidth, content, occupied, left, right);
    const int height = rowHeight(bar->client);
    layout.left = QRect(left.start, 0, left.end - left.start, height);
    layout.right = QRect(right.start, 0, right.end - right.start, height);
    // Reliability follows the scan gates, not the slot widths: a packed
    // taskbar legitimately reports no seat and the controller hides instead.
    layout.reliable = true;
    // Evidence line, only when the occupancy changed: a layout change then
    // attributes to concrete spans and the resolved seats. Probe-thread only,
    // like every other piece of computeLayout state: no lock, no sharing.
    static QString lastFingerprint;
    static qint64 lastFingerprintLogMs = 0;
    QString spans;
    const int capped = qMin<int>(24, occupied.size());
    for (int i = 0; i < capped; ++i)
        spans += QStringLiteral("%1..%2, ").arg(occupied[i].start).arg(occupied[i].end);
    if (occupied.size() > capped)
        spans += QStringLiteral("+%1 more").arg(occupied.size() - capped);
    const QString fingerprint = QStringLiteral("n=%1 [%2] cl=%3..%4 L=%5,%6 R=%7,%8")
                                    .arg(occupied.size())
                                    .arg(spans)
                                    .arg(content.start)
                                    .arg(content.end)
                                    .arg(layout.left.x())
                                    .arg(layout.left.width())
                                    .arg(layout.right.x())
                                    .arg(layout.right.width());
    if (fingerprint != lastFingerprint && qint64(GetTickCount64()) - lastFingerprintLogMs > 250) {
        lastFingerprint = fingerprint;
        lastFingerprintLogMs = qint64(GetTickCount64());
        qCDebug(taskbarLyricLog).nospace() << "occupied " << fingerprint;
    }
    return true;
}

/// UIA structure-change listener: any child added/removed/reordered inside the
/// taskbar means the button layout may have changed. The handler only bumps an
/// atomic counter (it fires on COM threads: no work, no waits here).
class StructureChangeHandler final : public IUIAutomationStructureChangedEventHandler
{
public:
    explicit StructureChangeHandler(std::atomic<quint64> &generation, std::mutex *mutex,
                                    std::condition_variable *wake)
        : m_generation(generation), m_mutex(mutex), m_wake(wake)
    {
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_ref;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --m_ref;
        if (!remaining)
            delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IUIAutomationStructureChangedEventHandler) {
            *out = static_cast<IUIAutomationStructureChangedEventHandler *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement *,
                                                          StructureChangeType changeType,
                                                          SAFEARRAY *) override
    {
        // Real layout changes only: ChildrenInvalidated storms the XAML taskbar
        // even when idle (PeriTray counted 59 of them in 15 quiet seconds) and
        // honoring it would keep the probe permanently busy. The informative
        // kinds are add/remove/bulk-add/remove plus ChildrenReordered (opening
        // and closing apps shifts the trailing buttons as a group).
        if (changeType != StructureChangeType_ChildAdded &&
            changeType != StructureChangeType_ChildRemoved &&
            changeType != StructureChangeType_ChildrenBulkAdded &&
            changeType != StructureChangeType_ChildrenBulkRemoved &&
            changeType != StructureChangeType_ChildrenReordered)
            return S_OK;
        // Bump and wake under the waiter's mutex: notify() alone could race a
        // waiter that is between the predicate check and the idle-block, and a
        // bump without any notify would at best shorten the next idle poll.
        std::lock_guard<std::mutex> lock(*m_mutex);
        m_generation.fetch_add(1, std::memory_order_relaxed);
        m_wake->notify_all();
        return S_OK;
    }

private:
    std::atomic<quint64> &m_generation;
    std::mutex *m_mutex;
    std::condition_variable *m_wake;
    std::atomic<ULONG> m_ref{1};
};

#endif // Q_OS_WIN
} // namespace

/// Worker thread: owns the UIA session, coalesces change notifications and
/// publishes layouts onto the owner's thread. All waits happen here - never on
/// the UI thread (EchoMusic/PeriTray thread model).
/// EchoMusic keeps the last foreground window on the watcher thread: the
/// create/rename/cloak families wake the probe only when it hides, dies or
/// cloaks (the fullscreen state re-checks itself on those).
///
/// Cross-thread by design and the only shared state between them: the WinEvent
/// pump writes it, the probe worker reads it, so it stays atomic (never a
/// plain pointer that a pump callback could free under the worker).
static std::atomic<qintptr> winEventForeground{0};

struct WindowsTaskbarLyric::Worker
{
#ifdef Q_OS_WIN
    std::thread thread;
    // EchoMusic's second wake channel: window-level WinEvents. The hooks
    // deliver through the message pump of the installing thread, so this
    // cursor owns one.
    std::thread watcherThread;
    std::atomic<bool> watcherQuit{false};
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<quint64> generation{0};
    std::atomic<bool> quit{false};
    // Cleared when the session dies: the worker must not publish to a dead
    // object any more.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    HWND child = nullptr;

    void notify()
    {
        wake.notify_all();
    }

    static std::shared_ptr<Worker> &wakeSink()
    {
        // Only the watcher thread and its own hook callbacks touch this, and
        // both run on the same pumping thread, so no lock guards the pointer.
        static std::shared_ptr<Worker> sink;
        return sink;
    }

    static void CALLBACK winEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
                                          LONG, DWORD, DWORD)
    {
        const auto sink = wakeSink();
        if (!sink)
            return;
        // EchoMusic shell_event: the foreground hand-off and capture end wake
        // unconditionally; the create/rename/cloak families wake only when the
        // shell itself or the foreground window is involved (a screenshot
        // overlay can hide without a foreground switch and the fullscreen
        // state has to be re-checked anyway). Without this filter a third-
        // party cloak churning - an input site uncloaking a few times a
        // second - pins the probe permanently busy.
        if (event == EVENT_SYSTEM_FOREGROUND) {
            winEventForeground = reinterpret_cast<qintptr>(hwnd);
            std::lock_guard<std::mutex> lock(sink->mutex);
            sink->generation.fetch_add(1, std::memory_order_relaxed);
            sink->wake.notify_all();
            return;
        }
        if (event == EVENT_SYSTEM_CAPTUREEND) {
            std::lock_guard<std::mutex> lock(sink->mutex);
            sink->generation.fetch_add(1, std::memory_order_relaxed);
            sink->wake.notify_all();
            return;
        }
        bool foregroundEnded = false;
        if (winEventForeground && winEventForeground.load() == reinterpret_cast<qintptr>(hwnd) &&
            (event == EVENT_OBJECT_HIDE || event == EVENT_OBJECT_DESTROY ||
             event == EVENT_OBJECT_CLOAKED || event == EVENT_OBJECT_UNCLOAKED))
            foregroundEnded = true;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        const HWND shell = FindWindowW(kShellClass, nullptr);
        DWORD shellPid = 0;
        if (shell)
            GetWindowThreadProcessId(shell, &shellPid);
        const bool shellChanged =
            shell &&
            (hwnd == shell || (shellPid && pid == shellPid && GetAncestor(hwnd, GA_ROOT) == shell));
        if (!shellChanged && !(idObject == OBJID_WINDOW && foregroundEnded))
            return;
        std::lock_guard<std::mutex> lock(sink->mutex);
        sink->generation.fetch_add(1, std::memory_order_relaxed);
        sink->wake.notify_all();
    }

    void runWatcher(const std::shared_ptr<Worker> &self)
    {
        wakeSink() = self;
        // EchoMusic seeds the foreground bookkeeping before arming the hooks.
        winEventForeground = reinterpret_cast<qintptr>(GetForegroundWindow());
        // EchoMusic's hook set: any window created or renamed, the foreground
        // hand-off, capture end and cloak state. Their foreground hook skips
        // nothing so a return to the app itself also schedules a probe.
        const DWORD selfFlags = WINEVENT_OUTOFCONTEXT;
        const DWORD foreignFlags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        const HWINEVENTHOOK hooks[] = {
            SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                            &Worker::winEventHookProc, 0, 0, selfFlags),
            SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_NAMECHANGE, nullptr,
                            &Worker::winEventHookProc, 0, 0, foreignFlags),
            SetWinEventHook(EVENT_SYSTEM_CAPTUREEND, EVENT_SYSTEM_CAPTUREEND, nullptr,
                            &Worker::winEventHookProc, 0, 0, foreignFlags),
            SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr,
                            &Worker::winEventHookProc, 0, 0, foreignFlags),
        };
        MSG message{};
        while (!watcherQuit.load(std::memory_order_acquire)) {
            const int got = GetMessageW(&message, nullptr, 0, 0);
            if (got <= 0)
                break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        for (const HWINEVENTHOOK hook : hooks)
            if (hook)
                UnhookWinEvent(hook);
        wakeSink().reset();
    }

    void run(WindowsTaskbarLyric *owner)
    {
        qCInfo(taskbarLyricLog).nospace() << "probe thread up";
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        qCInfo(taskbarLyricLog).nospace() << "COM initialized";
        IUIAutomation *automation = nullptr;
        const bool automationOk =
            SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IUIAutomation, reinterpret_cast<void **>(&automation)));
        qCInfo(taskbarLyricLog).nospace() << "UIA client up ok=" << automationOk;
        if (!automationOk || !automation) {
            qCWarning(taskbarLyricLog) << "UIA initialization failed; the probe thread stops";
            TaskbarLayout lost;
            lost.available = false;
            publish(owner, lost);
            CoUninitialize();
            return;
        }
        StructureChangeHandler handler(generation, &mutex, &wake);
        IUIAutomationElement *subscribed = nullptr;
        bool subscribedHandler = false;
        HWND lastShell = nullptr;
        DWORD cooldown = 0;
        quint64 seenGeneration = 0;
        // The taskbar is still filling its buttons right after the shell
        // starts; the first probe can see a half built taskbar and report a
        // gap that does not exist a moment later. Publish only when two
        // consecutive probes agree (PeriTray's reliable gate, same idea).
        QString pendingKey;
        QString confirmedKey;
        int pendingProbes = 0;
        // WinEvent churn re-probes at the desktop's own cadence; identical
        // layouts must not re-run the controller's SetWindowPos per burst
        // (EchoMusic dedupes on last_layout != value). The identity carries
        // scale, theme and the row height too: a Win10 "small taskbar buttons"
        // change moves only the height, and the strip's height is derived from
        // it, so a horizontal-only identity would never republish.
        QString publishedId;
        auto publishChanged = [&publishedId, this, owner](const TaskbarLayout &value) {
            const QString id = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                                   .arg(value.left.x())
                                   .arg(value.left.width())
                                   .arg(value.left.height())
                                   .arg(value.right.width())
                                   .arg(value.right.height())
                                   .arg(value.scaleFactor, 0, 'g', 9)
                                   .arg(value.dark)
                                   .arg(value.available && value.reliable);
            if (id == publishedId)
                return;
            publishedId = id;
            publish(owner, value);
        };
        for (;;) {
            {
                std::unique_lock lock(mutex);
                if (quit)
                    break;
                // Probe immediately on start, then a short settle cadence
                // until the layout is confirmed; afterwards wait for change
                // events, cooldowns and the idle safety poll. A layout that
                // is still waiting for its confirming second probe keeps the
                // short cadence too (kIdlePollMs here would make icon
                // changes apply only after the next 10 s tick).
                const DWORD wait = lastShell == nullptr ? 0
                                   : cooldown           ? cooldown
                                   : confirmedKey.isEmpty() || pendingKey != confirmedKey
                                       ? 400
                                       : kIdlePollMs;
                wake.wait_for(lock, std::chrono::milliseconds(wait),
                              [&] { return quit || generation != seenGeneration; });
                if (quit)
                    break;
            }
            seenGeneration = generation;
            // Startup stage markers: without them a worker that never reaches
            // the probe heartbeat is indistinguishable from a silent UIA stall.
            // Bounded to the first three iterations.
            {
                static int stageSeen = 0;
                stageSeen++;
                if (stageSeen <= 3)
                    qCInfo(taskbarLyricLog).nospace() << "probe stage iter=" << stageSeen;
            }
            // Coalesce bursts: one probe covers every change within the window.
            Sleep(kDebounceMs);
            HWND shell = findShellTrayWnd();
            if (shell != lastShell) {
                // (Re)subscribe structure changes on the current taskbar. Both
                // add and remove live on IUIAutomation and address the element.
                if (subscribed) {
                    if (subscribedHandler)
                        automation->RemoveStructureChangedEventHandler(subscribed, &handler);
                    subscribed->Release();
                    subscribed = nullptr;
                    subscribedHandler = false;
                }
                if (shell) {
                    if (SUCCEEDED(automation->ElementFromHandle(shell, &subscribed)) &&
                        subscribed) {
                        subscribedHandler = SUCCEEDED(automation->AddStructureChangedEventHandler(
                            subscribed, TreeScope_Subtree, nullptr, &handler));
                    }
                    if (!subscribedHandler && subscribed) {
                        subscribed->Release();
                        subscribed = nullptr;
                    }
                }
                lastShell = shell;
            }
            TaskbarLayout layout;
            // Self-healing: Qt may silently recreate the native window (style/
            // format churn), which drops the mount. Detect and re-mount.
            if (child && (!IsWindow(child) || !IsChild(shell, child))) {
                qCWarning(taskbarLyricLog) << "挂载丢失（窗口重建或被解挂），重新挂载";
                QMetaObject::invokeMethod(
                    owner, [owner] { owner->remount(); }, Qt::QueuedConnection);
            }
            QString reason;
            const bool usable = shell && computeLayout(automation, shell, layout, &reason);
            if (!shell)
                reason = QStringLiteral("Shell_TrayWnd not found");
            // Bounded heartbeat: every probe result is visible, but a steady
            // state logs at most once per 12 s. Without this the strip could
            // silently stay hidden (a 0x0 layout with no why at all).
            static qint64 lastProbeLogMs = 0;
            static QString lastProbeKind;
            const QString kind = usable ? QStringLiteral("usable left=%1 right=%2")
                                              .arg(layout.left.width())
                                              .arg(layout.right.width())
                                        : QStringLiteral("unusable (%1)").arg(reason);
            if (kind != lastProbeKind || qint64(GetTickCount64()) - lastProbeLogMs > 12000) {
                lastProbeKind = kind;
                lastProbeLogMs = qint64(GetTickCount64());
                qCInfo(taskbarLyricLog).nospace() << "probe " << kind;
            }
            if (!usable) {
                cooldown = kRearrangeCooldownMs; // EchoMusic re-arrange cooldown
                publishChanged(layout);
                continue;
            }
            cooldown = 0;
            // Same three spans as the publish identity, plus the row heights:
            // a height-only change still has to settle before it is published.
            const auto key = QString("%1|%2|%3|%4|%5")
                                 .arg(layout.left.x())
                                 .arg(layout.left.width())
                                 .arg(layout.left.height())
                                 .arg(layout.right.width())
                                 .arg(layout.right.height());
            if (key == confirmedKey) {
                // Keep the settling state aligned: the idle gate above keys
                // off pendingKey == confirmedKey.
                pendingKey = key;
                publishChanged(layout);
                continue;
            }
            if (key == pendingKey) {
                // Two consecutive probes agree: the taskbar has settled.
                confirmedKey = key;
                pendingProbes = 0;
                publishChanged(layout);
                continue;
            }
            pendingKey = key;
            if (++pendingProbes >= 8) {
                // Never leave the strip without a layout for long.
                confirmedKey = key;
                pendingProbes = 0;
                publishChanged(layout);
            }
        }
        if (subscribed) {
            if (subscribedHandler)
                automation->RemoveStructureChangedEventHandler(subscribed, &handler);
            subscribed->Release();
        }
        automation->Release();
        CoUninitialize();
    }

    void publish(WindowsTaskbarLyric *owner, const TaskbarLayout &layout)
    {
        if (!alive || !*alive)
            return;
        QMetaObject::invokeMethod(
            owner,
            [owner, layout, alive = this->alive] {
                if (!alive || !*alive)
                    return;
                emit owner->layoutChanged(layout);
            },
            Qt::QueuedConnection);
    }
#endif
};

WindowsTaskbarLyric::WindowsTaskbarLyric(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<EchoMusic::TaskbarLayout>("EchoMusic::TaskbarLayout");
}

WindowsTaskbarLyric::~WindowsTaskbarLyric()
{
#ifdef Q_OS_WIN
    if (m_worker && m_worker->alive)
        *m_worker->alive = false;
#endif
    detach();
    stopWorker();
}

bool WindowsTaskbarLyric::attach(QWindow *window)
{
    if (m_window == window && m_mounted)
        return true;
    if (!window)
        return false;
    // The probe worker outlives re-mounts: joining it can block the UI thread
    // while the worker sits in a cross process UIA call, which freezes the app.
    ensureWorker();
    return mountWindow(window);
}

bool WindowsTaskbarLyric::mountWindow(QWindow *window)
{
#ifdef Q_OS_WIN
    if (!window)
        return false;
    // Never touch the Qt parent/flags here: QWindow::setParent recreates the
    // platform window, which would leave the mount pointing at a stale handle
    // and drop a fresh top-level window at the default position. The strip is
    // created without a parent (Component.createObject(null)), so nothing
    // re-parents it behind our back.
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const HWND shell = findShellTrayWnd();
    if (!hwnd || !shell) {
        qCWarning(taskbarLyricLog) << "Shell_TrayWnd 不可用，挂载失败";
        return false;
    }
    // PeriTray: restyle BEFORE SetParent; the reversed order fails err=87.
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    // Drop the caption as well: a child window that keeps WS_CAPTION draws a
    // title bar inside the host window.
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~(WS_POPUP | WS_CAPTION)) | WS_CHILD);
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    SetLastError(0);
    const HWND previous = SetParent(hwnd, shell);
    const DWORD parentErr = GetLastError();
    const bool ok = (previous != nullptr || parentErr == 0) && GetParent(hwnd) == shell;
    if (!ok) {
        qCWarning(taskbarLyricLog) << "SetParent 失败 err=" << parentErr << "prev=" << previous;
        return false;
    }
    m_window = window;
    m_mounted = reinterpret_cast<quintptr>(hwnd);
    // The right click on the taskbar area is claimed by the shell's XAML
    // island, so the context menu has to be taken in the strip's own window
    // procedure. Only the right button is intercepted; everything else stays
    // with Qt.
    if (!GetWindowSubclass(hwnd, &StripMouseProc, reinterpret_cast<UINT_PTR>(this), nullptr)) {
        SetWindowSubclass(hwnd, &StripMouseProc, reinterpret_cast<UINT_PTR>(this), 0);
    }
    if (m_worker)
        m_worker->child = hwnd;
    qCInfo(taskbarLyricLog) << "挂载成功 hwnd=" << hwnd << "shell=" << shell;
    return true;
#else
    Q_UNUSED(window);
    return false;
#endif
}

bool WindowsTaskbarLyric::ensureMounted(QWindow *window)
{
#ifdef Q_OS_WIN
    if (!window)
        return false;
    // Mounts can win the race both ways: the frame ready path goes through
    // attach() (which spawns the probe worker), while this early path is what
    // actually mounts when the scene graph is warm. The probe worker has to
    // exist no matter which path mounted, or the layout is never published
    // and the strip stays hidden forever (exactly the intermittent
    // disappearance: a slow first frame meant an early mount, no worker).
    ensureWorker();
    const HWND current = reinterpret_cast<HWND>(window->winId());
    if (m_mounted == reinterpret_cast<quintptr>(current) && GetParent(current))
        return true;
    if (!current)
        return false;
    // The toolkit recreated the platform window: take the new handle. The probe
    // worker keeps running, so this stays cheap and never blocks the UI.
    return mountWindow(window);
#else
    Q_UNUSED(window);
    return false;
#endif
}

void WindowsTaskbarLyric::refreshExposure()
{
#ifdef Q_OS_WIN
    const HWND hwnd = m_mounted ? reinterpret_cast<HWND>(m_mounted) : nullptr;
    if (!hwnd || !IsWindow(hwnd))
        return;
    // Invalidate the child window so the toolkit sees a paint request and
    // restores the exposed state that SetParent cleared.
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
#endif
}

void WindowsTaskbarLyric::remount()
{
#ifdef Q_OS_WIN
    if (!m_window)
        return;
    qCInfo(taskbarLyricLog) << "remount";
    mountWindow(m_window);
#endif
}

void WindowsTaskbarLyric::setStripOffset(int x)
{
#ifdef Q_OS_WIN
    const HWND hwnd = m_mounted ? reinterpret_cast<HWND>(m_mounted) : nullptr;
    if (!hwnd || !IsWindow(hwnd))
        return;
    RECT client{};
    if (!GetClientRect(GetParent(hwnd), &client))
        return;
    m_clientWidth = client.right - client.left;
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect))
        return;
    const int stripWidth = rect.right - rect.left;
    const int maxOffset = qMax(0, m_clientWidth - stripWidth);
    const int target = qBound(0, x, maxOffset);
    m_stripOffset = target;
    moveStrip(hwnd, target);
#endif
}

void WindowsTaskbarLyric::detach()
{
#ifdef Q_OS_WIN
    // The probe worker stays alive: joining it here can block the UI thread for
    // as long as the shell keeps the UIA call busy, which freezes the app.
    // Only unmount the window; the worker idles until the session dies.
    unmountWindow();
    m_window = nullptr;
#endif
}

void WindowsTaskbarLyric::ensureWorker()
{
#ifdef Q_OS_WIN
    if (m_worker && m_worker->thread.joinable())
        return;
    m_worker = std::make_shared<Worker>();
    auto worker = m_worker;
    worker->thread = std::thread([worker, this] { worker->run(this); });
    worker->watcherThread = std::thread([worker] { worker->runWatcher(worker); });
#endif
}

void WindowsTaskbarLyric::stopWorker()
{
#ifdef Q_OS_WIN
    if (!m_worker)
        return;
    // The watcher must leave before the worker struct itself: its hook
    // callbacks dereference the sink. WM_QUIT breaks the GetMessageW pump.
    m_worker->watcherQuit = true;
    if (m_worker->watcherThread.joinable()) {
        const auto watcherHandle =
            reinterpret_cast<HANDLE>(m_worker->watcherThread.native_handle());
        PostThreadMessageW(GetThreadId(watcherHandle), WM_QUIT, 0, 0);
        if (WaitForSingleObject(watcherHandle, 2000) == WAIT_OBJECT_0)
            m_worker->watcherThread.join();
        else
            m_worker->watcherThread.detach();
    }
    m_worker->quit = true;
    {
        std::lock_guard lock(m_worker->mutex);
        m_worker->wake.notify_all();
    }
    if (m_worker->thread.joinable()) {
        // Wait with a timeout: the worker may sit in a cross process UIA call
        // that never returns on its own. Letting it finish on its own is safe
        // because the shared state keeps it valid.
        const auto handle = reinterpret_cast<HANDLE>(m_worker->thread.native_handle());
        if (WaitForSingleObject(handle, 2000) == WAIT_OBJECT_0)
            m_worker->thread.join();
        else
            m_worker->thread.detach();
    }
    m_worker.reset();
#endif
}

void WindowsTaskbarLyric::unmountWindow()
{
#ifdef Q_OS_WIN
    const HWND hwnd = m_mounted ? reinterpret_cast<HWND>(m_mounted) : nullptr;
    m_mounted = 0;
    if (!hwnd || !IsWindow(hwnd))
        return;
    // EchoMusic: hide first - SetParent(NULL) instantly turns the window
    // into a desktop top-level, flashing outside the taskbar.
    ShowWindow(hwnd, SW_HIDE);
    RemoveWindowSubclass(hwnd, &StripMouseProc, reinterpret_cast<UINT_PTR>(this));
    SetParent(hwnd, nullptr);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~WS_CHILD) | WS_POPUP);
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW));
#endif
}
} // namespace EchoMusic
