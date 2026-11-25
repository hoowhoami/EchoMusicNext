#pragma once
#include "ApiClient.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include <QPointer>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class RouteContext;
/// One appendable SQL source shared by browsing and playback. The discovery
/// service bounds retained feeds; only visible pages/current playback can fetch.
class RecommendationFeed final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DiscoveryService")
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString subtitle READ subtitle CONSTANT)
    Q_PROPERTY(QString queueKey READ queueKey CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogModel *collection READ collection CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(int status READ status NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY changed)
public:
    RecommendationFeed(ApiClient &, PlaybackService &, quint32 cardId, QString tag, QString title,
                       QString subtitle, QObject *parent);
    ~RecommendationFeed() override;
    static QString key(quint32 cardId, const QString &tag);
    QString title() const
    {
        return m_title;
    }
    QString subtitle() const
    {
        return m_subtitle;
    }
    QString queueKey() const
    {
        return key(m_cardId, m_tag);
    }
    SongCatalogModel *collection()
    {
        return &m_collection;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    int status() const
    {
        return m_status;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    bool hasMore() const
    {
        return m_hasMore && m_collection.storageReady();
    }
    bool loadingMore() const
    {
        return (m_request || m_pendingRefresh) && m_collection.totalCount() > 0;
    }
    bool preparingPlay() const
    {
        return m_playRead || m_playAfterLoad;
    }
    bool inUse() const;
    void addViewer(RouteContext *);
    void setNetworkEnabled(bool enabled);
    void playPreview(SongCatalogModel *, int first, int count, int songOffset = -1);
    void cancelPlay();
    void shutdown();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void retry();
signals:
    void changed();

private:
    friend class DiscoveryServiceTestAccess;
    bool viewed() const;
    bool current() const;
    bool request(bool refresh = false);
    void updateActivity();
    void cancel();
    void accept(quint64, const QByteArray &);
    void fail(quint64, const QString &);
    void playLoaded();
    ApiClient &m_api;
    PlaybackService &m_playback;
    SongCatalogModel m_collection;
    SongCatalogActions m_actions;
    const quint32 m_cardId;
    const QString m_tag, m_title, m_subtitle;
    QList<QPointer<RouteContext>> m_views;
    QPointer<QObject> m_playRead;
    QPointer<PlaybackQueueModel> m_queueAtRequest;
    quint64 m_request = 0, m_session = 0, m_queueRevision = 0, m_playSequence = 0;
    quint32 m_page = 1;
    int m_status = 0;
    QString m_error, m_startId;
    bool m_hasMore = true, m_refresh = false, m_network = false, m_closed = false,
         m_playAfterLoad = false, m_updating = false, m_pendingRefresh = false;
};
} // namespace EchoMusic
