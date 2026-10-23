#include "ApiDomains_p.h"
#include "echo_ffi.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

namespace EchoMusic
{
namespace
{
QString commentResourceName(ApiClient::CommentResource resource)
{
    switch (resource) {
    case ApiClient::CommentResource::Album:
        return QStringLiteral("album");
    case ApiClient::CommentResource::Playlist:
        return QStringLiteral("playlist");
    case ApiClient::CommentResource::Music:
        return QStringLiteral("music");
    }
    Q_UNREACHABLE();
}
QJsonObject commentWriteQuery(const ApiClient::CommentWrite &w)
{
    QJsonObject q{{"resource", commentResourceName(w.resource)},
                  {"id", w.id},
                  {"name", w.name},
                  {"content", w.content},
                  {"comment_id", w.commentId},
                  {"thread_id", w.threadId},
                  {"parent_id", w.parentId},
                  {"reply_user_name", w.replyUserName},
                  {"reply_content", w.replyContent}};
    if (!w.code.isEmpty())
        q.insert("code", w.code);
    if (w.mixSongId)
        q.insert("mix_song_id", qint64(w.mixSongId));
    return q;
}
} // namespace
quint64 ApiClient::SocialClient::comments(const CommentQuery &q)
{
    QString operation;
    switch (q.section) {
    case CommentSection::Category:
        operation = "comment_music_classify";
        break;
    case CommentSection::HotWord:
        operation = "comment_music_hotword";
        break;
    case CommentSection::Floor:
        operation = "comment_floor";
        break;
    case CommentSection::All:
        operation = "comment_" + commentResourceName(q.resource);
        break;
    }
    QJsonObject query{{"resource", commentResourceName(q.resource)},
                      {"id", q.id},
                      {"page", qint64(q.page)},
                      {"page_size", qint64(q.pageSize)}};
    if (!q.code.isEmpty())
        query.insert("code", q.code);
    if (q.categoryId)
        query.insert("category_id", qint64(q.categoryId));
    if (q.mixSongId)
        query.insert("mix_song_id", qint64(q.mixSongId));
    if (!q.threadId.isEmpty())
        query.insert("thread_id", q.threadId);
    if (!q.hotWord.isEmpty())
        query.insert("hot_word", q.hotWord);
    return api.submit({{"operation", operation}, {"query", query}}, true,
                      {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::sendComment(const CommentWrite &w)
{
    return api.submit({{"operation", "comment_" + commentResourceName(w.resource) + "_send"},
                       {"query", commentWriteQuery(w)}},
                      true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::replyComment(const CommentWrite &w)
{
    return api.submit({{"operation", "comment_floor_send"}, {"query", commentWriteQuery(w)}}, true,
                      {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::deleteComment(const CommentWrite &w)
{
    return api.submit({{"operation", "comment_music_del"}, {"query", commentWriteQuery(w)}}, true,
                      {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::commentCount(const QString &hash)
{
    return api.submit({{"operation", "comment_count"}, {"query", QJsonObject{{"hash", hash}}}},
                      true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::favoriteCount(quint64 mix)
{
    return api.submit(
        {{"operation", "favorite_count"}, {"query", QJsonObject{{"ids", QJsonArray{qint64(mix)}}}}},
        true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::collectVideo(quint64 id, bool collect)
{
    if (!api.m_userId || !id)
        return 0;
    return api.submit({{"operation", collect ? "mv_collect" : "mv_collect_del"},
                       {"query", QJsonObject{{"id", qint64(id)}}}},
                      true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::commentAuthorProducts(const QList<quint64> &ids)
{
    QJsonArray users;
    for (auto id : ids)
        users.append(qint64(id));
    return api.submit(
        {{"operation", "user_batch_union_vipinfo"}, {"query", QJsonObject{{"user_ids", users}}}},
        true, {AuthenticationMode::DeviceOnly, false});
}

quint64 ApiClient::SocialClient::songBarrage(const QString &hash, quint32 size)
{
    return api.submit(
        {{"operation", "song_barrage"},
         {"query", QJsonObject{{"hash", hash}, {"page", 1}, {"page_size", qint64(size)}}}},
        true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::sendSongBarrage(const QString &hash, const QString &name,
                                                 const QString &content)
{
    return api.submit(
        {{"operation", "song_barrage_send"},
         {"query", QJsonObject{{"hash", hash}, {"name", name}, {"content", content}}}},
        true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::videoBarrage(const QString &hash, quint32 size)
{
    return api.submit(
        {{"operation", "video_barrage"},
         {"query", QJsonObject{{"hash", hash}, {"page", 1}, {"page_size", qint64(size)}}}},
        true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::sendVideoBarrage(const QString &hash, const QString &name,
                                                  const QString &content)
{
    return api.submit(
        {{"operation", "video_barrage_send"},
         {"query", QJsonObject{{"hash", hash}, {"name", name}, {"content", content}}}},
        true, {AuthenticationMode::CurrentAccount, true});
}

quint64 ApiClient::SocialClient::userDetail(RequestOptions options)
{
    return api.submit({{"operation", "user_detail"}}, true, options);
}

quint64 ApiClient::SocialClient::userInfo()
{
    return api.submit({{"operation", "user_info"}});
}

quint64 ApiClient::SocialClient::userVipDetail()
{
    return api.submit({{"operation", "user_vip_detail"}});
}

quint64 ApiClient::SocialClient::userGradeInfo(std::optional<quint64> userId,
                                               bool automaticVerification)
{
    if ((userId && !*userId) || (!userId && !api.m_userId))
        return 0;
    QJsonObject query{
        {"user_id", userId ? QJsonValue(qint64(*userId)) : QJsonValue(QJsonValue::Null)}};
    return api.submit(
        {{"operation", "user_grade_info"}, {"query", query}}, true,
        userId ? RequestOptions{AuthenticationMode::DeviceOnly, false}
               : RequestOptions{AuthenticationMode::CurrentAccount, automaticVerification});
}

quint64 ApiClient::SocialClient::socialUsers(SocialList list)
{
    if (!api.m_userId)
        return 0;
    const QStringList operations{"user_follow", "user_friends", "user_fans", "user_visitors"};
    const auto value = int(list);
    if (value < 0 || value >= operations.size())
        return 0;
    return api.submit({{"operation", operations[value]}});
}

quint64 ApiClient::SocialClient::followUser(quint64 id, bool following)
{
    if (!api.m_userId || !id || id == api.m_userId)
        return 0;
    return api.submit(
        {{"operation", following ? "user_follow_add" : "user_follow_del"}, {"id", qint64(id)}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::SocialClient::followArtist(quint64 id, bool following)
{
    if (!api.m_userId || !id)
        return 0;
    return api.submit(
        {{"operation", following ? "artist_follow" : "artist_unfollow"}, {"id", qint64(id)}}, true,
        {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::SocialClient::socialMessages(quint64 target, const QString &tag)
{
    if (!api.m_userId || !target || target == api.m_userId)
        return 0;
    return api.submit({{"operation", "user_follow_message"},
                       {"query", QJsonObject{{"target", qint64(target)},
                                             {"tag", tag.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                   : QJsonValue(tag)}}}});
}

quint64 ApiClient::SocialClient::sendSocialMessage(quint64 target, const QString &tag,
                                                   const QString &text, const QString &nickname)
{
    if (!api.m_userId || !target || target == api.m_userId || text.trimmed().isEmpty() ||
        text.size() > 200)
        return 0;
    return api.submit({{"operation", "user_follow_chat"},
                       {"query", QJsonObject{{"target", qint64(target)},
                                             {"tag", tag.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                   : QJsonValue(tag)},
                                             {"text", text},
                                             {"nickname", nickname}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::SocialClient::updateUserProfile(const ProfilePatch &patch)
{
    if (!api.m_userId)
        return 0;
    QJsonObject query;
    for (const auto &entry :
         {std::pair<QString, std::optional<QString>>{"nickname", patch.nickname},
          {"birthday", patch.birthday},
          {"signature", patch.signature},
          {"province", patch.province},
          {"city", patch.city}})
        if (entry.second)
            query.insert(entry.first, *entry.second);
    if (patch.gender)
        query.insert("gender", qint64(*patch.gender));
    if (query.isEmpty())
        return 0;
    return api.submit({{"operation", "user_update"}, {"query", query}}, true,
                      {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::SocialClient::uploadUserAvatar(const QByteArray &image)
{
    if (!api.m_userId || image.isEmpty() || image.size() > 8 * 1024 * 1024)
        return 0;
    const auto id = api.submit(
        {{"operation", "user_avatar_upload"},
         {"query", QJsonObject{{"image_base64", QString::fromLatin1(image.toBase64())},
                               {"local_date", QDate::currentDate().toString("yyyyMMdd")}}}},
        true, {AuthenticationMode::CurrentAccount, false});
    // This write is never replayed, and the native runtime already owns its
    // serialized input. Do not retain another image/base64 in pending metadata.
    if (id)
        api.m_pending[id].request = {{"operation", "user_avatar_upload"}};
    return id;
}

quint64 ApiClient::SocialClient::updateUserAvatar(const QString &fileName)
{
    if (!api.m_userId || fileName.isEmpty())
        return 0;
    return api.submit(
        {{"operation", "user_update_avatar"}, {"query", QJsonObject{{"file_name", fileName}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::SocialClient::contentBlacklist(BlacklistKind kind, quint32 page,
                                                  quint32 pageSize)
{
    if (!api.m_userId || !page || !pageSize || pageSize > 500)
        return 0;
    return api.submit(
        {{"operation", "blacklist_list"},
         {"query", QJsonObject{{"label", kind == BlacklistKind::Song ? "song" : "singer"},
                               {"page", qint64(page)},
                               {"page_size", qint64(pageSize)}}}});
}

quint64 ApiClient::SocialClient::editContentBlacklist(BlacklistKind kind, const QString &key,
                                                      const QString &name, const QString &mixSongId,
                                                      bool remove)
{
    if (!api.m_userId || key.isEmpty())
        return 0;
    return api.submit(
        {{"operation", "blacklist"},
         {"query", QJsonObject{{"label", kind == BlacklistKind::Song ? "song" : "singer"},
                               {"key", key},
                               {"name", name},
                               {"mix_song_id", mixSongId},
                               {"remove", remove}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::SocialClient::listeningPreferences()
{
    return api.m_userId ? api.submit({{"operation", "user_preference"}}) : 0;
}

quint64 ApiClient::SocialClient::updateListeningPreferences(const PreferencePatch &patch)
{
    if (!api.m_userId)
        return 0;
    QJsonObject query;
    for (const auto &entry :
         {std::pair<QString, std::optional<QString>>{"recommendation_mode", patch.mode},
          {"gender", patch.gender},
          {"age", patch.age},
          {"languages", patch.languages},
          {"styles", patch.styles},
          {"recommendation", patch.recommendation}})
        if (entry.second)
            query.insert(entry.first, *entry.second);
    if (query.isEmpty())
        return 0;
    return api.submit({{"operation", "user_preference_update"}, {"query", query}}, true,
                      {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::SocialClient::userVideoCollect(quint32 page, quint32 pageSize)
{
    if (!api.m_userId || !page || pageSize > 100)
        return 0;
    return api.submit({{"operation", "user_video_collect"},
                       {"query", QJsonObject{{"page", int(page)}, {"page_size", int(pageSize)}}}});
}
} // namespace EchoMusic
