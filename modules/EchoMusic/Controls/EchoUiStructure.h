#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QTimer>

class QQuickItem;
class QQuickWindow;
namespace EchoMusic
{
class EchoUiPartRegistry;
class EchoUiPartRegistration;
// Demand-owned view of the live Qt Quick tree. It observes structural changes,
// never polls frames and never creates or replaces a visual host item.
class EchoUiStructure final : public QObject
{
    Q_OBJECT
public:
    explicit EchoUiStructure(EchoUiPartRegistry &);
    void acquire(QObject *consumer, bool inspector, bool immediate = true);
    void release(QObject *consumer);
    void schedule(QQuickItem *root = nullptr);
    void synchronize();
    void supersede(QQuickItem *);
    int watchCount() const
    {
        return m_watches.size();
    }
    bool hasConsumers() const
    {
        return !m_consumers.isEmpty();
    }
    bool owns(EchoUiPartRegistration *) const;

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    void watchDelegate(QQuickItem *);
    void watchView(QQuickItem *);
    void bindModel(QQuickItem *);
    void renewView(QQuickItem *, int first = -1, int last = -1, bool unknownOnly = false,
                   bool deactivate = false);
    void releaseDelegates();
    struct Delegate
    {
        QPointer<QObject> attached, relay;
        QPointer<QQuickItem> view;
        QList<QMetaObject::Connection> connections;
        bool managedResource = false;
    };
    QHash<QQuickItem *, Delegate> m_delegates;
    struct View
    {
        QList<QMetaObject::Connection> connections, modelConnections;
    };
    QHash<QQuickItem *, View> m_views;
private slots:
    void viewModelChanged();

private:
    struct Consumer
    {
        bool inspector;
        QMetaObject::Connection destroyed;
    };
    void clear();
    void indexParent(QQuickItem *);
    void forgetIndex(QQuickItem *);
    EchoUiPartRegistry &m_registry;
    QTimer m_update;
    QHash<QObject *, Consumer> m_consumers;
    QHash<QQuickItem *, QPointer<EchoUiPartRegistration>> m_nodes;
    QHash<QQuickItem *, QList<QMetaObject::Connection>> m_watches;
    QHash<QQuickItem *, QPointer<QQuickItem>> m_dirty;
    QHash<QQuickItem *, QQuickItem *> m_parents;
    QHash<QQuickItem *, QSet<QQuickItem *>> m_children;
    QSet<QQuickWindow *> m_windows;
    quint64 m_sequence = 0;
    bool m_full = true;
    bool m_building = false, m_filterInstalled = false;
};
} // namespace EchoMusic
