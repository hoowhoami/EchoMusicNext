#include "SongMetadataService.h"
#include "SongObservationIndex.h"
#include "SessionDatabase.h"
#include <QImage>
#include "SongCatalogModel.h"
#include "PlaybackQueueModel.h"
#include "PlaybackService.h"
#include "SettingsStore.h"
#include <QTemporaryDir>
#include "SearchModel.h"
#include <QAbstractItemModelTester>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class SongCatalogStorageTestAccess
{
public:
    static bool idle(const SongCatalogModel &model)
    {
        return model.storageReady() && !model.m_metadataLoading &&
               model.m_pendingMetadata.isEmpty() && !model.m_presentationWriting &&
               model.m_dirtyPresentations.isEmpty();
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
};
class PlaybackServiceTestAccess
{
public:
    static quint64 embedded(PlaybackService &player, const PlaybackTrack &track)
    {
        player.m_track = track;
        player.m_duration = 180000;
        player.m_embeddedRequest = 900004;
        return player.m_sequence;
    }
};
class SongMetadataServiceTestAccess
{
public:
    static void complete(SongMetadataService &service, const QJsonArray &values)
    {
        QStringList ids;
        for (const auto &v : values)
            ids.append(v.toObject()["id"].toString());
        service.m_flights.insert(900001, ids);
        service.accept(900001, QJsonDocument(values).toJson());
    }
    static void flush(SongMetadataService &service)
    {
        service.notifyObservers();
    }
    static void reset(SongMetadataService &service)
    {
        service.clear();
    }
    static quint64 sqlOwner(const SongMetadataService &service, QObject *owner)
    {
        return service.m_observers.value(owner).sqlId;
    }
    static SongObservationIndex *sqlIndex(SongMetadataService &service)
    {
        return service.m_sqlIndex.get();
    }
    static bool sqlRegistrationPending(const SongMetadataService &service)
    {
        for (const auto &observer : service.m_observers)
            if (observer.sqlWriting || !observer.sqlPending.isEmpty())
                return true;
        return false;
    }
    static bool noResidentRelations(const SongMetadataService &service)
    {
        if (!service.m_listeners.isEmpty() || !service.m_coverIdentities.isEmpty())
            return false;
        for (const auto &observer : service.m_observers)
            if (!observer.rows.isEmpty())
                return false;
        return true;
    }
    static bool pendingContainsOnlyObservationInputs(const SongMetadataService &service)
    {
        for (const auto &observer : service.m_observers)
            for (const auto &entry : observer.sqlPending)
                if (entry.song.value("metadata").toObject().contains("resources") ||
                    entry.song.value("metadata").toObject().contains("opaque"))
                    return false;
        return true;
    }
    static int observers(const SongMetadataService &service)
    {
        return service.m_observers.size();
    }
    static int jobs(const SongMetadataService &service)
    {
        return service.m_jobs.size();
    }
    static int listeners(const SongMetadataService &service)
    {
        return service.m_listeners.size();
    }
    static bool singletonIndicesAreInline(const SongMetadataService &service)
    {
        for (const auto &observer : service.m_observers)
            for (const auto &rows : observer.rows)
                if (rows.size() == 1 && rows.usesHashStorage())
                    return false;
        for (const auto &listeners : service.m_listeners)
            if (listeners.size() == 1 && listeners.usesHashStorage())
                return false;
        return true;
    }
    static void expire(SongMetadataService &service)
    {
        const auto expire = [](auto &cache) {
            for (const auto &key : cache.keys())
                if (const auto value = cache.peek(key))
                    cache.insert(key, *value, 1);
        };
        expire(service.m_cache);
        expire(service.m_embedded);
        expire(service.m_coverFacts);
        QTest::qWait(2);
    }
    static int pendingKeys(const SongMetadataService &service)
    {
        return service.m_pendingKeys.size();
    }
    static QSet<QString> waitingIds(const SongMetadataService &service)
    {
        QSet<QString> result;
        for (const auto &job : service.m_jobs)
            result.unite(job.waiting);
        return result;
    }
    static void reserve(SongMetadataService &service, const QStringList &ids)
    {
        service.m_flights.insert(900002, ids);
    }
    static int flights(const SongMetadataService &service)
    {
        return service.m_flights.size();
    }
    static void drain(SongMetadataService &service)
    {
        service.drain();
    }
    static void deliver(SongMetadataService &service, quint64 request, const QJsonArray &values)
    {
        service.accept(request, QJsonDocument(values).toJson());
    }
    static QSet<QString> waiting(const SongMetadataService &service, quint64 ticket)
    {
        return service.m_jobs.value(ticket).waiting;
    }
    static void fail(SongMetadataService &service, quint64 request, bool business = false)
    {
        service.fail(request, business);
    }
    static void endCooldown(SongMetadataService &service)
    {
        service.m_cooldown = 0;
    }
    static bool retryTimer(const SongMetadataService &service)
    {
        return service.m_retry.isActive();
    }
    static void retryNow(SongMetadataService &service)
    {
        service.m_cooldown = 0;
        service.m_retry.start(0);
    }
    static quint64 firstFlight(const SongMetadataService &service)
    {
        return service.m_flights.isEmpty() ? 0 : service.m_flights.constBegin().key();
    }
    static QList<int> batchSizes(const SongMetadataService &service)
    {
        QList<int> sizes;
        for (const auto &ids : service.m_flights)
            sizes.append(ids.size());
        return sizes;
    }
    static int embedded(const SongMetadataService &service)
    {
        return service.m_embedded.size();
    }
};
class SearchModelTestAccess
{
public:
    static void accept(SearchModel &model, ApiClient &api, SearchModel::Kind kind,
                       const QJsonArray &songs)
    {
        model.m_kind = kind;
        auto &bucket = model.active();
        bucket.request = 900003;
        bucket.revision = api.sessionRevision();
        bucket.pendingPage = 1;
        QJsonArray items;
        for (const auto &song : songs)
            items.append(kind == SearchModel::Kind::Lyric
                             ? QJsonObject{{"song", song}, {"excerpt", "保留歌词命中"}}
                             : song.toObject());
        const QJsonObject payload{{"kind", kind == SearchModel::Kind::Song ? "songs" : "lyrics"},
                                  {"page", QJsonObject{{"items", items}, {"total", items.size()}}}};
        model.succeeded(900003, QJsonDocument(payload).toJson());
        model.watchMetadata();
    }
};
} // namespace EchoMusic
namespace
{
QJsonObject song(QString id = "1", QString hash = QString(32, 'a'))
{
    return {{"id", id},          {"hash", hash},
            {"title", "原歌曲"}, {"artists", QJsonArray{}},
            {"album_id", "0"},   {"album_title", ""},
            {"cover_url", ""},   {"duration_ms", 0},
            {"privilege", 40},   {"metadata", QJsonObject{{"opaque", 42}}}};
}
QJsonObject presentation(QString id = "1")
{
    return {{"id", id},
            {"title", "目录歌曲"},
            {"artists", QJsonArray{QJsonObject{{"id", "51"}, {"name", "真实歌手"}}}},
            {"album_id", "81"},
            {"album_title", "真实专辑"},
            {"cover_url", "https://example.test/cover.jpg"},
            {"duration_ms", 123000}};
}
QJsonObject tags(QString hash = QString(32, 'a'))
{
    return {{"hash", hash},
            {"title", "内嵌歌曲"},
            {"album_title", "内嵌专辑"},
            {"artists", QJsonArray{QJsonObject{{"id", "999"}, {"name", "真实歌手"}}}},
            {"cover_url", "file:///temporary.jpg"},
            {"lyric", "不得传播"},
            {"album_id", "999"}};
}
} // namespace
class UnifiedSongMetadataTest : public QObject
{
    Q_OBJECT
private slots:
    void coldArtistFanoutPersistsAllViewsWithinTheWindowBudget()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        service.enableSqlIndex(database);
        {
            SongCatalogModel model;
            model.attachMetadata(&service);
            model.enableSessionStorage();
            model.setMetadataActive(false);
            QJsonArray songs;
            for (int i = 1; i <= 1200; ++i) {
                auto value = presentation(QString::number(i));
                value.insert("cover_url", "");
                songs.append(value);
            }
            model.replaceSongs(songs);
            auto *index = SongMetadataServiceTestAccess::sqlIndex(service);
            QTRY_VERIFY(SongCatalogStorageTestAccess::idle(model) && database.pendingJobs() == 0 &&
                        !SongMetadataServiceTestAccess::sqlRegistrationPending(service) &&
                        index->pendingJobs() == 0);
            const auto avatar = QString("https://example.test/late-artist.jpg");
            auto learned = presentation("5000");
            learned.insert("cover_url", "");
            learned.insert("artists",
                           QJsonArray{QJsonObject{
                               {"id", "51"}, {"name", "真实歌手"}, {"avatar_url", avatar}}});
            service.complete(learned);
            SongMetadataServiceTestAccess::flush(service);
            int maximum = 0;
            QVERIFY(QTest::qWaitFor(
                [&] {
                    maximum = std::max(maximum, SongCatalogStorageTestAccess::residentViews(model));
                    return SongCatalogStorageTestAccess::idle(model) &&
                           database.pendingJobs() == 0 && index->pendingJobs() == 0 &&
                           SongMetadataServiceTestAccess::pendingKeys(service) == 0;
                },
                10000));
            QVERIFY(maximum <= 384);
            QTRY_COMPARE(model.data(model.index(1199), SongCatalogModel::CoverUrl).toString(),
                         avatar);
            QTRY_COMPARE(model.data(model.index(0), SongCatalogModel::CoverUrl).toString(), avatar);
            QVERIFY(SongCatalogStorageTestAccess::residentViews(model) <= 256);
        }
        service.shutdown();
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void cachedCatalogResumeKeepsSqlBindingsAndOnlyRequestsRemainingGaps()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        service.enableSqlIndex(database);
        {
            SongCatalogModel model;
            model.attachMetadata(&service);
            model.enableSessionStorage();
            model.setMetadataActive(false);
            auto missing = song();
            missing.insert("title", "");
            model.replaceSongs({missing, presentation("2")});
            auto *index = SongMetadataServiceTestAccess::sqlIndex(service);
            QTRY_VERIFY(model.storageReady() && database.pendingJobs() == 0 &&
                        !SongMetadataServiceTestAccess::sqlRegistrationPending(service) &&
                        index->pendingJobs() == 0);
            const auto owner = SongMetadataServiceTestAccess::sqlOwner(service, &model);
            QVERIFY(owner);
            SongMetadataServiceTestAccess::reserve(service, {"1"});
            model.setMetadataActive(true);
            QVERIFY(!SongMetadataServiceTestAccess::sqlRegistrationPending(service));
            QCOMPARE(index->pendingJobs(), 0);
            QCOMPARE(SongMetadataServiceTestAccess::sqlOwner(service, &model), owner);
            QCOMPARE(SongMetadataServiceTestAccess::waitingIds(service), QSet<QString>{"1"});
            model.setMetadataActive(false);
            QVERIFY(SongMetadataServiceTestAccess::waitingIds(service).isEmpty());
            // Hidden bindings still deliver facts and persist the latest view.
            SongMetadataServiceTestAccess::complete(service, {presentation()});
            QTRY_COMPARE(model.data(model.index(0), SongCatalogModel::Title).toString(),
                         QString("目录歌曲"));
            QTRY_VERIFY(database.pendingJobs() == 0 && index->pendingJobs() == 0);
            model.setMetadataActive(true);
            QVERIFY(!SongMetadataServiceTestAccess::sqlRegistrationPending(service));
            QCOMPARE(index->pendingJobs(), 0);
            QVERIFY(SongMetadataServiceTestAccess::waitingIds(service).isEmpty());
        }
        service.shutdown();
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void asynchronousCachedReplayDoesNotRepublishExpiredSourceFacts()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        auto source = song();
        source.insert("cover_url", "https://example.test/source.jpg");
        service.complete(source);
        SongMetadataServiceTestAccess::flush(service);
        SongMetadataServiceTestAccess::expire(service);
        QCOMPARE(SongMetadataServiceTestAccess::pendingKeys(service), 0);
        std::optional<QJsonObject> received;
        // The original observer's synchronous no-publish scope has ended.
        QTimer::singleShot(0, &service, [&] { received = service.replay(source); });
        QTRY_VERIFY(received.has_value());
        QCOMPARE(received->value("cover_url").toString(),
                 QString("https://example.test/source.jpg"));
        QCOMPARE(SongMetadataServiceTestAccess::pendingKeys(service), 0);
        // New source admission still publishes genuine facts normally.
        service.complete(source);
        QVERIFY(SongMetadataServiceTestAccess::pendingKeys(service) > 0);
    }
    void sqlRegistrationBacklogDoesNotRetainRichSourcePayloads()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.path());
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        service.enableSqlIndex(database);
        QObject owner;
        auto value = song();
        auto metadata = value.value("metadata").toObject();
        metadata["resources"] = QJsonArray{QJsonObject{{"hash", QString(32, 'b')}}};
        metadata["opaque"] = QString(1024 * 1024, 'x');
        value["artists"] = QJsonArray{QJsonObject{{"id", "50"}, {"name", "Artist"}}};
        metadata["artists"] = QJsonArray{QJsonObject{
            {"id", "50"}, {"name", "Artist"}, {"avatar_url", "https://example.test/avatar.jpg"}}};
        value["metadata"] = metadata;
        const auto input = SongMetadataService::observationInput(value);
        QCOMPARE(input.value("artists").toArray().first().toObject().value("avatar_url").toString(),
                 QString("https://example.test/avatar.jpg"));
        QCOMPARE(SongMetadataService::identityKeys(input),
                 SongMetadataService::identityKeys(value));
        QCOMPARE(SongMetadataService::needsCompletion(input),
                 SongMetadataService::needsCompletion(value));
        QList<SongMetadataService::Observation> observations;
        for (int i = 0; i < 300; ++i)
            observations.append({QString::number(i), value});
        service.watch(&owner, observations, [](const auto &) {}, false);
        QVERIFY(SongMetadataServiceTestAccess::pendingContainsOnlyObservationInputs(service));
        service.pause(&owner);
        service.resume(&owner, observations, [](const auto &) {});
        QVERIFY(SongMetadataServiceTestAccess::pendingContainsOnlyObservationInputs(service));
        service.unwatch(&owner);
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
    }
    void sqlIdentityIndexReleasesAcrossRepeatedOwnersAndKeepsFanout()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.filePath("session"));
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        service.enableSqlIndex(database);
        auto *index = SongMetadataServiceTestAccess::sqlIndex(service);
        auto statistics = [&] {
            if (!QTest::qWaitFor(
                    [&] { return !SongMetadataServiceTestAccess::sqlRegistrationPending(service); },
                    5000))
                return SongObservationIndex::Result{{}, {}, 0, 0, "registration timeout"};
            auto value = std::make_shared<std::optional<SongObservationIndex::Result>>();
            index->statistics([value](auto result) { *value = std::move(result); });
            if (!QTest::qWaitFor([&] { return value->has_value(); }, 5000))
                return SongObservationIndex::Result{{}, {}, 0, 0, "timeout"};
            return value->value_or(SongObservationIndex::Result{{}, {}, 0, 0, "timeout"});
        };
        for (int iteration = 0; iteration < 8; ++iteration) {
            QObject owner;
            QList<SongMetadataService::Observation> observations;
            for (int i = 1; i <= 3000; ++i)
                observations.append(
                    {QString::number(i),
                     QJsonObject{
                         {"id", QString::number(i)},
                         {"hash", QString("%1").arg(i, 32, 16, QChar('0'))},
                         {"artists", QJsonArray{QJsonObject{{"id", "99"}, {"name", "Artist"}}}}}});
            service.watch(&owner, observations, [](const auto &) {}, false);
            const auto populated = statistics();
            QVERIFY2(populated.error.isEmpty(), qPrintable(populated.error));
            QCOMPARE(populated.relations, qint64(9000));
            QCOMPARE(populated.identities, qint64(6001));
            QVERIFY(SongMetadataServiceTestAccess::noResidentRelations(service));
            service.unwatch(&owner);
            const auto retired = statistics();
            QCOMPARE(retired.relations, qint64(0));
            QCOMPARE(retired.identities, qint64(0));
        }
        // Replace an owner while its registration is still in flight. Old
        // chunks must neither revive rows nor notify the replacement binding.
        QObject replacement;
        const QJsonObject unchanged{{"id", "90001"}};
        QCOMPARE(service.complete(unchanged), unchanged);
        QList<SongMetadataService::Observation> interrupted;
        for (int i = 0; i < 600; ++i)
            interrupted.append(
                {QString::number(i), QJsonObject{{"id", QString::number(70000 + i)}}});
        int retiredCalls = 0;
        QStringList replacementRows;
        service.watch(&replacement, interrupted, [&](const auto &) { ++retiredCalls; }, false);
        service.watch(
            &replacement, {{"replacement", QJsonObject{{"id", "90001"}}}},
            [&](const auto &rows) { replacementRows = rows; }, false);
        SongMetadataServiceTestAccess::complete(
            service, {QJsonObject{{"id", "70000"}, {"title", "Retired"}}});
        QCOMPARE(statistics().relations, qint64(1));
        QCOMPARE(retiredCalls, 0);
        QVERIFY(replacementRows.isEmpty());
        SongMetadataServiceTestAccess::complete(
            service, {QJsonObject{{"id", "90001"}, {"title", "Current"}}});
        QTRY_COMPARE(replacementRows, QStringList{"replacement"});
        service.unwatch(&replacement);
        QCOMPARE(statistics().relations, qint64(0));
        QObject first, second;
        QStringList firstRows, secondRows;
        const QJsonObject song{{"id", "42"}};
        service.watch(
            &first, {{"first", song}}, [&](const auto &rows) { firstRows = rows; }, false);
        service.watch(
            &second, {{"second", song}}, [&](const auto &rows) { secondRows = rows; }, false);
        SongMetadataServiceTestAccess::complete(service,
                                                {QJsonObject{{"id", "42"}, {"title", "Updated"}}});
        QTRY_COMPARE(firstRows, QStringList{"first"});
        QTRY_COMPARE(secondRows, QStringList{"second"});
        service.unwatch(&second);
        secondRows.clear();
        firstRows.clear();
        SongMetadataServiceTestAccess::complete(service,
                                                {QJsonObject{{"id", "42"}, {"title", "Newer"}}});
        QTRY_COMPARE(firstRows, QStringList{"first"});
        QVERIFY(secondRows.isEmpty());
        // A public-facts/account reset must not lose a retained queue's binding.
        SongMetadataServiceTestAccess::reset(service);
        firstRows.clear();
        SongMetadataServiceTestAccess::complete(
            service, {QJsonObject{{"id", "42"}, {"title", "After reset"}}});
        QTRY_COMPARE(firstRows, QStringList{"first"});
        service.unwatch(&first);
        QCOMPARE(statistics().relations, qint64(0));
        service.shutdown();
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void sqlObserverUpdatesHiddenCatalogAndLateArtistIdentity()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.filePath("session"));
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        service.enableSqlIndex(database);
        SongCatalogModel model;
        model.setMetadataActive(false);
        model.attachMetadata(&service);
        model.replaceSongs({QJsonObject{{"id", "42"}, {"hash", QString(32, 'a')}}});
        QTRY_COMPARE(SongMetadataServiceTestAccess::observers(service), 1);
        SongMetadataServiceTestAccess::complete(
            service,
            {QJsonObject{{"id", "42"},
                         {"title", "Completed"},
                         {"artists", QJsonArray{QJsonObject{{"id", "77"}, {"name", "Singer"}}}}}});
        QTRY_COMPARE(model.item(0)["title"].toString(), QString("Completed"));
        // A later fact for another song of this artist must reach the original
        // row through the artist identity added by the first completion.
        SongMetadataServiceTestAccess::complete(
            service,
            {QJsonObject{{"id", "43"},
                         {"title", "Other"},
                         {"artists", QJsonArray{QJsonObject{
                                         {"id", "77"},
                                         {"name", "Singer"},
                                         {"avatar_url", "https://example.com/artist.png"}}}}}});
        QTRY_COMPARE(model.item(0)["coverUrl"].toString(),
                     QString("https://example.com/artist.png"));
        QVERIFY(SongMetadataServiceTestAccess::noResidentRelations(service));
        const auto ownerId = SongMetadataServiceTestAccess::sqlOwner(service, &model);
        for (int i = 0; i < 8; ++i) {
            model.setMetadataActive(true);
            model.setMetadataActive(false);
        }
        QTRY_VERIFY(!SongMetadataServiceTestAccess::sqlRegistrationPending(service));
        QCOMPARE(SongMetadataServiceTestAccess::sqlOwner(service, &model), ownerId);
        QCOMPARE(SongMetadataServiceTestAccess::jobs(service), 0);
        model.clear();
        QTRY_COMPARE(SongMetadataServiceTestAccess::observers(service), 0);
        service.shutdown();
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void sqlObserverReceivesEmbeddedCoverAliasAndRetiresOnDestruction()
    {
        QTemporaryDir directory;
        SessionDatabase database(directory.filePath("session"));
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        service.enableSqlIndex(database);
        SongCatalogModel model;
        model.setMetadataActive(false);
        model.attachMetadata(&service);
        const QJsonObject song{{"id", "cloud:unmatched"}, {"hash", QString(32, 'b')}};
        model.replaceSongs({song});
        QTRY_COMPARE(SongMetadataServiceTestAccess::observers(service), 1);
        service.enableCoverCache(directory.filePath("covers"));
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(Qt::red);
        const auto path = directory.filePath("cover.png");
        QVERIFY(image.save(path));
        service.publishEmbeddedCover(song, path);
        QTRY_VERIFY_WITH_TIMEOUT(!model.item(0)["coverUrl"].toString().isEmpty(), 10000);
        QVERIFY(SongMetadataServiceTestAccess::noResidentRelations(service));
        model.clear();
        service.shutdown();
        QSignalSpy closed(&database, &SessionDatabase::closed);
        database.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void compactIndicesKeepLargeFanoutAndRetireOwners()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        QObject first;
        auto second = std::make_unique<QObject>();
        QList<SongMetadataService::Observation> observations;
        QSet<QString> expected;
        for (int i = 0; i < 1876; ++i) {
            const auto row = QString::number(i);
            observations.append({row, song()});
            expected.insert(row);
        }
        QSet<QString> firstRows, secondRows;
        service.watch(
            &first, observations,
            [&](const QStringList &rows) { firstRows = QSet<QString>(rows.cbegin(), rows.cend()); },
            false);
        service.watch(
            second.get(), {{"second", song()}},
            [&](const QStringList &rows) {
                secondRows = QSet<QString>(rows.cbegin(), rows.cend());
            },
            false);
        QVERIFY(SongMetadataServiceTestAccess::singletonIndicesAreInline(service));
        SongMetadataServiceTestAccess::complete(service, {presentation()});
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(firstRows, expected);
        QCOMPARE(secondRows, QSet<QString>{"second"});
        service.unwatch(&first);
        QVERIFY(SongMetadataServiceTestAccess::singletonIndicesAreInline(service));
        second.reset();
        QCOMPARE(SongMetadataServiceTestAccess::observers(service), 0);
        QCOMPARE(SongMetadataServiceTestAccess::listeners(service), 0);
        SongMetadataServiceTestAccess::flush(service);
    }
    void catalogSearchLyricQueueAndLatePageShareOneResult()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        SongCatalogModel first, hidden;
        QAbstractItemModelTester tester(&first,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        first.attachMetadata(&service);
        hidden.attachMetadata(&service);
        first.setMetadataActive(false);
        hidden.setMetadataActive(false);
        first.replaceSongs({song()});
        hidden.replaceSongs({song()});
        first.toggleSelected("1");
        hidden.setQuery("真实歌手");
        QCOMPARE(hidden.count(), 0);
        PlaybackQueueModel queue(nullptr);
        queue.replace({PlaybackTrack::fromDomain(song())}, 0);
        SongMetadataServiceTestAccess::reserve(service, {"1"});
        queue.attachMetadata(&service);
        SearchModel search(api);
        search.attachMetadata(service, false);
        SearchModelTestAccess::accept(search, api, SearchModel::Kind::Song, {song()});
        SearchModelTestAccess::accept(search, api, SearchModel::Kind::Lyric, {song()});
        QCoreApplication::processEvents();
        QSignalSpy reset(&first, &QAbstractItemModel::modelReset);
        QSignalSpy queueReset(&queue, &QAbstractItemModel::modelReset);
        SongMetadataServiceTestAccess::complete(service, {presentation()});
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(first.trackProjection("1")->artist, QString("真实歌手"));
        QCOMPARE(hidden.count(), 1);
        QCOMPARE(queue.at(0)->album, QString("真实专辑"));
        QCOMPARE(search.data(search.index(0), SearchModel::ArtistText).toString(),
                 QString("真实歌手"));
        QCOMPARE(search.data(search.index(0), SearchModel::ExtraText).toString(),
                 QString("保留歌词命中"));
        search.setKind(SearchModel::Kind::Song);
        QCOMPARE(search.data(search.index(0), SearchModel::AlbumText).toString(),
                 QString("真实专辑"));
        QCOMPARE(first.selectedCount(), 1);
        QCOMPARE(first.trackProjection("1")->hash, QString(32, 'a'));
        QCOMPARE(first.trackProjection("1")->catalogPrivilege, quint32(40));
        QCOMPARE(first.trackProjection("1")->catalogMetadata["opaque"].toInt(), 42);
        QCOMPARE(queue.currentIndex(), 0);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(queueReset.size(), 0);
        SongCatalogModel late;
        late.attachMetadata(&service);
        late.setMetadataActive(false);
        late.replaceSongs({song()});
        QCOMPARE(late.trackProjection("1")->albumId, quint64(81));
        service.shutdown();
        api.shutdown();
    }
    void embeddedFactsUseExactAudioAndNeverInventNavigationOrRetainArtifacts()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        SongCatalogModel model;
        model.attachMetadata(&service);
        model.setMetadataActive(false);
        auto first = song("0");
        first["title"] = "***";
        model.replaceSongs({first, song("2", QString(32, 'b')), song("3")});
        QCoreApplication::processEvents();
        service.publishEmbedded(tags());
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(model.sourceItem(0)["title"].toString(), QString("内嵌歌曲"));
        QCOMPARE(model.sourceItem(1)["artistText"].toString(), QString());
        QCOMPARE(model.sourceItem(2)["artistText"].toString(), QString("真实歌手"));
        const auto track = model.trackProjection("0").value();
        QCOMPARE(track.artistLinks()[0].toMap()["id"].toString(), QString());
        QCOMPARE(track.albumId, quint64(0));
        QVERIFY(track.cover.isEmpty());
        QVERIFY(!track.catalogMetadata.contains("lyric"));
        // Same catalogue ID with another hash does not inherit file tags.
        auto different = song("3", QString(32, 'c'));
        QCOMPARE(service.complete(different)["artists"].toArray().size(), 0);
        // A later provider reference can upgrade a matching text-only singer.
        SongMetadataServiceTestAccess::complete(service, {presentation("3")});
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(model.trackProjection("3")->artistLinks()[0].toMap()["id"].toString(),
                 QString("51"));
        QCOMPARE(model.trackProjection("3")->album, QString("内嵌专辑"));
        service.shutdown();
        api.shutdown();
    }
    void consumerCancellationDestructionExpiryAndSessionResetAreBounded()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        SongMetadataServiceTestAccess::reserve(service, {"1"});
        auto a = std::make_unique<QObject>();
        QObject b;
        int calls = 0;
        const QList<SongMetadataService::Observation> observed{{"row", song()}};
        service.watch(a.get(), observed, [&](const QStringList &) { ++calls; });
        service.watch(&b, observed, [&](const QStringList &) { ++calls; });
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(SongMetadataServiceTestAccess::flights(service), 1);
        QCOMPARE(SongMetadataServiceTestAccess::jobs(service), 2);
        a.reset();
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(SongMetadataServiceTestAccess::flights(service), 1);
        QCOMPARE(SongMetadataServiceTestAccess::jobs(service), 1);
        service.watch(&b, observed, [&](const QStringList &) { ++calls; }, false);
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(SongMetadataServiceTestAccess::jobs(service), 0);
        QCOMPARE(SongMetadataServiceTestAccess::flights(service), 0);
        service.publishEmbedded(tags());
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(calls, 1); // hidden owner receives facts without HTTP.
        SongMetadataServiceTestAccess::expire(service);
        QVERIFY(service.complete(song())["artists"].toArray().isEmpty());
        service.publishEmbedded(tags(QString(32, 'd')));
        SongMetadataServiceTestAccess::complete(service, {presentation()});
        QVERIFY(api.setAccount(100, "new-offline-session"));
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(calls, 1); // pending old-session notifications were canceled.
        QVERIFY(service.complete(song())["artists"].toArray().isEmpty());
        service.unwatch(&b);
        QCOMPARE(SongMetadataServiceTestAccess::observers(service), 0);
        QCOMPARE(SongMetadataServiceTestAccess::listeners(service), 0);
        for (int i = 0; i < 2200; ++i)
            service.publishEmbedded(tags(QString::number(i)));
        QVERIFY(SongMetadataServiceTestAccess::embedded(service) <= 2000);
        service.shutdown();
        api.shutdown();
    }
    void playbackReaderBoundaryPublishesRawTagsToOtherLists()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        PlaybackService playback(api, settings);
        SongMetadataService service(api);
        playback.attachMetadata(service);
        SongCatalogModel list;
        list.attachMetadata(&service);
        list.setMetadataActive(false);
        auto missing = song("0");
        missing["title"] = "***";
        list.replaceSongs({missing});
        auto current = PlaybackTrack::fromDomain(missing);
        current.title = "已知目录标题";
        playback.queue()->replace({current}, 0);
        const auto sequence = PlaybackServiceTestAccess::embedded(playback, current);
        const QJsonObject reader{{"title", "文件实际标题"},
                                 {"artist", "文件歌手"},
                                 {"album", "文件专辑"},
                                 {"lyric", ""}};
        playback.nativeBridge().completed(900004, sequence, true, QJsonDocument(reader).toJson());
        QTRY_COMPARE(list.trackProjection("0")->title, QString("文件实际标题"));
        QCOMPARE(list.trackProjection("0")->artist, QString("文件歌手"));
        QCOMPARE(list.trackProjection("0")->albumId, quint64(0));
        QCOMPARE(list.trackProjection("0")->durationMs, quint64(180000));
        QCOMPARE(playback.currentTrack().title, QString("已知目录标题"));
        QCOMPARE(playback.queue()->at(0)->artist, QString("文件歌手"));
        service.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void negativeCacheAndRetiredResponseDoNotRestartOrCrossAccounts()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        QObject owner;
        int notifications = 0;
        SongMetadataServiceTestAccess::reserve(service, {"1"});
        service.watch(&owner, {{"one", song()}}, [&](const QStringList &) { ++notifications; });
        SongMetadataServiceTestAccess::deliver(service, 900002, {});
        auto named = song();
        named["artists"] = QJsonArray{QJsonObject{{"name", "已有歌手"}}};
        QCOMPARE(service.complete(named), named); // A negative cache entry is not a fact.
        const auto cached = service.request({"1"});
        QVERIFY(SongMetadataServiceTestAccess::waiting(service, cached).isEmpty());
        SongMetadataServiceTestAccess::expire(service);
        const auto expired = service.request({"1"});
        QCOMPARE(SongMetadataServiceTestAccess::waiting(service, expired), QSet<QString>{"1"});
        QVERIFY(api.setAccount(200, "next-offline-session"));
        SongMetadataServiceTestAccess::deliver(service, 900002, {presentation()});
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(notifications, 0);
        QVERIFY(service.complete(song())["cover_url"].toString().isEmpty());
        service.shutdown();
        api.shutdown();
    }
    void failedMetadataRetriesOnlyLiveConsumersOnceAfterCooldown()
    {
        QTcpServer transport;
        QVERIFY(transport.listen(QHostAddress::LocalHost));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(transport.serverPort()));
        SongMetadataService service(api);
        QSignalSpy completed(&service, &SongMetadataService::completed);
        SongMetadataServiceTestAccess::reserve(service, {"1"});
        const auto first = service.request({"1"});
        const auto canceled = service.request({"1"});
        SongMetadataServiceTestAccess::fail(service, 900002);
        service.cancel(canceled);
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(completed.size(), 0);
        QCOMPARE(SongMetadataServiceTestAccess::waiting(service, first), QSet<QString>{"1"});
        QVERIFY(SongMetadataServiceTestAccess::retryTimer(service));
        SongMetadataServiceTestAccess::retryNow(service);
        QTRY_COMPARE(SongMetadataServiceTestAccess::flights(service), 1);
        const auto retry = SongMetadataServiceTestAccess::firstFlight(service);
        QVERIFY(retry != 900002);
        SongMetadataServiceTestAccess::deliver(service, retry, {presentation()});
        api.cancel(retry);
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first()[0].toULongLong(), first);
        QCOMPARE(completed.first()[1].toJsonArray().size(), 1);
        QVERIFY(!SongMetadataServiceTestAccess::retryTimer(service));

        SongMetadataServiceTestAccess::reserve(service, {"2"});
        service.request({"2"});
        SongMetadataServiceTestAccess::fail(service, 900002);
        SongMetadataServiceTestAccess::endCooldown(service);
        SongMetadataServiceTestAccess::reserve(service, {"2"});
        SongMetadataServiceTestAccess::fail(service, 900002);
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(completed.size(), 2);
        QVERIFY(completed.last()[1].toJsonArray().isEmpty());
        QVERIFY(!SongMetadataServiceTestAccess::retryTimer(service));

        SongMetadataServiceTestAccess::endCooldown(service);
        SongMetadataServiceTestAccess::reserve(service, {"3"});
        service.request({"3"});
        SongMetadataServiceTestAccess::fail(service, 900002, true);
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(completed.size(), 3);
        QVERIFY(!SongMetadataServiceTestAccess::retryTimer(service));

        SongMetadataServiceTestAccess::endCooldown(service);
        SongMetadataServiceTestAccess::reserve(service, {"4"});
        const auto cancelBeforeRetry = service.request({"4"});
        SongMetadataServiceTestAccess::fail(service, 900002);
        service.cancel(cancelBeforeRetry);
        SongMetadataServiceTestAccess::drain(service);
        QVERIFY(!SongMetadataServiceTestAccess::retryTimer(service));
        QCOMPARE(completed.size(), 3);
        SongMetadataServiceTestAccess::endCooldown(service);
        SongMetadataServiceTestAccess::reserve(service, {"5"});
        service.request({"5"});
        SongMetadataServiceTestAccess::fail(service, 900002);
        SongMetadataServiceTestAccess::drain(service);
        QVERIFY(SongMetadataServiceTestAccess::retryTimer(service));
        QVERIFY(api.setAccount(900003, "next-lifecycle-session"));
        QVERIFY(!SongMetadataServiceTestAccess::retryTimer(service));
        QCOMPARE(SongMetadataServiceTestAccess::jobs(service), 0);
        SongMetadataServiceTestAccess::deliver(service, 900002, {presentation("5")});
        QCOMPARE(completed.size(), 3);
        service.shutdown();
        api.shutdown();
    }
    void metadataBatchesStayWithinObservedUpstreamLimit()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        QStringList ids;
        for (int i = 1; i <= 151; ++i)
            ids.append(QString::number(i));
        const auto ticket = service.request(ids);
        SongMetadataServiceTestAccess::drain(service);
        const auto sizes = SongMetadataServiceTestAccess::batchSizes(service);
        QCOMPARE(sizes.size(), 3);
        for (auto size : sizes)
            QCOMPARE(size, 50);
        service.cancel(ticket);
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(SongMetadataServiceTestAccess::flights(service), 0);
        service.shutdown();
        api.shutdown();
    }
    void localPathsAndProvisionalNamesKeepTheirIdentity()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        auto durationOnly = presentation();
        durationOnly["hash"] = QString(32, 'a');
        durationOnly["duration_ms"] = 0;
        QVERIFY(!SongMetadataService::needsCompletion(durationOnly));
        auto local = song("42", {});
        local["local_path"] = "/music/one.flac";
        local["title"] = "one";
        auto embedded = tags({});
        embedded["local_path"] = "/music/one.flac";
        service.publishEmbedded(embedded);
        const auto completed = service.complete(local);
        QCOMPARE(completed["title"].toString(), QString("内嵌歌曲"));
        QCOMPARE(completed["artists"].toArray()[0].toObject()["name"].toString(),
                 QString("真实歌手"));
        QCOMPARE(SongMetadataService::identityKeys(local), QStringList{"path:/music/one.flac"});
        auto other = local;
        other["local_path"] = "/music/two.flac";
        QVERIFY(service.complete(other)["artists"].toArray().isEmpty());
        auto provisional = song();
        provisional["metadata"] = QJsonObject{{"filename_artist_name", "文件歌手"},
                                              {"source_file_name", "文件歌手 - 文件歌名.flac"}};
        provisional["artists"] = QJsonArray{QJsonObject{{"name", "文件歌手"}}};
        auto patch = presentation();
        const auto online = SongMetadataService::completeSong(provisional, patch);
        QCOMPARE(online["title"].toString(), QString("目录歌曲"));
        QCOMPARE(online["metadata"].toObject()["source_file_name"],
                 provisional["metadata"].toObject()["source_file_name"]);
        auto retained = PlaybackTrack::fromDomain(local);
        const auto key = retained.key();
        retained.durationMs = 456000; // Runtime duration can precede catalogue duration.
        retained.catalogMetadata["catalog_duration_ms"] = QJsonValue::Null;
        retained.applyPresentation(completed);
        QCOMPARE(retained.durationMs, quint64(456000));
        QCOMPARE(retained.key(), key);
        QCOMPARE(retained.localPath, QString("/music/one.flac"));
        service.shutdown();
        api.shutdown();
    }
    void largeCatalogOnlyNotifiesMatchingRows()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        SongCatalogModel model;
        model.attachMetadata(&service);
        model.setMetadataActive(false);
        QJsonArray songs;
        for (int i = 1; i <= 20000; ++i)
            songs.append(song(QString::number(i), QString::number(i)));
        model.replaceSongs(songs);
        QCoreApplication::processEvents();
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QElapsedTimer timer;
        timer.start();
        SongMetadataServiceTestAccess::complete(service, {presentation("19999")});
        SongMetadataServiceTestAccess::flush(service);
        qInfo() << "20000-row shared update (us):" << timer.nsecsElapsed() / 1000;
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed[0][0].value<QModelIndex>().row(), 19998);
        QCOMPARE(reset.size(), 0);
        model.appendSongs({song("20001", "20001")});
        QCoreApplication::processEvents();
        SongMetadataServiceTestAccess::complete(service,
                                                {presentation("1"), presentation("20001")});
        SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(model.trackProjection("1")->artist, QString("真实歌手"));
        QCOMPARE(model.trackProjection("20001")->artist, QString("真实歌手"));
        QCOMPARE(changed.size(), 3);
        QCOMPARE(reset.size(), 0);
        model.clear();
        QCoreApplication::processEvents();
        QCOMPARE(SongMetadataServiceTestAccess::observers(service), 0);
        QCOMPARE(SongMetadataServiceTestAccess::listeners(service), 0);
        service.shutdown();
        api.shutdown();
    }
};
QTEST_MAIN(UnifiedSongMetadataTest)
#include "UnifiedSongMetadataTest.moc"
