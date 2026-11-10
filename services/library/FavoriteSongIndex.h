#pragma once
#include "ApiClient.h"
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <optional>

namespace EchoMusic
{
class UserPlaylistModel;
class PlaylistMutationService;
/// Account-owned liked-song snapshot shared by the page and every heart. All
/// calls run on the GUI thread; membership does not require a deletion file ID.
class FavoriteSongIndex final : public QObject
{
    Q_OBJECT
public:
    struct Identity
    {
        QString id, hash;
        quint64 songId = 0;
    };
    FavoriteSongIndex(ApiClient &, UserPlaylistModel &);
    ~FavoriteSongIndex() override;
    void attachMutations(PlaylistMutationService &);
    void ensureLoaded(bool force = false);
    void refresh();
    void applyPresentations(const QJsonArray &);
    bool contains(const Identity &) const;
    bool committedContains(const Identity &) const;
    quint64 fileId(const Identity &) const;
    /// Playback may learn a catalog ID while retaining the same audio hash.
    void enrichIdentity(const Identity &);
    void beginPending(const Identity &, const QJsonObject &song, bool liked);
    void finishPending();
    void add(const QJsonArray &entries, const QList<Identity> &identities = {});
    void acceptReadback(const QJsonObject &, quint64 session);
    const QJsonArray &rows() const
    {
        return m_rows;
    }
    quint64 listId() const
    {
        return m_listId;
    }
    quint64 generation() const
    {
        return m_generation;
    }
    quint64 received() const
    {
        return m_seen;
    }
    std::optional<quint64> revision() const
    {
        return m_snapshotVersion;
    }
    bool coverConsistent() const
    {
        return m_consistent;
    }
    bool complete() const
    {
        return m_complete;
    }
    bool loading() const
    {
        return m_reading || m_waitingLibrary;
    }
    bool hasSnapshot() const
    {
        return m_hasSnapshot;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    static Identity identity(const QJsonObject &song);
    static QStringList keys(const Identity &);
signals:
    void changed();
    void snapshotChanged();
    void filesRemoved(const QJsonArray &entries);

private:
    friend class FavoriteSongIndexTestAccess;
    struct Change
    {
        QJsonArray adds;
        QSet<QString> keys;
        QSet<quint64> files;
        quint64 sequence = 0;
    };
    void reset();
    void start(quint64 listId);
    void readPage();
    void accept(quint64, const QByteArray &);
    void acceptPage(const QJsonObject &);
    void fail(const QString &);
    void publish();
    void removeFiles(quint64 listId, const QList<quint64> &, quint64 session);
    QJsonArray applyChanges(QJsonArray rows) const;
    QStringList rowKeys(const QJsonObject &) const;
    static bool intersects(const QStringList &, const QSet<QString> &);
    ApiClient &m_api;
    UserPlaylistModel &m_library;
    PlaylistMutationService *m_mutations = nullptr;
    QJsonArray m_base, m_rows, m_readRows;
    QHash<QString, qsizetype> m_index, m_committedIndex;
    QHash<QString, QSet<quint64>> m_aliases;
    // Coalesced per identity/file, retained through a writer's background read.
    QHash<QString, Change> m_changes;
    std::optional<Change> m_pending;
    quint64 m_session = 0, m_request = 0, m_listId = 0, m_generation = 0, m_seen = 0;
    quint64 m_changeSequence = 0;
    quint32 m_page = 1;
    std::optional<quint64> m_version, m_snapshotVersion;
    bool m_complete = false, m_hadComplete = false, m_hasSnapshot = false;
    bool m_consistent = true, m_waitingLibrary = false;
    bool m_reading = false, m_closed = false;
    QString m_error;
};
} // namespace EchoMusic
