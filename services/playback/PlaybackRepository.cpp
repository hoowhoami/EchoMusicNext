#include "PlaybackRepository_p.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDateTime>
#include <QMap>
#include <QJsonDocument>
namespace EchoMusic
{
PlaybackStorageWorker::~PlaybackStorageWorker()
{
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
        emit failed(m_database.lastError().text());
        return;
    }
    QSqlQuery q(m_database);
    if (!q.exec("PRAGMA user_version") || !q.next() || q.value(0).toInt() > 5) {
        emit failed(tr("播放数据库版本不受支持"));
        return;
    }
    const auto schemaVersion = q.value(0).toInt();
    q.finish();
    if (!m_database.transaction()) {
        emit failed(m_database.lastError().text());
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
            m_database.rollback();
            emit failed(q.lastError().text());
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
                m_database.rollback();
                emit failed(q.lastError().text());
                return;
            }
        }
    }
    if (schemaVersion < 3) {
        if (!q.exec("ALTER TABLE playback_accounts ADD COLUMN last_local_queue TEXT NOT NULL "
                    "DEFAULT 'default'") ||
            !q.exec("PRAGMA user_version=3")) {
            m_database.rollback();
            emit failed(q.lastError().text());
            return;
        }
    }
    if (schemaVersion < 4) {
        if (!q.exec(
                "ALTER TABLE playback_queues ADD COLUMN last_used INTEGER NOT NULL DEFAULT 0") ||
            !q.exec("PRAGMA user_version=4")) {
            m_database.rollback();
            emit failed(q.lastError().text());
            return;
        }
    }
    if (schemaVersion < 5 && !q.exec("PRAGMA user_version=5")) {
        m_database.rollback();
        emit failed(q.lastError().text());
        return;
    }
    if (!m_database.commit()) {
        emit failed(m_database.lastError().text());
        return;
    }
    for (const auto *sql :
         {"PRAGMA foreign_keys=ON", "PRAGMA journal_mode=WAL", "PRAGMA synchronous=NORMAL"})
        if (!q.exec(QString::fromLatin1(sql))) {
            emit failed(q.lastError().text());
            return;
        }
    m_valid = true;
}
void PlaybackStorageWorker::load(quint64 account, quint64 revision)
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
        emit failed(q.lastError().text());
        return;
    }
    if (q.next()) {
        active = q.value(0).toString();
        lastLocal = q.value(1).toString();
    }
    q.finish();
    q.prepare("SELECT queue_key,title,current_row,last_used FROM playback_queues WHERE "
              "account_id=? ORDER BY "
              "ordinal LIMIT 4");
    q.addBindValue(user);
    if (!q.exec()) {
        emit failed(q.lastError().text());
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
            "rank,catalog_metadata,catalog_privilege FROM playback_tracks WHERE account_id=? AND "
            "queue_key=? ORDER BY ordinal");
        tracks.addBindValue(user);
        tracks.addBindValue(state.key);
        if (!tracks.exec()) {
            emit failed(tracks.lastError().text());
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
            if (tracks.value(10).toInt() >= 0)
                next.insert(tracks.value(10).toInt(), t.key());
            state.tracks.append(std::move(t));
        }
        state.next = next.values();
        states.append(std::move(state));
    }
    emit loaded(account, revision, states, active, lastLocal);
}
void PlaybackStorageWorker::save(quint64 account, const PlaybackQueueStates &states,
                                 const QString &active, const QString &lastLocal)
{
    if (!m_valid || states.size() > 4)
        return;
    if (!m_database.transaction()) {
        emit failed(m_database.lastError().text());
        return;
    }
    const QString user = QString::number(account);
    QSqlQuery q(m_database);
    bool ok = true;
    q.prepare("DELETE FROM playback_queues WHERE account_id=?");
    q.addBindValue(user);
    ok = q.exec();
    QSqlQuery queues(m_database), tracks(m_database);
    queues.prepare("INSERT INTO playback_queues VALUES(?,?,?,?,?,?)");
    tracks.prepare("INSERT INTO playback_tracks VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (int i = 0; ok && i < states.size(); ++i) {
        const auto &s = states[i];
        queues.bindValue(0, user);
        queues.bindValue(1, s.key);
        queues.bindValue(2, s.title);
        queues.bindValue(3, s.current);
        queues.bindValue(4, i);
        queues.bindValue(5, qint64(s.lastUsed));
        ok = queues.exec();
        for (int row = 0; ok && row < s.tracks.size(); ++row) {
            const auto &t = s.tracks[row];
            const auto clean = [](const QString &v) { return v.isNull() ? QStringLiteral("") : v; };
            const QVariantList values{
                user,
                s.key,
                row,
                clean(t.id),
                clean(t.hash),
                clean(t.title),
                clean(t.artist),
                clean(t.album),
                clean(t.cover),
                clean(t.localPath),
                QString::number(t.albumId),
                QString::number(t.songId),
                qint64(t.durationMs),
                int(s.next.indexOf(t.key())),
                QJsonDocument(t.catalogMetadata).toJson(QJsonDocument::Compact),
                t.catalogPrivilege};
            for (int v = 0; v < values.size(); ++v)
                tracks.bindValue(v, values[v]);
            ok = tracks.exec();
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
    }
    if (!ok) {
        m_database.rollback();
        emit failed(tr("保存播放队列失败"));
    } else if (!m_database.commit())
        emit failed(m_database.lastError().text());
}
void PlaybackStorageWorker::record(quint64 account, const PlaybackTrack &track, qint64 listenedMs)
{
    if (!m_valid || !track.playable() || listenedMs <= 0)
        return;
    QSqlQuery q(m_database);
    q.prepare("INSERT INTO "
              "playback_history(account_id,track_key,catalog_id,hash,title,artist,cover,played_at,"
              "listened_ms,play_count,track_snapshot) VALUES(?,?,?,?,?,?,?,?,?,1,?) ON "
              "CONFLICT(account_id,track_key) DO UPDATE SET "
              "played_at=excluded.played_at,listened_ms=playback_history.listened_ms+excluded."
              "listened_ms,play_count=playback_history.play_count+1,track_snapshot=excluded.track_"
              "snapshot,title=excluded.title,artist=excluded.artist,cover=excluded.cover");
    for (const auto &v :
         QVariantList{QString::number(account), track.key(),
                      track.id.isNull() ? QStringLiteral("") : track.id,
                      track.hash.isNull() ? QStringLiteral("") : track.hash,
                      track.title.isNull() ? QStringLiteral("") : track.title,
                      track.artist.isNull() ? QStringLiteral("") : track.artist,
                      track.cover.isNull() ? QStringLiteral("") : track.cover,
                      QDateTime::currentMSecsSinceEpoch(), listenedMs,
                      QJsonDocument(track.toDomain()).toJson(QJsonDocument::Compact)})
        q.addBindValue(v);
    if (!q.exec())
        emit failed(q.lastError().text());
}
void PlaybackStorageWorker::loadHistory(quint64 account, quint64 revision)
{
    if (!m_valid) {
        emit historyFailed(tr("播放历史存储不可用"));
        return;
    }
    QSqlQuery q(m_database);
    q.prepare("SELECT track_key,played_at,listened_ms,play_count,track_snapshot "
              "FROM playback_history WHERE account_id=? ORDER BY played_at DESC LIMIT 500");
    q.addBindValue(QString::number(account));
    if (!q.exec()) {
        emit historyFailed(q.lastError().text());
        return;
    }
    QJsonArray entries;
    while (q.next()) {
        QJsonParseError error;
        const auto snapshot = QJsonDocument::fromJson(q.value(4).toByteArray(), &error);
        if (error.error != QJsonParseError::NoError || !snapshot.isObject()) {
            emit historyFailed(tr("播放历史的曲目资料已损坏"));
            return;
        }
        entries.append(QJsonObject{{"key", q.value(0).toString()},
                                   {"played_at", q.value(1).toLongLong()},
                                   {"listened_ms", q.value(2).toLongLong()},
                                   {"play_count", q.value(3).toLongLong()},
                                   {"track", snapshot.object()}});
    }
    emit historyLoaded(account, revision, entries);
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
        emit historyFailed(q.lastError().text());
        return;
    }
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
        emit historyFailed(q.lastError().text());
        return;
    }
    emit historyChanged(account);
}
void PlaybackStorageWorker::close()
{
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
        emit failed(query.lastError().text());
        return;
    }
    QJsonArray rooms;
    while (query.next()) {
        const auto value = QJsonDocument::fromJson(query.value(0).toByteArray());
        if (value.isObject())
            rooms.append(value.object());
    }
    query.finish();
    query.prepare(
        "SELECT room_id FROM listen_room_dissolved WHERE account_id=? ORDER BY ordinal LIMIT 200");
    query.addBindValue(QString::number(account));
    if (!query.exec()) {
        emit failed(query.lastError().text());
        return;
    }
    QStringList dissolved;
    while (query.next())
        dissolved.append(query.value(0).toString());
    emit roomsLoaded(account, revision, rooms, dissolved);
}
void PlaybackStorageWorker::saveRooms(quint64 account, const QJsonArray &rooms,
                                      const QStringList &dissolved)
{
    if (!m_valid || !account || !m_database.transaction())
        return;
    QSqlQuery query(m_database);
    const auto fail = [this, &query] {
        m_database.rollback();
        emit failed(query.lastError().text());
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
        emit failed(m_database.lastError().text());
}
PlaybackRepository::PlaybackRepository(const QString &path, QObject *parent)
    : QObject(parent), m_worker(new PlaybackStorageWorker)
{
    qRegisterMetaType<PlaybackQueueStates>();
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName("echo-playback-storage");
    connect(m_worker, &PlaybackStorageWorker::loaded, this, &PlaybackRepository::loaded);
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
void PlaybackRepository::load(quint64 a, quint64 r)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, a, r] { worker->load(a, r); });
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
void PlaybackRepository::loadHistory(quint64 a, quint64 r)
{
    if (!m_closing)
        QMetaObject::invokeMethod(m_worker,
                                  [worker = m_worker, a, r] { worker->loadHistory(a, r); });
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
