#include "SidebarRouteCoordinator.h"
#include "LibraryController.h"
#include "RouteController.h"
#include "UserPlaylistModel.h"

namespace EchoMusic
{
SidebarRouteCoordinator::SidebarRouteCoordinator(LibraryController &library,
                                                 UserPlaylistModel &source, RouteController &router,
                                                 QObject *parent)
    : QObject(parent), m_library(library), m_source(source), m_router(router)
{
    connect(&router, &RouteController::routeChanged, this, &SidebarRouteCoordinator::routeChanged);
    connect(&source, &QAbstractItemModel::modelReset, this, &SidebarRouteCoordinator::synchronize);
    connect(&source, &QAbstractItemModel::rowsInserted, this,
            &SidebarRouteCoordinator::synchronize);
    connect(&source, &QAbstractItemModel::rowsRemoved, this, &SidebarRouteCoordinator::synchronize);
    connect(&source, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                // Covers, titles and counts can update while the user browses
                // the other tab. Only identity/provenance changes need a resync.
                if (roles.isEmpty() || roles.contains(UserPlaylistModel::KindRole) ||
                    roles.contains(UserPlaylistModel::GlobalIdRole) ||
                    roles.contains(UserPlaylistModel::CreatorGlobalIdRole) ||
                    roles.contains(UserPlaylistModel::SourceCodeRole))
                    synchronize();
            });
    routeChanged();
}
void SidebarRouteCoordinator::routeChanged()
{
    const auto *current = m_router.current();
    const auto name = current ? current->name() : QString();
    const auto resource = current ? current->resourceId() : QString();
    if (name == m_routeName && resource == m_resourceId)
        return;
    m_routeName = name;
    m_resourceId = resource;
    m_revealedTab = -1;
    synchronize();
}
void SidebarRouteCoordinator::synchronize()
{
    const auto session = m_source.sessionRevision();
    if (session != m_sessionRevision) {
        m_sessionRevision = session;
        m_revealedTab = -1;
    }
    const auto *current = m_router.current();
    if (!current || current->name() != QStringLiteral("playlist-detail"))
        return;
    for (int row = 0; row < m_source.rowCount(); ++row) {
        const auto index = m_source.index(row);
        if (index.data(UserPlaylistModel::SourceCodeRole).toUInt() == 2)
            continue;
        const auto kind = index.data(UserPlaylistModel::KindRole).toString();
        if (kind != QStringLiteral("owned") && kind != QStringLiteral("collected"))
            continue;
        const bool owned = kind == QStringLiteral("owned");
        const auto id = index
                            .data(owned ? UserPlaylistModel::GlobalIdRole
                                        : UserPlaylistModel::CreatorGlobalIdRole)
                            .toString();
        if (!id.isEmpty() && id == current->resourceId()) {
            const int tab = owned ? 0 : 1;
            // Reset/insert notifications during refresh reveal the same route
            // again. Remember its membership, not the user's selected tab, so
            // manual browsing survives until navigation or provenance changes.
            if (m_revealedTab != tab) {
                m_revealedTab = tab;
                m_library.setTab(tab);
            }
            return;
        }
    }
}
} // namespace EchoMusic
