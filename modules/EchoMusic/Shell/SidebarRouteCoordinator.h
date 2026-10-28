#pragma once
#include <QObject>
#include <QString>

namespace EchoMusic
{
class LibraryController;
class RouteController;
class UserPlaylistModel;
/// Reveal the current playlist's catalog section after any navigation, including
/// history traversal and a catalog that arrives after the route. LibraryController
/// owns the selected tab; repeated catalog loads do not override manual browsing.
class SidebarRouteCoordinator final : public QObject
{
    Q_OBJECT
public:
    SidebarRouteCoordinator(LibraryController &library, UserPlaylistModel &source,
                            RouteController &router, QObject *parent = nullptr);

private:
    void routeChanged();
    void synchronize();
    LibraryController &m_library;
    UserPlaylistModel &m_source;
    RouteController &m_router;
    QString m_routeName, m_resourceId;
    quint64 m_sessionRevision = 0;
    int m_revealedTab = -1;
};
} // namespace EchoMusic
