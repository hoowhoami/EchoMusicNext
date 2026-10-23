#pragma once
#include "ApiClient.h"
#include <QSet>

namespace EchoMusic
{
class PlaylistMutationServiceTestAccess;
/// GUI-thread single writer for cloud playlists. Canonical models remain read
/// owners. Each acknowledgement is reconciled by fresh domain reads; cancellation
/// or transport failure never replays a mutation or fabricates a local rollback.
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
        Writing,
        Reconciling,
        RefreshRequired,
        OutcomeUnknown
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
    /// version recheck, playlist_update.pic, canonical readback. No write replay.
    bool saveCover(const ApiClient::PlaylistUpdate &update, const QByteArray &png);
    /// Metadata-only edit uses the same fresh ownership/version gate.
    bool saveEdit(const ApiClient::PlaylistUpdate &update, const QByteArray &png = {});
    bool remove(ApiClient::CloudPlaylistId id);
    /// Only reissue reads after acknowledgement; never repeat an unknown write.
    Q_INVOKABLE bool reconcile();
    Q_INVOKABLE void cancel();
    /// Explicit caller decision after inspecting a refreshed canonical model.
    /// This releases an unknown outcome, not a rollback or automatic retry.
    void acceptRefreshedUnknownOutcome();
signals:
    void changed();
    /// Inspect per-item resource codes; completion does not imply every track
    /// was accepted by the provider.
    void completed(const QByteArray &acknowledgement);
    void libraryInvalidated();
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
    void readNext();
    void requireRefresh(const QString &message);
    bool libraryOperation() const;
    void readCoverLibrary();
    void coverFailed(const QString &message);
    void coverSucceeded(const QJsonObject &object);
    ApiClient &m_api;
    State m_state = State::SignedOut;
    QString m_error;
    QByteArray m_acknowledgement;
    quint64 m_request = 0, m_revision = 0, m_userId = 0, m_expectedRevision = 0,
            m_expectedCount = 0;
    ApiClient::CloudPlaylistId m_id{0};
    Kind m_kind = Kind::Create;
    quint32 m_page = 1;
    quint64 m_seen = 0;
    bool m_found = false;
    bool m_closed = false, m_acceptNewer = false;
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
    QString m_coverFileName, m_expectedCoverUrl;
    QHash<quint64, ApiClient::PlaylistPosition> m_positions;
    QSet<quint64> m_verifiedPositions, m_libraryReadIds;
    quint64 m_coverCount = 0;
};
} // namespace EchoMusic
