#include "SidebarPlaylistModel.h"
namespace EchoMusic
{
SidebarPlaylistModel::SidebarPlaylistModel(UserPlaylistModel &source, QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSourceModel(&source);
    setDynamicSortFilter(true);
    setSortRole(UserPlaylistModel::SortIndexRole);
    sort(0);
    connect(this, &QAbstractItemModel::rowsInserted, this, &SidebarPlaylistModel::changed);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &SidebarPlaylistModel::changed);
    connect(this, &QAbstractItemModel::modelReset, this, &SidebarPlaylistModel::changed);
}
void SidebarPlaylistModel::setCollected(bool value)
{
    if (m_collected == value)
        return;
    beginFilterChange();
    m_collected = value;
    endFilterChange(Direction::Rows);
    emit changed();
}
void SidebarPlaylistModel::setHiddenDefaultKinds(const QSet<quint32> &kinds)
{
    if (m_hiddenDefaultKinds == kinds)
        return;
    beginFilterChange();
    m_hiddenDefaultKinds = kinds;
    endFilterChange(Direction::Rows);
    emit changed();
}
bool SidebarPlaylistModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto index = sourceModel()->index(row, 0, parent);
    return index.data(UserPlaylistModel::SourceCodeRole).toUInt() != 2 &&
           (index.data(UserPlaylistModel::KindRole).toString() == QStringLiteral("collected")) ==
               m_collected &&
           (m_collected || !m_hiddenDefaultKinds.contains(
                               index.data(UserPlaylistModel::DefaultKindRole).toUInt()));
}
bool SidebarPlaylistModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const bool aPinned = !m_collected && left.data(UserPlaylistModel::DefaultKindRole).toUInt() > 0;
    const bool bPinned =
        !m_collected && right.data(UserPlaylistModel::DefaultKindRole).toUInt() > 0;
    if (aPinned != bPinned)
        return aPinned;
    const auto a = left.data(UserPlaylistModel::SortIndexRole).toLongLong(),
               b = right.data(UserPlaylistModel::SortIndexRole).toLongLong();
    if (a != b)
        return a < b;
    return left.row() < right.row();
}
int SidebarPlaylistModel::sourceRow(quint64 cloudId) const
{
    for (int row = 0; row < sourceModel()->rowCount(); ++row)
        if (sourceModel()->index(row, 0).data(UserPlaylistModel::CloudListIdRole).toULongLong() ==
            cloudId)
            return row;
    return -1;
}
} // namespace EchoMusic
