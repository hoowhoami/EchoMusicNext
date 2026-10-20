#include "ApiDomains_p.h"
#include "echo_ffi.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

namespace EchoMusic
{
quint64 ApiClient::MediaClient::recognizeAudio(const QByteArray &pcm)
{
    if (pcm.isEmpty() || pcm.size() > 160000 || pcm.size() % 2)
        return 0;
    return api.submit(
        {{"operation", "audio_match"},
         {"query", QJsonObject{{"pcm_base64", QString::fromLatin1(pcm.toBase64())}}}});
}

quint64 ApiClient::MediaClient::personalFm(const FmRequest &request)
{
    const QStringList modes{"normal", "small", "peak", "radio"};
    const QStringList actions{"play",      "login",      "garbage",
                              "click_red", "cancel_red", "change_song_pool"};
    QJsonObject query{{"mode", modes.value(int(request.mode))},
                      {"action", actions.value(int(request.action))},
                      {"song_pool_id", int(request.pool)},
                      {"remain_songcnt", int(request.remaining)},
                      {"is_overplay", request.overplay},
                      {"hash", request.hash},
                      {"cur_mark", request.mark}};
    if (request.audioId)
        query.insert("songid", qint64(*request.audioId));
    if (request.playSeconds)
        query.insert("playtime", qint64(*request.playSeconds));
    // Explicit SSA completion permits one retry.
    // Transport failures do not authorize replaying this request.
    return api.submit({{"operation", "personal_fm"}, {"query", query}});
}

quint64 ApiClient::MediaClient::songClimax(const QString &hash)
{
    return api.submit({{"operation", "song_climax"}, {"query", QJsonObject{{"hash", hash}}}}, true,
                      {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::MediaClient::albumDynamicCover(quint64 audio, quint64 album)
{
    if (audio > quint64(std::numeric_limits<qint64>::max()) ||
        album > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    return api.submit(
        {{"operation", "album_dycover"},
         {"query", QJsonObject{{"album_audio_id", qint64(audio)}, {"album_id", qint64(album)}}}},
        true, {AuthenticationMode::DeviceOnly, false});
}

quint64 ApiClient::MediaClient::songRanking(quint64 mix, bool details)
{
    return api.submit(
        {{"operation", details ? "song_ranking_filter" : "song_ranking"},
         {"query", QJsonObject{{"album_audio_id", qint64(mix)}, {"page", 1}, {"page_size", 30}}}},
        true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::MediaClient::songMv(quint64 mix)
{
    return api.submit(
        {{"operation", "kmr_audio_mv"}, {"query", QJsonObject{{"album_audio_id", qint64(mix)}}}},
        true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::MediaClient::videoDetail(const QString &id)
{
    return api.submit({{"operation", "video_detail"}, {"query", QJsonObject{{"id", id}}}}, true,
                      {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::MediaClient::videoPrivilege(const QString &hash)
{
    return api.submit({{"operation", "video_privilege"}, {"query", QJsonObject{{"hash", hash}}}},
                      true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::MediaClient::videoUrl(const QString &hash)
{
    return api.submit({{"operation", "video_url"}, {"query", QJsonObject{{"hash", hash}}}}, true,
                      {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::MediaClient::reportListeningEvent(ListeningEvent event, quint64 songId,
                                                     qint64 durationMs, bool completed)
{
    if (!api.m_userId || !songId || durationMs < 0)
        return 0;
    return api.submit(
        {{"operation", "user_listen_report"},
         {"query",
          QJsonObject{{"event", event == ListeningEvent::Start ? "start" : "end"},
                      {"mixsongid", qint64(songId)},
                      {"duration_ms", event == ListeningEvent::End ? QJsonValue(durationMs)
                                                                   : QJsonValue(QJsonValue::Null)},
                      {"completed", completed}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::MediaClient::reportListeningProgress(quint64 baselineSeconds,
                                                        quint64 incrementSeconds)
{
    if (!api.m_userId)
        return 0;
    return api.submit({{"operation", "user_grade_progress"},
                       {"query", QJsonObject{{"baseline_seconds", qint64(baselineSeconds)},
                                             {"increment_seconds", qint64(incrementSeconds)}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::MediaClient::songMetadata(const QList<quint64> &ids, RequestOptions options)
{
    if (ids.isEmpty() || ids.size() > 100)
        return 0;
    QJsonArray values;
    QSet<quint64> seen;
    for (auto id : ids) {
        if (!id || id > quint64(std::numeric_limits<qint64>::max()) || seen.contains(id))
            return 0;
        seen.insert(id);
        values.append(qint64(id));
    }
    return api.submit(
        {{"operation", "krm_audio"}, {"query", QJsonObject{{"album_audio_ids", values}}}}, true,
        options);
}

quint64 ApiClient::MediaClient::audioEffects(EffectCategory category, quint32 page,
                                             quint32 pageSize, quint32 sort, quint64 brand)
{
    if (!page || !pageSize || pageSize > 50)
        return 0;
    static const QStringList operations{"effects_artist", "effects_brand", "effects_brand_detail",
                                        "get_model"};
    const auto index = int(category);
    if (index < 0 || index >= operations.size())
        return 0;
    QJsonObject query{{"page", int(page)}, {"page_size", int(pageSize)}};
    if (category == EffectCategory::Market) {
        if (sort < 2 || sort > 4)
            return 0;
        query.insert("sort", int(sort));
    }
    if (category == EffectCategory::HeadphoneModels) {
        if (!brand || brand > quint64(std::numeric_limits<qint64>::max()))
            return 0;
        query.insert("brand_id", qint64(brand));
    }
    return api.submit({{"operation", operations[index]}, {"query", query}}, true);
}

quint64 ApiClient::MediaClient::commonHeadphoneEffect()
{
    return api.submit({{"operation", "effects_match"}}, true);
}

quint64 ApiClient::MediaClient::lyricCandidates(const QString &hash, quint64 songId,
                                                quint64 durationMs, bool allCandidates)
{
    return api.submit({{"operation", "search_lyric"},
                       {"query", QJsonObject{{"hash", hash},
                                             {"song_id", qint64(songId)},
                                             {"duration_ms", qint64(durationMs)},
                                             {"all_candidates", allCandidates}}}});
}

quint64 ApiClient::MediaClient::lyric(const QString &id, const QString &accessKey)
{
    return api.submit(
        {{"operation", "lyric"}, {"query", QJsonObject{{"id", id}, {"accesskey", accessKey}}}});
}

quint64 ApiClient::MediaClient::lyricCandidatesByKeywords(const QString &keywords,
                                                          quint64 durationMs, bool allCandidates)
{
    if (keywords.trimmed().isEmpty())
        return 0;
    return api.submit({{"operation", "search_lyric"},
                       {"query", QJsonObject{{"keywords", keywords.trimmed()},
                                             {"duration_ms", qint64(durationMs)},
                                             {"all_candidates", allCandidates}}}});
}

quint64 ApiClient::MediaClient::playbackSource(const QString &hash, quint64 albumId, quint64 songId,
                                               PlaybackQuality quality,
                                               const QString &authorization, RequestOptions options,
                                               SoundEffect effect, const QString &originPage)
{
    if (albumId > quint64(std::numeric_limits<qint64>::max()) ||
        songId > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    static const QStringList qualities{"standard",   "higher",      "flac",        "hi_res",
                                       "viper_tape", "viper_clear", "viper_atmos", "automatic"};
    const int q = int(quality);
    if (q < 0 || q >= qualities.size())
        return 0;
    static const QStringList effects{"none",      "piano",   "vocal",  "accompaniment",
                                     "subwoofer", "ancient", "surnay", "dj"};
    if (int(effect) < 0 || int(effect) >= effects.size())
        return 0;
    QJsonObject query{{"effect", effects[int(effect)]},
                      {"hash", hash},
                      {"album_id", qint64(albumId)},
                      {"song_id", qint64(songId)},
                      {"quality", qualities[q]}};
    if (!authorization.isEmpty())
        query.insert(QStringLiteral("authorization"), authorization);
    if (!originPage.isEmpty())
        query.insert(QStringLiteral("origin_page"), originPage);
    return api.submit({{"operation", "song_url_auth_merge"}, {"query", query}}, true, options);
}

quint64 ApiClient::MediaClient::songPrivileges(const QList<PrivilegeResource> &resources,
                                               RequestOptions options)
{
    QJsonArray items;
    for (const auto &resource : resources) {
        QJsonObject item{{"hash", resource.hash}};
        if (resource.albumId) {
            if (*resource.albumId > quint64(std::numeric_limits<qint64>::max()))
                return 0;
            item.insert(QStringLiteral("album_id"), qint64(*resource.albumId));
        }
        items.append(item);
    }
    return api.submit(
        {{"operation", "privilege_lite"}, {"query", QJsonObject{{"resources", items}}}}, true,
        options);
}
} // namespace EchoMusic
