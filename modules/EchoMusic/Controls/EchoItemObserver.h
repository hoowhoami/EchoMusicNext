#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QSharedPointer>

namespace EchoMusic
{
class WindowDensityObserver;
// One observer per item, shared by every subscriber (image density, device
// pixel snap, ...). Transform notifications follow the parent chain instead of
// each subscriber creating its own connections, so N items on a page still
// subscribe once to every ancestor and once to the window density.
class EchoItemObserver final : public QObject
{
    Q_OBJECT
public:
    explicit EchoItemObserver(QQuickItem *item);
    ~EchoItemObserver() override;
    void retire();

signals:
    // The item's native scale/rotation or the window device pixel ratio
    // changed. Descendant observers forward this from their parent chain.
    void changed();
    // Any input that moves, resizes or transforms the item in the scene.
    void sceneChanged();

private:
    void reparent();
    void notifyTransform();
    QObject *m_key;
    QPointer<QQuickItem> m_item;
    QSharedPointer<EchoItemObserver> m_parent;
    QSharedPointer<WindowDensityObserver> m_density;
    QMetaObject::Connection m_parentChanged;
    QMetaObject::Connection m_parentSceneChanged;
    QMetaObject::Connection m_densityChanged;
};

QSharedPointer<EchoItemObserver> observeItem(QQuickItem *item);
// Broadcast an existing shared scene chain after presentation-only transforms.
// Does not allocate observers or invalidate image decode density.
void notifyItemScenePositionChanged(QQuickItem *item);
} // namespace EchoMusic
