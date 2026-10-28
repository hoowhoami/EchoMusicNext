#pragma once
#include "ApiClient.h"
#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include "StorageService.h"

namespace EchoMusic
{
class UserPlaylistModelTestAccess;
/// GUI-owned canonical library model. No upstream fields or account credentials
/// reach delegates; pagination and version checks belong to this single owner.
class UserPlaylistModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(quint64 totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(quint64 totalRevision READ totalRevision NOTIFY changed)
public:
    enum class State
    {
        SignedOut,
        Idle,
        Loading,
        Ready,
        Error,
        RestartRequired
    };
    Q_ENUM(State)
    enum Role
    {
        CloudListIdRole = Qt::UserRole + 1,
        GlobalIdRole,
        TitleRole,
        DescriptionRole,
        CoverUrlRole,
        SongCountRole,
        KindRole,
        EditableRole,
        PrivateRole,
        RevisionRole,
        SortIndexRole,
        CreatorIdRole,
        CreatorNameRole,
        DefaultKindRole,
        SourceCodeRole,
        CreatorGlobalIdRole,
        TagsRole,
        CreatorListIdRole
    };
    Q_ENUM(Role)
    explicit UserPlaylistModel(ApiClient &api, QObject *parent = nullptr);
    ~UserPlaylistModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    Q_INVOKABLE void reload();
    /// Apply confirmed track-read facts without resetting the directory. A
    /// negative count preserves the previous count when upstream omitted it.
    void applyTrackSnapshot(quint64 cloudListId, quint64 revision, qint64 songCount,
                            quint64 sessionRevision);
    void attachStorage(StorageService &storage);
    QString coverFor(const QString &globalId, const QString &fallback) const;
    /// Account-local editable metadata overlays public collection presentation.
    QJsonObject metadataFor(const QString &globalId) const;
    void publishAutoCover(ApiClient::CloudPlaylistId id, quint64 revision, const QString &cover,
                          quint64 session);
    /// Return the exact account ID/type pair; callers never substitute public ID.
    std::optional<ApiClient::CloudPlaylistId> cloudIdAt(int row) const;
    /// Catalog-local ID for remove/ordering, including collected playlists. This
    /// is distinct from the ownership-restricted cloud track accessor above.
    std::optional<ApiClient::CloudPlaylistId> libraryIdAt(int row) const;
    /// Account default-favorite playlist row shared by player and favorites UI.
    /// Returns -1 when no eligible row exists.
    int likedRow() const;
    /// Choose source from canonical list provenance before sending a request;
    /// never switch routes because a cloud endpoint happened to return empty.
    quint64 requestTracksAt(int row, quint32 page = 1, quint32 pageSize = 30);
    State state() const
    {
        return m_state;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    quint64 totalCount() const
    {
        return m_total;
    }
    quint64 totalRevision() const
    {
        return m_listRevision;
    }
signals:
    void changed();
    void coverChanged(const QString &globalId);

private:
    friend class UserPlaylistModelTestAccess;
    struct Row
    {
        ApiClient::CloudPlaylistId cloudId;
        QString globalId, creatorGlobalId, title, description, cover, creatorName, tags;
        QString kind = QStringLiteral("owned");
        quint64 songCount = 0, revision = 0, creatorId = 0, creatorListId = 0;
        qint64 sortIndex = 0;
        std::optional<quint32> defaultKind;
        quint32 sourceCode = 0;
        bool descriptionPresent = false, tagsPresent = false;
        bool editable = false, isPrivate = false, collected = false, customCover = false;
    };
    void resetState();
    void finishMutation();
    void succeed(quint64 id, const QByteArray &payload);
    void fail(quint64 id, const QByteArray &payload);
    ApiClient &m_api;
    QPointer<StorageService> m_storage;
    bool m_storageReady = false;
    quint64 m_coverAccount = 0;
    QHash<quint64, StoredPlaylistCover> m_autoCovers;
    QList<Row> m_rows;
    State m_state = State::SignedOut;
    QString m_error;
    quint64 m_request = 0, m_sessionRevision = 0, m_listRevision = 0, m_total = 0;
    quint32 m_page = 0;
    static constexpr quint32 PageSize = 30;
    bool m_exhausted = false, m_closed = false, m_mutating = false, m_resetPending = false,
         m_reloadPending = false;
};
} // namespace EchoMusic
