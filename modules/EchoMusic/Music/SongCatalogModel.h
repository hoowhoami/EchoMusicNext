#pragma once
#include "PlaybackTrack.h"
#include <QAbstractListModel>
#include <QCollator>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class SongMetadataService;
/// A bounded page catalog. Filtering, ordering and batch selection belong here,
/// never to a recycled delegate. Inputs are Rust domain data, not provider JSON.
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
        AlbumId
    };
    explicit SongCatalogModel(QObject *parent = nullptr);
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
    /// Selected catalog row IDs. Batch actions (e.g. cloud deletion) pass
    /// these to page commands; selection stays owned by the model.
    Q_INVOKABLE QStringList selectedIds() const;
    void setQuery(const QString &query);
    void attachMetadata(SongMetadataService *service);
    void setMetadataActive(bool active);
    /// Refresh only rows addressed by the shared identity index.
    void refreshMetadata(const QStringList &rows);
    void replaceSongs(const QJsonArray &songs, bool preserveSelection = false);
    void appendSongs(const QJsonArray &songs);
    /// Update presentation fields in place; resource/rights/selection stay intact.
    void applyPresentations(const QJsonArray &presentations);
    std::optional<PlaybackTrack> track(const QString &id) const;
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
    PlaybackTracks tracks(bool selectedOnly = false) const;
signals:
    void changed();
    void selectionChanged();
    void presentationsApplied(const QStringList &rows);

private:
    struct Entry
    {
        // Keep one immutable domain payload. Full playback metadata is assembled
        // only when a command exports a track, not for every cached catalog row.
        QJsonObject domainSong;
        QString id, title, artist, album, cover, extra;
        QString titleSort, artistSort, albumSort;
        QVariantList badges;
        QString quality;
        QVariantList qualityTags;
        quint64 albumId = 0, durationMs = 0;
        bool playable = true, trackPlayable = false, original = false;
        bool operator==(const Entry &) const = default;
    };
    QList<int> orderedRows(const QList<Entry> &entries) const;
    void replaceEntries(QList<Entry> entries, bool cards, bool preserveSelection);
    void watchMetadata();
    void notifyPresentations(const QSet<int> &changed, bool orderMayChange = true);
    QPointer<SongMetadataService> m_metadata;
    QTimer m_metadataRefresh;
    QMetaObject::Connection m_metadataReset;
    bool m_metadataActive = true, m_cards = false;
    int m_metadataObservedCount = 0;
    static Entry songEntry(const QJsonObject &song);
    void rebuild(bool resetStarted = false);
    QList<Entry> m_entries;
    QHash<QString, int> m_indices;
    QMultiHash<QString, int> m_domainIndices;
    QList<int> m_viewRows;
    QList<int> m_order;
    QSet<QString> m_selected;
    QString m_query;
    int m_sortField = 0, m_sortDirection = 0;
    QCollator m_collator;
};
} // namespace EchoMusic
