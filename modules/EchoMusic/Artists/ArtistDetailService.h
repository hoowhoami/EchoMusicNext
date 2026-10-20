#pragma once
#include "ArtistDetailContext.h"
#include <QHash>
#include <QPointer>
namespace EchoMusic
{
/// Factory follows the router's bounded cache. Contexts are children of their
/// route; there is no second unbounded resource cache or QML-owned network task.
class ArtistDetailService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    void attachProfile(ProfileService &profile)
    {
        m_profile = &profile;
    }
    ArtistDetailService(ApiClient &, PlaybackService &, RouteController &, ShellSettings &,
                        bool networkEnabled = true, QObject *parent = nullptr);
    ~ArtistDetailService() override;
    Q_INVOKABLE EchoMusic::ArtistDetailContext *contextFor(EchoMusic::RouteContext *route);
    void shutdown();

private:
    ProfileService *m_profile = nullptr;
    ApiClient &m_api;
    PlaybackService &m_playback;
    ShellSettings &m_settings;
    QHash<RouteContext *, QPointer<ArtistDetailContext>> m_contexts;
    bool m_network, m_closed = false;
};
} // namespace EchoMusic
