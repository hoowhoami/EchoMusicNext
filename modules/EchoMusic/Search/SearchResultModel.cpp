#include "SearchResultModel.h"
#include "SearchModel.h"
#include "SongCatalogModel.h"

namespace EchoMusic
{
SearchResultModel::SearchResultModel(SearchModel &source)
    : QSortFilterProxyModel(&source), m_search(source),
      m_collator(QLocale(QStringLiteral("zh_Hans_CN")))
{
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    setSourceModel(&source);
    connect(&source, &SearchModel::changed, this, [this] {
        const int category = int(m_search.kind());
        if (m_category != category) {
            m_category = category;
        }
        apply();
        emit changed();
    });
    connect(this, &QAbstractItemModel::rowsInserted, this, &SearchResultModel::changed);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &SearchResultModel::changed);
    connect(this, &QAbstractItemModel::modelReset, this, &SearchResultModel::changed);
}
QString SearchResultModel::query() const
{
    return m_categories[std::size_t(m_category)].query;
}
SearchResultModel::SortField SearchResultModel::sortField() const
{
    return m_categories[std::size_t(m_category)].field;
}
SearchResultModel::SortDirection SearchResultModel::sortDirection() const
{
    return m_categories[std::size_t(m_category)].direction;
}
void SearchResultModel::setQuery(const QString &query)
{
    auto &state = m_categories[std::size_t(m_category)];
    if (state.query == query)
        return;
    if (m_search.catalog()) {
        state.query = query;
        apply();
        emit changed();
        return;
    }
    beginFilterChange();
    state.query = query;
    m_needle = query.trimmed().toLower();
    endFilterChange(Direction::Rows);
    emit changed();
}
void SearchResultModel::toggleSort(SortField field)
{
    auto &state = m_categories[std::size_t(m_category)];
    if (field == Unsorted) {
        state.field = Unsorted;
        state.direction = None;
    } else if (state.field != field || state.direction == None) {
        state.field = field;
        state.direction = Ascending;
    } else if (state.direction == Ascending)
        state.direction = Descending;
    else {
        state.field = Unsorted;
        state.direction = None;
    }
    apply();
    emit changed();
}
void SearchResultModel::resetPresentation()
{
    m_categories = {};
    m_category = int(m_search.kind());
    apply();
    emit changed();
}
QVariant SearchResultModel::data(const QModelIndex &index, int role) const
{
    if (sourceModel() == m_search.catalog())
        return mapToSource(index).data(SearchModel::catalogRole(role));
    return QSortFilterProxyModel::data(index, role);
}
QHash<int, QByteArray> SearchResultModel::roleNames() const
{
    return m_search.roleNames();
}
void SearchResultModel::apply()
{
    auto *catalog = m_search.catalog();
    QAbstractItemModel *desired = catalog ? static_cast<QAbstractItemModel *>(catalog) : &m_search;
    if (sourceModel() != desired)
        setSourceModel(desired);
    if (catalog) {
        catalog->setQuery(query());
        catalog->setSort(int(sortField()), int(sortDirection()));
        sort(-1);
        return;
    }
    beginFilterChange();
    m_needle = query().trimmed().toLower();
    endFilterChange(Direction::Rows);
    sort(sortField() == Unsorted ? -1 : 0,
         sortDirection() == Descending ? Qt::DescendingOrder : Qt::AscendingOrder);
    // A changed comparison field needs reordering even if column/order stayed
    // equal; sort() alone can be a no-op in that case.
    if (sortField() != Unsorted)
        invalidate();
}
bool SearchResultModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (m_search.catalog() || m_needle.isEmpty())
        return true;
    const auto item = sourceModel()->index(row, 0, parent);
    for (int role : {SearchModel::Title, SearchModel::ArtistText, SearchModel::AlbumText})
        if (item.data(role).toString().toLower().contains(m_needle))
            return true;
    return false;
}
bool SearchResultModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (sortField() == Index)
        return left.row() < right.row();
    if (sortField() == Duration)
        return left.data(SearchModel::DurationMs).toULongLong() <
               right.data(SearchModel::DurationMs).toULongLong();
    const int role = sortField() == Artist ? SearchModel::ArtistText
                     : sortField() == Album
                         ? (m_search.kind() == SearchModel::Kind::Lyric ? SearchModel::ExtraText
                                                                        : SearchModel::AlbumText)
                         : SearchModel::Title;
    const int comparison = m_collator.compare(left.data(role).toString().trimmed(),
                                              right.data(role).toString().trimmed());
    if (comparison)
        return comparison < 0;
    if (sortField() == Album)
        return false; // Preserve stable source order for equal album/excerpt.
    const int tie = sortField() == Title ? SearchModel::ArtistText : SearchModel::Title;
    return m_collator.compare(left.data(tie).toString().trimmed(),
                              right.data(tie).toString().trimmed()) < 0;
}
} // namespace EchoMusic
