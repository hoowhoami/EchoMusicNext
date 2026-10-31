#pragma once
// Cross-boundary gate between the native file-dialog wrappers
// (modules/EchoMusic Controls + PluginSdk) and the Windows taskbar lyric
// probe (services/platform/WindowsTaskbarLyric).
//
// Issue #1: while any UIA event subscription is registered in this process,
// a provider-side event raise is delivered synchronously to the subscribed
// client through its apartment. The native common dialog fires MSAA events
// (bridged to UIA by oleacc) during IFileDialog::Show initialization, before
// the dialog thread pumps a single message. The original probe thread slept
// on a condition variable and never dispatched, so that delivery never
// completed: the dialog window was created but never presented, with every
// thread idle. Bisection proved the standing subscription is the only
// necessary factor (the probe, its hooks and the mounted strip without it
// never hang).
//
// Two independent defenses keep dialogs alive while push layout updates stay:
// 1. The probe thread is a message-pumping STA (WindowsTaskbarLyric.cpp), so
//    its subscription's deliveries are dispatched within one 100 ms pump
//    slice even while a dialog initializes.
// 2. This gate: the probe drops its StructureChanged subscription for the
//    lifetime of a native dialog session and restores it afterwards, so the
//    dialog's raises have no client delivery to depend on at all. Taskbar
//    layout freshness outside dialog sessions is unchanged (push via the
//    subscription); during a session the WinEvent hooks and the idle poll
//    cover updates - a dialog being open is exactly when the user does not
//    need taskbar re-layouts.
//
// Protocol (all state atomic, no locks; the probe thread is the only writer
// of `drained`, the dialog threads are the only writers of `users`):
//   1. A picker opens a Session: users 0->1 and bumps epoch.
//   2. The probe's pump wait wakes on the epoch change, unsubscribes and
//      publishes drained=true. The picker waits for drained with a bounded
//      timeout and proceeds anyway on expiry so a stalled probe can never
//      block a dialog (defense 1 keeps the dialog safe in that case).
//   3. The Session dies with the dialog: users 1->0, epoch bump; the probe
//      re-subscribes within one pump iteration (no 10 s idle-poll delay).
// With no probe running (taskbar lyric off) drained is trivially true.

#include <atomic>
#include <chrono>
#include <thread>

namespace EchoMusic::UiaEventGate
{
/// Number of open native-dialog sessions. 0 means the probe must subscribe.
inline std::atomic<int> &users()
{
    static std::atomic<int> value{0};
    return value;
}

/// Bumped on every users() change; the probe's pump wait wakes on it.
inline std::atomic<unsigned long long> &epoch()
{
    static std::atomic<unsigned long long> value{0};
    return value;
}

/// Probe-side publication: true while the probe holds no subscription.
/// Starts true (nothing subscribed until the probe's first iteration).
inline std::atomic<bool> &drained()
{
    static std::atomic<bool> value{true};
    return value;
}

/// Set by the probe thread for its whole lifetime.
inline std::atomic<bool> &ownerAlive()
{
    static std::atomic<bool> value{false};
    return value;
}

/// Probe side: publish the subscription state the picker side waits on.
inline void publishDrained(bool value)
{
    drained().store(value, std::memory_order_release);
}

/// Wait until the probe has confirmed the suspension. Returns false on
/// timeout (caller proceeds; a hung probe must never block a dialog).
inline bool waitDrained(int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!ownerAlive().load(std::memory_order_acquire) ||
            drained().load(std::memory_order_acquire))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return drained().load(std::memory_order_acquire);
}

/// RAII ticket for one native-dialog session. Hold it from just before the
/// native dialog may initialize until the dialog is fully closed.
class Session
{
public:
    Session()
    {
        users().fetch_add(1, std::memory_order_acq_rel);
        epoch().fetch_add(1, std::memory_order_release);
    }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    ~Session()
    {
        users().fetch_sub(1, std::memory_order_acq_rel);
        epoch().fetch_add(1, std::memory_order_release);
    }
};
} // namespace EchoMusic::UiaEventGate
