#include "ListenTogetherUi.h"
#include "NativeWindow.h"
#include "RouteController.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "AccountService.h"
#include "NetworkPreferences.h"
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QEventLoop>
#include <QScopeGuard>
#include <QNetworkProxy>
#include <QInputMethodEvent>
#include <iostream>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QDir>
#include <QSignalSpy>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    static AudioEngine *engine(PlaybackService &player)
    {
        return &player.m_engine;
    }
};

class ListenTogetherControllerTestAccess
{
public:
    static void rooms(ListenTogetherController &room)
    {
        for (int n = 0; n < 6; ++n)
            room.m_publicRooms.append(
                QVariantMap{{"id", QString::number(n + 1)},
                            {"name", n == 2 ? QString("愿与君共厮守\n·\n——於我而言，你比星璀璨——")
                                            : QString("一起听房间 %1").arg(n + 1)},
                            {"member_count", 5 + n},
                            {"owner", QVariantMap{{"id", "900001"}, {"name", "测试房主"}}}});
        room.updateRoomsProjection();
    }
    static void joined(ListenTogetherController &room)
    {
        room.m_phase = ListenTogetherController::Joined;
        room.m_roomId = "1";
        room.m_activeRoom = room.m_publicRooms.first().toMap();
        room.m_activeRoom.insert("chat_code", 1);
        QVariantList songs;
        for (int i = 0; i < 20; ++i)
            songs.append(QVariantMap{{"id", QString::number(i + 1)},
                                     {"hash", QString(32, QChar('a' + i % 6))},
                                     {"title", QString("房间歌曲 %1").arg(i + 1)},
                                     {"artist", "测试歌手"}});
        room.m_songs.replace(songs);
        room.m_members.replace({QVariantMap{{"id", "900001"}, {"name", "测试房主"}}});
        emit room.changed();
        emit room.sessionReady();
    }
};
} // namespace EchoMusic
namespace
{
QQuickItem *visual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *item = visual(child, name))
            return item;
    return nullptr;
}
QList<QQuickItem *> visuals(QQuickItem *root, const QString &name)
{
    QList<QQuickItem *> result;
    if (root->objectName() == name)
        result.append(root);
    for (auto *child : root->childItems())
        result.append(visuals(child, name));
    return result;
}
bool loadingItems(QQuickItem *root)
{
    if (root->inherits("QQuickLoader") && root->property("status").toInt() == 2)
        return true;
    for (auto *child : root->childItems())
        if (loadingItems(child))
            return true;
    return false;
}
QList<QObject *> popups(QObject *root)
{
    QList<QObject *> result;
    QSet<QObject *> seen;
    const auto walk = [&](auto &&self, QObject *item) -> void {
        if (!item || seen.contains(item))
            return;
        seen.insert(item);
        if (item->inherits("QQuickPopup"))
            result.append(item);
        for (auto *child : item->children())
            self(self, child);
        if (auto *visual = qobject_cast<QQuickItem *>(item))
            for (auto *child : visual->childItems())
                self(self, child);
    };
    walk(walk, root);
    return result;
}
} // namespace
class ListenTogetherUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void liveProductionPrivateRoom()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_LISTEN_TOGETHER_UI"))
            QSKIP("Explicit isolated native production entry only");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.0);
        prefs.setValue("playback/transitionMode", "none");
        ApiClient api(DeviceIdentity{
            identity["mid"].toString(), identity["dfid"].toString(), identity["guid"].toString(),
            identity["mac"].toString(), identity["device_name"].toString(),
            identity["uuid"].toString(), identity["render_fingerprint"].toString()});
        NetworkPreferences network(prefs, api);
        QVERIFY(network.transportAvailable());
        const auto oldProxy = QNetworkProxy::applicationProxy();
        const auto restoreProxy =
            qScopeGuard([&] { QNetworkProxy::setApplicationProxy(oldProxy); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                               identity["token"].toString(), identity["t1"].toString()));
        QSignalSpy success(&api, &ApiClient::succeeded), failed(&api, &ApiClient::failed);
        const auto bytes = [&](quint64 id) -> QByteArray {
            for (const auto &row : success)
                if (row[0].toULongLong() == id)
                    return row[1].toByteArray();
            return {};
        };
        const auto finished = [&](quint64 id) {
            if (!bytes(id).isEmpty())
                return true;
            for (const auto &row : failed)
                if (row[0].toULongLong() == id)
                    return true;
            return false;
        };
        const auto initial = api.listenTogetherRoom({});
        QTRY_VERIFY_WITH_TIMEOUT(finished(initial), 20000);
        QVERIFY(!bytes(initial).isEmpty());
        QVERIFY2(
            QJsonDocument::fromJson(bytes(initial)).object().value("room_id").toString().isEmpty(),
            "Existing room left untouched");
        AccountService account(api);
        PlaybackService player(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        connect(PlaybackServiceTestAccess::engine(player), &AudioEngine::eventReceived, this,
                [](const QJsonObject &event) {
                    const auto kind = event.value("event").toString();
                    if (kind == "file-loaded" || kind == "seeked" || kind == "error" ||
                        kind == "state-change")
                        qInfo() << "Native room audio event" << kind << event.value("seq")
                                << event.value("time");
                });
        auto &room = *player.listenTogether();
        room.attachAccount(account);
        UserPlaylistModel library(api);
        SongMetadataService metadata(api);
        room.attachMetadata(metadata);
        RouteController router;
        ShellSettings settings(prefs);
        ThemeStore theme(prefs);
        ListenTogetherUi service(api, player, library, metadata, router, true);
        service.registerRoutes(router);
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
            import EchoMusic.Shell
            import EchoMusic.Theme
            Window {
                id:host
                required property RouteController router
                required property ShellSettings settings
                width:1200; height:820; visible:true; title:"EchoMusicNext 一起听隔离验证"
                Rectangle { anchors.fill:parent; color:Theme.main }
                RouteHost { anchors.fill:parent; router:host.router; settings:host.settings }
            })",
                          QUrl("qrc:/together-live-production-entry.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QTRY_VERIFY_WITH_TIMEOUT(player.engineReady(), 20000);
        PlaybackTrack seed;
        seed.id = "878751506";
        seed.songId = 878751506;
        seed.hash = "85B7F80A853CA1BC5406FC8E1F1E0312";
        seed.title = "Woodland Caribou";
        seed.artist = "Panopticon";
        seed.albumId = 189879325;
        seed.durationMs = 730000;
        player.playTracks({seed}, 0, "隔离验证队列", "native-ui-room-seed");
        QTRY_VERIFY_WITH_TIMEOUT(!player.loading(), 30000);
        QVERIFY2(player.errorMessage().isEmpty(), qPrintable(player.errorMessage()));
        QTRY_VERIFY_WITH_TIMEOUT(player.positionMs() >= 500 || !player.errorMessage().isEmpty(),
                                 15000);
        QVERIFY2(player.errorMessage().isEmpty(), qPrintable(player.errorMessage()));
        QVERIFY(router.navigate("listen-together"));
        QTRY_VERIFY_WITH_TIMEOUT(visual(window->contentItem(), "listenCreateAction"), 10000);
        const auto click = [&](const QString &name) {
            auto *item = visual(window->contentItem(), name);
            if (!item)
                return false;
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        const auto expectedName =
            QString("EchoMusicNext 验证 %1").arg(QDateTime::currentMSecsSinceEpoch() % 1000000);
        QString createdId;
        const auto cleanup = qScopeGuard([&] {
            if (createdId.isEmpty() && room.activeRoom().value("name").toString() == expectedName)
                createdId = room.roomId();
            if (!createdId.isEmpty()) {
                ListenRoomRequest request;
                request.action = ListenRoomAction::Dismiss;
                request.roomId = createdId;
                const auto id = api.listenTogetherRoom(request);
                QElapsedTimer elapsed;
                elapsed.start();
                while (!finished(id) && elapsed.elapsed() < 20000) {
                    QCoreApplication::processEvents();
                    QTest::qWait(10);
                }
                qInfo() << "Private production room cleanup acknowledged" << !bytes(id).isEmpty();
            }
            player.shutdown();
            metadata.shutdown();
            api.shutdown();
        });
        QVERIFY(click("listenCreateAction"));
        QTRY_COMPARE(popups(root.data()).size(), 1);
        auto *name = visual(window->contentItem(), "listenCreateName");
        QVERIFY(name);
        name->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(name, "selectAll"));
        QInputMethodEvent edit;
        edit.setCommitString(expectedName);
        QCoreApplication::sendEvent(name, &edit);
        QCOMPARE(name->property("text").toString(), expectedName);
        QVERIFY(click("listenPrivacyOption1"));
        QVERIFY(click("listenCreateSubmit"));
        QTRY_VERIFY_WITH_TIMEOUT(room.joined() || room.phase() == ListenTogetherController::Error,
                                 30000);
        QVERIFY2(room.joined(), qPrintable(room.errorMessage()));
        QVERIFY2(room.activeRoom().value("name").toString() == expectedName,
                 "Recovered another session; leave it untouched");
        createdId = room.roomId();
        QTRY_VERIFY_WITH_TIMEOUT(!room.loadingRoom() && !player.loading(), 30000);
        QVERIFY2(player.errorMessage().isEmpty(), qPrintable(player.errorMessage()));
        QVERIFY2(room.errorMessage().isEmpty(), qPrintable(room.errorMessage()));
        QTRY_COMPARE(popups(root.data()).size(), 0);
        QTRY_VERIFY(visual(window->contentItem(), "listenRoomManageAction"));
        const auto directory = qEnvironmentVariable("ECHO_TOGETHER_ARTIFACTS");
        if (!directory.isEmpty()) {
            QDir().mkpath(directory);
            QVERIFY(window->grabWindow().save(directory + "/live-native-room.png"));
        }
        if (qEnvironmentVariableIsSet("ECHO_TOGETHER_PEER_HOST")) {
            const auto commandPath =
                QDir::currentPath() + "/build/listen-together-peer-command.txt";
            QFile::remove(commandPath);
            QFile ready(QDir::currentPath() + "/build/listen-together-peer-ready.json");
            QVERIFY(ready.open(QIODevice::WriteOnly));
            ready.write(QJsonDocument(QJsonObject{{"room_id", createdId}, {"name", expectedName}})
                            .toJson());
            ready.close();
            qInfo().noquote() << "PEER_ROOM_READY" << createdId;
            QEventLoop loop;
            QFileSystemWatcher watcher;
            watcher.addPath(QFileInfo(commandPath).absolutePath());
            QString last;
            connect(&watcher, &QFileSystemWatcher::directoryChanged, &loop, [&] {
                QFile input(commandPath);
                if (!input.open(QIODevice::ReadOnly))
                    return;
                const auto command = QString::fromUtf8(input.readAll()).trimmed();
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
            int count = room.members()->rowCount();
            connect(room.members(), &ListenTogetherListModel::changed, &loop, [&] {
                if (room.members()->rowCount() != count) {
                    count = room.members()->rowCount();
                    qInfo() << "PEER_MEMBER_COUNT" << count;
                }
            });
            QTimer deadline;
            deadline.setSingleShot(true);
            deadline.setInterval(1200000);
            connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
            deadline.start();
            loop.exec();
            QFile::remove(ready.fileName());
        }
        QVERIFY(click("listenRoomManageAction"));
        QTRY_COMPARE(popups(root.data()).size(), 1);
        QVERIFY(click("listenDismissSubmit"));
        QTRY_VERIFY_WITH_TIMEOUT(room.phase() == ListenTogetherController::Idle, 20000);
        QTRY_COMPARE(popups(root.data()).size(), 0);
        QCOMPARE(player.sourceKey(), QString("native-ui-room-seed"));
        QVERIFY(!player.playing());
        const auto final = api.listenTogetherRoom({});
        QTRY_VERIFY_WITH_TIMEOUT(finished(final), 20000);
        QVERIFY(!bytes(final).isEmpty());
        QVERIFY(
            QJsonDocument::fromJson(bytes(final)).object().value("room_id").toString().isEmpty());
        createdId.clear();
        for (const auto &warning : warnings)
            qWarning().noquote() << warning;
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void productionRouteAndDemandCreatedDialogs()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.0);
        ApiClient api(DeviceIdentity::temporary());
        api.setNetworkTransport(1);
        QVERIFY(api.setAccount(900001, "offline-fixture"));
        PlaybackService player(api, prefs);
        UserPlaylistModel library(api);
        RouteController router;
        ShellSettings settings(prefs);
        ThemeStore theme(prefs);
        SongMetadataService metadata(api);
        ListenTogetherUi service(api, player, library, metadata, router, false);
        service.registerRoutes(router);
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
            import EchoMusic.Shell
            import EchoMusic.Theme
            Window {
                id:host
                required property RouteController router
                required property ShellSettings settings
                width:1200; height:820; visible:true
                Rectangle { anchors.fill:parent; color:Theme.main }
                Item { width:parent.width/host.settings.uiScale; height:parent.height/host.settings.uiScale; scale:host.settings.uiScale; transformOrigin:Item.TopLeft
                    RouteHost { anchors.fill:parent; router:host.router; settings:host.settings }
                }
            })",
                          QUrl("qrc:/together-production-route-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(router.available("listen-together"));
        QVERIFY(router.navigate("listen-together"));
        ListenTogetherControllerTestAccess::rooms(*player.listenTogether());
        QTRY_VERIFY_WITH_TIMEOUT(visual(window->contentItem(), "listenTogetherPage"), 10000);
        QTRY_VERIFY(visual(window->contentItem(), "listenCreateAction"));
        QTRY_VERIFY(!loadingItems(window->contentItem()));
        QTRY_VERIFY(
            visual(window->contentItem(), "listenRoomGrid")->property("atYBeginning").toBool());
        QCOMPARE(popups(root.data()).size(), 0);
        const auto capture = qEnvironmentVariable("ECHO_TOGETHER_ARTIFACTS");
        if (!capture.isEmpty())
            QDir().mkpath(capture);
        const auto click = [&](const QString &name) {
            if (!Test::prepareNativeInput(window))
                return false;
            // Scroll/implicit-header changes settle on Qt's actual next frame.
            // Derive the hit point after that frame, not from stale layout.
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            if (frames.isEmpty() && !frames.wait(2000))
                return false;
            auto *item = visual(window->contentItem(), name);
            if (!item || !item->isVisible() || !item->isEnabled())
                return false;
            const auto point =
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            if (!QRect(QPoint{}, window->size()).contains(point))
                return false;
            Test::moveNativePointer(window, point);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
            return true;
        };
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (int zoom : {0, 5}) {
                settings.resetZoom();
                for (int i = 0; i < zoom; ++i)
                    settings.zoomIn();
                QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                window->update();
                QTRY_VERIFY(!frames.isEmpty());
                if (!capture.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        capture +
                        QString("/discovery-%1-%2.png").arg(int(mode)).arg(zoom ? 125 : 100)));
                auto *refresh = visual(window->contentItem(), "listenRoomsRefresh");
                QVERIFY(refresh);
                auto *refreshSurface = refresh->property("background").value<QQuickItem *>();
                QVERIFY(refreshSurface);
                QVERIFY(refreshSurface->property("color").value<QColor>().alpha() > 0);
                auto *grid = visual(window->contentItem(), "listenRoomGrid");
                QVERIFY(grid);
                QVERIFY(grid->setProperty("contentY", 0));
                grid->ensurePolished();
                QVERIFY(QMetaObject::invokeMethod(grid, "forceLayout"));
                const auto cardFrame = window->grabWindow();
                QVERIFY(!cardFrame.isNull());
                QVERIFY(!visuals(window->contentItem(), "listenRoomCard").isEmpty());
                for (auto *card : visuals(window->contentItem(), "listenRoomCard")) {
                    auto *banner = visual(card, "listenRoomCardVisual");
                    auto *body = visual(card, "listenRoomCardBody");
                    auto *title = visual(card, "listenRoomCardTitle");
                    QVERIFY(banner && body && title);

                    QCOMPARE(banner->height(), 96.0);
                    QCOMPARE(banner->mapToItem(card, QPointF{}).y(), 0.0);
                    QCOMPARE(title->property("lineCount").toInt(), 1);
                    QVERIFY(body->mapToItem(card, QPointF{}).y() + body->height() <=
                            card->height() - 13.5);
                }
                if (!capture.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        capture +
                        QString("/cards-%1-%2.png").arg(int(mode)).arg(zoom ? 125 : 100)));
                QVERIFY(QMetaObject::invokeMethod(grid, "positionViewAtBeginning"));
                QTRY_VERIFY_WITH_TIMEOUT(visual(window->contentItem(), "listenCreateAction")
                                                 ->mapToScene(QPointF{})
                                                 .y() >= 0,
                                         5000);
                QVERIFY(click("listenCreateAction"));
                QTRY_COMPARE(popups(root.data()).size(), 1);
                QPointer<QObject> dialog = popups(root.data()).first();
                QCOMPARE(dialog->objectName(), QString("listenRoomCreateDialog"));
                QVERIFY(visual(window->contentItem(), "listenCreateName"));
                QTRY_VERIFY(dialog->property("opened").toBool());

                if (!capture.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        capture +
                        QString("/create-%1-%2.png").arg(int(mode)).arg(zoom ? 125 : 100)));
                QVERIFY(Test::prepareNativeInput(window));
                QTRY_VERIFY_WITH_TIMEOUT(dialog->property("activeFocus").toBool(), 5000);
                QTest::keyClick(window, Qt::Key_Escape);

                QTRY_VERIFY_WITH_TIMEOUT(dialog.isNull(), 5000);
                QTRY_COMPARE(popups(root.data()).size(), 0);
            }
        }
        settings.resetZoom();
        ListenTogetherControllerTestAccess::joined(*player.listenTogether());
        QTRY_VERIFY(visual(window->contentItem(), "listenRoomManageAction"));
        {
            const auto *button = visual(window->contentItem(), "listenOrdersAction");
            QTRY_VERIFY(button->parentItem()->width() >
                        button->parentItem()->parentItem()->width() - 40);
        }
        for (const auto &entry : {std::pair{"listenRoomManageAction", "listenRoomManageDialog"},
                                  std::pair{"listenOrdersAction", "listenSongOrdersDialog"},
                                  std::pair{"listenPickSongAction", "listenSongPickerDialog"}}) {
            QVERIFY(click(entry.first));
            QTRY_COMPARE(popups(root.data()).size(), 1);
            QPointer<QObject> dialog = popups(root.data()).first();
            QCOMPARE(dialog->objectName(), QString(entry.second));
            QVERIFY(Test::prepareNativeInput(window));
            QTRY_VERIFY_WITH_TIMEOUT(dialog->property("activeFocus").toBool(), 5000);
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(dialog.isNull());
            QTRY_COMPARE(popups(root.data()).size(), 0);
        }
        const auto sequenceBeforeMinimize = player.audioSequence();
        const auto queueBeforeMinimize = player.sourceKey();
        QVERIFY(click("listenRoomManageAction"));
        QTRY_COMPARE(popups(root.data()).size(), 1);
        QPointer<QObject> minimizedDialog = popups(root.data()).first();
        QTRY_VERIFY(minimizedDialog->property("opened").toBool());
        auto *returnList = visual(window->contentItem(), "listenLeaveSubmit");
        QVERIFY(returnList);
        QCOMPARE(returnList->property("text").toString(), QString("返回房间列表"));
        QVERIFY(click("listenLeaveSubmit"));
        QTRY_VERIFY(minimizedDialog.isNull());
        auto *pageItem = visual(window->contentItem(), "listenTogetherPage");
        QVERIFY(pageItem->property("sessionMinimized").toBool());
        QVERIFY(player.listenTogether()->joined());
        QCOMPARE(player.audioSequence(), sequenceBeforeMinimize);
        QCOMPARE(player.sourceKey(), queueBeforeMinimize);
        QCOMPARE(player.listenTogether()->roomScope(), 1);
        QVERIFY(!window->grabWindow().isNull());
        QTRY_VERIFY(visual(window->contentItem(), "listenRoomReturnAction")->isVisible());
        QVERIFY(click("listenRoomReturnAction"));
        QVERIFY(!pageItem->property("sessionMinimized").toBool());
        QCOMPARE(player.audioSequence(), sequenceBeforeMinimize);
        QCOMPARE(player.sourceKey(), queueBeforeMinimize);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (int zoom : {0, 5}) {
                settings.resetZoom();
                for (int i = 0; i < zoom; ++i)
                    settings.zoomIn();
                auto *now = visual(window->contentItem(), "listenNowPlayingCard");
                auto *members = visual(window->contentItem(), "listenMembersCard");
                QVERIFY(now && members);
                QTRY_COMPARE(now->height(), members->height());
                QTRY_COMPARE(now->mapToScene(QPointF{}).y(), members->mapToScene(QPointF{}).y());
                const auto rows = visuals(window->contentItem(), "listenRoomSongButton");
                QVERIFY(!rows.isEmpty());
                for (auto *row : rows) {
                    QTRY_COMPARE(row->x(), 7.0);
                    QTRY_COMPARE(row->parentItem()->width() - row->width() - row->x(), 7.0);
                }
                auto *composer = visual(window->contentItem(), "listenMessageComposer");
                auto *send = visual(window->contentItem(), "listenSendMessage");
                auto *surface = visual(window->contentItem(), "listenComposerSurface");
                QVERIFY(composer && send && surface);
                QCOMPARE(composer->parentItem(), send->parentItem());
                QCOMPARE(composer->parentItem()->parentItem(), surface);
                QVERIFY(send->mapToItem(surface, QPointF{}).x() + send->width() <=
                        surface->width() - 7.5);
                QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                window->update();
                QTRY_VERIFY(!frames.isEmpty());
                if (!capture.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        capture +
                        QString("/session-%1-%2.png").arg(int(mode)).arg(zoom ? 125 : 100)));
            }
        }
        for (const auto &warning : warnings)
            qWarning().noquote() << warning;
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        player.shutdown();
        api.shutdown();
    }
};
QTEST_MAIN(ListenTogetherUiTest)
#include "ListenTogetherUiTest.moc"
