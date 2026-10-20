#include "PlaybackRepository_p.h"
#include "ListenTogetherController.h"
#include "PlaybackService.h"
#include "ListenTogetherSeed.h"
#include "NetworkPreferences.h"
#include "AccountService.h"
#include <QScopeGuard>
#include <QNetworkProxy>
#include <QFile>
#include <QFileSystemWatcher>
#include <QEventLoop>
#include <QTextStream>
#include <iostream>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static void hold(PlaybackService &p)
    {
        p.m_ready = false;
    }
    static void loaded(PlaybackService &p, bool playing)
    {
        p.m_loading = false;
        p.m_loaded = true;
        p.m_playing = playing;
        p.m_error.clear();
        emit p.changed();
    }
    static void position(PlaybackService &p, qint64 value)
    {
        p.m_position = value;
    }
};
class ListenTogetherControllerTestAccess
{
public:
    static int commands(const ListenTogetherController &room)
    {
        return room.m_ownerCommands.size();
    }
    static ListenMusicRequest queuedCommand(const ListenTogetherController &room)
    {
        return room.m_ownerCommands.value(0);
    }
    static bool ownerPending(const ListenTogetherController &room)
    {
        return room.m_ownerPending;
    }
    static int requests(const ListenTogetherController &room)
    {
        return room.m_pending.size();
    }
    static quint64 request(const ListenTogetherController &room)
    {
        return room.m_pending.isEmpty() ? 0 : room.m_pending.keys().first();
    }
    static void accept(ListenTogetherController &room, quint64 id, const QJsonObject &data)
    {
        room.succeeded(id, QJsonDocument(data).toJson());
    }
    static void fail(ListenTogetherController &room, quint64 id, int code, const QString &message)
    {
        room.failed(id, QJsonDocument(QJsonObject{{"code", code}, {"message", message}}).toJson());
    }
    static void joined(ListenTogetherController &room, bool owner, const PlaybackTracks &tracks)
    {
        room.m_phase = ListenTogetherController::Joined;
        room.m_roomId = "1234567890123456789";
        room.m_activeRoom = {
            {"id", room.m_roomId},
            {"name", "测试房间"},
            {"chat_code", 1},
            {"owner", QVariantMap{{"id", owner ? "900001" : "900002"}, {"name", "房主"}}}};
        room.m_tracks = tracks;
        room.m_previousQueue = room.m_playback.sourceKey();
        room.m_remote = {{"hash", tracks[0].hash},
                         {"song_id", tracks[0].id},
                         {"position_ms", 0},
                         {"pause_code", 2},
                         {"play_mode_code", 1}};
        room.m_remoteReceived.start();
        room.startTimers();
        room.applyRemote(true);
    }
    static void clear(ListenTogetherController &room)
    {
        room.clearSession();
    }
    static void apply(ListenTogetherController &room, qint64 position, bool force)
    {
        room.m_remote["position_ms"] = position;
        room.m_remoteReceived.restart();
        room.applyRemote(force);
    }
    static bool applying(const ListenTogetherController &room)
    {
        return room.m_remoteApplying;
    }
    static bool seeking(const ListenTogetherController &room)
    {
        return room.m_remoteSeekPending;
    }
    static bool timers(const ListenTogetherController &room)
    {
        return room.m_fastTimer.isActive() && room.m_slowTimer.isActive() &&
               room.m_heartbeatTimer.isActive();
    }
    static bool paused(const ListenTogetherController &room)
    {
        return room.m_guestPaused;
    }
    static void message(ListenTogetherController &room, const QVariantList &rows)
    {
        room.mergeMessages(rows);
    }
    static void remember(ListenTogetherController &room, const QString &id)
    {
        room.rememberDissolved(id);
    }
    static QStringList dissolved(const ListenTogetherController &room)
    {
        return room.m_dissolvedOrder;
    }
    static void pending(ListenTogetherController &room, std::function<void()> action)
    {
        room.watch(99887766, ListenTogetherController::Scope::Room,
                   [action](const QJsonObject &) { action(); });
    }
};
} // namespace EchoMusic
namespace
{
PlaybackTracks tracks()
{
    PlaybackTracks list;
    for (int n = 1; n <= 3; ++n) {
        PlaybackTrack t;
        t.id = QString::number(n);
        t.songId = n;
        t.hash = QString(32, QChar('a' + n));
        t.title = QString("歌曲%1").arg(n);
        t.durationMs = 200000;
        list.append(t);
    }
    return list;
}
struct Fixture
{
    QTemporaryDir dir;
    QSettings settings{dir.filePath("prefs.ini"), QSettings::IniFormat};
    ApiClient api{DeviceIdentity::temporary()};
    std::unique_ptr<PlaybackService> player;
    Fixture()
    {
        api.setNetworkTransport(1);
        api.setAccount(900001, "offline-test-fixture");
        player = std::make_unique<PlaybackService>(api, settings);
        PlaybackServiceTestAccess::hold(*player);
    }
    ListenTogetherController &room()
    {
        return *player->listenTogether();
    }
    ~Fixture()
    {
        player->shutdown();
        api.shutdown();
    }
};
} // namespace
class ListenTogetherTest final : public QObject
{
    Q_OBJECT
private slots:
    void liveNativePrivateRoomCycle()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_LISTEN_TOGETHER"))
            QSKIP("Explicit isolated provider probe only");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        QTemporaryDir dir;
        QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
        settings.setValue("playback/volume", 0.0);
        settings.setValue("playback/transitionMode", "none");
        ApiClient api(DeviceIdentity{
            identity["mid"].toString(), identity["dfid"].toString(), identity["guid"].toString(),
            identity["mac"].toString(), identity["device_name"].toString(),
            identity["uuid"].toString(), identity["render_fingerprint"].toString()});
        NetworkPreferences network(settings, api);
        QVERIFY(network.transportAvailable());
        const auto proxy = QNetworkProxy::applicationProxy();
        const auto restoreProxy = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(proxy); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                               identity["token"].toString(), identity["t1"].toString()));
        AccountService account(api);
        PlaybackService player(api, settings, nullptr, dir.filePath("playback.sqlite"), &network);
        player.listenTogether()->attachAccount(account);
        QSignalSpy failures(&api, &ApiClient::failed);
        QSignalSpy successes(&api, &ApiClient::succeeded);
        ListenRoomRequest status;
        const auto check = api.listenTogetherRoom(status);
        const auto response = [&](quint64 id) -> QByteArray {
            for (const auto &row : successes)
                if (row[0].toULongLong() == id)
                    return row[1].toByteArray();
            return {};
        };
        const auto finished = [&](quint64 id) {
            if (!response(id).isEmpty())
                return true;
            for (const auto &row : failures)
                if (row[0].toULongLong() == id)
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(finished(check), 20000);
        QVERIFY(!response(check).isEmpty());
        QVERIFY2(
            QJsonDocument::fromJson(response(check)).object().value("room_id").toString().isEmpty(),
            "Existing provider session left untouched; no test room created");
        QTRY_VERIFY_WITH_TIMEOUT(player.engineReady(), 20000);
        PlaybackTrack seed;
        seed.id = "878751506";
        seed.songId = 878751506;
        seed.hash = "85B7F80A853CA1BC5406FC8E1F1E0312";
        seed.title = "Woodland Caribou";
        seed.artist = "Panopticon";
        seed.albumId = 189879325;
        seed.durationMs = 730000;
        player.playTracks({seed}, 0, "隔离测试队列", "test-room-seed");
        QTRY_VERIFY_WITH_TIMEOUT(!player.loading(), 30000);
        QVERIFY2(player.errorMessage().isEmpty(), qPrintable(player.errorMessage()));
        auto &room = *player.listenTogether();
        QString temporaryRoom;
        const auto cleanup = qScopeGuard([&] {
            if (temporaryRoom.isEmpty())
                temporaryRoom = room.roomId();
            if (!temporaryRoom.isEmpty()) {
                ListenRoomRequest request;
                request.action = ListenRoomAction::Dismiss;
                request.roomId = temporaryRoom;
                const auto id = api.listenTogetherRoom(request);
                QElapsedTimer clock;
                clock.start();
                while (!finished(id) && clock.elapsed() < 20000) {
                    QCoreApplication::processEvents();
                    QTest::qWait(10);
                }
                qInfo() << "Private native room cleanup acknowledged" << !response(id).isEmpty();
            }
            player.shutdown();
            api.shutdown();
        });
        room.createRoom("EchoMusicNext 原生验证", 2);
        QTRY_VERIFY_WITH_TIMEOUT(room.joined() || room.phase() == ListenTogetherController::Error,
                                 30000);
        temporaryRoom = room.roomId();
        QVERIFY2(room.joined(), qPrintable(room.errorMessage()));
        QVERIFY(room.owner());
        QTRY_VERIFY_WITH_TIMEOUT(!room.loadingRoom() && !room.loadingSongs(), 30000);
        QVERIFY2(room.songs()->rowCount() > 0, qPrintable(room.errorMessage()));
        QTRY_COMPARE_WITH_TIMEOUT(player.sourceKey(), ListenTogetherController::queueKey(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!player.loading(), 30000);
        QVERIFY2(player.errorMessage().isEmpty(), qPrintable(player.errorMessage()));
        QSignalSpy sent(&room, &ListenTogetherController::messageSent);
        room.sendMessage("原生接口与房间服务验证");
        QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(room.messages()->rowCount() > 0, 20000);
        room.setChatEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(!room.updatingChat(), 20000);
        QCOMPARE(room.activeRoom().value("chat_code").toInt(), 2);
        room.setChatEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(!room.updatingChat(), 20000);
        QCOMPARE(room.activeRoom().value("chat_code").toInt(), 1);
        if (qEnvironmentVariableIsSet("ECHO_TOGETHER_PEER_HOST")) {
            const auto commandPath =
                QDir::currentPath() + "/build/listen-together-peer-command.txt";
            QFile::remove(commandPath);
            QFile ready(QDir::currentPath() + "/build/listen-together-peer-ready.json");
            QVERIFY(ready.open(QIODevice::WriteOnly));
            ready.write(QJsonDocument(QJsonObject{{"room_id", room.roomId()},
                                                  {"name", "EchoMusicNext 原生验证"}})
                            .toJson());
            ready.close();
            qInfo().noquote() << "PEER_ROOM_READY" << room.roomId();
            QEventLoop loop;
            QFileSystemWatcher watcher;
            watcher.addPath(QFileInfo(commandPath).absolutePath());
            QString last;
            connect(&watcher, &QFileSystemWatcher::directoryChanged, &loop, [&] {
                QFile file(commandPath);
                if (!file.open(QIODevice::ReadOnly))
                    return;
                const auto command = QString::fromUtf8(file.readAll()).trimmed();
                if (command == last)
                    return;
                last = command;
                if (command == "finish") {
                    loop.quit();
                    return;
                }
                if (command.startsWith("seek:"))
                    player.seek(command.mid(5).toLongLong());
                else if (command == "pause")
                    player.pause();
                else if (command == "play")
                    player.play();
                qInfo().noquote() << "PEER_HOST_COMMAND" << command;
            });
            QTimer deadline;
            deadline.setSingleShot(true);
            deadline.setInterval(600000);
            connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
            deadline.start();
            loop.exec();
            QFile::remove(ready.fileName());
        }
        ListenMusicRequest order;
        order.action = ListenMusicAction::OrderSong;
        order.roomId = room.roomId();
        order.audio = ListenRoomAudio{"7A628CE43F4B0B266546B7BCBBC9EA9C", "448844108"};
        const auto ordered = api.listenTogetherMusic(order);
        QTRY_VERIFY_WITH_TIMEOUT(finished(ordered), 20000);
        QVERIFY(!response(ordered).isEmpty());
        room.refreshOrders();
        QTRY_VERIFY_WITH_TIMEOUT(!room.loadingOrders(), 20000);
        QVERIFY(room.orders()->rowCount() > 0);
        QSignalSpy added(&room, &ListenTogetherController::songsSubmitted);
        room.approveOrder(room.orders()->item(0).value("key").toString());
        QTRY_COMPARE_WITH_TIMEOUT(added.size(), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!room.loadingSongs(), 20000);
        QCOMPARE(room.songs()->rowCount(), 2);
        room.leaveRoom(true);
        QTRY_VERIFY_WITH_TIMEOUT(
            !room.joined() && room.phase() != ListenTogetherController::Leaving, 20000);
        QCOMPARE(room.phase(), ListenTogetherController::Idle);
        QCOMPARE(player.sourceKey(), QString("test-room-seed"));
        QVERIFY(!player.playing());
        const auto final = api.listenTogetherRoom(status);
        QTRY_VERIFY_WITH_TIMEOUT(finished(final), 20000);
        QVERIFY(!response(final).isEmpty());
        QVERIFY(QJsonDocument::fromJson(response(final))
                    .object()
                    .value("room_id")
                    .toString()
                    .isEmpty());
        temporaryRoom.clear();
        qInfo() << "Native room create/init/hydrate/audio/chat/permissions/dismiss/queue "
                   "restore/status cycle passed";
    }
    void creationUsesTheSourceWindowBeforeAudioReferences()
    {
        PlaybackTracks queue;
        for (int i = 0; i < 60; ++i) {
            PlaybackTrack track;
            track.id = QString::number(i + 1);
            track.songId = i + 1;
            track.hash = QString::number(i + 10, 16).rightJustified(32, '0');
            queue.append(track);
        }
        auto current = queue[30];
        current.hash = current.hash.toUpper();
        current.id = "another-catalog-projection";
        const auto middle = listenTogetherSeed(queue, current);
        QCOMPARE(middle.size(), 10);
        QCOMPARE(middle.first(), queue[25]);
        QCOMPARE(middle.last(), queue[34]);
        QCOMPARE(listenTogetherSeed(queue, queue[0]).first(), queue[0]);
        QCOMPARE(listenTogetherSeed(queue, queue[59]).first(), queue[50]);
        QCOMPARE(listenTogetherSeed(queue, {}).first(), queue[0]);
        auto shortQueue = queue.mid(0, 50);
        shortQueue[1] = shortQueue[0];
        QCOMPARE(listenTogetherSeed(shortQueue, {}).size(), 50);
        QCOMPARE(listenTogetherSeed(shortQueue, {})[1], shortQueue[0]);
        PlaybackTrack local;
        local.localPath = "/test/local.flac";
        shortQueue.prepend(local);
        QCOMPARE(listenTogetherSeed(shortQueue, {}).size(), 50);
        QCOMPARE(listenTogetherSeed({}, queue[3]), PlaybackTracks{queue[3]});
        QVERIFY(listenTogetherSeed({local}, queue[3]).isEmpty());
    }
    void localOnlyCreationDoesNotIssueAnyRoomRequest()
    {
        Fixture f;
        PlaybackTrack local;
        local.id = "local";
        local.localPath = "/test/local.flac";
        f.player->playTracks({local}, 0, "本地音乐", "local-only");
        QSignalSpy notices(&f.room(), &ListenTogetherController::notice);
        const int pending = ListenTogetherControllerTestAccess::requests(f.room());
        f.room().createRoom("不应创建空房间", 2);
        QCOMPARE(f.room().phase(), ListenTogetherController::Idle);
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), pending);
        QCOMPARE(notices.size(), 1);
    }
    void sharedInvitationDoesNotRecoverOrResumeDetachedPlayback()
    {
        Fixture f;
        auto &room = f.room();
        const auto songs = tracks();
        f.player->playTracks(songs, 0, "普通队列", "ordinary");
        ListenTogetherControllerTestAccess::joined(room, false, songs);
        f.player->playTracks({songs[1]}, 0, "其它来源", "elsewhere");
        QVERIFY(room.playbackDetached());
        const auto source = f.player->sourceKey();
        const auto queue = f.player->queue()->tracks();
        const auto requests = ListenTogetherControllerTestAccess::requests(room);
        room.openSharedRoom("456", false); // Offline entry still follows the no-resume policy.
        QCOMPARE(f.player->sourceKey(), source);
        QCOMPARE(f.player->queue()->tracks(), queue);
        QVERIFY(room.playbackDetached());
        QCOMPARE(ListenTogetherControllerTestAccess::requests(room), requests);
        QVERIFY(room.joined());
    }
    void membershipPersistsOffPageAndGuestCannotAutoAdvance()
    {
        Fixture f;
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), 0);
        QVERIFY(!ListenTogetherControllerTestAccess::timers(f.room()));
        ListenTogetherControllerTestAccess::joined(f.room(), false, tracks());
        QVERIFY(f.room().joined());
        QVERIFY(!f.room().owner());
        QVERIFY(f.room().suppressAutomaticNext());
        const auto seq = f.player->audioSequence();
        f.player->next(true);
        QCOMPARE(f.player->audioSequence(), seq);
        f.room().setPageActive(false);
        QVERIFY(f.room().joined());
        QVERIFY(ListenTogetherControllerTestAccess::timers(f.room()));
        ListenTogetherControllerTestAccess::clear(f.room());
        QVERIFY(!ListenTogetherControllerTestAccess::timers(f.room()));
    }
    void joiningSameAudibleSongAdoptsRoomWithoutRestartingAudio()
    {
        Fixture f;
        auto ordinary = tracks();
        ordinary[0].hash = ordinary[0].hash.toUpper();
        f.player->playTracks(ordinary, 0, "普通队列", "ordinary");
        PlaybackServiceTestAccess::loaded(*f.player, false);
        const auto sequence = f.player->audioSequence();
        ListenTogetherControllerTestAccess::joined(f.room(), true, tracks());
        QCOMPARE(f.player->sourceKey(), ListenTogetherController::queueKey());
        QCOMPARE(f.player->audioSequence(), sequence);
        QVERIFY(!f.player->loading());
        QCOMPARE(f.player->queue()->rowCount(), 3);
    }
    void leavingRestoresPriorQueueWithoutPlaying()
    {
        Fixture f;
        const auto normal = tracks();
        f.player->playTracks(normal, 1, "普通队列", "ordinary");
        ListenTogetherControllerTestAccess::joined(f.room(), true, tracks());
        QCOMPARE(f.player->sourceKey(), ListenTogetherController::queueKey());
        ListenTogetherControllerTestAccess::clear(f.room());
        QCOMPARE(f.player->sourceKey(), QString("ordinary"));
        QCOMPARE(f.player->trackId(), normal[1].id);
        QVERIFY(!f.player->playing());
        QVERIFY(!f.player->loading());
        QCOMPARE(f.player->queue()->rowCount(), 3);
        QVERIFY(!f.player->queueCatalog()->modelForKey(ListenTogetherController::queueKey()));
    }
    void detachedSessionDoesNotSeizeOtherAudioOrDestroyOtherQueue()
    {
        Fixture f;
        ListenTogetherControllerTestAccess::joined(f.room(), false, tracks());
        PlaybackServiceTestAccess::loaded(*f.player, false);
        f.player->playTracks(tracks(), 2, "其它来源", "elsewhere");
        QVERIFY(f.room().playbackDetached());
        const auto seq = f.player->audioSequence();
        ListenTogetherControllerTestAccess::apply(f.room(), 50000, true);
        QCOMPARE(f.player->audioSequence(), seq);
        QCOMPARE(f.player->sourceKey(), QString("elsewhere"));
        ListenTogetherControllerTestAccess::clear(f.room());
        QCOMPARE(f.player->sourceKey(), QString("elsewhere"));
        QCOMPARE(f.player->trackId(), QString("3"));
        QVERIFY(f.player->loading());
    }
    void staleRoomAndAccountCallbacksCannotRepopulateNewSession()
    {
        Fixture f;
        ListenTogetherControllerTestAccess::joined(f.room(), true, tracks());
        bool called = false;
        ListenTogetherControllerTestAccess::pending(f.room(), [&] { called = true; });
        ListenTogetherControllerTestAccess::clear(f.room());
        ListenTogetherControllerTestAccess::accept(f.room(), 99887766, {});
        QVERIFY(!called);
        ListenTogetherControllerTestAccess::joined(f.room(), false, tracks());
        ListenTogetherControllerTestAccess::pending(f.room(), [&] { called = true; });
        f.api.setAccount(900003, "new-offline-session");
        ListenTogetherControllerTestAccess::accept(f.room(), 99887766, {});
        QVERIFY(!called);
        QVERIFY(!f.room().joined());
    }
    void ownerCannotCreateOrJoinAnotherRoomAndErrorsAreNotBypassed()
    {
        Fixture f;
        ListenTogetherControllerTestAccess::joined(f.room(), true, tracks());
        const auto pending = ListenTogetherControllerTestAccess::requests(f.room());
        QSignalSpy notices(&f.room(), &ListenTogetherController::notice);
        f.room().createRoom("新房间", 2);
        f.room().joinRoom("other");
        QCOMPARE(notices.size(), 2);
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), pending);
        QVERIFY(f.room().owner());
        ListenTogetherControllerTestAccess::clear(f.room());
        f.player->playTracks(tracks(), 0, "普通队列", "create-error-seed");
        f.room().createRoom("新房间", 2);
        QCOMPARE(f.room().phase(), ListenTogetherController::Creating);
        const auto id = ListenTogetherControllerTestAccess::request(f.room());
        QVERIFY(id);
        ListenTogetherControllerTestAccess::fail(f.room(), id, 20002, "服务器业务失败");
        QCOMPARE(f.room().phase(), ListenTogetherController::Error);
        QVERIFY(!f.room().joined());
        QVERIFY(!f.room().errorMessage().isEmpty());
    }
    void nativeSeekToleranceAndConfirmationAvoidRepeatedCorrection()
    {
        Fixture f;
        ListenTogetherControllerTestAccess::joined(f.room(), false, tracks());
        PlaybackServiceTestAccess::loaded(*f.player, false);
        QVERIFY(!ListenTogetherControllerTestAccess::applying(f.room()));
        PlaybackServiceTestAccess::position(*f.player, 1000);
        ListenTogetherControllerTestAccess::apply(f.room(), 1600, true);
        QVERIFY(!ListenTogetherControllerTestAccess::seeking(f.room()));
        ListenTogetherControllerTestAccess::apply(f.room(), 4000, true);
        QVERIFY(ListenTogetherControllerTestAccess::seeking(f.room()));
        PlaybackServiceTestAccess::position(*f.player, 4000);
        f.room().nativeSeekCompleted();
        QVERIFY(!ListenTogetherControllerTestAccess::seeking(f.room()));
        QVERIFY(!ListenTogetherControllerTestAccess::applying(f.room()));
        f.player->pause();
        QVERIFY(ListenTogetherControllerTestAccess::paused(f.room()));
        ListenTogetherControllerTestAccess::apply(f.room(), 50000, true);
        QVERIFY(!ListenTogetherControllerTestAccess::seeking(f.room()));
        QCOMPARE(f.player->positionMs(), qint64(4000));
    }
    void explicitResumeQueuesFreshSyncAndInvalidatesTheOldSnapshot()
    {
        Fixture f;
        ListenTogetherControllerTestAccess::joined(f.room(), false, tracks());
        f.room().resumeRoomPlayback();
        const auto first = ListenTogetherControllerTestAccess::request(f.room());
        QVERIFY(first);
        f.room().resumeRoomPlayback();
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), 1);
        ListenTogetherControllerTestAccess::accept(f.room(), first,
                                                   {{"hash", tracks()[1].hash},
                                                    {"song_id", "2"},
                                                    {"pause_code", 2},
                                                    {"position_ms", 20000}});
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), 1);
        const auto second = ListenTogetherControllerTestAccess::request(f.room());
        QVERIFY(second != first);
        QCOMPARE(f.player->trackId(), QString("1"));
        ListenTogetherControllerTestAccess::accept(
            f.room(), second,
            {{"hash", tracks()[0].hash}, {"song_id", "1"}, {"pause_code", 2}, {"position_ms", 0}});
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), 0);
    }
    void ownerCommandsAreSerializedAtTheNativeStateBoundary()
    {
        Fixture f;
        ListenTogetherControllerTestAccess::joined(f.room(), true, tracks());
        PlaybackServiceTestAccess::loaded(*f.player, false);
        PlaybackServiceTestAccess::loaded(*f.player, true);
        QVERIFY(ListenTogetherControllerTestAccess::ownerPending(f.room()));
        const auto first = ListenTogetherControllerTestAccess::request(f.room());
        QVERIFY(first);
        f.player->setPlayMode(PlaybackService::Shuffle);
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), 1);
        QCOMPARE(ListenTogetherControllerTestAccess::commands(f.room()), 1);
        ListenTogetherControllerTestAccess::accept(f.room(), first, {{"version", "2"}});
        QCOMPARE(ListenTogetherControllerTestAccess::commands(f.room()), 0);
        QCOMPARE(ListenTogetherControllerTestAccess::requests(f.room()), 1);
        const auto second = ListenTogetherControllerTestAccess::request(f.room());
        QVERIFY(second != first);
        ListenTogetherControllerTestAccess::accept(f.room(), second, {});
        QVERIFY(!ListenTogetherControllerTestAccess::ownerPending(f.room()));
    }
    void singleRepeatPublishesSwitchOnlyAfterItsNativeBoundary()
    {
        Fixture f;
        ListenTogetherControllerTestAccess::joined(f.room(), true, tracks());
        PlaybackServiceTestAccess::loaded(*f.player, false);
        f.room().nativeTrackReady();
        f.player->setPlayMode(PlaybackService::SingleLoop);
        QVERIFY(ListenTogetherControllerTestAccess::ownerPending(f.room()));
        f.player->next(true);
        QCOMPARE(ListenTogetherControllerTestAccess::commands(f.room()), 0);
        PlaybackServiceTestAccess::loaded(*f.player, false);
        f.room().nativeTrackReady();
        QCOMPARE(ListenTogetherControllerTestAccess::commands(f.room()), 1);
        const auto command = ListenTogetherControllerTestAccess::queuedCommand(f.room());
        QCOMPARE(command.action, ListenMusicAction::Switch);
        QVERIFY(command.automatic);
        QCOMPARE(command.audio->songId, QString("1"));
    }
    void roomIndexSurvivesStorageRestartAndIsAccountScoped()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("playback.sqlite");
        const QJsonArray first{QJsonObject{{"id", "1234567890123456789"}, {"name", "私密房间"}}};
        {
            PlaybackStorageWorker storage;
            storage.initialize(path);
            storage.saveRooms(900001, first, {"gone-a", "gone-b"});
            storage.saveRooms(
                900002,
                QJsonArray{QJsonObject{{"id", "9876543210987654321"}, {"name", "另一账号"}}}, {});
            storage.close();
        }
        PlaybackStorageWorker storage;
        QSignalSpy rows(&storage, &PlaybackStorageWorker::roomsLoaded);
        storage.initialize(path);
        storage.loadRooms(900001, 17);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0][0].toULongLong(), quint64(900001));
        QCOMPARE(rows[0][1].toULongLong(), quint64(17));
        QCOMPARE(qvariant_cast<QJsonArray>(rows[0][2]), first);
        QCOMPARE(rows[0][3].toStringList(), QStringList({"gone-a", "gone-b"}));
        storage.loadRooms(900002, 18);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(qvariant_cast<QJsonArray>(rows[1][2])[0].toObject().value("name").toString(),
                 QString("另一账号"));
        storage.close();
    }
    void presenceMessagesAndDissolvedIndexAreBoundedAndDeduplicated()
    {
        Fixture f;
        QVariantMap author{{"id", "42"}, {"name", "听众"}};
        ListenTogetherControllerTestAccess::message(
            f.room(),
            {QVariantMap{
                {"id", "a"}, {"author", author}, {"kind_code", 4001}, {"sent_at_seconds", 100}}});
        ListenTogetherControllerTestAccess::message(
            f.room(),
            {QVariantMap{
                {"id", "b"}, {"author", author}, {"kind_code", 4001}, {"sent_at_seconds", 115}}});
        QCOMPARE(f.room().messages()->rowCount(), 1);
        for (int i = 0; i < 240; ++i)
            ListenTogetherControllerTestAccess::message(f.room(),
                                                        {QVariantMap{{"id", QString::number(i)},
                                                                     {"sent_at_seconds", 200 + i},
                                                                     {"kind_code", 0}}});
        QCOMPARE(f.room().messages()->rowCount(), 200);
        for (int i = 0; i < 240; ++i)
            ListenTogetherControllerTestAccess::remember(f.room(), QString::number(i));
        QCOMPARE(ListenTogetherControllerTestAccess::dissolved(f.room()).size(), 200);
        QCOMPARE(ListenTogetherControllerTestAccess::dissolved(f.room()).first(), QString("40"));
    }
};
QTEST_MAIN(ListenTogetherTest)
#include "ListenTogetherTest.moc"
