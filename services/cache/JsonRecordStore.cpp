#include "JsonRecordStore.h"
#include "CacheCost.h"
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <QTimer>
#include <QUuid>

namespace EchoMusic
{
struct JsonRecordStore::State
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QPointer<SessionDatabase> database;
    QJsonArray staging;
    int size = 0, written = 0;
    bool failed = false, complete = false, touched = false;
    ~State()
    {
        if (database && touched) {
            Q_ASSERT(QThread::currentThread() == database->thread());
            database->discard([id = id](QSqlDatabase &db) {
                QSqlQuery q(db);
                q.prepare("DELETE FROM json_record WHERE generation=?");
                q.addBindValue(id);
                // No table exists if all records were retired before admission.
                q.exec();
            });
        }
    }
};
JsonRecordStore::JsonRecordStore(SessionDatabase &db, QObject *parent)
    : QObject(parent), m_database(db)
{
    connect(&db, &SessionDatabase::capacityAvailable, this, &JsonRecordStore::flush);
}
JsonRecordStore::Snapshot JsonRecordStore::create(QJsonArray rows)
{
    auto s = std::make_shared<State>();
    s->database = &m_database;
    s->size = int(rows.size());
    s->staging = std::move(rows);
    if (!s->size)
        s->complete = true;
    else {
        m_pending.append(s);
        QTimer::singleShot(0, this, &JsonRecordStore::flush);
    }
    return s;
}
int JsonRecordStore::count(const Snapshot &s) const
{
    return s ? s->size : 0;
}
bool JsonRecordStore::persisted(const Snapshot &s) const
{
    return s && s->complete;
}
void JsonRecordStore::flush()
{
    if (m_writing)
        return;
    while (!m_pending.isEmpty()) {
        auto s = m_pending.first().lock();
        if (!s || s->failed || s->complete) {
            m_pending.removeFirst();
            continue;
        }
        QJsonArray rows;
        quint64 cost = 0;
        const int start = s->written;
        for (int i = start; i < s->size && rows.size() < 64; ++i) {
            const auto bytes = jsonValueCost(s->staging[i]);
            if (!rows.isEmpty() && cost + bytes > 512 * 1024)
                break;
            rows.append(s->staging[i]);
            cost += bytes;
        }
        if (cost > 8 * 1024 * 1024) {
            s->failed = true;
            emit cacheFailed(tr("单条缓存记录过大，已保留原始数据"));
            continue;
        }
        const auto generation = s->id;
        m_writing = m_database.submit(
            this,
            [generation, start, rows](QSqlDatabase &db, const QString &error) {
                if (!error.isEmpty())
                    return error;
                QSqlQuery q(db);
                if (!q.exec("CREATE TABLE IF NOT EXISTS json_record(generation TEXT NOT NULL,"
                            "ordinal INTEGER NOT NULL,payload BLOB NOT NULL,"
                            "PRIMARY KEY(generation,ordinal)) WITHOUT ROWID"))
                    return q.lastError().text();
                if (!db.transaction())
                    return db.lastError().text();
                q.prepare("INSERT INTO json_record VALUES(?,?,?)");
                for (int i = 0; i < rows.size(); ++i) {
                    q.bindValue(0, generation);
                    q.bindValue(1, start + i);
                    q.bindValue(2,
                                QJsonDocument(rows[i].toObject()).toJson(QJsonDocument::Compact));
                    if (!q.exec()) {
                        const auto failure = q.lastError().text();
                        db.rollback();
                        return failure;
                    }
                }
                if (db.commit())
                    return QString();
                const auto failure = db.lastError().text();
                db.rollback();
                return failure;
            },
            [this, s, written = int(rows.size())](QString error) {
                m_writing = false;
                if (!error.isEmpty()) {
                    s->failed = true;
                    emit cacheFailed(tr("缓存保存失败：%1").arg(error));
                } else {
                    s->written += written;
                    if (s->written == s->size) {
                        s->complete = true;
                        s->staging = {};
                        emit stored();
                    }
                }
                flush();
            },
            cost);
        s->touched = s->touched || m_writing;
        return;
    }
}
void JsonRecordStore::read(Reference r, QObject *consumer, Completion done, int limit)
{
    readRange(std::move(r.snapshot), r.row, qBound(1, limit, 64), consumer, std::move(done));
}
void JsonRecordStore::readAll(Snapshot snapshot, QObject *consumer, Completion done)
{
    if (!snapshot) {
        done({}, tr("缓存记录不存在"));
        return;
    }
    struct Reader
    {
        QPointer<JsonRecordStore> store;
        QPointer<QObject> consumer;
        Snapshot snapshot;
        QJsonArray rows;
        Completion done;
    };
    auto reader =
        std::make_shared<Reader>(Reader{this, consumer, std::move(snapshot), {}, std::move(done)});
    auto next = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = next;
    *next = [reader, weak] {
        const auto next = weak.lock();
        if (!next || !reader->store || !reader->consumer)
            return;
        if (reader->rows.size() == reader->store->count(reader->snapshot)) {
            reader->done(std::move(reader->rows), {});
            return;
        }
        reader->store->read(
            {reader->snapshot, int(reader->rows.size())}, reader->consumer,
            [reader, next](QJsonArray rows, QString error) {
                if (error.isEmpty() && rows.isEmpty())
                    error = tr("缓存记录不完整");
                if (!error.isEmpty()) {
                    reader->done({}, error);
                    return;
                }
                for (const auto &row : rows)
                    reader->rows.append(row);
                QTimer::singleShot(0, reader->consumer, [next] { (*next)(); });
            },
            64);
    };
    if (consumer)
        QTimer::singleShot(0, consumer, [next] { (*next)(); });
}
void JsonRecordStore::readRange(Snapshot s, int start, int limit, QObject *consumer,
                                Completion done)
{
    if (!consumer)
        return;
    if (!s || start < 0 || start > s->size || limit < 0) {
        done({}, tr("缓存记录不存在"));
        return;
    }
    const int amount = qMin(limit, s->size - start);
    if (!s->complete || !amount) {
        QJsonArray result;
        quint64 cost = 0;
        for (int i = start; i < start + amount; ++i) {
            const auto bytes = jsonValueCost(s->staging[i]);
            if (!result.isEmpty() && cost + bytes > 512 * 1024)
                break;
            result.append(s->staging[i]);
            cost += bytes;
        }
        done(std::move(result), {});
        return;
    }
    auto work = [id = s->id, start, amount](QSqlDatabase &db, const QString &error) {
        QPair<QJsonArray, QString> result{{}, error};
        if (!error.isEmpty())
            return result;
        QSqlQuery q(db);
        q.prepare("SELECT ordinal,payload FROM json_record WHERE generation=? AND ordinal>=? "
                  "AND ordinal<? ORDER BY ordinal");
        q.addBindValue(id);
        q.addBindValue(start);
        q.addBindValue(start + amount);
        if (!q.exec()) {
            result.second = q.lastError().text();
            return result;
        }
        qsizetype bytes = 0;
        bool limited = false;
        while (q.next()) {
            if (q.value(0).toInt() != start + result.first.size()) {
                result.second = tr("缓存记录不完整");
                return result;
            }
            const auto payload = q.value(1).toByteArray();
            if (!result.first.isEmpty() && bytes + payload.size() > 512 * 1024) {
                limited = true;
                break;
            }
            bytes += payload.size();
            QJsonParseError parse;
            const auto doc = QJsonDocument::fromJson(payload, &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
                result.second = tr("缓存记录损坏");
                return result;
            }
            result.first.append(doc.object());
        }
        if (!limited && result.first.size() != amount)
            result.second = tr("缓存记录不完整");
        return result;
    };
    // A capacity wait owns no SQL work and is cancelled with its consumer.
    auto finish = [s, done](QPair<QJsonArray, QString> result) mutable {
        done(std::move(result.first), std::move(result.second));
    };
    if (!m_database.submit(consumer, std::move(work), std::move(finish))) {
        auto connection = std::make_shared<QMetaObject::Connection>();
        QPointer<QObject> guard(consumer);
        QPointer<JsonRecordStore> owner(this);
        *connection = connect(&m_database, &SessionDatabase::capacityAvailable, consumer,
                              [owner, s, start, amount, guard, done, connection] {
                                  disconnect(*connection);
                                  if (owner && guard)
                                      owner->readRange(s, start, amount, guard, done);
                              });
    }
}
} // namespace EchoMusic
