#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <memory>
namespace EchoMusic
{
// A demand-owned Qt KeyNavigation lease for one ordered adjacent group.
class EchoUiFocusOrder final : public QObject
{
    Q_OBJECT
public:
    EchoUiFocusOrder(QQuickItem *source, QObject *parent);
    ~EchoUiFocusOrder() override;
    void arrange(const QList<QQuickItem *> &before, const QList<QQuickItem *> &after,
                 const QString &axis);
    void release();
    void suspend();
signals:
    void refreshRequested();

private:
    void refresh();
    void schedule();
    struct Data;
    std::unique_ptr<Data> m_data;
    QPointer<QQuickItem> m_source;
    QList<QPointer<QQuickItem>> m_before, m_after;
    QList<QMetaObject::Connection> m_connections;
    QString m_axis;
    bool m_pending = false, m_released = false, m_updating = false;
};
} // namespace EchoMusic
