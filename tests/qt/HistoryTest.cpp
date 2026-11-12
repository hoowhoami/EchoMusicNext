#include "HistoryService.h"
#include "NativeWindow.h"
#include "NetworkPreferences.h"
#include "PlaybackRepository.h"
#include "PlaybackService.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "RenderedInk.h"
#include <QJsonArray>
#include <QFile>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static PlaybackRepository *repository(PlaybackService &service)
    {
        return service.m_repository.get();
    }
    static quint64 select(PlaybackService &service, const PlaybackTrack &track)
    {
        service.finishHistory();
        service.invalidate();
        service.m_track = track;
        service.m_loaded = false;
        service.m_loading = true;
        service.m_playing = false;
        service.m_autoplay = true;
        return service.m_audioSequence;
    }
    static void event(PlaybackService &service, const QJsonObject &event)
    {
        service.consumeOutputEvent(event);
    }
    static void elapsed(PlaybackService &service, qint64 ms)
    {
        service.m_listenedMs = ms;
    }
};
class HistoryServiceTestAccess
{
public:
    static void enableMetadata(HistoryService &s, bool enabled = true)
    {
        s.m_network = enabled;
        if (enabled)
            s.completeSongs();
    }
    static quint64 metadataTicket(const HistoryService &s)
    {
        return s.m_metadataRequests.isEmpty() ? 0 : s.m_metadataRequests.cbegin().key();
    }
    static void metadata(HistoryService &s, quint64 ticket, const QJsonArray &values)
    {
        s.acceptMetadata(ticket, values);
    }
    static void accept(HistoryService &s, const QJsonArray &entries)
    {
        s.accept(entries);
    }
    static QJsonArray entries(const HistoryService &s)
    {
        return s.m_entries;
    }
};
class SongMetadataServiceTestAccess
{
public:
    static void complete(SongMetadataService &s, const QStringList &ids, const QJsonArray &values)
    {
        constexpr quint64 request = 990001;
        s.m_flights.insert(request, ids);
        s.accept(request, QJsonDocument(values).toJson());
    }
    static QSet<QString> waiting(const SongMetadataService &s, quint64 ticket)
    {
        return s.m_jobs.value(ticket).waiting;
    }
};
} // namespace EchoMusic
namespace
{
PlaybackTrack historyTrack(const QString &id, const QString &title, int duration = 180000)
{
    PlaybackTrack track;
    track.id = id;
    track.hash = QString("%1").arg(qHash(id), 32, 16, QChar('0'));
    track.title = title;
    track.artist = "周杰伦";
    track.album = "魔杰座";
    track.cover = "";
    track.durationMs = quint64(duration);
    track.catalogPrivilege = 10;
    track.catalogMetadata =
        QJsonObject{{"catalog_duration_ms", duration}, {"catalog_privilege", 10}};
    return track;
}
QQuickItem *historyVisual(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = historyVisual(child, name))
            return found;
    return nullptr;
}
int historyVisualCount(QQuickItem *item, const QString &name)
{
    int count = item->objectName() == name;
    for (auto *child : item->childItems())
        count += historyVisualCount(child, name);
    return count;
}
QObject *historyObject(QObject *object, const QString &name)
{
    if (object->objectName() == name)
        return object;
    for (auto *child : object->children())
        if (auto *found = historyObject(child, name))
            return found;
    return nullptr;
}
} // namespace
class HistoryTest : public QObject
{
    Q_OBJECT
private slots:
    void loadedOccurrenceImmediatelyPromotesHistoryWithoutDoubleCounting()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SongMetadataService metadata(api);
        RouteController router;
        HistoryService service(api, playback, metadata, router, false);
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        const auto first = historyTrack("1", "第一首");
        const auto second = historyTrack("2", "第二首");
        QSignalSpy writes(repository, &PlaybackRepository::historyChanged);
        QSignalSpy promotions(&service, &HistoryService::recordPromoted);
        QSignalSpy reads(&playback, &PlaybackService::historyLoaded);
        QSignalSpy failures(repository, &PlaybackRepository::failed);
        const auto loaded = [&](quint64 seq) {
            PlaybackServiceTestAccess::event(playback,
                                             {{"event", "file-loaded"}, {"seq", qint64(seq)}});
            PlaybackServiceTestAccess::event(playback, {{"event", "state-change"},
                                                        {"track_seq", qint64(seq)},
                                                        {"state", QJsonObject{{"playing", true}}}});
        };

        playback.requestHistory();
        QTRY_VERIFY(!reads.isEmpty());
        const auto firstSeq = PlaybackServiceTestAccess::select(playback, first);
        QCOMPARE(service.songs()->count(), 0); // Selection/restoration alone is not a play.
        PlaybackServiceTestAccess::event(playback,
                                         {{"event", "file-loaded"}, {"seq", qint64(firstSeq)}});
        QCOMPARE(writes.size(), 0); // Autoplay must also reach native playing confirmation.
        loaded(firstSeq);
        QTRY_COMPARE(service.songs()->count(), 1); // No refresh, stop or first time tick.
        QCOMPARE(service.songs()->item(0)["title"].toString(), first.title);
        QCOMPARE(
            HistoryServiceTestAccess::entries(service)[0].toObject()["listened_ms"].toInteger(), 0);

        loaded(firstSeq); // Duplicate load, pause/resume and same-track source adoption.
        PlaybackServiceTestAccess::event(
            playback, {{"event", "state-change"}, {"state", QJsonObject{{"paused", true}}}});
        PlaybackServiceTestAccess::event(
            playback, {{"event", "state-change"}, {"state", QJsonObject{{"playing", true}}}});
        PlaybackServiceTestAccess::event(playback, {{"event", "source-switched"}});
        PlaybackServiceTestAccess::elapsed(playback, 1250);
        const auto secondSeq = PlaybackServiceTestAccess::select(playback, second);
        loaded(firstSeq); // Retired load cannot record the newly selected song.
        QCOMPARE(writes.size(), 1);
        PlaybackServiceTestAccess::event(playback, {{"event", "state-change"},
                                                    {"track_seq", qint64(secondSeq)},
                                                    {"state", QJsonObject{{"playing", true}}}});
        QCOMPARE(writes.size(), 1); // Playing may arrive before file-loaded.
        loaded(secondSeq);
        QTRY_COMPARE(service.songs()->count(), 2);
        QCOMPARE(service.songs()->item(0)["title"].toString(), second.title);
        auto entries = HistoryServiceTestAccess::entries(service);
        QCOMPARE(entries[1].toObject()["listened_ms"].toInteger(), 1250);
        QCOMPARE(entries[1].toObject()["play_count"].toInteger(), 1);
        const auto firstPlayedAt = entries[1].toObject()["played_at"].toInteger();

