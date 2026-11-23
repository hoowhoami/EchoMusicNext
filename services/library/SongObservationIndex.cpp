#include "SongObservationIndex.h"
#include <QSqlError>
#include <QPointer>
#include <QSqlQuery>
#include <QUuid>

namespace EchoMusic
{
namespace
{
QString schema(QSqlDatabase &db)
{
    QSqlQuery query(db);
    for (const auto *sql :
         {"CREATE TABLE IF NOT EXISTS song_observation("
          "scope TEXT NOT NULL,owner INTEGER NOT NULL,identity TEXT NOT NULL,row_id TEXT NOT NULL,"
          "cover_alias TEXT NOT NULL,PRIMARY KEY(scope,owner,identity,row_id)) WITHOUT ROWID",
          "CREATE INDEX IF NOT EXISTS song_observation_identity ON "
          "song_observation(scope,identity,owner,row_id)",
          "CREATE INDEX IF NOT EXISTS song_observation_cover ON "
          "song_observation(scope,cover_alias,identity) WHERE cover_alias<>''"})
        if (!query.exec(QString::fromLatin1(sql)))
            return QObject::tr("初始化歌曲身份索引失败：%1").arg(query.lastError().text());
    return {};
}
QString failure(const QSqlQuery &query)
{
    return query.lastError().isValid()
               ? QObject::tr("更新歌曲身份索引失败：%1").arg(query.lastError().text())
               : QString();
}
} // namespace
SongObservationIndex::SongObservationIndex(SessionDatabase &db, QObject *parent)
    : QObject(parent), m_database(db), m_scope(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    connect(&db, &SessionDatabase::aboutToInvalidate, this, [this] {
        m_queue.clear();
        m_extensions.clear();
        m_flush.stop();
        m_busy = false;
    });
    connect(&db, &SessionDatabase::capacityAvailable, this, &SongObservationIndex::drain);
    m_flush.setSingleShot(true);
    m_flush.setInterval(0);
    connect(&m_flush, &QTimer::timeout, this, &SongObservationIndex::flushExtensions);
}
SongObservationIndex::~SongObservationIndex()
{
    m_database.discard([scope = m_scope](QSqlDatabase &db) {
        QSqlQuery query(db);
        query.prepare("DELETE FROM song_observation WHERE scope=?");
        query.addBindValue(scope);
        query.exec();
    });
}
void SongObservationIndex::enqueue(Work work, std::function<void(Result)> done, quint64 cost,
                                   quint64 owner)
{
    m_queue.enqueue({std::move(work), std::move(done), cost, owner});
    drain();
}
void SongObservationIndex::drain()
{
    if (m_busy || m_queue.isEmpty())
        return;
    const auto &pending = m_queue.head();
    m_busy = m_database.submit(
        this,
        [work = pending.work, scope = m_scope](QSqlDatabase &db, const QString &error) {
            if (!error.isEmpty())
                return Result{{}, {}, 0, 0, error};
            if (const auto setup = schema(db); !setup.isEmpty())
                return Result{{}, {}, 0, 0, setup};
            return work(db, scope);
        },
        [this](Result result) {
            auto finished = m_queue.dequeue();
            m_busy = false;
            const QPointer<SongObservationIndex> guard(this);
            if (!result.error.isEmpty())
                emit failed(result.error);
            if (!guard)
                return;
            if (finished.done)
                finished.done(std::move(result));
            if (guard)
                drain();
        },
        pending.cost);
}
int SongObservationIndex::pendingJobs() const
{
    return m_queue.size() + !m_extensions.isEmpty();
}
void SongObservationIndex::insert(quint64 owner, QList<Relation> values,
                                  std::function<void(Result)> done)
{
    quint64 cost = 0;
    for (const auto &value : values)
        cost +=
            64 + 2 * quint64(value.identity.size() + value.row.size() + value.coverAlias.size());
    enqueue(
        [owner, values = std::move(values)](QSqlDatabase &db, const QString &scope) {
            Result result;
            if (!db.transaction()) {
                result.error = db.lastError().text();
                return result;
            }
            QSqlQuery query(db);
            query.prepare(
                "INSERT OR IGNORE INTO song_observation(scope,owner,identity,row_id,cover_alias) "
                "VALUES(?,?,?,?,?)");
            for (const auto &value : values) {
                query.bindValue(0, scope);
                query.bindValue(1, qulonglong(owner));
                query.bindValue(2, value.identity);
                query.bindValue(3, value.row);
                query.bindValue(4,
                                value.coverAlias.isNull() ? QStringLiteral("") : value.coverAlias);
                if (!query.exec()) {
                    result.error = failure(query);
                    break;
                }
            }
            if (result.error.isEmpty() && !db.commit())
                result.error = db.lastError().text();
            if (!result.error.isEmpty())
                db.rollback();
            return result;
        },
        std::move(done), cost, owner);
}
void SongObservationIndex::erase(quint64 owner)
{
    // A route can be replaced before admission. Retire its queued bindings
    // instead of retaining obsolete full-catalog input behind a busy database.
    for (int i = m_queue.size() - 1; i >= (m_busy ? 1 : 0); --i)
        if (m_queue[i].owner == owner)
            m_queue.removeAt(i);
    enqueue([owner](QSqlDatabase &db, const QString &scope) {
        QSqlQuery query(db);
        query.prepare("DELETE FROM song_observation WHERE scope=? AND owner=?");
        query.addBindValue(scope);
        query.addBindValue(qulonglong(owner));
        query.exec();
        return Result{{}, {}, 0, 0, failure(query)};
    });
}
void SongObservationIndex::clear()
{
    m_flush.stop();
    m_extensions.clear();
    for (int i = m_queue.size() - 1; i >= (m_busy ? 1 : 0); --i)
        if (m_queue[i].owner)
            m_queue.removeAt(i);
    enqueue([](QSqlDatabase &db, const QString &scope) {
        QSqlQuery query(db);
        query.prepare("DELETE FROM song_observation WHERE scope=?");
        query.addBindValue(scope);
        query.exec();
        return Result{{}, {}, 0, 0, failure(query)};
    });
}
void SongObservationIndex::extend(const QStringList &identities, const QString &artist)
{
    auto &keys = m_extensions[artist];
    for (const auto &key : identities)
        keys.insert(key);
    if (!m_flush.isActive())
        m_flush.start();
}
void SongObservationIndex::flushExtensions()
{
    m_flush.stop();
    if (m_extensions.isEmpty())
        return;
    enqueue([extensions = std::exchange(m_extensions, {})](QSqlDatabase &db, const QString &scope) {
        Result result;
        if (!db.transaction()) {
            result.error = db.lastError().text();
            return result;
        }
        QSqlQuery query(db);
        query.prepare(
            "INSERT OR IGNORE INTO song_observation(scope,owner,identity,row_id,cover_alias)"
            " SELECT scope,owner,?,row_id,'' FROM song_observation WHERE scope=? AND identity=?");
        for (auto it = extensions.cbegin(); it != extensions.cend(); ++it)
            for (const auto &identity : it.value()) {
                query.bindValue(0, it.key());
                query.bindValue(1, scope);
                query.bindValue(2, identity);
                if (!query.exec()) {
                    result.error = failure(query);
                    break;
                }
            }
        if (result.error.isEmpty() && !db.commit())
            result.error = db.lastError().text();
        if (!result.error.isEmpty())
            db.rollback();
        return result;
    });
}
void SongObservationIndex::lookup(QStringList keys, std::function<void(Result)> done)
{
    flushExtensions();
    enqueue(
        [keys = std::move(keys)](QSqlDatabase &db, const QString &scope) {
            Result result;
            QHash<quint64, QSet<QString>> rows;
            QSqlQuery query(db);
            query.prepare("SELECT owner,row_id FROM song_observation WHERE scope=? AND identity=?");
            for (const auto &key : keys) {
                query.bindValue(0, scope);
                query.bindValue(1, key);
                if (!query.exec()) {
                    result.error = failure(query);
                    break;
                }
                while (query.next())
                    rows[query.value(0).toULongLong()].insert(query.value(1).toString());
                if (query.lastError().isValid()) {
                    result.error = failure(query);
                    break;
                }
            }
            for (auto it = rows.cbegin(); it != rows.cend(); ++it)
                result.rows.insert(it.key(), QStringList(it->cbegin(), it->cend()));
            return result;
        },
        std::move(done));
}
void SongObservationIndex::lookupAliases(QStringList keys, std::function<void(Result)> done)
{
    enqueue(
        [keys = std::move(keys)](QSqlDatabase &db, const QString &scope) {
            Result result;
            QSet<QString> identities;
            QSqlQuery query(db);
            query.prepare("SELECT DISTINCT identity FROM song_observation WHERE scope=? AND "
                          "cover_alias=? AND cover_alias<>''");
            for (const auto &key : keys) {
                query.bindValue(0, scope);
                query.bindValue(1, key);
                if (!query.exec()) {
                    result.error = failure(query);
                    break;
                }
                while (query.next())
                    identities.insert(query.value(0).toString());
            }
            result.keys = QStringList(identities.cbegin(), identities.cend());
            return result;
        },
        std::move(done));
}
void SongObservationIndex::allKeys(std::function<void(Result)> done)
{
    enqueue(
        [](QSqlDatabase &db, const QString &scope) {
            Result result;
            QSqlQuery query(db);
            query.prepare("SELECT DISTINCT identity FROM song_observation WHERE scope=?");
            query.addBindValue(scope);
            if (!query.exec())
                result.error = failure(query);
            while (query.next())
                result.keys.append(query.value(0).toString());
            return result;
        },
        std::move(done));
}
void SongObservationIndex::setAliases(QList<QPair<QString, QString>> aliases)
{
    enqueue([aliases = std::move(aliases)](QSqlDatabase &db, const QString &scope) {
        Result result;
        if (!db.transaction()) {
            result.error = db.lastError().text();
            return result;
        }
        QSqlQuery query(db);
        query.prepare("UPDATE song_observation SET cover_alias=? WHERE scope=? AND identity=?");
        for (const auto &pair : aliases) {
            query.bindValue(0, pair.second);
            query.bindValue(1, scope);
            query.bindValue(2, pair.first);
            if (!query.exec()) {
                result.error = failure(query);
                break;
            }
        }
        if (result.error.isEmpty() && !db.commit())
            result.error = db.lastError().text();
        if (!result.error.isEmpty())
            db.rollback();
        return result;
    });
}
void SongObservationIndex::statistics(std::function<void(Result)> done)
{
    flushExtensions();
    enqueue(
        [](QSqlDatabase &db, const QString &scope) {
            Result result;
            QSqlQuery query(db);
            query.prepare(
                "SELECT count(*),count(DISTINCT identity) FROM song_observation WHERE scope=?");
            query.addBindValue(scope);
            if (!query.exec() || !query.next())
                result.error = failure(query);
            else {
                result.relations = query.value(0).toLongLong();
                result.identities = query.value(1).toLongLong();
            }
            return result;
        },
        std::move(done));
}
} // namespace EchoMusic
