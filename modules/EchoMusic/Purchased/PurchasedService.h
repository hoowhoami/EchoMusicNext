#pragma once
#include "ApiClient.h"
#include "RouteController.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include <QHash>
#include <QJsonArray>
namespace EchoMusic
{
class PlaybackService;
class PurchasedServiceTestAccess;
/// Purchased songs/albums consume Rust domain rows. This owner keeps independent
/// pagination and errors; playback uses the queue:purchased context.
class PurchasedService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *albums READ albums CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback CONSTANT)
    Q_PROPERTY(int tab READ tab NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool songsLoading READ songsLoading NOTIFY changed)
    Q_PROPERTY(bool albumsLoading READ albumsLoading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(quint64 songsTotal READ songsTotal NOTIFY changed)
    Q_PROPERTY(quint64 albumsTotal READ albumsTotal NOTIFY changed)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    PurchasedService(ApiClient &, PlaybackService &, RouteController &, bool networkEnabled,
                     QObject *parent = nullptr);
    ~PurchasedService() override;
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    SongCatalogModel *albums()
    {
        return &m_albums;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    PlaybackService *playback()
    {
        return &m_playback;
    }
    int tab() const
    {
        return m_tab;
    }
    bool loading() const
    {
        return !m_pending.isEmpty() || m_songContinuationQueued;
    }
    QString errorMessage() const
    {
        return m_tab == 0 ? m_songError : m_albumError;
    }
    bool songsLoading() const
    {
        return hasPending(PendingKind::Songs) || m_songContinuationQueued;
    }
    bool albumsLoading() const
    {
        return hasPending(PendingKind::Albums);
    }
    bool hasMore() const;
    bool signedIn() const
    {
        return m_api.currentAccountUserId() != 0;
    }
    quint64 songsTotal() const
    {
        return m_songTotal;
    }
    quint64 albumsTotal() const
    {
        return m_albumTotal;
    }
    Q_INVOKABLE void selectTab(int tab);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore();
    void shutdown();

signals:
    void changed();

private:
    friend class PurchasedServiceTestAccess;
    enum class PendingKind
    {
        Songs,
        Albums
    };
    struct Pending
    {
        PendingKind kind;
        quint64 session;
        quint32 page = 1;
    };
    void activate(RouteContext *context);
    void reset();
    void loadTab();
    void continueSongs();
    void request(PendingKind kind, quint32 page);
    bool hasPending(PendingKind kind) const;
    void cancelAll();
    void accept(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    static const QStringList &tabs();
    ApiClient &m_api;
    PlaybackService &m_playback;
    RouteController &m_router;
    SongCatalogModel m_songs, m_albums;
    SongCatalogActions m_actions;
    QHash<quint64, Pending> m_pending;
    QJsonArray m_albumRows;
    quint64 m_songTotal = 0, m_songReceived = 0;
    quint64 m_albumTotal = 0, m_albumReceived = 0;
    quint32 m_songPage = 0, m_albumPage = 0;
    quint64 m_generation = 0;
    int m_tab = 0;
    QString m_instance, m_songError, m_albumError;
    bool m_network = false, m_active = false, m_closed = false;
    bool m_songExhausted = false, m_albumExhausted = false;
    bool m_songContinuationQueued = false;
};
} // namespace EchoMusic
