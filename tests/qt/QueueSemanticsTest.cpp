#include "PlaybackService.h"
#include "DiscoveryService.h"
#include "AccountService.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <iostream>
using namespace EchoMusic;
namespace EchoMusic
{
class TrackTransitionControllerTestAccess
{
public:
    static int historySize(const TrackTransitionController &controller)
    {
        return controller.m_shuffleHistory.size();
    }
};
class PlaybackServiceTestAccess
{
public:
    static void hold(PlaybackService &p)
    {
        p.m_ready = false;
    }
    static quint64 sequence(const PlaybackService &p)
    {
        return p.m_sequence;
    }
    static bool restoring(const PlaybackService &p)
    {
        return p.m_queueRestorePending;
    }
    static void persist(PlaybackService &p)
    {
        p.persist();
    }
    static PlaybackRepository *repository(PlaybackService &p)
    {
        return p.m_repository.get();
    }
    static void context(PlaybackService &p, const PlaybackTracks &tracks, int current,
                        const QString &source)
    {
        p.m_sourceKey = source;
        p.m_catalog.activate(source, source, tracks, current);
        p.m_track = current >= 0 ? tracks[current] : PlaybackTrack{};
    }
};
class DiscoverQueueControllerTestAccess
{
public:
    static quint64 request(const DiscoverQueueController &c)
    {
        return c.m_request;
    }
    static int attempts(const DiscoverQueueController &c)
    {
        return c.m_attempt;
    }
    static void complete(DiscoverQueueController &c, const QJsonArray &rows, quint64 id = 0)
    {
        if (!id)
            id = c.m_request;
        c.m_api.cancel(id);
        c.succeeded(id, QJsonDocument(QJsonObject{{"songs", rows}}).toJson());
    }
};
class DiscoveryServiceTestAccess
{
public:
    static void seed(DiscoveryService &service, const QJsonArray &rows)
    {
        service.m_states[DiscoveryService::DiscoverFlow] = DiscoveryService::Ready;
        service.m_discoverFlow.replaceSongs(rows);
    }
};
} // namespace EchoMusic
namespace
{
PlaybackTrack track(const QString &id)
{
    PlaybackTrack result;
    result.id = result.title = id;
    result.hash =
        QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Md5).toHex());
    result.localPath = "/domain-only/" + id;
    return result;
}
PlaybackTracks tracks()
{
    return {track("a"), track("b"), track("c"), track("d"), track("e")};
}
struct Fixture
{
    QTemporaryDir dir;
    EchoMusic::SettingsStore settings{dir.filePath("prefs.ini")};
    ApiClient api{DeviceIdentity::temporary()};
    std::unique_ptr<PlaybackService> playback;
    Fixture()
    {
        settings.setValue("playback/volume", 0);
        settings.setValue("playback/transitionMode", "none");
        api.setNetworkTransport(1);
        playback = std::make_unique<PlaybackService>(api, settings);
        PlaybackServiceTestAccess::hold(*playback);
    }
};
QStringList ids(const PlaybackQueueModel &queue)
{
    QStringList result;
    for (const auto &t : queue.tracks())
        result.append(t.id);
    return result;
}
} // namespace
class QueueSemanticsTest : public QObject
{
    Q_OBJECT
private slots:
    void ordinaryQueuesAndPreferencesSurviveAccountSwitches()
    {
        Fixture f;
        PlaybackServiceTestAccess::context(*f.playback, tracks().mid(0, 4), 1, "shared-list");
        f.playback->queue()->restoreNext({track("d").key(), track("c").key()});
        f.playback->setVolume(37);
        f.playback->setSpeed(1.25);
        f.playback->preferences()->setDefaultQuality("flac");
        for (const auto account : {101ULL, 202ULL, 0ULL}) {
            QVERIFY(f.api.setAccount(account, account ? "offline-fixture" : ""));
            QCOMPARE(ids(*f.playback->queue()), (QStringList{"a", "b", "c", "d"}));
            QCOMPARE(f.playback->sourceKey(), QString("shared-list"));
            QCOMPARE(f.playback->trackId(), QString("b"));
            QCOMPARE(f.playback->queue()->currentIndex(), 1);
            QCOMPARE(f.playback->queue()->nextKeys(),
                     (QStringList{track("d").key(), track("c").key()}));
            QVERIFY(!f.playback->playing());
            QVERIFY(!f.playback->loading());
            QCOMPARE(f.playback->volume(), 37.);
            QCOMPARE(f.playback->speed(), 1.25);
            QCOMPARE(f.playback->preferences()->defaultQuality(), QString("flac"));
        }
    }
    void accountSwitchDropsDynamicQueuesAndKeepsOrdinarySources()
    {
        Fixture f;
        for (const auto key :
             {PersonalFmController::queueKey(), DiscoverQueueController::queueKey(),
              QString("queue:listen-together")}) {
            PlaybackServiceTestAccess::context(*f.playback, {track("a"), track("b")}, 1, "default");
            PlaybackServiceTestAccess::context(*f.playback, {track("dynamic")}, 0, key);
            f.api.advanceSession();
            QVERIFY(!f.playback->queueCatalog()->modelForKey(key));
            QCOMPARE(f.playback->sourceKey(), QString("default"));
            QCOMPARE(ids(*f.playback->queue()), (QStringList{"a", "b"}));
            QCOMPARE(f.playback->trackId(), QString("b"));
            QVERIFY(!f.playback->playing());
        }
    }
    void sharedQueueRestoresAfterRestartAndEarlyAccountSwitch()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("playback.sqlite");
        EchoMusic::SettingsStore settings(dir.filePath("prefs.ini"));
        {
            ApiClient api(DeviceIdentity::temporary());
            QVERIFY(api.setNetworkTransport(1));
            QVERIFY(api.setAccount(101, "offline-fixture"));
            PlaybackService playback(api, settings, nullptr, path);
            QTRY_VERIFY(!PlaybackServiceTestAccess::restoring(playback));
            PlaybackServiceTestAccess::context(playback, tracks().mid(0, 4), 1, "shared-list");
            playback.queue()->restoreNext({track("d").key()});
            PlaybackServiceTestAccess::persist(playback);
            auto *repository = PlaybackServiceTestAccess::repository(playback);
            repository->record(101, track("a"), 1000);
            repository->record(202, track("c"), 2000);
            QSignalSpy closed(&playback, &PlaybackService::closed);
            playback.shutdown();
            QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        }
        {
            ApiClient api(DeviceIdentity::temporary());
            QVERIFY(api.setNetworkTransport(1));
            PlaybackService playback(api, settings, nullptr, path);
            QSignalSpy history(&playback, &PlaybackService::historyLoaded);
            QVERIFY(PlaybackServiceTestAccess::restoring(playback));
            QVERIFY(api.setAccount(202, "offline-fixture"));
            QTRY_COMPARE(playback.trackId(), QString("b"));
            QCOMPARE(ids(*playback.queue()), (QStringList{"a", "b", "c", "d"}));
            QCOMPARE(playback.queue()->nextKeys(), QStringList{track("d").key()});
            QVERIFY(!playback.playing());
            QTRY_VERIFY(!history.isEmpty() && history.last()[0].value<QJsonArray>().size() == 1);
            QCOMPARE(history.last()[0]
                         .value<QJsonArray>()
                         .first()
                         .toObject()["track"]
                         .toObject()["title"]
                         .toString(),
                     QString("c"));
            QVERIFY(api.setAccount(0, ""));
            QCOMPARE(playback.trackId(), QString("b"));
            QTRY_VERIFY(history.last()[0].value<QJsonArray>().isEmpty());
            QVERIFY(api.setAccount(101, "offline-fixture"));
            QTRY_COMPARE(history.last()[0].value<QJsonArray>().size(), 1);
            QCOMPARE(history.last()[0]
                         .value<QJsonArray>()
                         .first()
                         .toObject()["track"]
                         .toObject()["title"]
                         .toString(),
                     QString("a"));
            QCOMPARE(playback.trackId(), QString("b"));
            QSignalSpy closed(&playback, &PlaybackService::closed);
            playback.shutdown();
            QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        }
    }
    void editsBeforeStartupRestoreAreNotOverwritten()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("playback.sqlite");
        {
            PlaybackRepository repository(path);
            repository.save(PlaybackRepository::SharedQueueScope,
                            {{"old", "Old queue", {track("old")}, 0, {}}}, "old");
            QSignalSpy closed(&repository, &PlaybackRepository::closed);
            repository.shutdown();
            QTRY_COMPARE(closed.size(), 1);
        }
        EchoMusic::SettingsStore settings(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        PlaybackService playback(api, settings, nullptr, path);
        QSignalSpy loaded(PlaybackServiceTestAccess::repository(playback),
                          &PlaybackRepository::loaded);
        playback.appendTracks({track("new")});
        QTRY_COMPARE(loaded.size(), 1);
        QCOMPARE(ids(*playback.queue()), QStringList{"new"});
        QVERIFY(!playback.playing());
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
    void orderedDecisionsMatchTheUnmodifiedSourceOracle()
    {
        Fixture f;
        f.playback->discoverQueue()->shutdown();
        QFile file(QStringLiteral(ECHO_QUEUE_ORACLE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto cases = QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray();
        QCOMPARE(cases.size(), 180);
        const auto list = tracks().mid(0, 3);
        const QHash<QString, PlaybackService::PlayMode> modes{
            {"list", PlaybackService::ListLoop},
            {"single", PlaybackService::SingleLoop},
            {"sequential", PlaybackService::Sequential}};
        for (const auto &value : cases) {
            const auto row = value.toObject();
            const auto currentIndex = QStringList{"a", "b", "c"}.indexOf(row["current"].toString());
            PlaybackServiceTestAccess::context(
                *f.playback, list, currentIndex,
                row["sourceKind"] == "discover" ? "queue:home-discover" : "oracle");
            f.playback->setPlayMode(modes.value(row["mode"].toString()));
            QStringList priorities;
            for (const auto &id : row["queued"].toArray())
                priorities.append(track(id.toString()).key());
            f.playback->queue()->restoreNext(priorities);
            const auto automatic = row["automatic"].toBool();
            const int index = f.playback->transition()->nextIndex(automatic);
            const auto actual = index >= 0 ? f.playback->queue()->at(index)->id : QString();
            QVERIFY2(actual == row["target"].toString(), QJsonDocument(row).toJson().constData());
            f.playback->transition()->consumeNext(index, automatic);
            QStringList remaining;
            for (const auto &id : row["remaining"].toArray())
                remaining.append(track(id.toString()).key());
            QVERIFY2(f.playback->queue()->nextKeys() == remaining,
                     QJsonDocument(row).toJson().constData());
        }
    }
    void nativeLocalIdentityDoesNotInventOrDropCatalogIds()
    {
        PlaybackQueueModel queue(nullptr);
        PlaybackTrack first, second;
        first.localPath = "/domain-only/first.wav";
        second.localPath = "/domain-only/second.wav";
        queue.replace({first, second}, 0);
        QCOMPARE(queue.rowCount(), 2);
        queue.append({second}, true);
        QCOMPARE(queue.currentIndex(), 0);
        QCOMPARE(queue.takeNextPriority(), 1);
        QVERIFY(queue.at(0)->id.isEmpty());
        QVERIFY(queue.at(1)->id.isEmpty());
    }
    void pendingQueueNeedsAnExplicitPlayIntent()
    {
        Fixture f;
        f.playback->appendTracks(tracks());
        f.playback->next();
        QVERIFY(!f.playback->hasTrack());
        f.playback->setPlayMode(PlaybackService::Shuffle);
        f.playback->play();
        QVERIFY(f.playback->hasTrack());
        QCOMPARE(f.playback->sourceKey(), QString("default"));
        QVERIFY(ids(*f.playback->queue()).contains(f.playback->trackId()));
    }
    void singleInsertionAndPhysicalNextPreserveTheSourceQueue()
    {
        Fixture f;
        const auto list = tracks();
        f.playback->playTracks(list.mid(0, 2), 0, "Context", "context");
        f.playback->preferences()->setQueueMode("single");
        f.playback->playFromSelection(list, 2, "Context", "context", true);
        QCOMPARE(ids(*f.playback->queue()), (QStringList{"a", "b", "c"}));
        f.playback->playFromSelection(list, 0, "Context", "context", true);
        f.playback->enqueueTracks({list[2]}, true);
        QCOMPARE(ids(*f.playback->queue()), (QStringList{"a", "c", "b"}));
        f.playback->enqueueTracks({list[3]}, true);
        f.playback->enqueueTracks({list[1]}, false);
        f.playback->enqueueTracks({list[4]}, false);
        QCOMPARE(ids(*f.playback->queue()), (QStringList{"a", "d", "c", "b", "e"}));
        const auto queued = f.playback->queue()->nextKeys();
        f.playback->enqueueTracks({list[0]}, true);
        QCOMPARE(f.playback->queue()->nextKeys(), queued);
        const auto snapshot = f.playback->queue()->tracks();
        f.playback->playTracks(snapshot, 0, "Context", "context");
        QCOMPARE(f.playback->queue()->nextKeys(), queued);
        f.playback->next();
        QCOMPARE(f.playback->trackId(), QString("d"));
        f.playback->playIndex(3); // Directly choosing a candidate consumes only that flag.
        QVERIFY(!f.playback->queue()->nextKeys().contains(track("b").key()));
        QVERIFY(f.playback->queue()->nextKeys().contains(track("c").key()));
    }
    void menuPlayNowAndQueuedTailAreIndependentOfRowPreferences()
    {
        Fixture f;
        SongCatalogModel model;
        QJsonArray rows;
        const auto list = tracks();
        for (const auto &entry : list)
            rows.append(entry.toCatalog());
        model.replaceSongs(rows);
        SongCatalogActions actions(model, *f.playback);
        actions.setQueueContext("Menu", "menu");
        f.playback->preferences()->setQueueMode("context");
        f.playback->playTracks(list.mid(0, 2), 0, "Menu", "menu");
        actions.playNow("c");
        QCOMPARE(ids(*f.playback->queue()), (QStringList{"a", "b", "c"}));
        const auto sequence = PlaybackServiceTestAccess::sequence(*f.playback);
        actions.playNow("c");
        QCOMPARE(PlaybackServiceTestAccess::sequence(*f.playback), sequence);
        QCOMPARE(f.playback->preferences()->queueMode(), QString("context"));
        actions.appendSong("d", false);
        actions.appendSong("e", false);
        actions.appendSong("a", true);
        QCOMPARE(ids(*f.playback->queue()), (QStringList{"b", "c", "a", "d", "e"}));
        QCOMPARE(f.playback->queue()->nextKeys(),
                 (QStringList{list[0].key(), model.track("d")->key(), model.track("e")->key()}));
        const auto priorities = f.playback->queue()->nextKeys();
        f.playback->appendTracks({track("last")});
        QCOMPARE(f.playback->queue()->nextKeys(), priorities);
        QCOMPARE(f.playback->queue()->tracks().last().id, QString("last"));
    }
    void randomCyclePeeksAppendAndHistoryFollowTheSource()
    {
        Fixture f;
        const auto list = tracks();
        f.playback->playTracks(list, 0, "Random", "random");
        f.playback->setPlayMode(PlaybackService::Shuffle);
        auto *order = f.playback->transition();
        const auto next = order->nextIndex(false);
        for (int i = 0; i < 20; ++i)
            QCOMPARE(order->nextIndex(false), next);
        f.playback->appendTracks({track("extra")});
        QCOMPARE(order->nextIndex(false), next);
        QSet<QString> heard{f.playback->trackId()};
        QString previous;
        for (int i = 0; i < 5; ++i) {
            previous = f.playback->trackId();
            f.playback->next();
            QVERIFY(!heard.contains(f.playback->trackId()));
            heard.insert(f.playback->trackId());
        }
        QCOMPARE(heard.size(), 6);
        f.playback->previous();
        QCOMPARE(f.playback->trackId(), previous);
        f.playback->setPlayMode(PlaybackService::Sequential);
        f.playback->playIndex(f.playback->queue()->rowCount() - 1);
        const auto tail = f.playback->trackId();
        f.playback->next();
        QCOMPARE(f.playback->trackId(), tail);
        QVERIFY(TrackTransitionControllerTestAccess::historySize(*order) > 0);
        QVERIFY(f.api.setAccount(9, "domain-only-account"));
        QCOMPARE(TrackTransitionControllerTestAccess::historySize(*order), 0);
    }
    void discoverReplenishesOffPageAndRejectsStaleResults()
    {
        Fixture f;
        auto *provider = f.playback->discoverQueue();
        const auto list = tracks();
        f.playback->setPlayMode(PlaybackService::SingleLoop);
        f.playback->playTracks(list, 2, "刷歌", DiscoverQueueController::queueKey());
        QVERIFY(provider->busy());
        const auto request = DiscoverQueueControllerTestAccess::request(*provider);
        QVERIFY(provider->replenish());
        QCOMPARE(DiscoverQueueControllerTestAccess::request(*provider), request);
        f.playback->next(true);
        QCOMPARE(f.playback->trackId(), QString("d"));
        f.playback->next(true);
        QCOMPARE(f.playback->trackId(), QString("e"));
        f.playback->next(true);
        QJsonArray more;
        for (const auto &id : {"f", "g", "h", "i"})
            more.append(track(id).toCatalog());
        DiscoverQueueControllerTestAccess::complete(*provider, more);
        QCOMPARE(f.playback->queue()->rowCount(), 9);
        QCOMPARE(f.playback->trackId(), QString("f"));
        QCOMPARE(f.playback->playMode(), PlaybackService::SingleLoop);
        f.playback->enqueueTracks({track("manual")}, false);
        QVERIFY(f.playback->queue()->nextKeys().isEmpty());
        QVERIFY(!f.playback->queueCatalog()->modelForKey("default")->nextKeys().isEmpty());
        f.playback->playTracks(list, 0, "Ordinary", "ordinary");
        QVERIFY(!f.playback->queueCatalog()->modelForKey(DiscoverQueueController::queueKey()));
        DiscoverQueueControllerTestAccess::complete(*provider, more, request);
        QCOMPARE(f.playback->queue()->rowCount(), 5);
    }
    void duplicateReplenishmentIsBoundedAndPauseRevokesAdvancement()
    {
        Fixture f;
        auto *provider = f.playback->discoverQueue();
        const auto first = track("a");
        f.playback->playTracks({first}, 0, "刷歌", DiscoverQueueController::queueKey());
        for (int attempt = 0; attempt < 3; ++attempt)
            DiscoverQueueControllerTestAccess::complete(*provider, {first.toCatalog()});
        QVERIFY(!provider->busy());
        QCOMPARE(DiscoverQueueControllerTestAccess::attempts(*provider), 3);
        f.playback->next();
        QVERIFY(provider->busy());
        f.playback->pause();
        DiscoverQueueControllerTestAccess::complete(*provider, {track("b").toCatalog()});
        QCOMPARE(f.playback->trackId(), QString("a"));
        QCOMPARE(f.playback->queue()->rowCount(), 2);
        f.playback->previous();
        QCOMPARE(f.playback->trackId(), QString("a")); // A transient source never wraps backward.
    }
    void discoverRefreshAndReentryFollowTheActualQueueOnlyAfterPlay()
    {
        Fixture f;
        AccountService account(f.api);
        DiscoveryService service(f.api, *f.playback, account);
        const auto domain = [](const QString &id) {
            auto row = track(id).toCatalog();
            row["artists"] = QJsonArray{QJsonObject{{"name", "Artist"}, {"id", "42"}}};
            row["album_title"] = "Album";
            row["cover_url"] = "data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'/>";
            return row;
        };
        DiscoveryServiceTestAccess::seed(service, {domain("a"), domain("b"), domain("c")});
        f.playback->preferences()->setQueueMode("single");
        service.playDiscover("b");
        QCOMPARE(f.playback->queue()->rowCount(), 3);
        QCOMPARE(service.discoverFlow()->track("b")->artist, QString("Artist"));
        QCOMPARE(service.discoverFlow()->track("b")->album, QString("Album"));
        QVERIFY(!service.discoverFlow()->track("b")->cover.isEmpty());
        const auto oldRequest =
            DiscoverQueueControllerTestAccess::request(*f.playback->discoverQueue());
        service.loadDiscoverFlow(false);
        DiscoveryServiceTestAccess::seed(service, {domain("k"), domain("l"), domain("m")});
        f.playback->playIndex(2);
        QCOMPARE(f.playback->trackId(), QString("c"));
        QCOMPARE(service.discoverFlow()->sourceItem(0)["resourceId"].toString(), QString("k"));
        service.playDiscover("k");
        QCOMPARE(ids(*f.playback->queue()), (QStringList{"k", "l", "m"}));
        DiscoverQueueControllerTestAccess::complete(*f.playback->discoverQueue(), {domain("stale")},
                                                    oldRequest);
        QCOMPARE(ids(*f.playback->queue()), (QStringList{"k", "l", "m"}));
        const auto pending =
            DiscoverQueueControllerTestAccess::request(*f.playback->discoverQueue());
        QVERIFY(pending);
        service.setPage("home", "home", false);
        QCOMPARE(DiscoverQueueControllerTestAccess::request(*f.playback->discoverQueue()), pending);
        DiscoverQueueControllerTestAccess::complete(*f.playback->discoverQueue(),
                                                    {domain("n"), domain("o"), domain("p")});
        QCOMPARE(service.discoverFlow()->count(), 6);
        service.setPage("discover-flow", "reentered", false);
        QCOMPARE(service.discoverFlow()->count(), 6);
        QCOMPARE(f.playback->trackId(), QString("k"));
    }
    void retentionRecencyAndTransientHydrationMatchTheSource()
    {
        PlaybackQueueCatalog catalog(nullptr);
        for (const auto &key : {"a", "b", "c", "d"})
            catalog.activate(key, key, {track(key)}, 0);
        QCOMPARE(catalog.snapshot().size(),
                 4); // Empty Qt default placeholder occupies no source slot.
        QCOMPARE(catalog.previewCount(), 4);
        const auto activate = [&](const QString &key) {
            for (int row = 0; row < catalog.rowCount(); ++row)
                if (catalog.data(catalog.index(row), Qt::UserRole + 1).toString() == key) {
                    catalog.setViewIndex(row);
                    catalog.activateViewed();
                    return;
                }
        };
        activate("a");
        catalog.activate("e", "e", {track("e")}, 0);
        QVERIFY(catalog.modelForKey("a"));
        QVERIFY(!catalog.modelForKey(
            "b")); // Oldest-created unprotected; usage does not change eviction order.
        activate("c");
        activate("e");
        catalog.switchPreview(1);
        QCOMPARE(catalog.viewTitle(), QString("c"));
        QTemporaryDir dir;
        PlaybackRepository repository(dir.filePath("playback.sqlite"));
        QSignalSpy loaded(&repository, &PlaybackRepository::loaded);
        repository.save(9, catalog.snapshot(), "e", "e");
        repository.load(9, 1);
        QTRY_COMPARE(loaded.size(), 1);
        PlaybackQueueCatalog restored(nullptr);
        restored.restore(loaded.first()[2].value<PlaybackQueueStates>(), "e", "e");
        restored.switchPreview(1);
        QCOMPARE(restored.viewTitle(), QString("c"));
        catalog.activate(PersonalFmController::queueKey(), "FM", {track("fm")}, 0);
        PlaybackQueueCatalog hydrated(nullptr);
        hydrated.restore(catalog.snapshot(), PersonalFmController::queueKey(), "e", true);
        QCOMPARE(hydrated.activeKey(), QString("e"));
        QVERIFY(!hydrated.modelForKey(PersonalFmController::queueKey()));
        catalog.activate(DiscoverQueueController::queueKey(), "刷歌", {track("discover")}, 0);
        hydrated.restore(catalog.snapshot(), DiscoverQueueController::queueKey(), "e", true);
        QVERIFY(!hydrated.modelForKey(DiscoverQueueController::queueKey()));
        QVERIFY(hydrated.activeKey() != DiscoverQueueController::queueKey());
    }
    void liveDiscoverQueue()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_PERSONAL_FM"))
            QSKIP("Optional real recommendation/audio probe; identity arrives on stdin.");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        const DeviceIdentity device{identity["mid"].toString(),
                                    identity["dfid"].toString(),
                                    identity["guid"].toString(),
                                    identity["mac"].toString(),
                                    identity["device_name"].toString(),
                                    "-",
                                    "-"};
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("prefs.ini"));
        settings.setValue("playback/volume", 0);
        settings.setValue("playback/transitionMode", "none");
        ApiClient api(device);
        NetworkPreferences network(settings, api);
        QVERIFY(network.transportAvailable());
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                               identity["token"].toString(), identity["t1"].toString()));
        PlaybackService playback(api, settings, nullptr, {}, &network);
        playback.setPlayMode(PlaybackService::SingleLoop);
        QSignalSpy replies(&api, &ApiClient::succeeded);
        const auto request = api.discoverRecommendations(4);
        QVERIFY(request);
        const auto response = [&] {
            for (const auto &row : replies)
                if (row[0].toULongLong() == request)
                    return row[1].toByteArray();
            return QByteArray{};
        };
        QTRY_VERIFY_WITH_TIMEOUT(!response().isEmpty(), 30000);
        PlaybackTracks list;
        for (const auto &row :
             DiscoverQueueController::songs(QJsonDocument::fromJson(response()).object())) {
            auto entry = PlaybackTrack::fromDomain(row.toObject());
            if (entry.playable())
                list.append(std::move(entry));
        }
        QVERIFY(list.size() >= 3);
        QTRY_VERIFY_WITH_TIMEOUT(playback.engineReady(), 15000);
        playback.playTracks(list, 0, "刷歌", DiscoverQueueController::queueKey());
        QTRY_VERIFY_WITH_TIMEOUT(
            playback.playing() && !playback.loading() && playback.positionMs() > 500, 30000);
        playback.next();
        QCOMPARE(playback.trackId(), list[1].id);
        playback.next();
        QCOMPARE(playback.trackId(),
                 list[2].id); // Every click advances before asynchronous resolution.
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(playback.queue()->rowCount() > list.size(), 30000);
        const auto tailIndex = playback.queue()->rowCount() - 1;
        playback.playIndex(tailIndex);
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 30000);
        const auto tail = playback.trackId();
        QVERIFY(playback.durationMs() > 2000);
        playback.seek(playback.durationMs() - 1200);
        QTRY_VERIFY_WITH_TIMEOUT(
            playback.playing() && !playback.loading() && playback.trackId() != tail, 30000);
        QVERIFY(playback.queue()->currentIndex() > tailIndex);
        QCOMPARE(playback.playMode(), PlaybackService::SingleLoop);
        playback.stop();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    }
};
QTEST_MAIN(QueueSemanticsTest)
#include "QueueSemanticsTest.moc"
