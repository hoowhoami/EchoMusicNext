#include "EchoImageMetrics.h"
#include "EchoItemObserver.h"
#include <QQuickWindow>
#include <QRegularExpression>
#include <QtMath>
#include <cmath>

namespace EchoMusic
{
EchoImageMetrics::~EchoImageMetrics() = default;
void EchoImageMetrics::setItem(QQuickItem *item)
{
    if (m_item == item)
        return;
    m_item = item;
    m_pixels = {};
    m_decodePixels = {};
    m_deviceSize = {};
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
        m_deviceSize = {};
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
    // Inactive KeepAlive pages retain their model/view state, not listener capacity.
    m_connections = {};
    m_observer.reset();
    if (m_active && m_item) {
        m_observer = observeItem(m_item.data());
        m_connections << connect(m_item, &QQuickItem::widthChanged, this,
                                 &EchoImageMetrics::updateSize)
                      << connect(m_item, &QQuickItem::heightChanged, this,
                                 &EchoImageMetrics::updateSize)
                      << connect(m_observer.data(), &EchoItemObserver::changed, this, [this] {
                             if (m_item) {
                                 updateSize();
                             } else {
                                 m_pixels = {};
                                 m_decodePixels = {};
                                 rebuild();
                                 emit metricsChanged();
                             }
                         });
    }
    updateSize();
}
void EchoImageMetrics::updateSize()
{
    if (!m_active || !m_item)
        return;
    const auto origin = m_item->mapToScene({0, 0});
    const auto x = m_item->mapToScene({1, 0}) - origin;
    const auto y = m_item->mapToScene({0, 1}) - origin;
    const auto *window = m_item->window();
    const auto dpr = window ? window->effectiveDevicePixelRatio() : 1.;
    const auto bucket = [](qreal pixels) {
        return qIsFinite(pixels) ? qBound(32, qCeil(qMin(pixels, 1600.) / 32) * 32, 1600) : 32;
    };
    const QSize display(bucket(m_item->width() * std::hypot(x.x(), x.y()) * dpr),
                        bucket(m_item->height() * std::hypot(y.x(), y.y()) * dpr));
    const QSize requested(
        bucket(qMax(m_item->width(), m_minimumSize.width()) * std::hypot(x.x(), x.y()) * dpr),
        bucket(qMax(m_item->height(), m_minimumSize.height()) * std::hypot(y.x(), y.y()) * dpr));
    // Glyph rasters need the un-bucketed device-pixel size: the artwork bucket
    // floor (32px) makes a small icon rasterize at ~2x its display size, and
    // the bilinear downscale then aliases line-art strokes.
    const auto glyphAxis = [](qreal itemPixels, qreal transformLength, qreal ratio) {
        const qreal devicePixels =
            qIsFinite(itemPixels) ? itemPixels * transformLength * ratio : 0.;
        return devicePixels > 0 ? qCeil(devicePixels) : 1;
    };
    const QSize device(glyphAxis(m_item->width(), std::hypot(x.x(), x.y()), dpr),
                       glyphAxis(m_item->height(), std::hypot(y.x(), y.y()), dpr));
    const auto size = m_pixels.expandedTo(display);
    const auto decoded = m_decodePixels.expandedTo(requested);
    if (size == m_pixels && decoded == m_decodePixels && device == m_deviceSize)
        return;
    m_pixels = size;
    m_decodePixels = decoded;
    m_deviceSize = device;
    emit metricsChanged();
}
} // namespace EchoMusic
