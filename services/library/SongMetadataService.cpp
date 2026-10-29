#include "SongMetadataService.h"
#include "EmbeddedCoverCache.h"
#include "EmbeddedCoverPresentation.h"
#include "ArtistCoverPresentation.h"
#include <QJsonDocument>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopedValueRollback>
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
int coverPriority(const QJsonObject &song)
{
    const auto cover = song["cover_url"].toString();
    if (cover.trimmed().isEmpty())
        return 0;
    const auto metadata = song["metadata"].toObject();
    return hasArtistCover(metadata, cover) ? 1 : hasEmbeddedCover(metadata, cover) ? 2 : 3;
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
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &SongMetadataService::drain);
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
        if (!key.startsWith("id:") && !key.startsWith("artist:"))
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
QStringList songCoverKeys(const QJsonObject &song)
{
    // Album artwork belongs to a catalog version. Exact audio identities are
    // only the fallback when this file has no catalog identity at all.
    const auto id = catalogId(song);
    return id.isEmpty() ? SongMetadataService::identityKeys(song) : QStringList{"id:" + id};
}
} // namespace
bool SongMetadataService::needsCompletion(const QJsonObject &s)
{
    if (missingText(s["title"].toString()) || missingText(s["cover_url"].toString()) ||
        hasEmbeddedCover(s["metadata"].toObject(), s["cover_url"].toString()) ||
        hasArtistCover(s["metadata"].toObject(), s["cover_url"].toString()) ||
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
    if (coverPriority(p) > coverPriority(s) &&
        hasEmbeddedCover(s["metadata"].toObject(), s["cover_url"].toString()))
        s = withoutEmbeddedCover(s);
    if (coverPriority(p) > coverPriority(s) &&
        hasArtistCover(s["metadata"].toObject(), s["cover_url"].toString()))
        s = withoutArtistCover(s);
    if (const auto artists = presentationArtists(s); !artists.isEmpty() || s.contains("artists"))
        s["artists"] = artists;
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
    if (!p["embedded_text"].toBool())
        s["artists"] = mergeArtistAvatars(s["artists"].toArray(), p["artists"].toArray(), true);
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
        !p["cover_url"].toString().trimmed().isEmpty()) {
        s["cover_url"] = p["cover_url"];
        auto metadata = s["metadata"].toObject();
        for (const auto &key : {"artist_cover_url", "embedded_cover_url"})
            if (p["metadata"].toObject()[key].toString() == p["cover_url"].toString())
                metadata[key] = p["cover_url"];
        s["metadata"] = metadata;
    }
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
    auto result = withoutArtistCover(withoutEmbeddedCover(song));
    if (const auto artists = presentationArtists(result);
        !artists.isEmpty() || result.contains("artists"))
        result["artists"] = artists;
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
    rememberCoverFacts(result);
    if (result["cover_url"].toString().trimmed().isEmpty())
        for (const auto &key : songCoverKeys(song)) {
            const auto *entry = m_coverFacts.object(key);
            if (entry && entry->expires > m_clock.elapsed() &&
                !entry->value["song_cover_url"].toString().isEmpty()) {
                result["cover_url"] = entry->value["song_cover_url"];
                break;
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
    if (result["cover_url"].toString().trimmed().isEmpty()) {
        const auto candidate = artistCover(result);
        const auto url = artistAvatar(candidate);
        if (!url.isEmpty()) {
            result["cover_url"] = url;
            auto meta = result["metadata"].toObject();
            meta["artist_cover_url"] = url;
            result["metadata"] = meta;
        }
    }
    return result;
}
QStringList SongMetadataService::observationKeys(const QJsonObject &song) const
{
    auto keys = identityKeys(song);
    if (!song["metadata"].toObject()["plugin_source"].toString().isEmpty())
        return keys;
    for (const auto &value : presentationArtists(song))
        if (const auto id = value.toObject()["id"].toVariant().toULongLong(); id)
            keys.append("artist:" + QString::number(id));
    keys.removeDuplicates();
    return keys;
}
void SongMetadataService::rememberCoverFacts(const QJsonObject &song, bool replace)
{
    // Observer callbacks only replay facts. Re-publication on every cache miss
    // would keep a catalog larger than the cache in an endless eviction loop.
    if (m_closed || (m_notifying && !replace))
        return;
    const auto cover = coverPriority(song) == 3 &&
                               song["metadata"].toObject()["plugin_source"].toString().isEmpty()
                           ? presentationImageUrl(song["cover_url"].toString())
                           : QString();
    const auto remember = [this, replace, &cover](const QString &key, const QJsonObject &artist,
                                                  bool songIdentity) {
        auto *previous = m_coverFacts.object(key);
        const bool valid = previous && previous->expires > m_clock.elapsed();
        auto value = valid ? previous->value : QJsonObject{};
        if (!artistAvatar(artist).isEmpty() && (replace || artistAvatar(value).isEmpty()))
            for (const auto &field : {"id", "name", "avatar_url"})
                value[field] = artist[field];
        if (songIdentity && !cover.isEmpty() &&
            (replace || value["song_cover_url"].toString().isEmpty()))
            value["song_cover_url"] = cover;
        if (value.isEmpty() || (valid && previous->value == value))
            return;
        m_coverFacts.insert(
            key, new CacheEntry{value, m_clock.elapsed() + 24 * 60 * 60 * 1000},
            std::max(1000, int(QJsonDocument(value).toJson(QJsonDocument::Compact).size() * 2)));
        m_pendingKeys.insert(key);
    };
    const auto refs = presentationArtists(song);
    const auto identities = identityKeys(song);
    const auto coverKeys = songCoverKeys(song);
    const bool catalogArtists = song["metadata"].toObject()["plugin_source"].toString().isEmpty();
    // An online completion can reveal an artist ID after the original watch.
    // Extend only matching row indices; never resubscribe an entire catalog.
    for (const auto &value : refs) {
        const auto id = value.toObject()["id"].toVariant().toULongLong();
        if (!id || !catalogArtists)
            continue;
        const auto artistKey = "artist:" + QString::number(id);
        for (const auto &key : identities)
            for (auto *owner : m_listeners.value(key)) {
                auto &observer = m_observers[owner];
                observer.rows[artistKey].unite(observer.rows.value(key));
                m_listeners[artistKey].insert(owner);
            }
    }
    QJsonObject first;
    for (const auto &value : refs) {
        const auto artist = value.toObject();
        const auto avatar = artistAvatar(artist);
        if (avatar.isEmpty() ||
            (!artist["id"].toVariant().toULongLong() && missingText(artist["name"].toString())))
            continue;
        // Retain only the identity and URL required by presentation completion.
        const QJsonObject fact{
            {"id", artist["id"]}, {"name", artist["name"]}, {"avatar_url", avatar}};
        if (first.isEmpty())
            first = fact;
        if (const auto id = artist["id"].toVariant().toULongLong(); id && catalogArtists)
            remember("artist:" + QString::number(id), fact, false);
    }
    if (!first.isEmpty() || !cover.isEmpty())
        for (const auto &key : identities)
            remember(key, first, coverKeys.contains(key));
    if (!m_pendingKeys.isEmpty())
        m_notify.start();
}
QJsonObject SongMetadataService::artistCover(const QJsonObject &song)
{
    const auto read = [this](const QString &key) -> QJsonObject {
        if (auto *entry = m_coverFacts.object(key); entry && entry->expires > m_clock.elapsed())
            return entry->value;
        m_coverFacts.remove(key);
        return {};
    };
    const auto artists = presentationArtists(song);
    const bool catalogArtists = song["metadata"].toObject()["plugin_source"].toString().isEmpty();
    for (const auto &value : artists) {
        const auto artist = value.toObject();
        if (const auto id = artist["id"].toVariant().toULongLong(); id && catalogArtists)
            if (const auto cached = read("artist:" + QString::number(id)); !cached.isEmpty())
                return cached;
        if (!artistAvatar(artist).isEmpty() &&
            (artist["id"].toVariant().toULongLong() || !missingText(artist["name"].toString())))
            return artist;
    }
    for (const auto &key : identityKeys(song)) {
        const auto cached = read(key);
        if (cached.isEmpty())
            continue;
        if (!hasArtistName(artists))
            return cached;
        for (const auto &value : artists)
            if (samePresentationArtist(value.toObject(), cached))
                return cached;
    }
    return {};
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
        for (const auto &key : observationKeys(entry.song)) {
            observer.rows[key].insert(entry.row);
            m_listeners[key].insert(owner);
            if (m_covers && !key.startsWith("id:") && !key.startsWith("artist:"))
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
    QScopedValueRollback notifying(m_notifying, true);
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
        } else
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
    m_retry.stop();
    m_jobs.clear();
    m_cache.clear();
    m_embedded.clear();
    m_coverFacts.clear();
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
    if (cache && !value.isEmpty())
        rememberCoverFacts(value, true);
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
    m_drain.stop();
    m_retry.stop();
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
        // One deadline for live consumers, never a polling timer. cancel/clear
        // discards the demand, so a hidden page cannot restart transport.
        if (!wanted.isEmpty())
            m_retry.start(int(m_cooldown - m_clock.elapsed()));
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
            if (ids.size() == 50)
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
    // Keep each live consumer for one bounded retry. A second failure (or a
    // business/rate-limit rejection) settles it without poisoning negative
    // cache entries. A future explicit demand can try again after cooldown.
    for (auto &job : m_jobs)
        for (const auto &id : ids)
            if (job.waiting.contains(id)) {
                if (throttled || job.retried.contains(id))
                    job.waiting.remove(id);
                else
                    job.retried.insert(id);
            }
    m_drain.start();
}
} // namespace EchoMusic
