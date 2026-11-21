#include "CacheCost.h"
#include "SongDatasetStore.h"
#include <QCollator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QSet>
#include <algorithm>

namespace EchoMusic
{
struct SongDatasetStore::Lease
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QPointer<SessionDatabase> database;
    bool written = false;
    Loader loader;
    ~Lease()
    {
        // Every worker result carries this lease to its GUI completion, so its
        // last release and cleanup admission happen on the database owner thread.
        if (database && written) {
            Q_ASSERT(QThread::currentThread() == database->thread());
            database->discard([generation = id](QSqlDatabase &db) {
                QSqlQuery query(db);
                query.prepare("DELETE FROM song_dataset WHERE generation=?");
                query.addBindValue(generation);
                if (!query.exec())
                    qWarning().noquote() << "Session cache retirement:" << query.lastError().text();
            });
        }
    }
};
namespace
{
QString schema(QSqlDatabase &db)
{
    QSqlQuery query(db);
    for (const auto *sql :
         {"CREATE TABLE IF NOT EXISTS song_dataset("
          "record INTEGER PRIMARY KEY,generation TEXT NOT NULL,ordinal INTEGER NOT NULL,"
          "view_id TEXT NOT NULL,title TEXT NOT NULL,artist TEXT NOT NULL,album TEXT NOT NULL,"
          "duration TEXT NOT NULL,extra TEXT NOT NULL,presentation BLOB NOT NULL)",
          // Keep resource blobs out of the pages scanned for display/search.
          "CREATE TABLE IF NOT EXISTS song_payload(record INTEGER PRIMARY KEY REFERENCES "
          "song_dataset(record) ON DELETE CASCADE,payload BLOB NOT NULL)",
          "CREATE INDEX IF NOT EXISTS song_dataset_order ON song_dataset(generation,ordinal)"})
        if (!query.exec(QString::fromLatin1(sql)))
            return QObject::tr("初始化歌曲缓存失败：%1").arg(query.lastError().text());
    return {};
}
QList<SongDatasetStore::Generation> sourceLeases(const SongDatasetStore::Snapshot &snapshot)
{
    QList<SongDatasetStore::Generation> leases;
    QSet<const SongDatasetStore::Lease *> seen;
    for (const auto &row : snapshot.rows)
        if (row.source && !seen.contains(row.source.get())) {
            seen.insert(row.source.get());
            leases.append(row.source);
        }
    return leases;
}
QString sortText(const QString &value)
{
    static const QRegularExpression marks(QStringLiteral(R"(\p{M})"));
    return value.normalized(QString::NormalizationForm_D).remove(marks);
}
bool matches(const QString &value, const QString &needle, SongDatasetStore::QueryStyle style)
{
    return style == SongDatasetStore::QueryStyle::Catalog
               ? value.contains(needle, Qt::CaseInsensitive)
               : value.toLower().contains(needle);
}
QString queryText(const QString &value, SongDatasetStore::QueryStyle style, bool title = false)
{
    return style == SongDatasetStore::QueryStyle::Catalog
               ? sortText(title ? value.trimmed() : value)
               : value.trimmed();
}
struct SortRow
{
    SongDatasetStore::Reference reference;
    QString title, artist, album;
    quint64 duration;
};
void finishQuery(SongDatasetStore::View &result, QList<SortRow> sorted, int field, int direction)
{
    if (field >= 2 && field <= 5) {
        QCollator collator(QLocale(QLocale::Chinese, QLocale::SimplifiedHanScript, QLocale::China));
        collator.setNumericMode(false);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(sorted.begin(), sorted.end(), [&](const SortRow &a, const SortRow &b) {
            int comparison = 0;
            if (field == 2) {
                comparison = collator.compare(a.title, b.title);
                if (!comparison)
                    comparison = collator.compare(a.artist, b.artist);
            } else if (field == 3) {
                comparison = collator.compare(a.artist, b.artist);
                if (!comparison)
                    comparison = collator.compare(a.title, b.title);
            } else if (field == 4)
                comparison = collator.compare(a.album, b.album);
            else
                comparison = a.duration < b.duration ? -1 : a.duration > b.duration ? 1 : 0;
            return direction == 2 ? comparison > 0 : comparison < 0;
        });
        for (auto &row : sorted)
            result.rows.append(std::move(row.reference));
    } else if (field == 1 && direction == 2)
        std::reverse(result.rows.begin(), result.rows.end());
}
} // namespace
SongDatasetStore::SongDatasetStore(SessionDatabase &database, QObject *parent)
    : QObject(parent), m_database(database)
{
}
SongDatasetStore::Generation SongDatasetStore::create(Loader loader)
{
    auto result = std::make_shared<Lease>();
    result->database = &m_database;
    result->loader = std::move(loader);
    return result;
}
quint64 SongDatasetStore::estimateCost(const QJsonValue &value)
{
    return jsonValueCost(value);
}
bool SongDatasetStore::append(Generation generation, QList<Row> rows, QObject *consumer,
                              std::function<void(Written)> done)
{
    quint64 cost = 0;
    for (const auto &row : rows)
        cost += jsonValueCost(row.song) + jsonValueCost(row.presentation);
    return m_database.submit(
        consumer,
        [generation = std::move(generation), rows = std::move(rows)](QSqlDatabase &db,
                                                                     const QString &error) {
            Written result{generation, {}, error};
            if (!result.error.isEmpty())
                return result;
            if (!generation) {
                result.error = QObject::tr("歌曲缓存已失效");
                return result;
            }
            result.error = schema(db);
            if (!result.error.isEmpty())
                return result;
            if (!db.transaction()) {
                result.error = QObject::tr("无法开始歌曲缓存写入：%1").arg(db.lastError().text());
                return result;
            }
            QSqlQuery query(db);
            query.prepare("INSERT INTO "
                          "song_dataset(generation,ordinal,view_id,title,artist,album,duration,"
                          "extra,presentation)"
                          " VALUES(?,?,?,?,?,?,?,?,?)");
            QSqlQuery payload(db);
            payload.prepare("INSERT INTO song_payload(record,payload) VALUES(?,?)");
            for (const auto &row : rows) {
                query.bindValue(0, generation->id);
                query.bindValue(1, row.ordinal);
                query.bindValue(2, row.id.isNull() ? QStringLiteral("") : row.id);
                query.bindValue(3, row.title.isNull() ? QStringLiteral("") : row.title);
                query.bindValue(4, row.artist.isNull() ? QStringLiteral("") : row.artist);
                query.bindValue(5, row.album.isNull() ? QStringLiteral("") : row.album);
                query.bindValue(6, QString::number(row.duration));
                query.bindValue(7, row.extra.isNull() ? QStringLiteral("") : row.extra);
                query.bindValue(8, QJsonDocument(row.presentation).toJson(QJsonDocument::Compact));
                if (!query.exec()) {
                    result.error =
                        QObject::tr("写入歌曲缓存失败：%1").arg(query.lastError().text());
                    break;
                }
                const auto record = query.lastInsertId().toLongLong();
                payload.bindValue(0, record);
                payload.bindValue(1, QJsonDocument(row.song).toJson(QJsonDocument::Compact));
                if (!payload.exec()) {
                    result.error =
                        QObject::tr("写入歌曲缓存失败：%1").arg(payload.lastError().text());
                    break;
                }
                result.rows.append({record, row.ordinal, row.id});
            }
            if (result.error.isEmpty() && !db.commit())
                result.error = QObject::tr("提交歌曲缓存失败：%1").arg(db.lastError().text());
            if (!result.error.isEmpty()) {
                db.rollback();
                result.rows.clear();
            }
            if (result.error.isEmpty())
                result.generation->written = true;
            return result;
        },
        std::move(done), cost);
}
bool SongDatasetStore::read(Snapshot snapshot, qsizetype cursor, QObject *consumer,
                            std::function<void(Read)> done, bool includePresentations)
{
    return readValues(std::move(snapshot), cursor, consumer, std::move(done), false,
                      includePresentations);
}
bool SongDatasetStore::readPresentations(Snapshot snapshot, qsizetype cursor, QObject *consumer,
                                         std::function<void(Read)> done)
{
    return readValues(std::move(snapshot), cursor, consumer, std::move(done), true, false);
}
bool SongDatasetStore::readValues(Snapshot snapshot, qsizetype cursor, QObject *consumer,
                                  std::function<void(Read)> done, bool presentationOnly,
                                  bool includePresentations)
{
    const QPointer<SongDatasetStore> owner(this);
    const QPointer<QObject> guard(consumer);
    auto deliver = [owner, guard, snapshot, cursor, presentationOnly,
                    done = std::move(done)](Read result) mutable {
        if (!owner || !guard)
            return;
        if (presentationOnly || !result.error.isEmpty()) {
            done(std::move(result));
            return;
        }
        struct Hydration
        {
            Read result;
            Snapshot snapshot;
            qsizetype first = 0, at = 0;
            std::function<void(Read)> done;
        };
        auto state = std::make_shared<Hydration>();
        state->result = std::move(result);
        state->snapshot = std::move(snapshot);
        state->first = cursor;
        state->done = std::move(done);
        auto step = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weak = step;
        *step = [owner, guard, state, weak] {
            if (!owner || !guard)
                return;
            while (state->at < state->result.values.size()) {
                const auto &ref = state->snapshot.rows[state->first + state->at];
                const auto generation = ref.source ? ref.source : state->snapshot.generation;
                if (!generation->loader) {
                    ++state->at;
                    continue;
                }
                const auto first = state->at;
                auto last = first + 1;
                while (last < state->result.values.size()) {
                    const auto &other = state->snapshot.rows[state->first + last];
                    if ((other.source ? other.source : state->snapshot.generation) != generation)
                        break;
                    ++last;
                }
                generation->loader(state->result.values.mid(first, last - first), guard,
                                   [owner, guard, state, first, last,
                                    step = weak.lock()](auto values, auto error) {
                                       if (!owner || !guard)
                                           return;
                                       if (error.isEmpty() &&
                                           (values.isEmpty() || values.size() > last - first))
                                           error = QObject::tr("歌曲资料读取结果不完整，请重试");
                                       if (!error.isEmpty()) {
                                           state->result.error = error;
                                           state->result.values.clear();
                                           state->result.presentations.clear();
                                           state->done(std::move(state->result));
                                           return;
                                       }
                                       for (qsizetype i = 0; i < values.size(); ++i)
                                           state->result.values[first + i] = std::move(values[i]);
                                       state->at = first + values.size();
                                       if (values.size() < last - first) {
                                           state->result.values.resize(state->at);
                                           if (!state->result.presentations.isEmpty())
                                               state->result.presentations.resize(state->at);
                                           state->result.next = state->first + state->at;
                                       }
                                       (*step)();
                                   });
                return;
            }
            state->done(std::move(state->result));
        };
        (*step)();
    };
    return m_database.submit(
        consumer,
        [snapshot = std::move(snapshot), cursor, presentationOnly,
         includePresentations](QSqlDatabase &db, const QString &error) {
            Read result{snapshot.generation, {}, cursor, error};
            result.leases = sourceLeases(snapshot);
            if (!result.error.isEmpty())
                return result;
            result.error = schema(db);
            if (!result.error.isEmpty())
                return result;
            if ((!snapshot.generation && snapshot.rows.isEmpty()) || cursor < 0 ||
                cursor > snapshot.rows.size()) {
                result.error = QObject::tr("歌曲缓存读取位置无效");
                return result;
            }
            QSqlQuery query(db);
            query.prepare(
                presentationOnly
                    ? "SELECT presentation FROM song_dataset WHERE generation=? AND record=?"
                : includePresentations
                    ? "SELECT p.payload,d.presentation FROM song_dataset d JOIN song_payload p "
                      "ON p.record=d.record WHERE d.generation=? AND d.record=?"
                    : "SELECT p.payload FROM song_dataset d JOIN song_payload p "
                      "ON p.record=d.record WHERE d.generation=? AND d.record=?");
            qint64 bytes = 0;
            while (result.next < snapshot.rows.size() && result.values.size() < 128) {
                const auto &row = snapshot.rows[result.next];
                const auto generation = row.source ? row.source : snapshot.generation;
                if (!generation) {
                    result.error = QObject::tr("歌曲缓存已失效");
                    break;
                }
                query.bindValue(0, generation->id);
                query.bindValue(1, row.record);
                if (!query.exec() || !query.next()) {
                    result.error =
                        query.lastError().isValid()
                            ? QObject::tr("读取歌曲缓存失败：%1").arg(query.lastError().text())
                            : QObject::tr("歌曲缓存记录已失效，请重试加载列表");
                    break;
                }
                const auto payload = query.value(0).toByteArray();
                const auto presentation =
                    includePresentations ? query.value(1).toByteArray() : QByteArray();
                const auto cost = payload.size() + presentation.size();
                if (!result.values.isEmpty() && cost > 2 * 1024 * 1024 - bytes)
                    break;
                QJsonParseError parse;
                const auto document = QJsonDocument::fromJson(payload, &parse);
                if (parse.error != QJsonParseError::NoError || !document.isObject()) {
                    result.error = presentationOnly
                                       ? QObject::tr("歌曲缓存展示记录无法解码，请重试加载列表")
                                       : QObject::tr("歌曲缓存记录无法解码，请重试加载列表");
                    break;
                }
                if (includePresentations) {
                    const auto view = QJsonDocument::fromJson(presentation, &parse);
                    if (parse.error != QJsonParseError::NoError || !view.isObject()) {
                        result.error = QObject::tr("歌曲缓存展示记录无法解码，请重试加载列表");
                        break;
                    }
                    result.presentations.append(view.object());
                }
                bytes += cost;
                result.values.append(document.object());
                ++result.next;
            }
            if (!result.error.isEmpty()) {
                result.values.clear();
                result.presentations.clear();
            }
            return result;
        },
        std::move(deliver), 2 * 1024 * 1024);
}
bool SongDatasetStore::updatePresentations(Generation generation, QList<Presentation> values,
                                           QObject *consumer, std::function<void(Updated)> done)
{
    quint64 cost = 0;
    for (const auto &value : values)
        cost += jsonValueCost(value.value) +
                quint64(value.title.size() + value.artist.size() + value.album.size()) * 2;
    return m_database.submit(
        consumer,
        [generation = std::move(generation), values = std::move(values)](QSqlDatabase &db,
                                                                         const QString &error) {
            Updated result{generation, error};
            if (!result.error.isEmpty())
                return result;
            if (!generation) {
                result.error = QObject::tr("歌曲缓存已失效");
                return result;
            }
            result.error = schema(db);
            if (!result.error.isEmpty())
                return result;
            if (!db.transaction()) {
                result.error =
                    QObject::tr("无法开始歌曲展示缓存写入：%1").arg(db.lastError().text());
                return result;
            }
            QSqlQuery query(db);
            query.prepare("UPDATE song_dataset SET title=?,artist=?,album=?,duration=?,"
                          "extra=?,presentation=? WHERE generation=? AND record=?");
            for (const auto &value : values) {
                query.bindValue(0, value.title.isNull() ? QStringLiteral("") : value.title);
                query.bindValue(1, value.artist.isNull() ? QStringLiteral("") : value.artist);
                query.bindValue(2, value.album.isNull() ? QStringLiteral("") : value.album);
                query.bindValue(3, QString::number(value.duration));
                query.bindValue(4, value.value.value("extra").toString(QStringLiteral("")));
                query.bindValue(5, QJsonDocument(value.value).toJson(QJsonDocument::Compact));
                query.bindValue(6, generation->id);
                query.bindValue(7, value.record);
                if (!query.exec()) {
                    result.error =
                        QObject::tr("写入歌曲展示缓存失败：%1").arg(query.lastError().text());
                    break;
                }
                if (query.numRowsAffected() != 1) {
                    result.error = QObject::tr("歌曲缓存展示记录已失效，请重试加载列表");
                    break;
                }
            }
            if (result.error.isEmpty() && !db.commit())
                result.error = QObject::tr("提交歌曲展示缓存失败：%1").arg(db.lastError().text());
            if (!result.error.isEmpty())
                db.rollback();
            return result;
        },
        std::move(done), cost);
}
bool SongDatasetStore::query(Generation generation, QString text, int field, int direction,
                             QObject *consumer, std::function<void(View)> done, qint64 lastOrdinal,
                             QueryStyle style)
{
    return m_database.submit(
        consumer,
        [generation = std::move(generation), text = std::move(text), field, direction, lastOrdinal,
         style](QSqlDatabase &db, const QString &error) {
            View result{generation, {}, error};
            if (!result.error.isEmpty())
                return result;
            result.error = schema(db);
            if (!result.error.isEmpty())
                return result;
            if (field < 0 || field > 5 || direction < 0 || direction > 2) {
                result.error = QObject::tr("歌曲缓存排序参数无效");
                return result;
            }
            if (!generation) {
                result.error = QObject::tr("歌曲缓存已失效");
                return result;
            }
            QList<SortRow> sorted;
            QSqlQuery query(db);
            query.setForwardOnly(true);
            query.prepare(
                "SELECT record,ordinal,view_id,title,artist,album,duration,extra FROM song_dataset"
                " WHERE generation=? AND ordinal<=? ORDER BY ordinal");
            query.addBindValue(generation->id);
            query.addBindValue(lastOrdinal);
            if (!query.exec()) {
                result.error = QObject::tr("检索歌曲缓存失败：%1").arg(query.lastError().text());
                return result;
            }
            const auto needle =
                style == QueryStyle::Catalog ? text.trimmed() : text.trimmed().toLower();
            while (query.next()) {
                const auto title = query.value(3).toString(), artist = query.value(4).toString(),
                           album = query.value(5).toString();
                if (!needle.isEmpty() && !matches(title, needle, style) &&
                    !matches(artist, needle, style) && !matches(album, needle, style))
                    continue;
                Reference reference{query.value(0).toLongLong(), query.value(1).toLongLong(),
                                    query.value(2).toString()};
                if (!field || field == 1)
                    result.rows.append(std::move(reference));
                else
                    sorted.append(
                        {std::move(reference), queryText(title, style, true),
                         queryText(artist, style),
                         queryText(style == QueryStyle::SearchLyric ? query.value(7).toString()
                                                                    : album,
                                   style),
                         query.value(6).toULongLong()});
            }
            if (query.lastError().isValid()) {
                result.error = QObject::tr("检索歌曲缓存失败：%1").arg(query.lastError().text());
                result.rows.clear();
                return result;
            }
            finishQuery(result, std::move(sorted), field, direction);
            return result;
        },
        std::move(done));
}
bool SongDatasetStore::query(Snapshot snapshot, QString text, int field, int direction,
                             QObject *consumer, std::function<void(View)> done, QueryStyle style)
{
    const auto cost = quint64(snapshot.rows.size()) * 128;
    return m_database.submit(
        consumer,
        [snapshot = std::move(snapshot), text = std::move(text), field, direction,
         style](QSqlDatabase &db, const QString &error) {
            View result{snapshot.generation, {}, error};
            result.leases = sourceLeases(snapshot);
            if (!result.error.isEmpty())
                return result;
            result.error = schema(db);
            if (!result.error.isEmpty())
                return result;
            if (field < 0 || field > 5 || direction < 0 || direction > 2) {
                result.error = QObject::tr("歌曲缓存排序参数无效");
                return result;
            }
            QList<SortRow> sorted;
            QSqlQuery query(db);
            query.prepare(
                "SELECT title,artist,album,duration,extra FROM song_dataset WHERE generation=? "
                "AND record=?");
            const auto needle =
                style == QueryStyle::Catalog ? text.trimmed() : text.trimmed().toLower();
            for (const auto &reference : snapshot.rows) {
                const auto generation = reference.source ? reference.source : snapshot.generation;
                if (!generation) {
                    result.error = QObject::tr("歌曲缓存已失效");
                    break;
                }
                query.bindValue(0, generation->id);
                query.bindValue(1, reference.record);
                if (!query.exec() || !query.next()) {
                    result.error =
                        query.lastError().isValid()
                            ? QObject::tr("检索歌曲缓存失败：%1").arg(query.lastError().text())
                            : QObject::tr("歌曲缓存记录已失效，请重试加载列表");
                    break;
                }
                const auto title = query.value(0).toString(), artist = query.value(1).toString(),
                           album = query.value(2).toString();
                if (!needle.isEmpty() && !matches(title, needle, style) &&
                    !matches(artist, needle, style) && !matches(album, needle, style))
                    continue;
                if (!field || field == 1)
                    result.rows.append(reference);
                else
                    sorted.append(
                        {reference, queryText(title, style, true), queryText(artist, style),
                         queryText(style == QueryStyle::SearchLyric ? query.value(4).toString()
                                                                    : album,
                                   style),
                         query.value(3).toULongLong()});
            }
            if (!result.error.isEmpty()) {
                result.rows.clear();
                return result;
            }
            finishQuery(result, std::move(sorted), field, direction);
            return result;
        },
        std::move(done), cost);
}
} // namespace EchoMusic
