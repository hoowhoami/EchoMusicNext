#include "LibraryCollectionService.h"
#include <QJsonArray>
namespace EchoMusic
{
LibraryCollectionService::LibraryCollectionService(ApiClient &a, UserPlaylistModel &l,
                                                   PlaylistMutationService &m, QObject *p)
    : QObject(p), m_api(a), m_library(l), m_mutations(m)
{
    connect(&a, &ApiClient::sessionChanged, this, [this] {
        m_pending = false;
        emit changed();
    });
    connect(&l, &UserPlaylistModel::changed, this, [this] {
        if (m_wanted && m_library.state() == UserPlaylistModel::State::Ready &&
            m_library.canFetchMore({}))
            QMetaObject::invokeMethod(this, [this] { ensureLoaded(); }, Qt::QueuedConnection);
        emit changed();
    });
    connect(&m, &PlaylistMutationService::changed, this, [this] {
        if (m_pending && m_mutations.state() != PlaylistMutationService::State::Writing &&
            (m_mutations.state() != PlaylistMutationService::State::Ready ||
             !m_mutations.errorMessage().isEmpty())) {
            m_pending = false;
            emit notice(m_mutations.errorMessage(), true);
        }
        emit changed();
    });
    connect(&m, &PlaylistMutationService::completed, this, [this] {
        if (!m_pending)
            return;
        m_pending = false;
        emit changed();
        emit notice(m_removing ? tr("已取消收藏") : tr("收藏成功"), false);
    });
}
bool LibraryCollectionService::available(bool album, const QJsonObject &d) const
{
    if (!m_api.currentAccountUserId())
        return false;
    if (album)
        return d["id"].toInteger() > 0;
    return d["creator_id"].toInteger() > 0 &&
           quint64(d["creator_id"].toInteger()) != m_api.currentAccountUserId() &&
           d["creator_list_id"].toInteger() > 0;
}
int LibraryCollectionService::find(bool album, const QJsonObject &d) const
{
    // Playlist list IDs are scoped to an account/creator and can collide across
    // unrelated collections. Prefer GID aliases. Only when both sides lack a
    // GID may the complete creator/user + creator/list pair identify a playlist.
    // Deletion still uses the matched catalog row's account-local ID; albums
    // retain their numeric album identity.
    const auto keys = album ? QStringList{"id", "global_id", "creator_list_id", "creator_global_id"}
                            : QStringList{"global_id", "creator_global_id"};
    const auto roles =
        album ? QList<int>{UserPlaylistModel::CloudListIdRole, UserPlaylistModel::GlobalIdRole,
                           UserPlaylistModel::CreatorGlobalIdRole,
                           UserPlaylistModel::CreatorListIdRole}
              : QList<int>{UserPlaylistModel::GlobalIdRole, UserPlaylistModel::CreatorGlobalIdRole};
    QSet<QString> ids;
    for (const auto &key : keys) {
        const auto v = d[key];
        const auto s = v.isString()   ? v.toString()
                       : v.isDouble() ? QString::number(v.toInteger())
                                      : QString();
        if (!s.isEmpty() && s != "0")
            ids.insert(s);
    }
    for (int row = 0; row < m_library.rowCount(); ++row) {
        const auto i = m_library.index(row, 0);
        const auto source = i.data(UserPlaylistModel::SourceCodeRole).toUInt();
        if ((source == 2) != album)
            continue;
        if (!album &&
            i.data(UserPlaylistModel::CreatorIdRole).toULongLong() == m_api.currentAccountUserId())
            continue;
        bool hasIdentity = false;
        for (int role : roles) {
            const auto s = i.data(role).toString();
            if (!s.isEmpty() && s != "0") {
                hasIdentity = true;
                if (ids.contains(s))
                    return row;
            }
        }
        if (!album && ids.isEmpty() && !hasIdentity) {
            const auto creator = d["creator_id"].toInteger();
            const auto list = d["creator_list_id"].toInteger();
            if (creator > 0 && list > 0 &&
                i.data(UserPlaylistModel::CreatorIdRole).toULongLong() == quint64(creator) &&
                i.data(UserPlaylistModel::CreatorListIdRole).toULongLong() == quint64(list))
                return row;
        }
    }
    return -1;
}
bool LibraryCollectionService::collected(bool album, const QJsonObject &d) const
{
    return find(album, d) >= 0;
}
bool LibraryCollectionService::busy() const
{
    return m_pending || m_mutations.state() != PlaylistMutationService::State::Ready ||
           m_library.state() != UserPlaylistModel::State::Ready || m_library.canFetchMore({});
}
void LibraryCollectionService::ensureLoaded()
{
    m_wanted = true;
    if (!m_api.currentAccountUserId())
        return;
    if (m_library.state() == UserPlaylistModel::State::Idle ||
        m_library.state() == UserPlaylistModel::State::Error)
        m_library.reload();
    else if (m_library.state() == UserPlaylistModel::State::Ready && m_library.canFetchMore({}))
        m_library.fetchMore({});
}
void LibraryCollectionService::toggle(bool album, const QJsonObject &d)
{
    if (!available(album, d))
        return;
    if (busy()) {
        ensureLoaded();
        emit notice(tr("请等待收藏目录加载完成后重试"), true);
        return;
    }
    const auto row = find(album, d);
    bool started = false;
    m_removing = row >= 0;
    // Mark before begin(), whose changed signal can synchronously re-enter observers.
    m_pending = true;
    if (row >= 0) {
        const auto id = m_library.libraryIdAt(row);
        started = id && m_mutations.remove(*id);
    } else {
        ApiClient::CollectionTarget t;
        t.title = d["title"].toString();
        t.source = album ? 2 : quint32(d["source_code"].toInt(1));
        t.creatorListId = quint64(d[album ? "id" : "creator_list_id"].toInteger());
        if (album) {
            const auto artists = d["artists"].toArray();
            if (!artists.isEmpty()) {
                const auto id = artists.first().toObject()["id"].toInteger();
                if (id > 0)
                    t.creatorId = quint64(id);
            }
        } else {
            t.creatorId = quint64(d["creator_id"].toInteger());
            t.creatorGlobalId = d["creator_global_id"].toString();
            if (t.creatorGlobalId.isEmpty())
                t.creatorGlobalId = d["global_id"].toString();
        }
        started = m_mutations.collect(t);
    }
    if (!started) {
        m_pending = false;
        emit notice(tr("收藏操作未能开始，请稍后重试"), true);
    }
    emit changed();
}
} // namespace EchoMusic
