#include "UserPlaylistModel.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <utility>

namespace EchoMusic
{
UserPlaylistModel::UserPlaylistModel(ApiClient &api, QObject *parent)
    : QAbstractListModel(parent), m_api(api)
{
    m_state = api.currentAccountUserId() ? State::Idle : State::SignedOut;
    connect(&api, &ApiClient::succeeded, this, &UserPlaylistModel::succeed);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &payload) { fail(id, payload); });
    connect(&api, &ApiClient::sessionChanged, this, [this] { resetState(); });
    connect(&api, &ApiClient::closed, this, [this] {
        m_closed = true;
        resetState();
    });
}
UserPlaylistModel::~UserPlaylistModel()
{
    if (m_request)
        m_api.cancel(m_request);
}
void UserPlaylistModel::attachStorage(StorageService &storage)
{
    Q_ASSERT(!m_storage);
    m_storage = &storage;
    m_coverAccount = m_api.currentAccountUserId();
    connect(&storage, &StorageService::playlistCoversFailed, this,
            [](const QString &message) { qWarning().noquote() << message; });
    connect(&storage, &StorageService::initialized, this, [this] {
        m_storageReady = true;
        if (m_coverAccount) {
            // A fast network result can precede SQL initialization. Queue that
            // current-account result before reading the persisted snapshot.
            for (const auto &cover : std::as_const(m_autoCovers))
                m_storage->savePlaylistCover(m_coverAccount, cover);
            m_storage->loadPlaylistCovers(m_api.sessionRevision(), m_coverAccount);
        }
    });
    connect(&storage, &StorageService::playlistCoversReady, this,
            [this](quint64 request, quint64 account, const StoredPlaylistCovers &covers) {
                if (m_closed || request != m_api.sessionRevision() ||
                    account != m_api.currentAccountUserId())
                    return;
                for (const auto &cover : covers)
                    if (!m_autoCovers.contains(cover.listId))
                        m_autoCovers.insert(cover.listId, cover);
                for (int row = 0; row < m_rows.size(); ++row)
                    if (m_autoCovers.contains(m_rows[row].cloudId.value)) {
                        const auto global = m_rows[row].globalId;
                        emit dataChanged(index(row), index(row), {CoverUrlRole});
                        emit coverChanged(global);
                    }
            });
}
QString UserPlaylistModel::coverFor(const QString &global, const QString &fallback) const
{
    if (global.isEmpty())
        return fallback;
    for (const auto &row : m_rows)
        if (row.globalId == global || (row.collected && row.creatorGlobalId == global)) {
            if (row.customCover)
                return row.cover;
            if (!row.collected && row.sourceCode == 1 && m_autoCovers.contains(row.cloudId.value))
                return m_autoCovers.value(row.cloudId.value).url;
            break;
        }
    return fallback;
}
QJsonObject UserPlaylistModel::metadataFor(const QString &global) const
{
    if (global.isEmpty() || m_sessionRevision != m_api.sessionRevision())
        return {};
    for (const auto &row : m_rows) {
        if (row.sourceCode == 2 || (row.kind != "owned" && row.kind != "collected") ||
            (row.collected ? row.creatorGlobalId : row.globalId) != global)
            continue;
        QJsonObject result;
        if (!row.title.isEmpty())
            result["title"] = row.title;
        if (row.descriptionPresent)
            result["description"] = row.description;
        if (row.tagsPresent)
            result["tags"] = row.tags;
        if (row.customCover)
            result["cover_url"] = row.cover;
        return result;
    }
    return {};
}
void UserPlaylistModel::publishAutoCover(ApiClient::CloudPlaylistId id, quint64 revision,
                                         const QString &cover, quint64 session)
{
    if (m_closed || m_mutating || session != m_api.sessionRevision() ||
        !m_api.currentAccountUserId())
        return;
    if (!cover.isEmpty()) {
        const QUrl url(cover);
        if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http"))
            return;
    }
    for (int row = 0; row < m_rows.size(); ++row) {
        const auto &item = m_rows[row];
        if (item.cloudId.value != id.value)
            continue;
        if (item.customCover || item.collected || item.sourceCode != 1 || item.revision > revision)
            return;
        const auto old = m_autoCovers.constFind(id.value);
        if (old != m_autoCovers.cend() &&
            (old->revision > revision || (old->revision == revision && old->url == cover)))
            return;
        const StoredPlaylistCover entry{id.value, revision, cover};
        m_autoCovers.insert(id.value, entry);
        if (m_storage && m_storageReady)
            m_storage->savePlaylistCover(m_api.currentAccountUserId(), entry);
        const auto global = item.globalId;
        emit dataChanged(index(row), index(row), {CoverUrlRole});
        emit coverChanged(global);
        return;
    }
}
int UserPlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}
int UserPlaylistModel::likedRow() const
{
    // Choose the default-favorite playlist by canonical title and identity.
    // Rows without an account cloud ID are ineligible.
    for (int pass = 0; pass < 4; ++pass)
        for (int row = 0; row < int(m_rows.size()); ++row) {
            if (!cloudIdAt(row))
                continue;
            const auto i = index(row);
            const auto title = data(i, TitleRole).toString().trimmed();
            const auto kind = data(i, DefaultKindRole);
            if ((pass == 0 && title == QStringLiteral("我喜欢的音乐")) ||
                (pass == 1 && title.contains(QStringLiteral("喜欢"))) ||
                (pass == 2 && kind.isValid() && !kind.isNull() && kind.toUInt() == 1) ||
                (pass == 3 && title == QStringLiteral("默认收藏")))
                return row;
        }
    return -1;
}
QVariant UserPlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 ||
        index.row() >= m_rows.size())
        return {};
    const auto &v = m_rows[index.row()];
    switch (role) {
    case CloudListIdRole:
        return QVariant::fromValue(v.cloudId.value);
    case GlobalIdRole:
        return v.globalId;
    case CreatorGlobalIdRole:
        return v.creatorGlobalId;
    case Qt::DisplayRole:
    case TitleRole:
        return v.title;
    case DescriptionRole:
        return v.description;
    case CoverUrlRole:
        return v.customCover || v.collected || v.sourceCode != 1 ||
                       !m_autoCovers.contains(v.cloudId.value)
                   ? v.cover
                   : m_autoCovers.value(v.cloudId.value).url;
    case TagsRole:
        return v.tags;
    case SongCountRole:
        return QVariant::fromValue(v.songCount);
    case KindRole:
        return v.kind;
    case EditableRole:
        return v.editable;
    case PrivateRole:
        return v.isPrivate;
    case RevisionRole:
        return QVariant::fromValue(v.revision);
    case SortIndexRole:
        return v.sortIndex;
    case CreatorIdRole:
        return QVariant::fromValue(v.creatorId);
    case CreatorNameRole:
        return v.creatorName;
    case DefaultKindRole:
        return v.defaultKind ? QVariant::fromValue(*v.defaultKind) : QVariant();
    case SourceCodeRole:
        return v.sourceCode;
    case CreatorListIdRole:
        return QVariant::fromValue(v.creatorListId);
    default:
        return {};
    }
}
QHash<int, QByteArray> UserPlaylistModel::roleNames() const
{
    return {{CloudListIdRole, "cloudListId"},
            {GlobalIdRole, "globalId"},
            {TitleRole, "title"},
            {DescriptionRole, "description"},
            {CoverUrlRole, "coverUrl"},
            {SongCountRole, "songCount"},
            {KindRole, "kind"},
            {EditableRole, "editable"},
            {PrivateRole, "isPrivate"},
            {RevisionRole, "revision"},
            {SortIndexRole, "sortIndex"},
            {CreatorIdRole, "creatorId"},
            {CreatorNameRole, "creatorName"},
            {DefaultKindRole, "defaultKind"},
            {TagsRole, "tags"},
            {SourceCodeRole, "sourceCode"},
            {CreatorGlobalIdRole, "creatorGlobalId"},
            {CreatorListIdRole, "creatorListId"}};
}
std::optional<ApiClient::CloudPlaylistId> UserPlaylistModel::cloudIdAt(int row) const
{
    if (m_closed || m_mutating || m_sessionRevision != m_api.sessionRevision() || row < 0 ||
        row >= m_rows.size())
        return std::nullopt;
    const auto &item = m_rows[row];
    if (!item.cloudId.value || item.kind != QStringLiteral("owned") || item.sourceCode != 1 ||
        item.creatorId != m_api.currentAccountUserId())
        return std::nullopt;
    return m_rows[row].cloudId;
}
std::optional<ApiClient::CloudPlaylistId> UserPlaylistModel::libraryIdAt(int row) const
{
    if (m_closed || m_mutating || m_sessionRevision != m_api.sessionRevision() || row < 0 ||
        row >= m_rows.size())
        return {};
    return m_rows[row].cloudId.value ? std::optional(m_rows[row].cloudId) : std::nullopt;
}
quint64 UserPlaylistModel::requestTracksAt(int row, quint32 page, quint32 pageSize)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || m_mutating || !m_api.currentAccountUserId() ||
        m_sessionRevision != m_api.sessionRevision() || row < 0 || row >= m_rows.size() ||
        m_rows[row].sourceCode != 1)
        return 0;
    if (const auto cloud = cloudIdAt(row))
        return m_api.cloudPlaylistTracks(*cloud, page, pageSize);
    return m_api.publicPlaylistTracks({m_rows[row].creatorGlobalId}, page, pageSize);
}
bool UserPlaylistModel::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && !m_closed && !m_mutating && !m_exhausted && !m_request &&
           m_api.currentAccountUserId() &&
           (m_state == State::Idle || m_state == State::Ready || m_state == State::Error);
}
void UserPlaylistModel::fetchMore(const QModelIndex &parent)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!canFetchMore(parent))
        return;
    m_sessionRevision = m_api.sessionRevision();
    m_request = m_api.userPlaylists(m_page + 1, PageSize);
    if (!m_request) {
        m_state = State::Error;
        m_error = tr("无法加载歌单，请重试");
        emit changed();
        return;
    }
    m_error.clear();
    m_state = State::Loading;
    emit changed();
}
void UserPlaylistModel::resetState()
{
    if (m_coverAccount != m_api.currentAccountUserId()) {
        m_autoCovers.clear();
        m_coverAccount = m_api.currentAccountUserId();
        if (m_storage && m_storageReady && m_coverAccount)
            m_storage->loadPlaylistCovers(m_api.sessionRevision(), m_coverAccount);
    }
    if (m_request)
        m_api.cancel(std::exchange(m_request, 0));
    m_sessionRevision = 0;
    if (m_mutating) {
        m_resetPending = true;
        return;
    }
    m_mutating = true;
    beginResetModel();
    m_rows.clear();
    m_page = 0;
    m_total = 0;
    m_listRevision = 0;
    m_exhausted = false;
    m_error.clear();
    m_state = m_api.currentAccountUserId() && !m_closed ? State::Idle : State::SignedOut;
    endResetModel();
    m_mutating = false;
    finishMutation();
    emit changed();
}
void UserPlaylistModel::finishMutation()
{
    if (std::exchange(m_resetPending, false))
        resetState();
    if (std::exchange(m_reloadPending, false))
        reload();
}
void UserPlaylistModel::reload()
{
    if (m_mutating) {
        m_reloadPending = true;
        return;
    }
    resetState();
    fetchMore({});
}
void UserPlaylistModel::succeed(quint64 id, const QByteArray &payload)
{
    if (!m_request || id != m_request || m_closed || m_sessionRevision != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto document = QJsonDocument::fromJson(payload);
    const auto object = document.object();
    const auto page = object.value("page").toObject();
    // Rust already decodes the domain response. Do not duplicate the schema
    // or constrain pagination to a recorded response shape.
    const auto total = quint64(qMax<qint64>(0, page.value("total").toInteger()));
    const auto revision = quint64(qMax<qint64>(0, object.value("total_revision").toInteger()));
    const auto number = m_page + 1;
    // Never append pages from different cloud revisions. Retain the old complete
    // prefix for display and require an explicit fresh reload.
    if (m_page && (revision != m_listRevision)) {
        m_state = State::RestartRequired;
        m_error = tr("歌单已更新，请重新加载");
        emit changed();
        return;
    }
    QList<Row> rows;
    for (const auto &value : page.value("items").toArray()) {
        const auto item = value.toObject();
        Row row;
        const auto kind = item.value("kind").toString();
        row.cloudId.value = quint64(qMax<qint64>(0, item.value("cloud_list_id").toInteger()));
        // A model index identifies the row; incomplete or repeated provider IDs
        // must not discard its title, provenance or other available facts.
        row.songCount = quint64(qMax<qint64>(0, item.value("song_count").toInteger()));
        row.revision = quint64(qMax<qint64>(0, item.value("revision").toInteger()));
        row.creatorId = quint64(qMax<qint64>(0, item.value("creator_id").toInteger()));
        row.creatorListId = quint64(qMax<qint64>(0, item.value("creator_list_id").toInteger()));
        const auto source = item.value("source_code").toInteger();
        if (!item.value("default_kind").isNull())
            row.defaultKind = quint32(item.value("default_kind").toInteger());
        row.globalId = item.value("global_id").toString();
        row.creatorGlobalId = item.value("creator_global_id").toString();
        row.title = item.value("title").toString();
        row.description = item.value("description").toString();
        row.tags = item.value("tags").toString();
        row.descriptionPresent = item.value("description_present").toBool();
        row.tagsPresent = item.value("tags_present").toBool();
        row.cover = item.value("cover_url").toString();
        row.creatorName = item.value("creator_name").toString();
        row.sortIndex = item.value("sort_index").toInteger();
        row.editable = kind == QStringLiteral("owned") && item.value("editable").toBool();
        row.isPrivate = item.value("private").toBool();
        row.kind = kind;
        row.collected = kind == QStringLiteral("collected");
        row.customCover = item.value("custom_cover").toBool();
        row.sourceCode = quint32(source);
        rows.append(std::move(row));
    }
    const auto oldRevision = m_sessionRevision;
    m_mutating = true;
    if (!rows.isEmpty()) {
        beginInsertRows({}, int(m_rows.size()), int(m_rows.size() + rows.size() - 1));
        m_rows.append(rows);
        endInsertRows();
    }
    m_mutating = false;
    if (m_resetPending || oldRevision != m_api.sessionRevision()) {
        finishMutation();
        return;
    }
    m_page = quint32(number);
    m_listRevision = revision;
    m_total = total;
    m_exhausted = rows.isEmpty() || (total > 0 && quint64(m_rows.size()) >= total);
    m_state = State::Ready;
    finishMutation();
    emit changed();
}
void UserPlaylistModel::fail(quint64 id, const QByteArray &payload)
{
    if (!m_request || id != m_request || m_closed || m_sessionRevision != m_api.sessionRevision())
        return;
    m_request = 0;
    m_state = State::Error;
    const auto kind = QJsonDocument::fromJson(payload).object().value("kind").toString();
    m_error = kind == QStringLiteral("authentication") ? tr("登录已失效，请重新登录")
                                                       : tr("歌单加载失败，请重试");
    emit changed();
}
} // namespace EchoMusic
