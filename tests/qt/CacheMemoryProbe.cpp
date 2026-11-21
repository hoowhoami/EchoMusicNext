#include "CompactSet.h"
#include "SessionDatabase.h"
#include "SongObservationIndex.h"
#include <QEventLoop>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <algorithm>

using namespace EchoMusic;
template <template <typename> class Set> int measure(int count)
{
    QHash<int, QHash<QString, Set<QString>>> rows;
    QHash<QString, Set<int>> listeners;
    for (int i = 0; i < count; ++i) {
        const int owner = i % 4;
        const auto row = QString::number(i);
        const QStringList identities{"id:" + row,
                                     "hash:" + QString::number(i, 16).rightJustified(32, '0')};
        for (const auto &identity : identities) {
            rows[owner][identity].insert(row);
            listeners[identity].insert(owner);
            if (i % 20 == 0) {
                rows[4][identity].insert(row);
                listeners[identity].insert(4);
            }
        }
        rows[owner]["artist:shared"].insert(row);
        listeners["artist:shared"].insert(owner);
    }
    qint64 relations = 0;
    for (const auto &owner : std::as_const(rows))
        for (const auto &values : owner)
            relations += values.size();
    for (const auto &owners : std::as_const(listeners))
        relations += owners.size();
    const QJsonObject result{{"pid", QCoreApplication::applicationPid()},
                             {"rows", count},
                             {"relations", relations},
                             {"listeners", listeners.size()},
                             {"row_set_size", int(sizeof(Set<QString>))}};
    const auto json = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::puts(json.constData());
    std::fflush(stdout);
    // The driver samples a fresh Release process while both indices are live.
    // No application data or UI is involved; newline retires the whole graph.
    std::getchar();
    return rows.isEmpty() || listeners.isEmpty();
}
int measureSql(int count)
{
    QTemporaryDir directory;
    SessionDatabase database(directory.path());
    SongObservationIndex index(database);
    // Match production registration: generate one bounded chunk, commit it,
    // release its identities, then produce the next chunk. Do not construct a
    // second complete in-memory graph just to populate a disk-backed index.
    bool failed = false;
    for (int first = 0; first < count; first += 128) {
        QList<SongObservationIndex::Relation> owners[5];
        for (int i = first; i < std::min(count, first + 128); ++i) {
            const int owner = i % 4;
            const auto row = QString::number(i);
            const QStringList identities{"id:" + row,
                                         "hash:" + QString::number(i, 16).rightJustified(32, '0')};
            for (const auto &identity : identities) {
                owners[owner].append({identity, row, {}});
                if (i % 20 == 0)
                    owners[4].append({identity, row, {}});
            }
            owners[owner].append({"artist:shared", row, {}});
        }
        QEventLoop written;
        int pending = 5;
        for (int owner = 0; owner < 5; ++owner)
            index.insert(quint64(owner + 1), std::move(owners[owner]), [&](auto result) {
                failed |= !result.error.isEmpty();
                if (!--pending)
                    written.quit();
            });
        written.exec();
        if (failed)
            return 1;
    }
    QEventLoop readyLoop;
    QJsonObject ready;
    index.statistics([&](auto statistics) {
        if (!statistics.error.isEmpty()) {
            readyLoop.quit();
            return;
        }
        database.submit(
            &index,
            [](QSqlDatabase &db, const QString &error) {
                QJsonObject result;
                if (!error.isEmpty())
                    return result;
                QSqlQuery query(db);
                QString scope;
                if (query.exec("SELECT scope FROM song_observation LIMIT 1") && query.next())
                    scope = query.value(0).toString();
                query.prepare("SELECT count(*) FROM(SELECT DISTINCT identity,owner FROM "
                              "song_observation WHERE scope=?)");
                query.addBindValue(scope);
                if (query.exec() && query.next())
                    result["listener_relations"] = query.value(0).toLongLong();
                qint64 pages = 0, pageSize = 0;
                if (query.exec("PRAGMA page_count") && query.next())
                    pages = query.value(0).toLongLong();
                if (query.exec("PRAGMA page_size") && query.next())
                    pageSize = query.value(0).toLongLong();
                result["disk_bytes"] = pages * pageSize;
                return result;
            },
            [&, statistics](QJsonObject result) {
                if (result.contains("listener_relations")) {
                    ready = result;
                    ready["pid"] = QCoreApplication::applicationPid();
                    ready["rows"] = count;
                    ready["relations"] =
                        statistics.relations + result["listener_relations"].toInteger();
                    ready["listeners"] = statistics.identities;
                    ready["sql_rows"] = statistics.relations;
                }
                readyLoop.quit();
            });
    });
    readyLoop.exec();
    if (ready.isEmpty())
        return 1;
    const auto json = QJsonDocument(ready).toJson(QJsonDocument::Compact);
    std::puts(json.constData());
    std::fflush(stdout);
    std::getchar();
    QEventLoop closed;
    QObject::connect(&database, &SessionDatabase::closed, &closed, &QEventLoop::quit);
    database.shutdown();
    closed.exec();
    return 0;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3)
        return 2;
    bool valid = false;
    const auto count = args[2].toInt(&valid);
    if (!valid || count < 1 || count > 50000)
        return 2;
    if (args[1] == "sqlite")
        return measureSql(count);
    if (args[1] == "legacy")
        return measure<QSet>(count);
    if (args[1] == "compact")
        return measure<CompactSet>(count);
    return 2;
}
