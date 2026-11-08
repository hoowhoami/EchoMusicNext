#include "EchoUiSelectionModel.h"
#include "EchoUiExtensionModel.h"
#include <QScopedValueRollback>

namespace EchoMusic
{
EchoUiSelectionModel::EchoUiSelectionModel(EchoUiExtensionModel &projection, QObject *owner)
    : QItemSelectionModel(&projection, owner), m_projection(projection)
{
    connect(this, &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &deselected) {
                if (m_updating || m_resetting || !m_source ||
                    m_source->model() != m_projection.sourceModel())
                    return;
                QScopedValueRollback guard(m_updating, true);
                m_source->select(m_projection.mapSelectionToSource(deselected), Deselect);
                m_source->select(m_projection.mapSelectionToSource(selected), Select);
            });
    connect(this, &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (m_updating || m_resetting || !m_source ||
            m_source->model() != m_projection.sourceModel())
            return;
        const auto source = m_projection.mapToSource(current);
        if (source.isValid() || !current.isValid()) {
            QScopedValueRollback guard(m_updating, true);
            m_source->setCurrentIndex(source, NoUpdate);
        }
    });
    connect(&projection, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        captureExtras();
        m_resetting = true;
    });
    connect(&projection, &QAbstractItemModel::modelReset, this, [this] {
        m_resetting = false;
        synchronize();
    });
}
void EchoUiSelectionModel::setSourceSelection(QItemSelectionModel *source)
{
    if (source == this || m_source == source)
        return;
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    m_source = source;
    m_extras.clear();
    m_currentExtra = {};
    if (source) {
        const auto changed = [this] {
            if (!m_updating && !m_resetting)
                captureExtras();
            synchronize();
        };
        m_connections << connect(source, &QItemSelectionModel::selectionChanged, this, changed)
                      << connect(source, &QItemSelectionModel::currentChanged, this, changed)
                      << connect(source, &QItemSelectionModel::modelChanged, this, changed)
                      << connect(source, &QObject::destroyed, this, [this] { synchronize(); });
    }
    synchronize();
}
EchoUiSelectionModel::Extra EchoUiSelectionModel::extra(const QModelIndex &at) const
{
    if (!at.isValid() || m_projection.mapToSource(at).isValid())
        return {};
    const auto names = m_projection.roleNames();
    int role = -1;
    for (auto it = names.begin(); it != names.end(); ++it)
        if (it.value() == "echoUiKey")
            role = it.key();
    const int nativeColumn = m_projection.nativeSection(at.column(), true);
    return {at.data(role).toString(), nativeColumn >= 0 ? nativeColumn : at.column(),
            m_projection.nativeSection(at.row(), false) >= 0 && m_projection.sourceModel()
                ? m_projection.sourceModel()->index(m_projection.nativeSection(at.row(), false), 0)
                : QModelIndex(),
            m_projection.headerData(at.column(), Qt::Horizontal, role).toString()};
}
QModelIndex EchoUiSelectionModel::resolve(const Extra &value) const
{
    if (value.key.isEmpty())
        return {};
    int column = m_projection.nativePosition(value.column, true);
    if (!value.columnKey.isEmpty()) {
        column = -1;
        const auto names = m_projection.roleNames();
        int keyRole = -1;
        for (auto it = names.begin(); it != names.end(); ++it)
            if (it.value() == "echoUiKey")
                keyRole = it.key();
        for (int i = 0; i < m_projection.columnCount(); ++i)
            if (m_projection.headerData(i, Qt::Horizontal, keyRole).toString() == value.columnKey) {
                column = i;
                break;
            }
    }
    for (int row = 0; column >= 0 && row < m_projection.rowCount(); ++row) {
        if (m_projection.extraSection(row, false) < 0)
            continue;
        const auto at = m_projection.index(row, column);
        if (extra(at).key == value.key)
            return at;
    }
    if (value.sourceRow.isValid())
        for (int column = 0; column < m_projection.columnCount(); ++column) {
            if (m_projection.extraSection(column, true) < 0)
                continue;
            const auto at = m_projection.index(
                m_projection.nativePosition(value.sourceRow.row(), false), column);
            if (extra(at).key == value.key)
                return at;
        }
    return {};
}
void EchoUiSelectionModel::captureExtras()
{
    m_extras.clear();
    for (const auto &at : selectedIndexes()) {
        auto value = extra(at);
        if (!value.key.isEmpty())
            m_extras.append(value);
    }
    m_currentExtra = extra(currentIndex());
}
void EchoUiSelectionModel::synchronize()
{
    if (m_updating || m_resetting)
        return;
    QScopedValueRollback guard(m_updating, true);
    QItemSelection selected;
    QModelIndex current;
    if (m_source && m_source->model() == m_projection.sourceModel()) {
        selected = m_projection.mapSelectionFromSource(m_source->selection());
        current = m_projection.mapFromSource(m_source->currentIndex());
    }
    for (const auto &value : m_extras) {
        const auto at = resolve(value);
        if (at.isValid())
            selected.select(at, at);
    }
    const auto extraCurrent = resolve(m_currentExtra);
    select(selected, ClearAndSelect);
    setCurrentIndex(extraCurrent.isValid() ? extraCurrent : current, NoUpdate);
}
} // namespace EchoMusic
