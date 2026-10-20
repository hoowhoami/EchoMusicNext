#include "ApiDomains_p.h"
#include "echo_ffi.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

namespace EchoMusic
{
quint64 ApiClient::CatalogClient::searchHot(RequestOptions options)
{
    return api.submit({{"operation", "search_hot"}}, true, options);
}

quint64 ApiClient::CatalogClient::searchDefault(RequestOptions options)
{
    return api.submit({{"operation", "search_default"}}, true, options);
}

quint64 ApiClient::CatalogClient::searchSuggest(const QString &keyword, RequestOptions options)
{
    return api.submit(
        {{"operation", "search_suggest"}, {"query", QJsonObject{{"keyword", keyword}}}}, true,
        options);
}

quint64 ApiClient::CatalogClient::search(SearchKind kind, const QString &keyword, quint32 page,
                                         quint32 pageSize, RequestOptions options)
{
    QString wireKind;
    switch (kind) {
    case SearchKind::Song:
        wireKind = QStringLiteral("song");
        break;
    case SearchKind::Album:
        wireKind = QStringLiteral("album");
        break;
    case SearchKind::Artist:
        wireKind = QStringLiteral("author");
        break;
    case SearchKind::Playlist:
        wireKind = QStringLiteral("special");
        break;
    case SearchKind::Lyric:
        wireKind = QStringLiteral("lyric");
        break;
    case SearchKind::Mv:
        wireKind = QStringLiteral("mv");
        break;
    }
    return api.submit({{"operation", "search"},
                       {"query", QJsonObject{{"keyword", keyword},
                                             {"kind", wireKind},
                                             {"page", qint64(page)},
                                             {"page_size", qint64(pageSize)}}}},
                      true, options);
}

quint64 ApiClient::CatalogClient::recommendedPlaylists(const QString &categoryId)
{
    return api.submit(
        {{"operation", "top_playlist"}, {"query", QJsonObject{{"category_id", categoryId}}}});
}

quint64 ApiClient::CatalogClient::editorPlaylists()
{
    return api.submit({{"operation", "top_ip"}});
}

quint64 ApiClient::CatalogClient::styleRecommendations(const QStringList &tagIds)
{
    return api.submit({{"operation", "everyday_style_recommend"},
                       {"query", QJsonObject{{"tag_ids", QJsonArray::fromStringList(tagIds)}}}});
}

quint64 ApiClient::CatalogClient::dailyRecommendations()
{
    return api.submit({{"operation", "everyday_recommend"}});
}

quint64 ApiClient::CatalogClient::freeListenSongs()
{
    return api.submit({{"operation", "ai_recommend_song"}});
}

quint64 ApiClient::CatalogClient::discoverRecommendations(quint32 pageSize)
{
    if (!pageSize)
        return 0;
    return api.submit(
        {{"operation", "home_discover"}, {"query", QJsonObject{{"page_size", int(pageSize)}}}},
        true, {AuthenticationMode::DeviceOnly, false});
}

quint64 ApiClient::CatalogClient::newSongCatalog()
{
    return api.submit({{"operation", "top_song"}}, true,
                      {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::CatalogClient::newAlbumCatalog()
{
    return api.submit({{"operation", "top_album"}}, true,
                      {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::CatalogClient::playlistCategories()
{
    return api.submit({{"operation", "playlist_tags"}}, true,
                      {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::CatalogClient::artistDirectory(quint32 sex, quint32 category, quint32 musician)
{
    return api.submit({{"operation", "artist_lists"},
                       {"query", QJsonObject{{"sex", int(sex)},
                                             {"category", int(category)},
                                             {"musician", int(musician)}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::CatalogClient::recommendedRanks()
{
    return api.submit({{"operation", "rank_top"}});
}

quint64 ApiClient::CatalogClient::rankCatalog()
{
    return api.submit({{"operation", "rank_list"}});
}

quint64 ApiClient::CatalogClient::rankSongs(quint64 rankId)
{
    return api.submit(
        {{"operation", "rank_audio"}, {"query", QJsonObject{{"rank_id", qint64(rankId)}}}});
}

quint64 ApiClient::CatalogClient::albumDetail(quint64 albumId, RequestOptions options)
{
    if (albumId > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    return api.submit({{"operation", "album_detail"}, {"id", qint64(albumId)}}, true, options);
}

quint64 ApiClient::CatalogClient::albumSongs(quint64 albumId, quint32 page, quint32 pageSize,
                                             RequestOptions options)
{
    if (albumId > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    return api.submit({{"operation", "album_songs"},
                       {"query", QJsonObject{{"id", qint64(albumId)},
                                             {"page", qint64(page)},
                                             {"page_size", qint64(pageSize)}}}},
                      true, options);
}

quint64 ApiClient::CatalogClient::artistDetail(quint64 artistId, RequestOptions options)
{
    if (artistId > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    return api.submit({{"operation", "artist_detail"}, {"id", qint64(artistId)}}, true, options);
}

quint64 ApiClient::CatalogClient::artistSongs(quint64 artistId, quint32 page, quint32 pageSize,
                                              const QString &sort, RequestOptions options)
{
    if (artistId > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    return api.submit({{"operation", "artist_songs"},
                       {"query", QJsonObject{{"id", qint64(artistId)},
                                             {"page", qint64(page)},
                                             {"page_size", qint64(pageSize)},
                                             {"sort", sort}}}},
                      true, options);
}

quint64 ApiClient::CatalogClient::artistAlbums(quint64 artistId, quint32 page, quint32 pageSize,
                                               const QString &sort, RequestOptions options)
{
    if (artistId > quint64(std::numeric_limits<qint64>::max()))
        return 0;
    return api.submit({{"operation", "artist_albums"},
                       {"query", QJsonObject{{"id", qint64(artistId)},
                                             {"page", qint64(page)},
                                             {"page_size", qint64(pageSize)},
                                             {"sort", sort}}}},
                      true, options);
}
} // namespace EchoMusic
