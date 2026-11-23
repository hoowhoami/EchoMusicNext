#include "AudioEffectCatalogCache.h"
#include "AudioEffectSearch.h"
#include "CacheCost.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QTimer>
#include <QDateTime>
#include <QLoggingCategory>
#include <utility>

namespace EchoMusic
{
Q_LOGGING_CATEGORY(effectCacheLog, "echo.cache.effects")
namespace
{
QByteArray encode(const QJsonObject &value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
QJsonObject decode(const QVariant &value)
{
    return QJsonDocument::fromJson(value.toByteArray()).object();
}
QJsonObject publicRow(const QJsonObject &row)
{
    QJsonObject result;
    for (const auto *field : {"id", "source", "name", "author", "intro", "labels", "icon_url",
                              "model_count", "user_count", "aliases"})
        if (row.contains(field))
            result[field] = row[field];
    return result;
}
QByteArray searchRow(const QJsonObject &row)
{
    auto value = row.toVariantMap();
    AudioEffectSearch::prepare(value);
    QJsonObject result;
    for (const auto *key : {"_names", "_author", "_labels", "_intro"})
        result[key] = QJsonValue::fromVariant(value.value(key));
    return encode(result);
}
} // namespace
struct AudioEffectCatalogCache::State
{
    QSqlDatabase db;
    QString path, connection = "effect-catalog-" + QUuid::createUuid().toString(), view;
    quint64 projection = 0, baseProjection = 0;
    QString baseView;
    bool valid = false, reset = false;
    QString error;
    bool exec(const QString &sql)
    {
        QSqlQuery query(db);
        if (query.exec(sql))
            return true;
        error = query.lastError().text();
        return false;
    }
    bool open()
    {
        if (!path.isEmpty())
            QDir().mkpath(QFileInfo(path).absolutePath());
        if (!db.isValid())
            db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path.isEmpty() ? ":memory:" : path);
        if (!db.open()) {
            error = db.lastError().text();
            return false;
        }
        QSqlQuery version(db);
        if (!version.exec("PRAGMA user_version") || !version.next()) {
            error = version.lastError().text();
            return false;
        }
        const int schema = version.value(0).toInt();
        version.finish();
        if (schema > 2) {
            error = QObject::tr("目录数据库版本高于当前应用支持的版本");
            return false;
        }
        for (const auto *sql :
             {"PRAGMA journal_mode=WAL", "PRAGMA max_page_count=16384", "PRAGMA cache_size=-2048",
              "PRAGMA temp_store=FILE",
              "CREATE TABLE IF NOT EXISTS effect_scopes(scope TEXT PRIMARY KEY, metadata BLOB NOT "
              "NULL)",
              "CREATE TABLE IF NOT EXISTS effect_entries(scope TEXT NOT NULL, identity TEXT NOT "
              "NULL, generation TEXT NOT NULL, ordinal INTEGER NOT NULL, metadata BLOB NOT NULL, "
              "search BLOB, PRIMARY KEY(scope,identity))",
              "CREATE INDEX IF NOT EXISTS effect_order ON effect_entries(scope,ordinal)",
              "CREATE INDEX IF NOT EXISTS effect_generation ON effect_entries(scope,generation)",
              "CREATE TEMP TABLE IF NOT EXISTS effect_projection(position INTEGER PRIMARY KEY, "
              "identity TEXT NOT NULL UNIQUE)",
              "CREATE TEMP TABLE IF NOT EXISTS effect_base(position INTEGER PRIMARY KEY, identity "
              "TEXT NOT NULL UNIQUE)",
              "CREATE TEMP TABLE IF NOT EXISTS effect_next(position INTEGER PRIMARY KEY, identity "
              "TEXT NOT NULL UNIQUE)",
              "CREATE TEMP TABLE IF NOT EXISTS effect_matches(identity TEXT PRIMARY KEY, score "
              "INTEGER, ordinal INTEGER, source_row INTEGER)"})
            if (!exec(QString::fromLatin1(sql)))
                return false;
        if (!db.transaction()) {
            error = db.lastError().text();
            return false;
        }
        const auto migrate = [&] {
            if (schema == 1 && !exec("ALTER TABLE effect_entries ADD COLUMN search BLOB"))
                return false;
            // Keyset batches release the read cursor before updating its rows.
            // Schema, normalized search fields and version commit together.
            qint64 cursor = 0;
            for (;;) {
                QSqlQuery rows(db), update(db);
                rows.prepare("SELECT rowid,metadata FROM effect_entries WHERE search IS NULL AND "
                             "rowid>? ORDER BY rowid LIMIT 32");
                rows.addBindValue(cursor);
                if (!rows.exec()) {
                    error = rows.lastError().text();
                    return false;
                }
                QList<QPair<qint64, QJsonObject>> pending;
                quint64 cost = 0;
                while (rows.next()) {
                    auto value = decode(rows.value(1));
                    const auto bytes = jsonValueCost(value);
                    if (!pending.isEmpty() && cost + bytes > 512 * 1024)
                        break;
                    cost += bytes;
                    pending.append({rows.value(0).toLongLong(), value});
                }
                if (rows.lastError().isValid()) {
                    error = rows.lastError().text();
                    return false;
                }
                rows.finish();
                if (pending.isEmpty())
                    break;
                for (const auto &[id, value] : pending) {
                    update.prepare("UPDATE effect_entries SET search=? WHERE rowid=?");
                    update.addBindValue(searchRow(value));
                    update.addBindValue(id);
                    if (!update.exec()) {
                        error = update.lastError().text();
                        return false;
                    }
                    cursor = id;
                }
            }
            return exec("PRAGMA user_version=2");
        };
        if (!migrate() || !db.commit()) {
            if (error.isEmpty())
                error = db.lastError().text();
            db.rollback();
            return false;
        }
        valid = true;
        return valid;
    }
    bool ensure()
    {
        error.clear();
        if (valid && db.isOpen() && (path.isEmpty() || QFileInfo::exists(path)))
            return true;
        if (valid) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            // Preserve an unlinked but still readable source before closing it.
            bool copied = !db.isOpen() && QFileInfo::exists(path);
            if (db.isOpen()) {
                QSqlQuery copy(db);
                copy.prepare("VACUUM main INTO ?");
                copy.addBindValue(path);
                copied = copy.exec();
                if (!copied) {
                    const int code = copy.lastError().nativeErrorCode().toInt() & 255;
                    if (code != 8 && code != 10 && code != 11 && code != 26) {
                        error = copy.lastError().text();
                        return false;
                    }
                }
            }
            db.close();
            valid = false;
            projection = 0;
            baseProjection = 0;
            view.clear();
            baseView.clear();
            reset = true;
            if (!copied) {
                QFile::remove(path);
                qCWarning(effectCacheLog)
                    << "Effect cache source lost; restarting directory pagination";
            }
        }
        return open();
    }
    QJsonObject scopes()
    {
        QJsonObject result;
        QSqlQuery query(db);
        if (!query.exec(
                "SELECT s.scope,s.metadata,COUNT(e.identity) FROM effect_scopes s LEFT JOIN "
                "effect_entries e ON e.scope=s.scope GROUP BY s.scope ORDER BY s.scope LIMIT 64")) {
            error = query.lastError().text();
            return result;
        }
        while (query.next()) {
            auto metadata = decode(query.value(1));
            const int count = query.value(2).toInt();
            if (metadata.value("count").toInt(-1) != count) {
                metadata["complete"] = false;
                metadata["limited"] = false;
                metadata["nextPage"] = 1;
                metadata["updated"] = 0;
                metadata["generation"] = QUuid::createUuid().toString();
            }
            metadata["count"] = count;
            result[query.value(0).toString()] = metadata;
        }
        if (query.lastError().isValid())
            error = query.lastError().text();
        return result;
    }
    bool changes(QList<Projection::Change> &changes, const std::atomic_bool &canceled)
    {
        using Change = Projection::Change;
        const auto run = [&](QString sql, QVariantList args = QVariantList{}) {
            QSqlQuery q(db);
            q.prepare(sql);
            for (const auto &arg : args)
                q.addBindValue(arg);
            if (q.exec())
                return true;
            error = q.lastError().text();
            return false;
        };
        const auto shift = [&](int first, int last, int delta) {
            return run("UPDATE effect_projection SET position=-position-1 WHERE position>=? AND "
                       "position<=?",
                       {first, last}) &&
                   run("UPDATE effect_projection SET position=-position-1+? WHERE position>=? AND "
                       "position<=?",
                       {delta, -qint64(last) - 1, -qint64(first) - 1});
        };
        QSqlQuery missing(db);
        if (!missing.exec(
                "SELECT p.position FROM effect_projection p LEFT JOIN effect_next n ON "
                "n.identity=p.identity WHERE n.identity IS NULL ORDER BY p.position DESC")) {
            error = missing.lastError().text();
            return false;
        }
        while (missing.next()) {
            if (canceled.load())
                return false;
            const int position = missing.value(0).toInt();
            if (!changes.isEmpty() && changes.last().first == position + 1)
                changes.last().first = position;
            else
                changes.append({Change::Remove, position, position});
            if (changes.size() > 256)
                return false;
        }
        if (missing.lastError().isValid()) {
            error = missing.lastError().text();
            return false;
        }
        missing.finish();
        for (const auto &change : changes)
            if (!run("DELETE FROM effect_projection WHERE position>=? AND position<=?",
                     {change.first, change.last}) ||
                !shift(change.last + 1, 2147483647, change.first - change.last - 1))
                return false;
        QSqlQuery wanted(db), location(db);
        if (!wanted.exec("SELECT position,identity FROM effect_next ORDER BY position")) {
            error = wanted.lastError().text();
            return false;
        }
        while (wanted.next()) {
            if (canceled.load())
                return false;
            const int target = wanted.value(0).toInt();
            const auto key = wanted.value(1);
            location.prepare("SELECT position FROM effect_projection WHERE identity=?");
            location.addBindValue(key);
            if (!location.exec()) {
                error = location.lastError().text();
                return false;
            }
            const bool found = location.next();
            const int old = found ? location.value(0).toInt() : -1;
            location.finish();
            if (old == target)
                continue;
            if (changes.size() >= 256)
                return false;
            if (!found) {
                if (!shift(target, 2147483647, 1) ||
                    !run("INSERT INTO effect_projection VALUES(?,?)", {target, key}))
                    return false;
                changes.append({Change::Insert, target, target});
            } else {
                if (old < target)
                    return false;
                if (!run("UPDATE effect_projection SET position=-1000000000 WHERE identity=?",
                         {key}) ||
                    !shift(target, old - 1, 1) ||
                    !run("UPDATE effect_projection SET position=? WHERE identity=?", {target, key}))
                    return false;
                changes.append({Change::Move, old, old, target});
            }
        }
        if (wanted.lastError().isValid()) {
            error = wanted.lastError().text();
            return false;
        }
        return true;
    }
    QJsonObject record(QSqlQuery &query, const QString &scope)
    {
        auto row = decode(query.value(0));
        row["key"] = scope + '|' + query.value(1).toString();
        row["_generation"] = query.value(2).toString();
        row["_ordinal"] = query.value(3).toInt();
        return row;
    }
};
AudioEffectCatalogCache::AudioEffectCatalogCache(QString path, QObject *parent)
    : QObject(parent), m_worker(new QObject(this)), m_state(std::make_shared<State>())
{
    m_state->path = std::move(path);
    m_thread.setObjectName("echo-effect-catalog-cache");
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, &AudioEffectCatalogCache::closed);
}
AudioEffectCatalogCache::~AudioEffectCatalogCache()
{
    shutdown();
    m_thread.wait();
}
void AudioEffectCatalogCache::work(std::function<void(State &)> task)
{
    if (m_closing)
        return;
    if (!m_thread.isRunning()) {
        m_worker->setParent(nullptr);
        m_worker->moveToThread(&m_thread);
        m_thread.start();
    }
    QMetaObject::invokeMethod(m_worker, [this, state = m_state, task = std::move(task)] {
        state->ensure();
        if (std::exchange(state->reset, false)) {
            emit invalidated();
            const auto values = state->valid ? state->scopes() : QJsonObject{};
            if (!state->error.isEmpty())
                emit failed(tr("音效目录缓存不可用：%1").arg(state->error));
            emit loaded(values);
        }
        task(*state);
    });
}
void AudioEffectCatalogCache::load()
{
    if (m_closing || m_thread.isRunning())
        return;
    work([this](State &s) {
        const auto values = s.error.isEmpty() ? s.scopes() : QJsonObject{};
        if (!s.error.isEmpty())
            emit failed(tr("音效目录缓存不可用：%1").arg(s.error));
        emit loaded(values);
    });
}
void AudioEffectCatalogCache::ingest(QString scope, QJsonObject metadata, QJsonArray rows,
                                     std::function<void(QJsonObject, QJsonArray, QString)> done)
{
    work([this, scope = std::move(scope), metadata = std::move(metadata), rows = std::move(rows),
          done = std::move(done)](State &s) mutable {
        QJsonArray changed;
        auto error = s.error;
        const auto generation = metadata.value("generation").toString();
        QSqlQuery count(s.db), select(s.db), insert(s.db);
        int seen = 0, novel = 0;
        if (error.isEmpty() && s.db.transaction()) {
            count.prepare("SELECT COUNT(*) FROM effect_entries WHERE scope=? AND generation=?");
            count.addBindValue(scope);
            count.addBindValue(generation);
            if (count.exec() && count.next())
                seen = count.value(0).toInt();
            else
                error = count.lastError().text();
            count.finish();
            for (const auto &value : rows) {
                if (!error.isEmpty())
                    break;
                auto row = value.toObject();
                if (scope == "headphone/0")
                    row["source"] = "brand";
                const auto id =
                    row.value("source").toString() + '-' + row.value("id").toVariant().toString();
                select.prepare("SELECT metadata,generation,ordinal FROM effect_entries WHERE "
                               "scope=? AND identity=?");
                select.addBindValue(scope);
                select.addBindValue(id);
                if (!select.exec()) {
                    error = select.lastError().text();
                    break;
                }
                const bool existed = select.next();
                const auto old = existed ? decode(select.value(0)) : QJsonObject{};
                const bool same = existed && select.value(1).toString() == generation;
                const int ordinal = same ? select.value(2).toInt() : seen++;
                select.finish();
                if (!same)
                    ++novel;
                auto aliases = row.value("aliases").toVariant().toStringList();
                if (same) {
                    const auto oldAliases = old.value("aliases").toVariant().toStringList();
                    const auto oldName = old.value("name").toString();
                    if (scope.startsWith("headphone/") && scope != "headphone/0") {
                        if (oldName != row.value("name").toString() &&
                            !oldAliases.contains(row.value("name").toString()))
                            ++novel;
                        aliases.append(oldName);
                    }
                    aliases.append(oldAliases);
                }
                aliases.removeAll(row.value("name").toString());
                aliases.removeDuplicates();
                row["aliases"] = QJsonArray::fromStringList(aliases);
                const auto publicValue = publicRow(row);
                insert.prepare(
                    "INSERT INTO effect_entries(scope,identity,generation,ordinal,metadata,search) "
                    "VALUES(?,?,?,?,?,?) ON CONFLICT(scope,identity) DO UPDATE SET "
                    "generation=excluded.generation,ordinal=excluded.ordinal,metadata=excluded."
                    "metadata,search=excluded.search");
                insert.addBindValue(scope);
                insert.addBindValue(id);
                insert.addBindValue(generation);
                insert.addBindValue(ordinal);
                insert.addBindValue(encode(publicValue));
                insert.addBindValue(searchRow(publicValue));
                if (!insert.exec()) {
                    error = insert.lastError().text();
                    break;
                }
                row["key"] = scope + '|' + id;
                row["_generation"] = generation;
                row["_ordinal"] = ordinal;
                changed.append(row);
            }
            metadata["nextPage"] = metadata.value("nextPage").toInt(1) + 1;
            if (rows.isEmpty()) {
                metadata["complete"] = true;
                metadata["updated"] = QDateTime::currentMSecsSinceEpoch();
                select.prepare("DELETE FROM effect_entries WHERE scope=? AND generation<>?");
                select.addBindValue(scope);
                select.addBindValue(generation);
                if (!select.exec())
                    error = select.lastError().text();
            } else if (!novel) {
                metadata["nextPage"] = metadata.value("nextPage").toInt() - 1;
                metadata["error"] = tr("服务重复返回相同目录，已暂停；可刷新重试");
            }
            count.prepare("SELECT COUNT(*) FROM effect_entries WHERE scope=?");
            count.addBindValue(scope);
            if (count.exec() && count.next())
                metadata["count"] = count.value(0).toInt();
            else
                error = count.lastError().text();
            count.finish();
            if (metadata.value("count").toInt() >= 10000 ||
                metadata.value("nextPage").toInt() > 1000)
                metadata["limited"] = true;
            select.prepare("INSERT OR REPLACE INTO effect_scopes VALUES(?,?)");
            select.addBindValue(scope);
            auto savedMetadata = metadata;
            savedMetadata.remove("error");
            select.addBindValue(encode(savedMetadata));
            if (!select.exec())
                error = select.lastError().text();
            if (!error.isEmpty() || !s.db.commit()) {
                if (error.isEmpty())
                    error = s.db.lastError().text();
                s.db.rollback();
            }
        } else if (error.isEmpty())
            error = s.db.lastError().text();
        QMetaObject::invokeMethod(this, [done = std::move(done), metadata, changed,
                                         error]() mutable { done(metadata, changed, error); });
    });
}
void AudioEffectCatalogCache::project(QString scope, QString view, QStringList terms,
                                      bool relevance, quint64 generation, quint64 baseGeneration,
                                      QString anchor, std::shared_ptr<std::atomic_bool> canceled,
                                      std::function<void(Projection)> done)
{
    work([this, scope = std::move(scope), view = std::move(view), terms = std::move(terms),
          relevance, generation, baseGeneration, anchor = std::move(anchor),
          canceled = std::move(canceled), done = std::move(done)](State &s) mutable {
        Projection result;
        if (!s.error.isEmpty())
            result.error = s.error;
        else if (!canceled->load()) {
            // Keep one acknowledged base in SQL. A canceled intermediate
            // projection must not turn a metadata update into a GUI reset.
            if (s.projection == baseGeneration) {
                s.exec("DELETE FROM effect_base");
                s.exec("INSERT INTO effect_base SELECT * FROM effect_projection");
                s.baseProjection = baseGeneration;
                s.baseView = s.view;
            }
            const bool sameBase = s.baseProjection == baseGeneration && s.baseView == view;
            s.exec("DELETE FROM effect_projection");
            if (s.baseProjection == baseGeneration)
                s.exec("INSERT INTO effect_projection SELECT * FROM effect_base");
            s.exec("DELETE FROM effect_next");
            s.exec("DELETE FROM effect_matches");
            QSqlQuery rows(s.db), insert(s.db);
            if (terms.isEmpty()) {
                insert.prepare("INSERT INTO effect_next SELECT ROW_NUMBER() OVER(ORDER BY "
                               "ordinal,rowid)-1,identity FROM effect_entries WHERE scope=?");
                insert.addBindValue(scope);
                if (!insert.exec())
                    s.error = insert.lastError().text();
            } else {
                rows.prepare("SELECT identity,ordinal,rowid,search FROM effect_entries WHERE "
                             "scope=? ORDER BY ordinal,rowid");
                rows.addBindValue(scope);
                if (!rows.exec())
                    s.error = rows.lastError().text();
                while (rows.next() && !canceled->load()) {
                    const int rank =
                        AudioEffectSearch::rank(decode(rows.value(3)).toVariantMap(), terms);
                    if (rank < 0)
                        continue;
                    insert.prepare("INSERT INTO effect_matches VALUES(?,?,?,?)");
                    insert.addBindValue(rows.value(0));
                    insert.addBindValue(rank);
                    insert.addBindValue(rows.value(1));
                    insert.addBindValue(rows.value(2));
                    if (!insert.exec()) {
                        s.error = insert.lastError().text();
                        break;
                    }
                }
                if (rows.lastError().isValid())
                    s.error = rows.lastError().text();
                if (!canceled->load() &&
                    !s.exec(QString("INSERT INTO effect_next SELECT ROW_NUMBER() OVER(ORDER BY "
                                    "%1ordinal,source_row)-1,identity FROM effect_matches")
                                .arg(relevance ? "score," : "")))
                    result.error = s.error;
            }
            if (!canceled->load() && s.error.isEmpty()) {
                rows.exec("SELECT COUNT(*) FROM effect_next");
                rows.next();
                result.count = rows.value(0).toInt();
                rows.finish();
                rows.exec("SELECT (SELECT COUNT(*) FROM effect_projection)<=(SELECT COUNT(*) FROM "
                          "effect_next) AND NOT EXISTS(SELECT 1 FROM effect_projection p LEFT JOIN "
                          "effect_next n ON n.position=p.position WHERE n.identity IS NULL OR "
                          "n.identity<>p.identity)");
                rows.next();
                result.append = sameBase && rows.value(0).toBool();
                rows.finish();
                result.incremental =
                    result.append || (sameBase && s.changes(result.changes, *canceled));
                if (!result.incremental)
                    result.changes.clear();
                rows.finish();
                if (!anchor.isEmpty()) {
                    rows.prepare("SELECT position FROM effect_next WHERE identity=?");
                    rows.addBindValue(anchor.section('|', 1));
                    if (rows.exec() && rows.next())
                        result.anchor = rows.value(0).toInt();
                    rows.finish();
                }
                s.exec("DELETE FROM effect_projection");
                s.exec("INSERT INTO effect_projection SELECT * FROM effect_next");
                s.view = view;
                s.projection = generation;
            }
            result.error = s.error;
            if (!s.error.isEmpty()) {
                s.projection = 0;
                s.view.clear();
            }
        }
        QMetaObject::invokeMethod(this,
                                  [done = std::move(done), result]() mutable { done(result); });
    });
}
void AudioEffectCatalogCache::read(quint64 generation, int first, Rows done)
{
    work([this, generation, first, done = std::move(done)](State &s) mutable {
        QJsonArray result;
        auto error = s.error;
        const bool latest = generation == s.projection;
        const bool base = generation == s.baseProjection;
        if (error.isEmpty() && (latest || base)) {
            const auto view = latest ? s.view : s.baseView;
            const auto scope = view.section('?', 0, 0);
            const auto table = latest ? QString("effect_projection") : QString("effect_base");
            QSqlQuery rows(s.db);
            rows.prepare(
                QString("SELECT e.metadata,p.identity,e.generation,e.ordinal FROM %1 p LEFT JOIN "
                        "effect_entries e ON e.identity=p.identity AND e.scope=? WHERE "
                        "p.position>=? AND p.position<? ORDER BY p.position")
                    .arg(table));
            rows.addBindValue(scope);
            rows.addBindValue(first);
            rows.addBindValue(first + 32);
            quint64 cost = 0;
            if (rows.exec())
                while (rows.next()) {
                    auto row = s.record(rows, scope);
                    if (rows.value(0).isNull())
                        row["_loading"] = true;
                    const auto bytes = jsonValueCost(row);
                    if (!result.isEmpty() && cost + bytes > 512 * 1024)
                        break;
                    cost += bytes;
                    result.append(row);
                }
            if (rows.lastError().isValid())
                error = rows.lastError().text();
        }
        QMetaObject::invokeMethod(
            this, [done = std::move(done), result, error]() mutable { done(result, error); });
    });
}
void AudioEffectCatalogCache::find(QString key, Rows done)
{
    work([this, key = std::move(key), done = std::move(done)](State &s) mutable {
        QJsonArray result;
        auto error = s.error;
        if (error.isEmpty()) {
            QSqlQuery query(s.db);
            query.prepare("SELECT metadata,identity,generation,ordinal FROM effect_entries WHERE "
                          "scope=? AND identity=?");
            query.addBindValue(key.section('|', 0, 0));
            query.addBindValue(key.section('|', 1));
            if (query.exec() && query.next())
                result.append(s.record(query, key.section('|', 0, 0)));
            if (query.lastError().isValid())
                error = query.lastError().text();
        }
        QMetaObject::invokeMethod(
            this, [done = std::move(done), result, error]() mutable { done(result, error); });
    });
}
void AudioEffectCatalogCache::locate(quint64 generation, QString key, std::function<void(int)> done)
{
    work([this, generation, key = std::move(key), done = std::move(done)](State &s) mutable {
        int position = -1;
        const bool latest = generation == s.projection;
        const auto view = latest ? s.view : s.baseView;
        if (s.error.isEmpty() && (latest || generation == s.baseProjection) &&
            key.section('|', 0, 0) == view.section('?', 0, 0)) {
            QSqlQuery query(s.db);
            query.prepare(QString("SELECT position FROM %1 WHERE identity=?")
                              .arg(latest ? "effect_projection" : "effect_base"));
            query.addBindValue(key.section('|', 1));
            if (query.exec() && query.next())
                position = query.value(0).toInt();
        }
        QMetaObject::invokeMethod(this,
                                  [done = std::move(done), position]() mutable { done(position); });
    });
}
void AudioEffectCatalogCache::remove(const QString &scope)
{
    work([scope](State &s) {
        if (!s.valid || !s.db.transaction())
            return;
        QSqlQuery query(s.db);
        bool ok = true;
        for (const auto *sql : {"DELETE FROM effect_entries WHERE scope=?",
                                "DELETE FROM effect_scopes WHERE scope=?"}) {
            query.prepare(sql);
            query.addBindValue(scope);
            ok = query.exec() && ok;
        }
        if (ok)
            s.db.commit();
        else
            s.db.rollback();
    });
}
void AudioEffectCatalogCache::shutdown()
{
    if (std::exchange(m_closing, true))
        return;
    if (!m_thread.isRunning()) {
        QTimer::singleShot(0, this, [this] { emit closed(); });
        return;
    }
    QMetaObject::invokeMethod(m_worker, [this, state = m_state] {
        state->db.close();
        state->db = {};
        QSqlDatabase::removeDatabase(state->connection);
        m_thread.quit();
    });
}
} // namespace EchoMusic
