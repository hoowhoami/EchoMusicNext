#include "SongMetadataService.h"
#include "SongCatalogModel.h"
#include "PlaybackQueueModel.h"
#include "PlaybackService.h"
#include <QSettings>
#include <QTemporaryDir>
#include "SearchModel.h"
#include <QAbstractItemModelTester>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
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
    static void expire(SongMetadataService &service)
    {
        for (const auto &key : service.m_cache.keys())
            service.m_cache.object(key)->expires = -1;
        for (const auto &key : service.m_embedded.keys())
            service.m_embedded.object(key)->expires = -1;
        for (const auto &key : service.m_coverFacts.keys())
            service.m_coverFacts.object(key)->expires = -1;
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
        QCOMPARE(first.track("1")->artist, QString("真实歌手"));
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
        QCOMPARE(first.track("1")->hash, QString(32, 'a'));
        QCOMPARE(first.track("1")->catalogPrivilege, quint32(40));
        QCOMPARE(first.track("1")->catalogMetadata["opaque"].toInt(), 42);
        QCOMPARE(queue.currentIndex(), 0);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(queueReset.size(), 0);
        SongCatalogModel late;
        late.attachMetadata(&service);
        late.setMetadataActive(false);
        late.replaceSongs({song()});
        QCOMPARE(late.track("1")->albumId, quint64(81));
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
        const auto track = model.track("0").value();
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
        QCOMPARE(model.track("3")->artistLinks()[0].toMap()["id"].toString(), QString("51"));
        QCOMPARE(model.track("3")->album, QString("内嵌专辑"));
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
        QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
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
        QTRY_COMPARE(list.track("0")->title, QString("文件实际标题"));
        QCOMPARE(list.track("0")->artist, QString("文件歌手"));
        QCOMPARE(list.track("0")->albumId, quint64(0));
        QCOMPARE(list.track("0")->durationMs, quint64(180000));
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
        QCOMPARE(model.track("1")->artist, QString("真实歌手"));
        QCOMPARE(model.track("20001")->artist, QString("真实歌手"));
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
