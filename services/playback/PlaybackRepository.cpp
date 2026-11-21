#include "PlaybackRepository_p.h"
#include "EmbeddedCoverPresentation.h"
#include "ArtistCoverPresentation.h"
#include "SqlDiagnostics.h"
#include "SongPresentation.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QJsonDocument>
#include <utility>
#include <QPointer>
namespace EchoMusic
{
namespace
{
QJsonObject historyProjection(const QJsonObject &snapshot)
{
    auto track = PlaybackTrack::fromSnapshot(snapshot);
    auto presentation = SongPresentation::value(track.toCatalog());
    auto song = presentation.value("song").toObject();
    auto metadata = song.value("metadata").toObject();
    metadata.remove("plugin_data");
    song.insert("metadata", metadata);
    presentation.insert("song", song);
    track.catalogMetadata = PlaybackTrack::publicMetadata(track.catalogMetadata);
    track.catalogMetadata.remove("plugin_data");
    auto projected = track.toDomain();
    projected.insert("_presentation", presentation);
    return projected;
}
} // namespace
PlaybackPayloadSnapshot::~PlaybackPayloadSnapshot()
{
    if (!m_retirement)
        return;
    // Disarming under the same lock keeps a last release on another thread
    // from posting to a storage worker that is already being destroyed.
    const std::lock_guard guard(m_retirement->mutex);
    if (m_retirement->notify)
        m_retirement->notify();
}
PlaybackStorageWorker::PlaybackStorageWorker(QObject *parent)
    : QObject(parent), m_queueRetirement(std::make_shared<PlaybackPayloadSnapshot::Retirement>()),
      m_retirement(std::make_shared<HistoryPayloadSnapshot::Retirement>())
{
    m_historyCollection.setSingleShot(true);
    m_historyCollection.setInterval(25);
    connect(&m_historyCollection, &QTimer::timeout, this,
            &PlaybackStorageWorker::collectHistoryPayloads);
    m_queueCollection.setSingleShot(true);
    m_queueCollection.setInterval(25);
    connect(&m_queueCollection, &QTimer::timeout, this,
            &PlaybackStorageWorker::collectQueuePayloads);
    m_retirement->notify = [this] {
        QMetaObject::invokeMethod(
            this, [this] { scheduleHistoryCollection(); }, Qt::QueuedConnection);
    };
    m_queueRetirement->notify = [this] {
        QMetaObject::invokeMethod(
            this, [this] { scheduleQueueCollection(); }, Qt::QueuedConnection);
    };
}
PlaybackStorageWorker::~PlaybackStorageWorker()
{
    for (const auto &retirement : {m_retirement, m_queueRetirement}) {
        const std::lock_guard guard(retirement->mutex);
        retirement->notify = {};
    }
    const auto name = m_database.connectionName();
    m_database.close();
    m_database = {};
    if (!name.isEmpty())
        QSqlDatabase::removeDatabase(name);
}
void PlaybackStorageWorker::initialize(const QString &path)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        emit failed(tr("无法创建播放数据目录"));
        return;
    }
    m_database = QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
    m_database.setDatabaseName(path);
    m_database.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    if (!m_database.open()) {
        emit failed(SqlDiagnostics::failure(m_database, "open", m_database.lastError()));
        return;
    }
    QSqlQuery q(m_database);
    // Reading can fail before a version is available (WAL/SHM, I/O, locks).
    // Never classify those failures as an unsupported schema.
    if (!q.exec("PRAGMA user_version") || !q.next()) {
        emit failed(
            tr("无法读取播放数据库版本：%1")
                .arg(SqlDiagnostics::failure(m_database, "read_user_version", q.lastError())));
        return;
    }
    const auto schemaVersion = q.value(0).toInt();
    q.finish();
    if (schemaVersion > 6) {
        SqlDiagnostics::unsupportedVersion(m_database, schemaVersion, 6);
        emit failed(tr("播放数据库版本不受支持（实际版本 %1，当前支持至 6）").arg(schemaVersion));
        return;
    }
    if (!m_database.transaction()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "initialize.begin", m_database.lastError()));
        return;
    }
    for (const auto *sql :
         {"CREATE TABLE IF NOT EXISTS playback_accounts(account_id TEXT PRIMARY KEY,active_queue "
          "TEXT NOT NULL)",
          "CREATE TABLE IF NOT EXISTS playback_queues(account_id TEXT NOT NULL,queue_key TEXT NOT "
          "NULL,title TEXT NOT NULL,current_row INTEGER NOT NULL,ordinal INTEGER NOT NULL,PRIMARY "
          "KEY(account_id,queue_key))",
          "CREATE TABLE IF NOT EXISTS playback_tracks(account_id TEXT NOT NULL,queue_key TEXT NOT "
          "NULL,ordinal INTEGER NOT NULL,catalog_id TEXT NOT NULL,hash TEXT NOT NULL,title TEXT "
          "NOT NULL,artist TEXT NOT NULL,album TEXT NOT NULL,cover TEXT NOT NULL,local_path TEXT "
          "NOT NULL,album_id TEXT NOT NULL,song_id TEXT NOT NULL,duration_ms INTEGER NOT "
          "NULL,next_rank INTEGER NOT NULL,PRIMARY KEY(account_id,queue_key,ordinal),FOREIGN "
          "KEY(account_id,queue_key) REFERENCES playback_queues(account_id,queue_key) ON DELETE "
          "CASCADE)",
          "CREATE TABLE IF NOT EXISTS playback_history(account_id TEXT NOT NULL,track_key TEXT NOT "
          "NULL,catalog_id TEXT NOT NULL,hash TEXT NOT NULL,title TEXT NOT NULL,artist TEXT NOT "
          "NULL,cover TEXT NOT NULL,played_at INTEGER NOT NULL,listened_ms INTEGER NOT "
          "NULL,play_count INTEGER NOT NULL,PRIMARY KEY(account_id,track_key))",
          "CREATE TABLE IF NOT EXISTS listen_room_index(account_id TEXT NOT NULL,room_id TEXT NOT "
          "NULL,snapshot TEXT NOT NULL,ordinal INTEGER NOT NULL,PRIMARY KEY(account_id,room_id))",
          "CREATE TABLE IF NOT EXISTS listen_room_dissolved(account_id TEXT NOT NULL,room_id TEXT "
          "NOT NULL,ordinal INTEGER NOT NULL,PRIMARY KEY(account_id,room_id))",
          "SELECT 1"})
        if (!q.exec(QString::fromLatin1(sql))) {
            const auto message =
                SqlDiagnostics::failure(m_database, "initialize.create_schema", q.lastError());
            m_database.rollback();
            emit failed(message);
            return;
        }
    // Upgrade only this application's schemas. External stores and signed
    // streaming URLs are not imported.
    if (schemaVersion < 2) {
        for (const auto *sql :
             {"ALTER TABLE playback_tracks ADD COLUMN catalog_metadata TEXT NOT NULL DEFAULT '{}'",
              "ALTER TABLE playback_tracks ADD COLUMN catalog_privilege INTEGER NOT NULL DEFAULT 0",
              "ALTER TABLE playback_history ADD COLUMN track_snapshot TEXT NOT NULL DEFAULT '{}'",
              "PRAGMA user_version=2"}) {
            if (!q.exec(QString::fromLatin1(sql))) {
                const auto message =
                    SqlDiagnostics::failure(m_database, "initialize.upgrade_v2", q.lastError());
                m_database.rollback();
                emit failed(message);
                return;
            }
        }
    }
    if (schemaVersion < 3) {
        if (!q.exec("ALTER TABLE playback_accounts ADD COLUMN last_local_queue TEXT NOT NULL "
                    "DEFAULT 'default'") ||
            !q.exec("PRAGMA user_version=3")) {
            const auto message =
                SqlDiagnostics::failure(m_database, "initialize.upgrade_v3", q.lastError());
            m_database.rollback();
            emit failed(message);
            return;
        }
    }
    if (schemaVersion < 4) {
        if (!q.exec(
                "ALTER TABLE playback_queues ADD COLUMN last_used INTEGER NOT NULL DEFAULT 0") ||
            !q.exec("PRAGMA user_version=4")) {
            const auto message =
                SqlDiagnostics::failure(m_database, "initialize.upgrade_v4", q.lastError());
            m_database.rollback();
            emit failed(message);
            return;
        }
    }
    if (schemaVersion < 5 && !q.exec("PRAGMA user_version=5")) {
        const auto message =
            SqlDiagnostics::failure(m_database, "initialize.upgrade_v5", q.lastError());
        m_database.rollback();
        emit failed(message);
        return;
    }
    if (!q.exec("CREATE TABLE IF NOT EXISTS playback_song_payloads(payload_key TEXT PRIMARY "
                "KEY,metadata TEXT NOT NULL)")) {
        m_database.rollback();
        emit failed(
            SqlDiagnostics::failure(m_database, "initialize.payload_schema", q.lastError()));
        return;
    }
    if (schemaVersion < 6) {
        bool columnExists = false;
        if (q.exec("PRAGMA table_info(playback_tracks)"))
            while (q.next())
                columnExists |= q.value(1).toString() == "payload_key";
        q.finish();
        if (!columnExists &&
            !q.exec(
                "ALTER TABLE playback_tracks ADD COLUMN payload_key TEXT NOT NULL DEFAULT ''")) {
            m_database.rollback();
            emit failed(
                SqlDiagnostics::failure(m_database, "initialize.upgrade_v6", q.lastError()));
            return;
        }
        QSqlQuery rows(m_database), insert(m_database), update(m_database);
        if (!rows.exec("SELECT rowid,catalog_metadata FROM playback_tracks")) {
            m_database.rollback();
            emit failed(SqlDiagnostics::failure(m_database, "initialize.upgrade_v6.read",
                                                rows.lastError()));
            return;
        }
        insert.prepare("INSERT INTO playback_song_payloads(payload_key,metadata) VALUES(?,?)");
        update.prepare("UPDATE playback_tracks SET catalog_metadata=?,payload_key=? WHERE rowid=?");
        while (rows.next()) {
            const auto bytes = rows.value(1).toByteArray();
            QJsonParseError error;
            const auto metadata = QJsonDocument::fromJson(bytes, &error);
            if (error.error != QJsonParseError::NoError || !metadata.isObject()) {
                m_database.rollback();
                emit failed(tr("播放队列的曲目资料已损坏"));
                return;
            }
            const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
            insert.bindValue(0, key);
            insert.bindValue(1, bytes);
            update.bindValue(0, QJsonDocument(PlaybackTrack::publicMetadata(metadata.object()))
                                    .toJson(QJsonDocument::Compact));
            update.bindValue(1, key);
            update.bindValue(2, rows.value(0));
            if (!insert.exec() || !update.exec()) {
                m_database.rollback();
                emit failed(tr("升级播放队列资料失败"));
                return;
            }
        }
        if (rows.lastError().isValid() || !q.exec("PRAGMA user_version=6")) {
            m_database.rollback();
            emit failed(tr("升级播放队列资料失败"));
            return;
        }
    }
    if (!q.exec("CREATE TABLE IF NOT EXISTS playback_history_payloads(account_id TEXT NOT "
                "NULL,payload_key TEXT PRIMARY KEY,snapshot TEXT NOT NULL)")) {
        m_database.rollback();
        emit failed(tr("创建历史歌曲资料表失败"));
        return;
    }
    bool historyPayloadColumn = false;
    if (q.exec("PRAGMA table_info(playback_history)"))
        while (q.next())
            historyPayloadColumn |= q.value(1).toString() == "payload_key";
    q.finish();
    if (!historyPayloadColumn) {
        if (!q.exec(
                "ALTER TABLE playback_history ADD COLUMN payload_key TEXT NOT NULL DEFAULT ''")) {
            m_database.rollback();
            emit failed(tr("升级历史歌曲资料失败"));
            return;
        }
        QSqlQuery rows(m_database), insert(m_database), update(m_database);
        if (!rows.exec("SELECT rowid,account_id,track_snapshot FROM playback_history")) {
            m_database.rollback();
            emit failed(tr("读取历史歌曲资料失败"));
            return;
        }
        insert.prepare(
            "INSERT INTO playback_history_payloads(account_id,payload_key,snapshot) VALUES(?,?,?)");
        update.prepare("UPDATE playback_history SET track_snapshot=?,payload_key=? WHERE rowid=?");
        while (rows.next()) {
            const auto bytes = rows.value(2).toByteArray();
            const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const auto document = QJsonDocument::fromJson(bytes);
            // Preserve corrupt source bytes for diagnosis; a bad history row
            // reports a history read failure, never blocks unrelated queue startup.
            const auto projected = document.isObject() ? historyProjection(document.object())
                                                       : QJsonObject{{"_invalid", true}};
            insert.bindValue(0, rows.value(1));
            insert.bindValue(1, key);
            insert.bindValue(2, bytes);
            update.bindValue(0, QJsonDocument(projected).toJson(QJsonDocument::Compact));
            update.bindValue(1, key);
            update.bindValue(2, rows.value(0));
            if (!insert.exec() || !update.exec()) {
                m_database.rollback();
                emit failed(tr("升级历史歌曲资料失败"));
                return;
            }
        }
        if (rows.lastError().isValid()) {
            m_database.rollback();
            emit failed(tr("升级历史歌曲资料失败"));
            return;
        }
    }
    if (!m_database.commit()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "initialize.commit", m_database.lastError()));
        return;
    }
    for (const auto *sql :
         {"PRAGMA foreign_keys=ON", "PRAGMA journal_mode=WAL", "PRAGMA synchronous=NORMAL",
          "CREATE INDEX IF NOT EXISTS playback_history_payload_ref ON "
          "playback_history(payload_key)",
          "CREATE INDEX IF NOT EXISTS playback_queue_payload_ref ON "
          "playback_tracks(payload_key)"})
        if (!q.exec(QString::fromLatin1(sql))) {
            emit failed(
                SqlDiagnostics::failure(m_database, "initialize.configure_wal", q.lastError()));
            return;
        }
    m_valid = true;
    collectPayloads();
}
void PlaybackStorageWorker::load(quint64 account, quint64 revision, bool projectionsOnly)
{
    if (!m_valid)
        return;
    const QString user = QString::number(account);
    PlaybackQueueStates states;
    QString active, lastLocal = "default";
    QSqlQuery q(m_database);
    q.prepare("SELECT active_queue,last_local_queue FROM playback_accounts WHERE account_id=?");
    q.addBindValue(user);
    if (!q.exec()) {
        emit failed(SqlDiagnostics::failure(m_database, "load_queues", q.lastError()));
        return;
    }
    if (q.next()) {
        active = q.value(0).toString();
        lastLocal = q.value(1).toString();
    }
    if (q.lastError().isValid()) {
        emit failed(SqlDiagnostics::failure(m_database, "load_queues.fetch_active", q.lastError()));
        return;
    }
    q.finish();
    q.prepare("SELECT queue_key,title,current_row,last_used FROM playback_queues WHERE "
              "account_id=? ORDER BY "
              "ordinal LIMIT 4");
    q.addBindValue(user);
    if (!q.exec()) {
        emit failed(SqlDiagnostics::failure(m_database, "load_queues", q.lastError()));
        return;
    }
    while (q.next()) {
        PlaybackQueueState state{
            q.value(0).toString(), q.value(1).toString(), {}, q.value(2).toInt(), {}};
        state.lastUsed = q.value(3).toULongLong();
        QSqlQuery tracks(m_database);
        tracks.prepare(
            "SELECT "
            "catalog_id,hash,title,artist,album,cover,local_path,album_id,song_id,duration_ms,next_"
            "rank,catalog_metadata,catalog_privilege,payload_key FROM playback_tracks WHERE "
            "account_id=? AND "
            "queue_key=? ORDER BY ordinal");
        tracks.addBindValue(user);
        tracks.addBindValue(state.key);
        if (!tracks.exec()) {
            emit failed(SqlDiagnostics::failure(m_database, "load_queues", tracks.lastError()));
            return;
        }
        QMap<int, QString> next;
        while (tracks.next()) {
            PlaybackTrack t;
            t.id = tracks.value(0).toString();
            t.hash = tracks.value(1).toString();
            t.title = tracks.value(2).toString();
            t.artist = tracks.value(3).toString();
            t.album = tracks.value(4).toString();
            t.cover = tracks.value(5).toString();
            t.localPath = tracks.value(6).toString();
            t.albumId = tracks.value(7).toString().toULongLong();
            t.songId = tracks.value(8).toString().toULongLong();
            t.durationMs = tracks.value(9).toULongLong();
            QJsonParseError metadataError;
            const auto metadata =
                QJsonDocument::fromJson(tracks.value(11).toByteArray(), &metadataError);
            if (metadataError.error != QJsonParseError::NoError || !metadata.isObject()) {
                emit failed(tr("播放队列的曲目资料已损坏"));
                return;
            }
            t.catalogMetadata = metadata.object();
            t.catalogPrivilege = tracks.value(12).toUInt();
            t.payloadKey = tracks.value(13).toString();
            t.payloadLoaded = false;
            if (!projectionsOnly) {
                auto result = hydrate(t);
                if (!result.second.isEmpty()) {
                    emit failed(result.second);
                    return;
                }
                t = std::move(result.first);
            }
            if (tracks.value(10).toInt() >= 0)
                next.insert(tracks.value(10).toInt(), t.key());
            state.tracks.append(std::move(t));
        }
        state.next = next.values();
        if (tracks.lastError().isValid()) {
            emit failed(SqlDiagnostics::failure(m_database, "load_queues.fetch_tracks",
                                                tracks.lastError()));
            return;
        }
        states.append(std::move(state));
    }
    if (q.lastError().isValid()) {
        emit failed(SqlDiagnostics::failure(m_database, "load_queues.fetch", q.lastError()));
        return;
    }
    QStringList keys;
    for (const auto &state : std::as_const(states))
        for (const auto &track : state.tracks)
            if (!track.payloadLoaded)
                keys.append(track.payloadKey);
    if (!keys.isEmpty()) {
        const auto lease = queueLease(keys);
        for (auto &state : states)
            for (auto &track : state.tracks)
                if (!track.payloadLoaded)
                    track.payloadLease = lease;
    }
    emit loaded(account, revision, states, active, lastLocal);
}
void PlaybackStorageWorker::save(quint64 account, const PlaybackQueueStates &states,
                                 const QString &active, const QString &lastLocal)
{
    if (!m_valid || states.size() > 4)
        return;
    if (!m_database.transaction()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "save_queues.begin", m_database.lastError()));
        return;
    }
    const QString user = QString::number(account);
    QSqlQuery q(m_database);
    bool ok = true;
    q.prepare("DELETE FROM playback_queues WHERE account_id=?");
    q.addBindValue(user);
    ok = q.exec();
    QSqlError writeError = q.lastError();
    const char *operation = "save_queues.delete";
    QSqlQuery queues(m_database), tracks(m_database);
    queues.prepare("INSERT INTO playback_queues VALUES(?,?,?,?,?,?)");
    tracks.prepare("INSERT INTO "
                   "playback_tracks(account_id,queue_key,ordinal,catalog_id,hash,title,artist,"
                   "album,cover,local_path,album_id,song_id,duration_ms,next_rank,catalog_metadata,"
                   "catalog_privilege,payload_key) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    QSqlQuery payloads(m_database), exists(m_database);
    payloads.prepare("INSERT INTO playback_song_payloads(payload_key,metadata) VALUES(?,?) ON "
                     "CONFLICT(payload_key) DO NOTHING");
    exists.prepare("SELECT 1 FROM playback_song_payloads WHERE payload_key=?");
    QStringList storedKeys;
    for (int i = 0; ok && i < states.size(); ++i) {
        const auto &s = states[i];
        queues.bindValue(0, user);
        queues.bindValue(1, s.key);
        queues.bindValue(2, s.title);
        queues.bindValue(3, s.current);
        queues.bindValue(4, i);
        queues.bindValue(5, qint64(s.lastUsed));
        ok = queues.exec();
        if (!ok) {
            writeError = queues.lastError();
            operation = "save_queues.insert_queue";
        }
        for (int row = 0; ok && row < s.tracks.size(); ++row) {
            const auto &t = s.tracks[row];
            const auto clean = [](const QString &v) { return v.isNull() ? QStringLiteral("") : v; };
            auto metadata = t.catalogMetadata;
            const auto payloadKey = t.payloadKey.isEmpty()
                                        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                        : t.payloadKey;
            metadata.remove("embedded_cover_url");
            metadata.remove("artist_cover_url");
            if (t.payloadLoaded) {
                payloads.bindValue(0, payloadKey);
                payloads.bindValue(1, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
                ok = payloads.exec();
                if (!ok) {
                    writeError = payloads.lastError();
                    operation = "save_queues.payload";
                    break;
                }
            } else {
                exists.bindValue(0, payloadKey);
                if (!exists.exec() || !exists.next()) {
                    ok = false;
                    writeError = exists.lastError();
                    operation = "save_queues.missing_payload";
                    break;
                }
                exists.finish();
            }
            storedKeys.append(payloadKey);
            metadata = PlaybackTrack::publicMetadata(metadata);
            const QVariantList values{user,
                                      s.key,
                                      row,
                                      clean(t.id),
                                      clean(t.hash),
                                      clean(t.title),
                                      clean(t.artist),
                                      clean(t.album),
                                      clean(hasEmbeddedCover(t.catalogMetadata, t.cover) ||
                                                    hasArtistCover(t.catalogMetadata, t.cover)
                                                ? QString()
                                                : t.cover),
                                      clean(t.localPath),
                                      QString::number(t.albumId),
                                      QString::number(t.songId),
                                      qint64(t.durationMs),
                                      int(s.next.indexOf(t.key())),
                                      QJsonDocument(metadata).toJson(QJsonDocument::Compact),
                                      t.catalogPrivilege,
                                      payloadKey};
            for (int v = 0; v < values.size(); ++v)
                tracks.bindValue(v, values[v]);
            ok = tracks.exec();
            if (!ok) {
                writeError = tracks.lastError();
                operation = "save_queues.insert_track";
            }
        }
    }
    if (ok) {
        q.prepare("INSERT INTO playback_accounts(account_id,active_queue,last_local_queue) "
                  "VALUES(?,?,?) ON CONFLICT(account_id) DO UPDATE SET "
                  "active_queue=excluded.active_queue,last_local_queue=excluded.last_local_queue");
        q.addBindValue(user);
        q.addBindValue(active);
        q.addBindValue(lastLocal.isEmpty()
                           ? (active.startsWith("fm/") ? QString("default") : active)
                           : lastLocal);
        ok = q.exec();
        if (!ok) {
            writeError = q.lastError();
            operation = "save_queues.select_active";
        }
    }
    if (!ok) {
        SqlDiagnostics::failure(m_database, operation, writeError);
        m_database.rollback();
        emit failed(tr("保存播放队列失败"));
    } else if (!m_database.commit()) {
        const auto error = m_database.lastError();
        m_database.rollback();
        emit failed(SqlDiagnostics::failure(m_database, "save_queues.commit", error));
    } else {
        emit saved(storedKeys, queueLease(storedKeys));
        scheduleQueueCollection();
    }
}
std::pair<PlaybackTrack, QString> PlaybackStorageWorker::hydrate(PlaybackTrack track)
{
    if (track.payloadLoaded)
        return {std::move(track), {}};
    if (!m_valid)
        return {{}, tr("播放队列存储不可用")};
    QSqlQuery query(m_database);
    query.prepare("SELECT metadata FROM playback_song_payloads WHERE payload_key=?");
    query.addBindValue(track.payloadKey);
    if (!query.exec())
        return {{}, SqlDiagnostics::failure(m_database, "read_queue_payload", query.lastError())};
    if (!query.next())
        return {{}, tr("播放队列的曲目资料已失效")};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(query.value(0).toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {{}, tr("播放队列的曲目资料已损坏")};
    auto metadata = document.object();
    const auto previous = PlaybackTrack::publicMetadata(metadata);
    for (auto it = previous.begin(); it != previous.end(); ++it)
        if (!track.catalogMetadata.contains(it.key()))
            metadata.remove(it.key());
    for (auto it = track.catalogMetadata.begin(); it != track.catalogMetadata.end(); ++it)
        metadata.insert(it.key(), it.value());
    track.catalogMetadata = std::move(metadata);
    track.payloadLoaded = true;
    track.payloadLease.reset();
    return {std::move(track), {}};
}
PlaybackPayloadLease PlaybackStorageWorker::queueLease(const QStringList &keys)
{
    auto lease = std::shared_ptr<PlaybackPayloadSnapshot>(new PlaybackPayloadSnapshot);
    lease->m_keys = keys;
    lease->m_retirement = m_queueRetirement;
    m_queueLeases.append(lease);
    return lease;
}
void PlaybackStorageWorker::scheduleQueueCollection()
{
    if (!m_valid)
        return;
    m_queueCollectionCursor = 0;
    if (!m_queueCollection.isActive())
        m_queueCollection.start();
}
void PlaybackStorageWorker::collectQueuePayloads()
{
    if (!m_valid)
        return;
    QSet<QString> pinned;
    QList<PlaybackPayloadLease> leases;
    for (auto it = m_queueLeases.begin(); it != m_queueLeases.end();) {
        if (auto lease = it->lock()) {
            for (const auto &key : lease->m_keys)
                pinned.insert(key);
            leases.append(std::move(lease));
            ++it;
        } else
            it = m_queueLeases.erase(it);
    }
    // Bound scanned rows as well as deletes, including when all old records
    // are pinned. The reference index avoids rescanning every live queue.
    QSqlQuery query(m_database);
    query.prepare("SELECT p.rowid,p.payload_key,EXISTS(SELECT 1 FROM playback_tracks t WHERE "
                  "t.payload_key=p.payload_key) FROM playback_song_payloads p WHERE p.rowid>? "
                  "ORDER BY p.rowid LIMIT 256");
    query.addBindValue(m_queueCollectionCursor);
    if (!query.exec()) {
        emit failed(SqlDiagnostics::failure(m_database, "collect_queue.select", query.lastError()));
        return;
    }
    int scanned = 0;
    auto cursor = m_queueCollectionCursor;
    QStringList retired;
    while (query.next()) {
        ++scanned;
        cursor = query.value(0).toLongLong();
        const auto key = query.value(1).toString();
        if (!query.value(2).toBool() && !pinned.contains(key))
            retired.append(key);
    }
    if (query.lastError().isValid()) {
        emit failed(SqlDiagnostics::failure(m_database, "collect_queue.fetch", query.lastError()));
        return;
    }
    query.finish();
    if (!retired.isEmpty()) {
        if (!m_database.transaction()) {
            emit failed(
                SqlDiagnostics::failure(m_database, "collect_queue.begin", m_database.lastError()));
            return;
        }
        query.prepare("DELETE FROM playback_song_payloads WHERE payload_key=?");
        for (const auto &key : retired) {
            query.bindValue(0, key);
            if (!query.exec()) {
                const auto error = query.lastError();
                m_database.rollback();
                emit failed(SqlDiagnostics::failure(m_database, "collect_queue.delete", error));
                return;
            }
        }
        if (!m_database.commit()) {
            const auto error = m_database.lastError();
            m_database.rollback();
            emit failed(SqlDiagnostics::failure(m_database, "collect_queue.commit", error));
            return;
        }
    }
    m_queueCollectionCursor = cursor;
    if (scanned == 256)
        m_queueCollection.start();
}
void PlaybackStorageWorker::collectPayloads()
{
    if (!m_valid)
        return;
    QSqlQuery query(m_database);
    if (!query.exec("DELETE FROM playback_song_payloads WHERE payload_key NOT IN (SELECT "
                    "payload_key FROM playback_tracks)"))
        emit failed(
            SqlDiagnostics::failure(m_database, "retire_queue_payloads", query.lastError()));
    if (!query.exec("DELETE FROM playback_history_payloads WHERE payload_key NOT IN (SELECT "
                    "payload_key FROM playback_history)"))
        emit failed(
            SqlDiagnostics::failure(m_database, "retire_history_payloads", query.lastError()));
}
void PlaybackStorageWorker::scheduleHistoryCollection()
{
    if (m_valid && !m_historyCollection.isActive())
        m_historyCollection.start();
}
void PlaybackStorageWorker::collectHistoryPayloads()
{
    if (!m_valid)
        return;
    QSet<QString> pinned;
    QList<HistoryPayloadLease> leases;
    for (auto it = m_historyLeases.begin(); it != m_historyLeases.end();) {
        if (auto lease = it->lock()) {
            for (const auto &key : lease->m_keys)
                pinned.insert(key);
            leases.append(std::move(lease));
            ++it;
        } else
            it = m_historyLeases.erase(it);
    }
    QSqlQuery query(m_database);
    if (!query.exec("SELECT payload_key FROM playback_history_payloads WHERE payload_key NOT IN "
                    "(SELECT payload_key FROM playback_history)")) {
        emit failed(
            SqlDiagnostics::failure(m_database, "collect_history.select", query.lastError()));
        return;
    }
    QStringList retired;
    while (retired.size() < 256 && query.next()) {
        const auto key = query.value(0).toString();
        if (!pinned.contains(key))
            retired.append(key);
    }
    if (query.lastError().isValid()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "collect_history.fetch", query.lastError()));
        return;
    }
    query.finish();
    if (retired.isEmpty())
        return;
    if (!m_database.transaction()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "collect_history.begin", m_database.lastError()));
        return;
    }
    query.prepare("DELETE FROM playback_history_payloads WHERE payload_key=?");
    for (const auto &key : retired) {
        query.bindValue(0, key);
        if (!query.exec()) {
            const auto error = query.lastError();
            m_database.rollback();
            emit failed(SqlDiagnostics::failure(m_database, "collect_history.delete", error));
            return;
        }
    }
    if (!m_database.commit()) {
        const auto error = m_database.lastError();
        m_database.rollback();
        emit failed(SqlDiagnostics::failure(m_database, "collect_history.commit", error));
    } else if (retired.size() == 256)
        scheduleHistoryCollection();
}
void PlaybackStorageWorker::record(quint64 account, const PlaybackTrack &track, qint64 listenedMs)
{
    if (!m_valid || !track.playable() || listenedMs < 0)
        return;
    if (!m_database.transaction()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "record_history.begin", m_database.lastError()));
        return;
    }
    const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto snapshot = track.toDomain();
    QSqlQuery payload(m_database);
    payload.prepare(
        "INSERT INTO playback_history_payloads(account_id,payload_key,snapshot) VALUES(?,?,?)");
    payload.addBindValue(QString::number(account));
    payload.addBindValue(key);
    payload.addBindValue(QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
    QSqlQuery q(m_database);
    q.prepare("INSERT INTO "
              "playback_history(account_id,track_key,catalog_id,hash,title,artist,cover,played_at,"
              "listened_ms,play_count,track_snapshot,payload_key) VALUES(?,?,?,?,?,?,?,?,?,1,?,?) "
              "ON CONFLICT(account_id,track_key) DO UPDATE SET "
              "played_at=excluded.played_at,listened_ms=playback_history.listened_ms+excluded."
              "listened_ms,play_count=playback_history.play_count+1,track_snapshot=excluded.track_"
              "snapshot,payload_key=excluded.payload_key,title=excluded.title,artist=excluded."
              "artist,cover=excluded.cover");
    const auto clean = [](const QString &value) {
        return value.isNull() ? QStringLiteral("") : value;
    };
    for (const auto &value : QVariantList{
             QString::number(account), historyKey(track), clean(track.id), clean(track.hash),
             clean(track.title), clean(track.artist),
             clean(hasEmbeddedCover(track.catalogMetadata, track.cover) ||
                           hasArtistCover(track.catalogMetadata, track.cover)
                       ? QString()
                       : track.cover),
             QDateTime::currentMSecsSinceEpoch(), listenedMs,
             QJsonDocument(historyProjection(snapshot)).toJson(QJsonDocument::Compact), key})
        q.addBindValue(value);
    if (!payload.exec() || !q.exec() || !m_database.commit()) {
        const auto error = payload.lastError().isValid() ? payload.lastError()
                           : q.lastError().isValid()     ? q.lastError()
                                                         : m_database.lastError();
        m_database.rollback();
        emit failed(SqlDiagnostics::failure(m_database, "record_history", error));
    } else {
        scheduleHistoryCollection();
        emit historyChanged(account);
    }
}

