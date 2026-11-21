#include "MenuContextRead.h"
#include "CatalogRead.h"
#include "SongSourceCache.h"
#include "SongCatalogActions.h"
#include "SongCatalogModel.h"
#include "SongMetadataService.h"
#include "SettingsStore.h"
#include <QAbstractItemModelTester>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSemaphore>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class SongCatalogStorageTestAccess
{
public:
    static bool stored(const SongCatalogModel &model)
    {
        return !model.m_writing && model.m_writeCursor == model.m_entries.size() &&
               !model.m_presentationWriting && model.m_dirtyPresentations.isEmpty() &&
               !model.m_metadataLoading && model.m_pendingMetadata.isEmpty();
    }
    static qint64 record(const SongCatalogModel &model, int row)
    {
        return model.m_entries[row].record;
    }
    static SongDatasetStore::Snapshot snapshot(const SongCatalogModel &model, int row)
    {
        const auto &entry = model.m_entries[row];
        return {model.m_generation, {{entry.record, row, entry.id}}};
    }
    static bool richPayloadRetired(const SongCatalogModel &model)
    {
        for (const auto &entry : model.m_entries)
            if (!entry.record ||
                (entry.view &&
                 entry.view->domainSong.value("metadata").toObject().contains("opaque")))
                return false;
        return true;
    }
    static int residentViews(const SongCatalogModel &model)
    {
        QSet<int> rows;
        for (const auto source : model.m_hotViews.keys())
            rows.insert(source);
        for (int i = 0; i < model.m_entries.size(); ++i)
            if (model.m_entries[i].view)
                rows.insert(i);
        return rows.size();
    }
    static void evictViews(SongCatalogModel &model)
    {
        model.m_hotViews.clear();
    }
};
} // namespace EchoMusic
class SongCatalogStorageTest : public QObject
{
    Q_OBJECT
    static QJsonObject song(int i)
    {
        return {{"id", QString::number(i)},
                {"hash", QString(32, 'a')},
                {"title", QString("Song %1").arg(i)},
                {"artists", QJsonArray{QJsonObject{{"id", "50"}, {"name", "Artist"}}}},
                {"duration_ms", i == 1 ? QJsonValue(QJsonValue::Null) : QJsonValue(i * 1000)},
                {"privilege", 10},
                {"metadata",
                 QJsonObject{{"opaque", QJsonObject{{"missing", QJsonValue::Null},
                                                    {"data", QString(4000, 'x')}}},
                             {"payment_type", 3},
                             {"resources", QJsonArray{QJsonObject{{"quality", "flac"},
                                                                  {"hash", QString(32, 'b')}}}}}}};
    }
    static void close(SessionDatabase &database)
    {
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
private slots:
    void sharedSourcesServeMultipleWindowsWithoutDuplicatingPayloads()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongSourceCache source(database);
            std::optional<QList<SongDatasetStore::CachedRow>> records;
            QString error;
            QJsonArray songs;
            for (int i = 1; i <= 600; ++i)
                songs.append(song(i));
            source.cache(songs, this, [&](auto rows, auto failure) {
                records = std::move(rows);
                error = failure;
            });
            QTRY_VERIFY(records.has_value());
            QVERIFY2(error.isEmpty(), qPrintable(error));
            SongCatalogModel first, second;
            first.enableSessionStorage(database);
            second.enableSessionStorage(database);
            QAbstractItemModelTester tester(&first,
                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
            first.replaceCachedRows(*records);
            second.replaceCachedRows(*records);
            QCOMPARE(first.totalCount(), 600);
            QCOMPARE(second.totalCount(), 600);
            QVERIFY(SongCatalogStorageTestAccess::residentViews(first) == 0);
            QTRY_COMPARE(first.sourceItem(599).value("title").toString(), QString("Song 600"));
            first.setQuery("Song 60");
            first.toggleSort(2);
            QTRY_VERIFY(!first.querying());
            QCOMPARE(first.count(), 2);
            const auto tracks = readCatalogTracks(&first);
            QCOMPARE(tracks.size(), 2);
            QCOMPARE(tracks.first().id, QString("60"));
            QVERIFY(tracks.first()
                        .catalogMetadata.value("opaque")
                        .toObject()
                        .value("missing")
                        .isNull());
            first.toggleSelected("600");
            first.replaceCachedRows(*records, true);
            QCOMPARE(first.selectedCount(), 1);
            QSignalSpy failures(&first, &SongCatalogModel::storageFailed);
            first.applyPresentations(
                {QJsonObject{{"id", "600"}, {"cover_url", "https://example.com/current.jpg"}}});
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(first));
            QCOMPARE(failures.size(), 0);
            const auto reflected = readCatalogTracks(&second);
            QCOMPARE(reflected.last().cover, QString("https://example.com/current.jpg"));
            first.clear();
            records.reset();
            const auto all = readCatalogTracks(&second);
            QCOMPARE(all.size(), 600);
            std::optional<int> count;
            QVERIFY(database.submit(
                this,
                [](QSqlDatabase &db, const QString &) {
                    QSqlQuery query(db);
                    return query.exec("SELECT count(*) FROM song_payload") && query.next()
                               ? query.value(0).toInt()
                               : -1;
                },
                [&](int value) { count = value; }));
            QTRY_VERIFY(count.has_value());
            QCOMPARE(*count, 600);
            QVERIFY(SongCatalogStorageTestAccess::residentViews(second) <= 256);
        }
        close(database);
    }
    void admittedSharedCommandsSurviveAppendAndReorderWithoutIncludingNewRows()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongSourceCache source(database);
            std::optional<QList<SongDatasetStore::CachedRow>> records;
            source.cache({song(1), song(2), song(3)}, this, [&](auto rows, auto error) {
                QVERIFY2(error.isEmpty(), qPrintable(error));
                records = std::move(rows);
            });
            QTRY_VERIFY(records.has_value());
            SongCatalogModel model;
            model.enableSessionStorage(database);
            model.replaceCachedRows(records->first(2));
            auto gate = std::make_shared<QSemaphore>();
            struct Release
            {
                std::shared_ptr<QSemaphore> gate;
                ~Release()
                {
                    gate->release();
                }
            } release{gate};
            QVERIFY(database.submit(
                this, [gate](QSqlDatabase &, const QString &) { return gate->tryAcquire(1, 5000); },
                [](bool) {}));
            std::optional<PlaybackTracks> tracks;
            model.requestTracks(false, this, [&](auto values) { tracks = std::move(values); });
            model.replaceCachedRows(*records, true);
            std::reverse(records->begin(), records->end());
            model.replaceCachedRows(*records, true);
            gate->release();
            QTRY_VERIFY(tracks.has_value());
            QCOMPARE(tracks->size(), 2);
            QCOMPARE(tracks->first().id, QString("1"));
            QCOMPARE(tracks->last().id, QString("2"));
            QCOMPARE(model.totalCount(), 3);
        }
        close(database);
    }
    void coldWindowsStayBoundedAndActionsResolveTheFrozenWholeList()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongCatalogModel model;
            model.enableSessionStorage(database);
            QAbstractItemModelTester tester(&model,
                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
            QJsonArray songs;
            for (int i = 1; i <= 1200; ++i)
                songs.append(song(i));
            model.replaceSongs(songs);
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            QVERIFY(SongCatalogStorageTestAccess::residentViews(model) <= 256);
            SongCatalogStorageTestAccess::evictViews(model);
            const auto key = model.trackProjection("1199");
            QVERIFY(key.has_value());
            QCOMPARE(key->hash, QString(32, 'a'));
            QCOMPARE(key->songId, quint64(1199));
            QTRY_COMPARE(model.sourceItem(1198).value("title").toString(), QString("Song 1199"));
            model.setQuery("Song 119");
            model.toggleSort(5);
            model.toggleSort(5);
            std::optional<PlaybackTracks> actual;
            model.requestTracks(false, &model, [&](auto tracks) { actual = std::move(tracks); });
            model.setQuery("Song 12");
            QTRY_VERIFY(actual.has_value());
            QCOMPARE(actual->size(), 11);
            for (int i = 0; i < 10; ++i)
                QCOMPARE((*actual)[i].toDomain(),
                         PlaybackTrack::fromDomain(song(1199 - i)).toDomain());
            QCOMPARE(actual->last().id, QString("119"));
            QTRY_VERIFY(!model.querying());
            model.toggleSelected("1");
            model.toggleSelected("1200");
            actual.reset();
            model.requestTracks(true, &model, [&](auto tracks) { actual = std::move(tracks); });
            QTRY_VERIFY(actual.has_value());
            QCOMPARE(actual->size(), 2);
            QCOMPARE(actual->last().toDomain(), PlaybackTrack::fromDomain(song(1200)).toDomain());
            QVERIFY(SongCatalogStorageTestAccess::residentViews(model) <= 256);
        }
        close(database);
    }
    void coldPluginMenusKeepPublicDataAndRetireWithTheirConsumer()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SettingsStore settings(directory.filePath("prefs.ini"));
            ApiClient api(DeviceIdentity::temporary());
            PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
            SongCatalogModel model;
            SongCatalogActions actions(model, playback);
            model.enableSessionStorage(database);
            QJsonArray songs;
            for (int i = 1; i <= 600; ++i) {
                auto value = song(i);
                auto metadata = value.value("metadata").toObject();
                metadata.insert("plugin_data", QJsonObject{{"exact", i}});
                value.insert("metadata", metadata);
                songs.append(value);
            }
            model.replaceSongs(songs);
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            model.selectAll(true);
            auto consumer = std::make_unique<QObject>();
            QSignalSpy ready(&actions, &SongCatalogActions::pluginMenuContextReady);
            const auto ticket = actions.requestPluginMenuContext("599", consumer.get());
            QTRY_COMPARE(ready.size(), 1);
            const auto context = ready.first()[1].toMap();
            QCOMPARE(context.value("track").toMap().value("data").toMap().value("exact").toInt(),
                     599);
            QCOMPARE(context.value("selectedTracks").toList().size(), 500);
            QCOMPARE(context.value("selectionCount").toInt(), 600);
            QVERIFY(context.value("truncated").toBool());
            QVERIFY(actions.pluginMenuContextCurrent(ticket));
            model.toggleSelected("1");
            QVERIFY(!actions.pluginMenuContextCurrent(ticket));
            consumer.reset();
            QVERIFY(!actions.pluginMenuContextCurrent(ticket));
            auto cancelled = std::make_unique<QObject>();
            actions.requestPluginMenuContext("1", cancelled.get());
            cancelled.reset();
            QCoreApplication::processEvents();
            QCOMPARE(ready.size(), 1);
        }
        close(database);
    }
    void failedWritesStopProducerCapacityAndRetainAcceptedSourceFacts()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongCatalogModel model;
            model.enableSessionStorage(database);
            model.replaceSongs({song(1)});
            QTRY_VERIFY(model.storageReady());
            std::optional<bool> installed;
            QVERIFY(database.submit(
                &model,
                [](QSqlDatabase &db, const QString &) {
                    QSqlQuery query(db);
                    return query.exec(
                        "CREATE TEMP TRIGGER reject_song BEFORE INSERT ON song_dataset "
                        "BEGIN SELECT RAISE(ABORT,'controlled cache write failure'); END");
                },
                [&](bool ok) { installed = ok; }));
            QTRY_VERIFY(installed.has_value());
            QVERIFY(*installed);
            QSignalSpy failed(&model, &SongCatalogModel::storageFailed);
            model.appendSongs({song(2)});
            model.setQuery("Song");
            QTRY_COMPARE(failed.size(), 1);
            QVERIFY(!model.storageReady());
            QVERIFY(!model.querying());
            QCOMPARE(model.totalCount(), 2);
            std::optional<PlaybackTrack> actual;
            model.requestTrack("2", &model, [&](auto track) { actual = std::move(track); });
            QTRY_VERIFY(actual.has_value());
            QCOMPARE(actual->toDomain(), PlaybackTrack::fromDomain(song(2)).toDomain());
            model.toggleSelected("1");
            installed.reset();
            QVERIFY(database.submit(
                &model,
                [](QSqlDatabase &db, const QString &) {
                    QSqlQuery query(db);
                    return query.exec("DROP TRIGGER reject_song");
                },
                [&](bool ok) { installed = ok; }));
            QTRY_VERIFY(installed.has_value());
            QVERIFY(*installed);
            QVERIFY(model.retryStorage());
            QTRY_VERIFY(model.storageReady() && !model.querying());
            QCOMPARE(model.totalCount(), 2);
            QCOMPARE(model.selectedIds(), QStringList{"1"});
            QVERIFY(!model.retryStorage());
            std::optional<PlaybackTracks> recovered;
            model.requestTracks(false, &model, [&](auto tracks) { recovered = std::move(tracks); });
            QTRY_VERIFY(recovered.has_value());
            QCOMPARE(recovered->size(), 2);
            QCOMPARE(recovered->last().toDomain(), PlaybackTrack::fromDomain(song(2)).toDomain());
        }
        close(database);
    }
    void pendingQueriesAndActionsKeepTheirOwnParametersAndMembership()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongCatalogModel model;
            model.enableSessionStorage(database);
            QAbstractItemModelTester tester(&model,
                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
            QJsonArray songs;
            for (int i = 1; i <= 30; ++i)
                songs.append(song(i));
            model.replaceSongs(songs);
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            auto gate = std::make_shared<QSemaphore>();
            struct Release
            {
                std::shared_ptr<QSemaphore> gate;
                ~Release()
                {
                    gate->release();
                }
            } release{gate};
            QVERIFY(database.submit(
                this, [gate](QSqlDatabase &, const QString &) { return gate->tryAcquire(1, 5000); },
                [](bool) {}));
            model.setQuery("Song 2");
            model.toggleSort(5);
            model.toggleSort(5);
            QVERIFY(model.querying());
            // The old view remains usable while a worker query is pending.
            QCOMPARE(model.count(), 30);
            std::optional<PlaybackTracks> actual;
            model.requestTracks(false, this, [&](auto tracks) { actual = std::move(tracks); });
            model.appendSongs({song(200)});
            model.setQuery("Song 1");
            model.clearSort();
            gate->release();
            QTRY_VERIFY(actual.has_value());
            QCOMPARE(actual->size(), 11);
            for (int i = 0; i < 10; ++i)
                QCOMPARE((*actual)[i].id, QString::number(29 - i));
            QCOMPARE(actual->last().id, QString("2"));
            QTRY_VERIFY(!model.querying());
            QCOMPARE(model.count(), 11);
            QCOMPARE(model.data(model.index(0), SongCatalogModel::ResourceId).toString(),
                     QString("1"));
            QCOMPARE(model.data(model.index(10), SongCatalogModel::ResourceId).toString(),
                     QString("19"));
        }
        close(database);
    }
    void aPendingActionKeepsFullFieldsAfterTheSourceWriteIsConfirmed()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongCatalogModel model;
            model.enableSessionStorage(database);
            model.replaceSongs({song(1)});
            std::optional<PlaybackTrack> received;
            model.requestTrack("1", &model, [&](auto track) { received = std::move(track); });
            auto marker = std::make_shared<QSemaphore>();
            QVERIFY(database.submit(
                &model,
                [marker](QSqlDatabase &, const QString &) {
                    marker->release();
                    return true;
                },
                [](bool) {}));
            // Finish SQL, then deliver only database completions. The action's
            // own queued timer deliberately runs after payload retirement.
            QVERIFY(marker->tryAcquire(1, 5000));
            QCoreApplication::sendPostedEvents(&database, QEvent::MetaCall);
            QTRY_VERIFY(received.has_value());
            QCOMPARE(received->toDomain(), PlaybackTrack::fromDomain(song(1)).toDomain());
        }
        close(database);
    }
    void metadataArrivingBeforeTheSourceCommitStillPublishesTheLatestSqlView()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            auto gate = std::make_shared<QSemaphore>();
            struct Release
            {
                std::shared_ptr<QSemaphore> gate;
                ~Release()
                {
                    gate->release();
                }
            } release{gate};
            QVERIFY(database.submit(
                this, [gate](QSqlDatabase &, const QString &) { return gate->tryAcquire(1, 5000); },
                [](bool) {}));
            SongCatalogModel model;
            model.enableSessionStorage(database);
            model.replaceSongs({song(1)});
            model.applyPresentations(
                {QJsonObject{{"id", "1"}, {"album_id", "88"}, {"album_title", "During write"}}});
            gate->release();
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            SongDatasetStore reader(database);
            std::optional<SongDatasetStore::Read> saved;
            QVERIFY(reader.readPresentations(SongCatalogStorageTestAccess::snapshot(model, 0), 0,
                                             &model,
                                             [&](auto result) { saved = std::move(result); }));
            QTRY_VERIFY(saved.has_value());
            QVERIFY2(saved->error.isEmpty(), qPrintable(saved->error));
            QCOMPARE(saved->values.first().value("song").toObject().value("album_title").toString(),
                     QString("During write"));
            QCOMPARE(saved->values.first().value("quality").toString(), QString("SQ"));
        }
        close(database);
    }
    void committedRichRowsRetireAndCommandsReadTheWholeSnapshot()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongCatalogModel model;
            model.enableSessionStorage(database);
            QAbstractItemModelTester tester(&model,
                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
            QJsonArray songs;
            for (int i = 1; i <= 300; ++i)
                songs.append(song(i));
            model.replaceSongs(songs);
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            QVERIFY(SongCatalogStorageTestAccess::richPayloadRetired(model));
            QTRY_COMPARE(model.item(0).value("qualityLabel").toString(), QString("SQ"));
            QVERIFY(model.item(0).value("durationText").toString().isEmpty());
            QCOMPARE(model.item(0).value("privilegeTags").toList().size(), 1);
            bool done = false;
            PlaybackTracks actual;
            model.requestTracks(false, this, [&](auto tracks) {
                actual = std::move(tracks);
                done = true;
            });
            // The action retains source order and full membership despite later UI changes.
            model.toggleSort(5);
            model.toggleSort(5);
            model.setQuery("Song 2");
            model.selectAll(true);
            QTRY_VERIFY(done);
            QCOMPARE(actual.size(), 300);
            for (int i = 0; i < actual.size(); ++i)
                QCOMPARE(actual[i].toDomain(), PlaybackTrack::fromDomain(song(i + 1)).toDomain());
            model.clearSort();
            model.setQuery("");
            model.clearSelection();
            model.toggleSelected("3");
            model.toggleSelected("299");
            done = false;
            model.requestTracks(true, this, [&](auto tracks) {
                actual = std::move(tracks);
                done = true;
            });
            model.clearSelection();
            QTRY_VERIFY(done);
            QCOMPARE(actual.size(), 2);
            QCOMPARE(actual[0].id, QString("3"));
            QCOMPARE(actual[1].id, QString("299"));
        }
        close(database);
    }
    void presentationOverlayPreservesRightsAndOpaqueMetadataInActualActions()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SettingsStore settings(directory.filePath("prefs.ini"));
            ApiClient api(DeviceIdentity::temporary());
            SongMetadataService metadata(api);
            metadata.enableSqlIndex(database);
            PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
            playback.attachMetadata(metadata);
            SongCatalogModel model;
            model.setMetadataActive(false);
            SongCatalogActions actions(model, playback);
            model.enableSessionStorage();
            model.replaceSongs({song(1)});
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            model.applyPresentations({QJsonObject{{"id", "1"},
                                                  {"album_id", "88"},
                                                  {"album_title", "Album"},
                                                  {"cover_url", "https://example.test/art.jpg"}}});
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            SongDatasetStore reader(database);
            const auto snapshot = SongCatalogStorageTestAccess::snapshot(model, 0);
            std::optional<SongDatasetStore::Read> saved;
            QVERIFY(reader.readPresentations(snapshot, 0, &model,
                                             [&](auto result) { saved = std::move(result); }));
            QTRY_VERIFY(saved.has_value());
            QVERIFY2(saved->error.isEmpty(), qPrintable(saved->error));
            QCOMPARE(saved->values.first().value("quality").toString(), QString("SQ"));
            QCOMPARE(saved->values.first().value("song").toObject().value("album_title").toString(),
                     QString("Album"));
            std::optional<SongDatasetStore::View> matching;
            QVERIFY(reader.query(snapshot.generation, "Album", 4, 1, &model,
                                 [&](auto result) { matching = std::move(result); }));
            QTRY_VERIFY(matching.has_value());
            QCOMPARE(matching->rows, snapshot.rows);
            QTRY_COMPARE(model.item(0).value("qualityLabel").toString(), QString("SQ"));
            QCOMPARE(model.item(0).value("privilegeTags").toList().size(), 1);
            model.toggleSelected("1");
            actions.appendSelected();
            QTRY_COMPARE(playback.queue()->tracks().size(), 1);
            const auto actual = playback.queue()->tracks().first();
            auto expected = PlaybackTrack::fromDomain(song(1));
            expected.applyPresentation(SongMetadataService::completeSong(
                song(1), QJsonObject{{"id", "1"},
                                     {"album_id", "88"},
                                     {"album_title", "Album"},
                                     {"cover_url", "https://example.test/art.jpg"}}));
            QCOMPARE(actual.toDomain(), expected.toDomain());
            QCOMPARE(readMenuContext(actions, "1").value("track").toMap(), actual.publicValue());
        }
        close(database);
    }
    void replacementAndDestroyedConsumerCancelAndCorruptionIsAnError()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SongCatalogModel model;
            model.enableSessionStorage(database);
            model.replaceSongs({song(1)});
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            int delivered = 0;
            model.requestTrack("1", this, [&](auto) { ++delivered; });
            model.replaceSongs({song(2)});
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            QCoreApplication::processEvents();
            QCOMPARE(delivered, 0);
            auto consumer = std::make_unique<QObject>();
            model.requestTrack("2", consumer.get(), [&](auto) { ++delivered; });
            consumer.reset();
            QCoreApplication::processEvents();
            QCOMPARE(delivered, 0);
            bool corrupted = false;
            QVERIFY(database.submit(
                this,
                [record = SongCatalogStorageTestAccess::record(model, 0)](QSqlDatabase &db,
                                                                          const QString &) {
                    QSqlQuery query(db);
                    query.prepare("UPDATE song_payload SET payload=X'00' WHERE record=?");
                    query.addBindValue(record);
                    return query.exec();
                },
                [&](bool ok) { corrupted = ok; }));
            QTRY_VERIFY(corrupted);
            QSignalSpy failed(&model, &SongCatalogModel::readFailed);
            model.requestTrack("2", this, [&](auto) { ++delivered; });
            QTRY_COMPARE(failed.size(), 1);
            QCOMPARE(delivered, 0);
            QCOMPARE(model.count(), 1);
            QTRY_COMPARE(model.item(0).value("title").toString(), QString("Song 2"));
            model.clear();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            std::optional<int> retained;
            QVERIFY(database.submit(
                this,
                [](QSqlDatabase &db, const QString &) {
                    QSqlQuery query(db);
                    return query.exec("SELECT (SELECT count(*) FROM song_dataset)+(SELECT count(*) "
                                      "FROM song_payload)") &&
                                   query.next()
                               ? query.value(0).toInt()
                               : -1;
                },
                [&](int count) { retained = count; }));
            QTRY_VERIFY(retained.has_value());
            QCOMPARE(*retained, 0);
        }
        close(database);
    }
    void aNewPlaybackIntentRetiresTheEarlierFullRead()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        {
            SettingsStore settings(directory.filePath("prefs.ini"));
            ApiClient api(DeviceIdentity::temporary());
            PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
            SongCatalogModel model;
            SongCatalogActions actions(model, playback);
            model.enableSessionStorage(database);
            QJsonArray songs;
            for (int i = 1; i <= 300; ++i)
                songs.append(song(i));
            model.replaceSongs(songs);
            QTRY_VERIFY(SongCatalogStorageTestAccess::stored(model));
            actions.play("1");
            actions.playNow("2");
            QTRY_COMPARE(playback.trackId(), QString("2"));
            QTRY_COMPARE(database.pendingJobs(), 0);
            QCOMPARE(playback.queue()->tracks().size(), 1);
            actions.play("1");
            playback.stop();
            QCoreApplication::processEvents();
            QTRY_COMPARE(database.pendingJobs(), 0);
            QVERIFY(!playback.playing());
            QVERIFY(playback.trackId() != QString("1"));
        }
        close(database);
    }
};
QTEST_MAIN(SongCatalogStorageTest)
#include "SongCatalogStorageTest.moc"
