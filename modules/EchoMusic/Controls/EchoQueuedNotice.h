#pragma once
#include <QMetaObject>
#include <QObject>
#include <utility>
namespace EchoMusic
{
// Coalesced queued notification: the first call schedules exactly one
// Qt::QueuedConnection turn; calls arriving before it runs only raise the
// flag. `pending` must be a member of `self` - the callable is bound to
// self's lifetime, so the reference cannot outlive it. The flag resets
// before work() runs, so a handler that re-triggers during work() can
// schedule the next turn instead of being swallowed.
template <typename F> void queueCoalescedNotice(QObject *self, bool &pending, F &&work)
{
    if (std::exchange(pending, true))
        return;
    QMetaObject::invokeMethod(
        self,
        [self, &pending, work = std::forward<F>(work)]() mutable {
            pending = false;
            work();
        },
        Qt::QueuedConnection);
}
} // namespace EchoMusic
