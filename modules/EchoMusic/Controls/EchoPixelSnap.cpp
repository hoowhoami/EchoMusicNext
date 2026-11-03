#include "EchoPixelSnap.h"
#include <QQuickWindow>
#include <QtGui/qguiapplication.h>

namespace EchoMusic
{
namespace
{
// A/B and field diagnostics: ECHO_PIXEL_SNAP=0 parks every compensation at
// zero; ECHO_PIXEL_SNAP_TRACE=<substring> logs scene/dpr changes for items
// whose object-name chain matches the substring. Both are read once; attached
// objects are created per item and must not race on a shared cache.
const QString &traceFilterValue()
{
    static const QString value = qEnvironmentVariable("ECHO_PIXEL_SNAP_TRACE");
    return value;
}
} // namespace

bool EchoPixelSnapAttached::snapEnabled()
{
    static const bool enabled = qEnvironmentVariable("ECHO_PIXEL_SNAP") != QLatin1String("0");
    return enabled;
}

EchoPixelSnapAttached::EchoPixelSnapAttached(QObject *parent)
    : QObject(parent)
    , m_item(qobject_cast<QQuickItem *>(parent))
{
    watchChain();
}

void EchoPixelSnapAttached::watchChain()
{
    disconnectChain();
    if (!m_item)
        return;
    // A new window carries a new device pixel ratio; a new parent can
    // replace the whole ancestor chain that carries the item around.
    // Rebuilt on every pass so a re-parent keeps the window tracked.
    m_connections.push_back(
        connect(m_item, &QQuickItem::windowChanged, this, &EchoPixelSnapAttached::refresh));
    // Layout-driven movement always shows up as x/y (or scale) changes along
    // the ancestor chain, so watching those catches every carrier including
    // scroll views, anchored repositions and responsive reflows.
    for (QQuickItem *item = m_item.data(); item; item = item->parentItem()) {
        m_connections.push_back(
            connect(item, &QQuickItem::xChanged, this, &EchoPixelSnapAttached::refresh));
        m_connections.push_back(
            connect(item, &QQuickItem::yChanged, this, &EchoPixelSnapAttached::refresh));
        m_connections.push_back(
            connect(item, &QQuickItem::scaleChanged, this, &EchoPixelSnapAttached::refresh));
        m_connections.push_back(
            connect(item, &QQuickItem::parentChanged, this, &EchoPixelSnapAttached::watchChain));
    }
    refresh();
}

void EchoPixelSnapAttached::disconnectChain()
{
    for (const QMetaObject::Connection &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
}

void EchoPixelSnapAttached::refresh()
{
    if (!m_item)
        return;
    const QPointF scene = m_item->mapToScene(QPointF(0, 0));
    const qreal dpr = m_item->window() ? m_item->window()->devicePixelRatio() : 1.0;
    const bool changed = scene != m_scenePosition
        || !qFuzzyCompare(dpr, m_devicePixelRatio);
    if (!changed)
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
            qWarning("pixel-snap: %s scene=(%.3f, %.3f) dpr=%.3f",
                     qUtf8Printable(joined), scene.x(), scene.y(), dpr);
        }
    }
    m_scenePosition = scene;
    m_devicePixelRatio = dpr;
    if (!snapEnabled())
        return;
    emit scenePositionChanged();
    emit devicePixelRatioChanged();
}
} // namespace EchoMusic
