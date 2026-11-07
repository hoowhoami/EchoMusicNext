#include "EchoUiExtensionModel.h"
#include "EchoUiValue.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <utility>
#include <algorithm>

namespace EchoMusic
{
EchoUiExtensionModel::EchoUiExtensionModel(QObject *parent) : QAbstractProxyModel(parent) {}
int EchoUiExtensionModel::nativeRows() const
{
    return sourceModel() ? sourceModel()->rowCount() : 0;
}
int EchoUiExtensionModel::nativeColumns() const
{
    return sourceModel() ? sourceModel()->columnCount() : 1;
}
bool EchoUiExtensionModel::compatible() const
{
    if (!sourceModel())
        return false;
    const auto names = sourceModel()->roleNames().values();
    for (const auto &name : names)
        if (name.startsWith("echoUi"))
            return false;
    return true;
}
void EchoUiExtensionModel::setSourceModel(QAbstractItemModel *source)
{
    if (source == this || source == sourceModel())
        return;
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    beginResetModel();
    QAbstractProxyModel::setSourceModel(source);
    resetRoles();
    endResetModel();
    if (!source)
        return;
    m_connections << connect(source, &QAbstractItemModel::dataChanged, this,
                             [this](const auto &top, const auto &bottom, const auto &roles) {
                                 if (top.parent().isValid() || bottom.parent().isValid())
                                     return;
                                 emit dataChanged(mapFromSource(top), mapFromSource(bottom), roles);
                                 if (!m_columns.isEmpty())
                                     emit dataChanged(index(top.row(), nativeColumns()),
                                                      index(bottom.row(), columnCount() - 1), {});
                             });
    m_connections << connect(source, &QAbstractItemModel::rowsAboutToBeInserted, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid())
                                     beginInsertRows({}, first, last);
                             })
                  << connect(source, &QAbstractItemModel::rowsInserted, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid())
                                     endInsertRows();
                             });
    m_connections << connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid())
                                     beginRemoveRows({}, first, last);
                             })
                  << connect(source, &QAbstractItemModel::rowsRemoved, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid())
                                     endRemoveRows();
                             });
    m_connections << connect(source, &QAbstractItemModel::columnsAboutToBeInserted, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid())
                                     beginInsertColumns({}, first, last);
                             })
                  << connect(source, &QAbstractItemModel::columnsInserted, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid())
                                     endInsertColumns();
                             });
    m_connections << connect(source, &QAbstractItemModel::columnsAboutToBeRemoved, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid())
                                     beginRemoveColumns({}, first, last);
                             })
                  << connect(source, &QAbstractItemModel::columnsRemoved, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid())
                                     endRemoveColumns();
                             });
    m_connections << connect(source, &QAbstractItemModel::rowsAboutToBeMoved, this,
                             [this](const auto &from, int first, int last, const auto &to,
                                    int destination) {
                                 if (!from.isValid() && !to.isValid())
                                     beginMoveRows({}, first, last, {}, destination);
                             })
                  << connect(source, &QAbstractItemModel::rowsMoved, this,
                             [this](const auto &from, int, int, const auto &to, int) {
                                 if (!from.isValid() && !to.isValid())
                                     endMoveRows();
                             });
    m_connections << connect(source, &QAbstractItemModel::columnsAboutToBeMoved, this,
                             [this](const auto &from, int first, int last, const auto &to,
                                    int destination) {
                                 if (!from.isValid() && !to.isValid())
                                     beginMoveColumns({}, first, last, {}, destination);
                             })
                  << connect(source, &QAbstractItemModel::columnsMoved, this,
                             [this](const auto &from, int, int, const auto &to, int) {
                                 if (!from.isValid() && !to.isValid())
                                     endMoveColumns();
                             });
    m_connections << connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_sourceReset = true;
        beginResetModel();
    }) << connect(source, &QAbstractItemModel::modelReset, this, [this] {
        if (std::exchange(m_sourceReset, false)) {
            resetRoles();
            endResetModel();
        }
    });
    m_connections << connect(source, &QAbstractItemModel::layoutAboutToBeChanged, this, [this] {
        emit layoutAboutToBeChanged();
        m_layoutOld = persistentIndexList();
        m_layoutSource.clear();
        m_layoutExtraColumns.clear();
        for (const auto &at : m_layoutOld) {
            m_layoutSource.append(
                at.row() < nativeRows()
                    ? sourceModel()->index(at.row(), qMin(at.column(), nativeColumns() - 1))
                    : QModelIndex());
            m_layoutExtraColumns.append(at.column() >= nativeColumns() ? at.column() : -1);
        }
    }) << connect(source, &QAbstractItemModel::layoutChanged, this, [this] {
        QModelIndexList next;
        for (qsizetype i = 0; i < m_layoutOld.size(); ++i)
            next.append(m_layoutSource[i].isValid()
                            ? index(m_layoutSource[i].row(), m_layoutExtraColumns[i] >= 0
                                                                 ? m_layoutExtraColumns[i]
                                                                 : m_layoutSource[i].column())
                            : m_layoutOld[i]);
        changePersistentIndexList(m_layoutOld, next);
        m_layoutOld.clear();
        m_layoutSource.clear();
        m_layoutExtraColumns.clear();
        emit layoutChanged();
    });
}
void EchoUiExtensionModel::resetRoles()
{
    m_roles = sourceModel() ? sourceModel()->roleNames() : QHash<int, QByteArray>();
    int next = Qt::UserRole;
    for (auto role = m_roles.begin(); role != m_roles.end(); ++role)
        next = qMax(next, role.key());
    m_kindRole = ++next;
    m_valuesRole = ++next;
    m_entryRole = ++next;
    m_keyRole = ++next;
    m_roles[m_kindRole] = "echoUiComponent";
    m_roles[m_valuesRole] = "echoUiRow";
    m_roles[m_entryRole] = "echoUiEntry";
    m_roles[m_keyRole] = "echoUiKey";
}
void EchoUiExtensionModel::setEntries(const QList<Entry> &rows, const QList<Entry> &columns)
{
    if (m_rows == rows && m_columns == columns)
        return;
    beginResetModel();
    m_rows = rows;
    m_columns = columns;
    endResetModel();
}
QModelIndex EchoUiExtensionModel::index(int row, int column, const QModelIndex &parent) const
{
    return !parent.isValid() && row >= 0 && row < rowCount() && column >= 0 &&
                   column < columnCount()
               ? createIndex(row, column)
               : QModelIndex();
}
QModelIndex EchoUiExtensionModel::parent(const QModelIndex &) const
{
    return {};
}
QModelIndex EchoUiExtensionModel::mapToSource(const QModelIndex &at) const
{
    return at.isValid() && at.model() == this && sourceModel() && at.row() < nativeRows() &&
                   at.column() < nativeColumns()
               ? sourceModel()->index(at.row(), at.column())
               : QModelIndex();
}
QModelIndex EchoUiExtensionModel::mapFromSource(const QModelIndex &at) const
{
    return at.isValid() && at.model() == sourceModel() && !at.parent().isValid()
               ? index(at.row(), at.column())
               : QModelIndex();
}
int EchoUiExtensionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : nativeRows() + m_rows.size();
}
int EchoUiExtensionModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : nativeColumns() + m_columns.size();
}
const EchoUiExtensionModel::Entry *EchoUiExtensionModel::entry(const QModelIndex &at) const
{
    if (!at.isValid() || at.model() != this)
        return nullptr;
    if (at.row() >= nativeRows() && at.row() - nativeRows() < m_rows.size())
        return &m_rows[at.row() - nativeRows()];
    if (at.column() >= nativeColumns() && at.column() - nativeColumns() < m_columns.size())
        return &m_columns[at.column() - nativeColumns()];
    return nullptr;
}
QVariantMap EchoUiExtensionModel::publicRow(int row) const
{
    QVariantMap result;
    qsizetype budget = 65536;
    if (row < nativeRows() && sourceModel())
        for (auto role = m_roles.begin(); role != m_roles.end(); ++role) {
            if (role.key() >= m_kindRole)
                continue;
            const auto value = sourceModel()->data(sourceModel()->index(row, 0), role.key());
            const auto key = QString::fromUtf8(role.value());
            if (key.size() * 2 > budget)
                continue;
            auto publicValue = EchoUiValue::json(value, budget);
            if (publicValue.isValid() && key.size() * 2 <= budget) {
                budget -= key.size() * 2;
                result.insert(key, publicValue);
            }
        }
    return result;
}
QVariant EchoUiExtensionModel::data(const QModelIndex &at, int role) const
{
    if (!at.isValid() || at.model() != this)
        return {};
    const auto *extra = entry(at);
    if (role == m_kindRole)
        return extra ? extra->componentKey : QString();
    if (role == m_keyRole)
        return extra ? extra->owner + ':' + extra->contribution + ':' + extra->key : QString();
    if (role == m_valuesRole)
        return at.row() < nativeRows() ? publicRow(at.row())
                                       : m_rows.value(at.row() - nativeRows()).values;
    if (role == m_entryRole) {
        if (!extra)
            return QVariantMap();
        auto values = extra->values;
        values["_echoUiKey"] = extra->owner + ':' + extra->contribution + ':' + extra->key;
        return values;
    }
    if (extra) {
        if (role == Qt::DisplayRole)
            return extra->values.value("label");
        return extra->values.value(QString::fromUtf8(m_roles.value(role)));
    }
    const auto source = mapToSource(at);
    return source.isValid() ? sourceModel()->data(source, role) : QVariant();
}
bool EchoUiExtensionModel::setData(const QModelIndex &at, const QVariant &value, int role)
{
    const auto source = mapToSource(at);
    return source.isValid() && sourceModel()->setData(source, value, role);
}
Qt::ItemFlags EchoUiExtensionModel::flags(const QModelIndex &at) const
{
    if (const auto *extra = entry(at))
        return extra->enabled ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
    const auto source = mapToSource(at);
    return source.isValid() ? sourceModel()->flags(source) : Qt::NoItemFlags;
}
QVariant EchoUiExtensionModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    const auto native = orientation == Qt::Horizontal ? nativeColumns() : nativeRows();
    if (section < native)
        return sourceModel() ? sourceModel()->headerData(section, orientation, role) : QVariant();
    const auto &entries = orientation == Qt::Horizontal ? m_columns : m_rows;
    return role == Qt::DisplayRole && section - native < entries.size()
               ? entries[section - native].values.value("label")
               : QVariant();
}
QHash<int, QByteArray> EchoUiExtensionModel::roleNames() const
{
    return m_roles;
}
} // namespace EchoMusic
