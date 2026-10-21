#pragma once
#include "ApiClient.h"
#include "HistoryStatsShare.h"
#include "RouteController.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include "SongMetadataService.h"
namespace EchoMusic
{
/// Local playback history page service. Reads and writes go through
/// PlaybackService's storage worker; this object only owns presentation state.
class HistoryService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(EchoMusic::SongCatalogModel *songs READ songs CONSTANT)
    Q_PROPERTY(EchoMusic::SongCatalogActions *actions READ actions CONSTANT)
    Q_PROPERTY(EchoMusic::HistoryStatsShare *statsShare READ statsShare CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    HistoryService(ApiClient &, PlaybackService &, SongMetadataService &, RouteController &,
                   bool networkEnabled, QObject *parent = nullptr);
    ~HistoryService() override;
    SongCatalogModel *songs()
    {
        return &m_songs;
    }
    SongCatalogActions *actions()
    {
        return &m_actions;
    }
    bool loading() const
    {
        return m_loading;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QVariantMap stats() const
    {
        return m_stats;
    }
    HistoryStatsShare *statsShare()
    {
        return &m_statsShare;
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void clear();
    void shutdown();
signals:
    void changed();
    void notice(const QString &message);

private:
    friend class HistoryServiceTestAccess;
    void accept(const QJsonArray &entries);
    void fail(const QString &message);
    void completeSongs();
    void acceptMetadata(quint64 ticket, const QJsonArray &values);
    /// Corrected playing-file tags refresh matching history rows in place.
    void applyPlaybackFacts(const QJsonObject &facts);
    void reapplyPlaybackFacts();
    /// Merge one facts snapshot into a history track row; appends the matching
    /// presentation when the row changed.
    bool applyFactsToRow(const QJsonObject &facts, QJsonObject &track, QJsonArray &projected);
    void cancelMetadata();
    ApiClient &m_api;
    PlaybackService &m_playback;
    SongMetadataService &m_metadata;
    SongCatalogModel m_songs;
    SongCatalogActions m_actions;
    HistoryStatsShare m_statsShare;
    QJsonArray m_entries;
    QHash<quint64, quint64> m_metadataRequests;
    quint64 m_metadataGeneration = 0, m_metadataSession = 0;
    bool m_loading = false, m_closed = false, m_active = false, m_network = false;
    QString m_error;
    QVariantMap m_stats;
};
} // namespace EchoMusic
