#include "EchoUiValueModel.h"
#include <QJSValueIterator>
#include <limits>
namespace EchoMusic
{
bool EchoUiValueModel::assign(const QVariant &source)
{
    const auto reject = [this] {
        beginResetModel();
        m_array = QJSValue();
        m_values.clear();
        m_source.clear();
        m_count = 0;
        m_roles = {{Qt::UserRole, "modelData"}};
        endResetModel();
        return false;
    };
    QJSValue array;
    QVariantList values;
    int count = 0;
    bool numeric = false;
    if (source.metaType().id() == qMetaTypeId<QJSValue>()) {
        array = source.value<QJSValue>();
        if (!array.isArray())
            return reject();
        const auto length = array.property("length").toUInt();
        if (length > uint(std::numeric_limits<int>::max()))
            return reject();
        count = int(length);
    } else if (source.metaType().id() == QMetaType::QVariantList ||
               source.metaType().id() == QMetaType::QStringList) {
        values = source.toList();
        if (values.size() > std::numeric_limits<int>::max())
            return reject();
        count = int(values.size());
    } else if (source.metaType().id() == QMetaType::Int ||
               source.metaType().id() == QMetaType::UInt) {
        if (source.toLongLong() < 0 || source.toLongLong() > std::numeric_limits<int>::max())
            return reject();
        count = source.toInt();
        numeric = true;
    } else
        return reject();
    beginResetModel();
    m_source = source;
    m_array = array;
    m_values = values;
    m_count = count;
    m_numeric = numeric;
    m_roles = {{Qt::UserRole, "modelData"}};
    QStringList names;
    if (count) {
        const auto first = value(0);
        if (first.metaType().id() == QMetaType::QVariantMap)
            names = first.toMap().keys();
        else if (first.metaType().id() == qMetaTypeId<QJSValue>()) {
            QJSValueIterator iterator(first.value<QJSValue>());
            while (iterator.hasNext() && names.size() <= 128) {
                iterator.next();
                names.append(iterator.name());
            }
        }
    }
    int role = Qt::UserRole;
    for (const auto &name : names) {
        if (name == "modelData" || name.isEmpty())
            continue;
        if (m_roles.size() >= 128)
            break;
        m_roles[++role] = name.toUtf8();
    }
    endResetModel();
    return true;
}
int EchoUiValueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_count;
}
QVariant EchoUiValueModel::value(int row) const
{
    if (row < 0 || row >= m_count)
        return {};
    return m_numeric           ? QVariant(row)
           : m_array.isArray() ? QVariant::fromValue(m_array.property(row))
                               : m_values.value(row);
}
QVariant EchoUiValueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_count)
        return {};
    const auto item = value(index.row());
    if (role == Qt::UserRole)
        return item;
    const auto key = QString::fromUtf8(m_roles.value(role));
    if (key.isEmpty())
        return {};
    return item.metaType().id() == qMetaTypeId<QJSValue>()
               ? QVariant::fromValue(item.value<QJSValue>().property(key))
               : item.toMap().value(key);
}
} // namespace EchoMusic