void PlaybackStorageWorker::addHistoryTime(quint64 account, const PlaybackTrack &track,
                                           qint64 listenedMs)
{
    if (!m_valid || !track.playable() || listenedMs <= 0)
        return;
    QSqlQuery q(m_database);
    q.prepare("UPDATE playback_history SET listened_ms=listened_ms+? "
              "WHERE account_id=? AND track_key=?");
    q.addBindValue(listenedMs);
    q.addBindValue(QString::number(account));
    q.addBindValue(historyKey(track));
    if (!q.exec())
        emit failed(SqlDiagnostics::failure(m_database, "add_history_time", q.lastError()));
}
QString PlaybackStorageWorker::historyKey(const PlaybackTrack &track)
{
    // Unmatched cloud uploads queue under view identities that changed over
    // time ("0" before; "cloud:<hash>" now). The stable key converges on the
    // audio hash so equal hashes share one row.
    return track.stableKey();
}
void PlaybackStorageWorker::loadHistory(quint64 account, quint64 revision, bool projectionsOnly)
{
    if (!m_valid) {
        emit historyFailed(tr("播放历史存储不可用"));
        return;
    }
    QSqlQuery q(m_database);
    q.prepare(
        QString(
            "SELECT h.track_key,h.played_at,h.listened_ms,h.play_count,%1,h.rowid,h.payload_key "
            "FROM playback_history h LEFT JOIN playback_history_payloads p ON "
            "p.payload_key=h.payload_key AND p.account_id=h.account_id WHERE h.account_id=? ORDER "
            "BY h.played_at DESC,h.rowid DESC LIMIT 1000")
            .arg(projectionsOnly ? "h.track_snapshot" : "p.snapshot"));
    q.addBindValue(QString::number(account));
    if (!q.exec()) {
        emit historyFailed(SqlDiagnostics::failure(m_database, "load_history", q.lastError()));
        return;
    }
    // Rows recorded under a superseded identity (view id changes) may share
    // one hash. Merge those unmatched-upload rows into the newest one so
    // repeats count on a single line; catalog rows never merge.
    struct Row
    {
        QString key, rowid, payloadKey;
        qint64 playedAt = 0, listenedMs = 0, playCount = 0;
    };
    QList<Row> rows;
    QList<QJsonObject> snapshots;
    QHash<QString, int> canonical;
    QSet<int> mergedRows;
    QStringList dropRowids;
    while (q.next()) {
        QJsonParseError error;
        const auto snapshot = QJsonDocument::fromJson(q.value(4).toByteArray(), &error);
        if (error.error != QJsonParseError::NoError || !snapshot.isObject() ||
            snapshot.object().value("_invalid").toBool()) {
            emit historyFailed(tr("播放历史的曲目资料已损坏"));
            return;
        }
        Row row{q.value(0).toString(),   q.value(5).toString(),   q.value(6).toString(),
                q.value(1).toLongLong(), q.value(2).toLongLong(), q.value(3).toLongLong()};
        const auto object = snapshot.object();
        const auto hash = object.value("hash").toString().toLower();
        bool numeric = false;
        const auto catalogId = object.value("id").toString().toULongLong(&numeric);
        const auto mergeable = !hash.isEmpty() && !(numeric && catalogId);
        const auto known = mergeable ? canonical.constFind(hash) : canonical.cend();
        if (known == canonical.cend()) {
            if (mergeable)
                canonical.insert(hash, int(rows.size()));
            rows.append(row);
            snapshots.append(object);
            continue;
        }
        // Older duplicate: disappear, and hand its totals to the newest row.
        auto &first = rows[*known];
        first.listenedMs += row.listenedMs;
        first.playCount += row.playCount;
        mergedRows.insert(*known);
        dropRowids.append(row.rowid);
    }
    if (q.lastError().isValid()) {
        emit historyFailed(
            SqlDiagnostics::failure(m_database, "load_history.fetch", q.lastError()));
        return;
    }
    if (!dropRowids.isEmpty()) {
        // The kept row must carry the merged totals before the duplicates are
        // removed; otherwise a restart would silently drop their plays. Both
        // writes stay in one transaction so a failed delete cannot leave the
        // totals counted twice.
        bool ok = m_database.transaction();
        QSqlError mergeError = m_database.lastError();
        const char *operation = "merge_history.begin";
        if (ok) {
            QSqlQuery update(m_database);
            update.prepare("UPDATE playback_history SET listened_ms=?,play_count=? "
                           "WHERE account_id=? AND rowid=?");
            for (const auto index : std::as_const(mergedRows)) {
                update.bindValue(0, rows[index].listenedMs);
                update.bindValue(1, rows[index].playCount);
                update.bindValue(2, QString::number(account));
                update.bindValue(3, rows[index].rowid);
                ok = update.exec();
                if (!ok) {
                    mergeError = update.lastError();
                    operation = "merge_history.update";
                    break;
                }
                update.finish();
            }
            QSqlQuery cleanup(m_database);
            cleanup.prepare(QStringLiteral("DELETE FROM playback_history WHERE account_id=? AND "
                                           "rowid IN (%1)")
                                .arg(dropRowids.join(u',')));
            cleanup.addBindValue(QString::number(account));
            if (ok && !cleanup.exec()) {
                ok = false;
                mergeError = cleanup.lastError();
                operation = "merge_history.delete";
            }
            if (ok && !m_database.commit()) {
                ok = false;
                mergeError = m_database.lastError();
                operation = "merge_history.commit";
            }
            if (!ok) {
                m_database.rollback();
            }
        }
        if (!ok)
            emit failed(SqlDiagnostics::failure(m_database, operation, mergeError));
    }
    QJsonArray entries;
    for (int i = 0; i < rows.size(); ++i)
        entries.append(QJsonObject{{"key", rows[i].key},
                                   {"played_at", rows[i].playedAt},
                                   {"listened_ms", rows[i].listenedMs},
                                   {"play_count", rows[i].playCount},
                                   {"track", snapshots[i]},
                                   {"payload_key", rows[i].payloadKey}});
    auto lease = std::shared_ptr<HistoryPayloadSnapshot>(new HistoryPayloadSnapshot);
    lease->m_retirement = m_retirement;
    for (const auto &row : rows)
        lease->m_keys.append(row.payloadKey);
    m_historyLeases.append(lease);
    emit historyLoaded(account, revision, entries, std::move(lease));
    scheduleHistoryCollection();
}
std::pair<QList<QJsonObject>, QString> PlaybackStorageWorker::historyTracks(quint64 account,
                                                                            const QStringList &keys)
{
    if (!m_valid)
        return {{}, tr("播放历史存储不可用")};
    if (keys.size() > 128)
        return {{}, tr("播放历史读取数量无效")};
    QList<QJsonObject> values;
    qint64 bytes = 0;
    QSqlQuery query(m_database);
    query.prepare(
        "SELECT snapshot FROM playback_history_payloads WHERE account_id=? AND payload_key=?");
    for (const auto &key : keys) {
        query.bindValue(0, QString::number(account));
        query.bindValue(1, key);
        if (!query.exec() || !query.next())
            return {{}, tr("历史歌曲资料已失效，请重试")};
        const auto payload = query.value(0).toByteArray();
        if (!values.isEmpty() && payload.size() > 2 * 1024 * 1024 - bytes)
            break;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return {{}, tr("播放历史的曲目资料已损坏")};
        values.append(document.object());
        bytes += payload.size();
    }
    return {std::move(values), {}};
}
void PlaybackStorageWorker::removeHistory(quint64 account, const QStringList &keys)
{
    if (!m_valid)
        return;
    if (keys.isEmpty() || keys.size() > 1000) {
        emit historyChanged(account);
        return;
    }
    QStringList marks;
    for (int i = 0; i < keys.size(); ++i)
        marks.append(QStringLiteral("?"));
    QSqlQuery q(m_database);
    q.prepare(
        QStringLiteral("DELETE FROM playback_history WHERE account_id=? AND track_key IN (%1)")
            .arg(marks.join(',')));
    q.addBindValue(QString::number(account));
    for (const auto &key : keys)
        q.addBindValue(key);
    if (!q.exec()) {
        emit historyFailed(SqlDiagnostics::failure(m_database, "remove_history", q.lastError()));
        return;
    }
    scheduleHistoryCollection();
    emit historyChanged(account);
}
void PlaybackStorageWorker::clearHistory(quint64 account)
{
    if (!m_valid)
        return;
    QSqlQuery q(m_database);
    q.prepare("DELETE FROM playback_history WHERE account_id=?");
    q.addBindValue(QString::number(account));
    if (!q.exec()) {
        emit historyFailed(SqlDiagnostics::failure(m_database, "clear_history", q.lastError()));
        return;
    }
    scheduleHistoryCollection();
    emit historyChanged(account);
}
void PlaybackStorageWorker::close()
{
    m_queueCollection.stop();
    m_historyCollection.stop();
    for (const auto &retirement : {m_retirement, m_queueRetirement}) {
        const std::lock_guard guard(retirement->mutex);
        retirement->notify = {};
    }
    collectPayloads();
    m_valid = false;
    m_database.close();
    emit drained();
}
void PlaybackStorageWorker::loadRooms(quint64 account, quint64 revision)
{
    if (!m_valid)
        return;
    QSqlQuery query(m_database);
    query.prepare("SELECT snapshot FROM listen_room_index WHERE account_id=? ORDER BY ordinal");
    query.addBindValue(QString::number(account));
    if (!query.exec()) {
        emit failed(SqlDiagnostics::failure(m_database, "load_rooms", query.lastError()));
        return;
    }
    QJsonArray rooms;
    while (query.next()) {
        const auto value = QJsonDocument::fromJson(query.value(0).toByteArray());
        if (value.isObject())
            rooms.append(value.object());
    }
    if (query.lastError().isValid()) {
        emit failed(SqlDiagnostics::failure(m_database, "load_rooms.fetch", query.lastError()));
        return;
    }
    query.finish();
    query.prepare(
        "SELECT room_id FROM listen_room_dissolved WHERE account_id=? ORDER BY ordinal LIMIT 200");
    query.addBindValue(QString::number(account));
    if (!query.exec()) {
        emit failed(SqlDiagnostics::failure(m_database, "load_rooms", query.lastError()));
        return;
    }
    QStringList dissolved;
    while (query.next())
        dissolved.append(query.value(0).toString());
    if (query.lastError().isValid()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "load_rooms.fetch_dissolved", query.lastError()));
        return;
    }
    emit roomsLoaded(account, revision, rooms, dissolved);
}
void PlaybackStorageWorker::saveRooms(quint64 account, const QJsonArray &rooms,
                                      const QStringList &dissolved)
{
    if (!m_valid || !account)
        return;
    if (!m_database.transaction()) {
        emit failed(
            SqlDiagnostics::failure(m_database, "save_rooms.begin", m_database.lastError()));
        return;
    }
    QSqlQuery query(m_database);
    const auto fail = [this, &query] {
        const auto message =
            SqlDiagnostics::failure(m_database, "save_rooms.write", query.lastError());
        m_database.rollback();
        emit failed(message);
    };
    for (const auto &table :
         {QStringLiteral("listen_room_index"), QStringLiteral("listen_room_dissolved")}) {
        query.prepare("DELETE FROM " + table + " WHERE account_id=?");
        query.addBindValue(QString::number(account));
        if (!query.exec()) {
            fail();
            return;
        }
    }
    for (int row = 0; row < rooms.size(); ++row) {
        const auto room = rooms[row].toObject();
        const auto id = room.value("id").toString();
        if (id.isEmpty())
            continue;
        query.prepare(
            "INSERT INTO listen_room_index(account_id,room_id,snapshot,ordinal) VALUES(?,?,?,?)");
        query.addBindValue(QString::number(account));
        query.addBindValue(id);
        query.addBindValue(QJsonDocument(room).toJson(QJsonDocument::Compact));
        query.addBindValue(row);
        if (!query.exec()) {
            fail();
            return;
        }
    }
    for (int row = 0; row < qMin(200, dissolved.size()); ++row) {
        query.prepare(
            "INSERT INTO listen_room_dissolved(account_id,room_id,ordinal) VALUES(?,?,?)");
        query.addBindValue(QString::number(account));
        query.addBindValue(dissolved[row]);
        query.addBindValue(row);
        if (!query.exec()) {
            fail();
            return;
        }
    }
    if (!m_database.commit())
        emit failed(
            SqlDiagnostics::failure(m_database, "save_rooms.commit", m_database.lastError()));
}
PlaybackRepository::PlaybackRepository(const QString &path, QObject *parent)
    : QObject(parent), m_worker(new PlaybackStorageWorker)
{
    qRegisterMetaType<PlaybackQueueStates>();
    qRegisterMetaType<HistoryPayloadLease>();
    qRegisterMetaType<PlaybackPayloadLease>();
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName("echo-playback-storage");
    connect(m_worker, &PlaybackStorageWorker::loaded, this, &PlaybackRepository::loaded);
    connect(m_worker, &PlaybackStorageWorker::saved, this, &PlaybackRepository::saved);
    connect(m_worker, &PlaybackStorageWorker::historyLoaded, this,
            &PlaybackRepository::historyLoaded);
    connect(m_worker, &PlaybackStorageWorker::historyFailed, this,
            &PlaybackRepository::historyFailed);
    connect(m_worker, &PlaybackStorageWorker::roomsLoaded, this, &PlaybackRepository::roomsLoaded);
    connect(m_worker, &PlaybackStorageWorker::historyChanged, this,
            &PlaybackRepository::historyChanged);
    connect(m_worker, &PlaybackStorageWorker::failed, this, &PlaybackRepository::failed);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &PlaybackStorageWorker::drained, &m_thread, &QThread::quit,
            Qt::DirectConnection);
    connect(&m_thread, &QThread::finished, this, &PlaybackRepository::closed);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, path] { worker->initialize(path); });
}
PlaybackRepository::~PlaybackRepository()
{
    shutdown();
    m_thread.wait();
}
void PlaybackRepository::load(quint64 a, quint64 r, bool projectionsOnly)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, a, r, projectionsOnly] {
            worker->load(a, r, projectionsOnly);
        });
}
void PlaybackRepository::requestTrack(PlaybackTrack track, QObject *consumer,
                                      std::function<void(PlaybackTrack, QString)> done)
{
    if (m_closing || !consumer)
        return;
    const QPointer<QObject> guard(consumer);
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, guard, track = std::move(track),
                                         done = std::move(done)]() mutable {
        auto result = worker->hydrate(std::move(track));
        QMetaObject::invokeMethod(
            this, [guard, result = std::move(result), done = std::move(done)]() mutable {
                if (guard)
                    done(std::move(result.first), std::move(result.second));
            });
    });
}
void PlaybackRepository::save(quint64 a, const PlaybackQueueStates &s, const QString &key,
                              const QString &lastLocal)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, a, s, key, lastLocal] {
            worker->save(a, s, key, lastLocal);
        });
}
void PlaybackRepository::record(quint64 a, const PlaybackTrack &t, qint64 ms)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker,
                                  [worker = m_worker, a, t, ms] { worker->record(a, t, ms); });
}
void PlaybackRepository::addHistoryTime(quint64 a, const PlaybackTrack &t, qint64 ms)
{
    if (!m_closing)
        QMetaObject::invokeMethod(
            m_worker, [worker = m_worker, a, t, ms] { worker->addHistoryTime(a, t, ms); });
}
void PlaybackRepository::loadHistory(quint64 a, quint64 r, bool projectionsOnly)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, a, r, projectionsOnly] {
            worker->loadHistory(a, r, projectionsOnly);
        });
}
void PlaybackRepository::requestHistoryTracks(quint64 account, QStringList keys, QObject *consumer,
                                              std::function<void(QList<QJsonObject>, QString)> done)
{
    if (m_closing || !consumer)
        return;
    const QPointer<QObject> guard(consumer);
    QMetaObject::invokeMethod(m_worker, [this, worker = m_worker, account, keys = std::move(keys),
                                         guard, done = std::move(done)]() mutable {
        auto result = worker->historyTracks(account, keys);
        QMetaObject::invokeMethod(
            this, [guard, result = std::move(result), done = std::move(done)]() mutable {
                if (guard)
                    done(std::move(result.first), std::move(result.second));
            });
    });
}
void PlaybackRepository::removeHistory(quint64 a, const QStringList &keys)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker,
                                  [worker = m_worker, a, keys] { worker->removeHistory(a, keys); });
}
void PlaybackRepository::clearHistory(quint64 a)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, a] { worker->clearHistory(a); });
}
void PlaybackRepository::loadRooms(quint64 account, quint64 revision)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, account, revision] {
            worker->loadRooms(account, revision);
        });
}
void PlaybackRepository::saveRooms(quint64 account, const QJsonArray &rooms,
                                   const QStringList &dissolved)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, account, rooms, dissolved] {
            worker->saveRooms(account, rooms, dissolved);
        });
}
void PlaybackRepository::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->close(); });
}
} // namespace EchoMusic
