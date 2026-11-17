#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <algorithm>

namespace EchoMusic
{
/// A visible grid row owns only its bounded record projection. Source data
/// changes update existing Repeater delegates instead of replacing a JS array.
class PluginCardRowModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(int start READ start WRITE setStart NOTIFY changed)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY changed)
public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_count;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!m_source || !index.isValid() || index.row() < 0 || index.row() >= m_count ||
            role != Qt::UserRole + 1 || m_recordRole < 0)
            return {};
        return m_source->data(m_source->index(m_start + index.row(), 0), m_recordRole);
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "pluginRecord"}};
    }
    QAbstractItemModel *source() const
    {
        return m_source;
    }
    int start() const
    {
        return m_start;
    }
    int limit() const
    {
        return m_limit;
    }
    void setSource(QAbstractItemModel *source)
    {
        if (m_source == source)
            return;
        if (m_source)
            disconnect(m_source, nullptr, this, nullptr);
        m_source = source;
        if (source) {
            connect(source, &QAbstractItemModel::dataChanged, this,
                    [this](const QModelIndex &first, const QModelIndex &last) {
                        if (first.parent().isValid())
                            return;
                        const int begin = std::max(0, first.row() - m_start);
                        const int end = std::min(m_count - 1, last.row() - m_start);
                        if (begin <= end)
                            emit dataChanged(index(begin), index(end), {Qt::UserRole + 1});
                    });
            connect(source, &QAbstractItemModel::modelReset, this, &PluginCardRowModel::reset);
            connect(source, &QAbstractItemModel::rowsInserted, this, &PluginCardRowModel::reset);
            connect(source, &QAbstractItemModel::rowsRemoved, this, &PluginCardRowModel::reset);
            connect(source, &QObject::destroyed, this, [this] {
                m_source = nullptr;
                reset();
            });
        }
        reset();
        emit changed();
    }
    void setStart(int value)
    {
        value = std::max(0, value);
        if (m_start == value)
            return;
        m_start = value;
        reset();
        emit changed();
    }
    void setLimit(int value)
    {
        value = std::clamp(value, 1, 8);
        if (m_limit == value)
            return;
        m_limit = value;
        reset();
        emit changed();
    }
signals:
    void changed();

private:
    void reset()
    {
        beginResetModel();
        m_recordRole = m_source ? m_source->roleNames().key("pluginRecord", -1) : -1;
        m_count = m_source ? std::clamp(m_source->rowCount() - m_start, 0, m_limit) : 0;
        endResetModel();
    }
    QPointer<QAbstractItemModel> m_source;
    int m_start = 0, m_limit = 1, m_count = 0, m_recordRole = -1;
};
} // namespace EchoMusic
