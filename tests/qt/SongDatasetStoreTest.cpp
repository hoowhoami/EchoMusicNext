#include "SongDatasetStore.h"
#include "SessionCacheLoss.h"
#include "JsonRecordStore.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSemaphore>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <optional>
using namespace EchoMusic;
class SongDatasetStoreTest : public QObject
{
    Q_OBJECT
    template <typename T, typename Start> static T wait(Start start)
    {
        auto result = std::make_shared<std::optional<T>>();
        if (!start([result](T value) { *result = std::move(value); })) {
            T error;
            error.error = "not admitted";
            return error;
        }
        if (!QTest::qWaitFor([&] { return result->has_value(); }, 5000)) {
            T error;
            error.error = "timeout";
            return error;
        }
        return std::move(**result);
    }
    static void close(SessionDatabase &database)
    {
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
private slots:
    void totalLossRejectsOldLeasesEvenWhenRecordNumbersAreReused()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        SongDatasetStore store(database);
        auto old = store.create();
        const auto first = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append(old, {{"old", "Old", {}, {}, 1, 0, {{"id", "old"}}, {}}}, this,
                                std::move(done));
        });
        QVERIFY(first.error.isEmpty());
        QVERIFY(loseSessionCache(database));
        auto fresh = store.create();
        const auto second = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append(fresh, {{"new", "New", {}, {}, 1, 0, {{"id", "new"}}, {}}}, this,
                                std::move(done));
        });
        QVERIFY(second.error.isEmpty());
        QCOMPARE(first.rows.first().record, second.rows.first().record);
        const auto stale = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.read({old, first.rows}, 0, this, std::move(done)); });
        QVERIFY(!stale.error.isEmpty());
        QVERIFY(stale.values.isEmpty());
        const auto staleWrite = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append(old, {{"stale", "Stale", {}, {}, 1, 1, {{"id", "stale"}}, {}}},
                                this, std::move(done));
        });
        QVERIFY(!staleWrite.error.isEmpty());
        const auto rows = wait<SongDatasetStore::View>(
            [&](auto done) { return store.query(fresh, {}, 0, 0, this, std::move(done)); });
        QCOMPARE(rows.rows.size(), 1);
        close(database);
    }
    void externalCacheCleanupPreservesSongSnapshotsAndNonSongRecords()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows does not permit this fixture to unlink open handles");
#else
        QTemporaryDir directory;
        const auto root = directory.filePath("session");
        SessionDatabase database(root);
        SongDatasetStore store(database);
        JsonRecordStore records(database);
        const QJsonObject complete{{"id", "track"},
                                   {"opaque", QJsonValue::Null},
                                   {"resources", QJsonArray{QString(32000, 'x')}}};
        auto generation = store.create();
        const auto written = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append(generation,
                                {{"track",
                                  "Title",
                                  "Artist",
                                  "Album",
                                  123,
                                  0,
                                  complete,
                                  {{"title", "Presentation"}}}},
                                this, std::move(done));
        });
        QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
        const SongDatasetStore::Snapshot snapshot{generation, written.rows};
        const QJsonArray authors{QJsonObject{{"id", "author"}, {"opaque", QJsonValue::Null}}};
        auto authorSnapshot = records.create(authors);
        QTRY_VERIFY(records.persisted(authorSnapshot));
        QVERIFY(QDir(root).removeRecursively());
        const auto restored = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.read(snapshot, 0, this, std::move(done), true); });
        QVERIFY2(restored.error.isEmpty(), qPrintable(restored.error));
        QCOMPARE(restored.values, QList<QJsonObject>{complete});
        QCOMPARE(restored.presentations.first().value("title").toString(), "Presentation");
        std::optional<QJsonArray> readAuthors;
        QString readError;
        records.readAll(authorSnapshot, this, [&](QJsonArray values, QString error) {
            readAuthors = std::move(values);
            readError = std::move(error);
        });
        QTRY_VERIFY(readAuthors.has_value());
        QVERIFY2(readError.isEmpty(), qPrintable(readError));
        QCOMPARE(*readAuthors, authors);
        const auto appended = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append(generation,
                                {{"second", "Second", {}, {}, 1, 1, {{"id", "second"}}, {}}}, this,
                                std::move(done));
        });
        QVERIFY2(appended.error.isEmpty(), qPrintable(appended.error));
        QCOMPARE(appended.rows.size(), 1);
        QVERIFY(appended.rows.first().record != snapshot.rows.first().record);
        generation.reset();
        authorSnapshot.reset();
        close(database);
