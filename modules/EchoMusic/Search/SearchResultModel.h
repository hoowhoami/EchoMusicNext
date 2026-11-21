#pragma once
#include <QCollator>
#include <QSortFilterProxyModel>
#include <array>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class SearchModel;
/// Presentation projection over the current category. Recycled delegates own
/// neither filtering/sorting nor selection state; the source retains pagination.
class SearchResultModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SearchModel")
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(SortField sortField READ sortField NOTIFY changed)
    Q_PROPERTY(SortDirection sortDirection READ sortDirection NOTIFY changed)
public:
    enum SortField
    {
        Unsorted,
        Index,
        Title,
        Artist,
        Album,
        Duration
    };
    Q_ENUM(SortField)
    enum SortDirection
    {
        None,
        Ascending,
        Descending
    };
    Q_ENUM(SortDirection)
    explicit SearchResultModel(SearchModel &source);
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString query() const;
    SortField sortField() const;
    SortDirection sortDirection() const;
    void setQuery(const QString &query);
    Q_INVOKABLE void toggleSort(SortField field);
    void resetPresentation();
signals:
    void changed();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    struct Presentation
    {
        QString query;
        SortField field = Unsorted;
        SortDirection direction = None;
    };
    void apply();
    SearchModel &m_search;
    QCollator m_collator;
    std::array<Presentation, 6> m_categories;
    int m_category = 0;
    QString m_needle;
};
} // namespace EchoMusic
