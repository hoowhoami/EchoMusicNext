#pragma once
#include "ApiClient.h"
#include <QCache>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
namespace EchoMusic
{
/// Shared optional presentation completion: 100 IDs/batch, three transports,
/// deduplicated consumers and a bounded public-facts cache. No playback rights.
class SongMetadataService final : public QObject
{
    Q_OBJECT
public:
    explicit SongMetadataService(ApiClient &, QObject *parent = nullptr);
    quint64 request(const QStringList &ids);
    void cancel(quint64 ticket);
    void shutdown();
    static bool needsCompletion(const QJsonObject &song);
    static QJsonObject completeSong(QJsonObject song, const QJsonObject &presentation);
    /// Artist reference objects for a tag string whose names join with "/".
    static QJsonArray artistRefs(const QString &artist);
    /// Provider policies censor some artist names with asterisk runs ("***").
    static bool maskedName(const QString &value);
signals:
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
    ApiClient &m_api;
    QCache<QString, CacheEntry> m_cache{2000};
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
