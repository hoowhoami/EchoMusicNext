#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include "ValueCache.h"
#include "CacheCost.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Immutable presentation records. Filtering and installation state belong to
/// the application manager; delegates never maintain their own business copy.
class PluginListModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PluginManager")
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() ||
            role != Qt::UserRole + 1)
            return {};
        if (const auto hit = m_visible.lookup(index.row()))
            return *hit;
        const auto object = m_rows[index.row()].toObject();
        auto value = object.toVariantMap();
        m_visible.insert(index.row(), value, decltype(m_visible)::NoExpiry, jsonValueCost(object));
        return value;
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "pluginRecord"}};
    }
    // State-only updates retain native view position and row identity. A changed
    // membership/order still resets the projection, as search/source filters do.
    void replace(const QVariantList &values, const QString &identity = QStringLiteral("key"))
    {
        const auto rows = QJsonArray::fromVariantList(values);
        if (rows == m_rows)
            return;
        bool sameOrder = rows.size() == m_rows.size();
        for (qsizetype row = 0; sameOrder && row < rows.size(); ++row) {
            const auto key = rows[row].toObject().value(identity).toString();
            sameOrder = !key.isEmpty() && key == m_rows[row].toObject().value(identity).toString();
        }
        if (sameOrder) {
            for (qsizetype row = 0; row < rows.size(); ++row) {
                if (m_rows[row] == rows[row])
                    continue;
                m_rows[row] = rows[row];
                m_visible.remove(int(row));
                emit dataChanged(index(int(row)), index(int(row)), {Qt::UserRole + 1});
            }
        } else {
            beginResetModel();
            m_rows = rows;
            m_visible.clear();
            endResetModel();
        }
        emit changed();
    }
signals:
    void changed();

private:
    QJsonArray m_rows;
    mutable ValueCache<int, QVariantMap> m_visible{128, 2 * 1024 * 1024};
};
} // namespace EchoMusic
