#include "PlaylistMutationService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <limits>
#include <utility>

namespace EchoMusic
{
namespace
{
std::optional<quint64> integer(const QJsonObject &object, const char *key)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble() || value.toInteger(-1) < 0)
        return {};
    const auto result = value.toInteger(-1);
    if (value.toDouble() != double(result))
        return {};
    return quint64(result);
}
qint64 count(const QJsonObject &object, const char *key)
{
    const auto value = integer(object, key);
    return value ? qint64(*value) : -1;
}
} // namespace
PlaylistMutationService::PlaylistMutationService(ApiClient &api, QObject *parent)
    : QObject(parent), m_api(api)
{
    connect(&api, &ApiClient::succeeded, this, &PlaylistMutationService::succeed);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &payload) { fail(id, payload); });
    connect(&api, &ApiClient::sessionChanged, this, [this] { reset(); });
    connect(&api, &ApiClient::closed, this, [this] {
        m_closed = true;
        reset();
    });
    reset();
}
PlaylistMutationService::~PlaylistMutationService()
{
    if (m_request)
        m_api.cancel(m_request);
    if (m_refreshRequest)
        m_api.cancel(m_refreshRequest);
}
bool PlaylistMutationService::ready() const
{
    return !m_closed && m_state == State::Ready && m_api.currentAccountUserId() != 0;
}
bool PlaylistMutationService::create(const QString &title, bool isPrivate)
{
    return ready() && begin(m_api.createPlaylist(title, isPrivate), Kind::Create, {0});
}
bool PlaylistMutationService::collect(const ApiClient::CollectionTarget &target)
{
    return ready() && begin(m_api.collectPlaylist(target), Kind::Create, {0});
}
bool PlaylistMutationService::addTracks(ApiClient::CloudPlaylistId id,
                                        const QList<ApiClient::PlaylistTrackInput> &tracks)
{
    return ready() && begin(m_api.addPlaylistTracks(id, tracks), Kind::AddTracks, id);
}
bool PlaylistMutationService::removeTracks(ApiClient::CloudPlaylistId id,
                                           const QList<ApiClient::PlaylistTrackFileId> &files)
{
    if (!ready() || files.isEmpty())
        return false;
    QSet<quint64> seen;
    for (const auto &file : files) {
        if (!file.value || file.value > quint64(std::numeric_limits<qint64>::max()) ||
            seen.contains(file.value))
            return false;
        seen.insert(file.value);
    }
    m_removeRemaining = files.mid(300);
    m_removeBatch.clear();
    for (const auto &file : files.mid(0, 300))
        m_removeBatch.append(file.value);
    m_removeAcknowledged = false;
    if (begin(m_api.removePlaylistTracks(id, files.mid(0, 300)), Kind::RemoveTracks, id))
        return true;
    m_removeRemaining.clear();
    m_removeBatch.clear();
    return false;
}
bool PlaylistMutationService::sortTracks(ApiClient::CloudPlaylistId id, quint64 revision,
                                         const QList<ApiClient::PlaylistTrackPosition> &positions)
{
    return ready() &&
           begin(m_api.sortPlaylistTracks(id, revision, positions), Kind::SortTracks, id);
}
bool PlaylistMutationService::update(const ApiClient::PlaylistUpdate &update)
{
    if (!ready())
        return false;
    return ready() && begin(m_api.updatePlaylist(update), Kind::Update, update.id);
}
bool PlaylistMutationService::libraryOperation() const
{
    return m_kind == Kind::Create || m_kind == Kind::Update || m_kind == Kind::Remove ||
           m_kind == Kind::SortLibrary;
}
bool PlaylistMutationService::sortLibrary(quint64 totalRevision,
                                          const QList<ApiClient::PlaylistPosition> &positions)
{
    if (!ready())
        return false;
    const auto request = m_api.sortPlaylists(totalRevision, positions);
    if (!request)
        return false;
    return begin(request, Kind::SortLibrary, {0});
}
bool PlaylistMutationService::saveCover(const ApiClient::PlaylistUpdate &update,
                                        const QByteArray &png)
{
    return !png.isEmpty() && saveEdit(update, png);
}
bool PlaylistMutationService::saveEdit(const ApiClient::PlaylistUpdate &update,
                                       const QByteArray &png)
{
    if (!ready() || !update.id.value || update.coverReference || png.size() > 8 * 1024 * 1024 ||
        (!png.isEmpty() && !png.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))))
        return false;
    m_revision = m_api.sessionRevision();
    m_error.clear();
    m_coverUpdate = update;
    m_coverPng = png;
    m_coverStage = CoverStage::BeforeUpload;
    m_page = 1;
    m_seen = 0;
    m_found = false;
    m_libraryReadIds.clear();
    QPointer<PlaylistMutationService> guard(this);
    readCoverLibrary();
    return guard && m_request != 0;
}
void PlaylistMutationService::coverFailed(const QString &message)
{
    // Earlier batches remain successful. Refresh their actual result without
    // retrying the failed/unknown write or discarding the provider's error.
    if (m_removeAcknowledged)
        queueRefresh(false, m_id);
    m_removeRemaining.clear();
    m_removeBatch.clear();
    m_removeAcknowledged = false;
    m_request = 0;
    m_coverStage = CoverStage::None;
    m_coverPng.clear();
    m_coverUpdate = {};
    m_libraryReadIds.clear();
    m_state = State::Ready;
    m_error = message;
    emit changed();
}
void PlaylistMutationService::readCoverLibrary()
{
    m_state = State::CheckingCover;
    m_request = m_api.userPlaylists(m_page, 100);
    if (!m_request) {
        coverFailed(QStringLiteral("无法开始封面版本检查"));
        return;
    }
    emit changed();
}
void PlaylistMutationService::coverSucceeded(const QJsonObject &object)
{
    if (m_coverStage == CoverStage::Upload) {
        const auto file = object.value("file_name").toString(),
                   reference = object.value("reference").toString();
        if (file.isEmpty() || file.contains(u'/') || file.contains(u'\\') ||
            !file.endsWith(QStringLiteral(".png")) ||
            reference != QStringLiteral("custom/") + file) {
            m_coverPng.clear();
            coverFailed(tr("封面上传失败，请重新选择图片"));
            return;
        }
        m_coverUpdate.coverReference = reference;
        m_coverStage = CoverStage::AfterUpload;
        m_page = 1;
        m_seen = 0;
        m_found = false;
        m_libraryReadIds.clear();
        readCoverLibrary();
        return;
    }
    const auto page = object.value("page").toObject();
    const auto version = integer(object, "total_revision"), total = integer(page, "total");
    if (!version || *version != m_coverUpdate.totalRevision) {
        coverFailed(tr("歌单列表已变化，请重新加载后修改"));
        return;
    }
    const auto previousSeen = m_seen;
    for (const auto &value : page.value("items").toArray()) {
        const auto row = value.toObject();
        const auto id = integer(row, "cloud_list_id");
        if (!id || !*id || m_libraryReadIds.contains(*id))
            continue;
        m_libraryReadIds.insert(*id);
        ++m_seen;
        if (*id == m_coverUpdate.id.value) {
            const auto kind = m_coverUpdate.kind == ApiClient::PlaylistKind::Owned
                                  ? QStringLiteral("owned")
                                  : QStringLiteral("collected");
            if (row.value("kind").toString() != kind ||
                integer(row, "source_code") == std::optional<quint64>(2) ||
                row.value("default_kind").toInt() > 0 ||
                integer(row, "sort_index") !=
                    std::optional<quint64>(quint64(m_coverUpdate.sortIndex))) {
                coverFailed(QStringLiteral("无法确认当前账号的可编辑歌单"));
                return;
            }
            m_found = true;
        }
    }
    if (m_seen > previousSeen &&
        (total ? m_seen < *total : page.value("items").toArray().size() >= 100)) {
        ++m_page;
        readCoverLibrary();
        return;
    }
    if (!m_found) {
        coverFailed(QStringLiteral("歌单已不存在"));
        return;
    }
    if (m_coverStage == CoverStage::BeforeUpload && !m_coverPng.isEmpty()) {
        m_request = m_api.uploadPlaylistCover(m_coverPng);
        m_coverPng.clear();
        if (!m_request) {
            coverFailed(QStringLiteral("封面 PNG 无效"));
            return;
        }
        m_coverStage = CoverStage::Upload;
        m_state = State::UploadingCover;
        emit changed();
        return;
    }
    m_coverStage = CoverStage::None;
    const auto request = m_api.updatePlaylist(m_coverUpdate);
    if (!request) {
        coverFailed(QStringLiteral("封面修改参数无效"));
        return;
    }
    begin(request, Kind::Update, m_coverUpdate.id);
}
bool PlaylistMutationService::remove(ApiClient::CloudPlaylistId id)
{
    return ready() && begin(m_api.deletePlaylist(id), Kind::Remove, id);
}
bool PlaylistMutationService::begin(quint64 request, Kind kind, ApiClient::CloudPlaylistId id)
{
    if (!request)
        return false;
    m_request = request;
    m_revision = m_api.sessionRevision();
    m_kind = kind;
    m_id = id;
    m_error.clear();
    m_state = State::Writing;
    emit changed();
    return true;
}
void PlaylistMutationService::reset()
{
    m_removeRemaining.clear();
    m_removeBatch.clear();
    m_removeAcknowledged = false;
    const auto request = std::exchange(m_request, 0);
    const auto refresh = std::exchange(m_refreshRequest, 0);
    m_refresh.reset();
    m_refreshQueue.clear();
    m_error.clear();
    m_id = {0};
    m_coverStage = CoverStage::None;
    m_coverUpdate = {};
    m_coverPng.clear();
    m_libraryReadIds.clear();
    m_revision = m_api.sessionRevision();
    m_state = !m_closed && m_api.currentAccountUserId() ? State::Ready : State::SignedOut;
    QPointer<PlaylistMutationService> guard(this);
    if (request)
        m_api.cancel(request);
    if (refresh)
        m_api.cancel(refresh);
    if (guard)
        emit changed();
}
void PlaylistMutationService::succeed(quint64 request, const QByteArray &payload)
{
    const auto object = QJsonDocument::fromJson(payload).object();
    if (request && request == m_refreshRequest) {
        m_refreshRequest = 0;
        if (m_refresh && m_refresh->session == m_api.sessionRevision())
            acceptRefresh(object);
        return;
    }
    if (!request || request != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    if (m_coverStage != CoverStage::None) {
        coverSucceeded(object);
        return;
    }
    if (m_kind == Kind::RemoveTracks) {
        const auto removalSession = m_revision;
        const auto removedFiles = m_removeBatch;
        QPointer<PlaylistMutationService> guard(this);
        emit trackFilesRemoved(m_id.value, removedFiles, removalSession);
        if (!guard || removalSession != m_api.sessionRevision())
            return;
        m_removeBatch.clear();
    }
    if (m_kind == Kind::RemoveTracks && !m_removeRemaining.isEmpty()) {
        m_removeAcknowledged = true;
        const auto batch = m_removeRemaining.mid(0, 300);
        m_removeRemaining.remove(0, batch.size());
        for (const auto &file : batch)
            m_removeBatch.append(file.value);
        if (!begin(m_api.removePlaylistTracks(m_id, batch), Kind::RemoveTracks, m_id))
            coverFailed(tr("无法继续移除歌单歌曲"));
        return;
    }
    m_removeAcknowledged = false;
    const auto session = m_revision;
    const auto id = m_id;
    const bool library = libraryOperation();
    m_state = State::Ready;
    m_error.clear();
    // API success is the write result. Refreshing the view is a separate read,
    // so a stale replica or failed refresh cannot turn it into a failed write.
    queueRefresh(library, id);
    QPointer<PlaylistMutationService> guard(this);
    if (!library) {
        emit tracksReconciled(id.value, integer(object, "revision").value_or(0),
                              count(object, "song_count"), session);
        if (!guard || session != m_api.sessionRevision())
            return;
        emit tracksInvalidated(id.value);
    }
    if (!guard || session != m_api.sessionRevision())
        return;
    emit completed(payload);
    if (guard && session == m_api.sessionRevision())
        emit changed();
}
void PlaylistMutationService::queueRefresh(bool library, ApiClient::CloudPlaylistId id)
{
    if (m_refresh && m_refresh->library == library &&
        (library || m_refresh->id.value == id.value)) {
        const auto request = std::exchange(m_refreshRequest, 0);
        m_refresh.reset();
        if (request)
            m_api.cancel(request);
    }
    // Coalesce queued updates; a newer acknowledgement retires that resource's
    // older refresh without affecting reads of other playlists.
    bool queued = false;
    for (const auto &refresh : std::as_const(m_refreshQueue))
        if (refresh.library == library && (library || refresh.id.value == id.value)) {
            queued = true;
            break;
        }
    if (!queued)
        m_refreshQueue.append(
            Refresh{library, id, m_api.sessionRevision(), 1, std::nullopt, {}, {}});
    QTimer::singleShot(0, this, &PlaylistMutationService::readRefresh);
}
void PlaylistMutationService::readRefresh()
{
    if (m_closed || m_refreshRequest)
        return;
    if (!m_refresh) {
        if (m_refreshQueue.isEmpty())
            return;
        m_refresh = m_refreshQueue.takeFirst();
    }
    if (m_refresh->session != m_api.sessionRevision()) {
        finishRefresh();
        return;
    }
    m_refreshRequest = m_refresh->library ? m_api.userPlaylists(m_refresh->page, 100)
                                          : m_api.cloudPlaylistTracks(m_refresh->id, 1, 300);
    if (!m_refreshRequest)
        finishRefresh();
}
void PlaylistMutationService::acceptRefresh(const QJsonObject &object)
{
    if (!m_refresh)
        return;
    const auto session = m_refresh->session;
    const auto page = object.value("page").toObject();
    const auto items = page.value("items").toArray();
    QPointer<PlaylistMutationService> guard(this);
    if (!m_refresh->library) {
        const auto id = m_refresh->id.value;
        emit tracksReconciled(id, integer(object, "revision").value_or(0), count(page, "total"),
                              session);
        if (!guard || session != m_api.sessionRevision())
            return;
        emit trackPageReconciled(object, session);
    } else {
        const auto version = integer(object, "total_revision");
        // This is one paged directory read; do not merge different snapshots.
        if (m_refresh->page > 1 && version != m_refresh->version) {
            finishRefresh();
            return;
        }
        m_refresh->version = version;
        const auto previousCount = m_refresh->items.size();
        bool repeatedPage =
            m_refresh->page > 1 && !items.isEmpty() && previousCount >= items.size();
        for (qsizetype index = 0; repeatedPage && index < items.size(); ++index)
            repeatedPage =
                m_refresh->items.at(previousCount - items.size() + index) == items[index];
        for (const auto &item : repeatedPage ? QJsonArray{} : items) {
            const auto id = quint64(item.toObject().value("cloud_list_id").toInteger());
            const auto found = m_refresh->positions.constFind(id);
            // Missing account-local IDs and distinct public identities must
            // remain separate rows, just as in the normal library reader.
            const auto previous = found != m_refresh->positions.cend()
                                      ? m_refresh->items[*found].toObject()
                                      : QJsonObject{};
            const auto row = item.toObject();
            if (id && found != m_refresh->positions.cend() && *found < previousCount &&
                previous.value("global_id") == row.value("global_id") &&
                previous.value("kind") == row.value("kind"))
                m_refresh->items[*found] = item;
            else {
                if (id)
                    m_refresh->positions.insert(id, m_refresh->items.size());
                m_refresh->items.append(item);
            }
        }
        const auto total = integer(page, "total");
        if (m_refresh->items.size() > previousCount &&
            (total ? quint64(m_refresh->items.size()) < *total : items.size() >= 100)) {
            ++m_refresh->page;
            readRefresh();
            return;
        }
        auto snapshot = object;
        auto completePage = page;
        completePage["items"] = m_refresh->items;
        snapshot["page"] = completePage;
        emit libraryReconciled(snapshot, session);
        if (!guard || session != m_api.sessionRevision())
            return;
        emit libraryInvalidated();
    }
    if (guard && session == m_api.sessionRevision())
        finishRefresh();
}
void PlaylistMutationService::finishRefresh()
{
    m_refresh.reset();
    m_refreshRequest = 0;
    QTimer::singleShot(0, this, &PlaylistMutationService::readRefresh);
}
void PlaylistMutationService::fail(quint64 request, const QByteArray &payload)
{
    if (request && request == m_refreshRequest) {
        finishRefresh(); // A view refresh cannot change an acknowledged write.
        return;
    }
    if (!request || request != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    coverFailed(ApiClient::errorMessage(payload, tr("歌单操作失败，请稍后重试")));
}
void PlaylistMutationService::cancel()
{
    if (!m_request)
        return;
    const auto request = std::exchange(m_request, 0);
    QPointer<PlaylistMutationService> guard(this);
    m_api.cancel(request);
    if (guard)
        coverFailed({});
}
} // namespace EchoMusic
