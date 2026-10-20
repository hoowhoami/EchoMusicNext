#include "AppContext_p.h"
#include "AlbumDetailService.h"
#include "ArtistDetailService.h"
#include "CloudService.h"
#include "DetailsService.h"
#include "ExploreService.h"
#include "FavoritesService.h"
#include "HistoryService.h"
#include "ListenTogetherUi.h"
#include "PlaylistDetailService.h"
#include "PluginManager.h"
#include "PurchasedService.h"
#include "RecognitionService.h"
#include "RouteController.h"
#include "ShareResolveService.h"
#include "WindowController.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSet>
#include <QStandardPaths>
#include <QProcess>
#include <QUrlQuery>

namespace EchoMusic
{
void AppContext::registerRoutes()
{
    auto &router = *m_router;
    auto &window = *m_window;
    auto &pluginManager = *m_pluginManager;
    m_playlistDetails->registerRoutes(router);
    m_favorites->registerRoutes(router);
    m_cloud->registerRoutes(router);
    m_purchased->registerRoutes(router);
    m_details->registerRoutes(router);
    m_albumDetails->registerRoutes(router);
    m_artistDetails->registerRoutes(router);
    m_history->registerRoutes(router);
    m_recognition->registerRoutes(router);
    m_explore->registerRoutes(router);
    m_togetherUi->registerRoutes(router);
    m_shareResolver->registerRoutes(router);
    router.registerPage({"themes",
                         "/main/themes",
                         QUrl("qrc:/qt/qml/EchoMusic/Themes/ThemeCenterPage.qml"),
                         {},
                         false,
                         false,
                         false,
                         {{"windowService", QVariant::fromValue(&window)}}});
    router.registerPage({"plugin-management",
                         "/main/settings/plugins",
                         QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginManagementPage.qml"),
                         {"view"},
                         true,
                         false,
                         false,
                         {{"manager", QVariant::fromValue(&pluginManager)}}});
    router.registerPage({"plugin-share-resolve",
                         "/main/settings/plugins/share",
                         QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginShareResolvePage.qml"),
                         {},
                         false,
                         false,
                         false,
                         {{"manager", QVariant::fromValue(&pluginManager)}}});
    // Preferences and every route recipe are ready before resolving the first card.
    if (!useStartup)
        router.navigateHome();
}
} // namespace EchoMusic
