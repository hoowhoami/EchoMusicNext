#pragma once
#include <QAbstractProxyModel>
#include <QPointer>
#include <QVariantMap>

namespace EchoMusic
{
// Flat view projection. Native indices and roles retain their original meaning;
// extension rows/columns follow native data and never write the source model.
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
    void setEntries(const QList<Entry> &rows, const QList<Entry> &columns);
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

private:
    void resetRoles();
    const Entry *entry(const QModelIndex &) const;
    QVariantMap publicRow(int row) const;
    QHash<int, QByteArray> m_roles;
    int m_kindRole = Qt::UserRole + 1, m_valuesRole = Qt::UserRole + 2;
    int m_entryRole = Qt::UserRole + 3, m_keyRole = Qt::UserRole + 4;
    QList<Entry> m_rows, m_columns;
    QList<QMetaObject::Connection> m_connections;
    QModelIndexList m_layoutOld;
    QList<QPersistentModelIndex> m_layoutSource;
    QList<int> m_layoutExtraColumns;
    bool m_sourceReset = false;
};
} // namespace EchoMusic
