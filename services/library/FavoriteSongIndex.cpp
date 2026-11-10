#include "FavoriteSongIndex.h"
#include "PlaylistMutationService.h"
#include "UserPlaylistModel.h"
#include "SongMetadataService.h"
#include <QJsonDocument>
#include <algorithm>
#include <utility>

namespace EchoMusic
{
FavoriteSongIndex::FavoriteSongIndex(ApiClient &api, UserPlaylistModel &library)
    : QObject(&library), m_api(api), m_library(library), m_session(api.sessionRevision())
{
    connect(&api, &ApiClient::sessionChanged, this, &FavoriteSongIndex::reset);
    connect(&api, &ApiClient::closed, this, [this] {
        m_closed = true;
        reset();
    });
    connect(&api, &ApiClient::succeeded, this, &FavoriteSongIndex::accept);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &bytes) {
        if (id && id == m_request && m_session == m_api.sessionRevision()) {
            m_request = 0;
            fail(ApiClient::errorMessage(bytes, tr("收藏歌曲加载失败")));
        }
    });
    connect(&library, &UserPlaylistModel::changed, this, [this] {
        if (!m_waitingLibrary)
            return;
        if (m_library.cloudIdAt(m_library.likedRow())) {
            m_waitingLibrary = false;
            ensureLoaded();
        } else if (m_library.state() != UserPlaylistModel::State::Loading) {
            if (m_library.canFetchMore({}))
                m_library.fetchMore({});
            else
                fail(m_library.errorMessage().isEmpty() ? tr("未找到收藏歌单，请刷新歌单后重试")
                                                        : m_library.errorMessage());
        }
    });
}
FavoriteSongIndex::~FavoriteSongIndex()
{
    if (m_request)
        m_api.cancel(m_request);
}
void FavoriteSongIndex::attachMutations(PlaylistMutationService &writer)
{
    if (m_mutations == &writer)
        return;
    Q_ASSERT(!m_mutations);
    m_mutations = &writer;
    connect(&writer, &PlaylistMutationService::trackPageReconciled, this,
            &FavoriteSongIndex::acceptReadback);
    connect(&writer, &PlaylistMutationService::trackFilesRemoved, this,
            &FavoriteSongIndex::removeFiles);
    connect(&writer, &QObject::destroyed, this, [this] { m_mutations = nullptr; });
}
FavoriteSongIndex::Identity FavoriteSongIndex::identity(const QJsonObject &song)
{
    const auto id = song.value("id").toString().trimmed();
    return {id, song.value("hash").toString(), id.toULongLong()};
}
QStringList FavoriteSongIndex::keys(const Identity &song)
{
    QStringList result;
    if (song.songId)
        result.append("mx:" + QString::number(song.songId));
    const auto hash = song.hash.trimmed().toLower();
    if (!hash.isEmpty())
        result.append("hash:" + hash);
    const auto id = song.id.trimmed();
    if (result.isEmpty() && !id.isEmpty() && id != "0")
        result.append("id:" + id);
    return result;
}
QStringList FavoriteSongIndex::rowKeys(const QJsonObject &row) const
{
    const auto song = identity(row.value("song").toObject());
    auto result = keys(song);
    for (auto id : m_aliases.value(song.hash.trimmed().toLower()))
        result.append("mx:" + QString::number(id));
    return result;
}
bool FavoriteSongIndex::intersects(const QStringList &keys, const QSet<QString> &other)
{
    return std::any_of(keys.cbegin(), keys.cend(),
                       [&](const auto &key) { return other.contains(key); });
}
bool FavoriteSongIndex::contains(const Identity &song) const
{
    for (const auto &key : keys(song))
        if (m_index.contains(key))
            return true;
    return false;
}
bool FavoriteSongIndex::committedContains(const Identity &song) const
{
    for (const auto &key : keys(song))
        if (m_committedIndex.contains(key))
            return true;
    return false;
}
quint64 FavoriteSongIndex::fileId(const Identity &song) const
{
    for (const auto &key : keys(song)) {
        const auto it = m_index.constFind(key);
        if (it != m_index.cend()) {
            const auto file = m_rows[*it].toObject().value("file_id").toInteger();
            if (file > 0)
                return quint64(file);
        }
    }
    return 0;
}
void FavoriteSongIndex::enrichIdentity(const Identity &song)
{
    const auto hash = song.hash.trimmed().toLower();
    if (!song.songId || hash.isEmpty() || !m_index.contains("hash:" + hash) ||
        m_aliases.value(hash).contains(song.songId))
        return;
    m_aliases[hash].insert(song.songId);
    publish();
}
void FavoriteSongIndex::reset()
{
    const auto request = std::exchange(m_request, 0);
    m_session = m_api.sessionRevision();
    ++m_generation;
    m_listId = m_seen = 0;
    m_page = 1;
    m_version.reset();
    m_snapshotVersion.reset();
    m_base = m_rows = m_readRows = {};
    m_index.clear();
    m_committedIndex.clear();
    m_aliases.clear();
    m_changes.clear();
    m_pending.reset();
    m_complete = m_hadComplete = m_hasSnapshot = m_waitingLibrary = m_reading = false;
    m_consistent = true;
    m_error.clear();
    if (request)
        m_api.cancel(request);
    emit snapshotChanged();
    emit changed();
}
void FavoriteSongIndex::start(quint64 listId)
{
    if (m_request)
        m_api.cancel(std::exchange(m_request, 0));
    if (m_listId && m_listId != listId)
        reset();
    m_listId = listId;
    m_session = m_api.sessionRevision();
    ++m_generation;
    m_hadComplete = m_complete || m_hadComplete;
    m_complete = false;
    m_reading = true;
    m_page = 1;
    m_seen = 0;
    m_readRows = {};
    m_version.reset();
    m_consistent = true;
    m_waitingLibrary = false;
    m_error.clear();
}
void FavoriteSongIndex::ensureLoaded(bool force)
{
    if (m_closed || !m_api.currentAccountUserId() || (!force && (m_reading || m_complete)))
        return;
    const auto list = m_library.cloudIdAt(m_library.likedRow());
    if (!list) {
        m_waitingLibrary = true;
        if (m_library.state() == UserPlaylistModel::State::Idle)
            m_library.reload();
        else if (m_library.canFetchMore({}))
            m_library.fetchMore({});
        else if (m_library.state() != UserPlaylistModel::State::Loading)
            fail(tr("未找到收藏歌单，请刷新歌单后重试"));
        emit changed();
        return;
    }
    start(list->value);
    readPage();
}
void FavoriteSongIndex::refresh()
{
    m_changes.clear();
    ensureLoaded(true);
}
void FavoriteSongIndex::readPage()
{
    m_request = m_api.cloudPlaylistTracks({m_listId}, m_page, 300);
    if (!m_request)
        fail(tr("无法加载收藏歌曲"));
    emit changed();
}
void FavoriteSongIndex::accept(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request || m_session != m_api.sessionRevision())
        return;
    m_request = 0;
    acceptPage(QJsonDocument::fromJson(bytes).object());
}
void FavoriteSongIndex::acceptReadback(const QJsonObject &root, quint64 session)
{
    const auto list = m_library.cloudIdAt(m_library.likedRow());
    if (session != m_api.sessionRevision() || !list ||
        quint64(root.value("cloud_list_id").toInteger()) != list->value)
        return;
    const auto version = root.value("revision").toInteger(-1);
    if (version >= 0 && m_snapshotVersion && quint64(version) < *m_snapshotVersion)
        return;
    // Retain successful changes while a replica/read started before the ACK
    // catches up. This read is synchronization, never a second write verdict.
    start(list->value);
    acceptPage(root);
}
void FavoriteSongIndex::acceptPage(const QJsonObject &root)
{
    const auto version = root.value("revision").toInteger(-1);
    if (version >= 0 && m_version && *m_version != quint64(version)) {
        fail(tr("收藏列表已改变，请刷新后重试"));
        return;
    }
    if (version < 0 || (m_version && *m_version != quint64(version)))
        m_consistent = false;
    if (version >= 0 && !m_version)
        m_version = quint64(version);
    const auto page = root.value("page").toObject();
    const auto items = page.value("items").toArray();
    for (const auto &row : items)
        m_readRows.append(row);
    m_seen += quint64(items.size());
    const auto total = page.value("total").toInteger(-1);
    const bool more =
        !items.isEmpty() && (total >= 0 ? m_seen < quint64(total) : items.size() >= 300);
    // Bound broken pagination without using deduplicated membership as progress.
    // Duplicate/hashless pages still advance by their original row count.
    if (more && m_page >= 50) {
        fail(tr("收藏歌曲尚未加载完整，请刷新后重试"));
        return;
    }
    if (!m_hadComplete || !more) {
        QList<QJsonObject> ordered;
        for (const auto &row : m_readRows)
            ordered.append(row.toObject());
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
            const auto left = a.value("sort_index"), right = b.value("sort_index");
            return left.isDouble() && (!right.isDouble() || left.toInteger() < right.toInteger());
        });
        QJsonArray rows;
        QSet<QString> seen;
        for (const auto &row : ordered) {
            const auto identities = keys(identity(row.value("song").toObject()));
            // EchoMusic deduplicates the display snapshot by its primary song
            // identity. Raw received counts above still drive pagination.
            const auto key = identities.value(0);
            if (!key.isEmpty() && seen.contains(key))
                continue;
            if (!key.isEmpty())
                seen.insert(key);
            rows.append(row);
        }
        if (!more && m_consistent) {
            // Retire the journal once a complete server snapshot reflects it;
            // stale replicas retain the corresponding delta until they catch up.
            QHash<QString, quint64> filesByKey;
            QSet<quint64> files;
            for (const auto &value : rows) {
                const auto row = value.toObject();
                const auto file = quint64(qMax<qint64>(0, row.value("file_id").toInteger()));
                if (file)
                    files.insert(file);
                for (const auto &key : rowKeys(row))
                    filesByKey.insert(key, file);
            }
            for (auto it = m_changes.begin(); it != m_changes.end();) {
                bool confirmed = true;
                for (const auto &key : it->keys)
                    confirmed &= !filesByKey.contains(key);
                for (auto file : it->files)
                    confirmed &= !files.contains(file);
                for (const auto &value : it->adds) {
                    const auto added = value.toObject();
                    const auto file = added.value("file_id").toInteger();
                    bool found = false;
                    for (const auto &key : rowKeys(added))
                        found |= filesByKey.contains(key) && filesByKey.value(key) > 0 &&
                                 (file <= 0 || filesByKey.value(key) == quint64(file));
                    confirmed &= found;
                }
                if (confirmed)
                    it = m_changes.erase(it);
                else
                    ++it;
            }
        }
        m_base = applyChanges(rows);
        m_snapshotVersion = m_version;
        m_hasSnapshot = true;
    }
    m_complete = !more;
    m_reading = more;
    m_error.clear();
    ++m_page;
    if (!more)
        m_readRows = {};
    publish();
    if (more)
        readPage();
}
void FavoriteSongIndex::fail(const QString &message)
{
    m_waitingLibrary = false;
    m_reading = false;
    m_complete = m_hadComplete;
    m_readRows = {};
    m_error = message;
    publish();
}
QJsonArray FavoriteSongIndex::applyChanges(QJsonArray rows) const
{
    QSet<QString> removedKeys;
    QSet<quint64> removedFiles;
    QJsonArray adds;
    QList<Change> changes = m_changes.values();
    std::stable_sort(changes.begin(), changes.end(), [](const auto &left, const auto &right) {
        return left.sequence > right.sequence;
    });
    for (const auto &change : changes) {
        removedKeys.unite(change.keys);
        removedFiles.unite(change.files);
        for (const auto &entry : change.adds)
            adds.append(entry);
    }
    QJsonArray result;
    QHash<QString, qsizetype> lookup;
    for (const auto &value : rows) {
        const auto row = value.toObject();
        const auto keys = rowKeys(row);
        if (removedFiles.contains(quint64(row.value("file_id").toInteger())) ||
            intersects(keys, removedKeys))
            continue;
        for (const auto &key : keys)
            lookup.insert(key, result.size());
        result.append(row);
    }
    const auto previousSize = result.size();
    for (const auto &value : adds) {
        auto row = value.toObject();
        const auto keys = rowKeys(row);
        std::optional<qsizetype> found;
        for (const auto &key : keys)
            if (lookup.contains(key)) {
                found = lookup.value(key);
                break;
            }
        if (found) {
            // Preserve original playlist identities and metadata on idempotent adds.
            auto existing = result[*found].toObject();
            if (row.value("file_id").toInteger() > 0)
                existing.insert("file_id", row.value("file_id"));
            result[*found] = existing;
        } else {
            for (const auto &key : keys)
                lookup.insert(key, result.size());
            result.append(row);
        }
    }
    // New likes are at the front until the canonical server sort is read.
    if (result.size() > previousSize) {
        QJsonArray reordered;
        for (qsizetype i = previousSize; i < result.size(); ++i)
            reordered.append(result[i]);
        for (qsizetype i = 0; i < previousSize; ++i)
            reordered.append(result[i]);
        return reordered;
    }
    return result;
}
void FavoriteSongIndex::publish()
{
    m_rows = m_base;
    if (m_pending) {
        QJsonArray rows;
        for (const auto &value : m_rows)
            if (!intersects(rowKeys(value.toObject()), m_pending->keys))
                rows.append(value);
        for (const auto &row : m_pending->adds)
            rows.prepend(row);
        m_rows = rows;
    }
    const auto indexRows = [this](const QJsonArray &rows, auto &index) {
        index.clear();
        for (qsizetype i = 0; i < rows.size(); ++i)
            for (const auto &key : rowKeys(rows[i].toObject()))
                if (!index.contains(key) ||
                    rows[index.value(key)].toObject().value("file_id").toInteger() <= 0)
                    index.insert(key, i);
    };
    indexRows(m_base, m_committedIndex);
    if (m_pending)
        indexRows(m_rows, m_index);
    else
        m_index = m_committedIndex;
    QSet<QString> hashes;
    const auto retainHashes = [&](const QJsonArray &rows) {
        for (const auto &row : rows)
            hashes.insert(
                identity(row.toObject().value("song").toObject()).hash.trimmed().toLower());
    };
    retainHashes(m_base);
    retainHashes(m_readRows);
    if (m_pending)
        retainHashes(m_pending->adds);
    for (auto it = m_aliases.begin(); it != m_aliases.end();)
        if (!hashes.contains(it.key()))
            it = m_aliases.erase(it);
        else
            ++it;
    emit snapshotChanged();
    emit changed();
}
void FavoriteSongIndex::beginPending(const Identity &song, const QJsonObject &domain, bool liked)
{
    Change change;
    const auto identities = keys(song);
    change.keys = QSet<QString>(identities.begin(), identities.end());
    if (liked) {
        auto presentation = domain;
        const auto metadata = presentation.take("metadata");
        change.adds.append(QJsonObject{{"song", presentation}, {"metadata", metadata}});
        if (song.songId && !song.hash.trimmed().isEmpty())
            m_aliases[song.hash.trimmed().toLower()].insert(song.songId);
    }
    m_pending = change;
    publish();
}
void FavoriteSongIndex::finishPending()
{
    if (!m_pending)
        return;
    m_pending.reset();
    publish();
}
void FavoriteSongIndex::add(const QJsonArray &entries, const QList<Identity> &identities)
{
    for (const auto &song : identities)
        if (song.songId && !song.hash.trimmed().isEmpty())
            m_aliases[song.hash.trimmed().toLower()].insert(song.songId);
    for (const auto &value : entries) {
        auto row = value.toObject();
        auto song = row.value("song").toObject();
        if (!row.contains("metadata") && song.contains("metadata")) {
            row.insert("metadata", song.take("metadata"));
            row.insert("song", song);
        }
        const auto keys = rowKeys(row);
        if (keys.isEmpty())
            continue;
        const QSet<QString> keySet(keys.begin(), keys.end());
        for (auto it = m_changes.begin(); it != m_changes.end();) {
            if (intersects(keys, it->keys) ||
                it->files.contains(quint64(row.value("file_id").toInteger())))
                it = m_changes.erase(it);
            else
                ++it;
        }
        for (const auto &key : keySet)
            m_changes.remove(key);
        m_changes.insert(keys.first(), Change{{row}, {}, {}, ++m_changeSequence});
    }
    m_base = applyChanges(m_base);
    m_hasSnapshot = true;
    publish();
}
void FavoriteSongIndex::removeFiles(quint64 listId, const QList<quint64> &files, quint64 session)
{
    const auto list = m_library.cloudIdAt(m_library.likedRow());
    if (session != m_api.sessionRevision() || !list || listId != list->value)
        return;
    QJsonArray removed;
    const QSet<quint64> fileSet(files.begin(), files.end());
    for (const auto &value : m_base) {
        const auto row = value.toObject();
        if (!fileSet.contains(quint64(row.value("file_id").toInteger())))
            continue;
        removed.append(row);
        const auto keys = rowKeys(row);
        for (auto it = m_changes.begin(); it != m_changes.end();) {
            bool matches = false;
            for (const auto &added : it->adds)
                matches |=
                    intersects(rowKeys(added.toObject()), QSet<QString>(keys.begin(), keys.end()));
            if (matches)
                it = m_changes.erase(it);
            else
                ++it;
        }
        m_changes.insert("file:" + QString::number(row.value("file_id").toInteger()),
                         Change{{},
                                QSet<QString>(keys.begin(), keys.end()),
                                {quint64(row.value("file_id").toInteger())}});
    }
    for (auto file : files)
        if (!m_changes.contains("file:" + QString::number(file)))
            m_changes.insert("file:" + QString::number(file), Change{{}, {}, {file}});
    m_base = applyChanges(m_base);
    publish();
    emit filesRemoved(removed);
}
void FavoriteSongIndex::applyPresentations(const QJsonArray &values)
{
    QHash<QString, QJsonObject> presentations;
    for (const auto &value : values)
        presentations.insert(value.toObject().value("id").toString(), value.toObject());
    const auto update = [&](QJsonArray &rows) {
        for (qsizetype i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toObject();
            auto song = row.value("song").toObject();
            const auto found = presentations.constFind(song.value("id").toString());
            if (found == presentations.cend())
                continue;
            song.insert("metadata", row.value("metadata"));
            auto completed = SongMetadataService::completeSong(song, *found);
            row.insert("metadata", completed.take("metadata"));
            row.insert("song", completed);
            rows[i] = row;
        }
    };
    update(m_base);
    update(m_readRows);
    for (auto &change : m_changes)
        update(change.adds);
    publish();
}
} // namespace EchoMusic
