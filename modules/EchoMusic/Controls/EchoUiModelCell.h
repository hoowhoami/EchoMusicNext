#pragma once
#include "EchoUiContentFactory.h"
#include <QVariantMap>
namespace EchoMusic
{
class EchoUiModelRecipe : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool active() const = 0;
    virtual bool select(const QVariantMap &)
    {
        return false;
    }
    virtual bool arm(const QVariantMap &)
    {
        return false;
    }
    virtual bool activate(const QVariantMap &, const QVariantMap &)
    {
        return false;
    }
    virtual EchoUiContentFactory *factory(QObject *owner, const QVariantMap &row,
                                          const QVariantMap &entry) = 0;
signals:
    void changed();
};
class EchoUiModelCell : public EchoUiContentHost
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *recipe READ recipe WRITE setRecipe NOTIFY cellChanged)
    Q_PROPERTY(QVariantMap rowData READ rowData WRITE setRowData NOTIFY cellChanged)
    Q_PROPERTY(QVariantMap entryData READ entryData WRITE setEntryData NOTIFY cellChanged)
    Q_PROPERTY(int viewRow READ viewRow WRITE setViewRow NOTIFY cellChanged)
    Q_PROPERTY(int viewColumn READ viewColumn WRITE setViewColumn NOTIFY cellChanged)
public:
    explicit EchoUiModelCell(QQuickItem *parent = nullptr);
    QObject *recipe() const
    {
        return m_recipe;
    }
    void setRecipe(QObject *);
    QVariantMap rowData() const
    {
        return m_row;
    }
    QVariantMap entryData() const
    {
        return m_entry;
    }
    void setRowData(const QVariantMap &);
    void setEntryData(const QVariantMap &);
    int viewRow() const
    {
        return m_viewRow;
    }
    int viewColumn() const
    {
        return m_viewColumn;
    }
    void setViewRow(int);
    void setViewColumn(int);
signals:
    void cellChanged();

protected:
    void componentComplete() override;

private:
    void retire();
    void rebuild();
    void schedule();
    void updateActive();
    void updateResourceKey();
    void refreshData(bool structural = false);
    QPointer<EchoUiModelRecipe> m_recipe;
    QPointer<EchoUiContentFactory> m_cellFactory;
    QVariantMap m_row, m_entry;
    QList<QMetaObject::Connection> m_recipeConnections;
    bool m_complete = false, m_pending = false;
    int m_viewRow = -1, m_viewColumn = 0;
    QString m_realizedResource;
    QObject m_cellCallbacks;
};
} // namespace EchoMusic
