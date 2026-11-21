#pragma once
#include "ApiClient.h"
#include "CompactSet.h"
#include "ValueCache.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <memory>
namespace EchoMusic
{
class EmbeddedCoverCache;
class SessionDatabase;
class SongObservationIndex;
/// Shared optional presentation completion: 50 IDs/batch, three transports,
/// deduplicated consumers and a bounded public-facts cache. No playback rights.
class SongMetadataService final : public QObject
{
    Q_OBJECT
public:
    explicit SongMetadataService(ApiClient &, QObject *parent = nullptr);
    ~SongMetadataService() override;
    /// Root is an application-owned cache directory; tests use an isolated root.
    void enableCoverCache(const QString &directory);
    /// Configure before observations. Production uses one application session DB.
    void enableSqlIndex(SessionDatabase &);
    SessionDatabase *sessionDatabase() const;
    void publishEmbeddedCover(const QJsonObject &song, const QString &exportedFile);
    quint64 request(const QStringList &ids);
    void cancel(quint64 ticket);
    void shutdown();
    struct Observation
    {
        QString row;
        QJsonObject song;
    };
    /// GUI-thread subscriptions retain identity indices only, never song payloads.
    /// Hidden owners keep receiving facts but release their HTTP consumer tickets.
    /// append extends stable row identities during pagination without rebuilding indices.
    void watch(QObject *owner, const QList<Observation> &songs,
               std::function<void(const QStringList &)> apply, bool active = true,
               bool append = false);
    /// Pause HTTP demand without deleting cached identity bindings. Registration
    /// can finish while hidden. Resume is for the same membership; replacing a
    /// catalog must unwatch first.
    void pause(QObject *owner);
    void resume(QObject *owner, const QList<Observation> &songs,
                std::function<void(const QStringList &)> apply);
    /// Restore demand for an unchanged SQL binding without copying/reinserting
    /// every observation. False requires the ordinary initial registration path.
    bool resumeDemand(QObject *owner, const QStringList &incompleteIds);
    void unwatch(QObject *owner);
    QJsonObject complete(const QJsonObject &song);
    /// Reapply current optional facts to an already admitted cached row. SQL
    /// hydration can finish after notifyObservers returns; it must not publish
    /// old source cover facts again when the bounded facts cache has evicted them.
    QJsonObject replay(const QJsonObject &song);
    /// Raw reader text, scoped by exact audio hash/path. No cover, lyrics or IDs.
    void publishEmbedded(const QJsonObject &song);
    static QStringList identityKeys(const QJsonObject &song);
    /// Minimal input for identity/completion registration. Never retain source
    /// resources/rights while a large SQL registration is waiting for capacity.
    static QJsonObject observationInput(const QJsonObject &song);
    static bool needsCompletion(const QJsonObject &song);
    static QString completionId(const QJsonObject &song);
    static QJsonObject completeSong(QJsonObject song, const QJsonObject &presentation);
    /// Artist reference objects for a tag string whose names join with "/".
    static QJsonArray artistRefs(const QString &artist);
    /// Provider policies censor some artist names with asterisk runs ("***").
    static bool maskedName(const QString &value);
signals:
    void cacheCleared();
    void cacheFailed(const QString &message);
    void closed();
    void completed(quint64 ticket, const QJsonArray &presentations);

private:
    friend class SongMetadataServiceTestAccess;
    struct Job
    {
        QSet<QString> waiting;
        QJsonArray values;
        QSet<QString> retried;
    };
    void drain();
    void accept(quint64, const QByteArray &);
    void fail(quint64, bool throttled = false);
    void settle(const QString &, const QJsonObject &, bool cache);
    void clear();
    void notifyObservers();
    void refreshSqlCoverAliases();
    void watchSqlChunk(QObject *owner);
    void rememberCoverFacts(const QJsonObject &song, bool replace = false);
    QStringList observationKeys(const QJsonObject &song) const;
    QJsonObject artistCover(const QJsonObject &song);
    struct Observer
    {
        quint64 sqlId = 0;
        QList<Observation> sqlPending;
        qsizetype sqlCursor = 0;
        bool sqlActive = false, sqlWriting = false;
        QHash<QString, CompactSet<QString>> rows;
        std::function<void(const QStringList &)> apply;
        QSet<quint64> tickets;
        QMetaObject::Connection destroyed;
    };
    std::unique_ptr<SongObservationIndex> m_sqlIndex;
    QPointer<SessionDatabase> m_sessionDatabase;
    quint64 m_sqlOwner = 0, m_sqlEpoch = 0;
    bool m_sqlLookup = false;
    QHash<QObject *, Observer> m_observers;
    QHash<QString, CompactSet<QObject *>> m_listeners;
    QSet<QString> m_pendingKeys;
    QTimer m_notify;
    ValueCache<QString, QJsonObject> m_embedded{2000, 4'000'000};
    ValueCache<QString, QJsonObject> m_coverFacts{2000, 2'000'000};
    std::unique_ptr<EmbeddedCoverCache> m_covers;
    QHash<QString, QString> m_coverIdentities;
    ApiClient &m_api;
    ValueCache<QString, QJsonObject> m_cache{2000, 8'000'000};
    QElapsedTimer m_clock;
    QTimer m_drain;
    QTimer m_retry;
    QHash<quint64, Job> m_jobs;
    QHash<quint64, QStringList> m_flights;
    quint64 m_ticket = 0;
    qint64 m_cooldown = 0;
    int m_failures = 0;
    bool m_closed = false;
    bool m_notifying = false;
};
} // namespace EchoMusic
