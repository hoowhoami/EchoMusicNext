#include "EchoUiExtensionModel.h"
#include "EchoUiValue.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
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
    if (m_ordered)
        rebuildAfterSource();
    endResetModel();
    if (!source)
        return;
    m_connections << connect(source, &QAbstractItemModel::dataChanged, this,
                             [this](const auto &top, const auto &bottom, const auto &roles) {
                                 if (top.parent().isValid() || bottom.parent().isValid())
                                     return;
                                 if (m_ordered) {
                                     if (!columnCount())
                                         return;
                                     for (int row = top.row(); row <= bottom.row(); ++row) {
                                         const int projected = nativePosition(row, false);
                                         if (projected >= 0)
                                             emit dataChanged(index(projected, 0),
                                                              index(projected, columnCount() - 1),
                                                              {});
                                     }
                                     return;
                                 }
                                 emit dataChanged(mapFromSource(top), mapFromSource(bottom), roles);
                                 if (!m_columns.isEmpty())
                                     emit dataChanged(index(top.row(), nativeColumns()),
                                                      index(bottom.row(), columnCount() - 1), {});
                             });
    m_connections << connect(source, &QAbstractItemModel::rowsAboutToBeInserted, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid()) {
                                     if (m_ordered)
                                         beginResetModel();
                                     else
                                         beginInsertRows({}, first, last);
                                 }
                             })
                  << connect(source, &QAbstractItemModel::rowsInserted, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid()) {
                                     if (m_ordered) {
                                         rebuildAfterSource();
                                         endResetModel();
                                     } else
                                         endInsertRows();
                                 }
                             });
    m_connections << connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid()) {
                                     if (m_ordered)
                                         beginResetModel();
                                     else
                                         beginRemoveRows({}, first, last);
                                 }
                             })
                  << connect(source, &QAbstractItemModel::rowsRemoved, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid()) {
                                     if (m_ordered) {
                                         rebuildAfterSource();
                                         endResetModel();
                                     } else
                                         endRemoveRows();
                                 }
                             });
    m_connections << connect(source, &QAbstractItemModel::columnsAboutToBeInserted, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid()) {
                                     if (m_ordered)
                                         beginResetModel();
                                     else
                                         beginInsertColumns({}, first, last);
                                 }
                             })
                  << connect(source, &QAbstractItemModel::columnsInserted, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid()) {
                                     if (m_ordered) {
                                         rebuildAfterSource();
                                         endResetModel();
                                     } else
                                         endInsertColumns();
                                 }
                             });
    m_connections << connect(source, &QAbstractItemModel::columnsAboutToBeRemoved, this,
                             [this](const auto &parent, int first, int last) {
                                 if (!parent.isValid()) {
                                     if (m_ordered)
                                         beginResetModel();
                                     else
                                         beginRemoveColumns({}, first, last);
                                 }
                             })
                  << connect(source, &QAbstractItemModel::columnsRemoved, this,
                             [this](const auto &parent, int, int) {
                                 if (!parent.isValid()) {
                                     if (m_ordered) {
                                         rebuildAfterSource();
                                         endResetModel();
                                     } else
                                         endRemoveColumns();
                                 }
                             });
    m_connections << connect(source, &QAbstractItemModel::rowsAboutToBeMoved, this,
                             [this](const auto &from, int first, int last, const auto &to,
                                    int destination) {
                                 if (!from.isValid() && !to.isValid()) {
                                     if (m_ordered)
                                         beginResetModel();
                                     else
                                         beginMoveRows({}, first, last, {}, destination);
                                 }
                             })
                  << connect(source, &QAbstractItemModel::rowsMoved, this,
                             [this](const auto &from, int, int, const auto &to, int) {
                                 if (!from.isValid() && !to.isValid()) {
                                     if (m_ordered) {
                                         rebuildAfterSource();
                                         endResetModel();
                                     } else
                                         endMoveRows();
                                 }
                             });
    m_connections << connect(source, &QAbstractItemModel::columnsAboutToBeMoved, this,
                             [this](const auto &from, int first, int last, const auto &to,
                                    int destination) {
                                 if (!from.isValid() && !to.isValid()) {
                                     if (m_ordered)
                                         beginResetModel();
                                     else
                                         beginMoveColumns({}, first, last, {}, destination);
                                 }
                             })
                  << connect(source, &QAbstractItemModel::columnsMoved, this,
                             [this](const auto &from, int, int, const auto &to, int) {
                                 if (!from.isValid() && !to.isValid()) {
                                     if (m_ordered) {
                                         rebuildAfterSource();
                                         endResetModel();
                                     } else
                                         endMoveColumns();
                                 }
                             });
    m_connections << connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_sourceReset = true;
        beginResetModel();
    }) << connect(source, &QAbstractItemModel::modelReset, this, [this] {
        if (std::exchange(m_sourceReset, false)) {
            resetRoles();
            rebuildAfterSource();
            endResetModel();
        }
    });
    m_connections << connect(source, &QAbstractItemModel::layoutAboutToBeChanged, this, [this] {
        if (m_ordered) {
            beginResetModel();
            return;
        }
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
        if (m_ordered) {
            rebuildAfterSource();
            endResetModel();
            return;
        }
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
int EchoUiExtensionModel::lookupNative(const QString &key, bool column) const
{
    if (!sourceModel())
        return -1;
    int found = -1;
    const int count = column ? nativeColumns() : nativeRows();
    for (int i = 0; i < count; ++i) {
        bool match = false;
        if (column)
            match = sourceModel()->headerData(i, Qt::Horizontal, Qt::UserRole).toString() == key ||
                    sourceModel()->headerData(i, Qt::Horizontal).toString() == key;
        else
            for (auto role = m_roles.begin(); role != m_roles.end(); ++role)
                if ((role.value() == "resourceId" || role.value() == "id" ||
                     role.value() == "key") &&
                    sourceModel()->index(i, 0).data(role.key()).toString() == key)
                    match = true;
        if (match) {
            if (found >= 0)
                return -1;
            found = i;
        }
    }
    return found;
}
bool EchoUiExtensionModel::rebuildOrder(QString &error)
{
    const auto build = [this, &error](const QList<Entry> &entries, bool column,
                                      EchoUiModelOrder &order) {
        QList<QVariantMap> values;
        for (const auto &entry : entries) {
            auto value = entry.values;
            value["_echoUiKey"] = entry.owner + ':' + entry.contribution + ':' + entry.key;
            values.append(value);
        }
        return order.build(
            column ? nativeColumns() : nativeRows(), values,
            [this, column](const QString &key) { return lookupNative(key, column); }, error);
    };
    EchoUiModelOrder rows, columns;
    if (!build(m_rows, false, rows) || !build(m_columns, true, columns))
        return false;
    m_rowOrder = std::move(rows);
    m_columnOrder = std::move(columns);
    return true;
}
void EchoUiExtensionModel::rebuildAfterSource()
{
    QString error;
    if (!rebuildOrder(error)) {
        // A removed anchor must never leave indices pointing past the new source.
        QString ignored;
        m_rowOrder.build(nativeRows(), {}, [](const QString &) { return -1; }, ignored);
        m_columnOrder.build(nativeColumns(), {}, [](const QString &) { return -1; }, ignored);
    }
    if (m_orderError != error) {
        m_orderError = error;
        QTimer::singleShot(0, this, [this, error] {
            if (m_orderError == error)
                emit projectionErrorChanged(error);
        });
    }
}
bool EchoUiExtensionModel::setEntries(const QList<Entry> &rows, const QList<Entry> &columns,
                                      QString *error)
{
    if (m_rows == rows && m_columns == columns)
        return true;
    const auto sameStructure = [](const QList<Entry> &old, const QList<Entry> &next) {
        if (old.size() != next.size())
            return false;
        for (int i = 0; i < old.size(); ++i) {
            if (old[i].key != next[i].key || old[i].owner != next[i].owner ||
                old[i].contribution != next[i].contribution ||
                old[i].componentKey != next[i].componentKey)
                return false;
            for (const auto *field : {"operation", "position", "anchor", "target"})
                if (old[i].values.value(field) != next[i].values.value(field))
                    return false;
        }
        return true;
    };
    if (sameStructure(m_rows, rows) && sameStructure(m_columns, columns)) {
        m_rows = rows;
        m_columns = columns;
        if (rowCount() && columnCount())
            emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {});
        if (!columns.isEmpty() && columnCount())
            emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
        if (!rows.isEmpty() && rowCount())
            emit headerDataChanged(Qt::Vertical, 0, rowCount() - 1);
        return true;
    }
    const auto oldRows = m_rows, oldColumns = m_columns;
    const auto oldRowOrder = m_rowOrder, oldColumnOrder = m_columnOrder;
    m_rows = rows;
    m_columns = columns;
    QString reason;
    if (!rebuildOrder(reason)) {
        m_rows = oldRows;
        m_columns = oldColumns;
        m_rowOrder = oldRowOrder;
        m_columnOrder = oldColumnOrder;
        if (error)
            *error = reason;
        return false;
    }
    const auto rowOrder = m_rowOrder, columnOrder = m_columnOrder;
    m_rows = oldRows;
    m_columns = oldColumns;
    m_rowOrder = oldRowOrder;
    m_columnOrder = oldColumnOrder;
    beginResetModel();
    m_rows = rows;
    m_columns = columns;
    m_rowOrder = rowOrder;
    m_columnOrder = columnOrder;
    m_ordered = false;
    for (const auto &entry : rows + columns)
        m_ordered = m_ordered || entry.values.value("operation", "insert") != "insert" ||
                    entry.values.value("position", "append") != "append";
    endResetModel();
    return true;
}
int EchoUiExtensionModel::nativeSection(int position, bool column) const
{
    if (!m_ordered)
        return position >= 0 && position < (column ? nativeColumns() : nativeRows()) ? position
                                                                                     : -1;
    return (column ? m_columnOrder : m_rowOrder).nativeAt(position);
}
int EchoUiExtensionModel::extraSection(int position, bool column) const
{
    if (!m_ordered) {
        const int offset = position - (column ? nativeColumns() : nativeRows());
        return offset >= 0 && offset < (column ? m_columns.size() : m_rows.size()) ? offset : -1;
    }
    return (column ? m_columnOrder : m_rowOrder).extraAt(position);
}
int EchoUiExtensionModel::nativePosition(int section, bool column) const
{
    return m_ordered ? (column ? m_columnOrder : m_rowOrder).positionOfNative(section) : section;
}
int EchoUiExtensionModel::extraPosition(int section, bool column) const
{
    return m_ordered ? (column ? m_columnOrder : m_rowOrder).positionOfExtra(section)
                     : (column ? nativeColumns() : nativeRows()) + section;
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
    return at.isValid() && at.model() == this && sourceModel() &&
                   nativeSection(at.row(), false) >= 0 && nativeSection(at.column(), true) >= 0
               ? sourceModel()->index(nativeSection(at.row(), false),
                                      nativeSection(at.column(), true))
               : QModelIndex();
}
QModelIndex EchoUiExtensionModel::mapFromSource(const QModelIndex &at) const
{
    return at.isValid() && at.model() == sourceModel() && !at.parent().isValid()
               ? index(nativePosition(at.row(), false), nativePosition(at.column(), true))
               : QModelIndex();
}
int EchoUiExtensionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : (m_ordered ? m_rowOrder.count() : nativeRows() + m_rows.size());
}
int EchoUiExtensionModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid()
               ? 0
               : (m_ordered ? m_columnOrder.count() : nativeColumns() + m_columns.size());
}
const EchoUiExtensionModel::Entry *EchoUiExtensionModel::entry(const QModelIndex &at) const
{
    if (!at.isValid() || at.model() != this)
        return nullptr;
    const int row = extraSection(at.row(), false), column = extraSection(at.column(), true);
    if (row >= 0)
        return &m_rows[row];
    if (column >= 0)
        return &m_columns[column];
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
        return nativeSection(at.row(), false) >= 0
                   ? publicRow(nativeSection(at.row(), false))
                   : m_rows.value(extraSection(at.row(), false)).values;
    if (role == m_entryRole) {
        if (!extra)
            return QVariantMap();
        auto values = extra->values;
        values["_echoUiKey"] = extra->owner + ':' + extra->contribution + ':' + extra->key;
        return values;
    }
    if (extra) {
        if (role == Qt::DisplayRole && extra->values.contains("label"))
            return extra->values.value("label");
        const auto name = QString::fromUtf8(m_roles.value(role));
        if (extra->values.contains(name))
            return extra->values.value(name);
    }
    const auto source = mapToSource(at);
    return source.isValid() ? sourceModel()->data(source, role) : QVariant();
}
void EchoUiExtensionModel::sort(int column, Qt::SortOrder order)
{
    const int native = nativeSection(column, true);
    if (sourceModel() && native >= 0)
        sourceModel()->sort(native, order);
}
bool EchoUiExtensionModel::setData(const QModelIndex &at, const QVariant &value, int role)
{
    const auto source = mapToSource(at);
    return source.isValid() && sourceModel()->setData(source, value, role);
}
Qt::ItemFlags EchoUiExtensionModel::flags(const QModelIndex &at) const
{
    const auto *extra = entry(at);
    if (extra && !extra->enabled)
        return Qt::NoItemFlags;
    const auto source = mapToSource(at);
    return source.isValid() ? sourceModel()->flags(source)
           : extra          ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                            : Qt::NoItemFlags;
}
QVariant EchoUiExtensionModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    const bool column = orientation == Qt::Horizontal;
    const int native = nativeSection(section, column), extra = extraSection(section, column);
    const auto &entries = column ? m_columns : m_rows;
    if (extra >= 0) {
        if (role == m_keyRole)
            return entries[extra].owner + ':' + entries[extra].contribution + ':' +
                   entries[extra].key;
        if (role == Qt::DisplayRole && entries[extra].values.contains("label"))
            return entries[extra].values.value("label");
    }
    return native >= 0 && sourceModel() ? sourceModel()->headerData(native, orientation, role)
                                        : QVariant();
}
QHash<int, QByteArray> EchoUiExtensionModel::roleNames() const
{
    return m_roles;
}
} // namespace EchoMusic
