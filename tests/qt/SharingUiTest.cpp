#include "HistoryService.h"
#include "AlbumDetailService.h"
#include "ArtistDetailService.h"
#include "PlaylistDetailService.h"
#include "DetailsService.h"
#include "NetworkPreferences.h"
#include "ApplicationUrlHandler.h"
#include <QFileOpenEvent>
#include <QBuffer>
#include <QScopeGuard>
#include <QNetworkProxy>
#include <iostream>
#include "ShareResolveService.h"
#include "SharingCommands.h"
#include "AccountService.h"
#include "PlaybackService.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "WindowController.h"
#include "NativeWindow.h"
#include <QClipboard>
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class HistoryServiceTestAccess
{
public:
    static void accept(HistoryService &service, const QJsonArray &entries)
    {
        service.accept(entries);
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
        if (auto *found = visual(child, name))
            return found;
    return nullptr;
}
QQuickItem *activePage(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name && root->property("pageActive").toBool())
        return root;
    for (auto *child : root->childItems())
        if (auto *found = activePage(child, name))
            return found;
    return nullptr;
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
QJsonArray historyEntries()
{
    QJsonArray result;
    for (int i = 0; i < 5; ++i) {
        PlaybackTrack track;
        track.id = QString::number(100 + i);
        track.hash = QString(32, QChar('a' + i));
        track.title = QString("统计歌曲 %1 · Native artwork").arg(i + 1);
        track.artist = i % 2 ? "周杰伦" : "Panopticon";
        track.album = "示例专辑";
        QImage artwork(32, 32, QImage::Format_RGB32);
        artwork.fill(QColor("#ff2633"));
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        artwork.save(&buffer, "PNG");
        track.cover = "data:image/png;base64," + QString::fromLatin1(png.toBase64());
        track.durationMs = 180000;
        result.append(
            QJsonObject{{"key", track.id},
                        {"track", track.toDomain()},
                        {"played_at", QDateTime::currentMSecsSinceEpoch() - i * 86400000LL},
                        {"play_count", 10 - i},
                        {"listened_ms", 10000}});
    }
    return result;
}
} // namespace
class SharingUiTest final : public QObject
{
    Q_OBJECT
private slots:
    void liveTypedSharesOpenProductionDetails()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_SHARING_UI"))
            QSKIP("Explicit selected-native-account live verification only");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity{identity["mid"].toString(), identity["dfid"].toString(),
                                     identity["guid"].toString(), identity["mac"].toString(),
                                     "EchoMusicNext", identity["uuid"].toString(),
                                     identity["render_fingerprint"].toString()});
        NetworkPreferences network(prefs, api);
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
        QList<ShareTarget> targets;
        for (const auto kind : {ApiClient::SearchKind::Song, ApiClient::SearchKind::Playlist,
                                ApiClient::SearchKind::Album, ApiClient::SearchKind::Artist}) {
            const auto request =
                api.search(kind, kind == ApiClient::SearchKind::Song ? "晴天" : "周杰伦", 1, 5);
            QTRY_VERIFY_WITH_TIMEOUT(finished(request), 20000);
            QVERIFY2(!bytes(request).isEmpty(), "Live discovery search failed");
            const auto items = QJsonDocument::fromJson(bytes(request))
                                   .object()["page"]
                                   .toObject()["items"]
                                   .toArray();
            QVERIFY(!items.isEmpty());
            const auto item = items.first().toObject();
            const auto id = item["id"].toVariant().toString();
            if (kind == ApiClient::SearchKind::Song)
                targets.append(
                    {ShareResource::Song,
                     item["hash"].toString(),
                     {{"albumId", item["album_id"].toVariant().toString()}, {"albumAudioId", id}}});
            else if (kind == ApiClient::SearchKind::Playlist)
                targets.append(
                    {ShareResource::Playlist,
                     item["global_id"].toString().isEmpty() ? id : item["global_id"].toString(),
                     {}});
            else
                targets.append({kind == ApiClient::SearchKind::Album ? ShareResource::Album
                                                                     : ShareResource::Artist,
                                id,
                                {}});
        }
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        SharingCommands commands(account);
        playback.songCommands()->attachSharing(commands);
        SongMetadataService metadata(api);
        RouteController router;
        ShellSettings settings(prefs);
        ShellController shell(prefs, router);
        shell.attachPlayback(playback);
        WindowController windowController(prefs, false);
        UserPlaylistModel lists(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        PlaylistDetailService playlists(api, playback, lists, mutations, order, metadata, router);
        playlists.registerRoutes(router);
        AlbumDetailService albums(api, playback, router);
        albums.registerRoutes(router);
        ArtistDetailService artists(api, playback, router, settings);
        artists.registerRoutes(router);
        DetailsService details(api, playback, router);
        details.registerRoutes(router);
        details.attachShell(shell);
        ShareResolveService shares(api, commands, router, shell, windowController);
        shares.registerRoutes(router);
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
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Playlists
import EchoMusic.Albums
import EchoMusic.Artists
import EchoMusic.Details
import EchoMusic.Sharing
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1280; height: 900
 Rectangle { anchors.fill: parent; color: Theme.main }
 RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
})",
                          QUrl("qrc:/sharing-live-production.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        windowController.attach(window);
        ApplicationUrlHandler delivery;
        connect(&delivery, &ApplicationUrlHandler::urlRequested, &shares,
                [&](const QUrl &url) { shares.openUrl(url); });
        const auto output = qEnvironmentVariable("ECHO_SHARE_UI_CAPTURE_DIR");
        if (!output.isEmpty())
            QVERIFY(QDir().mkpath(output));
        for (const auto &target : targets) {
            QVERIFY(commands.copyTarget(target));
            const auto received = parseShareTarget(QGuiApplication::clipboard()->text());
            QVERIFY(received && *received == target);
            QFileOpenEvent open(shareAppUrl(*received));
            QCoreApplication::sendEvent(qGuiApp, &open);
            delivery.beginDelivery(); // Also exercises the startup-queued first URL.
            const auto route = shareResourceName(target.type) + "-detail";
            QTRY_VERIFY_WITH_TIMEOUT(router.current() && router.current()->name() == route, 25000);
            const auto pageName = shareResourceName(target.type) + "DetailPage";
            QTRY_VERIFY_WITH_TIMEOUT(activePage(window->contentItem(), pageName), 10000);
            QTRY_VERIFY(!loadingItems(window->contentItem()));
            if (target.type == ShareResource::Song) {
                auto *context = details.songFor(router.current());
                QTRY_VERIFY_WITH_TIMEOUT(!context->loading(), 20000);
                QVERIFY(!context->detail().isEmpty());
                const auto track = context->songs()->track(context->songId());
                QVERIFY(track);
                QCOMPARE(track->hash.toLower(), target.id.toLower());
            } else {
                if (target.type == ShareResource::Album) {
                    auto *context = albums.contextFor(router.current());
                    QTRY_VERIFY_WITH_TIMEOUT(!context->detailLoading(), 20000);
                    QVERIFY(!context->detail().isEmpty());
                } else if (target.type == ShareResource::Artist) {
                    auto *context = artists.contextFor(router.current());
                    QTRY_VERIFY_WITH_TIMEOUT(!context->detailLoading(), 20000);
                    QVERIFY(!context->detail().isEmpty());
                } else {
                    auto *context = playlists.contextFor(router.current());
                    QTRY_VERIFY_WITH_TIMEOUT(!context->loading(), 20000);
                    QVERIFY(!context->detail().isEmpty());
                }
                auto *currentPage = activePage(window->contentItem(), pageName);
                QVERIFY(currentPage);
                QTRY_VERIFY(visual(currentPage, "detailShareButton"));
                auto *button = visual(currentPage, "detailShareButton");
                QVERIFY(button->isVisible());
                QVERIFY(Test::prepareNativeInput(window));
                QVERIFY(!window->grabWindow().isNull());
                const auto point =
                    button->mapToScene(QPointF(button->width() / 2, button->height() / 2))
                        .toPoint();
                Test::moveNativePointer(window, point);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                const auto fromEntry = parseShareTarget(QGuiApplication::clipboard()->text());
                QVERIFY(fromEntry);
                QCOMPARE(fromEntry->type, target.type);
                if (target.type != ShareResource::Playlist)
                    QCOMPARE(fromEntry->id, target.id);
                else {
                    const auto expectedTitle =
                        playlists.contextFor(router.current())->detail().value("title");
                    QVERIFY(shares.openText(QGuiApplication::clipboard()->text()));
                    QTRY_VERIFY_WITH_TIMEOUT(router.current() &&
                                                 router.current()->name() == route &&
                                                 router.current()->resourceId() == fromEntry->id,
                                             25000);
                    QTRY_VERIFY(activePage(window->contentItem(), pageName));
                    QTRY_VERIFY(!loadingItems(window->contentItem()));
                    auto *opened = playlists.contextFor(router.current());
                    QTRY_VERIFY_WITH_TIMEOUT(!opened->loading(), 20000);
                    QCOMPARE(opened->detail().value("title"), expectedTitle);
                }
            }
            QCOMPARE(playback.queue()->rowCount(), 0);
            if (!output.isEmpty())
                QVERIFY(window->grabWindow().save(output + "/live-" +
                                                  shareResourceName(target.type) + ".png"));
            qInfo().noquote() << "Live share opened production page:" << route;
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QChar(10))));
        shares.shutdown();
        details.shutdown();
        albums.shutdown();
        artists.shutdown();
        playlists.shutdown();
        metadata.shutdown();
        router.shutdown();
        shell.shutdown();
        windowController.shutdown();
        playback.shutdown();
        api.shutdown();
    }
    void productionHistoryBitmapAndIncomingPage()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("ui/motionEnabled", false);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-native-fixture"));
        AccountService account(api);
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SharingCommands commands(account);
        playback.songCommands()->attachSharing(commands);
        SongMetadataService metadata(api);
        RouteController router;
        ShellController shell(prefs, router);
        shell.attachPlayback(playback);
        WindowController windowController(prefs, false);
        HistoryService history(api, playback, metadata, router, false);
        history.registerRoutes(router);
        ShareResolveService shares(api, commands, router, shell, windowController, false);
        shares.registerRoutes(router);
        ThemeStore theme(prefs);
        ShellSettings settings(prefs);
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
import EchoMusic.History
import EchoMusic.Sharing
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1280; height: 900
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  width: parent.width / window.settings.uiScale
  height: parent.height / window.settings.uiScale
  scale: window.settings.uiScale
  transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/sharing-production-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"router", QVariant::fromValue(&router)},
                                                   {"settings", QVariant::fromValue(&settings)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(router.navigate("history", {{"tab", "stats"}}));
        QTRY_VERIFY(visual(window->contentItem(), "historyPage"));
        // Finish the real history read before injecting accepted domain data.
        QTRY_VERIFY(!history.loading());
        const auto entries = historyEntries();
        HistoryServiceTestAccess::accept(history, entries);
        QTRY_VERIFY(visual(window->contentItem(), "historyStatsShareAction"));
        QVERIFY(!visual(window->contentItem(), "historyStatsCapture"));
        QSignalSpy captured(history.statsShare(), &HistoryStatsShare::captured);
        QSignalSpy notices(history.statsShare(), &HistoryStatsShare::notice);
        QPointer<QQuickItem> lastStage;
        int createdStages = 0;
        bool snapshotMatches = true;
        bool viewportMatches = true;
        connect(history.statsShare(), &HistoryStatsShare::busyChanged, this, [&] {
            if (history.statsShare()->busy()) {
                lastStage = visual(window->contentItem(), "historyStatsCapture");
                ++createdStages;
                snapshotMatches =
                    lastStage && lastStage->property("stats").toMap() == history.stats();
                const auto viewportWidth = window->width() / settings.uiScale();
                auto *grid = lastStage ? visual(lastStage, "historyStatGrid") : nullptr;
                viewportMatches =
                    lastStage && grid &&
                    qAbs(lastStage->property("viewportWidth").toReal() - viewportWidth) < .001 &&
                    grid->property("columns").toInt() == (viewportWidth <= 640   ? 1
                                                          : viewportWidth <= 980 ? 2
                                                                                 : 4);
            }
        });
        const auto output = qEnvironmentVariable("ECHO_SHARE_UI_CAPTURE_DIR");
        if (!output.isEmpty())
            QVERIFY(QDir().mkpath(output));
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            theme.setMode(mode);
            for (const auto width : {1280, 850}) {
                for (const auto zoom : {100, 125}) {
                    settings.resetZoom();
                    if (zoom == 125)
                        for (int step = 0; step < 5; ++step)
                            settings.zoomIn();
                    window->setWidth(width);
                    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY(!frames.isEmpty());
                    QVERIFY(Test::prepareNativeInput(window));
                    auto *button = visual(window->contentItem(), "historyStatsShareAction");
                    QVERIFY(button && button->isVisible() && button->isEnabled());
                    const auto point =
                        button->mapToScene(QPointF(button->width() / 2, button->height() / 2))
                            .toPoint();
                    const auto before = captured.size();
                    Test::moveNativePointer(window, point);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                    QVERIFY2(createdStages == before + 1,
                             qPrintable(QString("created=%1 captured=%2 notices=%3")
                                            .arg(createdStages)
                                            .arg(captured.size())
                                            .arg(notices.size())));
                    QVERIFY(snapshotMatches);
                    QVERIFY(viewportMatches);
                    QTRY_COMPARE_WITH_TIMEOUT(captured.size(), before + 1, 5000);
                    QTRY_VERIFY(lastStage.isNull());
                    QVERIFY(!history.statsShare()->busy());
                    QVERIFY(!visual(window->contentItem(), "historyStatsCapture"));
                    const auto image = qvariant_cast<QImage>(captured.last().first());
                    QVERIFY(!image.isNull());
                    QVERIFY(image.width() <= (window->width() - 32) * window->devicePixelRatio());
                    QVERIFY(image.height() <= (window->height() - 32) * window->devicePixelRatio());
                    QCOMPARE(image.pixelColor(0, 0).alpha(), 255);
                    int redPixels = 0;
                    for (int y = 0; y < image.height(); ++y)
                        for (int x = 0; x < image.width(); ++x) {
                            const auto pixel = image.pixelColor(x, y);
                            if (pixel.red() > 230 && pixel.green() < 60 && pixel.blue() < 80)
                                ++redPixels;
                        }
                    QVERIFY(redPixels > 100); // Artwork loaded and painted before capture.
                    QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage());
                    QCOMPARE(QGuiApplication::clipboard()->image(), image);
                    if (!output.isEmpty())
                        QVERIFY(image.save(
                            output +
                            QString("/stats-%1-%2-%3.png").arg(int(mode)).arg(width).arg(zoom)));
                    QCOMPARE(history.stats()["totalPlays"].toInt(), 40);
                    QCOMPARE(playback.queue()->rowCount(), 0);
                }
            }
        }
        // Cancellation destroys the whole temporary tree before any image is
        // published, and a later real action remains usable.
        auto *page = visual(window->contentItem(), "historyPage");
        history.statsShare()->share(page);
        QPointer<QQuickItem> cancelled(visual(window->contentItem(), "historyStatsCapture"));
        QVERIFY(cancelled);
        const auto beforeCancel = captured.size();
        QVERIFY(shares.openText("echomusicnext://song/invalid"));
        QTRY_VERIFY(cancelled.isNull());
        QTRY_VERIFY(visual(window->contentItem(), "shareResolvePage"));
        QCOMPARE(captured.size(), beforeCancel);
        QVERIFY(!shares.loading());
        QCOMPARE(history.statsShare()->busy(), false);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(output + "/invalid-share.png"));
        for (const auto &warning : warnings)
            qWarning().noquote() << warning;
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        windowController.attach(window);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(commands.shareArtist("3520", "测试歌手"));
        shares.startClipboardMonitoring();
        QVERIFY(!shell.toasts()->visible()); // Own copies are suppressed before mutation.
        const auto external =
            shareText({ShareResource::Album, "123", {}}, "测试专辑", "另一位用户");
        QGuiApplication::clipboard()->setText(external);
        shares.inspectClipboard();
        QVERIFY(shell.toasts()->visible());
        QCOMPARE(shell.toasts()->actionLabel(), QString("打开"));
        QCOMPARE(router.current()->name(),
                 QString("share-resolve")); // Detection alone never navigates.
        shell.toasts()->triggerAction(shell.toasts()->noticeId());
        QCOMPARE(shares.targetId(), QString("123"));
        QVERIFY(!shares.loading()); // Offline fixture: the real error page remains reviewable.
        QTRY_VERIFY(activePage(window->contentItem(), "shareResolvePage"));
        QTRY_VERIFY(!loadingItems(window->contentItem()));
        QVERIFY(!window->grabWindow().isNull());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QChar(10))));
        shares.shutdown();
        history.shutdown();
        metadata.shutdown();
        router.shutdown();
        shell.shutdown();
        windowController.shutdown();
        playback.shutdown();
        api.shutdown();
        root.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};
QTEST_MAIN(SharingUiTest)
#include "SharingUiTest.moc"
