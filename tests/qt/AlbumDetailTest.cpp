#include "AlbumDetailService.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include <QAbstractItemModelTester>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class AlbumDetailContextTestAccess
{
public:
    static void pending(AlbumDetailContext &c, quint64 id, quint32 page)
    {
        c.m_requests.insert(id, {page, c.m_api.sessionRevision(), c.m_generation});
    }
    static void finishPending(AlbumDetailContext &c, quint64 id)
    {
        c.m_requests.remove(id);
        emit c.changed();
    }
    static void accept(AlbumDetailContext &c, quint64 id, const QJsonObject &value)
    {
        c.accept(id, QJsonDocument(value).toJson());
    }
    static void seed(AlbumDetailContext &c, const QString &title,
                     const QString &description = QStringLiteral("专辑介绍 Description"))
    {
        c.m_detail = {{"id", 960399},
                      {"title", title},
                      {"artists", QJsonArray{QJsonObject{{"id", 3520}, {"name", "周杰伦"}},
                                             QJsonObject{{"id", 3521}, {"name", "方文山"}}}},
                      {"cover_url", "https://example.test/album.jpg"},
                      {"published_at", "2008-10-15"},
                      {"description", description},
                      {"language", "国语"},
                      {"category", "录音室专辑"}};
        c.m_actions.setQueueContext(title, "queue:album:" + c.m_route.resourceId());
        emit c.changed();
    }
    static void setSize(AlbumDetailContext &c, quint32 size)
    {
        c.m_pageSize = size;
    }
    static void setCover(AlbumDetailContext &c, const QString &url)
    {
        c.m_detail["cover_url"] = url;
        emit c.changed();
    }
    static void fail(AlbumDetailContext &c, quint64 id)
    {
        c.fail(id, QByteArrayLiteral("{\"message\":\"sample album failure\"}"));
    }
    static void markComplete(AlbumDetailContext &c)
    {
        c.m_complete = true;
        emit c.changed();
    }
};
} // namespace EchoMusic
namespace
{
QJsonObject albumSong(int id)
{
    return {{"id", QString::number(id)},
            {"hash", QString("%1").arg(id, 32, 16, QChar('0'))},
            {"title", "中文 Song " + QString::number(id)},
            {"artists", QJsonArray{QJsonObject{{"id", 1}, {"name", "歌手 Artist"}}}},
            {"album_id", "960399"},
            {"album_title", "专辑 Album"},
            {"duration_ms", 180000},
            {"cover_url", "https://example.test/cover.jpg"},
            {"privilege", 10},
            {"metadata",
             QJsonObject{{"disc", 1},
                         {"position", id},
                         {"has_accompaniment", 0},
                         {"accompaniment_code", 0},
                         {"payment_type", 3},
                         {"resources", QJsonArray{QJsonObject{
                                           {"quality", "flac"},
                                           {"hash", QString("%1").arg(id, 32, 16, QChar('0'))},
                                           {"size_bytes", 2048}}}}}}};
}
QJsonObject domainPage(int number, std::initializer_list<int> ids)
{
    QJsonArray items;
    for (int id : ids)
        items.append(QJsonObject{{"song", albumSong(id)},
                                 {"metadata", QJsonObject{}},
                                 {"disc", 1},
                                 {"position", id},
                                 {"has_accompaniment", 0},
                                 {"accompaniment_code", 0}});
    return {
        {"page", QJsonObject{{"page", number}, {"page_size", 2}, {"total", 7}, {"items", items}}},
        {"disc_count", 2}};
}
QQuickItem *albumVisual(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = albumVisual(child, name))
            return found;
    return nullptr;
}
QObject *albumObject(QObject *object, const QString &name)
{
    if (object->objectName() == name)
        return object;
    for (auto *child : object->children())
        if (auto *found = albumObject(child, name))
            return found;
    return nullptr;
}
int albumVisualCount(QQuickItem *item, const QString &name)
{
    int count = item->objectName() == name;
    for (auto *child : item->childItems())
        count += albumVisualCount(child, name);
    return count;
}
} // namespace
class AlbumDetailTest : public QObject
{
    Q_OBJECT
private slots:
    void pagedCatalogArrivesInSourceOrder()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        AlbumDetailService service(api, playback, router, false);
        QVERIFY(!router.available("album-detail"));
        service.registerRoutes(router);
        QVERIFY(router.available("album-detail"));
        QVERIFY(router.navigate("album-detail", {}, "960399"));
        auto *context = service.contextFor(router.current());
        QVERIFY(context);
        QAbstractItemModelTester check(context->songs(),
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        AlbumDetailContextTestAccess::seed(*context, "魔杰座");
        AlbumDetailContextTestAccess::setSize(*context, 2);
        AlbumDetailContextTestAccess::pending(*context, 1001, 1);
        AlbumDetailContextTestAccess::pending(*context, 1002, 2);
        AlbumDetailContextTestAccess::pending(*context, 1003, 3);
        AlbumDetailContextTestAccess::accept(*context, 1002, domainPage(2, {3, 4}));
        QCOMPARE(context->songs()->count(), 0);
        AlbumDetailContextTestAccess::accept(*context, 1003, domainPage(3, {5}));
        QCOMPARE(context->songs()->count(), 0);
        AlbumDetailContextTestAccess::accept(*context, 1001, domainPage(1, {1, 2}));
        QCOMPARE(context->songs()->count(), 5);
        QVERIFY(context->complete());
        QVERIFY(!context->songsLoading());
        QCOMPARE(context->errorMessage(), QString());
        QCOMPARE(context->songs()->item(0)["resourceId"].toString(), QString("1"));
        QCOMPARE(context->songs()->item(4)["resourceId"].toString(), QString("5"));
        // Disc/position/accompaniment facts stay in the queue/history snapshot.
        const auto track = context->songs()->track("3");
        QVERIFY(track);
        QCOMPARE(track->catalogMetadata.value("disc").toInt(), 1);
        QCOMPARE(track->catalogMetadata.value("position").toInt(), 3);
        QVERIFY(track->catalogMetadata.contains("accompaniment_code"));
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
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        AlbumDetailService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("album-detail", {}, "960399"));
        auto *context = service.contextFor(router.current());
        AlbumDetailContextTestAccess::seed(*context, "魔杰座");
        AlbumDetailContextTestAccess::setSize(*context, 2);
        AlbumDetailContextTestAccess::pending(*context, 2201, 1);
        AlbumDetailContextTestAccess::pending(*context, 2202, 2);
        AlbumDetailContextTestAccess::pending(*context, 2203, 3);
        AlbumDetailContextTestAccess::accept(*context, 2201, domainPage(1, {1, 2}));
        AlbumDetailContextTestAccess::accept(*context, 2203, domainPage(3, {4, 5}));
        AlbumDetailContextTestAccess::accept(*context, 2202, domainPage(2, {3}));
        QCOMPARE(context->songs()->count(), 3);
        QVERIFY(context->complete());
        // A page completing after EOF must not extend the album.
        AlbumDetailContextTestAccess::pending(*context, 2204, 4);
        AlbumDetailContextTestAccess::accept(*context, 2204, domainPage(4, {9}));
        QCOMPARE(context->songs()->count(), 3);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void refreshFailurePreservesCachedCatalog()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        AlbumDetailService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("album-detail", {}, "960399"));
        auto *context = service.contextFor(router.current());
        AlbumDetailContextTestAccess::seed(*context, "魔杰座");
        AlbumDetailContextTestAccess::setSize(*context, 2);
        AlbumDetailContextTestAccess::pending(*context, 2001, 1);
        AlbumDetailContextTestAccess::accept(*context, 2001, domainPage(1, {1, 2}));
        AlbumDetailContextTestAccess::pending(*context, 2101, 2);
        AlbumDetailContextTestAccess::accept(*context, 2101, domainPage(2, {3}));
        QCOMPARE(context->songs()->count(), 3);
        QVERIFY(context->complete());
        // A refresh keeps the playable catalog until every page succeeds.
        context->reload();
        QCOMPARE(context->songs()->count(), 3);
        AlbumDetailContextTestAccess::pending(*context, 2002, 1);
        AlbumDetailContextTestAccess::fail(*context, 2002);
        QCOMPARE(context->songs()->count(), 3);
        QCOMPARE(context->errorMessage(), QString("sample album failure"));
        QVERIFY(context->complete());
        // A successful retry replaces the catalog.
        context->reload();
        AlbumDetailContextTestAccess::pending(*context, 2003, 1);
        AlbumDetailContextTestAccess::accept(*context, 2003, domainPage(1, {7}));
        QCOMPARE(context->songs()->count(), 1);
        QCOMPARE(context->songs()->item(0)["resourceId"].toString(), QString("7"));
        QVERIFY(context->errorMessage().isEmpty());
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void sessionChangeClearsAndRejectsLatePages()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        RouteController router;
        AlbumDetailService service(api, playback, router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("album-detail", {}, "960399"));
        auto *context = service.contextFor(router.current());
        AlbumDetailContextTestAccess::seed(*context, "魔杰座");
        AlbumDetailContextTestAccess::setSize(*context, 2);
        AlbumDetailContextTestAccess::pending(*context, 3001, 1);
        AlbumDetailContextTestAccess::pending(*context, 3002, 2);
        api.advanceSession();
        QCOMPARE(context->songs()->count(), 0);
        QVERIFY(context->detail().isEmpty());
        AlbumDetailContextTestAccess::accept(*context, 3001, domainPage(1, {1, 2}));
        AlbumDetailContextTestAccess::accept(*context, 3002, domainPage(2, {3, 4}));
        QCOMPARE(context->songs()->count(), 0);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void nativeAlbumPage()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        AlbumDetailService service(api, playback, router, false);
        service.registerRoutes(router);
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
import EchoMusic.Albums
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
                          QUrl("qrc:/album-detail-runtime.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowActive(window));
        QVERIFY(router.navigate("album-detail", {}, "960399"));
        auto *context = service.contextFor(router.current());
        QVERIFY(context);
        QTRY_VERIFY(albumVisual(window->contentItem(), "albumDetailPage"));
        auto *pageItem = albumVisual(window->contentItem(), "albumDetailPage");
        // The route creates no popup before a user action.
        QTRY_VERIFY(albumObject(pageItem, "albumIntroDialogOwner"));
        auto *introOwner = albumObject(pageItem, "albumIntroDialogOwner");
        QVERIFY(!introOwner->property("item").value<QObject *>());
        // Publishing a detail request shows the album skeleton, then the real
        // header/songs after the typed response arrives.
        AlbumDetailContextTestAccess::pending(*context, 4001, 0);
        emit context->changed();
        QTRY_VERIFY(albumVisual(window->contentItem(), "songDetailSkeleton"));
        AlbumDetailContextTestAccess::finishPending(*context, 4001);
        QTRY_VERIFY(!albumVisual(window->contentItem(), "songDetailSkeleton"));
        AlbumDetailContextTestAccess::seed(
            *context, "中文与 Latin 专辑长名称",
            QStringLiteral("中文与 Latin 的完整专辑简介，用于检查截断和查看详情入口。 ")
                .repeated(80));
        // A local file keeps the visual verification deterministic and avoids a
        // background HTTP request during the offscreen page test.
        QImage cover(400, 400, QImage::Format_RGB32);
        cover.fill(QColor("#4b5c6b"));
        const auto artworkFile = qEnvironmentVariable("ECHO_ALBUM_COVER_FILE");
        if (!artworkFile.isEmpty()) {
            QVERIFY(cover.load(artworkFile));
        }
        QVERIFY(cover.save(dir.filePath("album-cover.png")));
        const auto coverUrl = QUrl::fromLocalFile(dir.filePath("album-cover.png")).toString();
        AlbumDetailContextTestAccess::setCover(*context, coverUrl);
        QTRY_VERIFY(albumVisual(pageItem, "albumDetailCover"));
        auto *albumCover = albumVisual(pageItem, "albumDetailCover");
        QTRY_COMPARE(albumCover->property("source").toUrl(), QUrl(coverUrl));
        QTRY_COMPARE(albumCover->property("status").toInt(), 1);
        QCOMPARE(albumCover->width(), albumCover->height());
        QJsonArray songs;
        for (int i = 1; i <= 200; ++i) {
            auto song = albumSong(i);
            song["cover_url"] = coverUrl;
            songs.append(song);
        }
        context->songs()->appendSongs(songs);
        AlbumDetailContextTestAccess::markComplete(*context);
        QTRY_VERIFY(albumVisual(window->contentItem(), "albumDetailPage"));
        QTRY_VERIFY(albumVisual(window->contentItem(), "songDelegate"));
        auto *header = albumVisual(window->contentItem(), "songDetailHeader");
        QVERIFY(header);
        auto *title = albumVisual(header, "songDetailTitleRow");
        auto *publish = albumVisual(header, "albumPublishLine");
        auto *category = albumVisual(header, "albumCategoryLine");
        auto *artist = albumVisual(header, "albumArtistLink");
        auto *typeTag = albumVisual(header, "songDetailTypeTag");
        QVERIFY(title && publish && category && artist && typeTag);
        QCOMPARE(typeTag->property("text").toString(), QString("ALBUM"));
        QVERIFY(publish->property("text").toString().contains("2008-10-15"));
        QVERIFY(publish->property("text").toString().contains("200"));
        QCOMPARE(category->property("text").toString(), QString("录音室专辑 • 国语"));
        QCOMPARE(albumVisualCount(header, "albumArtistLink"), 2);
        // Header collapse follows the shared sliver rules.
        auto *list = albumVisual(window->contentItem(), "discoverySongs");
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(header->property("progress").toReal(), 0., 3000);
        list->setProperty("contentY", 500);
        QTRY_COMPARE(header->property("progress").toReal(), 1.);
        list->setProperty("contentY", -list->property("topMargin").toReal());
        QTRY_COMPARE(header->property("progress").toReal(), 0.);
        // Search and sort are owned by the shared catalog model.
        context->songs()->setQuery("Song 20");
        QTRY_VERIFY(context->songs()->count() > 0 && context->songs()->count() < 200);
        context->songs()->setQuery(QString());
        QCOMPARE(context->songs()->count(), 200);
        // Intro dialog is created on the user action and released after close.
        const auto artifacts = qEnvironmentVariable("ECHO_ALBUM_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(QMetaObject::invokeMethod(introOwner, "open", Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(introOwner->property("item").value<QObject *>());
        auto *dialog = introOwner->property("item").value<QObject *>();
        QCOMPARE(dialog->property("heading").toString(), QString("专辑介绍"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *introScroll = albumVisual(window->contentItem(), "textDialogScroll");
        QVERIFY(introScroll);
        QVERIFY(introScroll->property("contentHeight").toReal() > introScroll->height());
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/intro-top.png"));
        QCOMPARE(dialog->property("text").toString(),
                 context->detail().value("description").toString());
        introScroll->forceActiveFocus();
        const auto wheelAt =
            introScroll->mapToScene({introScroll->width() / 2, introScroll->height() / 2});
        QWheelEvent wheel(wheelAt, window->mapToGlobal(wheelAt), {}, {0, -120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(introScroll->property("contentY").toReal() > 0);
        QTest::keyClick(window, Qt::Key_End);
        QTRY_VERIFY(introScroll->property("contentY").toReal() > 0);
        QCOMPARE(introScroll->property("contentY").toReal(),
                 introScroll->property("contentHeight").toReal() - introScroll->height());
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/intro-end.png"));
        QVERIFY(introOwner->metaObject()->invokeMethod(introOwner, "close"));
        QTRY_VERIFY(!introOwner->property("item").value<QObject *>());
        // Render the real header at both themes and zooms for visual review.
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
                        QString("/album-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
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
QTEST_MAIN(AlbumDetailTest)
#include "AlbumDetailTest.moc"
