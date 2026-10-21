#pragma once
#include "ProfileService.h"
#include "ApiClient.h"
#include "RouteController.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include "ShellSettings.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
namespace EchoMusic
{
/// One artist in the router's bounded cache. The context owns the detail and
/// every song page; presentation and playback stay in the shared Music types.
class ArtistDetailContext final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a mounted route")
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *albums READ albums CONSTANT)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY changed)
    Q_PROPERTY(QString sort READ sort NOTIFY changed)
    Q_PROPERTY(QString albumSort READ albumSort NOTIFY changed)
    Q_PROPERTY(bool albumsLoading READ albumsLoading NOTIFY changed)
    Q_PROPERTY(bool albumsLoaded READ albumsLoaded NOTIFY changed)
    Q_PROPERTY(QString albumError READ albumError NOTIFY changed)
    Q_PROPERTY(bool detailLoading READ detailLoading NOTIFY changed)
    Q_PROPERTY(bool songsLoading READ songsLoading NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool interactionAvailable READ interactionAvailable NOTIFY changed)
    Q_PROPERTY(bool followed READ followed NOTIFY changed)
    Q_PROPERTY(bool followBusy READ followBusy NOTIFY changed)
    Q_PROPERTY(bool followPending READ followPending NOTIFY changed)
    Q_PROPERTY(bool blocked READ blocked NOTIFY changed)
    Q_PROPERTY(bool blockBusy READ blockBusy NOTIFY changed)
public:
    void attachProfile(ProfileService &profile);
    bool interactionAvailable() const
    {
        return m_profile && m_api.currentAccountUserId() > 0 && m_detail["id"].toInteger() > 0;
    }
    bool followPending() const
    {
        return m_profile && m_profile->artistPending(quint64(m_detail["id"].toInteger()));
    }
    bool followed() const
    {
        return m_profile && m_profile->artistFollowed(quint64(m_detail["id"].toInteger()));
    }
    bool followBusy() const
    {
        return !m_profile || !m_profile->artistFollowsLoaded() ||
               m_profile->artistPending(quint64(m_detail["id"].toInteger()));
    }
    bool blocked() const
    {
        return m_profile &&
               m_profile->blacklist()->singerStatus(quint64(m_detail["id"].toInteger())) ==
                   ContentBlacklistService::SongStatus::Present;
    }
    bool blockBusy() const
    {
        return !m_profile || m_profile->blacklist()->singerBusy();
    }
    Q_INVOKABLE void toggleFollow()
    {
        if (interactionAvailable() && m_route.active())
            m_profile->toggleArtistFollow(quint64(m_detail["id"].toInteger()));
    }
    Q_INVOKABLE void toggleBlock()
    {
        if (interactionAvailable() && m_route.active())
            m_profile->blacklist()->toggleSinger(quint64(m_detail["id"].toInteger()),
                                                 m_detail["name"].toString());
    }
    ArtistDetailContext(ApiClient &, PlaybackService &, RouteContext &, ShellSettings &,
                        bool networkEnabled);
    ~ArtistDetailContext() override;
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    QVariantMap detail() const
    {
        return m_detail.toVariantMap();
    }
    QString sort() const
    {
        return m_sort;
    }
    SongCatalogModel *albums()
    {
        return &m_albums;
    }
    QString albumSort() const
    {
        return m_albumSort;
    }
    bool albumsLoading() const;
    bool albumsLoaded() const
    {
        return m_albumComplete;
    }
    QString albumError() const
    {
        return m_albumError;
    }
    bool detailLoading() const;
    bool songsLoading() const;
    bool complete() const
    {
        return m_complete;
    }
    QString errorMessage() const
    {
        return m_detail.isEmpty() ? m_detailError : m_songError;
    }
    Q_INVOKABLE void setSort(const QString &sort);
    Q_INVOKABLE void ensureAlbums();
    Q_INVOKABLE void setAlbumSort(const QString &sort);
    Q_INVOKABLE void reload();
    void shutdown();
signals:
    void notice(const QString &message, bool warning);
    void changed();

private:
    friend class ArtistDetailContextTestAccess;
    struct Pending
    {
        quint32 page;
        quint64 session, generation;
    };
    struct AlbumPending
    {
        quint32 page;
        quint64 session, generation;
    };
    void activate();
    void cancel();
    void cancelSongRequests();
    void requestPage(quint32 page);
    void requestBatch();
    void consumeBatch();
    void requestAlbumPage(quint32 page);
    void requestAlbumBatch();
    void consumeAlbumBatch();
    void finishAlbumPage(quint32 page, const QJsonObject &value, const QString &error = {});
    void cancelAlbumRequests();
    void accept(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    void finishPage(quint32 page, const QJsonObject &value, const QString &error = {});
    ProfileService *m_profile = nullptr;
    ApiClient &m_api;
    RouteContext &m_route;
    ShellSettings &m_settings;
    SongCatalogModel m_songs;
    SongCatalogActions m_actions;
    SongCatalogModel m_albums;
    QJsonObject m_detail;
    QHash<quint64, Pending> m_requests;
    QMap<quint32, QJsonObject> m_pages;
    QMap<quint32, QString> m_errors;
    QHash<quint64, AlbumPending> m_albumRequests;
    QMap<quint32, QJsonObject> m_albumPages;
    QMap<quint32, QString> m_albumErrors;
    QJsonArray m_albumCards;
    quint64 m_generation = 0;
    quint32 m_nextPage = 1, m_pageSize = 100;
    QString m_sort = QStringLiteral("new");
    QString m_albumSort = QStringLiteral("new");
    QString m_detailError, m_songError, m_albumError;
    quint32 m_albumNextPage = 1, m_albumPageSize = 30;
    bool m_network, m_closed = false, m_complete = false, m_albumComplete = false,
                    m_albumsRequested = false;
};
} // namespace EchoMusic
