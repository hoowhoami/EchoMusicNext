#pragma once

#include "ApiClient.h"
#include "PlaybackTrack.h"
#include "SearchResultModel.h"
#include "SearchSuggestionModel.h"
#include <QAbstractListModel>
#include <array>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class StorageService;
class PlaybackService;
class PlaybackLibraryActions;
/// Small, read-only discovery groups. Upstream URLs never become actions in QML.
class HotSearchModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SearchModel")
public:
    struct Group
    {
        QString title;
        QStringList keywords;
        QStringList reasons;
    };
    explicit HotSearchModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    friend class SearchModel;
    void replace(QList<Group> groups);
    QList<Group> m_groups;
};

/// GUI-thread owner of results, pagination and cancellation. Projects typed Rust
/// domain values into display roles and commands. ApiClient outlives this model.
class SearchModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(Kind kind READ kind NOTIFY changed)
    Q_PROPERTY(QString keyword READ keyword NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(quint64 total READ total NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
    Q_PROPERTY(bool discoveryLoading READ discoveryLoading NOTIFY discoveryChanged)
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY discoveryChanged)
    Q_PROPERTY(QString currentTrackId READ currentTrackId NOTIFY playbackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(EchoMusic::HotSearchModel *hotGroups READ hotGroups CONSTANT)
    Q_PROPERTY(EchoMusic::SearchResultModel *results READ results CONSTANT)
    Q_PROPERTY(EchoMusic::SearchSuggestionModel *suggestions READ suggestions CONSTANT)
    Q_PROPERTY(QStringList searchHistory READ searchHistory NOTIFY historyChanged)
    Q_PROPERTY(quint64 favoriteRevision READ favoriteRevision NOTIFY favoritesChanged)
    Q_PROPERTY(quint64 blacklistRevision READ blacklistRevision NOTIFY blacklistChanged)
public:
    enum class State
    {
        Idle,
        Loading,
        Ready,
        LoadingMore,
        Error
    };
    Q_ENUM(State)
    enum class Kind
    {
        Song,
        Album,
        Artist,
        Playlist,
        Lyric,
        Mv
    };
    Q_ENUM(Kind)
    enum Role
    {
        Title = Qt::UserRole + 1,
        ArtistText,
        AlbumText,
        CoverUrl,
        DurationText,
        ExtraText,
        ResourceId,
        DurationMs,
        Original,
        ArtistLinks,
        AlbumId,
        PrivilegeTags,
        QualityLabel
    };
    explicit SearchModel(ApiClient &api, QObject *parent = nullptr);
    void attachPlayback(PlaybackService &playback);
    QString currentTrackId() const;
    bool playing() const;
    ~SearchModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    State state() const
    {
        return active().state;
    }
    Kind kind() const
    {
        return m_kind;
    }
    QString keyword() const
    {
        return m_keyword;
    }
    QString errorMessage() const
    {
        return active().error;
    }
    quint64 total() const
    {
        return active().total;
    }
    int count() const
    {
        return int(active().rows.size());
    }
    bool hasMore() const
    {
        return active().nextPage > 0 && quint64(active().nextPage - 1) * 30 < active().total;
    }
    bool discoveryLoading() const
    {
        return m_hotRequest != 0;
    }
    QString discoveryError() const
    {
        return m_hotError;
    }
    HotSearchModel *hotGroups()
    {
        return &m_hot;
    }
    SearchResultModel *results() const
    {
        return m_results;
    }
    SearchSuggestionModel *suggestions() const
    {
        return m_suggestions;
    }
    QStringList searchHistory() const;
    void setHistoryStore(StorageService &storage);
    Q_INVOKABLE void removeHistory(const QString &keyword);
    Q_INVOKABLE void clearHistory();
    /// Not invoked by offline smoke; ordinary app startup loads public discovery.
    void initialize();
    void shutdown();
    /// Stop page-owned HTTP work without closing the reusable service.
    void setPageActive(bool active);
    Q_INVOKABLE void search(const QString &keyword, Kind kind = Kind::Song);
    Q_INVOKABLE void setKind(Kind kind);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void playSong(const QString &id);
    Q_INVOKABLE void playNow(const QString &id);
    Q_INVOKABLE void playAll();
    Q_INVOKABLE void appendAll(bool next = false);
    /// 搜索结果右键菜单的共享歌曲命令。
    Q_INVOKABLE void appendSong(const QString &id, bool next = false);
    Q_INVOKABLE void addSong(const QString &id);
    Q_INVOKABLE bool isFavorite(const QString &id) const;
    Q_INVOKABLE void toggleFavorite(const QString &id);
    Q_INVOKABLE void shareSong(const QString &id);
    Q_INVOKABLE void copySong(const QString &id);
    Q_INVOKABLE void openDetails(const QString &id);
    Q_INVOKABLE void openMv(const QString &id);
    Q_INVOKABLE void openArtist(const QString &id);
    Q_INVOKABLE void openAlbum(const QString &id);
    Q_INVOKABLE bool blacklistAvailable(const QString &id) const;
    Q_INVOKABLE bool blacklisted(const QString &id) const;
    Q_INVOKABLE void addToBlacklist(const QString &id);
    Q_INVOKABLE void removeFromBlacklist(const QString &id);
    Q_INVOKABLE bool blacklistBusy() const;
    quint64 favoriteRevision() const
    {
        return m_favoriteRevision;
    }
    quint64 blacklistRevision() const;
    Q_INVOKABLE void retry();
    Q_INVOKABLE void showDiscovery();
    Q_INVOKABLE void reloadDiscovery();
    // Popup lifecycle is independent from the results route. Hot/history data
    // remain service-owned and session-bound when opened from another page.
    Q_INVOKABLE void setDiscoveryActive(bool active);
signals:
    void changed();
    void discoveryChanged();
    void playbackChanged();
    void historyChanged();
    void favoritesChanged();
    void blacklistChanged();
    void tracksPlayRequested(const EchoMusic::PlaybackTracks &tracks, int index,
                             const QString &title, const QString &sourceKey, bool selectedSong);
    void tracksAppendRequested(const EchoMusic::PlaybackTracks &tracks, bool next);
    void trackQueueRequested(const EchoMusic::PlaybackTrack &track, bool cut);
    void trackPlayNowRequested(const EchoMusic::PlaybackTrack &track, const QString &title,
                               const QString &key);

private:
    friend class SearchModelTestAccess;
    PlaybackTrack rowTrack(const QString &id) const;
    void requestPlayback(const QString &id, bool selectedSong);
    struct Row
    {
        QString title, artists, album, cover, duration, extra, id;
        quint64 durationMs = 0;
        PlaybackTrack track;
        bool original = false;
        QVariantList badges;
        QString quality;
    };
    struct Bucket
    {
        QList<Row> rows;
        State state = State::Idle;
        QString error;
        quint64 request = 0, revision = 0, total = 0;
        quint32 pendingPage = 0, nextPage = 0;
        bool loaded = false;
    };
    Bucket &active()
    {
        return m_buckets[std::size_t(m_kind)];
    }
    const Bucket &active() const
    {
        return m_buckets[std::size_t(m_kind)];
    }
    void requestPage(quint32 page);
    void succeeded(quint64 id, const QByteArray &payload);
    void failed(quint64 id, int status, const QByteArray &payload);
    void cancelRequests();
    void clearBuckets();
    void resetSession();
    ApiClient &m_api;
    QPointer<PlaybackService> m_playback;
    HotSearchModel m_hot;
    SearchResultModel *m_results = nullptr;
    SearchSuggestionModel *m_suggestions = nullptr;
    StorageService *m_historyStore = nullptr;
    std::array<Bucket, 6> m_buckets;
    Kind m_kind = Kind::Song;
    QString m_keyword, m_hotError;
    quint64 m_hotRequest = 0, m_hotRevision = 0;
    bool m_pageActive = true, m_started = false, m_closing = false, m_changingRows = false;
    bool m_discoveryActive = false;
    QPointer<PlaybackLibraryActions> m_libraryActions;
    quint64 m_favoriteRevision = 0;
};
} // namespace EchoMusic
