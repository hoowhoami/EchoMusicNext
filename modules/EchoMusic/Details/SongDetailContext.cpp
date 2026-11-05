#include "SongDetailContext.h"
#include "SongMetadataService.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
namespace EchoMusic
{
SongDetailContext::SongDetailContext(ApiClient &a, PlaybackService &p, RouteContext &r,
                                     bool network, CommentAuthorService *authors)
    : SongDetailContext(a, p, &r, std::nullopt, network, &r, authors)
{
}
SongDetailContext::SongDetailContext(ApiClient &a, PlaybackService &p, const PlaybackTrack &track,
                                     bool network, QObject *parent, CommentAuthorService *authors)
    : SongDetailContext(a, p, nullptr, track, network, parent, authors)
{
}
SongDetailContext::SongDetailContext(ApiClient &a, PlaybackService &p, RouteContext *r,
                                     std::optional<PlaybackTrack> entry, bool network,
                                     QObject *parent, CommentAuthorService *authors)
    : QObject(parent), m_api(a), m_playback(p), m_route(r), m_entryTrack(std::move(entry)),
      m_songs(this), m_actions(m_songs, p, this), m_comments(a, r, network, this, authors),
      m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_actions.setPlayInCurrentQueue(true);
    if (m_entryTrack) {
        const auto &track = *m_entryTrack;
        m_entryQuery = {{"type", "music"},      {"mixSongId", QString::number(track.songId)},
                        {"hash", track.hash},   {"albumId", QString::number(track.albumId)},
                        {"title", track.title}, {"artist", track.artist},
                        {"album", track.album}, {"cover", track.cover}};
    }
    connect(&a, &ApiClient::succeeded, this, &SongDetailContext::accept);
    connect(&a, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &b) { fail(id, b); });
    connect(&a, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_loaded = false;
        m_headerStatsRequested = false;
        m_qualities.clear();
        m_effects.clear();
        m_ranking = {};
        m_rankingDetails = {};
        m_comments.clear();
        hydrate();
        if (active())
            reload();
    });
    connect(&a, &ApiClient::closed, this, &SongDetailContext::shutdown);
    if (r) {
        connect(r, &RouteContext::activeChanged, this, &SongDetailContext::activate);
        connect(r, &RouteContext::queryChanged, this, &SongDetailContext::changed);
    }
    connect(&m_comments, &CommentsController::notice, this, &SongDetailContext::notice);
    connect(&m_comments, &CommentsController::readFailed, this,
            [this](const QString &message) { emit notice(message, true); });
    connect(&m_comments, &CommentsController::initialFinished, this,
            &SongDetailContext::requestHeaderStats);
    if (auto *metadata = p.metadata())
        connect(metadata, &SongMetadataService::completed, this,
                [this](quint64 ticket, const QJsonArray &values) {
                    if (!ticket || ticket != m_metadataTicket)
                        return;
                    m_metadataTicket = 0;
                    if (!m_closed && active()) {
                        applyMetadata(values);
                        rankingsAndComments();
                        emit changed();
                    }
                });
    connect(&m_songs, &SongCatalogModel::presentationsApplied, this, [this] {
        if (m_songs.totalCount() != 1)
            return;
        const auto track = m_songs.track(m_songs.sourceItem(0)["resourceId"].toString());
        if (!track)
            return;
        const auto song = track->toCatalog();
        m_track["metadata"] = track->catalogMetadata;
        for (const auto &field : {"title", "artists", "album_title", "album_id", "cover_url"})
            m_track[field] = song[field];
        m_detail["title"] = track->title;
        m_detail["artists"] = song["artists"];
        m_detail["artist"] = track->artist;
        m_detail["album"] = track->album;
        m_detail["cover_url"] = track->cover;
        if (!m_detail["album_id"].toVariant().toULongLong())
            m_detail["album_id"] = song["album_id"];
        emit changed();
    });
    hydrate();
    activate();
}
SongDetailContext::~SongDetailContext()
{
    shutdown();
}
QVariantMap SongDetailContext::query() const
{
    return m_route ? m_route->query() : m_entryQuery;
}
QString SongDetailContext::resourceId() const
{
    return m_route ? m_route->resourceId() : m_entryTrack ? m_entryTrack->id : QString();
}
bool SongDetailContext::active() const
{
    return !m_closed && (m_route ? m_route->active() : m_presented);
}
void SongDetailContext::setPresented(bool presented)
{
    if (m_route || m_closed || m_presented == presented)
        return;
    m_presented = presented;
    activate();
}
void SongDetailContext::hydrate()
{
    const auto q = query();
    m_mix = q.value("mixSongId", resourceId()).toULongLong();
    m_album = q.value("albumId").toULongLong();
    m_hash = q.value("hash").toString();
    if (m_hash.isEmpty() && QRegularExpression("^[0-9a-fA-F]{32}$").match(resourceId()).hasMatch())
        m_hash = resourceId();
    m_detail = QJsonObject::fromVariantMap({{"title", q.value("title")},
                                            {"artist", q.value("artist")},
                                            {"album", q.value("album")},
                                            {"cover_url", q.value("cover")},
                                            {"artist_id", q.value("artistId")},
                                            {"album_id", q.value("albumId")}});
    const auto &current = m_playback.currentTrack();
    if (current.songId == m_mix && m_mix) {
        m_track = current.toDomain();
        m_track["metadata"] = current.catalogMetadata;
        m_track["cover_url"] = current.cover;
        m_track["artists"] = current.catalogMetadata.value("artist_refs");
        m_track["album_title"] = current.album;
        if (m_hash.isEmpty())
            m_hash = current.hash;
        if (!m_album)
            m_album = current.albumId;
        const auto artists = m_track.value("artists").toArray();
        m_detail["artists"] = artists;
        if (!m_detail.value("artist_id").toVariant().toULongLong() && !artists.isEmpty())
            m_detail["artist_id"] = artists[0].toObject().value("id");
        if (!m_detail.value("album_id").toVariant().toULongLong() && m_album)
            m_detail["album_id"] = QString::number(m_album);
    } else
        m_track = {{"id", QString::number(m_mix)},
                   {"hash", m_hash},
                   {"title", q.value("title").toString()},
                   {"album_id", QString::number(m_album)},
                   {"album_title", q.value("album").toString()},
                   {"cover_url", q.value("cover").toString()},
                   {"artists", QJsonArray{QJsonObject{{"name", q.value("artist").toString()}}}}};
    if (m_detail.value("title").toString().isEmpty())
        m_detail["title"] = m_track.value("title");
    if (m_detail.value("artist").toString().isEmpty())
        m_detail["artist"] = current.songId == m_mix ? current.artist : QString();
    if (m_detail.value("cover_url").toString().isEmpty())
        m_detail["cover_url"] = m_track.value("cover_url");
    if (m_entryTrack)
        adoptEntryTrack(*m_entryTrack);
    else
        updateSong();
}
void SongDetailContext::adoptEntryTrack(const PlaybackTrack &track)
{
    if (query().value("type", "music").toString() != "music" ||
        (m_mix ? track.songId != m_mix : track.id != resourceId()) ||
        (!m_hash.isEmpty() && m_hash != track.hash))
        return;
    m_track = track.toDomain();
    m_track["metadata"] = track.catalogMetadata;
    m_track["artists"] = track.catalogMetadata.value("artist_refs");
    m_track["album_title"] = track.album;
    m_track["cover_url"] = track.cover;
    m_detail["artists"] = m_track.value("artists");
    for (const auto &pair : {qMakePair("title", track.title), qMakePair("artist", track.artist),
                             qMakePair("album", track.album), qMakePair("cover_url", track.cover)})
        if (m_detail.value(pair.first).toString().isEmpty())
            m_detail[pair.first] = pair.second;
    updateSong();
}
void SongDetailContext::updateSong()
{
    m_track["hash"] = m_hash;
    m_track["id"] = QString::number(m_mix);
    m_track["title"] = m_detail.value("title");
    m_track["cover_url"] = m_detail.value("cover_url");
    m_actions.setQueueContext(m_detail.value("title").toString(),
                              "queue:song-detail:" + resourceId());
    if (m_mix && !m_hash.isEmpty())
        m_songs.replaceSongs({m_track});
    emit changed();
}
namespace
{
QString positiveNavigationId(const QVariant &value)
{
    bool valid = false;
    const auto id = value.toString().trimmed().toULongLong(&valid);
    return valid && id ? QString::number(id) : QString();
}
} // namespace
QString SongDetailContext::artistNavigationId() const
{
    const auto type = query().value("type").toString();
    if (!type.isEmpty() && type != "music")
        return {};
    // Explicit route identity takes precedence over fetched metadata.
    auto id = positiveNavigationId(query().value("artistId"));
    if (id.isEmpty())
        id = positiveNavigationId(m_detail.value("artist_id").toVariant());
    return id;
}
QVariantList SongDetailContext::artistLinks() const
{
    PlaybackTrack presentation;
    presentation.artist = m_detail.value("artist").toString();
    const auto artists = m_detail.value("artists").toArray();
    const bool named = std::any_of(artists.begin(), artists.end(), [](const QJsonValue &v) {
        return !v.toObject().value("name").toString().trimmed().isEmpty();
    });
    if (named)
        presentation.catalogMetadata["artist_refs"] = artists;
    auto links = presentation.artistLinks();
    // Route text has only the first artist's explicit identity. Never assign
    // that identity to a different author returned by metadata or to every name.
    if (!named && !links.isEmpty()) {
        auto first = links[0].toMap();
        first["id"] = artistNavigationId();
        links[0] = first;
    }
    const auto type = query().value("type").toString();
    if (!type.isEmpty() && type != "music")
        for (auto &link : links) {
            auto entry = link.toMap();
            entry["id"] = QString();
            link = entry;
        }
    return links;
}
QString SongDetailContext::albumNavigationId() const
{
    const auto type = query().value("type").toString();
    if (!type.isEmpty() && type != "music")
        return {};
    auto id = positiveNavigationId(query().value("albumId"));
    if (id.isEmpty())
        id = positiveNavigationId(m_detail.value("album_id").toVariant());
    return id;
}
QVariantMap SongDetailContext::information() const
{
    auto info = m_detail.value("information").toObject();
    const auto metadata = m_track.value("metadata").toObject();
    // Catalog entry facts remain useful while the richer shared read loads.
    for (const auto &key : {"published_at", "language"})
        if (info.value(key).isNull() || info.value(key).isUndefined() ||
            info.value(key).toString().trimmed().isEmpty())
            info[key] = metadata.value(key).toString().trimmed().isEmpty() ? m_detail.value(key)
                                                                           : metadata.value(key);
    info["duration_ms"] = m_track.value("duration_ms");
    return info.toVariantMap();
}
bool SongDetailContext::loading() const
{
    if (m_metadataTicket)
        return true;
    for (const auto &p : m_pending)
        if (p.kind <= RankingDetails)
            return true;
    return false;
}
void SongDetailContext::request(quint64 id, Kind kind)
{
    if (id)
        m_pending.insert(id, {kind, m_api.sessionRevision(), m_epoch});
    else
        m_error = tr("歌曲详情请求未能启动");
}
void SongDetailContext::cancel()
{
    ++m_epoch;
    if (m_metadataTicket) {
        if (auto *metadata = m_playback.metadata())
            metadata->cancel(m_metadataTicket);
        m_metadataTicket = 0;
    }
    auto ids = m_pending.keys();
    m_pending.clear();
    for (auto id : ids)
        m_api.cancel(id);
    m_comments.suspend();
    m_commentsStarted = false;
}
void SongDetailContext::shutdown()
{
    m_songs.setMetadataActive(false);
    if (m_closed)
        return;
    m_closed = true;
    cancel();
    m_comments.shutdown();
    disconnect(&m_api, nullptr, this, nullptr);
}
void SongDetailContext::activate()
{
    const bool music = query().value("type", "music").toString() == "music";
    m_songs.attachMetadata(music && !commentsPage() ? m_playback.metadata() : nullptr);
    m_songs.setMetadataActive(!m_closed && m_network && active() && music && !commentsPage());
    if (m_closed)
        return;
    if (!active()) {
        cancel();
        emit changed();
    } else if (!m_loaded)
        reload();
    else if (commentsPage())
        m_comments.start();
}
void SongDetailContext::reload()
{
    if (m_closed)
        return;
    cancel();
    // Retries retain supplementary content that has already loaded.
    m_error.clear();
    m_rankingsStarted = false;
    m_loaded = false;
    if (m_network && active()) {
        const auto type = query().value("type", "music").toString();
        // A known resource can read comments without waiting for catalog facts
        // or rankings used only by the separate information page.
        if (commentsPage() && (type != "music" || m_mix)) {
            startComments();
            emit changed();
            return;
        }
        if (!m_hash.isEmpty())
            request(m_api.songPrivileges(
                        {{m_hash, m_album ? std::optional<quint64>(m_album) : std::nullopt}},
                        {AuthenticationMode::CurrentAccount, true}),
                    Privilege);
        else if (m_mix)
            requestMetadata();
        else
            rankingsAndComments();
    }
    emit changed();
}
void SongDetailContext::rankingsAndComments()
{
    if (m_rankingsStarted)
        return;
    m_rankingsStarted = true;
    if (m_mix) {
        request(m_api.songRanking(m_mix), Ranking);
        request(m_api.songRanking(m_mix, true), RankingDetails);
    } else {
        m_error = tr("歌曲详情缺少有效歌曲标识");
        startComments();
    }
    if (!loading())
        startComments();
    emit changed();
}
void SongDetailContext::requestMetadata()
{
    if (auto *metadata = m_playback.metadata()) {
        m_metadataTicket = metadata->request({QString::number(m_mix)});
        if (m_metadataTicket)
            return;
    } else {
        const auto id = m_api.songMetadata({m_mix}, {AuthenticationMode::CurrentAccount, true});
        request(id, Metadata);
        if (id)
            return;
    }
    rankingsAndComments();
}
bool SongDetailContext::commentsPage() const
{
    const auto type = query().value("type", "music").toString();
    return (m_route && m_route->name() == "song-comments") || type != "music";
}
void SongDetailContext::requestHeaderStats()
{
    if (!m_network || !active() || m_headerStatsRequested)
        return;
    m_headerStatsRequested = true;
    if (m_mix)
        request(m_api.favoriteCount(m_mix), FavoriteCount);
    if (!m_hash.isEmpty())
        request(m_api.commentCount(m_hash), CommentCount);
}
void SongDetailContext::startComments()
{
    m_loaded = true;
    if (!commentsPage()) {
        requestHeaderStats();
        emit changed();
        return;
    }
    if (m_commentsStarted)
        return;
    m_commentsStarted = true;
    const auto type = query().value("type", "music").toString();
    const auto resource = type == "album"      ? ApiClient::CommentResource::Album
                          : type == "playlist" ? ApiClient::CommentResource::Playlist
                                               : ApiClient::CommentResource::Music;
    m_comments.configure(resource,
                         resource == ApiClient::CommentResource::Music ? QString::number(m_mix)
                                                                       : resourceId(),
                         m_detail.value("title").toString(),
                         resource == ApiClient::CommentResource::Music ? m_mix : 0);
    m_comments.start();
    emit changed();
}
void SongDetailContext::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    auto p = m_pending.take(id);
    if (p.epoch != m_epoch || p.session != m_api.sessionRevision() || !active() || m_closed)
        return;
    auto doc = QJsonDocument::fromJson(bytes);
    switch (p.kind) {
    case Privilege: {
        auto a = doc.array();
        if (!a.isEmpty()) {
            auto v = a[0].toObject();
            if (!m_mix)
                m_mix = v.value("song_id").toVariant().toULongLong();
            if (m_detail.value("title").toString().isEmpty())
                m_detail["title"] = v.value("display_name");
            if (m_detail.value("artist").toString().isEmpty())
                m_detail["artist"] = v.value("artist_text");
            if (m_detail.value("album").toString().isEmpty())
                m_detail["album"] = v.value("album_title");
            auto primary = v.value("primary").toObject();
            if (m_detail.value("cover_url").toString().isEmpty())
                m_detail["cover_url"] = primary.value("cover_url");
            m_detail["language"] = v.value("language");
            m_qualities.clear();
            m_effects.clear();
            const QHash<QString, QString> qualityLabels{
                {"128", "SD"}, {"320", "HQ"}, {"flac", "SQ"}, {"high", "Hi-Res"}};
            const QSet<QString> effects{"piano",  "vocal", "accompaniment", "subwoofer", "ancient",
                                        "surnay", "dj"};
            for (const auto &item : v.value("variants").toArray()) {
                const auto quality = item.toObject().value("quality").toString();
                const auto label = qualityLabels.value(quality);
                if (!label.isEmpty() && !m_qualities.contains(label))
                    m_qualities.append(label);
                if (effects.contains(quality) && !m_effects.contains(quality))
                    m_effects.append(quality);
            }
            m_detail["privileges"] = v;
            // Catalog facts travel with a newly queued detail song. They do
            // not substitute for the playback service's account authorization.
            auto metadata = m_track.value("metadata").toObject();
            metadata["resource_privileges"] = v;
            m_track["metadata"] = metadata;
            m_track["privilege"] = primary.value("privilege");
            if (m_track.value("duration_ms").isNull() || m_track.value("duration_ms").isUndefined())
                m_track["duration_ms"] = primary.value("duration_ms");
            updateSong();
        }
        // Author and album IDs come from metadata, never from name guesses.
        if (commentsPage())
            startComments();
        else if (m_mix)
            requestMetadata();
        else
            rankingsAndComments();
        break;
    }
    case Metadata: {
        applyMetadata(doc.array());
        rankingsAndComments();
        break;
    }
    case Ranking:
        m_ranking = doc.object();
        m_detail["ranking_summary"] = m_ranking.value("title1");
        break;
    case RankingDetails:
        m_rankingDetails = doc.object();
        break;
    case FavoriteCount: {
        auto a = doc.array();
        if (!a.isEmpty())
            m_detail["favorite_count"] = a[0].toObject().value("count");
        break;
    }
    case CommentCount: {
        const auto a = QJsonDocument::fromJson("[" + bytes + "]").array();
        if (!a.isEmpty())
            m_detail["comment_count"] = a[0];
        break;
    }
    }
    if (m_rankingsStarted && !loading())
        startComments();
    emit changed();
}
void SongDetailContext::applyMetadata(const QJsonArray &values)
{
    const auto &a = values;
    if (!a.isEmpty()) {
        auto v = a[0].toObject();
        if (v.value("information").isObject()) {
            auto information = m_detail.value("information").toObject();
            const auto incoming = v.value("information").toObject();
            for (auto it = incoming.begin(); it != incoming.end(); ++it)
                if (!it.value().isNull() &&
                    (!it.value().isString() || !it.value().toString().trimmed().isEmpty()))
                    information[it.key()] = it.value();
            m_detail["information"] = information;
            if (m_detail.value("language").toString().trimmed().isEmpty() &&
                !information.value("language").toString().trimmed().isEmpty())
                m_detail["language"] = information.value("language");
        }
        for (const auto &key : {"title", "album_title", "album_id", "cover_url", "artists"})
            if (!v.value(key).isNull() && !v.value(key).isUndefined())
                m_track[key] = v.value(key);
        m_detail["title"] = m_track.value("title");
        m_detail["album"] = m_track.value("album_title");
        if (!m_detail.value("album_id").toVariant().toULongLong())
            m_detail["album_id"] = m_track.value("album_id");
        m_detail["cover_url"] = m_track.value("cover_url");
        const auto artists = m_track.value("artists").toArray();
        m_detail["artists"] = artists;
        QStringList artistNames;
        for (const auto &artist : artists) {
            const auto name = artist.toObject().value("name").toString();
            if (!name.isEmpty())
                artistNames.append(name);
        }
        if (query().value("artist").toString().trimmed().isEmpty() && !artistNames.isEmpty())
            m_detail["artist"] = artistNames.join(" / ");
        if (!m_detail.value("artist_id").toVariant().toULongLong() && !artists.isEmpty())
            m_detail["artist_id"] = artists[0].toObject().value("id");
        updateSong();
    }
}
void SongDetailContext::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    auto p = m_pending.take(id);
    if (p.epoch != m_epoch || p.session != m_api.sessionRevision() || m_closed)
        return;
    // Statistics are supplementary; an independent read failure preserves
    // the known count or leaves it unknown.
    if (p.kind == FavoriteCount || p.kind == CommentCount) {
        emit changed();
        return;
    }
    m_error = ApiClient::errorMessage(bytes);
    if (m_error.isEmpty())
        m_error = tr("部分歌曲详情加载失败，请重试");
    if (p.kind == Privilege && commentsPage())
        startComments();
    else if (p.kind == Privilege && m_mix)
        requestMetadata();
    else if (p.kind == Privilege || p.kind == Metadata)
        rankingsAndComments();
    if (m_rankingsStarted && !loading())
        startComments();
    emit changed();
}
} // namespace EchoMusic
