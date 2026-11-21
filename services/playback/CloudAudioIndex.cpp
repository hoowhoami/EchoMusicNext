#include "CloudAudioIndex.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <utility>
#include <QTimer>
#include <limits>
#include <algorithm>
namespace EchoMusic
{
namespace
{
QString idKey(const QString &prefix, const QJsonValue &id)
{
    const auto text = id.toVariant().toString();
    bool ok = false;
    const auto n = text.toULongLong(&ok);
    return ok && n ? prefix + QString::number(n) : QString();
}
QString hashKey(const QString &prefix, const QString &hash)
{
    return hash.isEmpty() ? QString() : prefix + hash.trimmed().toLower();
}
} // namespace
CloudAudioIndex::CloudAudioIndex(ApiClient &api, QObject *parent) : QObject(parent), m_api(api)
{
    connect(&api, &ApiClient::sessionChanged, this, &CloudAudioIndex::invalidate);
    connect(&api, &ApiClient::succeeded, this, &CloudAudioIndex::accept);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &) {
        if (id && id == m_request) {
            m_request = 0;
            m_pending.clear();
            m_error = tr("云盘歌曲加载失败");
            emit changed();
            emit listingSettled();
            emit settled();
        }
    });
}
void CloudAudioIndex::enableSessionStorage(SessionDatabase &database)
{
    if (!m_cache) {
        Q_ASSERT(m_index.isEmpty());
        m_cache = std::make_unique<SongSourceCache>(database, this);
    }
}
QList<SongDatasetStore::CachedRow> CloudAudioIndex::cachedRows() const
{
    QList<QJsonObject> ordered = m_rows;
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        const auto aa = a.value("added_at_seconds").toInteger(-1),
                   bb = b.value("added_at_seconds").toInteger(-1);
        if (aa != bb)
            return aa > bb;
        const auto as = a.value("sort_index").toInteger(), bs = b.value("sort_index").toInteger();
        return as != bs ? as < bs
                        : a.value("_arrival").toInteger() < b.value("_arrival").toInteger();
    });
    QList<SongDatasetStore::CachedRow> result;
    for (const auto &row : ordered)
        result.append(m_records.value(row.value("cache_record").toInteger()));
    return result;
}
QJsonObject CloudAudioIndex::readSource(qint64 record) const
{
    if (!record)
        return {};
    if (const auto hot = m_hot.lookup(record))
        return *hot;
    if (m_reads.contains(record))
        return {};
    auto *owner = const_cast<CloudAudioIndex *>(this);
    const auto cached = m_records.value(record);
    const auto epoch = m_epoch;
    auto *request = new QObject(owner);
    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = step;
    m_reads.insert(record);
    *step = [owner, cached, epoch, record, request, weak] {
        if (epoch != owner->m_epoch) {
            request->deleteLater();
            return;
        }
        if (!owner->m_cache->store().read(
                {cached.generation, {cached.reference}}, 0, request,
                [owner, epoch, record, request, step = weak.lock()](auto result) {
                    request->deleteLater();
                    if (epoch != owner->m_epoch)
                        return;
                    owner->m_reads.remove(record);
                    if (!result.error.isEmpty()) {
                        owner->m_error = result.error;
                        owner->m_hot.insert(record, {}, std::numeric_limits<qint64>::max(), 1);
                        emit owner->changed();
                    } else {
                        const auto source = result.values.first()
                                                .value("metadata")
                                                .toObject()
                                                .value("cloud_source")
                                                .toObject();
                        owner->m_hot.insert(record, source, std::numeric_limits<qint64>::max(),
                                            SongDatasetStore::estimateCost(source));
                    }
                    emit owner->settled();
                }))
            QTimer::singleShot(10, request, [step = weak.lock()] { (*step)(); });
    };
    (*step)();
    return {};
}
void CloudAudioIndex::requestHints(QStringList hashes, QObject *consumer,
                                   std::function<void(QHash<QString, QJsonObject>, QString)> done)
{
    if (!consumer)
        return;
    if (!m_cache || !m_ready) {
        done({}, tr("云盘音源索引尚未就绪"));
        return;
    }
    struct Request
    {
        SongDatasetStore::Snapshot snapshot;
        QStringList hashes;
        QHash<QString, QJsonObject> values;
        qsizetype cursor = 0;
        bool waiting = false;
        std::function<void(QHash<QString, QJsonObject>, QString)> done;
    };
    auto state = std::make_shared<Request>();
    state->done = std::move(done);
    for (const auto &hash : hashes) {
        const auto normalized = hash.trimmed().toLower();
        const auto record = m_lookup.value(hashKey("hash:", normalized));
        if (!record)
            continue;
        const auto cached = m_records.value(record);
        if (!state->snapshot.generation)
            state->snapshot.generation = cached.generation;
        auto reference = cached.reference;
        reference.source = cached.generation;
        state->snapshot.rows.append(reference);
        state->hashes.append(normalized);
    }
    const auto epoch = m_epoch;
    auto *request = new QObject(this);
    connect(consumer, &QObject::destroyed, request, &QObject::deleteLater);
    const QPointer<QObject> lifetime(request), guard(consumer);
    auto step = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = step;
    *step = [this, state, epoch, lifetime, guard, weak] {
        if (!lifetime)
            return;
        if (!guard) {
            lifetime->deleteLater();
            return;
        }
        if (epoch != m_epoch) {
            lifetime->deleteLater();
            state->done({}, tr("云盘音源索引已更新"));
            return;
        }
        if (state->waiting)
            return;
        if (state->cursor == state->snapshot.rows.size()) {
            lifetime->deleteLater();
            state->done(std::move(state->values), {});
            return;
        }
        state->waiting = true;
        const auto first = state->cursor;
        if (!m_cache->store().read(
                state->snapshot, first, lifetime,
                [this, state, epoch, lifetime, guard, first, step = weak.lock()](auto result) {
                    if (!lifetime || !guard)
                        return;
                    state->waiting = false;
                    if (epoch != m_epoch) {
                        lifetime->deleteLater();
                        state->done({}, tr("云盘音源索引已更新"));
                        return;
                    }
                    if (!result.error.isEmpty()) {
                        lifetime->deleteLater();
                        state->done({}, result.error);
                        return;
                    }
                    for (int i = 0; i < result.values.size(); ++i)
                        state->values.insert(state->hashes[first + i], result.values[i]
                                                                           .value("metadata")
                                                                           .toObject()
                                                                           .value("cloud_source")
                                                                           .toObject());
                    state->cursor = result.next;
                    (*step)();
                }))
            state->waiting = false;
    };
    connect(&m_cache->database(), &SessionDatabase::capacityAvailable, request,
            [step] { (*step)(); });
    QTimer::singleShot(0, request, [step] { (*step)(); });
}
void CloudAudioIndex::acceptCachedPage(const QJsonObject &root)
{
    const auto page = root.value("page").toObject();
    const auto incoming = page.value("items").toArray();
    QList<QJsonObject> accepted;
    QSet<quint64> files;
    QSet<QString> used;
    for (const auto &row : m_rows) {
        files.insert(quint64(row.value("file_id").toInteger()));
        used.insert(row.value("song").toObject().value("id").toString());
    }
    QJsonArray songs;
    for (const auto &value : incoming) {
        auto row = value.toObject();
        const auto file = quint64(row.value("file_id").toInteger());
        if (file && files.contains(file))
            continue;
        files.insert(file);
        row.insert("_arrival", qint64(++m_arrival));
        auto song = row.value("song").toObject();
        const auto originalId = song.value("id").toString();
        const auto hash = song.value("hash").toString().trimmed().toLower();
        QString id = originalId.toULongLong() ? originalId
                     : hash.isEmpty()         ? QString()
                                              : "cloud:" + hash;
        if (id.isEmpty() || used.contains(id))
            id = QString("cloud-file:%1").arg(file ? file : m_arrival);
        used.insert(id);
        auto metadata = row.value("metadata").toObject();
        metadata.insert("cloud_source", fromRow(row));
        metadata.insert("source_kind", "cloud");
        song.insert("metadata", metadata);
        song.insert("id", id);
        auto sourceRow = row;
        sourceRow.remove("song");
        sourceRow.remove("metadata");
        song.insert("_source_row", sourceRow);
        songs.append(song);
        accepted.append(row);
    }
    const auto epoch = m_epoch;
    m_writing = true;
    m_cache->cache(
        std::move(songs), this,
        [this, epoch, accepted, root, page, incoming](auto records, auto error) {
            if (epoch != m_epoch)
                return;
            m_writing = false;
            if (!error.isEmpty()) {
                m_error = error;
                emit changed();
                emit listingSettled();
                emit settled();
                return;
            }
            for (int i = 0; i < accepted.size(); ++i) {
                const auto &row = accepted[i];
                const auto &record = records[i];
                const auto source = fromRow(row);
                const QStringList keys{idKey("song:", source.value("song_id")),
                                       idKey("audio:", source.value("audio_id")),
                                       hashKey("std:", source.value("hash_std").toString()),
                                       hashKey("hash:", source.value("hash").toString())};
                if (!source.value("hash").toString().isEmpty())
                    for (const auto &key : keys)
                        if (!key.isEmpty() && !m_lookup.contains(key))
                            m_lookup.insert(key, record.reference.record);
                const auto song = row.value("song").toObject();
                m_rows.append({{"song", QJsonObject{{"id", record.reference.id},
                                                    {"hash", song.value("hash")}}},
                               {"file_id", row.value("file_id")},
                               {"audio_id", row.value("audio_id")},
                               {"catalog_id", song.value("id")},
                               {"sort_index", row.value("sort_index")},
                               {"added_at_seconds", row.value("added_at_seconds")},
                               {"_arrival", row.value("_arrival")},
                               {"cache_record", record.reference.record}});
                m_records.insert(record.reference.record, record);
            }
            m_loaded += accepted.size();
            m_scanned += incoming.size();
            if (page.value("total").isDouble())
                m_total = quint64(qMax<qint64>(0, page.value("total").toInteger()));
            if (!root.value("counters").toObject().isEmpty())
                m_counters = root.value("counters").toObject();
            m_error.clear();
            const bool more =
                !incoming.isEmpty() &&
                (page.value("total").isDouble() ? m_scanned < m_total : incoming.size() == 100);
            m_ready = !more;
            emit changed();
            if (more)
                next();
            else {
                emit listingSettled();
                emit settled();
            }
        });
}
void CloudAudioIndex::invalidate()
{
    const auto id = std::exchange(m_request, 0);
    if (id)
        m_api.cancel(id);
    ++m_epoch;
    m_writing = false;
    m_reads.clear();
    m_hot.clear();
    m_lookup.clear();
    m_records.clear();
    m_rows.clear();
    m_counters = {};
    m_total = m_scanned = 0;
    m_arrival = 0;
    m_error.clear();
    m_ready = false;
    m_index.clear();
    m_pending.clear();
    m_page = 0;
    m_loaded = 0;
    m_revision = m_api.sessionRevision();
    emit changed();
    emit settled();
}
void CloudAudioIndex::ensure()
{
    if (m_ready || m_request || m_writing)
        return;
    m_revision = m_api.sessionRevision();
    if (!m_api.currentAccountUserId()) {
        m_ready = true;
        emit listingSettled();
        emit settled();
        return;
    }
    m_pending.clear();
    m_page = 0;
    m_loaded = 0;
    next();
}
void CloudAudioIndex::next()
{
    m_request = m_api.userCloud(++m_page, 100);
    emit changed();
    if (!m_request) {
        m_pending.clear();
        emit settled();
    }
}
QJsonObject CloudAudioIndex::fromRow(const QJsonObject &row)
{
    const auto song = row.value("song").toObject();
    return {{"hash", song.value("hash")},
            {"hash_std", row.value("hash_std")},
            {"size_bytes", row.value("size_bytes")},
            {"song_id", song.value("id").toVariant().toLongLong()},
            {"audio_id", row.value("audio_id")},
            {"file_id", row.value("file_id")},
            {"name", row.value("source_file_name")},
            // Presentation facts for surfaces whose provider snapshot stripped
            // them (cloud playlist entries keep only title + hash).
            {"title", song.value("title")},
            {"artists", song.value("artists")},
            {"duration_ms", song.value("duration_ms")},
            {"album", song.value("album_title")},
            {"cover_url", song.value("cover_url")}};
}
QJsonObject CloudAudioIndex::byHash(const QString &hash) const
{
    if (!m_ready)
        return {};
    return m_cache ? readSource(m_lookup.value(hashKey("hash:", hash)))
                   : m_index.value(hashKey("hash:", hash));
}
void CloudAudioIndex::accept(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (m_cache) {
        acceptCachedPage(root);
        return;
    }
    const auto page = root.value("page").toObject();
    const auto rows = page.value("items").toArray();
    for (const auto &value : rows) {
        const auto s = fromRow(value.toObject());
        if (s.value("hash").toString().isEmpty())
            continue;
        const QStringList keys{idKey("song:", s.value("song_id")),
                               idKey("audio:", s.value("audio_id")),
                               hashKey("std:", s.value("hash_std").toString()),
                               hashKey("hash:", s.value("hash").toString())};
        for (const auto &key : keys)
            if (!key.isEmpty() && !m_pending.contains(key))
                m_pending.insert(key, s);
    }
    m_loaded += rows.size();
    const auto total = page.value("total");
    if (!rows.isEmpty() &&
        (total.isDouble() ? m_loaded < quint64(total.toInteger()) : rows.size() == 100)) {
        next();
        return;
    }
    m_index = std::move(m_pending);
    m_ready = true;
    emit listingSettled();
    emit settled();
}
QJsonObject CloudAudioIndex::source(const PlaybackTrack &track) const
{
    const auto embedded = track.catalogMetadata.value("cloud_source").toObject();
    if (!embedded.value("hash").toString().isEmpty())
        return embedded;
    const auto metadata = track.catalogMetadata;
    const QStringList keys{
        idKey("song:", qint64(track.songId)), idKey("audio:", metadata.value("audio_id")),
        hashKey("std:", metadata.value("hash_std").toString()), hashKey("hash:", track.hash)};
    if (m_cache) {
        for (const auto &key : keys)
            if (!key.isEmpty() && m_lookup.contains(key))
                return readSource(m_lookup.value(key));
        return {};
    }
    for (const auto &key : keys)
        if (!key.isEmpty() && m_index.contains(key))
            return m_index.value(key);
    return {};
}
} // namespace EchoMusic

EchoMusic::CloudAudioIndex::~CloudAudioIndex()
{
    if (m_request)
        m_api.cancel(m_request);
}