#endif
    }
    void externalSourcesHydrateOnlyCommandsAndHonorPartialReadProgress()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        SongDatasetStore store(database);
        int reads = 0;
        auto generation = store.create([&](auto placeholders, QObject *consumer, auto done) {
            ++reads;
            QList<QJsonObject> full;
            for (int i = 0; i < placeholders.size() && i < 2; ++i) {
                auto row = placeholders[i];
                row.insert("opaque", QJsonValue::Null);
                row.insert("resources", QJsonArray{QString(10000, 'x')});
                full.append(row);
            }
            QTimer::singleShot(0, consumer,
                               [done = std::move(done), full = std::move(full)]() mutable {
                                   done(std::move(full), {});
                               });
        });
        QList<SongDatasetStore::Row> rows;
        for (int i = 0; i < 5; ++i)
            rows.append(
                {QString::number(i), "Title", {}, {}, 0, i, {{"id", i}}, {{"title", "View"}}});
        const auto written = wait<SongDatasetStore::Written>(
            [&](auto done) { return store.append(generation, rows, this, std::move(done)); });
        QVERIFY(written.error.isEmpty());
        const SongDatasetStore::Snapshot snapshot{generation, written.rows};
        const auto views = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.readPresentations(snapshot, 0, this, std::move(done)); });
        QVERIFY(views.error.isEmpty());
        QCOMPARE(views.values.size(), 5);
        QCOMPARE(reads, 0);
        const auto first = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.read(snapshot, 0, this, std::move(done), true); });
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QCOMPARE(first.next, 2);
        QCOMPARE(first.values.size(), 2);
        QCOMPARE(first.presentations.size(), 2);
        QVERIFY(first.values.first().value("opaque").isNull());
        QCOMPARE(reads, 1);
        const auto second = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.read(snapshot, first.next, this, std::move(done)); });
        QVERIFY(second.error.isEmpty());
        QCOMPARE(second.next, 4);
        QCOMPARE(second.values.first().value("id").toInt(), 2);
        close(database);
    }
    void sharedReferencesKeepMultipleSourceLeasesThroughCancelledReads()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        SongDatasetStore store(database);
        auto gate = std::make_shared<QSemaphore>();
        int delivered = 0;
        {
            auto a = store.create(), b = store.create();
            auto first = wait<SongDatasetStore::Written>([&](auto done) {
                return store.append(a,
                                    {{"1",
                                      "Beta",
                                      "Artist",
                                      "Album",
                                      10,
                                      0,
                                      {{"id", "1"}, {"opaque", QJsonValue::Null}},
                                      {{"song", QJsonObject{{"id", "1"}}}}}},
                                    this, std::move(done));
            });
            auto second = wait<SongDatasetStore::Written>([&](auto done) {
                return store.append(b,
                                    {{"2",
                                      "Alpha",
                                      "Artist",
                                      "Album",
                                      20,
                                      0,
                                      {{"id", "2"}, {"opaque", 42}},
                                      {{"song", QJsonObject{{"id", "2"}}}}}},
                                    this, std::move(done));
            });
            QVERIFY(first.error.isEmpty() && second.error.isEmpty());
            auto other = second.rows.first();
            other.source = b;
            other.ordinal = 1;
            SongDatasetStore::Snapshot snapshot{a, {first.rows.first(), other}};
            const auto read = wait<SongDatasetStore::Read>(
                [&](auto done) { return store.read(snapshot, 0, this, std::move(done), true); });
            QVERIFY2(read.error.isEmpty(), qPrintable(read.error));
            QCOMPARE(read.values.size(), 2);
            QVERIFY(read.values[0].value("opaque").isNull());
            QCOMPARE(read.values[1].value("opaque").toInt(), 42);
            const auto sorted = wait<SongDatasetStore::View>(
                [&](auto done) { return store.query(snapshot, "", 2, 1, this, std::move(done)); });
            QVERIFY2(sorted.error.isEmpty(), qPrintable(sorted.error));
            QCOMPARE(sorted.rows[0].id, QString("2"));
            QCOMPARE(sorted.rows[1].id, QString("1"));
            QVERIFY(database.submit(
                this, [gate](QSqlDatabase &, const QString &) { return gate->tryAcquire(1, 5000); },
                [](bool) {}));
            auto consumer = std::make_unique<QObject>();
            QVERIFY(store.read(std::move(snapshot), 0, consumer.get(), [&](auto) { ++delivered; }));
            consumer.reset();
            // The worker snapshot now outlives every GUI source owner. Its
            // result must carry both leases back before retirement is admitted.
        }
        gate->release();
        QTRY_COMPARE(database.pendingJobs(), 0);
        QCOMPARE(delivered, 0);
        std::optional<int> retained;
        QVERIFY(database.submit(
            this,
            [](QSqlDatabase &db, const QString &) {
                QSqlQuery query(db);
                return query.exec("SELECT count(*) FROM song_dataset") && query.next()
                           ? query.value(0).toInt()
                           : -1;
            },
            [&](int count) { retained = count; }));
        QTRY_VERIFY(retained.has_value());
        QCOMPARE(*retained, 0);
        close(database);
    }
    void presentationAndCombinedReadsHonorByteBudgetsAndOversizeProgress()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        SongDatasetStore store(database);
        auto generation = store.create();
        QList<SongDatasetStore::Row> rows;
        for (int i = 0; i < 3; ++i)
            rows.append({QString::number(i),
                         {},
                         {},
                         {},
                         0,
                         i,
                         {{"id", i}, {"body", QString(800 * 1024, 's')}},
                         {{"id", i}, {"body", QString((i == 2 ? 3072 : 800) * 1024, 'p')}}});
        const auto written = wait<SongDatasetStore::Written>(
            [&](auto done) { return store.append(generation, rows, this, std::move(done)); });
        QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
        const SongDatasetStore::Snapshot snapshot{generation, written.rows};
        const auto first = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.readPresentations(snapshot, 0, this, std::move(done)); });
        QVERIFY(first.error.isEmpty());
        QCOMPARE(first.next, 2);
        QCOMPARE(first.values.size(), 2);
        const auto oversize = wait<SongDatasetStore::Read>([&](auto done) {
            return store.readPresentations(snapshot, first.next, this, std::move(done));
        });
        QVERIFY(oversize.error.isEmpty());
        QCOMPARE(oversize.next, 3);
        QCOMPARE(oversize.values.size(), 1);
        QCOMPARE(oversize.values.first().value("id").toInt(), 2);
        const auto combined = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.read(snapshot, 0, this, std::move(done), true); });
        QVERIFY(combined.error.isEmpty());
        QCOMPARE(combined.next, 1);
        QCOMPARE(combined.values.size(), 1);
        QCOMPARE(combined.presentations.size(), 1);
        close(database);
    }
    void presentationReadsAndUpdatesStayIndependentOfImmutableSourceJson()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        SongDatasetStore store(database);
        auto generation = store.create();
        const QJsonObject source{{"id", "one"}, {"opaque", QJsonArray{1, QJsonValue::Null}}};
        const QJsonObject oldView{{"title", "Old"}, {"badge", QJsonArray{"VIP"}}};
        const QJsonObject secondView{{"title", "Other"}, {"unknown", QJsonValue::Null}};
        auto written = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append(
                generation,
                {{"one", "Old", "Artist", "Album", 1000, 0, source, oldView},
                 {"two", "Other", "Artist", "Album", 2000, 1, {{"id", "two"}}, secondView}},
                this, std::move(done));
        });
        QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
        const SongDatasetStore::Snapshot all{generation, written.rows};
        const QJsonObject newView{{"title", "新 Title"}, {"cover", QJsonValue::Null}};
        const auto updated = wait<SongDatasetStore::Updated>([&](auto done) {
            return store.updatePresentations(
                generation,
                {{written.rows[0].record, "新 Title", "Artist", "Album", 1000, newView}}, this,
                std::move(done));
        });
        QVERIFY2(updated.error.isEmpty(), qPrintable(updated.error));
        const auto found = wait<SongDatasetStore::View>(
            [&](auto done) { return store.query(generation, "新", 2, 1, this, std::move(done)); });
        QCOMPARE(found.rows, QList<SongDatasetStore::Reference>{written.rows[0]});
        const auto combined = wait<SongDatasetStore::Read>([&](auto done) {
            return store.read({generation, {written.rows[0]}}, 0, this, std::move(done), true);
        });
        QVERIFY(combined.error.isEmpty());
        QCOMPARE(combined.values, QList<QJsonObject>{source});
        QCOMPARE(combined.presentations, QList<QJsonObject>{newView});
        const auto rejected = wait<SongDatasetStore::Updated>([&](auto done) {
            return store.updatePresentations(generation,
                                             {{written.rows[0].record, "Wrong", {}, {}, 0, {}},
                                              {999999, "Missing", {}, {}, 0, {}}},
                                             this, std::move(done));
        });
        QVERIFY(!rejected.error.isEmpty());
        const auto preserved = wait<SongDatasetStore::Read>(
            [&](auto done) { return store.readPresentations(all, 0, this, std::move(done)); });
        QCOMPARE(preserved.values, (QList<QJsonObject>{newView, secondView}));
        bool corrupted = false;
        QVERIFY(database.submit(
            this,
            [first = written.rows[0].record, second = written.rows[1].record](QSqlDatabase &db,
                                                                              const QString &) {
                QSqlQuery query(db);
                return query.exec(QString("UPDATE song_payload SET payload=X'00' WHERE record=%1")
                                      .arg(second)) &&
                       query.exec(
                           QString("UPDATE song_dataset SET presentation=X'00' WHERE record=%1")
                               .arg(first));
            },
            [&](bool ok) { corrupted = ok; }));
        QTRY_VERIFY(corrupted);
        const auto goodPresentation = wait<SongDatasetStore::Read>([&](auto done) {
            return store.readPresentations({generation, {written.rows[1]}}, 0, this,
                                           std::move(done));
        });
        QCOMPARE(goodPresentation.values, QList<QJsonObject>{secondView});
        const auto goodSource = wait<SongDatasetStore::Read>([&](auto done) {
            return store.read({generation, {written.rows[0]}}, 0, this, std::move(done));
        });
        QCOMPARE(goodSource.values, QList<QJsonObject>{source});
        const auto broken = wait<SongDatasetStore::Read>([&](auto done) {
            return store.readPresentations({generation, {written.rows[0]}}, 0, this,
                                           std::move(done));
        });
        QVERIFY(!broken.error.isEmpty());
        QVERIFY(broken.values.isEmpty());
        const auto invalid = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append({}, {{"", {}, {}, {}, 0, 0, {}}}, this, std::move(done));
        });
        QVERIFY(!invalid.error.isEmpty());
        close(database);
    }
    void queryColumnsDoNotParseOtherSongsAndReadsPreserveUnknownFields()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        SongDatasetStore store(database);
        auto generation = store.create();
        const QJsonObject song{{"id", "one"},
                               {"unknown", QJsonObject{{"null", QJsonValue::Null},
                                                       {"value", QJsonArray{true, 1, "中文"}}}}};
        const auto written = wait<SongDatasetStore::Written>([&](auto done) {
            return store.append(generation,
                                {{"one", "Älbum", "Artist", "Album", 1000, 0, song},
                                 {"two", "Other", "Artist", "Album", 2000, 1, {}}},
                                this, std::move(done));
        });
        QVERIFY2(written.error.isEmpty(), qPrintable(written.error));
        auto corrupt = std::make_shared<std::optional<QString>>();
        QVERIFY(database.submit(
            this,
            [record = written.rows[1].record](QSqlDatabase &db, const QString &error) {
                if (!error.isEmpty())
                    return error;
                QSqlQuery query(db);
                query.prepare("UPDATE song_payload SET payload=X'00' WHERE record=?");
                query.addBindValue(record);
                query.exec();
                return query.lastError().text();
            },
            [corrupt](auto value) { *corrupt = std::move(value); }));
        QTRY_VERIFY(corrupt->has_value());
        QVERIFY((**corrupt).isEmpty());
        const auto found = wait<SongDatasetStore::View>(
            [&](auto done) { return store.query(generation, "äl", 2, 1, this, std::move(done)); });
        QVERIFY2(found.error.isEmpty(), qPrintable(found.error));
        QCOMPARE(found.rows.size(), 1);
        QCOMPARE(found.rows[0].id, QString("one"));
        const auto read = wait<SongDatasetStore::Read>([&](auto done) {
            return store.read({generation, found.rows}, 0, this, std::move(done));
        });
        QVERIFY(read.error.isEmpty());
        QCOMPARE(read.values, QList<QJsonObject>{song});
        const auto broken = wait<SongDatasetStore::Read>([&](auto done) {
            return store.read({generation, {written.rows[1]}}, 0, this, std::move(done));
        });
        QVERIFY(!broken.error.isEmpty());
        QVERIFY(broken.values.isEmpty());
        close(database);
    }
    void stableCollationAndCursorReadsKeepSnapshotOrder()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        SongDatasetStore store(database);
        auto generation = store.create();
        QList<SongDatasetStore::Row> rows;
        for (int i = 0; i < 300; ++i)
            rows.append(
                {QString::number(i), i % 2 ? "echo" : "Écho", "artist", "album", quint64(i), i,
                 QJsonObject{{"id", QString::number(i)}, {"extension", QString(1024, 'x')}}});
        const auto written = wait<SongDatasetStore::Written>(
            [&](auto done) { return store.append(generation, rows, this, std::move(done)); });
        QVERIFY(written.error.isEmpty());
        const SongDatasetStore::Snapshot snapshot{generation, written.rows};
        for (int direction : {1, 2}) {
            const auto view = wait<SongDatasetStore::View>([&](auto done) {
                return store.query(generation, "", 2, direction, this, std::move(done));
            });
            QVERIFY(view.error.isEmpty());
            QCOMPARE(view.rows,
                     written.rows); // equal normalized keys retain source order in both directions
        }
        const auto duration = wait<SongDatasetStore::View>(
            [&](auto done) { return store.query(generation, "", 5, 2, this, std::move(done)); });
        QCOMPARE(duration.rows.first().id, QString("299"));
        qsizetype cursor = 0;
        QList<QJsonObject> values;
        while (cursor < snapshot.rows.size()) {
            auto part = wait<SongDatasetStore::Read>(
                [&](auto done) { return store.read(snapshot, cursor, this, std::move(done)); });
            QVERIFY2(part.error.isEmpty(), qPrintable(part.error));
            QVERIFY(part.next > cursor);
            QVERIFY(part.values.size() <= 128);
            cursor = part.next;
            values += part.values;
        }
        QCOMPARE(values.size(), rows.size());
        for (int i = 0; i < rows.size(); ++i)
            QCOMPARE(values[i], rows[i].song);
        close(database);
    }
};
QTEST_GUILESS_MAIN(SongDatasetStoreTest)
#include "SongDatasetStoreTest.moc"
