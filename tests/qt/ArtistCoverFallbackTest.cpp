#include "ArtistCoverPresentation.h"
#include "EmbeddedCoverPresentation.h"
#include "SongMetadataService.h"
#include "SongCatalogModel.h"
#include "PlaybackQueueModel.h"
#include "PlaybackService.h"
#include "SettingsKeys.h"
#include "SearchModel.h"
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void preset(PlaybackService &player, const PlaybackTrack &track)
    {
        player.m_track = track;
    }
    static void sourceCover(PlaybackService &player, const QString &cover)
    {
        auto track = player.m_track;
        track.cover = cover;
        emit player.m_source.privilegesReady(track, {});
    }
};
class SongMetadataServiceTestAccess
{
public:
    static void online(SongMetadataService &s, const QJsonArray &values)
    {
        QStringList ids;
        for (const auto &v : values)
            ids.append(v.toObject()["id"].toString());
        s.m_flights.insert(999812, ids);
        s.accept(999812, QJsonDocument(values).toJson());
    }
    static void flush(SongMetadataService &s)
    {
        s.notifyObservers();
    }
    static void drain(SongMetadataService &s)
    {
        s.drain();
    }
    static int flights(const SongMetadataService &s)
    {
        return s.m_flights.size();
    }
    static void empty(SongMetadataService &s)
    {
        const auto requests = s.m_flights.keys();
        for (auto id : requests)
            s.accept(id, "[]");
        s.drain();
    }
    static void expire(SongMetadataService &s)
    {
        for (const auto &key : s.m_coverFacts.keys())
            s.m_coverFacts.object(key)->expires = -1;
    }
    static void reset(SongMetadataService &s)
    {
        s.clear();
    }
    static int avatars(const SongMetadataService &s)
    {
        return s.m_coverFacts.size();
    }
    static int avatarCost(const SongMetadataService &s)
    {
        return s.m_coverFacts.totalCost();
    }
    static int listeners(const SongMetadataService &s)
    {
        return s.m_listeners.size();
    }
    static int pending(const SongMetadataService &s)
    {
        return s.m_pendingKeys.size();
    }
};
class SearchModelTestAccess
{
public:
    static QString cover(const SearchModel &model, SearchModel::Kind kind)
    {
        return model.m_buckets[size_t(kind)].rows[0].cover;
    }
    static void seed(SearchModel &model, ApiClient &api, SearchModel::Kind kind,
                     const QJsonObject &song)
    {
        model.m_kind = kind;
        auto &bucket = model.active();
        bucket.request = 900003;
        bucket.revision = api.sessionRevision();
        bucket.pendingPage = 1;
        const auto item = kind == SearchModel::Kind::Lyric
                              ? QJsonObject{{"song", song}, {"excerpt", "Keep lyric hit"}}
                              : song;
        model.succeeded(
            900003,
            QJsonDocument(
                QJsonObject{{"kind", kind == SearchModel::Kind::Song ? "songs" : "lyrics"},
                            {"page", QJsonObject{{"items", QJsonArray{item}}, {"total", 1}}}})
                .toJson());
        model.watchMetadata();
    }
};
} // namespace EchoMusic
namespace
{
const QString portrait = "https://example.test/artist.jpg";
QJsonObject artist(QString id = "42", QString avatar = {})
{
    return {{"id", id}, {"name", "Singer"}, {"avatar_url", avatar}};
}
QJsonObject song(QString id = "1", QString artistId = "42", QString avatar = {})
{
    const auto hash =
        QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Md5).toHex());
    return {{"id", id},         {"hash", hash},
            {"title", "Song"},  {"artists", QJsonArray{artist(artistId, avatar)}},
            {"album_id", "10"}, {"album_title", "Album"},
            {"cover_url", ""},  {"privilege", 40}};
}
void attach(SongCatalogModel &model, SongMetadataService &service, const QJsonObject &row)
{
    model.attachMetadata(&service);
    model.setMetadataActive(false);
    model.replaceSongs({row});
}
QString cover(const SongCatalogModel &model, const QString &id)
{
    const auto track = model.track(id);
    return track ? track->cover : QString();
}
} // namespace
class ArtistCoverFallbackTest : public QObject
{
    Q_OBJECT
private slots:
    void priorityAndArtistMatchingKeepProviderIdentity()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        auto row = song("0", "0", portrait);
        const auto fallback = service.complete(row);
        auto sameAudio = song("0", "0");
        QCOMPARE(service.complete(sameAudio)["cover_url"], QJsonValue(portrait));
        sameAudio["hash"] = "different-audio";
        QVERIFY(service.complete(sameAudio)["cover_url"].toString().isEmpty());
        auto genuine = row;
        genuine["cover_url"] = "https://example.test/song.jpg";
        QCOMPARE(SongMetadataService::completeSong(genuine, fallback)["cover_url"],
                 genuine["cover_url"]);
        auto embedded = row;
        embedded["cover_url"] = "file:///cache/image.png";
        embedded["metadata"] = QJsonObject{{"embedded_cover_url", embedded["cover_url"]}};
        QCOMPARE(SongMetadataService::completeSong(embedded, fallback)["cover_url"],
                 embedded["cover_url"]);
        auto conflicting = song("2", "72");
        conflicting["metadata"] = QJsonObject{{"artists", QJsonArray{artist("73", portrait)}}};
        QVERIFY(service.complete(conflicting)["cover_url"].toString().isEmpty());
        auto unnamed = song("71", "71", portrait);
        auto unnamedArtist = unnamed["artists"].toArray()[0].toObject();
        unnamedArtist["name"] = "";
        unnamed["artists"] = QJsonArray{unnamedArtist};
        QCOMPARE(service.complete(unnamed)["cover_url"].toString(), portrait);
        QVERIFY(artistAvatar(artist("42", "file:///arbitrary.png")).isEmpty());
        QVERIFY(artistAvatar(artist("42", "javascript:alert(1)")).isEmpty());
        service.shutdown();
        api.shutdown();
    }
    void sourceAvatarsShareByIdAndRefreshPreviouslyUnknownArtists()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        SongCatalogModel source, target, wrong, noId, learned, official;
        auto row = song();
        row["metadata"] = QJsonObject{{"artists", QJsonArray{artist("42", portrait)}}};
        attach(source, service, row);
        attach(target, service, song("2"));
        attach(wrong, service, song("3", "99"));
        attach(noId, service, song("4", "0"));
        auto unknown = song("5");
        unknown["artists"] = QJsonArray{};
        attach(learned, service, unknown);
        auto withCover = song("6");
        withCover["cover_url"] = "https://example.test/album.jpg";
        attach(official, service, withCover);
        QTRY_COMPARE(cover(source, "1"), portrait);
        QTRY_COMPARE(cover(target, "2"), portrait);
        QVERIFY(cover(wrong, "3").isEmpty());
        QVERIFY(cover(noId, "4").isEmpty());
        QCOMPARE(cover(official, "6"), withCover["cover_url"].toString());
        auto plugin = song("plugin:1", "42", "https://example.test/plugin.jpg");
        plugin["metadata"] = QJsonObject{{"plugin_source", "test.private"}};
        QCOMPARE(service.complete(plugin)["cover_url"].toString(),
                 QString("https://example.test/plugin.jpg"));
        QCOMPARE(service.complete(song("8"))["cover_url"].toString(), portrait);
        SongMetadataServiceTestAccess::online(
            service, {QJsonObject{{"id", "5"}, {"artists", QJsonArray{artist()}}}});
        QTRY_COMPARE(cover(learned, "5"), portrait);
        const auto updated = QString("https://example.test/new-portrait.jpg");
        SongMetadataServiceTestAccess::online(
            service, {QJsonObject{{"id", "1"}, {"artists", QJsonArray{artist("42", updated)}}}});
        QTRY_COMPARE(cover(target, "2"), updated);
        QTRY_COMPARE(cover(learned, "5"), updated);
        QTRY_COMPARE(cover(source, "1"), updated);
        QCOMPARE(cover(official, "6"), withCover["cover_url"].toString());
        SongCatalogModel late;
        attach(late, service, song("7"));
        QCOMPARE(cover(late, "7"), updated);
        for (auto *model : {&source, &target, &wrong, &noId, &learned, &official, &late})
            model->clear();
        QCoreApplication::processEvents();
        QCOMPARE(SongMetadataServiceTestAccess::listeners(service), 0);
        service.shutdown();
        api.shutdown();
    }
    void embeddedThenAlbumReplaceFallbackAcrossCatalogSearchAndQueue()
    {
        QTemporaryDir dir;
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        SongMetadataService service(api);
        service.enableCoverCache(dir.filePath("cache"));
        const auto row = song("1", "42", portrait);
        SongCatalogModel first, hidden;
        attach(first, service, row);
        auto sameAudio = row;
        sameAudio["id"] = "2";
        attach(hidden, service, sameAudio);
        first.toggleSelected("1");
        PlaybackQueueModel queue(nullptr);
        queue.replace({PlaybackTrack::fromDomain(row)}, 0);
        QVERIFY(queue.at(0));
        queue.attachMetadata(&service);
        SearchModel search(api);
        search.attachMetadata(service, false);
        SearchModelTestAccess::seed(search, api, SearchModel::Kind::Song, row);
        SearchModelTestAccess::seed(search, api, SearchModel::Kind::Lyric, row);
        QTRY_COMPARE(cover(first, "1"), portrait);
        QTRY_COMPARE(queue.at(0)->cover, portrait);
        QVERIFY(SongMetadataService::needsCompletion(first.track("1")->toCatalog()));
        const auto path = dir.filePath("reader.png");
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        service.publishEmbeddedCover(row, path);
        QTRY_VERIFY(cover(first, "1") != portrait);
        const auto embedded = cover(first, "1");
        QVERIFY(QUrl(embedded).isLocalFile());
        QTRY_COMPARE(cover(hidden, "2"), embedded);
        QTRY_COMPARE(queue.at(0)->cover, embedded);
        QTRY_COMPARE(search.data(search.index(0), SearchModel::CoverUrl).toString(), embedded);
        QCOMPARE(SearchModelTestAccess::cover(search, SearchModel::Kind::Song), embedded);
        QVERIFY(!first.track("1")->catalogMetadata.contains("artist_cover_url"));
        QCOMPARE(first.selectedCount(), 1);
        QCOMPARE(first.track("1")->catalogPrivilege, quint32(40));
        const auto album = QString("https://example.test/album.jpg");
        queue.updateFacts("1", row["hash"].toString(), {}, {}, {}, album, 0);
        QCOMPARE(queue.at(0)->cover, album);
        QVERIFY(!queue.at(0)->catalogMetadata.contains("embedded_cover_url"));
        SongMetadataServiceTestAccess::online(service,
                                              {QJsonObject{{"id", "1"}, {"cover_url", album}}});
        QTRY_COMPARE(cover(first, "1"), album);
        QTRY_COMPARE(SearchModelTestAccess::cover(search, SearchModel::Kind::Song), album);
        QCOMPARE(SearchModelTestAccess::cover(search, SearchModel::Kind::Lyric), album);
        QVERIFY(QFile::remove(QUrl(embedded).toLocalFile()));
        QTRY_COMPARE(cover(hidden, "2"), portrait);
        QCOMPARE(cover(first, "1"), album);
        QCOMPARE(queue.at(0)->cover, album);
        QSignalSpy closed(&service, &SongMetadataService::closed);
        service.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void fallbackDoesNotBlockDeduplicatedOnlineRequestsAndNegativeCache()
    {
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        SongMetadataService service(api);
        const auto row = service.complete(song("100", "42", portrait));
        QVERIFY(hasArtistCover(row["metadata"].toObject(), portrait));
        QVERIFY(SongMetadataService::needsCompletion(row));
        QObject a, b, c;
        service.watch(&a, {{"a", row}}, [](const auto &) {});
        service.watch(&b, {{"b", row}}, [](const auto &) {});
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(SongMetadataServiceTestAccess::flights(service), 1);
        SongMetadataServiceTestAccess::empty(service);
        QCOMPARE(SongMetadataServiceTestAccess::flights(service), 0);
        service.watch(&c, {{"c", row}}, [](const auto &) {});
        SongMetadataServiceTestAccess::drain(service);
        QCOMPARE(SongMetadataServiceTestAccess::flights(service), 0);
        auto absent = song("101");
        QCOMPARE(service.complete(absent)["cover_url"].toString(), portrait);
        SongMetadataServiceTestAccess::expire(service);
        QVERIFY(service.complete(absent)["cover_url"].toString().isEmpty());
        for (int i = 1; i <= 2500; ++i)
            service.complete(song(QString::number(i), QString::number(i), portrait));
        QVERIFY(SongMetadataServiceTestAccess::avatars(service) <= 2000);
        QVERIFY(SongMetadataServiceTestAccess::avatarCost(service) <= 2'000'000);
        SongMetadataServiceTestAccess::reset(service);
        QCOMPARE(SongMetadataServiceTestAccess::avatars(service), 0);
        service.shutdown();
        QCOMPARE(SongMetadataServiceTestAccess::listeners(service), 0);
        api.shutdown();
    }
    void avatarUpdateOnlyChangesMatchingRowInLargeFilteredCatalog()
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
        model.setQuery("Song");
        model.toggleSort(2);
        QCoreApplication::processEvents();
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QElapsedTimer timer;
        timer.start();
        SongMetadataServiceTestAccess::online(service, {song("30000", "19999", portrait)});
        SongMetadataServiceTestAccess::flush(service);
        qInfo() << "20000-row artist-cover update (us):" << timer.nsecsElapsed() / 1000;
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed[0][0].value<QModelIndex>().row(), 19998);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(model.count(), 20000);
        QCOMPARE(cover(model, "19999"), portrait);
        service.shutdown();
        api.shutdown();
    }
    void sourceSongCoverBroadcastsAndOversizedCatalogSettles()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService service(api);
        SongCatalogModel first, second, anotherVersion;
        attach(first, service, song("1", "42", portrait));
        attach(second, service, song("1"));
        auto other = song("2");
        other["hash"] = song()["hash"];
        attach(anotherVersion, service, other);
        QTRY_COMPARE(cover(second, "1"), portrait);
        const auto album = QString("https://example.test/from-album-page.jpg");
        auto freshPage = song("1");
        freshPage["cover_url"] = album;
        QCOMPARE(service.complete(freshPage)["cover_url"].toString(), album);
        QTRY_COMPARE(cover(first, "1"), album);
        QTRY_COMPARE(cover(second, "1"), album);
        QCOMPARE(cover(anotherVersion, "2"), portrait);
        SongCatalogModel large;
        large.attachMetadata(&service);
        large.setMetadataActive(false);
        QJsonArray songs;
        for (int i = 10000; i < 12500; ++i) {
            auto row = song(QString::number(i), QString::number(i), portrait);
            row["cover_url"] = album;
            songs.append(row);
        }
        large.replaceSongs(songs);
        QCoreApplication::processEvents();
        QSignalSpy changed(&large, &QAbstractItemModel::dataChanged);
        for (int i = 0; i < 3; ++i)
            SongMetadataServiceTestAccess::flush(service);
        QCOMPARE(SongMetadataServiceTestAccess::pending(service), 0);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(large.count(), 2500);
        QVERIFY(SongMetadataServiceTestAccess::avatars(service) <= 2000);
        QVERIFY(SongMetadataServiceTestAccess::avatarCost(service) <= 2'000'000);
        service.shutdown();
        api.shutdown();
    }
    void playbackSourceCoverPublishesWithoutChangingTrackIdentity()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue(SettingsKeys::Playback::Volume, 0.0);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        SongMetadataService service(api);
        PlaybackService player(api, prefs);
        const auto row = song("1", "42", "http://127.0.0.1:1/artist.jpg");
        PlaybackServiceTestAccess::preset(player, PlaybackTrack::fromDomain(service.complete(row)));
        player.attachMetadata(service);
        SongCatalogModel list;
        attach(list, service, song());
        QTRY_COMPARE(cover(list, "1"),
                     row["artists"].toArray()[0].toObject()["avatar_url"].toString());
        const auto original = player.currentTrack();
        const auto source = QString("http://127.0.0.1:1/song.jpg");
        PlaybackServiceTestAccess::sourceCover(player, source);
        QTRY_COMPARE(cover(list, "1"), source);
        QCOMPARE(player.cover(), source);
        QCOMPARE(player.currentTrack().stableKey(), original.stableKey());
        QCOMPARE(player.currentTrack().catalogPrivilege, original.catalogPrivilege);
        QVERIFY(!player.currentTrack().catalogMetadata.contains("artist_cover_url"));
        service.shutdown();
        QSignalSpy closed(&player, &PlaybackService::closed);
        player.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(ArtistCoverFallbackTest)
#include "ArtistCoverFallbackTest.moc"
