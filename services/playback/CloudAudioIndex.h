#pragma once
#include "ApiClient.h"
#include "PlaybackTrack.h"
#include "SongSourceCache.h"
#include "ValueCache.h"
#include <QHash>
namespace EchoMusic
{
class CloudAudioIndex final : public QObject
{
    Q_OBJECT
public:
    explicit CloudAudioIndex(ApiClient &, QObject *parent = nullptr);
    ~CloudAudioIndex() override;
    void ensure();
    void enableSessionStorage(SessionDatabase &);
    bool cached() const
    {
        return bool(m_cache);
    }
    const QList<QJsonObject> &rows() const
    {
        return m_rows;
    }
    QList<SongDatasetStore::CachedRow> cachedRows() const;
    QJsonObject counters() const
    {
        return m_counters;
    }
    quint64 received() const
    {
        return m_loaded;
    }
    quint64 total() const
    {
        return m_total;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    bool complete() const
    {
        return m_ready;
    }
    void invalidate();
    bool loading() const
    {
        return m_request != 0 || m_writing || !m_reads.isEmpty();
    }
    QJsonObject source(const PlaybackTrack &) const;
    /// Cloud file record (with presentation facts) for an audio hash.
    QJsonObject byHash(const QString &hash) const;
    /// Consumer-owned enrichment reads; no synchronous cold hint or whole-list replay.
    void requestHints(QStringList hashes, QObject *,
                      std::function<void(QHash<QString, QJsonObject>, QString)>);
    static QJsonObject fromRow(const QJsonObject &);
signals:
    void settled();
    void listingSettled();
    void changed();

private:
    friend class CloudAudioIndexTestAccess;
    void accept(quint64, const QByteArray &);
    void next();
    void acceptCachedPage(const QJsonObject &);
    QJsonObject readSource(qint64) const;
    ApiClient &m_api;
    quint64 m_request = 0, m_revision = 0, m_loaded = 0;
    quint32 m_page = 0;
    bool m_ready = false;
    QHash<QString, QJsonObject> m_index, m_pending;
    std::unique_ptr<SongSourceCache> m_cache;
    QHash<QString, qint64> m_lookup;
    QHash<qint64, SongDatasetStore::CachedRow> m_records;
    QList<QJsonObject> m_rows;
    QJsonObject m_counters;
    quint64 m_total = 0, m_epoch = 0, m_scanned = 0;
    quint32 m_arrival = 0;
    QString m_error;
    bool m_writing = false;
    mutable ValueCache<qint64, QJsonObject> m_hot{64, 1024 * 1024};
    mutable QSet<qint64> m_reads;
};
} // namespace EchoMusic
