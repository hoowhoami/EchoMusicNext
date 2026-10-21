#include "SongMetadataService.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
namespace EchoMusic
{
namespace
{
bool hasArtistName(const QJsonValue &value)
{
    const auto artists = value.toArray();
    return std::any_of(artists.begin(), artists.end(), [](const QJsonValue &artist) {
        const auto name = artist.toObject().value("name").toString().trimmed();
        return !name.isEmpty() && !SongMetadataService::maskedName(name);
    });
}
bool needsArtistCompletion(const QJsonObject &song)
{
    return !hasArtistName(song["artists"]) ||
           !song["metadata"].toObject()["filename_artist_name"].toString().isEmpty();
}
} // namespace
QJsonArray SongMetadataService::artistRefs(const QString &artist)
{
    QJsonArray refs;
    const auto names = artist.split(QRegularExpression(QStringLiteral("\\s*/\\s*")),
                                    Qt::SkipEmptyParts);
    for (const auto &name : names)
        if (!name.trimmed().isEmpty())
            refs.append(QJsonObject{{"name", name.trimmed()}, {"id", {}}});
    return refs;
}
bool SongMetadataService::maskedName(const QString &value)
{
    // Provider policies censor some artist names with asterisk runs (e.g.
    // "***"); those are placeholders rather than usable facts.
    static const QRegularExpression masked(QStringLiteral("^\\*+$"));
    return masked.match(value.trimmed()).hasMatch();
}
SongMetadataService::SongMetadataService(ApiClient &api, QObject *parent)
    : QObject(parent), m_api(api)
{
    m_clock.start();
    m_drain.setSingleShot(true);
    m_drain.setInterval(0);
    connect(&m_drain, &QTimer::timeout, this, &SongMetadataService::drain);
    connect(&api, &ApiClient::succeeded, this, &SongMetadataService::accept);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &bytes) {
        const auto error = QJsonDocument::fromJson(bytes).object();
        const auto code = error["code"].toInt();
        fail(id, error["kind"] == "business" || code == 403 || code == 429);
    });
    connect(&api, &ApiClient::sessionChanged, this, &SongMetadataService::clear);
    connect(&api, &ApiClient::closed, this, &SongMetadataService::shutdown);
}
bool SongMetadataService::needsCompletion(const QJsonObject &s)
{
    return s["cover_url"].toString().isEmpty() || s["album_id"].toString().isEmpty() ||
           s["album_id"].toString() == "0" || s["album_title"].toString().isEmpty() ||
           needsArtistCompletion(s);
}
QJsonObject SongMetadataService::completeSong(QJsonObject s, const QJsonObject &p)
{
    // Presentations address rows by view id; an unmatched cloud upload keeps
    // the raw id "0" (or an empty one) in its domain song until a catalog
    // identity appears, so those songs accept file-scoped facts.
    const auto id = s["id"].toString();
    if (id != p["id"] && id != "0" && !id.isEmpty())
        return s;
    if (needsArtistCompletion(s) && hasArtistName(p["artists"])) {
        s["artists"] = p["artists"];
        // Canonical metadata replaces provisional filename presentation without
        // modifying the original filename or its artist facts.
        if (!p["title"].toString().isEmpty())
            s["title"] = p["title"];
    }
    if (s["album_id"].toString().isEmpty() || s["album_id"].toString() == "0")
        s["album_id"] = p["album_id"];
    if (s["album_title"].toString().isEmpty())
        s["album_title"] = p["album_title"];
    if (s["cover_url"].toString().isEmpty())
        s["cover_url"] = p["cover_url"];
    // A missing duration is a display gap, not a conflict; the playing file's
    // parsed duration is authoritative for the same audio.
    if (s["duration_ms"].toDouble() <= 0 && p["duration_ms"].toDouble() > 0)
        s["duration_ms"] = p["duration_ms"];
    auto meta = s["metadata"].toObject();
    if (hasArtistName(p["artists"]))
        meta.remove("filename_artist_name");
    if (!meta.contains("artists") && hasArtistName(p["artists"]))
        meta["artists"] = p["artists"];
    s["metadata"] = meta;
    return s;
}
quint64 SongMetadataService::request(const QStringList &ids)
{
    if (m_closed || ids.isEmpty())
        return 0;
    const auto ticket = ++m_ticket;
    Job job;
    for (const auto &id : ids) {
        bool valid = false;
        const auto n = id.toULongLong(&valid);
        if (!valid || !n)
            continue;
        if (auto *cached = m_cache.object(id); cached && cached->expires > m_clock.elapsed()) {
            if (!cached->value.isEmpty())
                job.values.append(cached->value);
        } else if (m_clock.elapsed() >= m_cooldown)
            job.waiting.insert(id);
    }
    m_jobs.insert(ticket, std::move(job));
    m_drain.start();
    return ticket;
}
void SongMetadataService::cancel(quint64 ticket)
{
    m_jobs.remove(ticket);
    m_drain.start();
}
void SongMetadataService::clear()
{
    m_drain.stop();
    m_jobs.clear();
    const auto ids = m_flights.keys();
    m_flights.clear();
    for (auto id : ids)
        m_api.cancel(id);
}
void SongMetadataService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    clear();
}
void SongMetadataService::settle(const QString &id, const QJsonObject &value, bool cache)
{
    if (cache)
        m_cache.insert(
            id, new CacheEntry{value, m_clock.elapsed() + (value.isEmpty() ? 30 * 60 * 1000
                                                                           : 24 * 60 * 60 * 1000)});
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
        if (it->waiting.remove(id) && !value.isEmpty())
            it->values.append(value);
}
void SongMetadataService::drain()
{
    if (m_closed)
        return;
    for (auto id : m_jobs.keys())
        if (m_jobs.contains(id) && m_jobs[id].waiting.isEmpty()) {
            const auto values = m_jobs.take(id).values;
            emit completed(id, values);
        }
    // Retire a transport only after its last consumer has gone away.
    QSet<QString> wanted;
    for (const auto &job : std::as_const(m_jobs))
        wanted.unite(job.waiting);
    for (auto request : m_flights.keys()) {
        bool needed = false;
        for (const auto &id : m_flights[request])
            needed |= wanted.contains(id);
        if (!needed) {
            m_flights.remove(request);
            m_api.cancel(request);
        }
    }
    if (m_clock.elapsed() < m_cooldown) {
        for (const auto &id : wanted)
            settle(id, {}, false);
        if (!m_jobs.isEmpty())
            m_drain.start();
        return;
    }
    QSet<QString> busy;
    for (const auto &ids : std::as_const(m_flights))
        for (const auto &id : ids)
            busy.insert(id);
    wanted.subtract(busy);
    while (!wanted.isEmpty() && m_flights.size() < 3) {
        QStringList ids;
        QList<quint64> numeric;
        for (const auto &id : wanted) {
            ids.append(id);
            numeric.append(id.toULongLong());
            if (ids.size() == 100)
                break;
        }
        for (const auto &id : ids)
            wanted.remove(id);
        const auto request = m_api.songMetadata(numeric);
        if (request)
            m_flights.insert(request, ids);
        else {
            for (const auto &id : ids)
                settle(id, {}, false);
            m_drain.start();
        }
    }
}
void SongMetadataService::accept(quint64 request, const QByteArray &bytes)
{
    if (!m_flights.contains(request))
        return;
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isArray()) {
        fail(request);
        return;
    }
    const auto ids = m_flights.take(request);
    QHash<QString, QJsonObject> values;
    for (const auto &v : doc.array()) {
        const auto o = v.toObject();
        const auto id = o["id"].toString();
        if (!ids.contains(id) || values.contains(id)) {
            m_flights.insert(request, ids);
            fail(request);
            return;
        }
        values.insert(id, o);
    }
    m_failures = 0;
    for (const auto &id : ids)
        settle(id, values.value(id), true);
    m_drain.start();
}
void SongMetadataService::fail(quint64 request, bool throttled)
{
    if (!m_flights.contains(request))
        return;
    const auto ids = m_flights.take(request);
    const auto delay = throttled
                           ? 30 * 60 * 1000
                           : std::min<qint64>(30 * 60 * 1000, 60000LL << std::min(m_failures++, 5));
    if (m_clock.elapsed() >= m_cooldown)
        qWarning("Song metadata completion deferred after request failure");
    m_cooldown = m_clock.elapsed() + delay;
    for (const auto &id : ids)
        settle(id, {}, false);
    m_drain.start();
}
} // namespace EchoMusic
