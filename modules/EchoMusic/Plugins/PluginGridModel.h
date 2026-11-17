#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <algorithm>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Virtual grid membership; record changes are forwarded by each visible row's
/// PluginCardRowModel without replacing its Repeater model or sibling cards.
class PluginGridModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY changed)
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() || !m_source ? 0
                                             : (m_source->rowCount() + m_columns - 1) / m_columns;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!m_source || !index.isValid() || index.row() < 0 || index.row() >= rowCount() ||
            role != Qt::UserRole + 1)
            return {};
        return index.row();
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "rowIndex"}};
    }
    QAbstractItemModel *source() const
    {
        return m_source;
    }
    int columns() const
    {
        return m_columns;
    }
    void setSource(QAbstractItemModel *source)
    {
        if (m_source == source)
            return;
        if (m_source)
            disconnect(m_source, nullptr, this, nullptr);
        beginResetModel();
        m_source = source;
        endResetModel();
        if (source) {
            connect(source, &QAbstractItemModel::modelReset, this, &PluginGridModel::reset);
            connect(source, &QAbstractItemModel::rowsInserted, this, &PluginGridModel::reset);
            connect(source, &QAbstractItemModel::rowsRemoved, this, &PluginGridModel::reset);
        }
        emit changed();
    }
    Q_INVOKABLE int rowForPlugin(const QString &id) const
    {
        if (!m_source)
            return -1;
        const auto role = m_source->roleNames().key("pluginRecord", -1);
        for (int i = 0; role >= 0 && i < m_source->rowCount(); ++i)
            if (m_source->data(m_source->index(i, 0), role).toMap().value("id").toString() == id)
                return i / m_columns;
        return -1;
    }
    void setColumns(int value)
    {
        value = std::clamp(value, 1, 8);
        if (value == m_columns)
            return;
        beginResetModel();
        m_columns = value;
        endResetModel();
        emit changed();
    }
signals:
    void changed();

private:
    void reset()
    {
        beginResetModel();
        endResetModel();
        emit changed();
    }
    QPointer<QAbstractItemModel> m_source;
    int m_columns = 1;
};
} // namespace EchoMusic
