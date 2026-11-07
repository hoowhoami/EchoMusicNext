#include "EchoUiPresentation.h"
#include <QMetaMethod>
#include <QGuiApplication>
#include <QTimer>
#include <QSet>
#include <QMetaProperty>
#include <QPolygonF>
#include <QQuickWindow>
#include <cmath>
#include <functional>
#include <QtQuick/private/qquickitem_p.h>

namespace EchoMusic
{
namespace
{
class MatrixObserver final : public QObject, private QQuickItemChangeListener
{
public:
    MatrixObserver(QQuickItem *item, QObject *parent, std::function<void()> changed)
        : QObject(parent), m_item(item), m_changed(std::move(changed))
    {
        if (m_item)
            QQuickItemPrivate::get(m_item)->addItemChangeListener(
                this, QQuickItemPrivate::Matrix | QQuickItemPrivate::Destroyed);
    }
    ~MatrixObserver() override
    {
        if (m_item)
            QQuickItemPrivate::get(m_item)->removeItemChangeListener(
                this, QQuickItemPrivate::Matrix | QQuickItemPrivate::Destroyed);
    }

private:
    void itemTransformChanged(QQuickItem *, QQuickItem *) override
    {
        m_changed();
    }
    void itemDestroyed(QQuickItem *) override
    {
        m_item = nullptr;
    }
    QQuickItem *m_item;
    std::function<void()> m_changed;
};
QPolygonF polygon(const QQuickItem *item, bool measure = false)
{
    auto bounds = item->boundingRect();
    if (measure)
        bounds.setSize({qMax(1., bounds.width()), qMax(1., bounds.height())});
    QPolygonF points(bounds);
    for (auto &point : points)
        point = item->mapToScene(point);
    return points;
}
} // namespace
namespace
{
bool evaluate(const QQuickItem *target, bool measure)
{
    const auto *window = target ? target->window() : nullptr;
    if (!window || !window->isVisible() || window->visibility() == QWindow::Minimized ||
        (!measure && (target->width() <= 0 || target->height() <= 0)))
        return false;
    auto area = polygon(target, measure);
    for (auto *item = target; item && !area.isEmpty(); item = item->parentItem()) {
        if (!item->isVisible() || item->opacity() <= 0 || item->scale() == 0)
            return false;
        if (item->clip())
            area = area.intersected(polygon(item, measure && item == target));
    }
    area = area.intersected(QPolygonF(QRectF(QPointF(), window->size())));
    double twiceArea = 0;
    for (qsizetype i = 0; i < area.size(); ++i) {
        const auto &a = area[i], &b = area[(i + 1) % area.size()];
        twiceArea += a.x() * b.y() - b.x() * a.y();
    }
    return std::isfinite(twiceArea) && std::abs(twiceArea) > 0;
}
} // namespace
bool EchoUiPresentation::presented(const QQuickItem *target)
{
    return evaluate(target, false);
}
bool EchoUiPresentation::measurable(const QQuickItem *target)
{
    return evaluate(target, true);
}
EchoUiPresentation::EchoUiPresentation(QQuickItem *target, QObject *parent)
    : QObject(parent), m_target(target)
{
    observe();
}
EchoUiPresentation::~EchoUiPresentation()
{
    if (m_matrixObserver)
        m_matrixObserver->deleteLater();
}
void EchoUiPresentation::observe()
{
    if (m_observing)
        return;
    m_observing = true;
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    const auto rewire = [this] {
        observe();
        emit this->changed();
    };
    const auto changed = [this] { emit this->changed(); };
    for (auto *item = m_target.data(); item; item = item->parentItem()) {
        m_connections << connect(item, &QQuickItem::childrenChanged, this, rewire)
                      << connect(item, &QQuickItem::parentChanged, this, rewire)
                      << connect(item, &QQuickItem::windowChanged, this, rewire)
                      << connect(item, &QQuickItem::visibleChanged, this, changed)
                      << connect(item, &QQuickItem::opacityChanged, this, changed)
                      << connect(item, &QQuickItem::xChanged, this, changed)
                      << connect(item, &QQuickItem::yChanged, this, changed)
                      << connect(item, &QQuickItem::widthChanged, this, changed)
                      << connect(item, &QQuickItem::heightChanged, this, changed)
                      << connect(item, &QQuickItem::clipChanged, this, changed)
                      << connect(item, &QQuickItem::scaleChanged, this, changed)
                      << connect(item, &QQuickItem::rotationChanged, this, changed)
                      << connect(item, &QQuickItem::transformOriginChanged, this, changed);
    }
    if (auto *window = m_target ? m_target->window() : nullptr)
        m_connections << connect(window, &QWindow::visibilityChanged, this, changed)
                      << connect(window, &QWindow::widthChanged, this, changed)
                      << connect(window, &QWindow::heightChanged, this, changed);
    m_observing = false;
    // Qt iterates raw native change listeners during transforms/reparenting.
    // Do not delete or add those listeners from within that same broadcast.
    // The weak callback becomes inert immediately when its scope is retired.
    if (!std::exchange(m_matrixPending, true))
        QTimer::singleShot(0, this, [this] {
            m_matrixPending = false;
            auto *previous = m_matrixObserver;
            m_matrixObserver = new QObject(qGuiApp);
            const QPointer<EchoUiPresentation> self(this);
            for (auto *item = m_target.data(); item; item = item->parentItem())
                new MatrixObserver(item, m_matrixObserver, [self] {
                    if (self)
                        emit self->changed();
                });
            if (previous)
                previous->deleteLater();
            emit this->changed();
        });
}
} // namespace EchoMusic