        QTest::qWait(2);
        loaded(PlaybackServiceTestAccess::select(playback, first));
        QTRY_COMPARE(service.songs()->item(0)["title"].toString(), first.title);
        entries = HistoryServiceTestAccess::entries(service);
        QCOMPARE(entries[0].toObject()["play_count"].toInteger(), 2);
        QVERIFY(entries[0].toObject()["played_at"].toInteger() > firstPlayedAt);
        QCOMPARE(writes.size(), 3);
        QCOMPARE(promotions.size(), 2);
        // Replaying the existing top row still produces one visual insertion;
        // duplicate native events and listened-time settlement do not.
        loaded(PlaybackServiceTestAccess::select(playback, first));
        QTRY_COMPARE(
            HistoryServiceTestAccess::entries(service)[0].toObject()["play_count"].toInteger(), 3);
        QCOMPARE(promotions.size(), 3);

        // Deleting the current song must survive its eventual time settlement.
        service.remove(service.songs()->item(0)["resourceId"].toString());
        QTRY_COMPARE(service.songs()->count(), 1);
        PlaybackServiceTestAccess::elapsed(playback, 900);
        PlaybackServiceTestAccess::select(playback, first); // Not loaded: no new play.
        service.clear();
        QTRY_COMPARE(service.songs()->count(), 0);
        PlaybackServiceTestAccess::elapsed(playback, 500);
        QVERIFY(api.setAccount(200, "another-offline-fixture"));
        loaded(firstSeq); // Old account/load response is retired.
        QSignalSpy stored(repository, &PlaybackRepository::historyLoaded);
        constexpr quint64 readRevision = 990001;
        repository->loadHistory(100, readRevision);
        const auto ownRead = [](const QList<QVariant> &row) {
            return row[0].toULongLong() == 100 && row[1].toULongLong() == readRevision;
        };
        QTRY_VERIFY(std::any_of(stored.cbegin(), stored.cend(), ownRead));
        const auto found = std::find_if(stored.cbegin(), stored.cend(), ownRead);
        QVERIFY(qvariant_cast<QJsonArray>((*found)[2]).isEmpty());
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        QCOMPARE(service.songs()->count(), 0);
        QCOMPARE(failures.size(), 0);
        service.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
    void metadataCompletesPresentationAndStatsWithoutWritingHistory()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SongMetadataService metadata(api);
        RouteController router;
        HistoryService service(api, playback, metadata, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("history"));
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        auto original = historyTrack("1", "原歌曲");
        original.artist = "临时歌手";
        original.album.clear();
        original.catalogMetadata["filename_artist_name"] = "临时歌手";
        original.catalogMetadata["source_file_name"] = "原始.mp3";
        original.catalogMetadata["opaque"] = QJsonObject{{"fact", 42}};
        repository->record(100, original, 12345);
        repository->record(100, original, 23456);
        auto local = historyTrack("local:track", "本地歌曲");
        local.localPath = dir.filePath("local.mp3");
        repository->record(100, local, 1000);
        service.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 2, 5000);
        QTRY_COMPARE(service.stats()["totalPlays"].toInt(), 3);
        QVERIFY(!service.loading());
        const auto before = HistoryServiceTestAccess::entries(service);
        const auto statsBefore = service.stats();
        const auto trackBefore = service.songs()->track("1").value();
        service.songs()->toggleSelected("1");
        service.songs()->setQuery("正式歌手");
        QCOMPARE(service.songs()->count(), 0);
        HistoryServiceTestAccess::enableMetadata(service);
        const auto ticket = HistoryServiceTestAccess::metadataTicket(service);
        QVERIFY(ticket);
        // The local ID is filtered before any remote lookup is scheduled.
        QCOMPARE(SongMetadataServiceTestAccess::waiting(metadata, ticket), QSet<QString>{"1"});
        const QJsonArray facts{
            QJsonObject{{"id", "1"},
                        {"title", "正式歌曲"},
                        {"artists", QJsonArray{QJsonObject{{"id", 51}, {"name", "正式歌手"}}}},
                        {"album_id", "81"},
                        {"album_title", "正式专辑"},
                        {"cover_url", "https://example.test/1.jpg"},
                        {"hash", "NOT-A-SOURCE"},
                        {"duration_ms", 999000},
                        {"privilege", 0}}};
        SongMetadataServiceTestAccess::complete(metadata, {"1"}, facts);
        QTRY_COMPARE(service.songs()->count(), 1);
        QCOMPARE(HistoryServiceTestAccess::metadataTicket(service), quint64(0));
        QVERIFY(!service.loading());
        QCOMPARE(service.songs()->selectedCount(), 1);
        const auto completed = service.songs()->track("1").value();
        QCOMPARE(completed.title, QString("正式歌曲"));
        QCOMPARE(completed.album, QString("正式专辑"));
        QCOMPARE(completed.hash, trackBefore.hash);
        QCOMPARE(completed.durationMs, trackBefore.durationMs);
        QCOMPARE(completed.catalogPrivilege, trackBefore.catalogPrivilege);
        QCOMPARE(completed.catalogMetadata["history_key"],
                 trackBefore.catalogMetadata["history_key"]);
        QCOMPARE(completed.catalogMetadata["source_file_name"],
                 trackBefore.catalogMetadata["source_file_name"]);
        QCOMPARE(completed.catalogMetadata["opaque"], trackBefore.catalogMetadata["opaque"]);
        const auto after = HistoryServiceTestAccess::entries(service);
        QCOMPARE(after.size(), before.size());
        for (qsizetype i = 0; i < before.size(); ++i)
            for (const auto &key : {"key", "played_at", "play_count", "listened_ms"})
                QCOMPARE(after[i].toObject()[key], before[i].toObject()[key]);
        QCOMPARE(service.stats()["totalPlays"], statsBefore["totalPlays"]);
        QCOMPARE(service.stats()["statCards"], statsBefore["statCards"]);
        QCOMPARE(service.stats()["topSongs"].toList().first().toMap()["title"].toString(),
                 QString("正式歌曲"));
        QCOMPARE(service.stats()["topAlbum"].toMap()["name"].toString(), QString("正式专辑"));
        service.actions()->appendSong("1", false);
        QCOMPARE(playback.queue()->tracks().last().album, completed.album);
        QCOMPARE(playback.queue()->tracks().last().hash, original.hash);
        // A real SQL re-read proves completion did not record another play or
        // persist its presentation into the history row.
        HistoryServiceTestAccess::enableMetadata(service, false);
        QSignalSpy reads(&playback, &PlaybackService::historyLoaded);
        service.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!reads.isEmpty(), 5000);
        QCOMPARE(HistoryServiceTestAccess::entries(service), before);
        service.songs()->setQuery({});
        service.remove("1");
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 1, 5000);
        QCOMPARE(service.songs()->item(0)["resourceId"].toString(), QString("local:track"));
        service.shutdown();
        metadata.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void metadataRefreshRouteAccountAndDeletionScopes()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs);
        SongMetadataService metadata(api);
        RouteController router;
        HistoryService service(api, playback, metadata, router, true);
        service.registerRoutes(router);
        QVERIFY(router.registerPage({"other", "/main/other", QUrl("qrc:/not-instantiated.qml")}));
        QVERIFY(router.navigate("history"));
        const QJsonArray entries{QJsonObject{{"key", "1:history"},
                                             {"play_count", 2},
                                             {"played_at", qint64(1000)},
                                             {"listened_ms", 1234},
                                             {"track", historyTrack("1", "歌曲").toDomain()}}};
        const QJsonArray facts{QJsonObject{{"id", "1"},
                                           {"album_id", "8"},
                                           {"album_title", "新专辑"},
                                           {"cover_url", "https://example.test/1.jpg"}}};
        HistoryServiceTestAccess::accept(service, entries);
        const auto retired = HistoryServiceTestAccess::metadataTicket(service);
        QVERIFY(retired);
        service.refresh();
        HistoryServiceTestAccess::accept(service, entries);
        HistoryServiceTestAccess::metadata(service, retired, facts);
        QVERIFY(service.songs()->track("1")->cover.isEmpty());
        QVERIFY(router.navigate("other"));
        QCOMPARE(HistoryServiceTestAccess::metadataTicket(service), quint64(0));
        SongMetadataServiceTestAccess::complete(metadata, {"1"}, facts);
        QTRY_COMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        router.back();
        QTRY_COMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        HistoryServiceTestAccess::accept(service, entries);
        const auto removing = HistoryServiceTestAccess::metadataTicket(service);
        service.remove("1");
        HistoryServiceTestAccess::metadata(service, removing, facts);
        QCOMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        HistoryServiceTestAccess::accept(service, entries);
        const auto clearing = HistoryServiceTestAccess::metadataTicket(service);
        service.clear();
        HistoryServiceTestAccess::metadata(service, clearing, facts);
        QCOMPARE(service.songs()->track("1")->cover, QString("https://example.test/1.jpg"));
        HistoryServiceTestAccess::accept(service, entries);
        const auto oldSession = HistoryServiceTestAccess::metadataTicket(service);
        QVERIFY(api.setAccount(200, "other-offline-domain-fixture"));
        HistoryServiceTestAccess::metadata(service, oldSession, facts);
        QCOMPARE(service.songs()->count(), 0);
        QCOMPARE(HistoryServiceTestAccess::metadataTicket(service), quint64(0));
        service.shutdown();
        metadata.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void readsRemovesAndClearsAccountHistory()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "history-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        SongMetadataService songMetadata(api);
        HistoryService service(api, playback, songMetadata, router, false);
        service.registerRoutes(router);
        QVERIFY(router.available("history"));
        QSignalSpy notices(&service, &HistoryService::notice);
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        QVERIFY(repository);
        repository->record(100, historyTrack("song-1", "第一首"), 12000);
        QTest::qWait(10);
        repository->record(100, historyTrack("song-2", "第二首"), 5000);
        service.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 2, 5000);
        QTRY_VERIFY(!service.loading());
        const auto stats = service.stats();
        QCOMPARE(stats.value("totalSongs").toInt(), 2);
        QCOMPARE(stats.value("totalPlays").toLongLong(), 2);
        QCOMPARE(stats.value("statCards").toList().size(), 4);
        QCOMPARE(stats.value("calendarDays").toList().size(), 35);
        QCOMPARE(stats.value("hourlyFootprint").toList().size(), 24);
        QCOMPARE(stats.value("topArtists").toList().size(), 1);
        QCOMPARE(stats.value("topArtists").toList().first().toMap().value("name").toString(),
                 QString("周杰伦"));
        QCOMPARE(stats.value("topSongs").toList().size(), 2);
        QCOMPARE(stats.value("topAlbum").toMap().value("name").toString(), QString("魔杰座"));
        QCOMPARE(service.songs()->item(0)["title"].toString(), QString("第二首"));
        const auto item = service.songs()->item(1);
        QCOMPARE(item["title"].toString(), QString("第一首"));
        QCOMPARE(item["artistText"].toString(), QString("周杰伦"));
        QVERIFY(service.songs()
                    ->track(service.songs()->item(1)["resourceId"].toString())
                    ->catalogMetadata.contains("history_key"));
        QCOMPARE(item["durationText"].toString(), QString("3:00"));
        // Removing uses the view id's history key, not a provider identity.
        service.remove(service.songs()->item(1)["resourceId"].toString());
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 1, 5000);
        QTRY_COMPARE(notices.size(), 1);
        service.clear();
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 0, 5000);
        QCOMPARE(notices.size(), 2);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void batchRemovalUsesHistoryKeysAndKeepsOtherAccounts()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-history-fixture"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        SongMetadataService metadata(api);
        HistoryService service(api, playback, metadata, router, false);
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        QSignalSpy failures(&playback, &PlaybackService::historyFailed);
        QSignalSpy notices(&service, &HistoryService::notice);
        const auto first = historyTrack("1", "移除一");
        const auto second = historyTrack("2", "移除二");
        repository->record(100, first, 1000);
        repository->record(100, second, 2000);
        repository->record(100, historyTrack("3", "保留"), 3000);
        repository->record(200, first, 4000);
        repository->record(200, second, 5000);
        service.refresh();
        QTRY_COMPARE(service.songs()->totalCount(), 3);
        service.songs()->setQuery("保留");
        QCOMPARE(service.songs()->count(), 1);
        service.removeMany({});
        service.removeMany({"missing"});
        QCOMPARE(notices.size(), 0);
        // Catalog IDs are not SQLite history keys. Resolve selected source rows
        // even when a filter hides them, deduplicating before the single write.
        service.removeMany({"1", "2", "1", "missing"});
        QTRY_COMPARE(service.songs()->totalCount(), 1);
        QCOMPARE(service.songs()->item(0)["resourceId"].toString(), QString("3"));
        QCOMPARE(service.stats()["totalSongs"].toInt(), 1);
        QCOMPARE(service.stats()["totalPlays"].toInt(), 1);
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().first().toString().contains("2"));
        service.refresh();
        QTRY_VERIFY(!service.loading());
        QCOMPARE(service.songs()->totalCount(), 1);
        QVERIFY(api.setAccount(200, "other-offline-history-fixture"));
        service.songs()->setQuery("");
        service.refresh();
        QTRY_COMPARE(service.songs()->totalCount(), 2);
        QCOMPARE(service.stats()["totalPlays"].toInt(), 2);
        QCOMPARE(failures.size(), 0);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void statisticsRespectSnapshotSemanticsAndRenderBoundedContent()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        SongMetadataService metadata(api);
        HistoryService service(api, playback, metadata, router, false);
        const auto today = QDate::currentDate();
        const auto at = QDateTime(today, QTime(7, 0)).toMSecsSinceEpoch();
        const auto yesterday = QDateTime(today.addDays(-1), QTime(20, 0)).toMSecsSinceEpoch();
        QJsonArray entries;
        const auto append = [&](int id, int plays, qint64 time, qint64 listened) {
            entries.append(
                QJsonObject{{"key", QString::number(id)},
                            {"play_count", plays},
                            {"played_at", time},
                            {"listened_ms", listened},
                            {"track", historyTrack(QString::number(id), "歌曲").toDomain()}});
        };
        append(1, 10, at, 60000);
        append(2, 1, yesterday, 60000);
        append(3, 2, 0, -1000);
        HistoryServiceTestAccess::accept(service, entries);
        const auto stats = service.stats();
        QCOMPARE(stats["totalPlays"].toInt(), 13);
        QCOMPARE(stats["listenedMs"].toLongLong(), 120000); // Never duration * plays.
        QCOMPARE(stats["datedSongs"].toInt(), 2);           // Unknown dates are not 1970.
        QCOMPARE(stats["calendarDays"].toList().size(), 35);
        QCOMPARE(stats["hourlyFootprint"].toList().size(), 24);
        QCOMPARE(stats["hourlyFootprint"].toList()[7].toMap()["count"].toInt(), 1);
        QCOMPARE(stats["hourlyFootprint"].toList()[20].toMap()["count"].toInt(), 1);
        QCOMPARE(stats["topSongs"].toList().first().toMap()["id"].toString(), QString("1"));
        QCOMPARE(stats["replayPercent"].toInt(), 67);
        QCOMPARE(stats["statCards"].toList()[1].toMap()["value"].toString(), QString("2 分钟"));
        int dated = 0;
        for (const auto &bucket : stats["hourlyFootprint"].toList()) {
            dated += bucket.toMap()["count"].toInt();
        }
        QCOMPARE(dated, 2);
        QCOMPARE(stats["dateRangeLabel"].toString(),
                 today.addDays(-1).toString("yyyy.MM.dd") + " — " + today.toString("yyyy.MM.dd"));

        // Large snapshots keep six song tiles and at most twelve artist entries.
        entries = {};
        const QStringList titles{"海边的风", "Midnight Walk", "晴天之后",
                                 "慢慢喜欢", "Echoes",        "下一站"};
        const QStringList artists{"林间", "The Blue Hour", "山海", "白日梦", "River", "晚风",
                                  "青空", "北岸",          "远山", "星河",   "小岛",  "南风"};
        for (int i = 0; i < 1000; ++i) {
            auto track = historyTrack(QString::number(i + 1), titles[i % 6]);
            track.artist = artists[i % artists.size()];
            track.album = i % 2 ? "日常的回响" : "沿途的风景";
            QImage cover(48, 48, QImage::Format_RGB32);
            cover.fill(QColor::fromHsv(((i % 6) * 44 + 150) % 360, 95, 180));
            const auto path = dir.filePath(QString("cover-%1.png").arg(i % 6));
            QVERIFY(cover.save(path));
            track.cover = QUrl::fromLocalFile(path).toString();
            entries.append(QJsonObject{
                {"key", track.id},
                {"track", track.toDomain()},
                {"play_count", 1 + i % 8},
                {"listened_ms", 90000},
                {"played_at",
                 QDateTime(today.addDays(-i % 11), QTime(i % 24, 0)).toMSecsSinceEpoch()}});
        }
        HistoryServiceTestAccess::accept(service, entries);
        ThemeStore theme(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &items) {
            for (const auto &item : items)
                warnings.append(item.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.History
import EchoMusic.Theme
Window {
    id: window
    required property var stats
    visible: true
    width: 1040
    height: stage.height
    color: Theme.main
    Rectangle {
    id: stage
    objectName: "statsStage"
    color: Theme.main
    width: parent.width
    height: body.implicitHeight + 48
    HistoryStatsContent {
        id: body
        objectName: "statsBody"
        x: 24; y: 24
        width: parent.width - 48
        viewportWidth: window.width
        stats: window.stats
    }
    }
})",
                          QUrl("qrc:/statistics-render-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"stats", service.stats()}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(historyVisualCount(window->contentItem(), "historyTopSongRow"), 6);
        QCOMPARE(historyVisualCount(window->contentItem(), "historyTopArtistRow"), 12);
        auto *body = historyVisual(window->contentItem(), "statsBody");
        auto *bar = historyVisual(body, "historyDayBar");
        QVERIFY(body && bar);
        auto *animation = historyObject(bar, "historyBarMotion");
        QVERIFY(animation);
        const auto originalRatio = bar->property("ratio").toReal();
        bar->setProperty("ratio", 1.);
        QTest::qWait(300);
        bar->setProperty("ratio", .2);
        QTest::qWait(60);
        QVERIFY(bar->property("presentedRatio").toReal() > .2);
        QVERIFY(bar->property("presentedRatio").toReal() < 1.);
        QVERIFY(animation->property("running").toBool());
        auto *fill = historyVisual(bar, "historyBarFill");
        QVERIFY(fill && fill->height() > bar->height() * .2 && fill->height() < bar->height());
        window->hide();
        QTRY_COMPARE(bar->property("presentedRatio").toReal(), .2);
        QVERIFY(!animation->property("running").toBool());
        window->show();
        theme.setMotionEnabled(false);
        bar->setProperty("ratio", .7);
        QCOMPARE(bar->property("presentedRatio").toReal(), .7);
        bar->setProperty("ratio", originalRatio);
        theme.setMotionEnabled(true);
        QSignalSpy playRequested(body, SIGNAL(playRequested(QString)));
        auto *favoritePlay = historyVisual(body, "historyFavoritePlay");
        QVERIFY(favoritePlay && favoritePlay->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(favoritePlay, "clicked"));
        QCOMPARE(playRequested.size(), 1);
        QCOMPARE(playRequested.first().first().toString(),
                 service.stats()["topSongs"].toList().first().toMap()["id"].toString());
        const auto artifacts = qEnvironmentVariable("ECHO_HISTORY_ARTIFACTS");
        // An explicit local domain snapshot can render current artwork after
        // the deterministic regression above. It never opens a user database.
        const auto preview = qEnvironmentVariable("ECHO_HISTORY_RENDER_SNAPSHOT");
        if (!preview.isEmpty() && !artifacts.isEmpty()) {
            QFile input(preview);
            QVERIFY(input.open(QIODevice::ReadOnly));
            const auto document = QJsonDocument::fromJson(input.readAll());
            QVERIFY(document.isArray() && !document.array().isEmpty());
            HistoryServiceTestAccess::accept(service, document.array());
            window->setProperty("stats", service.stats());
        }
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto width : {1040, 430}) {
                window->resize(width, window->height());
                QTest::qWait(250);
                QTRY_VERIFY(body->height() > 500);
                auto *clock = historyVisual(body, "historyClockDial");
                auto *timePanel = historyVisual(body, "historyTimeCard");
                qInfo() << "clock-layout" << width << clock->x() << clock->parentItem()->width()
                        << timePanel->width()
                        << clock->mapToScene(QPointF(clock->width() / 2, 0)).x()
                        << timePanel->mapToScene(QPointF(timePanel->width() / 2, 0)).x();
                QTRY_COMPARE_WITH_TIMEOUT(body->property("pendingImages").toInt(), 0, 8000);
                if (!artifacts.isEmpty()) {
                    QVERIFY(QDir().mkpath(artifacts));
                    auto *stage = historyVisual(window->contentItem(), "statsStage");
                    auto capture = stage->grabToImage();
                    QVERIFY(capture);
                    QSignalSpy ready(capture.data(), &QQuickItemGrabResult::ready);
                    QTRY_VERIFY(!ready.isEmpty());
                    QVERIFY(capture->image().save(
                        artifacts + QString("/redesign-%1-%2.png")
                                        .arg(mode == ThemeStore::Light ? "light" : "dark")
                                        .arg(width)));
                }
            }
        }
        theme.setGlobalFontSizeScale(150);
        window->setWidth(850);
        QTRY_COMPARE(historyVisual(body, "historyStatGrid")->property("columns").toInt(), 2);
        QTRY_COMPARE(historyVisual(body, "historyActivityGrid")->property("columns").toInt(), 1);
        theme.setGlobalFontSizeScale(100);
        HistoryServiceTestAccess::accept(service, {});
        window->setProperty("stats", service.stats());
        QTRY_VERIFY(historyVisual(body, "historyStatsEmpty")->isVisible());
        QVERIFY(!historyVisual(body, "historyRankingGrid")->isVisible());
        body->setProperty("loading", true);
        QTRY_COMPARE(historyVisual(body, "historyStatsEmptyTitle")->property("text").toString(),
                     QString("正在整理听歌记录"));
        body->setProperty("loading", false);
        body->setProperty("errorMessage", "读取失败");
        auto *retry = historyVisual(body, "historyStatsRetry");
        QTRY_VERIFY(retry->isVisible());
        QSignalSpy retryRequested(body, SIGNAL(retryRequested()));
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QCOMPARE(retryRequested.size(), 1);
        QCOMPARE(service.stats()["listenedMs"].toLongLong(), 0);
        QVERIFY(service.stats()["dateRangeLabel"].toString().isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        service.shutdown();
        metadata.shutdown();
        router.shutdown();
        QSignalSpy drained(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 15000);
        api.shutdown();
    }
    void statisticsRollSummaryNumbersAndRetireHiddenPresentation()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &warning : warnings)
                errors.append(warning.toString());
        });
        QVariantMap stats{
            {"totalPlaysLabel", "1,597"},
            {"statCards",
             QVariantList{QVariantMap{{"value", "211"}, {"unit", "首"}, {"label", "听过的歌曲"}},
                          QVariantMap{{"value", "88 小时"}, {"unit", ""}, {"label", "已记录时长"}},
                          QVariantMap{{"value", "188"}, {"unit", "位"}, {"label", "常听歌手"}},
                          QVariantMap{{"value", "59"}, {"unit", "%"}, {"label", "复听歌曲占比"}}}}};
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.History
import EchoMusic.Theme
Window {
    id: window
    required property var stats
    visible: true
    width: 820
    height: 240
    color: Theme.main
    HistoryStatsOverview {
        objectName: "overview"
        x: 24
        y: 24
        width: parent.width - 48
        stats: window.stats
    }
})",
                          QUrl("qrc:/statistics-number-test.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties({{"stats", stats}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto *overview = historyVisual(window->contentItem(), "overview");
        auto *total = historyVisual(overview, "historyTotalPlays");
        QVERIFY(total);
        QList<QPointer<QQuickItem>> numbers{total};
        QList<QQuickItem *> pending{overview};
        while (!pending.isEmpty()) {
            auto *item = pending.takeFirst();
            pending.append(item->childItems());
            if (item->objectName() == "historyMetricValue")
                numbers.append(item);
        }
        QCOMPARE(numbers.size(), 5);
        for (auto number : numbers) {
            QTRY_VERIFY(historyObject(number, "digitRoll"));
            QCOMPARE(historyObject(number, "digitRoll")->property("duration").toInt(), 900);
        }
        auto *roll = historyObject(total, "digitRoll");
        QPointer<QObject> presentation = roll->property("target").value<QObject *>();
        QSet<int> intermediateDigits;
        const auto artifacts = qEnvironmentVariable("ECHO_HISTORY_ARTIFACTS");
        bool capturedFrame = false;
        QElapsedTimer clock;
        clock.start();
        while (roll->property("running").toBool() && clock.elapsed() < 2000) {
            auto *digit = historyVisual(total, "rollingDigit-3");
            QVERIFY(digit);
            const auto value = historyObject(digit, "rollingDigitValue")->property("text").toInt();
            if (value > 0 && value < 9) {
                intermediateDigits.insert(value);
                if (!artifacts.isEmpty() && !capturedFrame) {
                    QVERIFY(QDir().mkpath(artifacts));
                    QVERIFY(window->grabWindow().save(artifacts + "/statistics-number-midway.png"));
                    capturedFrame = true;
                }
            }
            QTest::qWait(16);
        }
        QVERIFY(intermediateDigits.size() >= 2);
        for (auto number : numbers)
            QTRY_VERIFY(!historyObject(number, "digitRoll")->property("running").toBool());
        QCOMPARE(total->property("text").toString(), QString("1,597 次播放"));
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/statistics-number-final.png"));

        auto cards = stats["statCards"].toList();
        const QStringList values{"212", "89 小时", "189", "60"};
        for (int i = 0; i < cards.size(); ++i) {
            auto card = cards[i].toMap();
            card["value"] = values[i];
            cards[i] = card;
        }
        stats["statCards"] = cards;
        stats["totalPlaysLabel"] = "1,608";
        root->setProperty("stats", stats);
        QCOMPARE(roll->property("target").value<QObject *>(), presentation.data());
        for (auto number : numbers) {
            QVERIFY(number); // A snapshot refresh must retain all four delegates.
            QTRY_VERIFY(historyObject(number, "digitRoll")->property("running").toBool());
        }
        QTest::qWait(100);
        auto *units = historyVisual(total, "rollingDigit-4");
        QVERIFY(units);
        QVERIFY(units->property("position").toReal() > 7);
        QVERIFY(units->property("position").toReal() < 8);
        overview->setProperty("resourceActive", false);
        for (auto number : numbers)
            QTRY_VERIFY(!historyObject(number, "digitRoll"));
        QTRY_VERIFY(presentation.isNull());
        overview->setProperty("resourceActive", true);
        for (auto number : numbers) {
            QTRY_VERIFY(historyObject(number, "digitRoll"));
            QVERIFY(!historyObject(number, "digitRoll")->property("running").toBool());
        }
        overview->setProperty("animate", false);
        for (auto number : numbers)
            QTRY_VERIFY(!historyObject(number, "digitRoll"));
        QCOMPARE(total->property("text").toString(), QString("1,608 次播放"));
        overview->setProperty("animate", true);
        theme.setMotionEnabled(false);
        for (auto number : numbers)
            QTRY_VERIFY(!historyObject(number, "digitRoll"));
        theme.setMotionEnabled(true);
        window->hide();
        for (auto number : numbers)
            QTRY_VERIFY(!historyObject(number, "digitRoll"));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void nativeHistoryPageAndClearDialog()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "history-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        SongMetadataService songMetadata(api);
        HistoryService service(api, playback, songMetadata, router, false);
        service.registerRoutes(router);
        auto *repository = PlaybackServiceTestAccess::repository(playback);
        repository->record(100, historyTrack("song-1", "历史歌曲一"), 12000);
        repository->record(100, historyTrack("song-2", "历史歌曲二"), 5000);
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &warning : warnings)
                errors.append(warning.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.History
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 property real sidebarWidth: 0
 visible: true; width: 1150; height: 800
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  width: parent.width / window.settings.uiScale
  height: parent.height / window.settings.uiScale
  scale: window.settings.uiScale
  transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; anchors.leftMargin: window.sidebarWidth; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/history-runtime.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(router.navigate("history"));
        QTRY_VERIFY(historyVisual(window->contentItem(), "historyPage"));
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 2, 5000);
        QTRY_VERIFY(historyVisual(window->contentItem(), "songDelegate"));
        auto *count = historyVisual(window->contentItem(), "historySongCount");
        QVERIFY(count);
        QVERIFY(count->property("text").toString().contains("2 首歌曲"));
        auto *pageItem = historyVisual(window->contentItem(), "historyPage");
        auto *detailHeader = historyVisual(pageItem, "songDetailHeader");
        QVERIFY(detailHeader);
        QCOMPARE(detailHeader->property("heading").toString(), QString("播放历史"));
        QTRY_VERIFY(historyObject(pageItem, "historyClearDialogOwner"));
        auto *clearOwner = historyObject(pageItem, "historyClearDialogOwner");
        QVERIFY(!clearOwner->property("item").value<QObject *>());
        auto *initialTab = historyVisual(window->contentItem(), "historyTabSongs");
        auto *initialIndicator = historyVisual(window->contentItem(), "detailTabIndicator");
        auto *tabRow = historyVisual(window->contentItem(), "historyTabToolbarRow");
        auto *divider = historyVisual(window->contentItem(), "historyTabDivider");
        QVERIFY(initialTab && initialIndicator && tabRow && divider);
        QTRY_VERIFY(initialIndicator->width() > 20);
        QTRY_COMPARE(initialIndicator->width(), initialTab->width());
        QTRY_COMPARE(tabRow->height(), 56.);
        QCOMPARE(divider->mapToScene(QPointF()).y(), tabRow->mapToScene(QPointF(0, 55)).y());
        const auto artifacts = qEnvironmentVariable("ECHO_HISTORY_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QTest::qWait(200);
            QVERIFY(window->grabWindow().save(artifacts + "/history-loaded.png"));
        }
        auto *batchOwner = historyObject(pageItem, "historyBatchDrawerOwner");
        QVERIFY(batchOwner);
        QVERIFY(!batchOwner->property("item").value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(detailHeader, "batchRequested"));
        QTRY_VERIFY(batchOwner->property("item").value<QObject *>());
        QPointer<QObject> batch = batchOwner->property("item").value<QObject *>();
        auto *remove = historyVisual(window->contentItem(), "batchRemove");
        QVERIFY(remove && remove->isVisible());
        QVERIFY(!remove->isEnabled());
        auto *selectAll = historyVisual(window->contentItem(), "batchSelectAll");
        QVERIFY(selectAll);
        QVERIFY(QMetaObject::invokeMethod(selectAll, "click"));
        // The native checkbox's toggled handler is the production selection command.
        QTRY_COMPARE(service.songs()->selectedCount(), 2);
        QTRY_VERIFY(remove->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
        QPointer<QObject> confirmation;
        QTRY_VERIFY((confirmation = historyObject(batch, "batchRemoveConfirm")));
        QCOMPARE(confirmation->property("heading").toString(), QString("从播放历史移除"));
        QVERIFY(confirmation->property("description").toString().contains("2 首"));
        if (!artifacts.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(artifacts + "/history-batch-remove.png"));
        }
        QVERIFY(QMetaObject::invokeMethod(confirmation, "close"));
        QTRY_VERIFY(confirmation.isNull());
        QCOMPARE(service.songs()->totalCount(), 2);
        QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
        QTRY_VERIFY((confirmation = historyObject(batch, "batchRemoveConfirm")));
        QQuickItem *removeConfirm = nullptr;
        QTRY_VERIFY(
            (removeConfirm = historyVisual(window->contentItem(), "batchRemoveConfirmAction")));
        QVERIFY(QMetaObject::invokeMethod(removeConfirm, "clicked"));
        QTRY_COMPARE(service.songs()->totalCount(), 0);
        QTRY_VERIFY(batch.isNull());
        QTRY_VERIFY(confirmation.isNull());
        QCOMPARE(service.stats()["totalSongs"].toInt(), 0);
        service.refresh();
        QTRY_VERIFY(!service.loading());
        QCOMPARE(service.songs()->totalCount(), 0);
        // Restore only disposable fixture history for the existing stats/clear checks.
        repository->record(100, historyTrack("song-1", "历史歌曲一"), 12000);
        repository->record(100, historyTrack("song-2", "历史歌曲二"), 5000);
        service.refresh();
        QTRY_COMPARE(service.songs()->totalCount(), 2);
        // Stats tab: bounded native panel, no placeholder charts.
        auto *statsTab = historyVisual(window->contentItem(), "historyTabStats");
        QVERIFY(statsTab);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            statsTab->mapToScene(QPointF(statsTab->width() / 2, statsTab->height() / 2)).toPoint());
        QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("stats"));
        QTRY_VERIFY(historyVisual(window->contentItem(), "historyStatsView"));
        auto *statsView = historyVisual(window->contentItem(), "historyStatsView");
        QVERIFY(statsView->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(statsView->property("opacity").toDouble(), 1., 2000);
        QTRY_COMPARE_WITH_TIMEOUT(historyVisualCount(statsView, "historyStatCard"), 4, 2000);
        QCOMPARE(historyVisualCount(statsView, "historyCalendarDay"), 35);
        QCOMPARE(historyVisualCount(statsView, "historyHourSpoke"), 24);
        QCOMPARE(historyVisualCount(statsView, "historyTopArtistRow"), 1);
        QCOMPARE(historyVisualCount(statsView, "historyTopSongRow"), 2);
        {
            QList<QQuickItem *> stack{statsView};
            QQuickItem *icon = nullptr;
            while (!stack.isEmpty() && !icon) {
                auto *item = stack.takeFirst();
                stack.append(item->childItems());
                if (item->objectName() == "iconImage" && item->isVisible())
                    icon = item;
            }
            QVERIFY(icon);
            QTRY_COMPARE(icon->property("status").toInt(), 1);
        }
        QVERIFY(!historyVisual(window->contentItem(), "historyClearAction")->isVisible());
        auto *indicator = historyVisual(window->contentItem(), "detailTabIndicator");
        QVERIFY(indicator);
        QTRY_COMPARE_WITH_TIMEOUT(indicator->property("x").toDouble(), statsTab->x(), 2000);
        QTRY_COMPARE(tabRow->height(), 56.);
        QCOMPARE(divider->mapToScene(QPointF()).y(), tabRow->mapToScene(QPointF(0, 55)).y());
        QVERIFY(indicator->mapToScene(QPointF(0, indicator->height())).y() <
                divider->mapToScene(QPointF()).y());
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto zoom : {1., 1.25}) {
                shell.resetZoom();
                if (zoom > 1.)
                    for (int step = 0; step < 5; ++step)
                        shell.zoomIn();
                // CSS media queries depend on the whole zoom-adjusted window.
                // A sidebar changes available panel space, never the breakpoint.
                auto *statGrid = historyVisual(statsView, "historyStatGrid");
                auto *activityGrid = historyVisual(statsView, "historyActivityGrid");
                auto *rankingGrid = historyVisual(statsView, "historyRankingGrid");
                auto *week = historyVisual(statsView, "historyWeekCard");
                auto *time = historyVisual(statsView, "historyTimeCard");
                auto *artists = historyVisual(statsView, "historyArtistCard");
                auto *topSongs = historyVisual(statsView, "historyTopSongsCard");
                auto *latest = historyVisual(pageItem, "historyLatestPlayed");
                auto *chart = historyVisual(statsView, "historyCalendar");
                auto *clock = historyVisual(statsView, "historyClockDial");
                auto *clockFrame = historyVisual(statsView, "historyClockFrame");
                QVERIFY(statGrid && activityGrid && rankingGrid && week && time && artists &&
                        topSongs && latest && chart && clock && clockFrame);
                for (const int viewportWidth : {639, 640, 641, 979, 980, 981, 1150}) {
                    window->resize(qRound(viewportWidth * zoom), 1000);
                    for (const int sidebarWidth : {0, 216}) {
                        window->setProperty("sidebarWidth", sidebarWidth);
                        const auto logicalWidth = window->width() / shell.uiScale();
                        const auto contentWidth = logicalWidth - sidebarWidth - 48;
                        QTRY_COMPARE(statsView->width(), contentWidth);
                        const int columns = contentWidth >= 640 ? 4 : contentWidth >= 300 ? 2 : 1;
                        QTRY_COMPARE(statGrid->property("columns").toInt(), columns);
                        QTRY_COMPARE(activityGrid->property("columns").toInt(),
                                     contentWidth < 700 ? 1 : 2);
                        QTRY_COMPARE(rankingGrid->property("columns").toInt(),
                                     contentWidth < 700 ? 1 : 2);
                        QTRY_COMPARE(latest->isVisible(), logicalWidth >= 640);
                        QTRY_COMPARE(chart->property("columns").toInt(), 7);
                        QTRY_VERIFY(qAbs(clockFrame->width() - time->width()) < .01);
                        QTRY_VERIFY2(
                            qAbs(clock->mapToScene(QPointF(clock->width() / 2, 0)).x() -
                                 time->mapToScene(QPointF(time->width() / 2, 0)).x()) *
                                    window->devicePixelRatio() <=
                                1.,
                            qPrintable(
                                QString("viewport=%1 sidebar=%2 zoom=%3 time=(%4,%5) frame=(%6,%7) "
                                        "dial=(%8,%9) parent=(%10,%11) centers=%12/%13")
                                    .arg(logicalWidth)
                                    .arg(sidebarWidth)
                                    .arg(zoom)
                                    .arg(time->x())
                                    .arg(time->width())
                                    .arg(clockFrame->x())
                                    .arg(clockFrame->width())
                                    .arg(clock->x())
                                    .arg(clock->width())
                                    .arg(clockFrame->parentItem()->x())
                                    .arg(clockFrame->parentItem()->width())
                                    .arg(clock->mapToScene(QPointF(clock->width() / 2, 0)).x())
                                    .arg(time->mapToScene(QPointF(time->width() / 2, 0)).x())));
                        if (contentWidth >= 700) {
                            QTRY_VERIFY(week->width() >= 280);
                            QTRY_VERIFY(time->width() >= 240);
                            QTRY_COMPARE(week->y(), time->y());
                            QTRY_COMPARE(week->height(), time->height());
                            QTRY_VERIFY(qAbs(week->width() + time->width() + 40 -
                                             activityGrid->width()) <= 1.);
                            QTRY_VERIFY(qAbs(artists->width() + topSongs->width() + 40 -
                                             rankingGrid->width()) <= 1.);
                            QTRY_VERIFY(qAbs(topSongs->x() - artists->width() - 40) <= 1.);
                            QTRY_COMPARE(artists->y(), topSongs->y());
                        } else {
                            QTRY_VERIFY(time->y() > week->y());
                            QTRY_VERIFY(topSongs->y() > artists->y());
                        }
                    }
                    if (!artifacts.isEmpty()) {
                        QTest::qWait(40);
                        QVERIFY(window->grabWindow().save(
                            artifacts + QString("/history-breakpoint-%1-%2-%3.png")
                                            .arg(int(mode))
                                            .arg(zoom)
                                            .arg(viewportWidth)));
                    }
                }
                window->setProperty("sidebarWidth", 0);
                window->resize(1150, 800);
                QTest::qWait(100);
                QCOMPARE(tabRow->height(), 56.);
                if (!artifacts.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/history-stats-%1-%2.png")
                                        .arg(mode == ThemeStore::Light ? "light" : "dark")
                                        .arg(zoom)));
                QVERIFY(QMetaObject::invokeMethod(statsView, "positionViewAtEnd"));
                QTRY_COMPARE(detailHeader->property("progress").toDouble(), 1.);
                auto *cover = historyVisual(detailHeader, "themedDetailCover");
                QVERIFY(cover);
                QTRY_COMPARE(cover->property("status").toInt(), 1);
                QTest::qWait(80);
                const auto image = window->grabWindow();
                const auto scene = cover->mapRectToScene(cover->boundingRect());
                const double ratio = double(image.width()) / window->width();
                const QRectF pixels(scene.topLeft() * ratio, scene.size() * ratio);
                const auto ink = Test::renderedInkBounds(
                    image,
                    pixels
                        .adjusted(pixels.width() * .2, pixels.height() * .2, -pixels.width() * .2,
                                  -pixels.height() * .2)
                        .toAlignedRect(),
                    Qt::white);
                QVERIFY(ink);
                if (!artifacts.isEmpty())
                    QVERIFY(image.save(artifacts +
                                       QString("/history-sticky-%1-%2.png")
                                           .arg(mode == ThemeStore::Light ? "light" : "dark")
                                           .arg(zoom)));
                const double glyphFraction = ink->width() / pixels.width();
                QVERIFY2(glyphFraction > .30 && glyphFraction < .40,
                         qPrintable(QString("collapsed clock fraction %1").arg(glyphFraction)));
                QCOMPARE(detailHeader->property("heading").toString(), QString("播放历史"));
                statsView->setProperty("contentY", -statsView->property("topMargin").toReal());
                QTRY_COMPARE(detailHeader->property("progress").toDouble(), 1.);
            }
        }
        theme.setMode(ThemeStore::Light);
        shell.resetZoom();
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-stats.png"));
        window->resize(1778, 1000);
        QTest::qWait(250);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-stats-wide.png"));
        window->resize(1150, 800);
        QTest::qWait(150);
        QMetaObject::invokeMethod(statsView, "positionViewAtEnd");
        QTest::qWait(200);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-stats-bottom.png"));
        statsView->setProperty("contentY", -statsView->property("topMargin").toReal());
        // Reduced motion and hidden windows show content immediately, never at
        // a stuck transparent frame.
        window->hide();
        router.replaceQuery({{"tab", QString("songs")}});
        QTRY_VERIFY(historyVisual(window->contentItem(), "discoverySongs")->isVisible());
        auto *songsView = historyVisual(window->contentItem(), "catalogSongList");
        QVERIFY(songsView);
        QTRY_COMPARE_WITH_TIMEOUT(songsView->property("opacity").toDouble(), 1., 2000);
        window->show();
        theme.setMotionEnabled(false);
        router.replaceQuery({{"tab", QString("stats")}});
        QTRY_COMPARE_WITH_TIMEOUT(statsView->property("opacity").toDouble(), 1., 2000);
        QTRY_COMPARE_WITH_TIMEOUT(indicator->property("x").toDouble(), statsTab->x(), 2000);
        theme.setMotionEnabled(true);
        router.replaceQuery({{"tab", QString("songs")}});
        QTRY_COMPARE_WITH_TIMEOUT(songsView->property("opacity").toDouble(), 1., 2000);
        QTRY_COMPARE_WITH_TIMEOUT(detailHeader->property("progress").toReal(), 0., 2000);
        auto *songsTab = historyVisual(window->contentItem(), "historyTabSongs");
        QVERIFY(songsTab);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            songsTab->mapToScene(QPointF(songsTab->width() / 2, songsTab->height() / 2)).toPoint());
        QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("songs"));
        auto *songBody = historyVisual(window->contentItem(), "catalogSongList");
        QVERIFY(songBody);
        auto *viewport = songBody->property("pageContainer").value<QObject *>();
        QVERIFY(viewport);
        for (int count : {0, 3, 80}) {
            QJsonArray rows;
            for (int i = 1; i <= count; ++i)
                rows.append(QJsonObject{{"id", QString::number(i)},
                                        {"title", "History " + QString::number(i)}});
            service.songs()->replaceSongs(rows);
            for (int height : {500, 800}) {
                window->resize(1150, height);
                QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 0.)));
                for (int cycle = 0; cycle < 3; ++cycle) {
                    router.replaceQuery({{"tab", "stats"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 0., 1000);
                    router.replaceQuery({{"tab", "songs"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 0., 1000);
                }
                if (count == 80) {
                    QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 60.)));
                    router.replaceQuery({{"tab", "stats"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 60., 1000);
                    router.replaceQuery({{"tab", "songs"}});
                    QTest::qWait(40);
                    QTRY_COMPARE_WITH_TIMEOUT(viewport->property("scrollTop").toReal(), 60., 1000);
                }
            }
        }
        QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 0.)));
        window->resize(1150, 800);
        auto *clearAction = historyVisual(window->contentItem(), "historyClearAction");
        QVERIFY(clearAction);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            clearAction->mapToScene(QPointF(clearAction->width() / 2, clearAction->height() / 2))
                .toPoint());
        QTRY_VERIFY(clearOwner->property("item").value<QObject *>());
        auto *confirm =
            historyObject(clearOwner->property("item").value<QObject *>(), "historyClearConfirm");
        QVERIFY(confirm);
        QMetaObject::invokeMethod(confirm, "clicked");
        QTRY_COMPARE_WITH_TIMEOUT(service.songs()->count(), 0, 5000);
        QTRY_VERIFY(!clearOwner->property("item").value<QObject *>());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/history-empty.png"));
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN_WRAPPER(HistoryTest, QTEST_MAIN_SETUP())
#include "HistoryTest.moc"
