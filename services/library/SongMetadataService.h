#pragma once
#include "ApiClient.h"
#include <QCache>
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
/// Shared optional presentation completion: 100 IDs/batch, three transports,
/// deduplicated consumers and a bounded public-facts cache. No playback rights.
class SongMetadataService final : public QObject
{
    Q_OBJECT
public:
    explicit SongMetadataService(ApiClient &, QObject *parent = nullptr);
    ~SongMetadataService() override;
    /// Root is an application-owned cache directory; tests use an isolated root.
    void enableCoverCache(const QString &directory);
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
    void unwatch(QObject *owner);
    QJsonObject complete(const QJsonObject &song);
    /// Raw reader text, scoped by exact audio hash/path. No cover, lyrics or IDs.
    void publishEmbedded(const QJsonObject &song);
    static QStringList identityKeys(const QJsonObject &song);
    static bool needsCompletion(const QJsonObject &song);
    static QJsonObject completeSong(QJsonObject song, const QJsonObject &presentation);
    /// Artist reference objects for a tag string whose names join with "/".
    static QJsonArray artistRefs(const QString &artist);
    /// Provider policies censor some artist names with asterisk runs ("***").
    static bool maskedName(const QString &value);
signals:
    void cacheCleared();
    void closed();
    void completed(quint64 ticket, const QJsonArray &presentations);

private:
    friend class SongMetadataServiceTestAccess;
    struct CacheEntry
    {
        QJsonObject value;
        qint64 expires = 0;
    };
    struct Job
    {
        QSet<QString> waiting;
        QJsonArray values;
    };
    void drain();
    void accept(quint64, const QByteArray &);
    void fail(quint64, bool throttled = false);
    void settle(const QString &, const QJsonObject &, bool cache);
    void clear();
    void notifyObservers();
    struct Observer
    {
        QHash<QString, QStringList> rows;
        std::function<void(const QStringList &)> apply;
        QSet<quint64> tickets;
        QMetaObject::Connection destroyed;
    };
    QHash<QObject *, Observer> m_observers;
    QHash<QString, QSet<QObject *>> m_listeners;
    QSet<QString> m_pendingKeys;
    QTimer m_notify;
    QCache<QString, CacheEntry> m_embedded{4'000'000};
    std::unique_ptr<EmbeddedCoverCache> m_covers;
    QHash<QString, QString> m_coverIdentities;
    ApiClient &m_api;
    QCache<QString, CacheEntry> m_cache{8'000'000};
    QElapsedTimer m_clock;
    QTimer m_drain;
    QHash<quint64, Job> m_jobs;
    QHash<quint64, QStringList> m_flights;
    quint64 m_ticket = 0;
    qint64 m_cooldown = 0;
    int m_failures = 0;
    bool m_closed = false;
};
} // namespace EchoMusic
