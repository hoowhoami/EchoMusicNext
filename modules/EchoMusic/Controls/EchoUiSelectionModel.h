#pragma once
#include <QItemSelectionModel>
#include <QPointer>

namespace EchoMusic
{
class EchoUiExtensionModel;
// A view-local selection facade. The host's shared selection object always
// keeps its source model; extension cells never become source business indices.
class EchoUiSelectionModel final : public QItemSelectionModel
{
public:
    EchoUiSelectionModel(EchoUiExtensionModel &, QObject *);
    void setSourceSelection(QItemSelectionModel *);
    void synchronize();

private:
    struct Extra
    {
        QString key;
        int column = 0;
        QPersistentModelIndex sourceRow;
        QString columnKey;
    };
    Extra extra(const QModelIndex &) const;
    QModelIndex resolve(const Extra &) const;
    void captureExtras();
    EchoUiExtensionModel &m_projection;
    QPointer<QItemSelectionModel> m_source;
    QList<QMetaObject::Connection> m_connections;
    QList<Extra> m_extras;
    Extra m_currentExtra;
    bool m_updating = false, m_resetting = false;
};
} // namespace EchoMusic
