#include "EchoPixelSnap.h"
#include "EchoItemObserver.h"
#include "EchoQueuedNotice.h"
#include <QDebug>
#include <QQuickWindow>
#include <QtMath>
#include <cmath>
#include <utility>

namespace EchoMusic
{
namespace
{
// A/B and field diagnostics: ECHO_PIXEL_SNAP=0 parks every compensation at
// zero; ECHO_PIXEL_SNAP_TRACE=<substring> logs scene/density changes for items
// whose object-name chain matches the substring. Both are read once; attached
// objects are created per item and must not race on a shared cache.
const QString &traceFilterValue()
{
    static const QString value = qEnvironmentVariable("ECHO_PIXEL_SNAP_TRACE");
    return value;
}
// A collapsing transition can drive the ancestor scale through (or to) zero.
// Treat that as "no scaling" so the inner surface does not derive an unbounded
// device-pixel size, and keep the compression path invisible instead.
qreal density(qreal dpr, qreal transformLength)
{
    return qIsFinite(transformLength) && transformLength > 0.001 ? dpr * transformLength : dpr;
}
} // namespace

bool EchoPixelSnapAttached::snapEnabled()
{
    static const bool enabled = qEnvironmentVariable("ECHO_PIXEL_SNAP") != QLatin1String("0");
    return enabled;
}

EchoPixelSnapAttached::EchoPixelSnapAttached(QObject *parent)
    : QObject(parent), m_item(qobject_cast<QQuickItem *>(parent))
{
    if (!snapEnabled() || !m_item)
        return;
    m_observer = observeItem(m_item.data());
    connect(m_observer.data(), &EchoItemObserver::sceneChanged, this,
            &EchoPixelSnapAttached::refresh);
    refresh();
}

void EchoPixelSnapAttached::refresh()
{
    if (!m_item)
        return;
    const QPointF scene = m_item->mapToScene(QPointF(0, 0));
    const QPointF unitX = m_item->mapToScene(QPointF(1, 0)) - scene;
    const QPointF unitY = m_item->mapToScene(QPointF(0, 1)) - scene;
    const QQuickWindow *window = m_item->window();
    const qreal dpr = window ? window->effectiveDevicePixelRatio() : 1.0;
    const qreal densityX = density(dpr, std::hypot(unitX.x(), unitX.y()));
    const qreal densityY = density(dpr, std::hypot(unitY.x(), unitY.y()));
    const bool positionChanged = scene != m_scenePosition;
    const bool ratioChanged = !qFuzzyCompare(dpr + 1.0, m_devicePixelRatio + 1.0);
    const bool densityValueChanged = !qFuzzyCompare(densityX + 1.0, m_densityX + 1.0) ||
                                     !qFuzzyCompare(densityY + 1.0, m_densityY + 1.0);
    if (!positionChanged && !ratioChanged && !densityValueChanged)
        return;
    const QString &filter = traceFilterValue();
    if (!filter.isEmpty()) {
        QString joined;
        for (const QQuickItem *item = m_item.data(); item; item = item->parentItem()) {
            const QString name = item->objectName();
            if (!name.isEmpty())
                joined = name + u'/' + joined;
        }
        if (joined.contains(filter)) {
            qWarning("pixel-snap: %s scene=(%.3f, %.3f) dpr=%.3f density=(%.3f, %.3f)",
                     qUtf8Printable(joined), scene.x(), scene.y(), dpr, densityX, densityY);
        }
    }
    m_scenePosition = scene;
    m_devicePixelRatio = dpr;
    m_densityX = densityX;
    m_densityY = densityY;
    // Values land immediately so any re-evaluation reads fresh data, but the
    // notifications take the event-loop road. A synchronous emit re-runs the
    // consumer snap bindings, which write child geometry, whose change
    // signals re-enter refresh() through the observer chain; under scene
    // churn (model resets, loader rebuilds) that round trip nests on one
    // stack until it overflows. Queuing coalesces a burst into one flush per
    // event-loop turn and turns the feedback into a bounded settle - snap
    // only affects sub-pixel alignment, so a one-turn lag is invisible.
    if (positionChanged)
        m_pendingPosition = true;
    if (ratioChanged)
        m_pendingRatio = true;
    if (densityValueChanged)
        m_pendingDensity = true;
    queueCoalescedNotice(this, m_flushPending, [this] { flushNotifications(); });
}

void EchoPixelSnapAttached::flushNotifications()
{
    if (std::exchange(m_pendingPosition, false))
        emit scenePositionChanged();
    if (std::exchange(m_pendingRatio, false))
        emit devicePixelRatioChanged();
    if (std::exchange(m_pendingDensity, false))
        emit densityChanged();
}
} // namespace EchoMusic
