#pragma once
#include "ApiClient.h"
#include <QSet>
#include <QHash>
#include <QJsonArray>

namespace EchoMusic
{
class PlaylistMutationServiceTestAccess;
/// GUI-thread single writer. API success completes the mutation; independent
/// refreshes update canonical models without changing that result or replaying writes.
class PlaylistMutationService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    enum class State
    {
        SignedOut,
        Ready,
        CheckingCover,
        UploadingCover,
        Writing
    };
    Q_ENUM(State)
    explicit PlaylistMutationService(ApiClient &api, QObject *parent = nullptr);
    ~PlaylistMutationService() override;
    State state() const
    {
        return m_state;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    bool create(const QString &title, bool isPrivate);
    bool collect(const ApiClient::CollectionTarget &target);
    bool addTracks(ApiClient::CloudPlaylistId id,
                   const QList<ApiClient::PlaylistTrackInput> &tracks);
    bool removeTracks(ApiClient::CloudPlaylistId id,
                      const QList<ApiClient::PlaylistTrackFileId> &files);
    bool sortTracks(ApiClient::CloudPlaylistId id, quint64 revision,
                    const QList<ApiClient::PlaylistTrackPosition> &positions);
    bool update(const ApiClient::PlaylistUpdate &update);
    bool sortLibrary(quint64 totalRevision, const QList<ApiClient::PlaylistPosition> &positions);
    /// Exact editor flow: fresh version/ownership read, upload prepared PNG,
    /// version recheck, then playlist_update.pic. No write replay.
    bool saveCover(const ApiClient::PlaylistUpdate &update, const QByteArray &png);
    /// Metadata-only edit uses the same fresh ownership/version gate.
    bool saveEdit(const ApiClient::PlaylistUpdate &update, const QByteArray &png = {});
    bool remove(ApiClient::CloudPlaylistId id);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    /// Inspect per-item resource codes; completion does not imply every track
    /// was accepted by the provider.
    void completed(const QByteArray &acknowledgement);
    /// Directory membership/metadata/order changed; track-only writes use the
    /// reconciled snapshot below so sidebar delegates remain intact.
    void libraryInvalidated();
    /// Complete, version-consistent background directory snapshot.
    void libraryReconciled(const QJsonObject &snapshot, quint64 sessionRevision);
    /// First track page from an independent background refresh.
    void trackPageReconciled(const QJsonObject &snapshot, quint64 sessionRevision);
    /// Server-reported facts from the acknowledgement or a later refresh.
    /// songCount == -1 means unknown; never derive it from attempted writes.
    void tracksReconciled(quint64 cloudListId, quint64 revision, qint64 songCount,
                          quint64 sessionRevision);
    void tracksInvalidated(quint64 cloudListId);

private:
    friend class PlaylistMutationServiceTestAccess;
    enum class Kind
    {
        Create,
        AddTracks,
        RemoveTracks,
        SortTracks,
        SortLibrary,
        Update,
        Remove
    };
    bool ready() const;
    bool begin(quint64 request, Kind kind, ApiClient::CloudPlaylistId id);
    void succeed(quint64 request, const QByteArray &payload);
    void fail(quint64 request, const QByteArray &payload);
    void reset();
    bool libraryOperation() const;
    void queueRefresh(bool library, ApiClient::CloudPlaylistId id);
    void readRefresh();
    void acceptRefresh(const QJsonObject &object);
    void finishRefresh();
    void readCoverLibrary();
    void coverFailed(const QString &message);
    void coverSucceeded(const QJsonObject &object);
    ApiClient &m_api;
    State m_state = State::SignedOut;
    QString m_error;
    quint64 m_request = 0, m_revision = 0;
    ApiClient::CloudPlaylistId m_id{0};
    Kind m_kind = Kind::Create;
    quint32 m_page = 1;
    quint64 m_seen = 0;
    bool m_found = false;
    bool m_closed = false;
    enum class CoverStage
    {
        None,
        BeforeUpload,
        Upload,
        AfterUpload
    };
    CoverStage m_coverStage = CoverStage::None;
    ApiClient::PlaylistUpdate m_coverUpdate{};
    QByteArray m_coverPng;
    QSet<quint64> m_libraryReadIds;
    struct Refresh
    {
        bool library = false;
        ApiClient::CloudPlaylistId id{0};
        quint64 session = 0;
        quint32 page = 1;
        std::optional<quint64> version;
        QJsonArray items;
        QHash<quint64, qsizetype> positions;
    };
    QList<Refresh> m_refreshQueue;
    std::optional<Refresh> m_refresh;
    quint64 m_refreshRequest = 0;
};
} // namespace EchoMusic
