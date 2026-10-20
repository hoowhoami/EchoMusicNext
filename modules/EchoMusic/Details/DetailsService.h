#pragma once
#include "SongDetailContext.h"
#include "PlaybackDiscussion.h"
#include "MvDetailContext.h"
#include "VideoCollectionService.h"
#include "WindowController.h"
#include <optional>
namespace EchoMusic
{
class ShellController;
class DetailsService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::PlaybackDiscussion *playbackDiscussion READ playbackDiscussion CONSTANT)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    DetailsService(ApiClient &, PlaybackService &, RouteController &, bool network = true,
                   QObject *parent = nullptr, CommentAuthorService *authors = nullptr,
                   WindowController *window = nullptr);
    ~DetailsService() override;
    Q_INVOKABLE EchoMusic::SongDetailContext *songFor(EchoMusic::RouteContext *);
    Q_INVOKABLE EchoMusic::MvDetailContext *mvFor(EchoMusic::RouteContext *);
    PlaybackDiscussion *playbackDiscussion()
    {
        return &m_discussion;
    }
    VideoCollectionService *videoCollection()
    {
        return &m_collection;
    }
    void setNetworkPreferences(NetworkPreferences *);
    void attachShell(ShellController &);
    void shutdown();
signals:
    void notice(const QString &, bool warning);

private:
    QPointer<NetworkPreferences> m_networkPreferences;
    QMetaObject::Connection m_entryConnection;
    std::optional<PlaybackTrack> m_entryTrack;
    quint64 m_entrySession = 0;
    WindowController *m_window;
    CommentAuthorService *m_authors;
    ApiClient &m_api;
    PlaybackService &m_playback;
    VideoCollectionService m_collection;
    PlaybackDiscussion m_discussion;
    QHash<RouteContext *, QPointer<SongDetailContext>> m_songs;
    QHash<RouteContext *, QPointer<MvDetailContext>> m_videos;
    bool m_network, m_closed = false;
};
} // namespace EchoMusic
