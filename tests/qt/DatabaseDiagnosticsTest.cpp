#include "DatabaseWorker.h"
#include "PlaybackRepository_p.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <thread>

using namespace EchoMusic;
namespace
{
QList<QJsonObject> records;
QList<QtMsgType> severities;
QtMessageHandler previousHandler = nullptr;
void captureSql(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (qstrcmp(context.category, "echo.storage.sql") == 0) {
        records.append(QJsonDocument::fromJson(message.toUtf8()).object());
        severities.append(type);
    } else if (previousHandler)
        previousHandler(type, context, message);
}
struct Database
{
    QString name = QUuid::createUuid().toString();
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
    explicit Database(const QString &path)
    {
        db.setDatabaseName(path);
    }
    ~Database()
    {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(name);
    }
};
PlaybackTrack sampleTrack()
{
    PlaybackTrack track;
    track.id = "42";
    track.hash = QString(32, 'a');
    track.title = QStringLiteral("中文歌曲 Song");
    track.artist = QStringLiteral("中文歌手");
    track.album = QStringLiteral("专辑");
    track.durationMs = 180000;
    return track;
}
QString chinesePath(const QTemporaryDir &directory, const QString &file)
{
    return directory.filePath(QStringLiteral("用户 中文/AppData/Local/音乐 Next/") + file);
}
} // namespace

class DatabaseDiagnosticsTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        records.clear();
        severities.clear();
        previousHandler = qInstallMessageHandler(captureSql);
    }
    void cleanup()
    {
        qInstallMessageHandler(previousHandler);
        previousHandler = nullptr;
    }
    void queueCollectionKeepsSnapshotsRollsBackAndReclaimsWhileRunning()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("queue.sqlite");
        PlaybackTracks survivingShutdown;
        Database observer(path);
        {
            PlaybackStorageWorker storage;
            QSignalSpy errors(&storage, &PlaybackStorageWorker::failed);
            QSignalSpy saved(&storage, &PlaybackStorageWorker::saved);
            QSignalSpy loaded(&storage, &PlaybackStorageWorker::loaded);
            storage.initialize(path);
            QVERIFY(observer.db.open());
            const auto count = [&] {
                QSqlQuery query(observer.db);
                return query.exec("SELECT count(*) FROM playback_song_payloads") && query.next()
                           ? query.value(0).toInt()
                           : -1;
            };
            PlaybackTracks originals;
            for (int i = 0; i < 600; ++i) {
                auto track = sampleTrack();
                track.id = QString::number(i);
                track.payloadKey = "original-" + track.id;
                track.catalogMetadata = QJsonObject{{"revision", 0},
                                                    {"unknown", QJsonValue::Null},
                                                    {"resources", QJsonArray{QString(4096, 'a')}}};
                originals.append(track);
            }
            storage.save(42, {{"old", "Old queue", originals, 0, {}}}, "old");
            storage.load(42, 1, true);
            QCOMPARE(loaded.size(), 1);
            auto retained = loaded.first()[2].value<PlaybackQueueStates>().first().tracks;
            QVERIFY(retained.first().payloadLease);
            loaded.clear();
            saved.clear();
            auto other = sampleTrack();
            other.payloadKey = "other-account";
            storage.save(99, {{"other", "Other", {other}, 0, {}}}, "other");
            auto replacement = sampleTrack();
            replacement.payloadKey = "replacement";
            storage.save(42, {{"new", "Replacement", {replacement}, 0, {}}}, "new");
            saved.clear();
            QTest::qWait(150);
            QCOMPARE(count(), 602); // Both current scopes plus the fixed old snapshot.
            auto hydrated = storage.hydrate(retained.first());
            QVERIFY2(hydrated.second.isEmpty(), qPrintable(hydrated.second));
            QCOMPARE(hydrated.first.catalogMetadata, originals.first().catalogMetadata);
            QVERIFY(!hydrated.first.payloadLease); // Full source no longer needs cold storage.
            QSqlQuery query(observer.db);
            QVERIFY(query.exec(
                "EXPLAIN QUERY PLAN SELECT 1 FROM playback_tracks WHERE payload_key='x'"));
            QVERIFY(query.next());
            QVERIFY(query.value(3).toString().contains("playback_queue_payload_ref"));
            query.finish();
            QVERIFY(
                query.exec("CREATE TRIGGER reject_queue BEFORE INSERT ON playback_tracks "
                           "WHEN NEW.payload_key='failed' BEGIN SELECT RAISE(ABORT,'test'); END"));
            auto failed = replacement;
            failed.payloadKey = "failed";
            storage.save(42, {{"broken", "Broken", {failed}, 0, {}}}, "broken");
            QCOMPARE(errors.size(), 1);
            QCOMPARE(count(),
                     602); // Failed replacement cannot remove current or publish a version.
            errors.clear();
            QVERIFY(query.exec("DROP TRIGGER reject_queue"));
            storage.save(42, {}, "default"); // Clear/eviction preserves the admitted old snapshot.
            saved.clear();
            QTRY_COMPARE(count(), 601);
            QVERIFY(query.exec(
                "CREATE TRIGGER reject_collection BEFORE DELETE ON playback_song_payloads "
                "BEGIN SELECT RAISE(ABORT,'test'); END"));
            std::thread release([held = std::move(retained)]() mutable { held.clear(); });
            release.join();
            QTRY_VERIFY(!errors.isEmpty());
            QCOMPARE(count(), 601); // A failed collection is an atomic rollback.
            errors.clear();
            QVERIFY(query.exec("DROP TRIGGER reject_collection"));
            storage.save(99, {{"other", "Other", {other}, 0, {}}}, "other");
            saved.clear();
            QTRY_COMPARE(count(), 1); // More than two collection batches, without close/reopen.
            storage.load(99, 2, true);
            survivingShutdown = loaded.last()[2].value<PlaybackQueueStates>().first().tracks;
            loaded.clear();
            storage.save(99, {}, "default");
            saved.clear();
            QTest::qWait(75);
            QCOMPARE(count(), 1);
            QVERIFY(errors.isEmpty());
            storage.close();
            QCOMPARE(count(), 0);
        }
        std::thread release([held = std::move(survivingShutdown)]() mutable { held.clear(); });
        release.join();
    }
    void historyCollectionKeepsLeasedVersionsAndReclaimsAfterLastRelease()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("history.sqlite");
        HistoryPayloadLease survivingShutdown;
        Database observer(path);
        {
            PlaybackStorageWorker storage;
            QSignalSpy errors(&storage, &PlaybackStorageWorker::failed);
            QSignalSpy manifests(&storage, &PlaybackStorageWorker::historyLoaded);
            storage.initialize(path);
            QVERIFY(observer.db.open());
            const auto count = [&] {
                QSqlQuery query(observer.db);
                return query.exec("SELECT count(*) FROM playback_history_payloads") && query.next()
                           ? query.value(0).toInt()
                           : -1;
            };
            auto track = sampleTrack();
            track.catalogMetadata.insert("unknown", QJsonValue::Null);
            storage.record(42, track, 10);
            storage.loadHistory(42, 1, true);
            QCOMPARE(manifests.size(), 1);
            auto lease = manifests.first()[3].value<HistoryPayloadLease>();
            QVERIFY(lease);
            const auto key = manifests.first()[2]
                                 .toJsonArray()
                                 .first()
                                 .toObject()
                                 .value("payload_key")
                                 .toString();
            manifests.clear();
            for (int i = 0; i < 600; ++i) {
                track.catalogMetadata.insert("revision", i);
                storage.record(42, track, 10);
            }
            storage.record(99, track, 20);
            // More than one 256-row collection batch; current account rows and
            // a published old snapshot must all survive.
            QTRY_COMPARE(count(), 3);
            const auto old = storage.historyTracks(42, {key});
            QVERIFY2(old.second.isEmpty(), qPrintable(old.second));
            QCOMPARE(old.first.size(), 1);
            QVERIFY(old.first.first().value("metadata").toObject().value("unknown").isNull());
            QVERIFY(!old.first.first().value("metadata").toObject().contains("revision"));
            QVERIFY(!storage.historyTracks(99, {key}).second.isEmpty());
            storage.clearHistory(42);
            QTRY_COMPARE(count(), 2);
            // Last release may occur away from the worker. It schedules one
            // coalesced collection, with no polling or unsafe worker ownership.
            std::thread release([held = std::move(lease)]() mutable { held.reset(); });
            release.join();
            QTRY_COMPARE(count(), 1);
            QVERIFY(!storage.historyTracks(42, {key}).second.isEmpty());
            storage.loadHistory(99, 2, true);
            survivingShutdown = manifests.last()[3].value<HistoryPayloadLease>();
            manifests.clear();
            storage.clearHistory(99);
            QTest::qWait(60);
            QCOMPARE(count(), 1);
            QVERIFY(errors.isEmpty());
            storage.close();
            QCOMPARE(count(), 0);
        }
        survivingShutdown.reset();
    }
    void chinesePathsPreserveMusicAndPlaybackAfterReopen()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto musicPath = chinesePath(directory, "music.sqlite");
        const auto playbackPath = chinesePath(directory, "playback.sqlite");
        const auto track = sampleTrack();
        const PlaybackQueueState queue{"default", QStringLiteral("中文队列"), {track}, 0, {}};
        for (int pass = 0; pass < 2; ++pass) {
            DatabaseWorker music;
            QSignalSpy initialized(&music, &DatabaseWorker::initialized);
            QSignalSpy failures(&music, &DatabaseWorker::failed);
            QSignalSpy searches(&music, &DatabaseWorker::historyReady);
            music.initialize(musicPath);
            QCOMPARE(initialized.size(), 1);
            QVERIFY(failures.isEmpty());
            if (!pass)
                music.recordSearch(QStringLiteral("中文检索 Song"));
            QCOMPARE(searches.last()[0].toStringList(),
                     QStringList{QStringLiteral("中文检索 Song")});
            music.close();

            PlaybackStorageWorker playback;
            QSignalSpy errors(&playback, &PlaybackStorageWorker::failed);
            QSignalSpy loaded(&playback, &PlaybackStorageWorker::loaded);
            QSignalSpy history(&playback, &PlaybackStorageWorker::historyLoaded);
            playback.initialize(playbackPath);
            if (!pass) {
                playback.save(42, {queue}, queue.key);
                playback.record(42, track, 1500);
            }
            playback.load(42, 7);
            playback.loadHistory(42, 7);
            QVERIFY(errors.isEmpty());
            QCOMPARE(loaded.size(), 1);
            const auto queues = loaded.first()[2].value<PlaybackQueueStates>();
            QCOMPARE(queues.size(), 1);
            QCOMPARE(queues.first().title, queue.title);
            QCOMPARE(queues.first().tracks.first().title, track.title);
            QCOMPARE(history.size(), 1);
            QCOMPARE(history.first()[2].toJsonArray().size(), 1);
            QCOMPARE(history.first()[2].toJsonArray().first().toObject()["listened_ms"].toInt(),
                     1500);
            playback.close();
        }
        QVERIFY(records.isEmpty());
    }
    void playbackVersionReadFailure_data()
    {
        QTest::addColumn<bool>("locked");
        QTest::newRow("invalid-database") << false;
        QTest::newRow("exclusive-lock") << true;
    }
    void playbackVersionReadFailure()
    {
        QFETCH(bool, locked);
        QTemporaryDir directory;
        const auto path = chinesePath(directory, "playback.sqlite");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        Database blocker(path);
        if (locked) {
            QVERIFY(blocker.db.open());
            QSqlQuery query(blocker.db);
            QVERIFY(query.exec("PRAGMA user_version=5"));
            QVERIFY(query.exec("BEGIN EXCLUSIVE"));
        } else {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(512, 'x')), 512);
        }
        PlaybackStorageWorker worker;
        QSignalSpy failures(&worker, &PlaybackStorageWorker::failed);
        QSignalSpy loaded(&worker, &PlaybackStorageWorker::loaded);
        worker.initialize(path);
        worker.load(42, 1);
        QCOMPARE(failures.size(), 1);
        QVERIFY(failures.first()[0].toString().contains(QStringLiteral("无法读取播放数据库版本")));
        QVERIFY(!failures.first()[0].toString().contains(QStringLiteral("版本不受支持")));
        QVERIFY(loaded.isEmpty());
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first()["database"].toString(), QString("playback.sqlite"));
        QCOMPARE(records.first()["operation"].toString(), QString("read_user_version"));
        QCOMPARE(severities.first(), QtCriticalMsg); // Visible at the error-only log level.
        QCOMPARE(records.first()["primary_code"].toInt(), locked ? 5 : 26);
        QVERIFY(!records.first()["native_code"].toString().isEmpty());
        QVERIFY(!records.first()["database_text"].toString().isEmpty());
        QVERIFY(!records.first().contains("actual_version"));
        worker.close();
    }
    void futureVersionsAreReportedAndPreserved_data()
    {
        QTest::addColumn<bool>("playback");
        QTest::newRow("music") << false;
        QTest::newRow("playback") << true;
    }
    void futureVersionsAreReportedAndPreserved()
    {
        QFETCH(bool, playback);
        QTemporaryDir directory;
        const QString filename = playback ? "playback.sqlite" : "music.sqlite";
        const auto path = chinesePath(directory, filename);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        {
            Database database(path);
            QVERIFY(database.db.open());
            QSqlQuery query(database.db);
            QVERIFY(query.exec("PRAGMA user_version=999"));
            QVERIFY(query.exec("CREATE TABLE sentinel(value TEXT)"));
            QVERIFY(query.exec("INSERT INTO sentinel VALUES('keep')"));
        }
        QString message;
        if (playback) {
            PlaybackStorageWorker worker;
            QSignalSpy failures(&worker, &PlaybackStorageWorker::failed);
            worker.initialize(path);
            QCOMPARE(failures.size(), 1);
            message = failures.first()[0].toString();
            worker.close();
        } else {
            DatabaseWorker worker;
            QSignalSpy failures(&worker, &DatabaseWorker::failed);
            QSignalSpy initialized(&worker, &DatabaseWorker::initialized);
            worker.initialize(path);
            QCOMPARE(failures.size(), 1);
            QVERIFY(initialized.isEmpty());
            message = failures.first()[0].toString();
            worker.close();
        }
        QVERIFY(message.contains("999"));
        QVERIFY(message.contains("6"));
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first()["database"].toString(), filename);
        QCOMPARE(records.first()["operation"].toString(), QString("check_user_version"));
        QCOMPARE(records.first()["actual_version"].toInt(), 999);
        QCOMPARE(records.first()["supported_version"].toInt(), 6);
        Database database(path);
        QVERIFY(database.db.open());
        QSqlQuery query(database.db);
        QVERIFY(query.exec("PRAGMA user_version"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 999);
        QVERIFY(query.exec("SELECT value FROM sentinel"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString("keep"));
    }
    void musicReadFailureRetainsDriverError()
    {
        QTemporaryDir directory;
        const auto path = chinesePath(directory, "music.sqlite");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArray(512, 'x')), 512);
        file.close();
        DatabaseWorker worker;
        QSignalSpy failures(&worker, &DatabaseWorker::failed);
        QSignalSpy initialized(&worker, &DatabaseWorker::initialized);
        worker.initialize(path);
        QCOMPARE(failures.size(), 1);
        QVERIFY(initialized.isEmpty());
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first()["database"].toString(), QString("music.sqlite"));
        QCOMPARE(records.first()["operation"].toString(), QString("initialize.configure_wal"));
        QCOMPARE(records.first()["primary_code"].toInt(), 26);
        QVERIFY(!records.first()["driver_text"].toString().isEmpty());
        worker.close();
    }
    void failedOpenIdentifiesDatabase_data()
    {
        QTest::addColumn<bool>("playback");
        QTest::newRow("music") << false;
        QTest::newRow("playback") << true;
    }
    void failedOpenIdentifiesDatabase()
    {
        QFETCH(bool, playback);
        QTemporaryDir directory;
        const QString filename = playback ? "playback.sqlite" : "music.sqlite";
        const auto path = chinesePath(directory, filename);
        // A directory at the database filename fails deterministically on all
        // platforms, including developer machines with elevated privileges.
        QVERIFY(QDir().mkpath(path));
        if (playback) {
            PlaybackStorageWorker worker;
            QSignalSpy failures(&worker, &PlaybackStorageWorker::failed);
            worker.initialize(path);
            QCOMPARE(failures.size(), 1);
            worker.close();
        } else {
            DatabaseWorker worker;
            QSignalSpy failures(&worker, &DatabaseWorker::failed);
            worker.initialize(path);
            QCOMPARE(failures.size(), 1);
            worker.close();
        }
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first()["database"].toString(), filename);
        QCOMPARE(records.first()["operation"].toString(), QString("open"));
        QCOMPARE(records.first()["primary_code"].toInt(), 14);
        QCOMPARE(records.first()["error_type"].toInt(), int(QSqlError::ConnectionError));
    }
    void runtimeReadFailuresIdentifyDatabaseAndOperation()
    {
        QTemporaryDir directory;
        const auto musicPath = chinesePath(directory, "music.sqlite");
        const auto playbackPath = chinesePath(directory, "playback.sqlite");
        DatabaseWorker music;
        PlaybackStorageWorker playback;
        music.initialize(musicPath);
        playback.initialize(playbackPath);
        QVERIFY(records.isEmpty());
        {
            Database database(musicPath);
            QVERIFY(database.db.open());
            QSqlQuery query(database.db);
            QVERIFY(query.exec("DROP TABLE playlist_auto_covers"));
        }
        {
            Database database(playbackPath);
            QVERIFY(database.db.open());
            QSqlQuery query(database.db);
            QVERIFY(query.exec("DROP TABLE playback_history"));
        }
        QSignalSpy covers(&music, &DatabaseWorker::playlistCoversReady);
        QSignalSpy coverFailures(&music, &DatabaseWorker::playlistCoversFailed);
        QSignalSpy history(&playback, &PlaybackStorageWorker::historyLoaded);
        QSignalSpy historyFailures(&playback, &PlaybackStorageWorker::historyFailed);
        music.loadPlaylistCovers(1, 42);
        playback.loadHistory(42, 1);
        QVERIFY(covers.isEmpty());
        QVERIFY(history.isEmpty());
        QCOMPARE(coverFailures.size(), 1);
        QCOMPARE(historyFailures.size(), 1);
        QCOMPARE(records.size(), 2);
        QCOMPARE(records[0]["operation"].toString(), QString("load_playlist_covers"));
        QCOMPARE(records[0]["database"].toString(), QString("music.sqlite"));
        QCOMPARE(records[1]["operation"].toString(), QString("load_history"));
        QCOMPARE(records[1]["database"].toString(), QString("playback.sqlite"));
        for (const auto &record : records) {
            QCOMPARE(record["primary_code"].toInt(), 1);
            // Qt 6.12 classifies a prepare failure as ConnectionError; preserve
            // the driver's classification instead of translating it ourselves.
            QCOMPARE(record["error_type"].toInt(), int(QSqlError::ConnectionError));
        }
        music.close();
        playback.close();
    }
    void failedWritesKeepOriginalErrorAndDoNotLogBoundCredentials()
    {
        QTemporaryDir directory;
        DatabaseWorker music;
        PlaybackStorageWorker playback;
        music.initialize(chinesePath(directory, "music.sqlite"));
        playback.initialize(chinesePath(directory, "playback.sqlite"));
        QSignalSpy accountFailures(&music, &DatabaseWorker::accountsFailed);
        StoredAccount account;
        account.userId = 42;
        account.token = "private-token-not-for-logs";
        account.refreshCredential = "private-refresh-not-for-logs";
        account.expiresAtSeconds = -1; // Real SQLite CHECK failure.
        music.saveAccount(1, account, true);
        QCOMPARE(accountFailures.size(), 1);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first()["operation"].toString(), QString("save_account.write"));
        QCOMPARE(records.first()["primary_code"].toInt(), 19);
        QVERIFY(records.first()["native_code"].toString().toInt() > 0xff);

        const PlaybackQueueState queue{"default", "Original", {sampleTrack()}, 0, {}};
        playback.save(42, {queue}, queue.key);
        QSignalSpy failures(&playback, &PlaybackStorageWorker::failed);
        playback.save(42, {queue, queue}, queue.key); // Duplicate primary key.
        QCOMPARE(failures.size(), 1);
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.last()["operation"].toString(), QString("save_queues.insert_queue"));
        QCOMPARE(records.last()["primary_code"].toInt(), 19);
        QVERIFY(records.last()["native_code"].toString().toInt() > 0xff);
        QSignalSpy loaded(&playback, &PlaybackStorageWorker::loaded);
        playback.load(42, 1);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first()[2].value<PlaybackQueueStates>().size(), 1);
        for (const auto &record : records) {
            const auto text = QJsonDocument(record).toJson();
            QVERIFY(!text.contains(account.token.toUtf8()));
            QVERIFY(!text.contains(account.refreshCredential.toUtf8()));
            QVERIFY(!record.contains("sql"));
            QVERIFY(!record.contains("parameters"));
        }
        music.close();
        playback.close();
    }
    void historyMergeFailureKeepsTheQueryErrorBeforeRollback()
    {
        QTemporaryDir directory;
        const auto path = chinesePath(directory, "playback.sqlite");
        PlaybackStorageWorker playback;
        playback.initialize(path);
        auto track = sampleTrack();
        track.id = "cloud:unmatched";
        playback.record(42, track, 1500);
        {
            Database database(path);
            QVERIFY(database.db.open());
            QSqlQuery query(database.db);
            // Seed a legacy alias of the same unmatched upload. A trigger
            // rejects the merge's UPDATE while allowing the original read.
            QVERIFY(query.exec("INSERT INTO playback_history SELECT account_id,'legacy-alias',"
                               "catalog_id,hash,title,artist,cover,played_at-1,listened_ms,"
                               "play_count,track_snapshot,payload_key FROM playback_history"));
            QVERIFY(query.exec("CREATE TRIGGER reject_merge BEFORE UPDATE ON playback_history "
                               "BEGIN SELECT RAISE(ABORT,'merge blocked'); END"));
        }
        QVERIFY(records.isEmpty());
        QSignalSpy failures(&playback, &PlaybackStorageWorker::failed);
        playback.loadHistory(42, 1);
        QCOMPARE(failures.size(), 1);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first()["operation"].toString(), QString("merge_history.update"));
        QCOMPARE(records.first()["primary_code"].toInt(), 19);
        QCOMPARE(records.first()["native_code"].toString().toInt(), 1811);
        QVERIFY(records.first()["database_text"].toString().contains("merge blocked"));
        playback.close();
        Database database(path);
        QVERIFY(database.db.open());
        QSqlQuery query(database.db);
        QVERIFY(query.exec("SELECT COUNT(*),SUM(listened_ms) FROM playback_history"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);
        QCOMPARE(query.value(1).toInt(), 3000);
    }
};
QTEST_GUILESS_MAIN(DatabaseDiagnosticsTest)
#include "DatabaseDiagnosticsTest.moc"
