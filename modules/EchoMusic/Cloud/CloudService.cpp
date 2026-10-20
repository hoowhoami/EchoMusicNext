#include "CloudService.h"
#include "CloudAudioIndex.h"
#include "PlaybackService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlEngine>
#include <QSet>
#include <algorithm>
namespace EchoMusic
{
CloudService::CloudService(ApiClient &api, PlaybackService &playback, SongMetadataService &metadata,
                           RouteController &router, bool network, QObject *parent)
    : QObject(parent), m_api(api), m_playback(playback), m_metadata(metadata), m_router(router),
      m_upload(api, network, this), m_actions(m_songs, playback, this), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    connect(&m_upload, &CloudUploadService::cloudChanged, this, [this] {
        m_playback.cloudSources()->invalidate();
        m_page = 0;
        m_exhausted = false;
        refresh();
    });
    connect(&m_upload, &CloudUploadService::notice, this, &CloudService::notice);
    m_actions.setQueueContext(tr("云盘音乐"), "queue:cloud");
    connect(&router, &RouteController::pageActivated, this, &CloudService::activate);
    connect(&api, &ApiClient::succeeded, this, &CloudService::accept);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &bytes) { fail(id, bytes); });
    connect(&api, &ApiClient::sessionChanged, this, &CloudService::reset);
    connect(&api, &ApiClient::closed, this, &CloudService::shutdown);
    connect(&metadata, &SongMetadataService::completed, this, &CloudService::acceptMetadata);
}
CloudService::~CloudService()
{
    shutdown();
}
bool CloudService::hasMore() const
{
    return !m_exhausted && m_total > 0 && m_received < m_total;
}
void CloudService::activate(RouteContext *context)
{
    if (m_closed)
        return;
    if (!context || context->name() != "cloud") {
        m_active = false;
        cancelAll();
        return;
    }
    if (m_instance != context->instanceId()) {
        m_active = false;
        reset();
        m_instance = context->instanceId();
    }
    m_active = true;
    if (m_active && m_network && !m_page && m_api.currentAccountUserId())
        requestPage(1);
    else if (m_page)
        completeSongs(m_items);
    emit changed();
}
void CloudService::refresh()
{
    if (m_closed || !m_active || !m_network || !m_api.currentAccountUserId())
        return;
    cancelMetadata();
    if (m_listRequest) {
        m_api.cancel(m_listRequest);
        m_listRequest = 0;
    }
    m_page = 0;
    m_exhausted = false;
    m_error.clear();
    requestPage(1);
}
void CloudService::loadMore()
{
    if (m_closed || !m_active || !m_network || m_listRequest || !hasMore())
        return;
    requestPage(m_page + 1);
}
void CloudService::requestPage(quint32 page)
{
    if (m_closed || !m_network || m_listRequest || !m_api.currentAccountUserId())
        return;
    m_listSession = m_api.sessionRevision();
    m_listRequest = m_api.userCloud(page, 100);
    if (!m_listRequest) {
        m_error = tr("加载云盘歌曲失败");
        emit changed();
        return;
    }
    m_error.clear();
    emit changed();
}
bool CloudService::canRemoveSong(const QString &resourceId) const
{
    // Ineligible rows (missing/zero kv_id) are disabled in the menu instead of
    // surfacing a server-side skip; unknown ids are equally ineligible.
    return m_targets.value(resourceId).kvId != 0;
}
bool CloudService::removeSongs(const QStringList &ids)
{
    if (m_closed || m_deleteRequest || ids.isEmpty() || !m_api.currentAccountUserId())
        return false;
    m_deleteTargets.clear();
    m_deleteIds.clear();
    for (const auto &id : ids) {
        const auto target = m_targets.value(id);
        if (!target.kvId)
            continue;
        m_deleteTargets.append(target);
        m_deleteIds.append(id);
    }
    m_deleteRequested = ids.size();
    if (m_deleteTargets.isEmpty()) {
        m_error = tr("缺少云盘文件标识，无法删除");
        // Warn when no selected row carries a file ID.
        // The menu normally prevents this; retain the service guard for other callers.
        emit notice(m_error, true);
        emit changed();
        return false;
    }
    m_deleteSession = m_api.sessionRevision();
    m_deleteRequest = m_api.userCloudDelete(m_deleteTargets);
    if (!m_deleteRequest) {
        m_error = tr("删除云盘歌曲失败");
        emit changed();
        return false;
    }
    m_error.clear();
    emit changed();
    return true;
}
void CloudService::updateCounters(const QJsonObject &counters)
{
    if (counters.isEmpty())
        return;
    const auto used = counters.value("used_size");
    const auto available = counters.value("availble_size");
    const auto capacity = counters.value("max_size");
    if (used.isDouble())
        m_usedBytes = qint64(used.toInteger());
    if (available.isDouble())
        m_availableBytes = qint64(available.toInteger());
    if (capacity.isDouble())
        m_capacityBytes = qint64(capacity.toInteger());
    const auto count = counters.value("list_count");
    if (count.isDouble())
        m_total = quint64(qMax<qint64>(0, count.toInteger()));
}
void CloudService::project()
{
    QList<QJsonObject> ordered = m_items;
    // Order by added time descending, then provider sort index ascending.
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const QJsonObject &a, const QJsonObject &b) {
                         const auto aa = a.value("added_at_seconds").toInteger(-1);
                         const auto bb = b.value("added_at_seconds").toInteger(-1);
                         if (aa != bb)
                             return aa > bb;
                         const auto as = a.value("sort_index").toInteger(0);
                         const auto bs = b.value("sort_index").toInteger(0);
                         if (as != bs)
                             return as < bs;
                         return a.value("_arrival").toInteger() < b.value("_arrival").toInteger();
                     });
    QJsonArray songs;
    QSet<QString> used;
    m_targets.clear();
    for (const auto &item : ordered) {
        auto song = item.value("song").toObject();
        auto metadata = item.value("metadata").toObject();
        metadata.insert("cloud_source", CloudAudioIndex::fromRow(item));
        metadata.insert("source_kind", "cloud");
        song.insert("metadata", metadata);
        const auto fileId = quint64(item.value("file_id").toInteger());
        const auto albumAudioId = song.value("id").toString().toULongLong();
        QString id = song.value("id").toString();
        // Keep the existing file-scoped fallback for missing/conflicting
        // presentation IDs; deletion always uses the provider file identity.
        if (id.isEmpty() || used.contains(id)) {
            id = fileId ? QStringLiteral("cloud-file:%1").arg(fileId)
                        : QStringLiteral("cloud-file:%1")
                              .arg(qint64(item.value("_arrival").toInteger()));
        }
        used.insert(id);
        song.insert("id", id);
        m_targets.insert(id, {fileId, albumAudioId});
        songs.append(song);
    }
    m_songs.replaceSongs(songs, true);
}
void CloudService::accept(quint64 id, const QByteArray &bytes)
{
    if (m_closed)
        return;
    if (m_deleteRequest && id == m_deleteRequest && m_deleteSession == m_api.sessionRevision()) {
        finishDelete(bytes);
        return;
    }
    if (!m_listRequest || id != m_listRequest || m_listSession != m_api.sessionRevision())
        return;
    m_listRequest = 0;
    const auto object = QJsonDocument::fromJson(bytes).object();
    const auto page = object.value("page").toObject();
    const auto items = page.value("items").toArray();
    const auto number = quint32(page.value("page").toInteger(m_page + 1));
    QSet<quint64> seen;
    if (number > 1)
        for (const auto &item : m_items)
            seen.insert(quint64(item.value("file_id").toInteger()));
    QList<QJsonObject> accepted;
    for (const auto &value : items) {
        auto item = value.toObject();
        const auto fileId = quint64(item.value("file_id").toInteger());
        // Keep rows without cloud file IDs visible but disable deletion.
        // Deduplicate only rows with real IDs.
        if (fileId && seen.contains(fileId))
            continue;
        if (fileId)
            seen.insert(fileId);
        item.insert("_arrival", qint64(++m_arrival));
        accepted.append(item);
    }
    if (number <= 1) {
        m_items = accepted;
        m_received = quint64(accepted.size());
        m_arrival = quint32(accepted.size());
    } else {
        m_items += accepted;
        m_received += quint64(accepted.size());
    }
    m_page = number;
    if (page.value("total").isDouble())
        m_total = quint64(qMax<qint64>(0, page.value("total").toInteger()));
    updateCounters(object.value("counters").toObject());
    m_exhausted = items.isEmpty() || (m_total > 0 && m_received >= m_total);
    m_error.clear();
    project();
    completeSongs(accepted);
    emit changed();
}
void CloudService::completeSongs(const QList<QJsonObject> &items)
{
    if (m_closed || !m_active || !m_network || !signedIn())
        return;
    QStringList ids;
    QSet<QString> scheduled;
    for (const auto &item : items) {
        auto song = item.value("song").toObject();
        song["metadata"] = item.value("metadata");
        const auto id = song.value("id").toString();
        if (!id.isEmpty() && !scheduled.contains(id) &&
            SongMetadataService::needsCompletion(song)) {
            scheduled.insert(id);
            ids.append(id);
        }
    }
    if (const auto ticket = m_metadata.request(ids))
        m_metadataRequests.insert(ticket, {m_api.sessionRevision(), m_metadataGeneration});
}
void CloudService::acceptMetadata(quint64 ticket, const QJsonArray &values)
{
    if (!m_metadataRequests.contains(ticket))
        return;
    const auto pending = m_metadataRequests.take(ticket);
    if (m_closed || !m_active || pending.session != m_api.sessionRevision() ||
        pending.generation != m_metadataGeneration)
        return;
    QHash<QString, QJsonObject> presentations;
    for (const auto &value : values) {
        const auto presentation = value.toObject();
        presentations.insert(presentation.value("id").toString(), presentation);
    }
    // Keep every file's source presentation current before a later page/delete
    // projects the list again. File identity, order and rights stay untouched.
    for (auto &item : m_items) {
        auto song = item.value("song").toObject();
        const auto found = presentations.constFind(song.value("id").toString());
        if (found == presentations.cend())
            continue;
        song["metadata"] = item.value("metadata");
        auto completed = SongMetadataService::completeSong(song, *found);
        item["metadata"] = completed.take("metadata");
        item["song"] = completed;
    }
    QJsonArray projected;
    for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it) {
        const auto found = presentations.constFind(QString::number(it->albumAudioId));
        if (found == presentations.cend())
            continue;
        auto presentation = *found;
        // Apply facts to the existing view ID without changing file identity.
        presentation["id"] = it.key();
        projected.append(presentation);
    }
    m_songs.applyPresentations(projected);
}
void CloudService::cancelMetadata()
{
    ++m_metadataGeneration;
    const auto tickets = m_metadataRequests.keys();
    m_metadataRequests.clear();
    for (const auto ticket : tickets)
        m_metadata.cancel(ticket);
}
void CloudService::finishDelete(const QByteArray &bytes)
{
    m_playback.cloudSources()->invalidate();
    m_deleteRequest = 0;
    const auto object = QJsonDocument::fromJson(bytes).object();
    updateCounters(object.value("counters").toObject());
    QSet<quint64> removedIds;
    for (const auto &target : m_deleteTargets)
        removedIds.insert(target.kvId);
    const int removed = m_deleteTargets.size();
    m_items.removeIf([&removedIds](const QJsonObject &item) {
        return removedIds.contains(quint64(item.value("file_id").toInteger()));
    });
    m_total = m_total > quint64(removed) ? m_total - quint64(removed) : 0;
    m_received = m_items.isEmpty() ? 0 : qMin(m_received, quint64(m_items.size()));
    const int requested = m_deleteRequested;
    const int skipped = requested - removed;
    if (requested <= 1)
        emit notice(tr("已从云盘删除"), false);
    else if (skipped > 0)
        emit notice(tr("已从云盘删除 %1 首，%2 首缺少文件标识").arg(removed).arg(skipped), true);
    else
        emit notice(tr("已从云盘删除 %1 首").arg(removed), false);
    m_deleteTargets.clear();
    m_deleteIds.clear();
    m_deleteRequested = 0;
    project();
    emit changed();
    // Re-read after a committed mutation instead of trusting the ack alone.
    refresh();
}
void CloudService::fail(quint64 id, const QByteArray &bytes)
{
    const auto error = QJsonDocument::fromJson(bytes).object();
    const auto kind = error.value("kind").toString();
    const auto message = error.value("message").toString();
    if (m_deleteRequest && id == m_deleteRequest && m_deleteSession == m_api.sessionRevision()) {
        m_deleteRequest = 0;
        m_deleteTargets.clear();
        m_deleteIds.clear();
        m_deleteRequested = 0;
        const auto text = kind == QStringLiteral("authentication") ? tr("登录已失效，请重新登录")
                          : !message.isEmpty()                     ? message
                                                                   : tr("删除云盘歌曲失败");
        m_error = text;
        emit notice(text, true);
        emit changed();
        return;
    }
    if (!m_listRequest || id != m_listRequest || m_listSession != m_api.sessionRevision())
        return;
    m_listRequest = 0;
    m_error = kind == QStringLiteral("authentication") ? tr("登录已失效，请重新登录")
              : !message.isEmpty()                     ? message
                                                       : tr("加载云盘歌曲失败");
    emit changed();
}
void CloudService::cancelAll()
{
    cancelMetadata();
    if (m_listRequest) {
        m_api.cancel(m_listRequest);
        m_listRequest = 0;
    }
    if (m_deleteRequest) {
        m_api.cancel(m_deleteRequest);
        m_deleteRequest = 0;
        m_deleteTargets.clear();
        m_deleteIds.clear();
        m_deleteRequested = 0;
    }
}
void CloudService::reset()
{
    if (!m_listRequest && !m_deleteRequest && m_items.isEmpty() && m_page == 0)
        return;
    cancelAll();
    m_items.clear();
    m_targets.clear();
    m_songs.clear();
    m_total = m_received = 0;
    m_page = 0;
    m_arrival = 0;
    m_exhausted = false;
    m_usedBytes = m_availableBytes = m_capacityBytes = -1;
    m_error.clear();
    if (m_active && m_router.current() && m_router.current()->name() == "cloud" && m_network &&
        m_api.currentAccountUserId())
        requestPage(1);
    emit changed();
}
void CloudService::shutdown()
{
    m_upload.shutdown();
    if (m_closed)
        return;
    m_closed = true;
    cancelAll();
    emit changed();
}
} // namespace EchoMusic

namespace EchoMusic
{
void CloudService::registerRoutes(RouteController &router)
{
    router.registerPage({"cloud",
                         "/main/cloud",
                         QUrl("qrc:/qt/qml/EchoMusic/Cloud/CloudPage.qml"),
                         {},
                         true,
                         true,
                         false,
                         {{"service", QVariant::fromValue(this)}}});
}
} // namespace EchoMusic
