#include "ApiDomains_p.h"
#include "echo_ffi.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

namespace EchoMusic
{
quint64 ApiClient::LibraryClient::userPlaylists(quint32 page, quint32 pageSize)
{
    if (!api.m_userId || !page || !pageSize || pageSize > 200)
        return 0;
    return api.submit(
        {{"operation", "user_playlist"},
         {"query", QJsonObject{{"page", qint64(page)}, {"page_size", qint64(pageSize)}}}});
}

quint64 ApiClient::LibraryClient::importPlaylistLink(const QString &text)
{
    if (!api.m_userId || text.isEmpty() || text.size() > 8192)
        return 0;
    return api.submit({{"operation", "import_playlist"},
                       {"query", QJsonObject{{"action", "link"}, {"url", text}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::submitPlaylistImportImage(const QString &serial,
                                                            const QByteArray &image)
{
    if (!api.m_userId || serial.isEmpty() || image.isEmpty() || image.size() > 10 * 1024 * 1024)
        return 0;
    return api.submit(
        {{"operation", "import_playlist"},
         {"query", QJsonObject{{"action", "submit_image"},
                               {"task_sn", serial},
                               {"image_base64", QString::fromLatin1(image.toBase64())}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::importPlaylistScreenshot(const QString &serial,
                                                           CloudPlaylistId id, const QString &title)
{
    if (!api.m_userId || !id.value || serial.isEmpty() || title.isEmpty())
        return 0;
    return api.submit({{"operation", "import_playlist"},
                       {"query", QJsonObject{{"action", "screenshot"},
                                             {"task_sn", serial},
                                             {"cloud_list_id", qint64(id.value)},
                                             {"title", title}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::playlistImportStatus(quint64 task)
{
    if (!api.m_userId || !task)
        return 0;
    return api.submit(
        {{"operation", "import_playlist"},
         {"query", QJsonObject{{"action", "status"}, {"task_ids", QJsonArray{qint64(task)}}}}},
        true);
}

quint64 ApiClient::LibraryClient::playlistImportResult(CloudPlaylistId id, quint32 page,
                                                       quint32 size)
{
    if (!api.m_userId || !id.value || !page || !size || size > 100)
        return 0;
    return api.submit({{"operation", "import_playlist"},
                       {"query", QJsonObject{{"action", "result"},
                                             {"cloud_list_id", qint64(id.value)},
                                             {"page", qint64(page)},
                                             {"page_size", qint64(size)}}}},
                      true);
}

quint64 ApiClient::LibraryClient::purchasedSongs(quint32 page, quint32 pageSize)
{
    if (!api.m_userId || !page || pageSize > 100)
        return 0;
    return api.submit({{"operation", "user_purchased_songs"},
                       {"query", QJsonObject{{"page", int(page)}, {"page_size", int(pageSize)}}}});
}

quint64 ApiClient::LibraryClient::purchasedAlbums(quint32 page, quint32 pageSize)
{
    if (!api.m_userId || !page || pageSize > 100)
        return 0;
    return api.submit({{"operation", "user_purchased_albums"},
                       {"query", QJsonObject{{"page", int(page)}, {"page_size", int(pageSize)}}}});
}

quint64 ApiClient::LibraryClient::playlistDetails(const QList<PublicCollectionId> &ids,
                                                  RequestOptions options)
{
    if (ids.isEmpty() || ids.size() > 100)
        return 0;
    QJsonArray values;
    for (const auto &id : ids) {
        if (id.value.isEmpty() || id.value.size() > 256 || id.value.contains(u','))
            return 0;
        values.append(id.value);
    }
    return api.submit(
        {{"operation", "playlist_detail"}, {"query", QJsonObject{{"global_ids", values}}}}, true,
        options);
}

quint64 ApiClient::LibraryClient::publicPlaylistTracks(const PublicCollectionId &id, quint32 page,
                                                       quint32 pageSize, RequestOptions options)
{
    if (id.value.isEmpty() || id.value.size() > 256 || !page || !pageSize || pageSize > 200)
        return 0;
    return api.submit({{"operation", "playlist_track_all"},
                       {"query", QJsonObject{{"global_id", id.value},
                                             {"page", qint64(page)},
                                             {"page_size", qint64(pageSize)}}}},
                      true, options);
}

quint64 ApiClient::LibraryClient::cloudPlaylistTracks(CloudPlaylistId id, quint32 page,
                                                      quint32 pageSize)
{
    if (!api.m_userId || !id.value || id.value > quint64(std::numeric_limits<qint64>::max()) ||
        !page || !pageSize || pageSize > 300)
        return 0;
    return api.submit({{"operation", "playlist_track_all_new"},
                       {"query", QJsonObject{{"cloud_list_id", qint64(id.value)},
                                             {"page", qint64(page)},
                                             {"page_size", qint64(pageSize)}}}});
}

quint64 ApiClient::LibraryClient::createPlaylist(const QString &title, bool isPrivate)
{
    if (!api.m_userId || title.trimmed().isEmpty())
        return 0;
    return api.submit({{"operation", "playlist_add"},
                       {"query", QJsonObject{{"title", title}, {"private", isPrivate}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::collectPlaylist(const CollectionTarget &target)
{
    if (!api.m_userId || target.title.trimmed().isEmpty() || !target.creatorListId)
        return 0;
    QJsonObject collection{{"source", int(target.source)},
                           {"creator_id", target.creatorId ? QJsonValue(qint64(*target.creatorId))
                                                           : QJsonValue(QJsonValue::Null)},
                           {"creator_list_id", qint64(target.creatorListId)},
                           {"creator_global_id", target.creatorGlobalId}};
    return api.submit(
        {{"operation", "playlist_add"},
         {"query",
          QJsonObject{{"title", target.title}, {"private", false}, {"collection", collection}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::addPlaylistTracks(CloudPlaylistId id,
                                                    const QList<PlaylistTrackInput> &tracks)
{
    if (!api.m_userId || !id.value || id.value > quint64(std::numeric_limits<qint64>::max()) ||
        tracks.isEmpty() || tracks.size() > 300)
        return 0;
    QJsonArray values;
    for (const auto &track : tracks) {
        if (track.albumId > quint64(std::numeric_limits<qint64>::max()) ||
            track.songId > quint64(std::numeric_limits<qint64>::max()))
            return 0;
        values.append(QJsonObject{{"title", track.title},
                                  {"hash", track.hash},
                                  {"album_id", qint64(track.albumId)},
                                  {"song_id", qint64(track.songId)}});
    }
    return api.submit(
        {{"operation", "playlist_tracks_add"},
         {"query", QJsonObject{{"cloud_list_id", qint64(id.value)}, {"tracks", values}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::removePlaylistTracks(CloudPlaylistId id,
                                                       const QList<PlaylistTrackFileId> &files)
{
    if (!api.m_userId || !id.value || id.value > quint64(std::numeric_limits<qint64>::max()) ||
        files.isEmpty() || files.size() > 300)
        return 0;
    QJsonArray values;
    for (auto file : files) {
        if (!file.value || file.value > quint64(std::numeric_limits<qint64>::max()))
            return 0;
        values.append(qint64(file.value));
    }
    return api.submit(
        {{"operation", "playlist_tracks_del"},
         {"query", QJsonObject{{"cloud_list_id", qint64(id.value)}, {"file_ids", values}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::sortPlaylistTracks(CloudPlaylistId id, quint64 revision,
                                                     const QList<PlaylistTrackPosition> &positions)
{
    // Allow ordering across all loaded pages. Reject empty or duplicate IDs
    // without imposing a single-page count limit.
    if (!api.m_userId || !id.value || id.value > quint64(std::numeric_limits<qint64>::max()) ||
        revision > quint64(std::numeric_limits<qint64>::max()) || positions.isEmpty())
        return 0;
    QJsonArray values;
    for (auto position : positions) {
        if (!position.fileId.value ||
            position.fileId.value > quint64(std::numeric_limits<qint64>::max()))
            return 0;
        values.append(QJsonObject{{"file_id", qint64(position.fileId.value)},
                                  {"position", qint64(position.position)}});
    }
    return api.submit({{"operation", "playlist_tracks_sort"},
                       {"query", QJsonObject{{"cloud_list_id", qint64(id.value)},
                                             {"revision", qint64(revision)},
                                             {"positions", values}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::sortPlaylists(quint64 totalRevision,
                                                const QList<PlaylistPosition> &positions)
{
    if (!api.m_userId || totalRevision > quint64(std::numeric_limits<qint64>::max()) ||
        positions.isEmpty())
        return 0;
    QJsonArray values;
    QSet<quint64> ids;
    QSet<QPair<int, quint32>> occupiedPositions;
    for (const auto &p : positions) {
        if ((p.kind != PlaylistKind::Owned && p.kind != PlaylistKind::Collected) || !p.id.value ||
            p.id.value > quint64(std::numeric_limits<qint64>::max()) || ids.contains(p.id.value) ||
            occupiedPositions.contains({int(p.kind), p.position}))
            return 0;
        ids.insert(p.id.value);
        occupiedPositions.insert({int(p.kind), p.position});
        values.append(QJsonObject{{"cloud_list_id", qint64(p.id.value)},
                                  {"kind", p.kind == PlaylistKind::Owned ? "owned" : "collected"},
                                  {"position", qint64(p.position)}});
    }
    return api.submit(
        {{"operation", "playlist_sort"},
         {"query", QJsonObject{{"total_revision", qint64(totalRevision)}, {"positions", values}}}},
        true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::uploadPlaylistCover(const QByteArray &png)
{
    Q_ASSERT(QThread::currentThread() == api.thread());
    if (api.m_closing)
        return 0;
    if (png.size() > 8 * 1024 * 1024 || !png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
        return 0;
    const auto date = QDate::currentDate().toString(QStringLiteral("yyyyMMdd")).toUInt();
    Pending pending;
    pending.revision = api.m_revision;
    pending.automaticVerification = false;
    // Metadata only: no image bytes/base64 retained in Qt's pending requests.
    pending.request = {{"operation", "playlist_pic_upload"}, {"local_date", qint64(date)}};
    const auto id = echo_runtime_upload_playlist_cover(
        api.m_runtime, reinterpret_cast<const uint8_t *>(png.constData()), size_t(png.size()), date,
        api.m_revision, callback, api.m_dispatcher);
    if (id) {
        pending.logicalId = id;
        api.m_pending.insert(id, std::move(pending));
    }
    return id;
}

quint64 ApiClient::LibraryClient::updatePlaylist(const PlaylistUpdate &update)
{
    if (!api.m_userId || !update.id.value ||
        update.id.value > quint64(std::numeric_limits<qint64>::max()) ||
        update.totalRevision > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    QJsonObject query{{"cloud_list_id", qint64(update.id.value)},
                      {"total_revision", qint64(update.totalRevision)},
                      {"sort_index", update.sortIndex},
                      {"description", update.description},
                      {"tags", update.tags}};
    if (update.title)
        query.insert(QStringLiteral("title"), *update.title);
    if (update.coverReference)
        query.insert(QStringLiteral("cover_reference"), *update.coverReference);
    return api.submit({{"operation", "playlist_update"}, {"query", query}}, true,
                      {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::LibraryClient::deletePlaylist(CloudPlaylistId id)
{
    if (!api.m_userId || !id.value || id.value > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    return api.submit({{"operation", "playlist_del"},
                       {"query", QJsonObject{{"cloud_list_id", qint64(id.value)}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}
} // namespace EchoMusic
