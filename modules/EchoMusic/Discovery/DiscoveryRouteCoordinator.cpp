#include "DiscoveryRouteCoordinator.h"
#include "DiscoveryService.h"
#include "RouteController.h"
namespace EchoMusic
{
DiscoveryRouteCoordinator::DiscoveryRouteCoordinator(DiscoveryService &service,
                                                     RouteController &router, bool networkEnabled,
                                                     QObject *parent)
    : QObject(parent)
{
    for (const auto &entry :
         {std::pair{"recommendation-detail", "RecommendationDetailPage"},
          std::pair{"home", "HomePage"}, std::pair{"recommend-songs", "DailyRecommendPage"},
          std::pair{"ranking", "RankingPage"}, std::pair{"free-listen", "FreeListenPage"},
          std::pair{"discover-flow", "DiscoverFlowPage"},
          std::pair{"personal-fm", "PersonalFmPage"}}) {
        const auto name = QString::fromLatin1(entry.first);
        router.registerPage(
            {name,
             name == "recommendation-detail" ? "/main/recommendation-detail/:id"
             : name == "home"                ? "/main/home"
             : name == "ranking"             ? "/main/ranking"
             : name == "free-listen"         ? "/main/free-listen"
             : name == "personal-fm"         ? "/main/personal-fm"
             : name == "discover-flow"       ? "/main/discover-flow"
                                             : "/main/recommend",
             QUrl("qrc:/qt/qml/EchoMusic/Discovery/" + QString::fromLatin1(entry.second) + ".qml"),
             {},
             true,
             true,
             false,
             {{"service", QVariant::fromValue(&service)}}});
    }
    connect(&router, &RouteController::pageActivated, this,
            [&service, networkEnabled](RouteContext *context) {
                if (context->name() == "recommendation-detail")
                    service.setDetailCard(context->resourceId());
                service.setPage(context->name(), context->instanceId(), networkEnabled);
            });
}
} // namespace EchoMusic
