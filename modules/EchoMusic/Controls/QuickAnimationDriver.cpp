#include "QuickAnimationDriver.h"
#include <QAbstractEventDispatcher>
#include <QAnimationDriver>
#include <QLoggingCategory>
#include <QPointer>
#include <QQuickWindow>
#include <QThread>
#include <QtCore/private/qabstractanimation_p.h>
#include <QtQuick/private/qsgrenderloop_p.h>

namespace EchoMusic
{
Q_LOGGING_CATEGORY(quickAnimationLog, "echo.qt.animation")
namespace
{
class AnimationDriverGuard final : public QObject
{
public:
    explicit AnimationDriverGuard(QAnimationDriver &driver)
        : QObject(&driver), m_driver(driver), m_timer(QUnifiedTimer::instance())
    {
        setObjectName("echoQuickAnimationDriverGuard");
        connect(&driver, &QAnimationDriver::started, this, [this] { schedule(); });
        schedule();
    }

private:
    void schedule()
    {
        if (m_idleCheck)
            return;
        auto *dispatcher = QAbstractEventDispatcher::instance(thread());
        if (!dispatcher)
            return;
        // Drain Qt's posted start/stop deliveries in the native loop first.
        // A queued meta-call or zero timer can overtake a pending Qt stop.
        // Remove this observer on its first delivery; never keep it per frame.
        m_idleCheck = connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, this, [this] {
            disconnect(m_idleCheck);
            m_idleCheck = {};
            if (!m_timer || !m_driver.isRunning() || m_timer->runningAnimationCount() != 0)
                return;
            // Qt 6.12 localRestart() can start an empty driver. Qt's own slot
            // checks registered queues, including pending/paused timers; no
            // animation, clock, frame rate or window rendering policy changes.
            QMetaObject::invokeMethod(m_timer, "stopTimer", Qt::DirectConnection);
            if (!m_driver.isRunning())
                qCDebug(quickAnimationLog) << "Stopped animation driver with no registered timers";
        });
    }

    QAnimationDriver &m_driver;
    QPointer<QUnifiedTimer> m_timer;
    QMetaObject::Connection m_idleCheck;
};
} // namespace

void installQuickAnimationDriverGuard(QQuickWindow &window)
{
    if (window.thread() != QThread::currentThread())
        return;
    auto *loop = QSGRenderLoop::instance();
    auto *driver = loop ? loop->animationDriver() : nullptr;
    if (driver &&
        !driver->findChild<QObject *>("echoQuickAnimationDriverGuard", Qt::FindDirectChildrenOnly))
        new AnimationDriverGuard(*driver);
}
} // namespace EchoMusic
