#include "EchoImageMetrics.h"
#include <QEvent>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QHash>
#include <QSharedPointer>
#include <QtMath>
#include <cmath>

namespace EchoMusic
{
namespace
{
// Weak registries do not keep hidden pages or windows alive. Subscribers own a
// shared observer; the last subscriber retires it after any in-flight signal.
template <typename T> struct ObserverEntry
{
    T *object;
    QWeakPointer<T> weak;
};
template <typename T> auto &observers()
{
    static QHash<QObject *, ObserverEntry<T>> entries;
    return entries;
}
template <typename T, typename Item> QSharedPointer<T> observe(Item *item)
{
    if (!item)
        return {};
    auto &entries = observers<T>();
    if (auto existing = entries.value(item).weak.toStrongRef())
        return existing;
    QSharedPointer<T> result(new T(item), [](T *observer) {
        observer->retire();
        observer->deleteLater();
    });
    entries.insert(item, {result.data(), result.toWeakRef()});
    return result;
}
template <typename T> void forget(QObject *key, T *object)
{
    auto &entries = observers<T>();
    const auto it = entries.find(key);
    if (it != entries.end() && it->object == object)
        entries.erase(it);
}
class WindowDensityObserver final : public QObject
{
    Q_OBJECT
public:
    explicit WindowDensityObserver(QQuickWindow *window) : m_key(window), m_window(window)
    {
        window->installEventFilter(this);
        connect(window, &QWindow::screenChanged, this, &WindowDensityObserver::changed);
    }
    ~WindowDensityObserver() override
    {
        retire();
    }
    void retire()
    {
        if (m_window) {
            m_window->removeEventFilter(this);
            disconnect(m_window, nullptr, this, nullptr);
        }
        forget(m_key, this);
    }
signals:
    void changed();

private:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::DevicePixelRatioChange)
            emit changed();
        return false;
    }
    QObject *m_key;
    QPointer<QQuickWindow> m_window;
};
} // namespace

class EchoImageTransformObserver final : public QObject
{
    Q_OBJECT
public:
    explicit EchoImageTransformObserver(QQuickItem *item) : m_key(item), m_item(item)
    {
        connect(item, &QQuickItem::scaleChanged, this, &EchoImageTransformObserver::changed);
        connect(item, &QQuickItem::rotationChanged, this, &EchoImageTransformObserver::changed);
        connect(item, &QQuickItem::parentChanged, this, &EchoImageTransformObserver::reparent);
        connect(item, &QQuickItem::windowChanged, this, &EchoImageTransformObserver::reparent);
        connect(item, &QObject::destroyed, this, [this] {
            m_parent.reset();
            m_density.reset();
            emit changed();
        });
        reparent();
    }
    ~EchoImageTransformObserver() override
    {
        retire();
    }
    void retire()
    {
        if (m_item)
            disconnect(m_item, nullptr, this, nullptr);
        disconnect(m_parentConnection);
        disconnect(m_densityConnection);
        m_parent.reset();
        m_density.reset();
        forget(m_key, this);
    }
signals:
    void changed();

private:
    void reparent()
    {
        disconnect(m_parentConnection);
        disconnect(m_densityConnection);
        m_parent = observe<EchoImageTransformObserver>(m_item ? m_item->parentItem() : nullptr);
        // Only the visual root observes window density. Descendants receive one
        // propagated notification instead of N independent window subscriptions.
        m_density =
            observe<WindowDensityObserver>(m_item && !m_parent ? m_item->window() : nullptr);
        if (m_parent)
            m_parentConnection = connect(m_parent.data(), &EchoImageTransformObserver::changed,
                                         this, &EchoImageTransformObserver::changed);
        if (m_density)
            m_densityConnection = connect(m_density.data(), &WindowDensityObserver::changed, this,
                                          &EchoImageTransformObserver::changed);
        emit changed();
    }
    QObject *m_key;
    QPointer<QQuickItem> m_item;
    QSharedPointer<EchoImageTransformObserver> m_parent;
    QSharedPointer<WindowDensityObserver> m_density;
    QMetaObject::Connection m_parentConnection, m_densityConnection;
};

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
        m_observer = observe<EchoImageTransformObserver>(m_item.data());
        m_connections
            << connect(m_item, &QQuickItem::widthChanged, this, &EchoImageMetrics::updateSize)
            << connect(m_item, &QQuickItem::heightChanged, this, &EchoImageMetrics::updateSize)
            << connect(m_observer.data(), &EchoImageTransformObserver::changed, this, [this] {
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

#include "EchoImageMetrics.moc"
