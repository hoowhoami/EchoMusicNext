#pragma once
#include <QAbstractListModel>
#include <QJSValue>
namespace EchoMusic
{
// Lazy facade over Qt's value-model forms. The source array is retained by
// reference (or implicit sharing); only requested delegate rows are read.
class EchoUiValueModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit EchoUiValueModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    bool assign(const QVariant &source);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override
    {
        return m_roles;
    }

private:
    QVariant value(int row) const;
    QVariant m_source;
    QJSValue m_array;
    QVariantList m_values;
    QHash<int, QByteArray> m_roles;
    int m_count = 0;
    bool m_numeric = false;
};
} // namespace EchoMusic
