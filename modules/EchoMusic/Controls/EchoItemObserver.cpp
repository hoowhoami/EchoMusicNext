#include "EchoItemObserver.h"
#include <QEvent>
#include <QHash>
#include <QQuickWindow>
#include <QWeakPointer>

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
} // namespace

// Only the visual root observes window density. Descendants receive one
// propagated notification instead of N independent window subscriptions.
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

EchoItemObserver::EchoItemObserver(QQuickItem *item) : m_key(item), m_item(item)
{
    if (!item)
        return;
    connect(item, &QQuickItem::xChanged, this, &EchoItemObserver::sceneChanged);
    connect(item, &QQuickItem::yChanged, this, &EchoItemObserver::sceneChanged);
    connect(item, &QQuickItem::widthChanged, this, &EchoItemObserver::sceneChanged);
    connect(item, &QQuickItem::heightChanged, this, &EchoItemObserver::sceneChanged);
    connect(item, &QQuickItem::transformOriginChanged, this, &EchoItemObserver::sceneChanged);
    connect(item, &QQuickItem::scaleChanged, this, &EchoItemObserver::notifyTransform);
    connect(item, &QQuickItem::rotationChanged, this, &EchoItemObserver::notifyTransform);
    connect(item, &QQuickItem::parentChanged, this, &EchoItemObserver::reparent);
    connect(item, &QQuickItem::windowChanged, this, &EchoItemObserver::reparent);
    connect(item, &QObject::destroyed, this, [this] {
        m_item = nullptr;
        retire();
        emit changed();
        emit sceneChanged();
    });
    reparent();
}

EchoItemObserver::~EchoItemObserver()
{
    retire();
}

void EchoItemObserver::retire()
{
    if (m_item)
        disconnect(m_item, nullptr, this, nullptr);
    disconnect(m_parentChanged);
    disconnect(m_parentSceneChanged);
    disconnect(m_densityChanged);
    m_parent.reset();
    m_density.reset();
    forget(m_key, this);
}

void EchoItemObserver::notifyTransform()
{
    emit changed();
    emit sceneChanged();
}

void EchoItemObserver::reparent()
{
    disconnect(m_parentChanged);
    disconnect(m_parentSceneChanged);
    disconnect(m_densityChanged);
    m_parent = observeItem(m_item ? m_item->parentItem() : nullptr);
    m_density = observe<WindowDensityObserver>(!m_parent && m_item ? m_item->window() : nullptr);
    if (m_parent) {
        m_parentChanged =
            connect(m_parent.data(), &EchoItemObserver::changed, this, &EchoItemObserver::changed);
        m_parentSceneChanged = connect(m_parent.data(), &EchoItemObserver::sceneChanged, this,
                                       &EchoItemObserver::sceneChanged);
    }
    if (m_density)
        m_densityChanged = connect(m_density.data(), &WindowDensityObserver::changed, this,
                                   &EchoItemObserver::notifyTransform);
    emit changed();
    emit sceneChanged();
}

QSharedPointer<EchoItemObserver> observeItem(QQuickItem *item)
{
    return observe<EchoItemObserver>(item);
}
} // namespace EchoMusic

#include "EchoItemObserver.moc"
