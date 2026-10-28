#include "SettingsKeys.h"
#include "SidebarLayoutModel.h"
#include "ApiClient.h"
#include "PlaybackService.h"
#include "RouteController.h"
#include "UserPlaylistModel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <utility>

namespace EchoMusic
{
namespace
{
QVariantMap resource(const QString &route, const QString &id, const QString &title,
                     const QString &cover = {})
{
    return {
        {"key", route + ":" + id}, {"route", route},
        {"resourceId", id},        {"title", title},
        {"cover", cover},          {"glyph", route == "artist-detail" ? "user" : "playlist-add"}};
}
} // namespace
SidebarLayoutModel::~SidebarLayoutModel()
{
    cancelPortraits();
}
void SidebarLayoutModel::attachResources(ApiClient &api, PlaybackService &playback,
                                         UserPlaylistModel &library, bool networkEnabled)
{
    if (m_playback)
        return;
    m_resourceApi = &api;
    m_playback = &playback;
    m_resourceLibrary = &library;
    m_resourceNetwork = networkEnabled;
    connect(&playback, &PlaybackService::historyLoaded, this, &SidebarLayoutModel::acceptHistory);
    connect(&playback, &PlaybackService::historyFailed, this, [this](const QString &message) {
        m_historyLoading = false;
        m_historyError = message;
        emit changed();
    });
    connect(playback.queueCatalog(), &PlaybackQueueCatalog::changed, this,
            &SidebarLayoutModel::changed);
    connect(&library, &UserPlaylistModel::changed, this, &SidebarLayoutModel::changed);
    connect(&library, &UserPlaylistModel::coverChanged, this, &SidebarLayoutModel::changed);
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancelPortraits();
        m_portraitAttempts.clear();
        m_frequentArtists.clear();
        m_historyError.clear();
        m_historyLoading = m_browsing;
        emit changed();
        if (m_browsing && m_playback)
            m_playback->requestHistory();
        updatePortraits();
    });
    connect(&api, &ApiClient::succeeded, this, [this](quint64 request, const QByteArray &bytes) {
        const auto key = m_portraitRequests.take(request);
        if (key.isEmpty())
            return;
        const auto detail = QJsonDocument::fromJson(bytes).object();
        const auto id = detail.value("id").toVariant().toString();
        const auto cover = detail.value("avatar_url").toString();
        if (cover.isEmpty() || key != "artist-detail:" + id)
            return;
        for (auto &value : m_frequentArtists) {
            auto entry = value.toMap();
            if (entry.value("key") == key) {
                entry["cover"] = cover;
                value = entry;
            }
        }
        if (m_resources.contains(key)) {
            m_resources[key]["cover"] = cover;
            saveResources();
        }
        emit changed();
        updatePortraits();
    });
    connect(&api, &ApiClient::failed, this, [this](quint64 request, int, const QByteArray &) {
        if (m_portraitRequests.remove(request))
            updatePortraits();
    });
    updatePortraits();
}
void SidebarLayoutModel::browseResources(bool active)
{
    if (m_browsing == active)
        return;
    m_browsing = active;
    if (!active) {
        updatePortraits();
        return;
    }
    m_portraitAttempts.clear();
    m_historyError.clear();
    m_historyLoading = bool(m_playback);
    emit changed();
    if (m_playback)
        m_playback->requestHistory();
    updatePortraits();
}
void SidebarLayoutModel::acceptHistory(const QJsonArray &entries)
{
    struct Artist
    {
        QVariantMap entry;
        qint64 plays = 0, lastPlayed = 0;
    };
    QHash<QString, Artist> artists;
    for (const auto &value : entries) {
        const auto history = value.toObject();
        const auto refs =
            history["track"].toObject()["metadata"].toObject()["artist_refs"].toArray();
        QSet<QString> seen;
        for (const auto &ref : refs) {
            const auto artist = ref.toObject();
            const auto numericId = artist.value("id").toVariant().toString().toULongLong();
            const auto name = artist.value("name").toString().trimmed();
            if (!numericId || name.isEmpty())
                continue;
            const auto id = QString::number(numericId);
            if (seen.contains(id))
                continue;
            seen.insert(id);
            auto &stat = artists[id];
            if (stat.entry.isEmpty())
                stat.entry =
                    resource("artist-detail", id, name, artist.value("avatar_url").toString());
            if (stat.entry.value("cover").toString().isEmpty() &&
                !artist.value("avatar_url").toString().isEmpty())
                stat.entry["cover"] = artist.value("avatar_url").toString();
            stat.plays += std::max(qint64(1), history["play_count"].toInteger());
            stat.lastPlayed = std::max(stat.lastPlayed, history["played_at"].toInteger());
        }
    }
    auto ranked = artists.values();
    std::stable_sort(ranked.begin(), ranked.end(), [](const Artist &a, const Artist &b) {
        if (a.plays != b.plays)
            return a.plays > b.plays;
        if (a.lastPlayed != b.lastPlayed)
            return a.lastPlayed > b.lastPlayed;
        return a.entry.value("resourceId").toString() < b.entry.value("resourceId").toString();
    });
    QVariantList next;
    for (const auto &stat : ranked) {
        auto entry = stat.entry;
        // Preserve already resolved portraits across history updates.
        const auto previous = resourceEntry(entry.value("key").toString());
        if (entry.value("cover").toString().isEmpty())
            entry["cover"] = previous.value("cover", "");
        next.append(entry);
        if (next.size() == 6)
            break;
    }
    m_frequentArtists = next;
    m_historyLoading = false;
    m_historyError.clear();
    emit changed();
    updatePortraits();
}
QVariantList SidebarLayoutModel::frequentArtists() const
{
    auto result = m_frequentArtists;
    for (auto &value : result) {
        auto entry = value.toMap();
        entry["pinned"] = m_pinned.contains(entry.value("key").toString());
        value = entry;
    }
    return result;
}
QVariantMap SidebarLayoutModel::resolveResource(QVariantMap entry) const
{
    if (entry.value("route") != "playlist-detail" || !m_resourceLibrary)
        return entry;
    const auto id = entry.value("resourceId").toString();
    for (int row = 0; row < m_resourceLibrary->rowCount(); ++row) {
        const auto index = m_resourceLibrary->index(row);
        if (index.data(UserPlaylistModel::SourceCodeRole).toUInt() == 2)
            continue;
        const auto global = index.data(UserPlaylistModel::GlobalIdRole).toString();
        const auto creator = index.data(UserPlaylistModel::CreatorGlobalIdRole).toString();
        if (id != global && id != creator &&
            id != index.data(UserPlaylistModel::CloudListIdRole).toString())
            continue;
        const auto title = index.data(UserPlaylistModel::TitleRole).toString();
        if (!title.isEmpty())
            entry["title"] = title;
        // Empty is an authoritative cleared cover, not a reason to revive a snapshot.
        entry["cover"] = m_resourceLibrary->coverFor(
            global, index.data(UserPlaylistModel::CoverUrlRole).toString());
        break;
    }
    return entry;
}
QVariantList SidebarLayoutModel::recentPlaylists() const
{
    QVariantList result;
    if (!m_playback)
        return result;
    auto queues = m_playback->queueCatalog()->snapshot();
    std::stable_sort(queues.begin(), queues.end(),
                     [](const auto &a, const auto &b) { return a.lastUsed > b.lastUsed; });
    QSet<QString> seen;
    for (const auto &queue : queues) {
        if (!queue.key.startsWith("queue:playlist:") || queue.tracks.isEmpty() ||
            queue.title.trimmed().isEmpty())
            continue;
        auto id = queue.key.sliced(QStringLiteral("queue:playlist:").size());
        if (id.isEmpty() || id == "0")
            continue;
        if (m_resourceLibrary) {
            for (int row = 0; row < m_resourceLibrary->rowCount(); ++row) {
                const auto index = m_resourceLibrary->index(row);
                if (index.data(UserPlaylistModel::SourceCodeRole).toUInt() == 2)
                    continue;
                const auto global = index.data(UserPlaylistModel::GlobalIdRole).toString();
                const auto creator = index.data(UserPlaylistModel::CreatorGlobalIdRole).toString();
                if (id == global || id == creator ||
                    id == index.data(UserPlaylistModel::CloudListIdRole).toString()) {
                    id = creator.isEmpty() ? global : creator;
                    break;
                }
            }
        }
        if (id.isEmpty() || seen.contains(id))
            continue;
        seen.insert(id);
        auto entry = resolveResource(
            resource("playlist-detail", id, queue.title, queue.tracks.first().cover));
        entry["pinned"] = m_pinned.contains(entry.value("key").toString());
        result.append(entry);
        if (result.size() == 12)
            break;
    }
    return result;
}
QVariantMap SidebarLayoutModel::resourceEntry(const QString &key) const
{
    if (m_resources.contains(key))
        return resolveResource(m_resources.value(key));
    for (const auto &value : m_frequentArtists)
        if (value.toMap().value("key") == key)
            return value.toMap();
    for (const auto &value : recentPlaylists())
        if (value.toMap().value("key") == key)
            return value.toMap();
    return {};
}
QString SidebarLayoutModel::resourceIdForKey(const QString &key) const
{
    return resourceEntry(key).value("resourceId").toString();
}
QString SidebarLayoutModel::homeResourceId() const
{
    for (const auto &value : shortcuts()) {
        const auto entry = value.toMap();
        if (!entry.value("route").toString().isEmpty())
            return entry.value("resourceId").toString();
    }
    return {};
}
bool SidebarLayoutModel::openShortcut(RouteController *router, const QString &key)
{
    if (!router)
        return false;
    if (isPlugin(key))
        return activate(key);
    const auto entry = resourceEntry(key);
    if (!entry.isEmpty())
        return router->navigate(entry.value("route").toString(), {},
                                entry.value("resourceId").toString());
    for (const auto &value : shortcutCatalog()) {
        const auto function = value.toMap();
        if (function.value("key") == key)
            return router->navigate(function.value("route").toString());
    }
    return false;
}
void SidebarLayoutModel::updatePortraits()
{
    if (!m_resourceNetwork || !m_resourceApi)
        return;
    // Pinned cards outlive the picker. Only candidate-only work retires on
    // close; visible sidebar artwork continues under the layout owner.
    auto wanted = m_browsing ? m_frequentArtists : QVariantList{};
    for (const auto &entry : m_resources)
        if (entry.value("route") == "artist-detail")
            wanted.append(entry);
    QSet<QString> visible;
    for (const auto &value : wanted)
        visible.insert(value.toMap().value("key").toString());
    for (const auto request : m_portraitRequests.keys()) {
        if (!visible.contains(m_portraitRequests.value(request))) {
            m_portraitAttempts.remove(m_portraitRequests.take(request));
            m_resourceApi->cancel(request);
        }
    }
    for (const auto &value : wanted) {
        const auto entry = value.toMap();
        const auto key = entry.value("key").toString();
        if (!entry.value("cover").toString().isEmpty() || m_portraitAttempts.contains(key))
            continue;
        if (m_portraitRequests.size() >= 6)
            break;
        m_portraitAttempts.insert(key);
        const auto request =
            m_resourceApi->artistDetail(entry.value("resourceId").toString().toULongLong());
        if (request)
            m_portraitRequests.insert(request, key);
    }
}
void SidebarLayoutModel::cancelPortraits()
{
    const auto requests = std::exchange(m_portraitRequests, {});
    if (m_resourceApi)
        for (auto request : requests.keys())
            m_resourceApi->cancel(request);
}
void SidebarLayoutModel::saveResources()
{
    QJsonArray values;
    for (const auto &key : m_pinned)
        if (m_resources.contains(key))
            values.append(QJsonObject::fromVariantMap(m_resources.value(key)));
    m_preferences.setValue(SettingsKeys::Sidebar::ResourceCards,
                           QJsonDocument(values).toJson(QJsonDocument::Compact));
}
} // namespace EchoMusic
