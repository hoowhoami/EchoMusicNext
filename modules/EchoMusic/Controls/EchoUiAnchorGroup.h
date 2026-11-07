#pragma once
#include "EchoItemTranslation.h"
#include <QObject>
#include <QSet>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
namespace EchoMusic
{
// A free-form anchor's native geometry/parent/bindings stay with its owner.
// Only a scoped presentation transform follows the source slot in a real Qt layout.
class EchoUiAnchorGroup final : public QObject
{
    Q_OBJECT
public:
    EchoUiAnchorGroup(QQmlEngine &, QQuickItem *, QObject *);
    ~EchoUiAnchorGroup() override;
    void arrange(const QList<QQuickItem *> &before, const QList<QQuickItem *> &after,
                 const QString &axis, qreal gap);
    void release();
    void setPeers(const QSet<QQuickItem *> &peers)
    {
        m_peers = peers;
    }
    void settle()
    {
        for (int pass = 0; pass < 8; ++pass) {
            m_pending = false;
            layout();
            if (!m_pending)
                break;
        }
    }
    QString capacityError() const
    {
        return m_capacityError;
    }
    bool pending() const
    {
        return m_pending;
    }
signals:
    void arranged();

private:
    void schedule();
    void layout();
    void reserve(qreal length);
    void watchParent(QQuickItem *);
    void watchNeighbours();
    QQmlEngine &m_engine;
    QPointer<QQuickItem> m_source, m_parent, m_flow, m_slot;
    QPointer<QObject> m_padding;
    EchoItemTranslation m_translation;
    QList<QPointer<QQuickItem>> m_before, m_after;
    QList<QMetaObject::Connection> m_connections, m_itemConnections;
    QList<QMetaObject::Connection> m_parentConnections, m_neighbourConnections;
    QSet<QQuickItem *> m_peers;
    QString m_axis;
    QString m_capacityError;
    qreal m_gap = 0;
    bool m_pending = false, m_updating = false, m_released = false;
};
} // namespace EchoMusic
