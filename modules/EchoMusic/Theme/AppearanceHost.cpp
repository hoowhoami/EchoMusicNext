#include "AppearanceHost.h"
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
namespace EchoMusic
{
namespace
{
// A native observer keeps presentation work tied to effective visibility,
// including ancestor opacity and clipped scroll positions.
class PresentationVisibility final : public QObject
{
public:
    PresentationVisibility(QQuickItem &item, std::function<void()> changed)
        : QObject(&item), m_item(item), m_changed(std::move(changed))
    {
        rewire();
    }

private:
    void rewire()
    {
        delete m_connections;
        m_connections = new QObject(this);
        for (auto *item = &m_item; item; item = item->parentItem()) {
            connect(item, &QQuickItem::parentChanged, m_connections, [this] {
                rewire();
                m_changed();
            });
            connect(item, &QQuickItem::visibleChanged, m_connections, m_changed);
            connect(item, &QQuickItem::opacityChanged, m_connections, m_changed);
            connect(item, &QQuickItem::xChanged, m_connections, m_changed);
            connect(item, &QQuickItem::yChanged, m_connections, m_changed);
            connect(item, &QQuickItem::widthChanged, m_connections, m_changed);
            connect(item, &QQuickItem::heightChanged, m_connections, m_changed);
            connect(item, &QQuickItem::clipChanged, m_connections, m_changed);
        }
    }
    QQuickItem &m_item;
    std::function<void()> m_changed;
    QObject *m_connections = nullptr;
};
bool ownsQmlUrl(QObject *object, const QUrl &url)
{
    if (!object)
        return false;
    if (auto *context = qmlContext(object); context && context->baseUrl() == url)
        return true;
    for (auto *child : object->children())
        if (ownsQmlUrl(child, url))
            return true;
    return false;
}
bool presented(const QQuickItem &item)
{
    auto rect = item.mapRectToScene(item.boundingRect());
    for (auto *ancestor = &item; ancestor; ancestor = ancestor->parentItem()) {
        if (!ancestor->isVisible() || ancestor->opacity() <= 0)
            return false;
        if (ancestor->clip())
            rect = rect.intersected(ancestor->mapRectToScene(ancestor->boundingRect()));
    }
    if (item.window())
        rect = rect.intersected(QRectF(QPointF(), item.window()->size()));
    return !rect.isEmpty();
}
} // namespace
AppearanceHost::AppearanceHost(QQuickItem *p) : QQuickItem(p)
{
    m_visibility = new PresentationVisibility(*this, [this] { reconcile(); });
    connect(this, &QQuickItem::visibleChanged, this, &AppearanceHost::reconcile);
    connect(this, &QQuickItem::windowChanged, this, [this] {
        disconnect(m_windowConnection);
        if (window())
            m_windowConnection =
                connect(window(), &QWindow::visibilityChanged, this, &AppearanceHost::reconcile);
        reconcile();
    });
}
AppearanceHost::~AppearanceHost()
{
    delete m_visibility;
    release();
}
void AppearanceHost::componentComplete()
{
    QQuickItem::componentComplete();
    m_complete = true;
    reconcile();
}
void AppearanceHost::setEntry(AppearanceEntry *e)
{
    if (m_entry == e)
        return;
    if (m_entry)
        disconnect(m_entry, nullptr, this, nullptr);
    disconnect(m_catalogConnection);
    release();
    m_entry = e;
    if (e) {
        if (auto catalog = qobject_cast<AppearanceCatalog *>(e->parent()))
            m_catalogConnection =
                connect(catalog, &AppearanceCatalog::presentationsInvalidated, this, [this] {
                    release();
                    reconcile();
                });
        connect(e, &QObject::destroyed, this, [this] {
            release();
            m_entry = nullptr;
            emit entryChanged();
        });
        connect(e, &AppearanceEntry::changed, this, &AppearanceHost::reconcile);
    }
    emit entryChanged();
    reconcile();
}
void AppearanceHost::setPlacement(const QString &p)
{
    if (p == m_placement)
        return;
    release();
    m_placement = p;
    emit placementChanged();
    emit entryChanged();
    reconcile();
}
void AppearanceHost::setResourceActive(bool a)
{
    if (m_active == a)
        return;
    m_active = a;
    emit resourceActiveChanged();
    reconcile();
}
void AppearanceHost::geometryChange(const QRectF &a, const QRectF &b)
{
    QQuickItem::geometryChange(a, b);
    if (m_item)
        m_item->setSize(size());
    reconcile();
}
void AppearanceHost::release()
{
    ++m_generation;
    m_loading = false;
    disconnect(m_warningConnection);
    const auto context = m_context;
    const auto item = m_item;
    const auto component = m_component;
    m_context = nullptr;
    m_item = nullptr;
    m_component = nullptr;
    // Revocation can synchronously select another contribution. Detach all
    // previous state first so its cleanup cannot discard the new component.
    if (context) {
        context->setProperty("active", false);
        context->setParent(nullptr);
    }
    const bool had = item;
    if (item) {
        item->setParentItem(nullptr);
        item->deleteLater();
    }
    // Completion-time replacement is finalized after createWithInitialProperties
    // returns, when finish() can delete the unattached visual before its owner.
    if (context && !context->property("constructing").toBool())
        context->deleteLater();
    if (component)
        component->deleteLater();
    if (had)
        emit loadedChanged();
}
void AppearanceHost::reconcile()
{
    if (!m_complete)
        return;
    if (!m_active || !presented(*this) || !window() || !window()->isVisible() ||
        window()->visibility() == QWindow::Minimized || width() <= 0 || height() <= 0 || !m_entry ||
        m_entry->retired || !m_entry->error.isEmpty()) {
        release();
        return;
    }
    const auto path = m_entry->components.value(m_placement);
    if (path.isEmpty()) {
        release();
        return;
    }
    if (m_item || m_loading)
        return;
    m_loading = true;
    const auto generation = ++m_generation;
    m_component = new QQmlComponent(qmlEngine(this), this);
    m_warningConnection = connect(
        qmlEngine(this), &QQmlEngine::warnings, this,
        [this, generation, path](const QList<QQmlError> &errors) {
            for (const auto &error : errors) {
                bool owned = false;
                for (auto *object = error.object(); object; object = object->parent())
                    if (object == m_item || object == m_context) {
                        owned = true;
                        break;
                    }
                const auto resourceRoot =
                    m_context ? m_context->property("resourceRoot").toString() : QString();
                const bool constructing = m_context && m_context->property("constructing").toBool();
                owned = owned ||
                        (constructing && !resourceRoot.isEmpty() && error.url().isLocalFile() &&
                         error.url().toLocalFile().startsWith(resourceRoot + '/'));
                if (!error.object() && error.url() == path)
                    owned = true;
                if (!error.object() && !resourceRoot.isEmpty() && error.url().isLocalFile() &&
                    error.url().toLocalFile().startsWith(resourceRoot + '/') &&
                    ownsQmlUrl(m_item, error.url()))
                    owned = true;
                if (owned) {
                    const auto message = error.toString();
                    QTimer::singleShot(0, this, [this, generation, message] {
                        if (generation != m_generation || !m_entry)
                            return;
                        auto e = m_entry;
                        release();
                        if (auto catalog = qobject_cast<AppearanceCatalog *>(e->parent()))
                            catalog->fail(e, message);
                    });
                    break;
                }
            }
        });
    connect(m_component, &QQmlComponent::statusChanged, this, [this, generation](auto s) {
        if (s == QQmlComponent::Ready || s == QQmlComponent::Error)
            QTimer::singleShot(0, this, [this, generation] { finish(generation); });
    });
    m_component->loadUrl(path, QQmlComponent::Asynchronous);
    if (m_component && m_component->status() != QQmlComponent::Loading)
        QTimer::singleShot(0, this, [this, generation] { finish(generation); });
}
void AppearanceHost::finish(quint64 generation)
{
    if (generation != m_generation || !m_component || !m_loading || !m_entry)
        return;
    auto report = [this](const QString &message) {
        auto e = m_entry;
        release();
        if (e)
            if (auto catalog = qobject_cast<AppearanceCatalog *>(e->parent()))
                catalog->fail(e, message);
    };
    if (m_component->status() == QQmlComponent::Error) {
        report(m_component->errorString());
        return;
    }
    if (m_component->status() != QQmlComponent::Ready)
        return;
    m_context = new QObject(this);
    m_context->setProperty("active", true);
    auto properties = m_entry->factory ? m_entry->factory(m_placement, m_context) : QVariantMap();
    m_context->setProperty("resourceRoot", properties.take("$resourceRoot"));
    properties.insert("parent", QVariant::fromValue<QQuickItem *>(this));
    properties.insert("width", width());
    properties.insert("height", height());
    m_context->setProperty("constructing", true);
    const auto owner = m_context;
    auto *object = m_component->createWithInitialProperties(properties);
    if (owner)
        owner->setProperty("constructing", false);
    if (generation != m_generation || !m_entry || !m_active) {
        if (auto *item = qobject_cast<QQuickItem *>(object))
            item->setParentItem(nullptr);
        if (object)
            object->deleteLater();
        if (owner)
            owner->deleteLater();
        return;
    }
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        const auto error = m_component->errorString();
        if (object)
            object->deleteLater();
        report(error.isEmpty() ? "Appearance component root must be Item" : error);
        return;
    }
    item->setParent(m_context);
    item->setParentItem(this);
    item->setSize(size());
    m_item = item;
    setImplicitHeight(item->implicitHeight());
    setImplicitWidth(item->implicitWidth());
    connect(item, &QQuickItem::implicitHeightChanged, this,
            [this, item] { setImplicitHeight(item->implicitHeight()); });
    connect(item, &QQuickItem::implicitWidthChanged, this,
            [this, item] { setImplicitWidth(item->implicitWidth()); });
    m_loading = false;
    emit loadedChanged();
}
} // namespace EchoMusic
