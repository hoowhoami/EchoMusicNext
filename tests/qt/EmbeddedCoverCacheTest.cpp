#include "EmbeddedCoverCache.h"
#include "EmbeddedCoverPresentation.h"
#include "SongMetadataService.h"
#include "SongCatalogModel.h"
#include "PlaybackQueueModel.h"
#include "PlaybackService.h"
#include "PlaybackRepository_p.h"
#include "SettingsKeys.h"
#include "ThemeStore.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void present(PlaybackService &player, const PlaybackTrack &track)
    {
        player.m_track = track;
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
};
} // namespace EchoMusic
namespace
{
QString image(const QString &root, const QString &name, QRgb color)
{
    const auto path = QDir(root).filePath(name + ".png");
    QImage pixels(64, 64, QImage::Format_RGB32);
    pixels.fill(color);
    return pixels.save(path) ? path : QString();
}
QJsonObject song(QString id = "0", QString hash = QString(32, 'a'))
{
    return {{"id", id},        {"hash", hash},    {"title", "本地封面"}, {"artists", QJsonArray{}},
            {"album_id", "0"}, {"cover_url", ""}, {"privilege", 40}};
}
QString file(const QString &url)
{
    return QUrl(url).toLocalFile();
}
void close(EmbeddedCoverCache &cache)
{
    QSignalSpy closed(&cache, &EmbeddedCoverCache::closed);
    cache.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
}
} // namespace
class EmbeddedCoverCacheTest : public QObject
{
    Q_OBJECT
private slots:
    void diskPersistenceContentDedupAndScopeIsolation()
    {
        QTemporaryDir dir;
        const auto source = image(dir.path(), "reader", qRgb(40, 120, 220));
        const auto root = dir.filePath("cache");
        QString saved;
        {
            EmbeddedCoverCache cache(root, {});
            cache.setScope(1);
            cache.store({"hash:one"}, source);
            QTRY_VERIFY_WITH_TIMEOUT(!cache.lookup({"hash:one"}).isEmpty(), 5000);
            saved = cache.lookup({"hash:one"});
            QVERIFY(QFileInfo::exists(file(saved)));
            cache.store({"hash:two"}, source);
            QTRY_COMPARE(cache.lookup({"hash:two"}), saved);
            QCOMPARE(QDir(root).entryList({"*.png"}, QDir::Files).size(), 1);
            QVERIFY(QFile::remove(source)); // The reader's temporary owner is gone.
            QCOMPARE(QImage(file(saved)).size(), QSize(64, 64));
            close(cache);
        }
        {
            EmbeddedCoverCache restored(root, {});
            restored.setScope(2);
            QSignalSpy ready(&restored, &EmbeddedCoverCache::ready);
            QTRY_VERIFY(!ready.isEmpty());
            QVERIFY(restored.lookup({"hash:one"}).isEmpty());
            restored.setScope(1);
            QTRY_COMPARE(restored.lookup({"hash:one"}), saved);
            close(restored);
        }
    }
    void lruEntryAndByteBudgetsReclaimFiles()
    {
        QTemporaryDir dir;
        const auto first = image(dir.path(), "one", qRgb(255, 0, 0));
        const auto second = image(dir.path(), "two", qRgb(0, 255, 0));
        const auto third = image(dir.path(), "three", qRgb(0, 0, 255));
        EmbeddedCoverCache::Limits limits;
        limits.entries = 2;
        const auto root = dir.filePath("entries");
        EmbeddedCoverCache cache(root, limits);
        cache.store({"hash:one"}, first);
        QTRY_VERIFY(!cache.lookup({"hash:one"}).isEmpty());
        cache.store({"hash:two"}, second);
        QTRY_VERIFY(!cache.lookup({"hash:two"}).isEmpty());
        const auto oldSecond = cache.lookup({"hash:two"});
        QTest::qWait(5);
        cache.lookup({"hash:one"}); // One is now newer than two.
        cache.store({"hash:three"}, third);
        QTRY_VERIFY(!cache.lookup({"hash:three"}).isEmpty());
        QVERIFY(!cache.lookup({"hash:one"}).isEmpty());
        QVERIFY(cache.lookup({"hash:two"}).isEmpty());
        QVERIFY(!QFileInfo::exists(file(oldSecond)));
        QCOMPARE(QDir(root).entryList({"*.png"}, QDir::Files).size(), 2);
        close(cache);
        limits.entries = 512;
        limits.bytes = std::max(QFileInfo(first).size(), QFileInfo(second).size()) + 1;
        EmbeddedCoverCache bytes(dir.filePath("bytes"), limits);
        bytes.store({"hash:one"}, first);
        QTRY_VERIFY(!bytes.lookup({"hash:one"}).isEmpty());
        bytes.store({"hash:two"}, second);
        QTRY_VERIFY(!bytes.lookup({"hash:two"}).isEmpty());
        QVERIFY(bytes.lookup({"hash:one"}).isEmpty());
        qint64 total = 0;
        for (const auto &f : QDir(dir.filePath("bytes")).entryInfoList({"*.png"}, QDir::Files))
            total += f.size();
        QVERIFY(total <= limits.bytes);
        close(bytes);
    }
    void expiryExternalDeletionCorruptionAndInvalidInput()
    {
        QTemporaryDir dir;
        const auto source = image(dir.path(), "reader", qRgb(100, 30, 80));
        EmbeddedCoverCache::Limits limits;
        limits.ageMs = 500;
        EmbeddedCoverCache cache(dir.filePath("cache"), limits);
        cache.store({"hash:one"}, source);
        QTRY_VERIFY(!cache.lookup({"hash:one"}).isEmpty());
        const auto url = cache.lookup({"hash:one"});
        QTRY_VERIFY_WITH_TIMEOUT(cache.lookup({"hash:one"}).isEmpty(), 5000);
        QTRY_VERIFY(!QFileInfo::exists(file(url)));
        close(cache);
        EmbeddedCoverCache other(dir.filePath("other"), {});
        other.store({"hash:one"}, source);
        QTRY_VERIFY(!other.lookup({"hash:one"}).isEmpty());
        const auto previousUrl = other.lookup({"hash:one"});
        QVERIFY(QFile::remove(file(previousUrl)));
        QTRY_VERIFY_WITH_TIMEOUT(other.lookup({"hash:one"}).isEmpty(), 5000);
        other.store({"hash:one"}, source);
        QTRY_VERIFY(!other.lookup({"hash:one"}).isEmpty());
        QCOMPARE(file(other.lookup({"hash:one"})), file(previousUrl));
        QVERIFY(other.lookup({"hash:one"}) != previousUrl);
        other.store({"hash:two"}, source);
        QTRY_VERIFY(!other.lookup({"hash:two"}).isEmpty());
        QFile corrupt(file(other.lookup({"hash:two"})));
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("broken");
        corrupt.close();
        QTRY_VERIFY_WITH_TIMEOUT(other.lookup({"hash:two"}).isEmpty(), 5000);
        other.store({"hash:bad"}, corrupt.fileName());
        QFile invalid(dir.filePath("not-image"));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not a supported picture");
        invalid.close();
        QSignalSpy ready(&other, &EmbeddedCoverCache::ready);
        other.store({"hash:invalid"}, invalid.fileName());
        QTRY_VERIFY(!ready.isEmpty());
        QVERIFY(other.lookup({"hash:invalid"}).isEmpty());
        close(other);
    }
    void sharedListsReceiveAndRevokeCoverLeases()
    {
        QTemporaryDir dir;
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService metadata(api);
        metadata.enableCoverCache(dir.filePath("cache"));
        SongCatalogModel a, b;
        a.attachMetadata(&metadata);
        b.attachMetadata(&metadata);
        a.setMetadataActive(false);
        b.setMetadataActive(false);
        a.replaceSongs({song()});
        b.replaceSongs({song("2")});
        PlaybackQueueModel queue(nullptr);
        queue.replace({PlaybackTrack::fromDomain(song())}, 0);
        queue.attachMetadata(&metadata);
        a.toggleSelected("0");
        const auto source = image(dir.path(), "reader", qRgb(30, 80, 150));
        metadata.publishEmbeddedCover(song(), source);
        QTRY_VERIFY_WITH_TIMEOUT(!a.track("0")->cover.isEmpty(), 5000);
        const auto url = a.track("0")->cover;
        auto otherwiseComplete = a.track("0")->toCatalog();
        otherwiseComplete["album_id"] = "10";
        otherwiseComplete["album_title"] = "Album";
        otherwiseComplete["artists"] = QJsonArray{QJsonObject{{"id", "20"}, {"name", "Artist"}}};
        QVERIFY(SongMetadataService::needsCompletion(otherwiseComplete));
        otherwiseComplete["cover_url"] = "https://example.test/official.jpg";
        QVERIFY(!SongMetadataService::needsCompletion(otherwiseComplete));
        QTRY_COMPARE(b.track("2")->cover, url);
        QTRY_COMPARE(queue.at(0)->cover, url);
        QCOMPARE(a.selectedCount(), 1);
        QCOMPARE(a.track("0")->catalogPrivilege, quint32(40));
        QVERIFY(!a.track("0")->toDomain()["metadata"].toObject().contains("embedded_cover_url"));
        QCOMPARE(a.track("0")->toDomain()["cover"].toString(), QString());
        SongCatalogModel late;
        late.attachMetadata(&metadata);
        late.setMetadataActive(false);
        late.replaceSongs({song("3")});
        QCOMPARE(late.track("3")->cover, url);
        SongMetadataServiceTestAccess::online(
            metadata,
            {QJsonObject{{"id", "2"}, {"cover_url", "https://example.test/official.jpg"}}});
        QTRY_COMPARE(b.track("2")->cover, QString("https://example.test/official.jpg"));
        QVERIFY(QFile::remove(file(url)));
        QTRY_VERIFY_WITH_TIMEOUT(a.track("0")->cover.isEmpty(), 5000);
        QTRY_VERIFY(late.track("3")->cover.isEmpty());
        QTRY_VERIFY(queue.at(0)->cover.isEmpty());
        QCOMPARE(b.track("2")->cover, QString("https://example.test/official.jpg"));
        QSignalSpy closed(&metadata, &SongMetadataService::closed);
        metadata.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void scopeSwitchAndShutdownDiscardQueuedImports()
    {
        QTemporaryDir dir;
        const auto source = image(dir.path(), "reader", qRgb(80, 70, 60));
        EmbeddedCoverCache cache(dir.filePath("cache"), {});
        cache.setScope(1);
        cache.store({"hash:previous"}, source);
        cache.setScope(2);
        cache.store({"hash:current"}, source);
        QTRY_VERIFY(!cache.lookup({"hash:current"}).isEmpty());
        QVERIFY(cache.lookup({"hash:previous"}).isEmpty());
        cache.setScope(1);
        QSignalSpy ready(&cache, &EmbeddedCoverCache::ready);
        QTRY_VERIFY(!ready.isEmpty());
        QVERIFY(cache.lookup({"hash:current"}).isEmpty());
        // Queue multiple copies then close before the GUI can consume replies.
        for (int i = 0; i < 8; ++i)
            cache.store({"hash:pending" + QString::number(i)}, source);
        close(cache);
        QVERIFY(cache.lookup({"hash:pending0"}).isEmpty());
        QCoreApplication::processEvents();
        QCOMPARE(ready.size(), 1);
    }
    void coverUpdateDoesNotResortLargeFilteredCatalog()
    {
        QTemporaryDir dir;
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService metadata(api);
        metadata.enableCoverCache(dir.filePath("cache"));
        SongCatalogModel model;
        model.attachMetadata(&metadata);
        model.setMetadataActive(false);
        QJsonArray songs;
        for (int i = 1; i <= 20000; ++i)
            songs.append(song(QString::number(i), QString::number(i)));
        model.replaceSongs(songs);
        model.setQuery("本地");
        model.toggleSort(2);
        QCoreApplication::processEvents(); // Finish the initial identity subscription.
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QElapsedTimer timer;
        qint64 publicationUs = -1;
        connect(&model, &QAbstractItemModel::dataChanged, this,
                [&] { publicationUs = timer.nsecsElapsed() / 1000; });
        const auto picture = image(dir.path(), "reader", qRgb(50, 60, 70));
        timer.start();
        metadata.publishEmbeddedCover(song("19999", "19999"), picture);
        QTRY_VERIFY(!changed.isEmpty());
        qInfo() << "20000-row filtered/sorted cover publication, including worker (us):"
                << publicationUs;
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed[0][0].value<QModelIndex>().row(), 19998);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(model.count(), 20000);
        QSignalSpy closed(&metadata, &SongMetadataService::closed);
        metadata.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        api.shutdown();
    }
    void sharedCoverLoadsDiskFileAndReleasesInactiveImage()
    {
        QTemporaryDir dir;
        EmbeddedCoverCache cache(dir.filePath("cache"), {});
        cache.store({"hash:one"}, image(dir.path(), "reader", qRgb(80, 90, 100)));
        QTRY_VERIFY(!cache.lookup({"hash:one"}).isEmpty());
        QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Controls
Window {
    id: window
    visible: true; width: 100; height: 100
    property url cover
    EchoCover { objectName: "sharedCover"; anchors.fill: parent; source: window.cover }
})",
                          QUrl("qrc:/embedded-cover-cache.qml"));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *cover = window->findChild<QObject *>("sharedCover");
        QVERIFY(cover);
        QVERIFY(window->setProperty("cover", QUrl(cache.lookup({"hash:one"}))));
        QTRY_COMPARE(cover->property("status").toInt(), 1); // Image.Ready
        auto *picture = cover->findChild<QObject *>("coverImage");
        QVERIFY(picture);
        QVERIFY(picture->property("source").toUrl().isLocalFile());
        QVERIFY(cover->setProperty("resourceActive", false));
        QTRY_COMPARE(cover->property("status").toInt(), 0); // Image.Null
        QVERIFY(picture->property("source").toUrl().isEmpty());
        window.reset();
        close(cache);
    }
    void cacheLeaseNeverEntersQueueOrHistorySql()
    {
        QTemporaryDir dir;
        auto row = song("1");
        row["cover_url"] = "file:///owned-cache/cover.png";
        row["metadata"] = QJsonObject{{"embedded_cover_url", row["cover_url"]}};
        auto track = PlaybackTrack::fromDomain(row);
        QCOMPARE(track.toCatalog()["cover_url"], row["cover_url"]);
        QCOMPARE(track.toDomain()["cover"].toString(), QString());
        PlaybackStorageWorker storage;
        const auto path = dir.filePath("playback.sqlite");
        storage.initialize(path);
        storage.save(0, {{"default", "Queue", {track}, 0, {}, 1}}, "default");
        storage.record(100, track, 1000);
        storage.close();
        const auto connection = QStringLiteral("cover-cache-sql-oracle");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT cover,catalog_metadata FROM playback_tracks"));
            QVERIFY(q.next());
            QVERIFY(q.value(0).toString().isEmpty());
            QVERIFY(!q.value(1).toByteArray().contains("owned-cache"));
            QVERIFY(q.exec("SELECT cover,track_snapshot FROM playback_history"));
            QVERIFY(q.next());
            QVERIFY(q.value(0).toString().isEmpty());
            QVERIFY(!q.value(1).toByteArray().contains("owned-cache"));
            q.finish();
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void currentArtworkIsReleasedWhenDiskCoverIsRevoked()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
        settings.setValue(SettingsKeys::Playback::Volume, 0.0);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        PlaybackService player(api, settings);
        SongMetadataService metadata(api);
        metadata.enableCoverCache(dir.filePath("cache"));
        PlaybackServiceTestAccess::present(player, PlaybackTrack::fromDomain(song()));
        player.attachMetadata(metadata);
        metadata.publishEmbeddedCover(song(), image(dir.path(), "reader", qRgb(80, 90, 100)));
        QTRY_VERIFY(!player.cover().isEmpty());
        QTRY_VERIFY(!player.coverPng().isEmpty());
        QVERIFY(QFile::remove(file(player.cover())));
        QTRY_VERIFY(player.cover().isEmpty());
        QVERIFY(player.coverPng().isEmpty());
        QVERIFY(!player.coverColor().isValid());
        QSignalSpy picturesClosed(&metadata, &SongMetadataService::closed);
        metadata.shutdown();
        QTRY_COMPARE(picturesClosed.size(), 1);
        QSignalSpy playerClosed(&player, &PlaybackService::closed);
        player.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(playerClosed.size(), 1, 15000);
        api.shutdown();
    }
    void actualMutedReaderCoverReachesListsAndSurvivesStop()
    {
        const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("Local FFmpeg executable required for the generated audio fixture");
        QTemporaryDir dir;
        const auto picture = image(dir.path(), "reader", qRgb(90, 140, 40));
        const auto audio = dir.filePath("tagged.flac");
        QProcess encoder;
        encoder.start(ffmpeg, {"-v",
                               "error",
                               "-f",
                               "lavfi",
                               "-i",
                               "anullsrc=r=44100:cl=stereo",
                               "-i",
                               picture,
                               "-map",
                               "0:a",
                               "-map",
                               "1:v",
                               "-c:a",
                               "flac",
                               "-c:v",
                               "copy",
                               "-disposition:v",
                               "attached_pic",
                               "-t",
                               "8",
                               "-metadata",
                               "title=Disk artwork test",
                               "-y",
                               audio});
        QVERIFY(encoder.waitForFinished(15000));
        QCOMPARE(encoder.exitCode(), 0);
        QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
        settings.setValue(SettingsKeys::Playback::Volume, 0.0);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        PlaybackService player(api, settings);
        SongMetadataService metadata(api);
        metadata.enableCoverCache(dir.filePath("cache"));
        player.attachMetadata(metadata);
        SongCatalogModel first, second;
        first.attachMetadata(&metadata);
        second.attachMetadata(&metadata);
        first.setMetadataActive(false);
        second.setMetadataActive(false);
        auto track = PlaybackTrack::fromDomain(song("local:test", {}));
        track.localPath = audio;
        first.replaceSongs({track.toCatalog()});
        second.replaceSongs({track.toCatalog()});
        QTRY_VERIFY_WITH_TIMEOUT(player.engineReady(), 15000);
        player.playTracks({track}, 0, "Cover cache", "cover-cache-test");
        QTRY_VERIFY_WITH_TIMEOUT(!first.track("local:test")->cover.isEmpty(), 15000);
        const auto cover = first.track("local:test")->cover;
        QTRY_COMPARE(second.track("local:test")->cover, cover);
        QVERIFY(file(cover).startsWith(dir.filePath("cache")));
        QCOMPARE(QImage(file(cover)).pixelColor(10, 10), QColor(90, 140, 40));
        player.stop();
        QVERIFY(QFileInfo::exists(file(cover)));
        QCOMPARE(first.track("local:test")->cover, cover);
        QSignalSpy picturesClosed(&metadata, &SongMetadataService::closed);
        metadata.shutdown();
        QTRY_COMPARE(picturesClosed.size(), 1);
        QSignalSpy playerClosed(&player, &PlaybackService::closed);
        player.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(playerClosed.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(EmbeddedCoverCacheTest)
#include "EmbeddedCoverCacheTest.moc"
