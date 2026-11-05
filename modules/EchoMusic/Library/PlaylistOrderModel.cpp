#include "PlaylistOrderModel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QHash>
#include <QStringList>
#include <algorithm>
#include <limits>
#include <utility>

namespace EchoMusic
{
namespace
{
bool unknownTrackTitle(const QString &title)
{
    // Hide missing titles and the unknown or unnamed title pattern.
    if (title.isEmpty())
        return true;
    const QStringList unknown{QStringLiteral("未知歌曲"), QStringLiteral("未命名")};
    for (const auto &token : unknown)
        if (title == token || title.endsWith(QStringLiteral(" - ") + token))
            return true;
    return false;
}
QString stripTransportSuffix(QString title)
{
    // Strip one terminal transport extension from title candidates.
    // Rust display_title already handles .mp3; this covers filename fallback.
    static const QStringList suffixes{
        QStringLiteral(".mp3"),  QStringLiteral(".MP3"), QStringLiteral(".flac"),
        QStringLiteral(".FLAC"), QStringLiteral(".wav"), QStringLiteral(".WAV"),
        QStringLiteral(".aac"),  QStringLiteral(".AAC"), QStringLiteral(".m4a"),
        QStringLiteral(".M4A"),  QStringLiteral(".ogg"), QStringLiteral(".OGG"),
        QStringLiteral(".wma"),  QStringLiteral(".WMA"), QStringLiteral(".ape"),
        QStringLiteral(".APE")};
    for (const auto &suffix : suffixes)
        if (title.endsWith(suffix))
            return title.chopped(suffix.size()).trimmed();
    return title;
}
} // namespace
PlaylistOrderModel::PlaylistOrderModel(ApiClient &api, PlaylistMutationService &mutations,
                                       QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_mutations(mutations)
{
    connect(&api, &ApiClient::succeeded, this, &PlaylistOrderModel::succeed);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &payload) {
        if (id && id == m_request)
            fail(ApiClient::errorMessage(payload, tr("读取排序失败，请稍后重试")));
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        close();
        emit invalidated();
    });
    connect(&mutations, &PlaylistMutationService::completed, this, [this](const QByteArray &) {
        if (!m_saving)
            return;
        m_saving = false;
        m_original = m_order;
        emit changed();
        emit saved();
        emit notice(tr("顺序已保存到酷狗"), false);
    });
    connect(&mutations, &PlaylistMutationService::changed, this, [this] {
        if (m_saving && !m_mutations.errorMessage().isEmpty()) {
            m_saving = false;
            m_error = m_mutations.errorMessage();
            emit changed();
        }
    });
}
PlaylistOrderModel::~PlaylistOrderModel()
{
    if (m_request)
        m_api.cancel(m_request);
}
QVariant PlaylistOrderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_order.size())
        return {};
    const auto &row = m_snapshot[m_order[index.row()]];
    if (role == Title)
        return row.title;
    if (role == Subtitle)
        return row.subtitle;
    return {};
}
void PlaylistOrderModel::close()
{
    if (m_request)
        m_api.cancel(std::exchange(m_request, 0));
    m_open = false;
    m_checking = false;
    m_saving = false;
    beginResetModel();
    m_snapshot.clear();
    m_order.clear();
    m_original.clear();
    endResetModel();
    m_selected = -1;
    m_error.clear();
    emit changed();
}
void PlaylistOrderModel::open(int tab)
{
    if (m_saving)
        return;
    close();
    if (!m_api.currentAccountUserId() || tab < 0 || tab > 1)
        return;
    m_open = true;
    m_tab = tab;
    m_mode = 0;
    m_listId = 0;
    m_trackTitle.clear();
    m_seenFiles.clear();
    m_session = m_api.sessionRevision();
    m_page = 1;
    m_request = m_api.userPlaylists(1, 200);
    if (!m_request)
        fail(tr("无法加载歌单，请重试"));
    emit changed();
}
void PlaylistOrderModel::openTracks(quint64 listId, const QString &title)
{
    if (m_saving)
        return;
    close();
    if (!m_api.currentAccountUserId() || !listId ||
        listId > quint64(std::numeric_limits<qint64>::max()))
        return;
    m_open = true;
    m_mode = 1;
    m_listId = listId;
    m_trackTitle = title;
    m_trackRevision = 0;
    m_seenFiles.clear();
    m_session = m_api.sessionRevision();
    m_page = 1;
    m_request = m_api.cloudPlaylistTracks({m_listId}, 1, 300);
    if (!m_request)
        fail(tr("无法加载歌曲，请重试"));
    emit changed();
}
void PlaylistOrderModel::fail(const QString &message)
{
    m_request = 0;
    m_checking = false;
    m_saving = false;
    m_error = message;
    emit changed();
}
void PlaylistOrderModel::select(int row)
{
    if (row >= 0 && row < m_order.size()) {
        m_selected = row;
        emit changed();
    }
}
void PlaylistOrderModel::move(int from, int to)
{
    if (busy() || from < 0 || from >= m_order.size() || to < 0 || to >= m_order.size() ||
        from == to)
        return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_order.move(from, to);
    endMoveRows();
    m_selected = to;
    emit changed();
}
void PlaylistOrderModel::restore()
{
    if (busy())
        return;
    beginResetModel();
    m_order = m_original;
    endResetModel();
    m_selected = m_order.isEmpty() ? -1 : 0;
    emit changed();
}
void PlaylistOrderModel::save()
{
    if (!m_open || busy() || !dirty() || m_session != m_api.sessionRevision())
        return;
    m_error.clear();
    m_checking = true;
    // Re-read version and count immediately before writing playlist order.
    m_request =
        m_mode == 1 ? m_api.cloudPlaylistTracks({m_listId}, 1, 1) : m_api.userPlaylists(1, 1);
    if (!m_request)
        fail(m_mode == 1 ? tr("无法加载歌曲顺序，请重试") : tr("无法加载歌单顺序，请重试"));
    emit changed();
}
QList<ApiClient::PlaylistPosition> PlaylistOrderModel::positions() const
{
    QList<ApiClient::PlaylistPosition> result;
    int cursor = 0;
    quint32 position = 0;
    for (const auto &slot : m_snapshot) {
        if (slot.type != m_tab)
            continue;
        const auto &row = slot.fixed ? slot : m_snapshot[m_order[cursor++]];
        result.append({{row.id},
                       m_tab ? ApiClient::PlaylistKind::Collected : ApiClient::PlaylistKind::Owned,
                       position++});
    }
    return result;
}
void PlaylistOrderModel::succeed(quint64 id, const QByteArray &payload)
{
    if (!m_open || !id || id != m_request || m_session != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto object = QJsonDocument::fromJson(payload).object();
    if (m_mode == 1) {
        succeedTracks(object);
        return;
    }
    const auto page = object.value("page").toObject();
    const auto version = object.value("total_revision").toInteger(-1);
    const auto total = page.value("total").toInteger(-1);
    if (version < 0) {
        fail(tr("列表数据不完整，请刷新后重试"));
        return;
    }
    if (m_checking) {
        m_checking = false;
        if (quint64(version) != m_version) {
            fail(tr("列表已在其他位置更新，请重新加载后排序"));
            return;
        }
        m_saving = true;
        if (!m_mutations.sortLibrary(m_version, positions()))
            fail(tr("无法保存排序，请稍后重试"));
        emit changed();
        return;
    }
    const auto items = page.value("items").toArray();
    if (m_page > 1 && quint64(version) != m_version) {
        fail(tr("列表在加载期间发生变化，请重新加载"));
        return;
    }
    m_version = quint64(version);
    m_total = quint64(qMax<qint64>(0, total));
    const auto previousCount = m_snapshot.size();
    QHash<quint64, qsizetype> seen;
    for (qsizetype i = 0; i < m_snapshot.size(); ++i)
        seen.insert(m_snapshot[i].id, i);
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto key = item.value("cloud_list_id").toInteger();
        const auto kind = item.value("kind").toString();
        if (key <= 0)
            continue;
        const auto songs = quint64(item.value("song_count").toInteger());
        Row next{quint64(key),
                 kind == "collected" ? 1
                 : kind == "owned"   ? 0
                                     : -1,
                 item.value("sort_index").toInteger(),
                 item.value("title").toString(),
                 tr("%1 首歌曲").arg(songs),
                 songs,
                 item.value("source_code").toInt() == 2 ||
                     (kind == "owned" && item.value("default_kind").toInt() > 0),
                 false};
        const auto found = seen.constFind(quint64(key));
        if (found != seen.cend())
            m_snapshot[*found] = std::move(next);
        else {
            seen.insert(quint64(key), m_snapshot.size());
            m_snapshot.append(std::move(next));
        }
    }
    if (m_snapshot.size() > previousCount &&
        (total >= 0 ? quint64(m_snapshot.size()) < m_total : items.size() >= 200)) {
        m_request = m_api.userPlaylists(++m_page, 200);
        if (!m_request)
            fail(tr("无法继续读取歌单，请重试"));
        emit changed();
        return;
    }
    std::stable_sort(m_snapshot.begin(), m_snapshot.end(), [](const Row &a, const Row &b) {
        return a.type != b.type ? a.type < b.type : a.sort < b.sort;
    });
    beginResetModel();
    for (int i = 0; i < m_snapshot.size(); ++i)
        if (m_snapshot[i].type == m_tab && !m_snapshot[i].fixed)
            m_order.append(i);
    m_original = m_order;
    endResetModel();
    m_selected = m_order.isEmpty() ? -1 : 0;
    emit changed();
}
void PlaylistOrderModel::succeedTracks(const QJsonObject &object)
{
    const auto page = object.value("page").toObject();
    const auto revision = object.value("revision").toInteger(-1);
    if (revision < 0) {
        fail(tr("列表数据不完整，请刷新后重试"));
        return;
    }
    if (m_checking) {
        m_checking = false;
        const auto total = page.value("total").toInteger(-1);
        if (quint64(revision) != m_trackRevision ||
            (total >= 0 && quint64(total) != quint64(m_snapshot.size()))) {
            fail(tr("列表已在其他位置更新，请重新加载后排序"));
            return;
        }
        m_saving = true;
        if (!m_mutations.sortTracks({m_listId}, m_trackRevision, trackPositions()))
            fail(tr("无法保存排序，请稍后重试"));
        emit changed();
        return;
    }
    if (!m_snapshot.isEmpty() && quint64(revision) != m_trackRevision) {
        fail(tr("列表在加载期间发生变化，请重新加载"));
        return;
    }
    m_trackRevision = quint64(revision);
    if (page.value("total").isDouble())
        m_total = quint64(qMax<qint64>(0, page.value("total").toInteger()));
    const auto items = page.value("items").toArray();
    const auto previousCount = m_snapshot.size();
    for (const auto &value : items) {
        const auto item = value.toObject();
        if (!item.value("file_id").isDouble()) {
            // Without a file ID the row cannot be addressed for reordering.
            fail(tr("列表缺少有效的排序标识，请刷新后重试"));
            return;
        }
        const auto fileId = quint64(item.value("file_id").toInteger());
        if (!fileId || m_seenFiles.contains(fileId)) {
            fail(tr("列表包含重复数据，请重新加载"));
            return;
        }
        m_seenFiles.insert(fileId, true);
        const auto song = item.value("song").toObject();
        // Choose the first non-empty title or filename, strip one transport
        // extension, then test the unknown-title pattern. A real filename remains visible.
        QString title = stripTransportSuffix(song.value("title").toString().trimmed());
        if (title.isEmpty())
            title = stripTransportSuffix(
                item.value("metadata").toObject().value("source_file_name").toString().trimmed());
        QStringList artists;
        for (const auto &artist : song.value("artists").toArray())
            if (!artist.toObject().value("name").toString().trimmed().isEmpty())
                artists.append(artist.toObject().value("name").toString().trimmed());
        Row next;
        next.id = fileId;
        next.type = 0;
        next.sort = item.value("sort_index").isDouble() ? item.value("sort_index").toInteger()
                                                        : std::numeric_limits<qint64>::max();
        next.title = title.isEmpty() ? tr("未命名") : title;
        next.subtitle = artists.join(QStringLiteral("、"));
        next.hidden = unknownTrackTitle(title);
        m_snapshot.append(next);
    }
    if (m_snapshot.size() > previousCount &&
        (m_total > 0 ? quint64(m_snapshot.size()) < m_total : items.size() >= 300)) {
        if (m_page >= 500) {
            fail(tr("列表过大，暂时无法调整顺序"));
            return;
        }
        m_request = m_api.cloudPlaylistTracks({m_listId}, ++m_page, 300);
        if (!m_request)
            fail(tr("无法继续读取歌曲，请重试"));
        emit changed();
        return;
    }
    std::stable_sort(m_snapshot.begin(), m_snapshot.end(),
                     [](const Row &a, const Row &b) { return a.sort < b.sort; });
    beginResetModel();
    m_order.clear();
    for (int i = 0; i < m_snapshot.size(); ++i)
        if (!m_snapshot[i].hidden)
            m_order.append(i);
    m_original = m_order;
    endResetModel();
    m_selected = m_order.isEmpty() ? -1 : 0;
    emit changed();
}
QList<ApiClient::PlaylistTrackPosition> PlaylistOrderModel::trackPositions() const
{
    // Hidden rows keep their slots; visible rows consume the edited order in place.
    QList<ApiClient::PlaylistTrackPosition> result;
    result.reserve(m_snapshot.size());
    int cursor = 0;
    quint32 position = 0;
    for (const auto &slot : m_snapshot) {
        const auto &row = slot.hidden ? slot : m_snapshot[m_order[cursor++]];
        result.append({{row.id}, position++});
    }
    return result;
}
} // namespace EchoMusic
