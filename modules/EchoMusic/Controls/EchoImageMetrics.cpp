#include "EchoImageMetrics.h"
#include <QEvent>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QtMath>
#include <cmath>

namespace EchoMusic
{
EchoImageMetrics::~EchoImageMetrics()
{
    if (m_window)
        m_window->removeEventFilter(this);
}
void EchoImageMetrics::setItem(QQuickItem *item)
{
    if (m_item == item)
        return;
    m_item = item;
    m_pixels = {};
    m_decodePixels = {};
    rebuild();
    emit itemChanged();
    emit metricsChanged();
}
void EchoImageMetrics::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (!active) {
        m_pixels = {};
        m_decodePixels = {};
    }
    rebuild();
    emit activeChanged();
    emit metricsChanged();
}
void EchoImageMetrics::setSource(const QUrl &source)
{
    if (m_source == source)
        return;
    m_source = source;
    m_pixels = {};
    m_decodePixels = {};
    updateSize();
    emit sourceChanged();
    emit metricsChanged();
}
void EchoImageMetrics::setMinimumSize(const QSizeF &size)
{
    if (m_minimumSize == size)
        return;
    m_minimumSize = size;
    updateSize();
    emit minimumSizeChanged();
}
QUrl EchoImageMetrics::resolvedSource() const
{
    if (!m_active)
        return {};
    auto url = m_source;
    auto path = url.path();
    const int needed = qMax(m_decodePixels.width(), m_decodePixels.height());
    const int requested = needed <= 240 ? 240 : needed <= 400 ? 400 : needed <= 800 ? 800 : 1600;
    // A size placeholder explicitly grants resizing. For already-normalized
    // URLs only known Kugou artwork paths are eligible; never rewrite arbitrary
    // hosts, local files, query parameters, signatures or date/hash segments.
    if ((url.scheme() == "https" || url.scheme() == "http") &&
        path.contains(QStringLiteral("{size}"))) {
        path.replace(QStringLiteral("{size}"), QString::number(requested));
    } else if (!url.hasQuery() && (url.scheme() == "https" || url.scheme() == "http")) {
        const auto host = url.host().toLower();
        if (host == "imge.kugou.com" || host == "imgessl.kugou.com" || host == "c1.kgimg.com" ||
            host == "imge.kgimg.com") {
            static const QRegularExpression sized(
                QStringLiteral("^/(?:stdmusic|custom)/(\\d+)(?=/)"));
            const auto match = sized.match(path);
            if (match.hasMatch() && match.captured(1).toInt() < needed)
                path.replace(match.capturedStart(1), match.capturedLength(1),
                             QString::number(requested));
        }
    }
    // Avoid re-encoding an unrelated URL's path (e.g. an encoded slash).
    if (path != url.path())
        url.setPath(path);
    return url;
}
void EchoImageMetrics::rebuild()
{
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = nullptr;
    if (m_active && m_item) {
        m_connections << connect(m_item, &QQuickItem::widthChanged, this,
                                 &EchoImageMetrics::updateSize)
                      << connect(m_item, &QQuickItem::heightChanged, this,
                                 &EchoImageMetrics::updateSize);
        for (auto *ancestor = m_item.data(); ancestor; ancestor = ancestor->parentItem()) {
            m_connections
                << connect(ancestor, &QQuickItem::scaleChanged, this, &EchoImageMetrics::updateSize)
                << connect(ancestor, &QQuickItem::rotationChanged, this,
                           &EchoImageMetrics::updateSize)
                << connect(ancestor, &QQuickItem::parentChanged, this, &EchoImageMetrics::rebuild)
                << connect(ancestor, &QQuickItem::windowChanged, this, &EchoImageMetrics::rebuild)
                << connect(ancestor, &QObject::destroyed, this, &EchoImageMetrics::rebuild);
        }
        m_window = m_item->window();
        if (m_window) {
            m_window->installEventFilter(this);
            m_connections << connect(m_window, &QWindow::screenChanged, this,
                                     &EchoImageMetrics::rebuild);
        }
    }
    updateSize();
}
bool EchoImageMetrics::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_window && event->type() == QEvent::DevicePixelRatioChange)
        updateSize();
    return false;
}
void EchoImageMetrics::updateSize()
{
    if (!m_active || !m_item)
        return;
    const auto origin = m_item->mapToScene({0, 0});
    const auto x = m_item->mapToScene({1, 0}) - origin;
    const auto y = m_item->mapToScene({0, 1}) - origin;
    const auto dpr = m_window ? m_window->effectiveDevicePixelRatio() : 1.;
    const auto bucket = [](qreal pixels) {
        return qIsFinite(pixels) ? qBound(32, qCeil(qMin(pixels, 1600.) / 32) * 32, 1600) : 32;
    };
    const QSize display(bucket(m_item->width() * std::hypot(x.x(), x.y()) * dpr),
                        bucket(m_item->height() * std::hypot(y.x(), y.y()) * dpr));
    const QSize requested(
        bucket(qMax(m_item->width(), m_minimumSize.width()) * std::hypot(x.x(), x.y()) * dpr),
        bucket(qMax(m_item->height(), m_minimumSize.height()) * std::hypot(y.x(), y.y()) * dpr));
    const auto size = m_pixels.expandedTo(display);
    const auto decoded = m_decodePixels.expandedTo(requested);
    if (size == m_pixels && decoded == m_decodePixels)
        return;
    m_pixels = size;
    m_decodePixels = decoded;
    emit metricsChanged();
}
} // namespace EchoMusic
