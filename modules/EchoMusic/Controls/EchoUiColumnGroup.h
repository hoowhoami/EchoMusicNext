#pragma once
#include "EchoUiModelAdapter.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiColumnVisibility final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool nativeVisible MEMBER nativeVisible)
public:
    bool nativeVisible = true;
    std::unique_ptr<EchoUiPropertyRelay> relay;
};
// Shared protocol for composite views whose native cells use a RowLayout.
// The controller is shared by a header and the virtualized row instances.
class EchoUiColumnGroup final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_ATTACHED(EchoUiColumnGroup)
    QML_UNCREATABLE("Attach to a native row layout")
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY changed)
    Q_PROPERTY(QStringList keys MEMBER m_keys NOTIFY changed)
    Q_PROPERTY(QVariantMap row READ row WRITE setRow NOTIFY changed)
    Q_PROPERTY(int rowIndex READ rowIndex WRITE setRowIndex NOTIFY changed)
    Q_PROPERTY(bool header MEMBER m_header NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
public:
    explicit EchoUiColumnGroup(QObject *);
    ~EchoUiColumnGroup() override;
    static EchoUiColumnGroup *qmlAttachedProperties(QObject *object)
    {
        return new EchoUiColumnGroup(object);
    }
    QObject *controller() const
    {
        return m_controller;
    }
    void setController(QObject *);
    QVariantMap row() const
    {
        return m_row;
    }
    void setRow(const QVariantMap &);
    int rowIndex() const
    {
        return m_rowIndex;
    }
    void setRowIndex(int);
    bool active() const
    {
        return m_active;
    }
    void setActive(bool);
signals:
    void changed();
private slots:
    void schedule();

private:
    void refresh();
    void restore();
    QPointer<QQuickItem> m_layout;
    QPointer<QObject> m_controller;
    QPointer<EchoUiModelAdapter> m_adapter;
    QStringList m_keys;
    QVariantMap m_row;
    QList<QPointer<QQuickItem>> m_native;
    QHash<QString, QPointer<QQuickItem>> m_cells;
    QHash<QQuickItem *, EchoUiColumnVisibility *> m_hidden;
    QList<QMetaObject::Connection> m_connections;
    QMetaObject::Connection m_adapterConnection;
    int m_rowIndex = -1;
    bool m_header = false, m_active = true, m_pending = false;
};
} // namespace EchoMusic
