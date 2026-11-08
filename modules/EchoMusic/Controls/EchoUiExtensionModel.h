#pragma once
#include "EchoUiModelOrder.h"
#include <QAbstractProxyModel>
#include <QPointer>
#include <QVariantMap>

namespace EchoMusic
{
// Flat, sparse view projection. mapToSource/mapFromSource preserve business
// identity; extension placement never mutates or copies the source model.
class EchoUiExtensionModel final : public QAbstractProxyModel
{
    Q_OBJECT
public:
    struct Entry
    {
        QString key, owner, contribution, componentKey;
        QVariantMap values;
        bool enabled = true;
        bool operator==(const Entry &) const = default;
    };
    explicit EchoUiExtensionModel(QObject *parent = nullptr);
    void setSourceModel(QAbstractItemModel *) override;
    bool setEntries(const QList<Entry> &rows, const QList<Entry> &columns,
                    QString *error = nullptr);
    int nativeSection(int position, bool column) const;
    int extraSection(int position, bool column) const;
    int nativePosition(int section, bool column) const;
    int extraPosition(int section, bool column) const;
    QModelIndex index(int, int, const QModelIndex & = {}) const override;
    QModelIndex parent(const QModelIndex &) const override;
    QModelIndex mapToSource(const QModelIndex &) const override;
    QModelIndex mapFromSource(const QModelIndex &) const override;
    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &, int = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &, const QVariant &, int = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &) const override;
    QVariant headerData(int, Qt::Orientation, int = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int nativeRows() const;
    int nativeColumns() const;
    bool compatible() const;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
signals:
    void projectionErrorChanged(const QString &);

private:
    friend class EchoUiModelAdapter;
    void resetRoles();
    bool rebuildOrder(QString &);
    void rebuildAfterSource();
    int lookupNative(const QString &, bool column) const;
    const Entry *entry(const QModelIndex &) const;
    QVariantMap publicRow(int row) const;
    QHash<int, QByteArray> m_roles;
    int m_kindRole = Qt::UserRole + 1, m_valuesRole = Qt::UserRole + 2;
    int m_entryRole = Qt::UserRole + 3, m_keyRole = Qt::UserRole + 4;
    QList<Entry> m_rows, m_columns;
    EchoUiModelOrder m_rowOrder, m_columnOrder;
    bool m_ordered = false;
    QString m_orderError;
    QList<QMetaObject::Connection> m_connections;
    QModelIndexList m_layoutOld;
    QList<QPersistentModelIndex> m_layoutSource;
    QList<int> m_layoutExtraColumns;
    bool m_sourceReset = false;
};
} // namespace EchoMusic
