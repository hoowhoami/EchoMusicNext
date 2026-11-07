#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>

namespace EchoMusic
{
// Shared native visibility boundary for public parts and plugin presentations.
// Observation is demand-owned; evaluation itself creates no scenegraph items.
class EchoUiPresentation final : public QObject
{
    Q_OBJECT
public:
    explicit EchoUiPresentation(QQuickItem *, QObject *parent);
    ~EchoUiPresentation() override;
    static bool presented(const QQuickItem *);
    static bool measurable(const QQuickItem *);
signals:
    void changed();

private:
    void observe();
    QPointer<QQuickItem> m_target;
    QObject *m_matrixObserver = nullptr;
    QList<QMetaObject::Connection> m_connections;
    bool m_observing = false, m_matrixPending = false;
};
} // namespace EchoMusic
