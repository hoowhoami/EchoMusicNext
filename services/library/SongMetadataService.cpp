#include "SongMetadataService.h"
#include "EmbeddedCoverCache.h"
#include "EmbeddedCoverPresentation.h"
#include <QJsonDocument>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <utility>
namespace EchoMusic
{
namespace
{
bool missingText(const QString &value);
bool hasArtistName(const QJsonValue &value)
{
    const auto artists = value.toArray();
    return std::any_of(artists.begin(), artists.end(), [](const QJsonValue &artist) {
        const auto name = artist.toObject().value("name").toString().trimmed();
        return !missingText(name);
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
    const auto names =
        artist.split(QRegularExpression(QStringLiteral("\\s*/\\s*")), Qt::SkipEmptyParts);
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
    m_notify.setSingleShot(true);
    connect(&m_notify, &QTimer::timeout, this, &SongMetadataService::notifyObservers);
    connect(this, &SongMetadataService::completed, this, [this](quint64 ticket) {
        for (auto &observer : m_observers)
            observer.tickets.remove(ticket);
    });
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
SongMetadataService::~SongMetadataService()
{
    shutdown();
}
void SongMetadataService::enableCoverCache(const QString &directory)
{
    if (m_closed || m_covers || directory.isEmpty())
        return;
    m_covers = std::make_unique<EmbeddedCoverCache>(directory, EmbeddedCoverCache::Limits{});
    connect(m_covers.get(), &EmbeddedCoverCache::closed, this, &SongMetadataService::closed);
    connect(m_covers.get(), &EmbeddedCoverCache::changed, this, [this](const QStringList &keys) {
        if (m_closed)
            return;
        for (const auto &key : keys)
            if (const auto identity = m_coverIdentities.value(key); !identity.isEmpty())
                m_pendingKeys.insert(identity);
        if (!m_pendingKeys.isEmpty())
            m_notify.start();
    });
    m_covers->setScope(m_api.currentAccountUserId());
    for (const auto &key : m_listeners.keys())
        if (!key.startsWith("id:"))
            m_coverIdentities.insert(m_covers->identityKey(key), key);
    // Re-project existing leases as well as asynchronously restored entries.
    m_pendingKeys.unite(QSet<QString>(m_listeners.keyBegin(), m_listeners.keyEnd()));
    if (!m_pendingKeys.isEmpty())
        m_notify.start();
}
void SongMetadataService::publishEmbeddedCover(const QJsonObject &song, const QString &file)
{
    if (!m_closed && m_covers)
        m_covers->store(identityKeys(song), file);
}
namespace
{
bool missingText(const QString &value)
{
    const auto text = value.trimmed().toLower();
    return text.isEmpty() || SongMetadataService::maskedName(text) ||
           QStringList{"未知歌曲", "未知歌手",      "未知艺术家",     "未知专辑",
                       "unknown",  "unknown title", "unknown artist", "unknown album"}
               .contains(text);
}
QString catalogId(const QJsonObject &song)
{
    // Plugin/file IDs are opaque even when their spelling happens to be numeric.
    const auto meta = song["metadata"].toObject();
    if (!song["local_path"].toString().isEmpty() || !meta["plugin_source"].toString().isEmpty())
        return {};
    const auto cloud = meta["cloud_source"].toObject();
    if (const auto id = cloud["song_id"].toInteger(); id > 0)
        return QString::number(id);
    const auto id = song["id"].toString();
    bool ok = false;
    const auto numeric = id.toULongLong(&ok);
    return ok && numeric ? QString::number(numeric) : QString();
}
} // namespace
bool SongMetadataService::needsCompletion(const QJsonObject &s)
{
    if (missingText(s["title"].toString()) || missingText(s["cover_url"].toString()) ||
        hasEmbeddedCover(s["metadata"].toObject(), s["cover_url"].toString()) ||
        !s["album_id"].toString().toULongLong() || missingText(s["album_title"].toString()) ||
        needsArtistCompletion(s))
        return true;
    for (const auto &artist : s["artists"].toArray())
        if (!artist.toObject()["id"].toVariant().toULongLong())
            return true;
    return false;
}
QJsonObject SongMetadataService::completeSong(QJsonObject s, const QJsonObject &p)
{
    const auto id = s["id"].toString();
    if (id != p["id"] && id != "0" && !id.isEmpty())
        return s;
    if (!p["cover_url"].toString().isEmpty() &&
        hasEmbeddedCover(s["metadata"].toObject(), s["cover_url"].toString()))
        s = withoutEmbeddedCover(s);
    const bool provisional = !s["metadata"].toObject()["filename_artist_name"].toString().isEmpty();
    const bool artistGap = needsArtistCompletion(s);
    if (artistGap && hasArtistName(p["artists"]))
        s["artists"] = p["artists"];
    else if (hasArtistName(p["artists"])) {
        // Only a matching explicit provider reference may add an ID to an
        // existing tag name. Never attach one singer's ID to another name.
        auto artists = s["artists"].toArray();
        for (qsizetype i = 0; i < artists.size(); ++i) {
            auto artist = artists[i].toObject();
            if (artist["id"].toVariant().toULongLong())
                continue;
            for (const auto &value : p["artists"].toArray()) {
                const auto ref = value.toObject();
                if (ref["id"].toVariant().toULongLong() &&
                    ref["name"].toString().trimmed().compare(artist["name"].toString().trimmed(),
                                                             Qt::CaseInsensitive) == 0) {
                    artist["id"] = ref["id"];
                    artists[i] = artist;
                    break;
                }
            }
        }
        s["artists"] = artists;
    }
    const auto localPath = s["local_path"].toString();
    const bool filenameTitle =
        !localPath.isEmpty() && s["title"].toString() == QFileInfo(localPath).completeBaseName();
    if ((missingText(s["title"].toString()) || filenameTitle || provisional ||
         (artistGap && !p["embedded_text"].toBool())) &&
        !missingText(p["title"].toString()))
        s["title"] = p["title"];
    if (!s["album_id"].toString().toULongLong() && p["album_id"].toString().toULongLong())
        s["album_id"] = p["album_id"];
    if (missingText(s["album_title"].toString()) && !missingText(p["album_title"].toString()))
        s["album_title"] = p["album_title"];
    if (s["cover_url"].toString().trimmed().isEmpty() &&
        !p["cover_url"].toString().trimmed().isEmpty())
        s["cover_url"] = p["cover_url"];
    if (s["duration_ms"].toDouble() <= 0 && p["duration_ms"].toDouble() > 0)
        s["duration_ms"] = p["duration_ms"];
    auto meta = s["metadata"].toObject();
    if (hasArtistName(s["artists"]) && s["artists"] != meta["artist_refs"])
        meta["artist_refs"] = s["artists"];
    if (artistGap && hasArtistName(p["artists"]))
        meta.remove("filename_artist_name");
    if (meta != s["metadata"].toObject())
        s["metadata"] = meta;
    return s;
}
QStringList SongMetadataService::identityKeys(const QJsonObject &song)
{
    QStringList keys;
    const auto id = catalogId(song);
    if (!id.isEmpty())
        keys.append("id:" + id);
    const auto hash = song["hash"].toString().trimmed().toLower();
    if (!hash.isEmpty())
        keys.append("hash:" + hash);
    const auto path = song["local_path"].toString();
    if (!path.isEmpty())
        keys.append("path:" + path);
    return keys;
}
QJsonObject SongMetadataService::complete(const QJsonObject &song)
{
    auto result = withoutEmbeddedCover(song);
    const auto id = catalogId(song);
    if (auto *cached = m_cache.object(id);
        cached && !cached->value.isEmpty() && cached->expires > m_clock.elapsed()) {
        auto presentation = cached->value;
        presentation["id"] = song["id"];
        result = completeSong(result, presentation);
    }
    for (const auto &key : identityKeys(song)) {
        if (key.startsWith("id:"))
            continue;
        if (auto *cached = m_embedded.object(key); cached && cached->expires > m_clock.elapsed()) {
            auto presentation = cached->value;
            presentation["id"] = song["id"];
            result = completeSong(result, presentation);
        }
    }
    if (m_covers && result["cover_url"].toString().trimmed().isEmpty()) {
        const auto url = m_covers->lookup(identityKeys(song));
        if (!url.isEmpty()) {
            result["cover_url"] = url;
            auto meta = result["metadata"].toObject();
            meta["embedded_cover_url"] = url;
            result["metadata"] = meta;
        }
    }
    return result;
}
void SongMetadataService::publishEmbedded(const QJsonObject &song)
{
    if (m_closed)
        return;
    QJsonObject text{{"embedded_text", true}};
    for (const auto &field : {"title", "album_title"})
        if (!missingText(song[field].toString()))
            text[field] = song[field].toString().trimmed();
    if (hasArtistName(song["artists"])) {
        QJsonArray refs;
        for (const auto &value : song["artists"].toArray()) {
            const auto name = value.toObject()["name"].toString().trimmed();
            if (!missingText(name))
                refs.append(QJsonObject{{"name", name}, {"id", QJsonValue::Null}});
        }
        text["artists"] = refs;
    }
    if (song["duration_ms"].toDouble() > 0)
        text["duration_ms"] = song["duration_ms"];
    if (text.size() == 1)
        return;
    for (const auto &key : identityKeys(song)) {
        if (key.startsWith("id:"))
            continue;
        auto *previous = m_embedded.object(key);
        if (previous && previous->value == text)
            continue;
        m_embedded.insert(
            key, new CacheEntry{text, m_clock.elapsed() + 24 * 60 * 60 * 1000},
            std::max(2000, int(QJsonDocument(text).toJson(QJsonDocument::Compact).size() * 2)));
        m_pendingKeys.insert(key);
    }
    if (!m_pendingKeys.isEmpty())
        m_notify.start();
}
void SongMetadataService::unwatch(QObject *owner)
{
    auto found = m_observers.find(owner);
    if (found == m_observers.end())
        return;
    const auto observer = m_observers.take(owner);
    disconnect(observer.destroyed);
    for (const auto ticket : observer.tickets)
        cancel(ticket);
    for (auto it = observer.rows.cbegin(); it != observer.rows.cend(); ++it) {
        auto listeners = m_listeners.find(it.key());
        if (listeners != m_listeners.end() && listeners->remove(owner) && listeners->isEmpty())
            m_listeners.erase(listeners);
        if (m_covers && !m_listeners.contains(it.key()))
            m_coverIdentities.remove(m_covers->identityKey(it.key()));
    }
}
void SongMetadataService::watch(QObject *owner, const QList<Observation> &songs,
                                std::function<void(const QStringList &)> apply, bool active,
                                bool append)
{
    if (!append)
        unwatch(owner);
    if (m_closed || !owner || songs.isEmpty())
        return;
    if (!m_observers.contains(owner)) {
        Observer observer;
        observer.destroyed =
            connect(owner, &QObject::destroyed, this, [this, owner] { unwatch(owner); });
        m_observers.insert(owner, std::move(observer));
    }
    auto &observer = m_observers[owner];
    observer.apply = std::move(apply);
    QSet<QString> ids;
    for (const auto &entry : songs) {
        for (const auto &key : identityKeys(entry.song)) {
            observer.rows[key].append(entry.row);
            m_listeners[key].insert(owner);
            if (m_covers && !key.startsWith("id:"))
                m_coverIdentities.insert(m_covers->identityKey(key), key);
        }
        const auto id = catalogId(entry.song);
        if (active && !id.isEmpty() && needsCompletion(complete(entry.song)))
            ids.insert(id);
    }
    if (!ids.isEmpty())
        if (const auto ticket = request(QStringList(ids.cbegin(), ids.cend())))
            observer.tickets.insert(ticket);
}
void SongMetadataService::notifyObservers()
{
    const auto keys = std::exchange(m_pendingKeys, {});
    QHash<QObject *, QSet<QString>> rows;
    for (const auto &key : keys)
        for (auto *owner : m_listeners.value(key))
            for (const auto &row : m_observers.value(owner).rows.value(key))
                rows[owner].insert(row);
    QHash<QObject *, QPointer<QObject>> guards;
    for (auto *owner : rows.keys())
        guards.insert(owner, owner);
    // Callbacks may replace subscriptions or destroy another owner.
    for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
        if (!guards.value(it.key()))
            continue;
        const auto found = m_observers.constFind(it.key());
        if (found == m_observers.cend())
            continue;
        const auto apply = found->apply;
        apply(QStringList(it->cbegin(), it->cend()));
    }
}
quint64 SongMetadataService::request(const QStringList &ids)
{
    if (m_closed || ids.isEmpty())
        return 0;
    const auto ticket = ++m_ticket;
    Job job;
    QSet<QString> seen;
    for (const auto &value : ids) {
        bool valid = false;
        const auto n = value.toULongLong(&valid);
        if (!valid || !n)
            continue;
        const auto id = QString::number(n);
        if (seen.contains(id))
            continue;
        seen.insert(id);
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
    m_cache.clear();
    m_embedded.clear();
    m_coverIdentities.clear();
    if (m_covers && !m_closed)
        m_covers->setScope(m_api.currentAccountUserId());
    m_notify.stop();
    m_pendingKeys.clear();
    m_cooldown = 0;
    m_failures = 0;
    for (auto &observer : m_observers)
        observer.tickets.clear();
    const auto ids = m_flights.keys();
    m_flights.clear();
    for (auto id : ids)
        m_api.cancel(id);
    if (!m_closed)
        emit cacheCleared();
}
void SongMetadataService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    clear();
    const auto owners = m_observers.keys();
    for (auto *owner : owners)
        unwatch(owner);
    if (m_covers)
        m_covers->shutdown();
    else
        emit closed();
}
void SongMetadataService::settle(const QString &id, const QJsonObject &value, bool cache)
{
    if (cache)
        m_cache.insert(
            id,
            new CacheEntry{value, m_clock.elapsed() +
                                      (value.isEmpty() ? 30 * 60 * 1000 : 24 * 60 * 60 * 1000)},
            std::max(4000, int(QJsonDocument(value).toJson(QJsonDocument::Compact).size() * 2)));
    if (cache && !value.isEmpty())
        m_pendingKeys.insert("id:" + id);
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
    // Existing page consumers update their source snapshots before shared views
    // receive the same result, keeping headers/automatic covers coherent.
    if (!m_pendingKeys.isEmpty())
        m_notify.start();
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
