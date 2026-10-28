#pragma once
#include "PlaybackTrack.h"
#include <QAbstractListModel>
#include <QSet>
#include <QPointer>
#include <QTimer>
namespace EchoMusic
{
class SongMetadataService;
/// Stable song identities and next-priority flags are model state. Delegate
/// reuse never changes order, selection or the active entry.
class PlaybackQueueModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
public:
    enum Role
    {
        Title = Qt::UserRole + 1,
        ArtistText,
        AlbumText,
        CoverUrl,
        DurationText,
        ResourceId,
        Current,
        QueuedNext
    };
    explicit PlaybackQueueModel(QObject *parent);
    void attachMetadata(SongMetadataService *service);
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_tracks.size());
    }
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    const PlaybackTracks &tracks() const
    {
        return m_tracks;
    }
    int currentIndex() const
    {
        return m_current;
    }
    const PlaybackTrack *at(int row) const
    {
        return row >= 0 && row < m_tracks.size() ? &m_tracks[row] : nullptr;
    }
    void replace(PlaybackTracks tracks, int current = -1);
    void setCurrent(int row);
    /// Adopt the canonical playback identity (view id + file identity) into a
    /// queue row whose persisted snapshot predates it.
    void adoptIdentity(int row, const PlaybackTrack &track);
    /// Merge corrected identity facts (embedded tags) into the matching row so
    /// queue copies stay aligned with the playing track's presentation.
    void updateFacts(const QString &id, const QString &hash, const QString &title,
                     const QString &artist, const QString &album, const QString &cover = {},
                     quint64 durationMs = 0);
    void append(const PlaybackTracks &tracks, bool next, const QString &currentTrackId = {},
                bool sequential = false);
    void consumePlayedPriority(const QString &trackId);
    quint64 revision() const
    {
        return m_revision;
    }
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();
    int takeNextPriority();
    int peekNextPriority() const;
    int takeNextPriority(const QString &excludeId);
    int peekNextPriority(const QString &excludeId) const;
    QStringList nextKeys() const
    {
        return m_next;
    }
    void restoreNext(const QStringList &keys);
signals:
    void changed();
    void currentRemoved();
    void presentationChanged();

private:
    void watchMetadata();
    void refreshMetadata(const QStringList &rows);
    QPointer<SongMetadataService> m_metadata;
    QTimer m_metadataRefresh;
    QMetaObject::Connection m_metadataReset;
    PlaybackTracks m_tracks;
    QStringList m_next;
    QSet<QString> m_nextSet;
    int m_current = -1;
    quint64 m_revision = 0;
};
} // namespace EchoMusic
