#pragma once
#include "PlaybackTrack.h"
#include "SongDatasetStore.h"
#include "ValueCache.h"
#include <QAbstractListModel>
#include <QCollator>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <QPointer>
#include <QSharedData>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <tuple>

namespace EchoMusic
{
class SongMetadataService;
/// A page catalog with optional disposable rich-payload storage. Filtering,
/// ordering and selection belong here, never to a recycled delegate. Inputs
/// are Rust domain data, not provider JSON.
class SongCatalogModel final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by an application page service")
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(int sortField READ sortField NOTIFY changed)
    Q_PROPERTY(int sortDirection READ sortDirection NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool querying READ querying NOTIFY changed)
    Q_PROPERTY(bool cacheRecovering READ cacheRecovering NOTIFY cacheRecoveryChanged)
public:
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
        Selected,
        PrivilegeTags,
        QualityLabel,
        /// Every distinct quality label in resource order. DiscoverFlow shows the
        /// complete set; single-label controls use QualityLabel.
        QualityTags,
        CatalogPlayable,
        Original,
        ArtistLinks,
        AlbumId,
        Recommendation,
        PresentationReady
    };
    explicit SongCatalogModel(QObject *parent = nullptr);
    ~SongCatalogModel() override;
    /// Enable before loading. Rich domain payloads retire after a committed
    /// page; synchronous projections remain UI facts, never playback inputs.
    void enableSessionStorage(SessionDatabase &database);
    /// Use the application database injected through shared metadata, if any.
    void enableSessionStorage();
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const
    {
        return int(m_order.size());
    }
    int totalCount() const
    {
        return m_entries.size();
    }
    QString query() const
    {
        return m_query;
    }
    int sortField() const
    {
        return m_sortField;
    }
    int sortDirection() const
    {
        return m_sortDirection;
    }
    int selectedCount() const
    {
        return m_selected.size();
    }
    bool querying() const
    {
        return m_queryNeeded || m_querying;
    }
    /// Producers retain their original network batch/concurrency, but wait for
    /// accepted rich pages to retire before issuing the next background batch.
    bool storageReady() const
    {
        return !m_store || m_cards ||
               (!m_storageFailed && !m_writing && m_writeCursor == m_entries.size());
    }
    /// Explicit local retry keeps accepted rows/selection and never replays a
    /// network write. False means this model has no failed cache work.
    bool retryStorage();
    bool cacheRecovering() const
    {
        return m_cacheRecovering;
    }
    /// A producer ends recovery at its original EOF or explicit read failure.
    void finishCacheRecovery();
    /// Selected catalog row IDs. Batch actions (e.g. cloud deletion) pass
    /// these to page commands; selection stays owned by the model.
    Q_INVOKABLE QStringList selectedIds() const;
    void setQuery(const QString &query);
    void setQueryStyle(SongDatasetStore::QueryStyle style)
    {
        m_queryStyle = style;
    }
    void setSort(int field, int direction);
    QVariant sourceData(int source, int role) const;
    int sourceIndexOf(const QString &id) const
    {
        return m_indices.value(id, -1);
    }
    void attachMetadata(SongMetadataService *service);
    void setMetadataActive(bool active);
    /// Refresh only rows addressed by the shared identity index.
    void refreshMetadata(const QStringList &rows);
    void replaceSongs(const QJsonArray &songs, bool preserveSelection = false);
    void appendSongs(const QJsonArray &songs);
    /// Attach committed records owned by a shared account data source. No payload copy/write.
    void replaceCachedRows(const QList<SongDatasetStore::CachedRow> &,
                           bool preserveSelection = false);
    /// Update presentation fields in place; resource/rights/selection stay intact.
    void applyPresentations(const QJsonArray &presentations);
    /// Synchronous identity/display projection for predicates and public menus.
    /// Playback and commands must use requestTrack(s) for complete source facts.
    std::optional<PlaybackTrack> trackProjection(const QString &id) const;
    void requestTrack(const QString &id, QObject *consumer,
                      std::function<void(PlaybackTrack)> done);
    void requestTracks(bool selectedOnly, QObject *consumer,
                       std::function<void(PlaybackTracks)> done);
    void requestRows(const QStringList &ids, QObject *consumer,
                     std::function<void(PlaybackTracks)> done);
    /// Read display facts without decoding full source resources or rights.
    void requestPresentations(const QStringList &, QObject *, std::function<void(QJsonArray)>);
    void requestCover(QObject *, std::function<void(QString)>);
    bool presentationsReady() const
    {
        return !m_metadataLoading && m_pendingMetadata.isEmpty() && !m_presentationWriting &&
               m_dirtyPresentations.isEmpty();
    }
    QStringList incompletePresentationIds() const;
    bool catalogPlayable(const QString &id) const;
    void replaceCards(const QJsonArray &cards);
    void clear();
    Q_INVOKABLE void toggleSort(int field);
    /// Restore model order after saving. The page clears its own search text separately.
    Q_INVOKABLE void clearSort();
    Q_INVOKABLE QVariantMap item(int row) const;
    Q_INVOKABLE QVariantMap sourceItem(int row) const;
    Q_INVOKABLE int indexOf(const QString &id) const;
    Q_INVOKABLE void toggleSelected(const QString &id);
    Q_INVOKABLE void selectAll(bool selected);
    Q_INVOKABLE void clearSelection();
    bool hasPlayableTracks() const;
