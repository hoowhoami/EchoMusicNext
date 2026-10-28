#include "PlaylistMutationService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

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
    return ready() && begin(m_api.removePlaylistTracks(id, files), Kind::RemoveTracks, id);
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
    m_coverFileName = update.coverReference ? update.coverReference->section(u'/', -1) : QString();
    m_expectedCoverUrl.clear();
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
    m_positions.clear();
    for (const auto &position : positions)
        m_positions.insert(position.id.value, position);
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
    m_userId = m_api.currentAccountUserId();
    m_error.clear();
    m_acknowledgement.clear();
    m_coverUpdate = update;
    m_coverPng = png;
    m_coverFileName.clear();
    m_expectedCoverUrl.clear();
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
    m_request = 0;
    m_coverStage = CoverStage::None;
    m_coverPng.clear();
    m_coverUpdate = {};
    m_coverFileName.clear();
    m_expectedCoverUrl.clear();
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
            m_state = State::OutcomeUnknown;
            m_error = QStringLiteral("上传结果无法核对，歌单尚未提交修改");
            emit changed();
            return;
        }
        m_coverFileName = file;
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
    const auto user = integer(object, "user_id"), version = integer(object, "total_revision"),
               total = integer(page, "total");
    if (!user || *user != m_userId || !version || *version != m_coverUpdate.totalRevision ||
        !page.value("items").isArray()) {
        coverFailed(QStringLiteral("歌单库已变化或读取无法核对，请保留草稿并重新加载"));
        return;
    }
    m_coverCount = total.value_or(0);
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
    m_userId = m_api.currentAccountUserId();
    m_kind = kind;
    m_id = id;
    if (kind != Kind::SortLibrary)
        m_positions.clear();
    if (kind != Kind::Update)
        m_coverFileName.clear();
    m_expectedCoverUrl.clear();
    m_acknowledgement.clear();
    m_error.clear();
    m_state = State::Writing;
    emit changed();
    return true;
}
void PlaylistMutationService::reset()
{
    const auto request = m_request;
    m_request = 0;
    m_acknowledgement.clear();
    m_error.clear();
    m_id = {0};
    m_coverStage = CoverStage::None;
    m_coverUpdate = {};
    m_coverPng.clear();
    m_coverFileName.clear();
    m_expectedCoverUrl.clear();
    m_positions.clear();
    m_verifiedPositions.clear();
    m_libraryReadIds.clear();
    m_revision = m_api.sessionRevision();
    m_state = !m_closed && m_api.currentAccountUserId() ? State::Ready : State::SignedOut;
    QPointer<PlaylistMutationService> guard(this);
    if (request)
        m_api.cancel(request);
    if (guard)
        emit changed();
}
void PlaylistMutationService::requireRefresh(const QString &message)
{
    m_request = 0;
    m_error = message;
    m_state = State::RefreshRequired;
    emit changed();
}
void PlaylistMutationService::succeed(quint64 request, const QByteArray &payload)
{
    if (request != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto object = QJsonDocument::fromJson(payload).object();
    if (m_coverStage != CoverStage::None) {
        coverSucceeded(object);
        return;
    }
    const auto user = integer(object, "user_id");
    if (m_state == State::Writing) {
        const auto id = integer(object, "cloud_list_id");
        const bool library = libraryOperation();
        const auto version = integer(object, library ? "total_revision" : "revision");
        const auto count = integer(object, library ? "playlist_count" : "song_count");
        const bool idValid = m_kind == Kind::SortLibrary
                                 ? !object.contains("cloud_list_id")
                                 : id && *id && (!m_id.value || *id == m_id.value);
        if (!user || *user != m_userId || !idValid || !version) {
            m_state = State::OutcomeUnknown;
            m_error = QStringLiteral("写操作响应无法核对，请先刷新歌单");
            emit changed();
            return;
        }
        if (id)
            m_id = {*id};
        if (!m_coverFileName.isEmpty()) {
            const auto cover = object.value("cover_url");
            if (!cover.isString() || cover.toString().isEmpty()) {
                m_state = State::OutcomeUnknown;
                m_error = QStringLiteral("封面响应无法核对，请先刷新");
                emit changed();
                return;
            }
            m_expectedCoverUrl = cover.toString();
        }
        m_expectedRevision = *version;
        if (count)
            m_expectedCount = *count;
        m_acknowledgement = payload;
        reconcile();
        return;
    }
    if (m_state != State::Reconciling)
        return;
    const bool library = libraryOperation();
    const auto page = object.value("page").toObject();
    const auto version = integer(object, library ? "total_revision" : "revision");
    const auto total = integer(page, "total");
    // Manual reconciliation can observe a later legitimate mutation. Capture
    // its first-page revision and require every remaining page to match.
    if (m_acceptNewer && m_page == 1 && version && *version >= m_expectedRevision && total) {
        m_expectedRevision = *version;
        m_expectedCount = *total;
    }
    if (!user || *user != m_userId || !version || *version != m_expectedRevision ||
        !page.value("items").isArray()) {
        requireRefresh(QStringLiteral("歌单已变化或读取无法核对，请刷新后查看当前状态"));
        return;
    }
    if (!library && integer(object, "cloud_list_id") != std::optional<quint64>(m_id.value)) {
        requireRefresh(QStringLiteral("歌曲所属歌单无法核对"));
        return;
    }
    const auto items = page.value("items").toArray();
    if (library) {
        m_seen += quint64(items.size());
        for (const auto &value : items) {
            const auto row = value.toObject();
            const auto cloudId = integer(row, "cloud_list_id");
            if (!cloudId || !*cloudId || m_libraryReadIds.contains(*cloudId))
                continue;
            m_libraryReadIds.insert(*cloudId);
            if (m_kind == Kind::SortLibrary) {
                const auto id = integer(row, "cloud_list_id");
                const auto found = m_positions.constFind(id.value_or(0));
                if (found != m_positions.cend()) {
                    const auto &position = found.value();
                    const auto kind = position.kind == ApiClient::PlaylistKind::Owned
                                          ? QStringLiteral("owned")
                                          : QStringLiteral("collected");
                    if (row.value("kind").toString() != kind ||
                        integer(row, "sort_index") != std::optional<quint64>(position.position) ||
                        m_verifiedPositions.contains(position.id.value)) {
                        requireRefresh(QStringLiteral("歌单排序读取与提交位置不一致"));
                        return;
                    }
                    m_verifiedPositions.insert(position.id.value);
                }
            }
            if (integer(value.toObject(), "cloud_list_id") == std::optional<quint64>(m_id.value)) {
                if (!m_coverFileName.isEmpty() &&
                    (!row.value("custom_cover").toBool() ||
                     row.value("cover_url").toString() != m_expectedCoverUrl)) {
                    requireRefresh(QStringLiteral("自定义封面尚未从歌单读取确认"));
                    return;
                }
                m_found = true;
            }
        }
        if (!items.isEmpty() && (total ? m_seen < *total : items.size() >= 200)) {
            ++m_page;
            readNext();
            return;
        }
        if (m_kind == Kind::SortLibrary ? m_verifiedPositions.size() != m_positions.size()
                                        : m_found != (m_kind != Kind::Remove)) {
            requireRefresh(QStringLiteral("写入后的歌单状态尚未确认"));
            return;
        }
    }
    const auto acknowledgement = m_acknowledgement;
    const auto revision = m_revision;
    const auto id = m_id.value;
    m_state = State::Ready;
    m_error.clear();
    m_acknowledgement.clear();
    QPointer<PlaylistMutationService> guard(this);
    if (library)
        emit libraryInvalidated();
    else
        emit tracksReconciled(id, *version, total ? qint64(*total) : -1, revision);
    if (!guard || revision != m_api.sessionRevision())
        return;
    if (!library)
        emit tracksInvalidated(id);
    if (!guard || revision != m_api.sessionRevision())
        return;
    emit completed(acknowledgement);
    if (!guard || revision != m_api.sessionRevision())
        return;
    emit changed();
}
void PlaylistMutationService::fail(quint64 request, const QByteArray &payload)
{
    if (request != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    if (m_coverStage != CoverStage::None && m_coverStage != CoverStage::Upload) {
        coverFailed(QStringLiteral("封面版本检查失败，请保留草稿并重新加载"));
        return;
    }
    if (m_state == State::Reconciling) {
        requireRefresh(QStringLiteral("写操作已收到响应，但读取核对失败，请重新刷新"));
        return;
    }
    const auto kind = QJsonDocument::fromJson(payload).object().value("kind").toString();
    // A business/auth/challenge rejection is definitive; other errors (including
    // invalid DTO/HTTP/transport) can follow a committed write and stay unknown.
    if (kind == QStringLiteral("business") || kind == QStringLiteral("authentication") ||
        kind == QStringLiteral("input") || kind == QStringLiteral("challenge")) {
        m_state = State::Ready;
        m_coverStage = CoverStage::None;
        m_coverPng.clear();
        m_coverUpdate = {};
        m_coverFileName.clear();
        m_expectedCoverUrl.clear();
        m_error = QStringLiteral("音乐服务拒绝了写操作");
    } else {
        m_state = State::OutcomeUnknown;
        m_error = QStringLiteral("写操作结果未知，请先刷新，避免重复提交");
    }
    emit changed();
}
bool PlaylistMutationService::reconcile()
{
    if (m_request || m_acknowledgement.isEmpty() || m_revision != m_api.sessionRevision())
        return false;
    m_acceptNewer = m_state == State::RefreshRequired;
    m_page = 1;
    m_seen = 0;
    m_found = false;
    m_verifiedPositions.clear();
    m_libraryReadIds.clear();
    m_state = State::Reconciling;
    m_error.clear();
    const auto revision = m_revision;
    QPointer<PlaylistMutationService> guard(this);
    emit changed();
    if (!guard || revision != m_api.sessionRevision() || m_state != State::Reconciling)
        return false;
    readNext();
    return guard && m_request != 0;
}
void PlaylistMutationService::readNext()
{
    const bool library = libraryOperation();
    m_request =
        library ? m_api.userPlaylists(m_page, 100) : m_api.cloudPlaylistTracks(m_id, 1, 300);
    if (!m_request)
        requireRefresh(QStringLiteral("读取核对无法启动"));
}
void PlaylistMutationService::cancel()
{
    if (!m_request)
        return;
    const auto request = m_request;
    m_request = 0;
    QPointer<PlaylistMutationService> guard(this);
    m_api.cancel(request);
    if (!guard)
        return;
    if (m_coverStage == CoverStage::BeforeUpload || m_coverStage == CoverStage::AfterUpload) {
        coverFailed(QStringLiteral("已取消封面版本检查，歌单未提交修改"));
        return;
    }
    if (m_state == State::Writing || m_state == State::UploadingCover) {
        m_state = State::OutcomeUnknown;
        m_error = QStringLiteral("已停止等待，远端写入结果需刷新确认");
        emit changed();
    } else
        requireRefresh(QStringLiteral("已停止读取核对，请重新刷新"));
}
void PlaylistMutationService::acceptRefreshedUnknownOutcome()
{
    if (m_state == State::OutcomeUnknown && m_revision == m_api.sessionRevision()) {
        m_coverStage = CoverStage::None;
        m_coverPng.clear();
        m_coverUpdate = {};
        m_coverFileName.clear();
        m_expectedCoverUrl.clear();
        m_positions.clear();
        m_libraryReadIds.clear();
        m_acknowledgement.clear();
        m_state = State::Ready;
        m_error.clear();
        emit changed();
    }
}
} // namespace EchoMusic
