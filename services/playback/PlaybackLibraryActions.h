#pragma once
#include "PlaybackTrack.h"
#include "PlaylistMutationService.h"
#include "UserPlaylistModel.h"
#include <QAbstractListModel>
#include <QHash>
#include <QQueue>
#include <QSet>
namespace EchoMusic
{
class PlaybackService;
/// Shared player/queue add and favorite commands. Targets and per-song outcomes
/// belong to the GUI model, never reused delegates. Cloud writes use the common
/// single writer and are not replayed after an unknown transport outcome.
class PlaybackLibraryActions final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool favorite READ favorite NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool favoriteLoading READ favoriteLoading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool multiple READ multiple WRITE setMultiple NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(int targetCount READ rowCount NOTIFY changed)
    Q_PROPERTY(int sectionCount READ sectionCount NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool hasFailures READ hasFailures NOTIFY changed)
public:
    explicit PlaybackLibraryActions(PlaybackService &, ApiClient &, UserPlaylistModel &,
                                    PlaylistMutationService &, QObject *parent);
    ~PlaybackLibraryActions() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool favorite() const;
    bool signedIn() const
    {
        return m_api.currentAccountUserId() != 0;
    }
    bool favoriteLoading() const
    {
        return m_favoriteRead != 0 || m_favoriteIntent.has_value();
    }
    bool busy() const
    {
        return m_busy;
    }
    bool multiple() const
    {
        return m_multiple;
    }
    int selectedCount() const;
    int sectionCount() const
    {
        return m_sectionCount;
    }

    QString errorMessage() const
    {
        return m_error;
    }
    bool hasFailures() const;
    bool favoriteTrack(const PlaybackTrack &track) const;
    void toggleFavoriteTrack(const PlaybackTrack &track, bool requireCurrent = false);
    Q_INVOKABLE void toggleFavorite();
    /// Typed host selection from a page; never accepts arbitrary QML/FFI records.
    void prepareTracks(const PlaybackTracks &tracks, bool playbackQueues = true);
    Q_INVOKABLE void prepareCurrent();
    Q_INVOKABLE void prepareViewedQueue();
    Q_INVOKABLE void choose(int row);
    Q_INVOKABLE void selectAll(bool checked);
    Q_INVOKABLE void setMultiple(bool value);
    Q_INVOKABLE void submitSelection();
    Q_INVOKABLE void fetchMoreTargets();
signals:
    void changed();
    void favoriteWritten(const EchoMusic::PlaybackTrack &track, bool favorite);
    void dialogRequested();
    void completed();
    void notice(const QString &message, bool warning);
    void loginRequested();

private:
    friend class PlaybackLibraryActionsTestAccess;
    enum Role
    {
        TargetKey = Qt::UserRole + 1,
        TargetTitle,
        TargetSection,
        TargetCount,
        TargetSelected,
        TargetResult,
        TargetFailed
    };
    struct Target
    {
        QString key, title, section;
        quint64 cloudId = 0;
        int count = 0;
    };
    struct Result
    {
        QString text;
        PlaybackTracks failed;
    };
    void reset();
    void rebuild();
    int likedRow() const;
    void refreshFavorites();
    void resumeFavorite();
    void failFavoriteRead(const QString &message);
    void readFavoritePage();
    void favoritePage(quint64, const QByteArray &);
    void nextWrite();
    void writeCompleted(const QByteArray &);
    void writeFailed(const QString &);
    void notifyRows();
    PlaybackService &m_playback;
    ApiClient &m_api;
    UserPlaylistModel &m_library;
    PlaylistMutationService &m_writer;
    QList<Target> m_targets;
    int m_sectionCount = 0;
    PlaybackTracks m_songs, m_writeSongs, m_remaining, m_failed;
    QHash<QString, Result> m_results;
    QHash<QString, Target> m_writeTargets;
    QSet<QString> m_selected;
    QQueue<QString> m_pending;
    QHash<QString, quint64> m_favorites, m_readFavorites;
    std::optional<PlaybackTrack> m_favoriteIntent;
    QString m_currentTarget, m_error;
    quint64 m_revision = 0, m_favoriteRead = 0, m_likedId = 0, m_favoriteVersion = 0;
    quint32 m_favoritePage = 1;
    quint64 m_favoriteSeen = 0, m_favoriteTotal = 0;
    bool m_playbackQueues = true;
    bool m_favoritesReady = false, m_multiple = false, m_busy = false, m_favoriteWrite = false,
         m_removing = false, m_ownedWrite = false;
    bool m_favoriteIntentRequiresCurrent = true;
};
} // namespace EchoMusic