signals:
    void cacheRecoveryChanged();
    void cacheInvalidated();
    void changed();
    void selectionChanged();
    void presentationsApplied(const QStringList &rows);
    void readFailed(const QString &message);
    void storageReadyChanged();
    void storageFailed(const QString &message);
    void presentationProgress();

private:
    bool m_cacheRecovering = false, m_cacheStale = false;
    QSet<QString> m_recoverySelection;
    struct Presentation : QSharedData
    {
        // Dense sources retain the domain payload. A session-backed source
        // replaces it with a display/identity projection after SQL commit;
        // complete playback facts are read only for an actual command.
        QJsonObject domainSong;
        QString title, artist, album, cover, extra;
        QString titleSort, artistSort, albumSort;
        QVariantList badges;
        QString quality;
        QVariantList qualityTags;
        quint64 albumId = 0, durationMs = 0;
        bool original = false;
        bool operator==(const Presentation &other) const
        {
            return std::tie(domainSong, title, artist, album, cover, extra, titleSort, artistSort,
                            albumSort, badges, quality, qualityTags, albumId, durationMs,
                            original) ==
                   std::tie(other.domainSong, other.title, other.artist, other.album, other.cover,
                            other.extra, other.titleSort, other.artistSort, other.albumSort,
                            other.badges, other.quality, other.qualityTags, other.albumId,
                            other.durationMs, other.original);
        }
    };
    struct Entry
    {
        QString id, domainId, hash, path, completionId;
        quint64 songId = 0, presentationRevision = 0;
        qint64 record = 0;
        SongDatasetStore::Generation source;
        bool playable = true, trackPlayable = false, incomplete = false;
        QExplicitlySharedDataPointer<Presentation> view;
        bool operator==(const Entry &other) const
        {
            return id == other.id && completionId == other.completionId &&
                   incomplete == other.incomplete && record == other.record &&
                   source == other.source && playable == other.playable &&
                   trackPlayable == other.trackPlayable &&
                   (view == other.view || (view && other.view && *view == *other.view));
        }
    };
    QList<int> orderedRows(const QList<Entry> &entries) const;
    void replaceEntries(QList<Entry> entries, bool cards, bool preserveSelection,
                        bool shared = false, bool preserveCommands = false);
    SongDatasetStore::Snapshot snapshot(const QList<Entry> &) const;
    void watchMetadata();
    void notifyPresentations(const QSet<int> &changed, bool orderMayChange = true);
    QPointer<SongMetadataService> m_metadata;
    QTimer m_metadataRefresh;
    QMetaObject::Connection m_metadataReset;
    bool m_metadataActive = true, m_metadataResume = false, m_cards = false;
    int m_metadataObservedCount = 0;
    static Entry songEntry(const QJsonObject &song);
    static void applyEntryPresentation(Entry &, const QJsonObject &);
    void flushStorage();
    void failStorage(const QString &message);
    static QJsonObject presentationValue(const Entry &);
    void queuePresentations(const QSet<int> &sources);
    void flushPresentations();
    void resetStorage(bool preserveCommands = false);
    using ViewPtr = QExplicitlySharedDataPointer<Presentation>;
    ViewPtr viewFor(int source) const;
    static Entry storedEntry(const QJsonObject &, const Entry &);
    void retireViews(const QSet<int> &);
    void flushViewReads();
    void flushMetadata();
    void loadViews(QList<int>, QObject *, std::function<void(QList<Entry>)>);
    void notifyViewRows(const QSet<int> &);
    mutable ValueCache<int, ViewPtr> m_hotViews{256, 4 * 1024 * 1024};
    mutable ValueCache<int, bool> m_viewDemand{256};
    QTimer m_viewLoad, m_metadataLoad;
    QSet<int> m_pendingMetadata;
    QHash<int, QJsonArray> m_pendingOverlays;
    bool m_loadingViews = false, m_metadataLoading = false, m_metadataWatching = false;
    void flushQuery();
    void applyOrder(QList<int>);
    void requestQueryTracks(QObject *, std::function<void(PlaybackTracks)>);
    void readEntries(QList<Entry>, QObject *, std::function<void(PlaybackTracks)>);
    friend class SongCatalogStorageTestAccess;
    friend class SongCatalogActions;
    QPointer<SessionDatabase> m_sessionDatabase;
    std::unique_ptr<SongDatasetStore> m_store;
    SongDatasetStore::Generation m_generation;
    qsizetype m_writeCursor = 0;
    quint64 m_storageEpoch = 0, m_commandEpoch = 0;
    bool m_writing = false, m_storageFailed = false, m_sharedSource = false;
    QTimer m_presentationFlush;
    QSet<int> m_dirtyPresentations;
    bool m_presentationWriting = false, m_presentationFailed = false;
    QTimer m_queryRefresh;
    quint64 m_viewRevision = 0;
    bool m_queryNeeded = false, m_querying = false;
    void rebuild(bool resetStarted = false);
    QList<Entry> m_entries;
    QHash<QString, int> m_indices;
    QMultiHash<QString, int> m_domainIndices;
    QList<int> m_viewRows;
    QList<int> m_order;
    QSet<QString> m_selected;
    QString m_query;
    int m_sortField = 0, m_sortDirection = 0;
    SongDatasetStore::QueryStyle m_queryStyle = SongDatasetStore::QueryStyle::Catalog;
    QCollator m_collator;
};
} // namespace EchoMusic
