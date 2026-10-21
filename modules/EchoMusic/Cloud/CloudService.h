#pragma once
#include "ApiClient.h"
#include "CloudUploadService.h"
#include "RouteController.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include "SongMetadataService.h"
#include <QHash>
#include <QJsonObject>
#include <QList>
namespace EchoMusic
{
class PlaybackService;
class CloudServiceTestAccess;
/// Cloud catalog owner: listing, quota and single/batch deletion.
/// Order by added time descending, then sort index ascending.
/// CloudUploadService separately owns uploads, including background work.
class CloudService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::PlaybackService *playback READ playback CONSTANT)
    Q_PROPERTY(EchoMusic::CloudUploadService *upload READ upload CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(quint64 totalCount READ totalCount NOTIFY changed)
    /// -1 keeps an unknown provider counter unknown instead of a fake 0.
    Q_PROPERTY(qint64 storageUsedBytes READ storageUsedBytes NOTIFY changed)
    Q_PROPERTY(qint64 storageAvailableBytes READ storageAvailableBytes NOTIFY changed)
    Q_PROPERTY(qint64 storageCapacityBytes READ storageCapacityBytes NOTIFY changed)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    CloudService(ApiClient &, PlaybackService &, SongMetadataService &, RouteController &,
                 bool networkEnabled, QObject *parent = nullptr);
    ~CloudService() override;
    CloudUploadService *upload()
    {
        return &m_upload;
    }
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    PlaybackService *playback()
    {
        return &m_playback;
    }
    bool loading() const
    {
        return m_listRequest != 0;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    bool hasMore() const;
    bool signedIn() const
    {
        return m_api.currentAccountUserId() != 0;
    }
    bool busy() const
    {
        return m_deleteRequest != 0;
    }
    quint64 totalCount() const
    {
        return m_total;
    }
    qint64 storageUsedBytes() const
    {
        return m_usedBytes;
    }
    qint64 storageAvailableBytes() const
    {
        return m_availableBytes;
    }
    qint64 storageCapacityBytes() const
    {
        return m_capacityBytes;
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore();
    /// Rows are removable only with a non-zero cloud file ID (kv_id).
    /// The wire uses a numeric string that Rust parses to u64.
    Q_INVOKABLE bool canRemoveSong(const QString &resourceId) const;
    /// Deletion by projected row id (resourceId). Rows without a cloud file ID
    /// are skipped; a successful ack is followed by a re-read, never trusted
    /// alone (the provider also answers success for non-existent IDs).
    Q_INVOKABLE bool removeSongs(const QStringList &ids);
    void shutdown();

signals:
    void changed();
    void notice(const QString &message, bool warning);

private:
    friend class CloudServiceTestAccess;
    void activate(RouteContext *context);
    void reset();
    void requestPage(quint32 page);
    void project();
    void updateCounters(const QJsonObject &counters);
    void accept(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &bytes);
    void finishDelete(const QByteArray &bytes);
    void cancelAll();
    void completeSongs(const QList<QJsonObject> &items);
    void acceptMetadata(quint64 ticket, const QJsonArray &values);
    void cancelMetadata();
    /// Corrected facts from the playing file's tags reach the cloud row for
    /// the same upload, so masked provider names stop shadowing real ones.
    void applyPlaybackFacts(const QJsonObject &facts);
    /// Merge corrected file facts into one cloud song snapshot (source level);
    /// returns whether the song changed.
    bool applyFactsToSong(QJsonObject &song, const QJsonObject &facts) const;
    ApiClient &m_api;
    PlaybackService &m_playback;
    SongMetadataService &m_metadata;
    RouteController &m_router;
    CloudUploadService m_upload;
    SongCatalogModel m_songs;
    SongCatalogActions m_actions;
    QList<QJsonObject> m_items;
    QHash<QString, ApiClient::CloudDeleteTarget> m_targets;
    struct MetadataPending
    {
        quint64 session, generation;
    };
    QHash<quint64, MetadataPending> m_metadataRequests;
    quint64 m_metadataGeneration = 0;
    QList<ApiClient::CloudDeleteTarget> m_deleteTargets;
    QStringList m_deleteIds;
    int m_deleteRequested = 0;
    quint64 m_total = 0, m_received = 0;
    quint64 m_listRequest = 0, m_listSession = 0;
    quint64 m_deleteRequest = 0, m_deleteSession = 0;
    quint32 m_page = 0;
    quint32 m_arrival = 0;
    qint64 m_usedBytes = -1, m_availableBytes = -1, m_capacityBytes = -1;
    QString m_instance, m_error;
    bool m_network = false, m_active = false, m_closed = false, m_exhausted = false;
};
} // namespace EchoMusic
