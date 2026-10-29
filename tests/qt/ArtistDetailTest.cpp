#include "ArtistDetailService.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include <QAbstractItemModelTester>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include "SettingsStore.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class ArtistDetailContextTestAccess
{
public:
    static void pending(ArtistDetailContext &c, quint64 id, quint32 page)
    {
        c.m_requests.insert(id, {page, c.m_api.sessionRevision(), c.m_generation});
    }
    static void finishPending(ArtistDetailContext &c, quint64 id)
    {
        c.m_requests.remove(id);
        emit c.changed();
    }
    static void accept(ArtistDetailContext &c, quint64 id, const QJsonObject &value)
    {
        c.accept(id, QJsonDocument(value).toJson());
    }
    static void seed(ArtistDetailContext &c, const QString &name,
                     const QString &description = QStringLiteral("歌手介绍 Description"))
    {
        c.m_detail = {{"id", 3520},
                      {"name", name},
                      {"avatar_url", ""},
                      {"description", description},
                      {"birthday", "1979-01-18"},
                      {"fans", 25801816},
                      {"album_count", 49},
                      {"mv_count", 2826},
                      {"published", true}};
        c.m_actions.setQueueContext(name, "queue:artist:" + c.m_route.resourceId());
        emit c.changed();
    }
    static void setSize(ArtistDetailContext &c, quint32 size)
    {
        c.m_pageSize = size;
    }
    static void setAvatar(ArtistDetailContext &c, const QString &url)
    {
        c.m_detail["avatar_url"] = url;
        emit c.changed();
    }
    static void albumPending(ArtistDetailContext &c, quint64 id, quint32 page)
    {
        c.m_albumRequests.insert(id, {page, c.m_api.sessionRevision(), c.m_generation});
    }
    static void acceptAlbum(ArtistDetailContext &c, quint64 id, const QJsonObject &value)
    {
        c.accept(id, QJsonDocument(value).toJson());
    }
    static void setAlbumSize(ArtistDetailContext &c, quint32 size)
    {
        c.m_albumPageSize = size;
    }
    static void fail(ArtistDetailContext &c, quint64 id)
    {
        c.fail(id, QByteArrayLiteral("{\"message\":\"sample artist failure\"}"));
    }
};
} // namespace EchoMusic
namespace
{
QJsonObject artistSong(int id)
{
    return {{"id", QString::number(id)},
            {"hash", QString("%1").arg(id, 32, 16, QChar('0'))},
            {"title", "中文 Song " + QString::number(id)},
            {"artists", QJsonArray{QJsonObject{{"id", 3520}, {"name", "周杰伦"}}}},
            {"album_id", "960399"},
            {"album_title", "专辑 Album"},
            {"duration_ms", 180000},
            {"cover_url", "https://example.test/cover.jpg"},
            {"privilege", 10}};
}
QJsonObject domainPage(int number, std::initializer_list<int> ids)
{
    QJsonArray items;
    for (int id : ids)
        items.append(artistSong(id));
    return {{"page", number}, {"page_size", 2}, {"total", 7}, {"items", items}};
}
QJsonObject albumItem(qint64 id, const QString &title, const QString &artist, const QString &cover,
                      const QString &published)
{
    QJsonArray artists;
    if (!artist.isEmpty())
        artists.append(QJsonObject{{"id", 3520}, {"name", artist}});
    return {{"id", id},
            {"title", title},
            {"artists", artists},
            {"cover_url", cover},
            {"published_at", published}};
}
QJsonObject albumPage(int page, int total, const QJsonArray &items, int size = 2)
{
    return {{"page", page}, {"page_size", size}, {"total", total}, {"items", items}};
}
QQuickItem *artistVisual(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = artistVisual(child, name))
            return found;
    return nullptr;
}
int artistVisualCount(QQuickItem *item, const QString &name)
{
    int count = item->objectName() == name;
    for (auto *child : item->childItems())
        count += artistVisualCount(child, name);
    return count;
}
QObject *artistObject(QObject *object, const QString &name)
{
    if (object->objectName() == name)
        return object;
    for (auto *child : object->children())
        if (auto *found = artistObject(child, name))
            return found;
    return nullptr;
}
} // namespace
class ArtistDetailTest : public QObject
{
    Q_OBJECT
private slots:
    void sortPreferencesAreSharedAcrossArtistsAndCachedRoutes()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ShellSettings shell(prefs);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ArtistDetailService service(api, playback, router, shell, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("artist-detail", {}, "3520"));
        auto *first = service.contextFor(router.current());
        QCOMPARE(first->sort(), QString("new"));
        QCOMPARE(first->albumSort(), QString("new"));
        ArtistDetailContextTestAccess::seed(*first, "First");
        ArtistDetailContextTestAccess::pending(*first, 1001, 1);
        ArtistDetailContextTestAccess::accept(*first, 1001, domainPage(1, {1}));
        first->setSort("hot");
        ArtistDetailContextTestAccess::pending(*first, 1002, 1);
        ArtistDetailContextTestAccess::accept(*first, 1002, domainPage(1, {2}));
        QCOMPARE(first->songs()->count(), 1);
        first->ensureAlbums();
        ArtistDetailContextTestAccess::albumPending(*first, 1200, 1);
        ArtistDetailContextTestAccess::acceptAlbum(
            *first, 1200, albumPage(1, 1, {albumItem(960399, "Album", "First", "", "2026")}));
        QCOMPARE(first->albums()->count(), 1);
        QVERIFY(router.navigate("artist-detail", {}, "6743"));
        auto *second = service.contextFor(router.current());
        QVERIFY(second != first);
        QCOMPARE(second->sort(), QString("hot"));
        QCOMPARE(second->albumSort(), QString("new"));
        second->setSort("new");
        second->setAlbumSort("hot");
        router.back();
        QCOMPARE(service.contextFor(router.current()), first);
        QCOMPARE(first->sort(), QString("new"));
        QCOMPARE(first->albumSort(), QString("hot"));
        QCOMPARE(first->songs()->count(), 0); // The old order cannot survive the label change.
        QCOMPARE(first->albums()->count(), 0);
        QVERIFY(!first->albumsLoaded());
        first->setSort("invalid");
        first->setAlbumSort("invalid");
        QCOMPARE(shell.artistSongSort(), QString("new"));
        QCOMPARE(shell.artistAlbumSort(), QString("hot"));
        QCOMPARE(prefs.value("artists/songSort").toString(), QString("new"));
        QCOMPARE(prefs.value("artists/albumSort").toString(), QString("hot"));
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void preferencesSurviveProcessRestart()
    {
        QTemporaryDir dir;
        for (const auto &stage :
             {"read-new-new", "write-hot-new", "read-hot-new", "write-new-hot", "read-new-hot",
              "write-hot-hot", "read-hot-hot", "write-new-new", "read-new-new"}) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("ECHO_ARTIST_PREFS_PROBE", dir.filePath("prefs.ini"));
            environment.insert("ECHO_ARTIST_PREFS_STAGE", stage);
            environment.insert("QT_QPA_PLATFORM", "offscreen");
            child.setProcessEnvironment(environment);
            child.setProcessChannelMode(QProcess::MergedChannels);
            child.start(QCoreApplication::applicationFilePath(), {"preferencesRestartWorker"});
            QVERIFY(child.waitForFinished(20000));
            const auto output = child.readAll();
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                     output.constData());
        }
    }
    void preferencesRestartWorker()
    {
        const auto path = qEnvironmentVariable("ECHO_ARTIST_PREFS_PROBE");
        if (path.isEmpty())
            QSKIP("Invoked only by the isolated process-restart test");
        const auto stage = qEnvironmentVariable("ECHO_ARTIST_PREFS_STAGE").split('-');
        QCOMPARE(stage.size(), 3);
        EchoMusic::SettingsStore prefs(path);
        ShellSettings shell(prefs);
        ApiClient api(DeviceIdentity::temporary());
        QTemporaryDir dir;
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ArtistDetailService service(api, playback, router, shell, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("artist-detail", {}, "3520"));
        auto *context = service.contextFor(router.current());
        if (stage[0] == "write") {
            context->setSort(stage[1]);
            context->setAlbumSort(stage[2]);
        }
        QCOMPARE(context->sort(), stage[1]);
        QCOMPARE(context->albumSort(), stage[2]);
        QCOMPARE(shell.artistSongSort(), stage[1]);
        QCOMPARE(shell.artistAlbumSort(), stage[2]);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
        // Normal EchoMusic::SettingsStore destruction flushes the write. The next assertion
        // runs in another process, with no shared EchoMusic::SettingsStore in-memory cache.
    }
    void pagedSongsAndProviderSort()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ShellSettings shell(prefs);
        ArtistDetailService service(api, playback, router, shell, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("artist-detail", {}, "3520"));
        auto *context = service.contextFor(router.current());
        QVERIFY(context);
        QAbstractItemModelTester check(context->songs(),
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        ArtistDetailContextTestAccess::seed(*context, "周杰伦");
        ArtistDetailContextTestAccess::setSize(*context, 2);
        ArtistDetailContextTestAccess::pending(*context, 1001, 1);
        ArtistDetailContextTestAccess::pending(*context, 1002, 2);
        ArtistDetailContextTestAccess::pending(*context, 1003, 3);
        ArtistDetailContextTestAccess::accept(*context, 1002, domainPage(2, {3, 4}));
        QCOMPARE(context->songs()->count(), 0);
        ArtistDetailContextTestAccess::accept(*context, 1003, domainPage(3, {5}));
        QCOMPARE(context->songs()->count(), 0);
        ArtistDetailContextTestAccess::accept(*context, 1001, domainPage(1, {1, 2}));
        QCOMPARE(context->songs()->count(), 5);
        QVERIFY(context->complete());
        QCOMPARE(context->errorMessage(), QString());
        // Switching between new and hot provider order resets the table.
        ArtistDetailContextTestAccess::pending(*context, 1050, 0);
        ArtistDetailContextTestAccess::albumPending(*context, 1051, 1);
        context->setSort("hot");
        QCOMPARE(context->sort(), QString("hot"));
        QCOMPARE(context->songs()->count(), 0);
        QVERIFY(!context->complete());
        QVERIFY(context->detailLoading());
        QVERIFY(context->albumsLoading());
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 1051, albumPage(1, 1, {albumItem(960399, "Album", "", "", "2026")}));
        QCOMPARE(context->albums()->count(), 1);
        ArtistDetailContextTestAccess::finishPending(*context, 1050);
        ArtistDetailContextTestAccess::pending(*context, 1101, 1);
        ArtistDetailContextTestAccess::accept(*context, 1101, domainPage(1, {9}));
        QCOMPARE(context->songs()->count(), 1);
        QCOMPARE(context->songs()->item(0)["resourceId"].toString(), QString("9"));
        context->setSort("invalid");
        QCOMPARE(context->sort(), QString("hot"));
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void shortPageDiscardsInFlightSiblings()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ShellSettings shell(prefs);
        ArtistDetailService service(api, playback, router, shell, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("artist-detail", {}, "3520"));
        auto *context = service.contextFor(router.current());
        ArtistDetailContextTestAccess::seed(*context, "周杰伦");
        ArtistDetailContextTestAccess::setSize(*context, 2);
        ArtistDetailContextTestAccess::pending(*context, 1201, 1);
        ArtistDetailContextTestAccess::pending(*context, 1202, 2);
        ArtistDetailContextTestAccess::pending(*context, 1203, 3);
        ArtistDetailContextTestAccess::accept(*context, 1201, domainPage(1, {1}));
        QCOMPARE(context->songs()->count(), 0);
        ArtistDetailContextTestAccess::accept(*context, 1202, domainPage(2, {2, 3}));
        ArtistDetailContextTestAccess::accept(*context, 1203, domainPage(3, {4, 5}));
        // The short first page establishes EOF; the other settled pages of the
        // same batch are discarded instead of being appended past the end.
        QCOMPARE(context->songs()->count(), 1);
        QVERIFY(context->complete());
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void failureAndSessionChange()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ShellSettings shell(prefs);
        ArtistDetailService service(api, playback, router, shell, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("artist-detail", {}, "3520"));
        auto *context = service.contextFor(router.current());
        // A failed detail is the page error while no album metadata exists.
        ArtistDetailContextTestAccess::pending(*context, 2001, 0);
        ArtistDetailContextTestAccess::fail(*context, 2001);
        QCOMPARE(context->errorMessage(), QString("sample artist failure"));
        QVERIFY(context->detail().isEmpty());
        // A failed song page keeps the detail and reports the song error.
        ArtistDetailContextTestAccess::seed(*context, "周杰伦");
        ArtistDetailContextTestAccess::pending(*context, 2002, 1);
        ArtistDetailContextTestAccess::fail(*context, 2002);
        QCOMPARE(context->songs()->count(), 0);
        QCOMPARE(context->errorMessage(), QString("sample artist failure"));
        ArtistDetailContextTestAccess::pending(*context, 3001, 1);
        api.advanceSession();
        QCOMPARE(context->songs()->count(), 0);
        QVERIFY(context->detail().isEmpty());
        ArtistDetailContextTestAccess::accept(*context, 3001, domainPage(1, {1}));
        QCOMPARE(context->songs()->count(), 0);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void albumPagingSortAndEof()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        ShellSettings shell(prefs);
        ArtistDetailService service(api, playback, router, shell, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("artist-detail", {}, "3520"));
        auto *context = service.contextFor(router.current());
        ArtistDetailContextTestAccess::seed(*context, "周杰伦");
        ArtistDetailContextTestAccess::setAlbumSize(*context, 2);
        context->ensureAlbums();
        QVERIFY(!context->albumsLoading());
        ArtistDetailContextTestAccess::albumPending(*context, 5001, 1);
        ArtistDetailContextTestAccess::albumPending(*context, 5002, 2);
        ArtistDetailContextTestAccess::albumPending(*context, 5003, 3);
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 5001,
            albumPage(
                1, 3,
                {albumItem(960399, "魔杰座", "周杰伦", "https://example.test/a.jpg", "2008-10-15"),
                 albumItem(554150, "不能说的秘密", "", "https://example.test/b.jpg",
                           "2007-08-13")}));
        QCOMPARE(context->albums()->count(), 0);
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 5002,
            albumPage(2, 3,
                      {albumItem(700001, "叶惠美", "周杰伦", "https://example.test/c.jpg",
                                 "2003-07-31")}));
        QCOMPARE(context->albums()->count(), 0);
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 5003, albumPage(3, 3, {albumItem(700002, "Later page", "", "", "")}));
        QCOMPARE(context->albums()->count(), 3);
        QVERIFY(context->albumsLoaded());
        const auto item = context->albums()->item(0);
        QCOMPARE(item["resourceId"].toString(), QString("960399"));
        QCOMPARE(item["title"].toString(), QString("魔杰座"));
        QVERIFY(item["extraText"].toString().contains("周杰伦"));
        QVERIFY(item["extraText"].toString().contains("2008-10-15"));
        // Sorting resets the accumulated cards and the provider order.
        context->setAlbumSort("hot");
        QCOMPARE(context->albumSort(), QString("hot"));
        QCOMPARE(context->albums()->count(), 0);
        QVERIFY(!context->albumsLoaded());
        ArtistDetailContextTestAccess::albumPending(*context, 5101, 1);
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 5101, albumPage(1, 1, {albumItem(1, "New order", "", "", "")}));
        QCOMPARE(context->albums()->count(), 1);
        QVERIFY(context->albumsLoaded());
        // A late page after EOF is ignored.
        ArtistDetailContextTestAccess::albumPending(*context, 5102, 2);
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 5102, albumPage(2, 1, {albumItem(2, "Late", "", "", "")}));
        QCOMPARE(context->albums()->count(), 1);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void nativeArtistPage()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        ShellSettings shell(prefs);
        ArtistDetailService service(api, playback, router, shell, false);
        service.registerRoutes(router);
        ThemeStore theme(prefs);
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
import EchoMusic.Artists
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1150; height: 800
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  width: parent.width / window.settings.uiScale
  height: parent.height / window.settings.uiScale
  scale: window.settings.uiScale
  transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/artist-detail-runtime.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowActive(window));
        QVERIFY(router.navigate("artist-detail", {}, "3520"));
        auto *context = service.contextFor(router.current());
        QVERIFY(context);
        QTRY_VERIFY(artistVisual(window->contentItem(), "artistDetailPage"));
        auto *pageItem = artistVisual(window->contentItem(), "artistDetailPage");
        QTRY_VERIFY(artistObject(pageItem, "artistIntroDialogOwner"));
        auto *introOwner = artistObject(pageItem, "artistIntroDialogOwner");
        QVERIFY(!introOwner->property("item").value<QObject *>());
        ArtistDetailContextTestAccess::pending(*context, 4001, 0);
        emit context->changed();
        QTRY_VERIFY(artistVisual(window->contentItem(), "songDetailSkeleton"));
        auto *initialCover = artistVisual(window->contentItem(), "songDetailInitialCoverSkeleton");
        auto *headerCover = artistVisual(window->contentItem(), "songDetailCoverSkeleton");
        auto *initialPage = artistVisual(window->contentItem(), "songDetailSkeleton");
        auto *detailViewport = artistVisual(window->contentItem(), "artistDetailViewport");
        QVERIFY(initialCover && headerCover && initialPage && detailViewport);
        QVERIFY(!detailViewport->isVisible());
        const auto skeletonArtifacts = qEnvironmentVariable("ECHO_ARTIST_ARTIFACTS");
        if (!skeletonArtifacts.isEmpty())
            QVERIFY(QDir().mkpath(skeletonArtifacts));
        for (bool dark : {false, true})
            for (double scale : {1., 1.25}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                for (auto *cover : {initialCover, headerCover}) {
                    initialPage->setVisible(cover == initialCover);
                    detailViewport->setVisible(cover == headerCover);
                    QTRY_COMPARE(cover->property("radius").toReal(), cover->width() / 2);
                    cover->setProperty("shimmer", false);
                    QTest::qWait(100);
                    auto frame = window->grabWindow();
                    QVERIFY(!frame.isNull());
                    const auto origin = cover->mapToScene(QPointF());
                    const auto size =
                        cover->mapToScene(QPointF(cover->width(), cover->height())) - origin;
                    const auto dpr = frame.width() / qreal(window->width());
                    const auto colorAt = [&](QPointF point) {
                        return frame.pixelColor((point * dpr).toPoint());
                    };
                    const auto outside = colorAt(origin + QPointF(-4, size.y() / 2));
                    const auto corner = colorAt(origin + QPointF(size.x() * .02, size.y() * .02));
                    const auto center = colorAt(origin + size / 2);
                    const auto distance = [](QColor a, QColor b) {
                        return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) +
                               qAbs(a.blue() - b.blue());
                    };
                    QVERIFY2(distance(corner, outside) < 20,
                             "circular skeleton corner must expose background");
                    QVERIFY(distance(center, outside) > 2);
                    if (!skeletonArtifacts.isEmpty())
                        QVERIFY(frame.save(skeletonArtifacts +
                                           QString("/skeleton-%1-%2-%3.png")
                                               .arg(cover == initialCover ? "initial" : "header")
                                               .arg(dark ? "dark" : "light")
                                               .arg(scale)));
                }
            }
        shell.resetZoom();
        theme.setMode(ThemeStore::Light);
        detailViewport->setVisible(true);
        ArtistDetailContextTestAccess::finishPending(*context, 4001);
        QTRY_VERIFY(!artistVisual(window->contentItem(), "songDetailSkeleton"));
        ArtistDetailContextTestAccess::seed(
            *context, "中文与 Latin 歌手长名称",
            QStringLiteral("中文与 Latin 的完整歌手简介，用于检查截断和查看详情入口。 ")
                .repeated(10));
        QImage avatar(400, 400, QImage::Format_RGB32);
        avatar.fill(QColor("#4b5c6b"));
        QVERIFY(avatar.save(dir.filePath("artist-avatar.png")));
        const auto avatarUrl = QUrl::fromLocalFile(dir.filePath("artist-avatar.png")).toString();
        ArtistDetailContextTestAccess::setAvatar(*context, avatarUrl);
        QJsonArray songs;
        for (int i = 1; i <= 200; ++i) {
            auto song = artistSong(i);
            song["cover_url"] = avatarUrl;
            songs.append(song);
        }
        context->songs()->appendSongs(songs);
        QTRY_VERIFY(artistVisual(window->contentItem(), "songDelegate"));
        auto *header = artistVisual(window->contentItem(), "songDetailHeader");
        QVERIFY(header);
        auto *counts = artistVisual(header, "artistCountsLine");
        auto *fans = artistVisual(header, "artistFansLine");
        auto *typeTag = artistVisual(header, "songDetailTypeTag");
        auto *avatarCover = artistVisual(header, "artistDetailCover");
        QVERIFY(counts && fans && typeTag && avatarCover);
        QCOMPARE(typeTag->property("text").toString(), QString("ARTIST"));
        QVERIFY(counts->property("text").toString().contains("200 歌曲"));
        QVERIFY(counts->property("text").toString().contains("49 专辑"));
        QVERIFY(counts->property("text").toString().contains("2826 MV"));
        QVERIFY(fans->property("text").toString().contains("2580.2万"));
        QVERIFY(fans->property("text").toString().contains("1979-01-18"));
        QCOMPARE(avatarCover->property("circular").toBool(), true);
        QTRY_COMPARE(avatarCover->property("source").toUrl(), QUrl(avatarUrl));
        QTRY_COMPARE(avatarCover->property("status").toInt(), 1);
        auto *sort = artistVisual(window->contentItem(), "artistSongSort");
        QVERIFY(sort);
        QCOMPARE(sort->property("currentIndex").toInt(), 0);
        QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, 1));
        QTRY_COMPARE(context->sort(), QString("hot"));
        QCOMPARE(context->songs()->count(), 0);
        // The real route's filter and collapse behavior is shared with songs.
        context->songs()->appendSongs(songs);
        context->songs()->setQuery("Song 20");
        QTRY_VERIFY(context->songs()->count() > 0 && context->songs()->count() < 200);
        context->songs()->setQuery(QString());
        auto *list = artistVisual(window->contentItem(), "discoverySongs");
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(header->property("progress").toReal(), 0., 3000);
        list->setProperty("contentY", 500);
        QTRY_COMPARE(header->property("progress").toReal(), 1.);
        list->setProperty("contentY", -list->property("topMargin").toReal());
        QTRY_COMPARE(header->property("progress").toReal(), 0.);
        QVERIFY(QMetaObject::invokeMethod(introOwner, "open", Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(introOwner->property("item").value<QObject *>());
        auto *dialog = introOwner->property("item").value<QObject *>();
        QCOMPARE(dialog->property("heading").toString(), QString("歌手介绍"));
        QVERIFY(QMetaObject::invokeMethod(introOwner, "close"));
        QTRY_VERIFY(!introOwner->property("item").value<QObject *>());
        // Load the albums tab on its first activation.
        auto *albumsTab = artistVisual(window->contentItem(), "artistTabAlbums");
        QVERIFY(albumsTab);
        const auto tabCenter =
            albumsTab->mapToScene(QPointF(albumsTab->width() / 2, albumsTab->height() / 2))
                .toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, tabCenter);
        QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("albums"));
        QTRY_VERIFY(artistVisual(window->contentItem(), "artistAlbumGrid"));
        auto *albumGrid = artistVisual(window->contentItem(), "artistAlbumGrid");
        QVERIFY(albumGrid->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(albumGrid->property("opacity").toDouble(), 1., 2000);
        auto *tabIndicator = artistVisual(window->contentItem(), "detailTabIndicator");
        QVERIFY(tabIndicator);
        auto *tabRow = artistVisual(window->contentItem(), "artistTabToolbarRow");
        auto *divider = artistVisual(window->contentItem(), "artistTabDivider");
        QVERIFY(tabRow && divider);
        QTRY_COMPARE(tabRow->height(), 56.);
        QCOMPARE(divider->mapToScene(QPointF()).y(), tabRow->mapToScene(QPointF(0, 55)).y());
        QTRY_COMPARE_WITH_TIMEOUT(tabIndicator->width(), albumsTab->width(), 2000);
        QVERIFY(tabIndicator->width() > 20);
        QVERIFY(tabIndicator->mapToScene(QPointF(0, tabIndicator->height())).y() <
                divider->mapToScene(QPointF()).y());
        QTRY_COMPARE_WITH_TIMEOUT(tabIndicator->property("x").toDouble(), albumsTab->x(), 2000);
        QVERIFY(!artistVisual(window->contentItem(), "discoverySongs")->isVisible());
        QVERIFY(!artistVisual(header, "catalogPlayAll")->isVisible());
        ArtistDetailContextTestAccess::albumPending(*context, 6001, 1);
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 6001,
            albumPage(1, 2,
                      {albumItem(960399, "魔杰座", "周杰伦", avatarUrl, "2008-10-15"),
                       albumItem(554150, "不能说的秘密", "周杰伦", avatarUrl, "2007-08-13")},
                      30));
        QTRY_COMPARE_WITH_TIMEOUT(artistVisualCount(albumGrid, "contentDelegate"), 2, 3000);
        auto *albumSort = artistVisual(window->contentItem(), "artistAlbumSort");
        auto *albumFilter = artistVisual(window->contentItem(), "artistAlbumFilter");
        QVERIFY(albumSort && albumFilter && albumSort->isVisible() && albumFilter->isVisible());
        QVERIFY(!artistVisual(window->contentItem(), "artistSongSort")->isVisible());
        context->albums()->setQuery("魔杰");
        QTRY_COMPARE(context->albums()->count(), 1);
        context->albums()->setQuery(QString());
        QTRY_COMPARE(context->albums()->count(), 2);
        QCOMPARE(albumSort->property("currentIndex").toInt(), 1); // Latest is the default.
        QMetaObject::invokeMethod(albumSort, "activated", Q_ARG(int, 0));
        QTRY_COMPARE(context->albumSort(), QString("hot"));
        QTRY_COMPARE(context->albums()->count(), 0);
        ArtistDetailContextTestAccess::albumPending(*context, 6002, 1);
        ArtistDetailContextTestAccess::acceptAlbum(
            *context, 6002,
            albumPage(1, 1, {albumItem(1, "新专辑", "周杰伦", avatarUrl, "2026")}, 30));
        QTRY_COMPARE(context->albums()->count(), 1);
        const auto albumsArtifacts = qEnvironmentVariable("ECHO_ARTIST_ALBUM_ARTIFACTS");
        if (!albumsArtifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(albumsArtifacts));
            for (bool dark : {false, true})
                for (double scale : {1., 1.25}) {
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    shell.resetZoom();
                    if (scale > 1.)
                        for (int i = 0; i < 5; ++i)
                            shell.zoomIn();
                    QTest::qWait(200);
                    QVERIFY(window->grabWindow().save(albumsArtifacts +
                                                      QString("/artist-albums-%1-%2.png")
                                                          .arg(dark ? "dark" : "light")
                                                          .arg(scale)));
                }
        }
        // Switch back: the song table and playback actions return. Restore the
        // base zoom first so the click uses the same logical geometry as the
        // earlier tab switch (the screenshot loop leaves 125% active).
        theme.setMode(ThemeStore::Light);
        shell.resetZoom();
        QTest::qWait(150);
        auto *songsTab = artistVisual(window->contentItem(), "artistTabSongs");
        QVERIFY(songsTab);
        const auto songsCenter =
            songsTab->mapToScene(QPointF(songsTab->width() / 2, songsTab->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, songsCenter);
        QTRY_COMPARE(router.current()->query().value("tab").toString(), QString("songs"));
        QTRY_VERIFY(artistVisual(window->contentItem(), "discoverySongs")->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(
            artistVisual(window->contentItem(), "catalogSongList")->property("opacity").toDouble(),
            1., 2000);
        QTRY_VERIFY(artistVisual(header, "catalogPlayAll")->isVisible());
        const auto artifacts = qEnvironmentVariable("ECHO_ARTIST_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        for (bool dark : {false, true})
            for (double scale : {1., 1.25}) {
                theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                shell.resetZoom();
                if (scale > 1.)
                    for (int i = 0; i < 5; ++i)
                        shell.zoomIn();
                QTest::qWait(200);
                const auto frame = window->grabWindow();
                QVERIFY(!frame.isNull());
                if (!artifacts.isEmpty())
                    QVERIFY(frame.save(
                        artifacts +
                        QString("/artist-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
            }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(ArtistDetailTest)
#include "ArtistDetailTest.moc"
