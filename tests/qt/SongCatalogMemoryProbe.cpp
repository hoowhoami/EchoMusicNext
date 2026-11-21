#include "SongCatalogModel.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>
using namespace EchoMusic;
namespace EchoMusic
{
class SongCatalogStorageTestAccess
{
public:
    static bool idle(const SongCatalogModel &model)
    {
        return !model.m_store ||
               (!model.m_writing && !model.m_presentationWriting && !model.m_loadingViews &&
                model.m_viewDemand.size() == 0 && model.m_dirtyPresentations.isEmpty() &&
                model.m_writeCursor == model.m_entries.size());
    }
    static int residentPayloads(const SongCatalogModel &model)
    {
        int count = 0;
        for (const auto &entry : model.m_entries)
            count += entry.record == 0;
        return count;
    }
    static int residentPresentations(const SongCatalogModel &model)
    {
        QSet<int> sources;
        for (const auto source : model.m_hotViews.keys())
            sources.insert(source);
        for (int source = 0; source < model.m_entries.size(); ++source)
            if (model.m_entries[source].view)
                sources.insert(source);
        return sources.size();
    }
};
} // namespace EchoMusic
/// Controlled, paged replay of a local domain corpus. No API, images, audio or
/// QObject inspection is injected into the user's running application.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3 || (args[1] != "dense" && args[1] != "sqlite"))
        return 2;
    QFile input(args[2]);
    if (!input.open(QIODevice::ReadOnly))
        return 3;
    const auto repeats = args.size() >= 4 ? args[3].toInt() : 1;
    if (repeats < 1)
        return 2;
    QTemporaryDir directory;
    SessionDatabase database(directory.path());
    SongCatalogModel model;
    if (args[1] == "sqlite")
        model.enableSessionStorage(database);
    QTimer feed, settle;
    feed.setSingleShot(true);
    settle.setSingleShot(true);
    int repetition = 0, exitCode = 0;
    qint64 sourceBytes = 0;
    bool finished = false, announced = false, windowRequested = false;
    const auto fail = [&] {
        exitCode = 4;
        feed.stop();
        settle.stop();
        database.shutdown();
    };
    QObject::connect(&model, &SongCatalogModel::readFailed, &app, fail);
    QObject::connect(&database, &SessionDatabase::closed, &app, [&] { app.exit(exitCode); });
    QObject::connect(&feed, &QTimer::timeout, &app, [&] {
        if (announced || !SongCatalogStorageTestAccess::idle(model))
            return;
        if (finished) {
            if (!settle.isActive())
                settle.start(1000);
            return;
        }
        QJsonArray rows;
        while (rows.size() < 100) {
            const auto line = input.readLine();
            if (line.isEmpty() && input.atEnd()) {
                if (++repetition == repeats) {
                    finished = true;
                    break;
                }
                if (!input.seek(0)) {
                    fail();
                    return;
                }
                continue;
            }
            QJsonParseError error;
            const auto value = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !value.isObject()) {
                fail();
                return;
            }
            sourceBytes += line.size();
            rows.append(value.object());
        }
        if (!rows.isEmpty())
            model.appendSongs(rows);
        if (SongCatalogStorageTestAccess::idle(model))
            feed.start();
    });
    QObject::connect(&database, &SessionDatabase::capacityAvailable, &app, [&] {
        if (!announced && !settle.isActive())
            feed.start();
    });
    QObject::connect(&settle, &QTimer::timeout, &app, [&] {
        if (!SongCatalogStorageTestAccess::idle(model) || database.pendingJobs()) {
            feed.start();
            return;
        }
        if (!windowRequested) {
            windowRequested = true;
            for (int row = std::max(0, model.count() - 32); row < model.count(); ++row)
                model.item(row);
            feed.start();
            return;
        }
        if (args[1] == "sqlite" &&
            (!SongCatalogStorageTestAccess::idle(model) || database.pendingJobs())) {
            feed.start();
            return;
        }
        announced = true;
        QDir root(directory.path());
        qint64 cacheBytes = 0;
        for (const auto &name : root.entryList({"run-*"}, QDir::Dirs | QDir::NoDotAndDotDot))
            cacheBytes += QFileInfo(root.filePath(name + "/session.sqlite")).size();
        const QJsonObject result{
            {"mode", args[1]},
            {"rows", model.totalCount()},
            {"source_utf8_bytes", sourceBytes},
            {"cache_file_bytes", cacheBytes},
            {"resident_payloads", SongCatalogStorageTestAccess::residentPayloads(model)},
            {"resident_presentations", SongCatalogStorageTestAccess::residentPresentations(model)}};
        QTextStream output(stdout);
        output << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
    });
    QSocketNotifier stop(fileno(stdin), QSocketNotifier::Read);
    QObject::connect(&stop, &QSocketNotifier::activated, &app, [&] {
        stop.setEnabled(false);
        feed.stop();
        settle.stop();
        database.shutdown();
    });
    // Both modes warm the same driver/worker. The SQLite mode's data/cache
    // costs remain part of the result rather than being subtracted away.
    if (!database.submit(
            &app,
            [](QSqlDatabase &db, const QString &error) {
                QSqlQuery query(db);
                return error.isEmpty() && query.exec("SELECT 1") && query.next();
            },
            [&](bool ok) {
                if (ok)
                    feed.start();
                else
                    fail();
            }))
        return 5;
    return app.exec();
}
