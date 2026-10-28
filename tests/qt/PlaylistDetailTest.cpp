#include "PlaylistDetailService.h"
#include "ShellSettings.h"
#include "ThemeStore.h"
#include "NetworkPreferences.h"
#include "PlaybackLibraryActions.h"
#include "SidebarPlaylistModel.h"
#include <iostream>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QAbstractItemModelTester>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class PlaybackServiceTestAccess
{
public:
    // Canonical current-song presentation without audio/network authorization.
    static void present(PlaybackService &playback, const PlaybackTrack &track)
    {
        playback.m_track = track;
        emit playback.changed();
    }
};
class UserPlaylistModelTestAccess
{
public:
    static void setKind(UserPlaylistModel &model, const QString &kind)
    {
        auto &row = model.m_rows[0];
        row.kind = kind;
        row.globalId = kind == "owned" ? "cover-playlist" : "local-collection";
        row.creatorGlobalId = "cover-playlist";
        emit model.changed();
    }
    static void accept(UserPlaylistModel &model, ApiClient &api, const QJsonObject &payload)
    {
        model.m_request = 123;
        model.m_sessionRevision = api.sessionRevision();
        model.succeed(123, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }
    static void changeOwner(UserPlaylistModel &model, quint64 creator, int source,
                            quint64 listId = 7)
    {
        model.m_rows[0].creatorId = creator;
        model.m_rows[0].sourceCode = source;
        model.m_rows[0].cloudId = {listId};
        emit model.changed();
    }
    static void seed(UserPlaylistModel &model, ApiClient &api, bool custom = false)
    {
        model.beginResetModel();
        model.m_rows.clear();
        UserPlaylistModel::Row row;
        row.cloudId = {7};
        row.globalId = "cover-playlist";
        row.title = "Cover playlist";
        row.cover = custom ? "https://example.com/manual.jpg" : "https://example.com/server.jpg";
        row.sourceCode = 1;
        row.revision = 9;
        row.songCount = 3;
        row.creatorId = api.currentAccountUserId();
        row.customCover = custom;
        model.m_rows.append(row);
        model.m_sessionRevision = api.sessionRevision();
        model.m_coverAccount = api.currentAccountUserId();
        model.endResetModel();
    }
};
class SongMetadataServiceTestAccess
{
public:
    static void flight(SongMetadataService &service, quint64 id, const QStringList &ids)
    {
        service.m_flights.insert(id, ids);
    }
    static void accept(SongMetadataService &service, quint64 id, const QJsonArray &values)
    {
        service.accept(id, QJsonDocument(values).toJson());
    }
};
class PlaylistDetailContextTestAccess
{
public:
    static void pending(PlaylistDetailContext &c, quint64 id, quint32 page)
    {
        c.m_requests.insert(id, {page, c.m_api.sessionRevision(), c.m_generation});
    }
    static void finishPending(PlaylistDetailContext &c, quint64 id)
    {
        c.m_requests.remove(id);
        emit c.changed();
    }
    static void accept(PlaylistDetailContext &c, quint64 id, const QJsonValue &value)
    {
        c.accept(
            id, (value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject()))
                    .toJson());
    }
    static void seed(PlaylistDetailContext &c, const QString &title,
                     const QString &description = QStringLiteral("歌单介绍 Description"))
    {
        c.m_detail = {{"global_id", c.m_route.resourceId()},
                      {"title", title},
                      {"creator_id", 100},
                      {"creator_name", "Donyean 中文"},
                      {"published_at", "2021-04-05"},
                      {"description", description},
                      {"tags", "韩语,R&B,安静"}};
        c.m_actions.setQueueContext(title, "queue:playlist:" + c.m_route.resourceId());
        emit c.changed();
    }
    static void setSize(PlaylistDetailContext &c, quint32 size)
    {
        c.m_pageSize = size;
    }
    static void fail(PlaylistDetailContext &c, quint64 id)
    {
        c.fail(id, QByteArrayLiteral("{\"message\":\"sample page failure\"}"));
    }
    static void markClosed(PlaylistDetailContext &c)
    {
        c.m_complete = true;
        c.m_total = c.songs()->totalCount();
        emit c.changed();
    }
    static void cloud(PlaylistDetailContext &c)
    {
        c.m_cloud = ApiClient::CloudPlaylistId{7};
        c.m_pageSize = 2;
    }
    static void resumeCachedPresentation(PlaylistDetailContext &c, const QJsonArray &values)
    {
        // Hide uses cancel(); cached public facts can arrive from another
        // consumer while this route is dormant. Resume without fetching tracks.
        c.cancel();
        SongMetadataServiceTestAccess::flight(c.m_metadata, 999003,
                                              c.m_songs.incompletePresentationIds());
        SongMetadataServiceTestAccess::accept(c.m_metadata, 999003, values);
        c.m_network = true;
        c.activate();
        c.m_network = false;
    }
    static bool completing(const PlaylistDetailContext &c)
    {
        return !c.m_metadataRequests.isEmpty();
    }
};
class ApiClientTestAccess
{
public:
    static QJsonObject requestForOperation(const ApiClient &api, const QString &operation)
    {
        for (const auto &pending : api.m_pending)
            if (pending.request.value("operation").toString() == operation)
                return pending.request;
        return {};
    }
};
} // namespace EchoMusic
QJsonObject playlistSong(int id)
{
    return {{"song",
             QJsonObject{{"id", QString::number(id)},
                         {"hash", QString("%1").arg(id, 32, 16, QChar('0'))},
                         {"title", "中文 Song " + QString::number(id)},
                         {"artists", QJsonArray{QJsonObject{{"id", 1}, {"name", "歌手 Artist"}}}},
                         {"album_id", "960399"},
                         {"album_title", "专辑 Album"},
                         {"duration_ms", 180000},
                         {"privilege", 10}}},
            {"metadata",
             QJsonObject{{"payment_type", 3},
                         {"old_copyright_code", 0},
                         {"resources", QJsonArray{QJsonObject{
                                           {"quality", "flac"},
                                           {"hash", QString("%1").arg(id, 32, 16, QChar('0'))},
                                           {"size_bytes", 2048}}}}}}};
}
QJsonObject domainPage(int number, std::initializer_list<int> ids)
{
    QJsonArray items;
    for (int id : ids)
        items.append(playlistSong(id));
    return {{"page", number}, {"page_size", 2}, {"total", 7}, {"items", items}};
}
QQuickItem *playlistVisual(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = playlistVisual(child, name))
            return found;
    return nullptr;
}
QQuickItem *renderedText(QQuickItem *item)
{
    if (item->property("textFormat").isValid() && item->property("lineHeight").isValid() &&
        item->property("font").canConvert<QFont>() && item->property("text").isValid())
        return item;
    for (auto *child : item->childItems())
        if (auto *found = renderedText(child))
            return found;
    return nullptr;
}
qreal renderedInkCenter(const QImage &image, QQuickWindow *window, QQuickItem *label)
{
    if (!label)
        return -1;
    const qreal ratio = image.width() / qreal(window->width());
    const auto bounds = label->mapRectToScene(QRectF(0, -2, label->width(), label->height() + 4));
    const auto region = QRectF(bounds.topLeft() * ratio, bounds.size() * ratio)
                            .toAlignedRect()
                            .intersected(image.rect());
    const auto foreground = label->property("color").value<QColor>();
    int top = image.height(), bottom = -1;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x) {
            const auto c = image.pixelColor(x, y);
            const int distance = qAbs(c.red() - foreground.red()) +
                                 qAbs(c.green() - foreground.green()) +
                                 qAbs(c.blue() - foreground.blue());
            if (distance < 50) {
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
    return bottom >= top ? (top + bottom + 1) / (2 * ratio) : -1;
}
int playlistVisualCount(QQuickItem *item, const QString &name)
{
    int count = item->objectName() == name;
    for (auto *child : item->childItems())
        count += playlistVisualCount(child, name);
    return count;
}
class PlaylistDetailTest : public QObject
{
    Q_OBJECT
private slots:
    void libraryResponseVariationsDoNotRejectThePage()
    {
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "test-token"));
        UserPlaylistModel library(api);
        QAbstractItemModelTester tester(&library,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        const QJsonObject row{{"cloud_list_id", 7},
                              {"global_id", "collection-7"},
                              {"title", "Known fields"},
                              {"kind", "owned"},
                              {"editable", true},
                              {"song_count", 3},
                              {"revision", 1}};
        auto other = row;
        other["cloud_list_id"] = 8;
        other["global_id"] = "collection-8";
        other["title"] = "Second row";
        other["kind"] = "unknown";
        // Count/page echoes and extensions must not become a second schema gate.
        const QJsonObject payload{{"user_id", 100},
                                  {"total_revision", 4},
                                  {"page", QJsonObject{{"total", 1},
                                                       {"page", 42},
                                                       {"page_size", 999},
                                                       {"items", QJsonArray{row, row, other}}}}};
        UserPlaylistModelTestAccess::accept(library, api, payload);
        QCOMPARE(library.state(), UserPlaylistModel::State::Ready);
        // Repeated identifiers are separate provider rows. The model assigns
        // stable view identities instead of dropping or merging catalog facts.
        QCOMPARE(library.rowCount(), 3);
        QCOMPARE(library.index(0).data(UserPlaylistModel::TitleRole).toString(), "Known fields");
        QCOMPARE(library.index(1).data(UserPlaylistModel::CloudListIdRole).toULongLong(), 7);
        QCOMPARE(library.index(2).data(UserPlaylistModel::CloudListIdRole).toULongLong(), 8);
        QCOMPARE(library.index(2).data(UserPlaylistModel::KindRole).toString(), "unknown");
        QVERIFY(!library.cloudIdAt(2));
        QVERIFY(library.errorMessage().isEmpty());
        api.shutdown();
    }

    void filenameArtistPresentationAcquiresIdentityWithoutChangingSource()
    {
        const QJsonObject rawArtists{{"id", 0}, {"name", ""}};
        QJsonObject song{{"id", "64323384"},
                         {"title", "我的楼兰"},
                         {"hash", "18EFAFDA366BDA9CEDEEC7F520227AB9"},
                         {"artists", QJsonArray{QJsonObject{{"name", "云朵"}}}},
                         {"album_id", "2603117"},
                         {"album_title", "倔强"},
                         {"cover_url", "https://example.test/cover.jpg"},
                         {"metadata", QJsonObject{{"source_file_name", "云朵 - 我的楼兰.mp3"},
                                                  {"filename_artist_name", "云朵"},
                                                  {"artists", QJsonArray{rawArtists}}}}};
        QVERIFY(SongMetadataService::needsCompletion(song));
        SongCatalogModel model;
        model.appendSongs({song});
        auto before = model.track("64323384").value();
        QCOMPARE(before.title, QString("我的楼兰"));
        QCOMPARE(before.artist, QString("云朵"));
        QCOMPARE(before.artistLinks()[0].toMap()["id"].toString(), QString());
        model.toggleSelected("64323384");
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        const QJsonObject presentation{
            {"id", "64323384"},
            {"title", "我的楼兰"},
            {"artists", QJsonArray{QJsonObject{{"id", 6743}, {"name", "云朵"}}}}};
        model.applyPresentations({presentation});
        const auto after = model.track("64323384").value();
        QCOMPARE(after.artistLinks()[0].toMap()["id"].toString(), QString("6743"));
        QCOMPARE(after.title, QString("我的楼兰"));
        QCOMPARE(after.catalogMetadata["artists"].toArray(), QJsonArray{rawArtists});
        QCOMPARE(after.hash, before.hash);
        QCOMPARE(after.catalogMetadata["source_file_name"],
                 before.catalogMetadata["source_file_name"]);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(model.selectedCount(), 1);
        song["artists"] = QJsonArray{rawArtists};
        auto meta = song["metadata"].toObject();
        meta.remove("filename_artist_name");
        song["metadata"] = meta;
        QVERIFY(SongMetadataService::needsCompletion(song));
        QCOMPARE(SongMetadataService::completeSong(song, presentation)["artists"],
                 presentation["artists"]);
    }
    void metadataConsumersShareFactsAndCancelIndependently()
    {
        ApiClient api(DeviceIdentity::temporary());
        SongMetadataService metadata(api);
        QSignalSpy ready(&metadata, &SongMetadataService::completed);
        SongMetadataServiceTestAccess::flight(metadata, 999001, {"1"});
        const auto first = metadata.request({"1"});
        const auto canceled = metadata.request({"1"});
        metadata.cancel(canceled);
        SongMetadataServiceTestAccess::accept(metadata, 999001,
                                              {QJsonObject{{"id", "1"}, {"title", "Canonical"}}});
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first()[0].toULongLong(), first);
        const auto cached = metadata.request({"1"});
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(ready.last()[0].toULongLong(), cached);
        QCOMPARE(ready.last()[1].toJsonArray().size(), 1);
        SongMetadataServiceTestAccess::flight(metadata, 999002, {"2"});
        metadata.request({"2"});
        SongMetadataServiceTestAccess::accept(metadata, 999002, {});
        QTRY_COMPARE(ready.size(), 3);
        metadata.request({"2"});
        QTRY_COMPARE(ready.size(), 4);
        QVERIFY(ready.last()[1].toJsonArray().isEmpty());
        metadata.shutdown();
        api.shutdown();
    }
    void completionPreservesOriginalTitleAndSynchronizesSortedCover()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "test-token"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api);
        SidebarPlaylistModel sidebar(library);
        PlaylistMutationService mutations(api);
        RouteController router;
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("playlist-detail", {}, "cover-playlist"));
        auto *context = service.contextFor(router.current());
        PlaylistDetailContextTestAccess::seed(*context, "Cover playlist");
        PlaylistDetailContextTestAccess::cloud(*context);
        auto entry = [](int id, int sort, const QString &cover) {
            auto v = playlistSong(id);
            auto s = v["song"].toObject();
            s["cover_url"] = cover;
            s["artists"] = QJsonArray{};
            s["album_id"] = "0";
            s["album_title"] = "";
            v["song"] = s;
            v["sort_index"] = sort;
            auto meta = v["metadata"].toObject();
            meta["source_file_name"] = "原歌手 - Original (Live).mp3";
            v["metadata"] = meta;
            return v;
        };
        const auto reply = [](int page, const QJsonArray &items) {
            return QJsonObject{
                {"user_id", 100},
                {"cloud_list_id", 7},
                {"revision", 9},
                {"page",
                 QJsonObject{{"page", page}, {"page_size", 2}, {"total", 3}, {"items", items}}}};
        };
        PlaylistDetailContextTestAccess::pending(*context, 90001, 1);
        PlaylistDetailContextTestAccess::accept(
            *context, 90001,
            reply(1, {entry(2, 20, "https://example.com/second.jpg"), entry(1, 10, "")}));
        QCOMPARE(library.index(0).data(UserPlaylistModel::CoverUrlRole).toString(),
                 QString("https://example.com/server.jpg"));
        PlaylistDetailContextTestAccess::pending(*context, 90002, 2);
        PlaylistDetailContextTestAccess::accept(
            *context, 90002, reply(2, {entry(3, 30, "https://example.com/third.jpg")}));
        QVERIFY(context->complete());
        QCOMPARE(context->detail()["cover_url"].toString(),
                 QString("https://example.com/second.jpg"));
        QCOMPARE(sidebar.index(0, 0).data(UserPlaylistModel::CoverUrlRole).toString(),
                 context->detail()["cover_url"].toString());
        const auto before = context->songs()->track("1").value();
        context->songs()->toggleSelected("1");
        QSignalSpy resets(context->songs(), &QAbstractItemModel::modelReset);
        PlaylistDetailContextTestAccess::resumeCachedPresentation(
            *context,
            {QJsonObject{{"id", "1"},
                         {"title", "Original (Live)"},
                         {"album_id", "123"},
                         {"album_title", "Album"},
                         {"artists", QJsonArray{QJsonObject{{"id", 8}, {"name", "Artist"}}}},
                         {"cover_url", "https://example.com/first.jpg"}}});
        QTRY_COMPARE(context->songs()->track("1")->title, QString("Original (Live)"));
        QCOMPARE(resets.size(), 0);
        QCOMPARE(context->songs()->selectedCount(), 1);
        const auto after = context->songs()->track("1").value();
        QCOMPARE(after.hash, before.hash);
        QCOMPARE(after.catalogPrivilege, before.catalogPrivilege);
        QCOMPARE(after.catalogMetadata["resources"], before.catalogMetadata["resources"]);
        QCOMPARE(after.catalogMetadata["source_file_name"],
                 before.catalogMetadata["source_file_name"]);
        QCOMPARE(after.title, QString("Original (Live)"));
        QCOMPARE(context->detail()["cover_url"].toString(),
                 QString("https://example.com/first.jpg"));
        QCOMPARE(sidebar.index(0, 0).data(UserPlaylistModel::CoverUrlRole).toString(),
                 context->detail()["cover_url"].toString());
        UserPlaylistModelTestAccess::seed(library, api, true);
        library.publishAutoCover({7}, 99, "https://example.com/auto.jpg", api.sessionRevision());
        QCOMPARE(context->detail()["cover_url"].toString(),
                 QString("https://example.com/manual.jpg"));
        const auto session = api.sessionRevision();
        QVERIFY(api.setAccount(200, "other-token"));
        UserPlaylistModelTestAccess::seed(library, api);
        library.publishAutoCover({7}, 100, "https://example.com/stale.jpg", session);
        QCOMPARE(library.index(0).data(UserPlaylistModel::CoverUrlRole).toString(),
                 QString("https://example.com/server.jpg"));
        service.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void refreshWhileDelegatesAreAliveKeepsTheBackingModel()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SongCatalogModel songs;
        SongCatalogActions actions(songs, playback);
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
import EchoMusic.Music
Window {
 id: window
 required property SongCatalogModel songs
 required property SongCatalogActions actions
 property bool busy: false
 property string errorText: ""
 visible: true; width: 1000; height: 700
 SongCatalogView {
  anchors.fill: parent
  collection: window.songs; actions: window.actions; loading: window.busy
  errorMessage: window.errorText
  headerComponent: Component { SongDetailHeader { heading: "Refresh lifecycle" } }
 }
})",
                          QUrl("qrc:/song-refresh-lifecycle.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"songs", QVariant::fromValue(&songs)}, {"actions", QVariant::fromValue(&actions)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        const auto drainIncubation = [&] {
            if (auto *controller = engine.incubationController())
                controller->incubateFor(20);
            QCoreApplication::processEvents();
        };
        QJsonArray page;
        for (int i = 1; i <= 100; ++i) {
            const auto value = playlistSong(i);
            auto song = value["song"].toObject();
            song["metadata"] = value["metadata"];
            page.append(song);
        }
        songs.appendSongs(page);
        QTRY_VERIFY(playlistVisual(window->contentItem(), "songDelegate"));
        drainIncubation();
        auto *list = playlistVisual(window->contentItem(), "discoverySongs");
        QVERIFY(list);
        QSignalSpy modelChanges(list, SIGNAL(modelChanged()));
        for (int i = 0; i < 40; ++i) {
            // Reproduce the success -> playlist reload -> empty -> loading
            // sequence with live required-role delegates, without online writes.
            songs.clear();
            window->setProperty("busy", true);
            drainIncubation();
            QTRY_VERIFY(playlistVisual(window->contentItem(), "songSkeletonRow"));
            auto *loading = playlistVisual(window->contentItem(), "catalogLoadingSongs");
            QVERIFY(loading);
            QTest::qWait(20);
            const auto loadingImage = window->grabWindow();
            const auto artifacts = qEnvironmentVariable("ECHO_SKELETON_OVERLAP_ARTIFACTS");
            if (i == 0 && !artifacts.isEmpty()) {
                QVERIFY(QDir().mkpath(artifacts));
                QVERIFY(loadingImage.save(artifacts + "/catalog-loading.png"));
            }
            auto *content = loading->property("contentItem").value<QQuickItem *>();
            QVERIFY(content);
            QList<QRectF> rows;
            for (auto *child : content->childItems())
                if (child->objectName() == "songSkeletonRow" && child->isVisible())
                    rows.append(child->mapRectToScene(child->boundingRect()));
            QVERIFY(rows.size() > 1);
            std::sort(rows.begin(), rows.end(),
                      [](const auto &a, const auto &b) { return a.top() < b.top(); });
            for (int r = 0; r < rows.size(); ++r) {
                QVERIFY2(qAbs(rows[r].height() - 60) < .5,
                         qPrintable(QString("Loading row height=%1").arg(rows[r].height())));
                if (r)
                    QVERIFY2(rows[r].top() >= rows[r - 1].bottom() - .5,
                             "Song skeleton delegates overlap while the catalog reloads");
            }
            theme.setMode(i % 2 == 0 ? ThemeStore::Dark : ThemeStore::Light);
            if (i % 5 == 0) {
                window->hide();
                drainIncubation();
                window->show();
            }
            // Allow an actual frame before retiring loading materials, rather
            // than only testing synchronous QML creation/destruction.
            QTest::qWait(20);
            if (i % 2 == 0) {
                window->setProperty("errorText", QStringLiteral("加载失败，请重试"));
                window->setProperty("busy", false); // Cancel/failed reload.
                drainIncubation();
            }
            songs.appendSongs(page);
            window->setProperty("errorText", QString());
            window->setProperty("busy", false);
            drainIncubation();
            QTRY_VERIFY(playlistVisual(window->contentItem(), "songDelegate"));
        }
        // Loading presentation must not replace a live role model with an
        // integer model. Stable ownership keeps delegate contexts valid.
        QCOMPARE(modelChanges.size(), 0);
        QCOMPARE(songs.count(), 100);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        root.reset();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void appendPreservesSelectionAndAvoidsResets()
    {
        SongCatalogModel model;
        QAbstractItemModelTester check(&model,
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto song = [](int id) {
            auto v = playlistSong(id);
            auto s = v["song"].toObject();
            s["metadata"] = v["metadata"];
            return s;
        };
        model.appendSongs({song(1), song(2)});
        model.toggleSelected("1");
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.appendSongs({song(2), song(3)});
        QCOMPARE(resets.size(), 0);
        // Duplicate domain ids stay as separate rows with distinct view keys.
        QCOMPARE(model.count(), 4);
        QCOMPARE(model.selectedCount(), 1);
        QVERIFY(model.item(1)["resourceId"].toString() != model.item(2)["resourceId"].toString());
        QCOMPARE(model.track("3")->catalogMetadata["payment_type"].toInt(), 3);
        model.setQuery("3");
        model.appendSongs({song(4)});
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.selectedCount(), 1);
        model.setQuery({});
        model.replaceSongs({song(2), song(1), song(3), song(4)}, true);
        QCOMPARE(model.item(0)["resourceId"].toString(), QString("2"));
        QCOMPARE(model.selectedCount(), 1);
    }
    void orderedBatchLateDeliveryAndRouteEviction()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        RouteController router;
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("playlist-detail", {}, "public-one"));
        auto *route = router.current();
        auto *context = service.contextFor(route);
        QAbstractItemModelTester check(context->songs(),
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        PlaylistDetailContextTestAccess::seed(*context, "第一个歌单");
        PlaylistDetailContextTestAccess::setSize(*context, 2);
        PlaylistDetailContextTestAccess::pending(*context, 1001, 1);
        PlaylistDetailContextTestAccess::accept(*context, 1001, domainPage(1, {1, 2}));
        QCOMPARE(context->songs()->count(), 2);
        // The disabled transport makes followup requests fail. Reset only the
        // presentation error; these are canonical-domain lifecycle inputs.
        context->reload();
        PlaylistDetailContextTestAccess::seed(*context, "第一个歌单");
        PlaylistDetailContextTestAccess::setSize(*context, 2);
        PlaylistDetailContextTestAccess::pending(*context, 2001, 1);
        PlaylistDetailContextTestAccess::pending(*context, 2002, 2);
        PlaylistDetailContextTestAccess::pending(*context, 2003, 3);
        PlaylistDetailContextTestAccess::pending(*context, 2004, 4);
        PlaylistDetailContextTestAccess::accept(*context, 2003, domainPage(3, {5}));
        QCOMPARE(context->songs()->count(), 0);
        PlaylistDetailContextTestAccess::accept(*context, 2002, domainPage(2, {2, 4}));
        QCOMPARE(context->songs()->count(), 0);
        PlaylistDetailContextTestAccess::accept(*context, 2001, domainPage(1, {1, 2}));
        // Short nonempty pages do not establish EOF while total says more.
        PlaylistDetailContextTestAccess::accept(*context, 2004, domainPage(4, {}));
        QCOMPARE(context->songs()->count(), 5);
        QVERIFY(context->complete());
        // The repeated album audio id keeps both provider rows and the second
        // occurrence receives its own view key.
        QVERIFY(context->songs()->item(1)["resourceId"].toString() !=
                context->songs()->item(2)["resourceId"].toString());
        QCOMPARE(context->songs()->item(3)["resourceId"].toString(), QString("4"));
        QCOMPARE(context->songs()->item(4)["resourceId"].toString(), QString("5"));
        PlaylistDetailContextTestAccess::pending(*context, 3001, 4);
        QVERIFY(router.navigate("playlist-detail", {}, "public-two"));
        PlaylistDetailContextTestAccess::accept(*context, 3001, domainPage(4, {7}));
        QCOMPARE(context->songs()->count(), 5);
        auto *second = service.contextFor(router.current());
        QVERIFY(second != context);
        QCOMPARE(second->songs()->count(), 0);
        QPointer<PlaylistDetailContext> old = context;
        router.setCachePolicy(false, 1);
        QTRY_VERIFY(old.isNull());
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void emptyPageDiscardsInFlightSiblings()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        RouteController router;
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("playlist-detail", {}, "eof-playlist"));
        auto *context = service.contextFor(router.current());
        PlaylistDetailContextTestAccess::seed(*context, "EOF");
        PlaylistDetailContextTestAccess::setSize(*context, 2);
        PlaylistDetailContextTestAccess::pending(*context, 9101, 1);
        PlaylistDetailContextTestAccess::pending(*context, 9102, 2);
        PlaylistDetailContextTestAccess::pending(*context, 9103, 3);
        PlaylistDetailContextTestAccess::accept(*context, 9101, domainPage(1, {1, 2}));
        PlaylistDetailContextTestAccess::accept(*context, 9103, domainPage(3, {5, 6}));
        PlaylistDetailContextTestAccess::accept(*context, 9102, domainPage(2, {}));
        QCOMPARE(context->songs()->count(), 2);
        QVERIFY(context->complete());
        // Late pages after EOF cannot extend the settled catalog.
        PlaylistDetailContextTestAccess::pending(*context, 9104, 4);
        PlaylistDetailContextTestAccess::accept(*context, 9104, domainPage(4, {9, 10}));
        QCOMPARE(context->songs()->count(), 2);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void partialFailureAndAccountGeneration()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        RouteController router;
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("playlist-detail", {}, "partial-playlist"));
        auto *context = service.contextFor(router.current());
        PlaylistDetailContextTestAccess::seed(*context, "Partial");
        PlaylistDetailContextTestAccess::setSize(*context, 2);
        PlaylistDetailContextTestAccess::pending(*context, 7001, 1);
        PlaylistDetailContextTestAccess::accept(*context, 7001, domainPage(1, {1, 2}));
        PlaylistDetailContextTestAccess::pending(*context, 7002, 2);
        PlaylistDetailContextTestAccess::pending(*context, 7003, 3);
        PlaylistDetailContextTestAccess::pending(*context, 7004, 4);
        PlaylistDetailContextTestAccess::fail(*context, 7003);
        QVERIFY(context->loading());
        QVERIFY(context->errorMessage().isEmpty());
        PlaylistDetailContextTestAccess::accept(*context, 7004, domainPage(4, {9}));
        PlaylistDetailContextTestAccess::accept(*context, 7002, domainPage(2, {3, 4}));
        QCOMPARE(context->songs()->count(), 4);
        QVERIFY(!context->complete());
        QVERIFY(!context->loading());
        QCOMPARE(context->errorMessage(), QString("sample page failure"));
        PlaylistDetailContextTestAccess::pending(*context, 8001, 4);
        api.advanceSession();
        QCOMPARE(context->songs()->count(), 0);
        QVERIFY(context->detail().isEmpty());
        PlaylistDetailContextTestAccess::accept(*context, 8001, domainPage(4, {9}));
        QCOMPARE(context->songs()->count(), 0);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void liveOwnPlaylistPlayback()
    {
        if (!qEnvironmentVariableIsSet("ECHO_PLAYLIST_ACCOUNT_INPUT"))
            QSKIP("Opt-in current account, memory-only credentials and isolated playback state");
        const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity{identity["mid"].toString(), identity["dfid"].toString(),
                                     identity["guid"].toString(), identity["mac"].toString(),
                                     identity["device_name"].toString(), "-", "-"});
        NetworkPreferences network(prefs, api);
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                               identity["token"].toString(), identity["t1"].toString()));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        playback.attachLibrary(library, mutations);
        RouteController router;
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, true);
        service.registerRoutes(router);
        library.fetchMore({});
        QTRY_VERIFY_WITH_TIMEOUT(library.state() != UserPlaylistModel::State::Loading, 45000);
        QCOMPARE(library.state(), UserPlaylistModel::State::Ready);
        int row = -1;
        for (int i = 0; i < library.rowCount(); ++i)
            if (library.cloudIdAt(i) &&
                library.index(i).data(UserPlaylistModel::SongCountRole).toULongLong() > 0) {
                row = i;
                break;
            }
        QVERIFY(row >= 0);
        const auto global = library.index(row).data(UserPlaylistModel::GlobalIdRole).toString();
        QVERIFY(router.navigate("playlist-detail", {}, global));
        auto *context = service.contextFor(router.current());
        QTRY_VERIFY_WITH_TIMEOUT(!context->loading(), 45000);
        QVERIFY2(context->errorMessage().isEmpty(),
                 "Current-account playlist detail or cloud tracks failed");
        QVERIFY(context->complete());
        QVERIFY(context->owned());
        QTRY_VERIFY_WITH_TIMEOUT(!playback.libraryActions()->favoriteLoading(), 45000);
        QVERIFY2(playback.libraryActions()->errorMessage().isEmpty(),
                 qPrintable(playback.libraryActions()->errorMessage()));
        QVERIFY(context->songs()->count() > 0);
        const auto tracks = context->songs()->tracks();
        QVERIFY(!tracks.isEmpty());
        QVERIFY(tracks.first().catalogMetadata["rights"].isObject());
        QVERIFY(!tracks.first().catalogMetadata["artists"].toArray().isEmpty());
        context->actions()->play(tracks.first().id, false);
        QTRY_VERIFY_WITH_TIMEOUT(!playback.loading(), 45000);
        QVERIFY2(playback.errorMessage().isEmpty(),
                 "Actual private-playlist source/decoder failed");
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && playback.positionMs() > 200, 15000);
        QCOMPARE(playback.currentTrack().catalogMetadata, tracks.first().catalogMetadata);
        playback.pause();
        QVERIFY(api.setAccount(0, {}));
        QCOMPARE(context->songs()->count(), 0);
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void nativePlaylistPage()
    {
        const bool ownedLive = qEnvironmentVariableIsSet("ECHO_PLAYLIST_LIVE_OWNED_UI");
        QJsonObject identity;
        if (ownedLive) {
            const std::string input((std::istreambuf_iterator<char>(std::cin)),
                                    std::istreambuf_iterator<char>());
            identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
            QVERIFY(!identity.isEmpty());
        }
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(ownedLive
                          ? DeviceIdentity{identity["mid"].toString(), identity["dfid"].toString(),
                                           identity["guid"].toString(), identity["mac"].toString(),
                                           identity["device_name"].toString(), "-", "-"}
                          : DeviceIdentity::temporary());
        NetworkPreferences network(prefs, api);
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        network.installApplicationTransport();
        if (ownedLive)
            QVERIFY(api.setAccount(quint64(identity["userid"].toInteger()),
                                   identity["token"].toString(), identity["t1"].toString()));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        RouteController router;
        const bool live = qEnvironmentVariableIsSet("ECHO_PLAYLIST_LIVE_UI") || ownedLive;
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, live);
        service.registerRoutes(router);
        SidebarPlaylistModel sidebar(library);
        QString global = "collection_3_2132029040_287_0";
        if (ownedLive) {
            library.fetchMore({});
            QTRY_COMPARE_WITH_TIMEOUT(library.state(), UserPlaylistModel::State::Ready, 45000);
            bool found = false;
            for (int i = 0; i < library.rowCount(); ++i) {
                const auto index = library.index(i);
                if (library.cloudIdAt(i) &&
                    index.data(UserPlaylistModel::SongCountRole).toULongLong() > 100 &&
                    index.data(UserPlaylistModel::TitleRole)
                        .toString()
                        .contains(QStringLiteral("喜欢"))) {
                    global = index.data(UserPlaylistModel::GlobalIdRole).toString();
                    found = true;
                    break;
                }
            }
            QVERIFY2(found, "Current account has no large owned favorites playlist");
        }
        ThemeStore theme(prefs);
        ShellSettings shell(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &w : warnings)
                errors.append(w.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Playlists
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible:true; width:1150; height:800
 Rectangle {anchors.fill:parent;color:Theme.main}
 Item {width:parent.width/window.settings.uiScale;height:parent.height/window.settings.uiScale;scale:window.settings.uiScale;transformOrigin:Item.TopLeft
 RouteHost {anchors.fill:parent;router:window.router;settings:window.settings} }
})",
                          QUrl("qrc:/playlist-detail-runtime.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowActive(window));
        QVERIFY(router.navigate("playlist-detail", {}, global));
        auto *context = service.contextFor(router.current());
        const auto artifacts = qEnvironmentVariable("ECHO_PLAYLIST_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        if (live) {
            QTRY_VERIFY_WITH_TIMEOUT(!context->loading(), 45000);
            QVERIFY2(context->errorMessage().isEmpty(), qPrintable(context->errorMessage()));
            QVERIFY(context->complete());
            QVERIFY(context->songs()->count() > 100);
            QCOMPARE(context->owned(), ownedLive);
            QVERIFY(context->songs()->tracks().first().catalogMetadata["rights"].isObject());
            QTRY_VERIFY_WITH_TIMEOUT(!PlaylistDetailContextTestAccess::completing(*context), 45000);
            if (ownedLive) {
                int originals = 0;
                for (const auto &track : context->songs()->tracks()) {
                    const auto raw = track.catalogMetadata["source_file_name"].toString();
                    QVERIFY(!raw.isEmpty());
                    if (raw.endsWith(".mp3", Qt::CaseInsensitive)) {
                        ++originals;
                        QVERIFY(!track.title.endsWith(".mp3", Qt::CaseInsensitive));
                    }
                }
                QVERIFY(originals > 100);
                const auto cover = context->detail()["cover_url"].toString();
                QVERIFY(!cover.isEmpty());
                bool matched = false;
                for (int i = 0; i < sidebar.rowCount(); ++i)
                    if (sidebar.index(i, 0).data(UserPlaylistModel::GlobalIdRole).toString() ==
                        global) {
                        QCOMPARE(
                            sidebar.index(i, 0).data(UserPlaylistModel::CoverUrlRole).toString(),
                            cover);
                        matched = true;
                    }
                QVERIFY(matched);
                qInfo("Current-account raw filename retention and detail/sidebar cover agree");
            }
        } else {
            // Exercise the actual route's full loading presentation before
            // publishing domain data. No network fixture or model swap.
            PlaylistDetailContextTestAccess::pending(*context, 9000001, 0);
            emit context->changed();
            QTRY_VERIFY(playlistVisual(window->contentItem(), "songDetailSkeleton"));
            auto *placeholder = playlistVisual(
                playlistVisual(window->contentItem(), "songDetailSkeleton"), "echoSkeleton");
            QVERIFY(placeholder);
            QTRY_VERIFY(placeholder->property("animationActive").toBool());
            theme.setMotionEnabled(false);
            QTRY_VERIFY(!placeholder->property("animationActive").toBool());
            theme.setMotionEnabled(true);
            window->hide();
            QTRY_VERIFY(!placeholder->property("animationActive").toBool());
            window->show();
            QTRY_VERIFY(placeholder->property("animationActive").toBool());
            if (!artifacts.isEmpty()) {
                for (bool dark : {false, true})
                    for (double scale : {1., 1.25}) {
                        theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                        shell.resetZoom();
                        if (scale > 1.)
                            for (int i = 0; i < 5; ++i)
                                shell.zoomIn();
                        QTest::qWait(450);
                        QVERIFY(
                            window->grabWindow().save(artifacts + QString("/skeleton-%1-%2.png")
                                                                      .arg(dark ? "dark" : "light")
                                                                      .arg(scale)));
                    }
                window->resize(650, 800);
                shell.resetZoom();
                QTest::qWait(200);
                QVERIFY(window->grabWindow().save(artifacts + "/skeleton-narrow.png"));
                window->resize(1150, 800);
            }
            PlaylistDetailContextTestAccess::finishPending(*context, 9000001);
            QTRY_VERIFY(!playlistVisual(window->contentItem(), "songDetailSkeleton"));
            PlaylistDetailContextTestAccess::seed(
                *context, "中文与 Latin 歌单长名称",
                QStringLiteral("中文与 Latin 的完整歌单简介，用于检查截断和查看详情入口。 ")
                    .repeated(10));
            QJsonArray songs;
            for (int i = 1; i <= 10000; ++i) {
                auto v = playlistSong(i);
                auto s = v["song"].toObject();
                s["metadata"] = v["metadata"];
                songs.append(s);
            }
            context->songs()->appendSongs(songs);
            PlaylistDetailContextTestAccess::markClosed(*context);
        }
        QTRY_VERIFY(playlistVisual(window->contentItem(), "playlistDetailPage"));
        QTRY_VERIFY(playlistVisual(window->contentItem(), "songDelegate"));
        auto *view = playlistVisual(window->contentItem(), "songCatalogView");
        QVERIFY(view);
        QTRY_COMPARE_WITH_TIMEOUT(view->property("scrollTop").toReal(), 0., 3000);
        for (bool dark : {false, true})
            for (double scale : {1., 1.25})
                for (int width : {1150, 650}) {
                    window->resize(width, 800);
                    theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
                    shell.resetZoom();
                    if (scale > 1.)
                        for (int i = 0; i < 5; ++i)
                            shell.zoomIn();
                    QTest::qWait(160);
                    QCOMPARE(playlistVisualCount(window->contentItem(), "songOriginalTag"), 0);
                    const auto frame = window->grabWindow();
                    QVERIFY(!frame.isNull());
                    if (!artifacts.isEmpty())
                        QVERIFY(frame.save(artifacts + QString("/playlist-%1-%2-w%3.png")
                                                           .arg(dark ? "dark" : "light")
                                                           .arg(scale)
                                                           .arg(width)));
                    auto *header = playlistVisual(window->contentItem(), "songDetailHeader");
                    auto *list = playlistVisual(window->contentItem(), "discoverySongs");
                    QVERIFY(header && list);
                    if (!live) {
                        auto *creator = playlistVisual(header, "playlistCreatorRow");
                        auto *tags = playlistVisual(header, "playlistMetadataTags");
                        auto *description = playlistVisual(header, "songDetailDescription");
                        auto *actions = playlistVisual(header, "songDetailActions");
                        auto *title = playlistVisual(header, "songDetailTitleRow");
                        QVERIFY(creator && tags && description && actions && title);
                        const auto box = [header](QQuickItem *item) {
                            return item->mapRectToItem(header, item->boundingRect());
                        };
                        const QList<qreal> gaps{box(creator).top() - box(title).bottom(),
                                                box(tags).top() - box(creator).bottom(),
                                                box(description).top() - box(tags).bottom(),
                                                box(actions).top() - box(description).bottom()};
                        for (auto gap : gaps)
                            QVERIFY2(qAbs(gap - gaps.first()) <= 1.01,
                                     qPrintable(QString("Metadata gaps %1 %2 %3 %4")
                                                    .arg(gaps[0])
                                                    .arg(gaps[1])
                                                    .arg(gaps[2])
                                                    .arg(gaps[3])));
                        for (const auto &pair :
                             {QPair<QString, QString>{"playlistSongCount", "playlistMetadataTag"},
                              {"songDetailDescriptionPreview", "songDetailDescriptionAction"},
                              {"playlistCreatorName", "playlistPublishedDate"}}) {
                            auto *left = playlistVisual(header, pair.first);
                            auto *right = playlistVisual(header, pair.second);
                            QVERIFY(left && right && left->isVisible() && right->isVisible());
                            const auto a = renderedInkCenter(frame, window, renderedText(left));
                            const auto b = renderedInkCenter(frame, window, renderedText(right));
                            if (qAbs(a - b) > .85 * scale) {
                                for (auto *item :
                                     {left, right, renderedText(left), renderedText(right)}) {
                                    const auto bounds = item->mapRectToScene(item->boundingRect());
                                    qInfo() << "Alignment probe" << item->objectName()
                                            << item->metaObject()->className() << bounds << "text"
                                            << item->property("text") << "font"
                                            << item->property("font") << "baseline"
                                            << item->property("baselineOffset");
                                }
                            }
                            QVERIFY2(a > 0 && b > 0 && qAbs(a - b) <= 0.85 * scale,
                                     qPrintable(QString("Rendered ink %1 / %2: %3 / %4 at %5")
                                                    .arg(pair.first, pair.second)
                                                    .arg(a)
                                                    .arg(b)
                                                    .arg(scale)));
                        }
                    }
                    if (!live) {
                        auto *count = playlistVisual(header, "playlistSongCount");
                        const auto center = renderedInkCenter(frame, window, renderedText(count));
                        const auto verifyTags = [&](auto &&self, QQuickItem *item) -> bool {
                            if (item->objectName() == "playlistMetadataTag" && item->isVisible()) {
                                const auto tagCenter =
                                    renderedInkCenter(frame, window, renderedText(item));
                                if (tagCenter <= 0 || qAbs(tagCenter - center) > .85 * scale)
                                    return false;
                            }
                            for (auto *child : item->childItems())
                                if (!self(self, child))
                                    return false;
                            return true;
                        };
                        QVERIFY(verifyTags(verifyTags, header));
                    }
                    auto *locate = playlistVisual(window->contentItem(), "discoveryLocate");
                    QVERIFY(locate);
                    QCOMPARE(locate->width(), 36.);
                    QCOMPARE(locate->height(), 36.);
                    auto *surface = playlistVisual(locate, "songLocateBackground");
                    QVERIFY(surface);
                    QTest::mouseMove(window, QPoint(2, 2));
                    QTRY_COMPARE(surface->property("color").value<QColor>(), theme.mutedSurface());
                    auto *colors = locate->property("colors").value<QObject *>();
                    QVERIFY(colors);
                    const auto center = locate->mapToScene({18, 18}).toPoint();
                    QTest::mouseMove(window, center);
                    QTRY_VERIFY(locate->property("hovered").toBool());
                    QTRY_COMPARE(surface->property("color").value<QColor>(),
                                 colors->property("hover").value<QColor>());
                    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
                    QTRY_COMPARE(surface->property("color").value<QColor>(),
                                 colors->property("neutralPressed").value<QColor>());
                    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center);
                    QTest::mouseMove(window, QPoint(2, 2));
                    QTRY_COMPARE(surface->property("color").value<QColor>(), theme.mutedSurface());
                    list->setProperty("contentY", 500);
                    QTRY_COMPARE(header->property("progress").toReal(), 1.);
                    if (!live) {
                        const auto tracks = context->songs()->tracks();
                        QVERIFY(tracks.size() > 12);
                        PlaybackServiceTestAccess::present(playback, tracks[12]);
                        const auto clickLocate = [&] {
                            const auto point = locate->mapToScene({18, 18}).toPoint();
                            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                        };
                        QVERIFY(QMetaObject::invokeMethod(list, "forceLayout"));
                        QSignalSpy movements(list, SIGNAL(contentYChanged()));
                        clickLocate();
                        QCOMPARE(movements.count(), 0);
                        QCOMPARE(list->property("contentY").toReal(), 500.);
                        // At contentY=720 the current row is behind the sticky
                        // toolbar even though ListView still owns its delegate.
                        list->setProperty("contentY", 720.);
                        QVERIFY(QMetaObject::invokeMethod(list, "forceLayout"));
                        clickLocate();
                        QVERIFY(list->property("contentY").toReal() < 720.);
                        const auto located = list->property("contentY").toReal();
                        movements.clear();
                        clickLocate();
                        QCOMPARE(movements.count(), 0);
                        QCOMPARE(list->property("contentY").toReal(), located);
                        list->setProperty("contentY", 500.);
                    }
                    auto *cover = playlistVisual(header, "songDetailCoverSlot");
                    auto *title = playlistVisual(header, "songDetailTitleRow");
                    QVERIFY(cover && title);
                    QTRY_VERIFY(qAbs(cover->mapToScene({0, cover->height() / 2}).y() -
                                     title->mapToScene({0, title->height() / 2}).y()) < scale);
                    if (!artifacts.isEmpty())
                        QVERIFY(window->grabWindow().save(
                            artifacts + QString("/playlist-collapsed-%1-%2-w%3.png")
                                            .arg(dark ? "dark" : "light")
                                            .arg(scale)
                                            .arg(width)));
                    list->setProperty("contentY", -list->property("topMargin").toReal());
                    QTRY_COMPARE(header->property("progress").toReal(), 0.);
                }
        if (!live) {
            PlaylistDetailContextTestAccess::seed(*context, "中文与 Latin 歌单", "简短简介");
            auto *action = playlistVisual(window->contentItem(), "songDetailDescriptionAction");
            QVERIFY(action);
            QTRY_VERIFY(!action->isVisible());
        }
        const int rows = playlistVisualCount(window->contentItem(), "songDelegate");
        QVERIFY(rows > 0 && rows < 100);
        qInfo("Playlist %d songs: %d instantiated rows", context->songs()->totalCount(), rows);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        root.reset();
        service.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void playlistDetailEditEntryRequiresOwnedLibraryMembership()
    {
        QTemporaryDir dir;
        QSettings preferences(dir.filePath("settings.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "edit-ownership-fixture"));
        PlaybackService playback(api, preferences, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        RouteController router;
        SongMetadataService metadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, metadata, router,
                                      false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("playlist-detail", {}, "cover-playlist"));
        auto *context = service.contextFor(router.current());
        QVERIFY(context);
        PlaylistDetailContextTestAccess::seed(*context, "Owner playlist");
        UserPlaylistModelTestAccess::setKind(library, "owned");
        QCOMPARE(context->editListId(), quint64(7));
        UserPlaylistModelTestAccess::setKind(library, "collected");
        QCOMPARE(context->editListId(), quint64(0));
    }
    void ownedPlaylistOrderPopupKeepsLocalTargetThroughInvalidation()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "order-test-token"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        RouteController router;
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("playlist-detail", {}, "cover-playlist"));
        auto *context = service.contextFor(router.current());
        PlaylistDetailContextTestAccess::seed(*context, "Owner playlist");
        PlaylistDetailContextTestAccess::cloud(*context);
        context->songs()->appendSongs({playlistSong(1)["song"]});
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings shell(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1000; height: 750
 RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
})",
                          QUrl("qrc:/playlist-order-production.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(QTest::qWaitForWindowActive(window));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = playlistVisual(window->contentItem(), "playlistDetailPage")));
        auto *owner = page->findChild<QObject *>("playlistOrderDialogOwner");
        QVERIFY(owner);
        QVERIFY(!owner->property("item").value<QObject *>());
        auto *button = playlistVisual(page, "playlistSongSort");
        QTRY_VERIFY(button && button->isVisible());
        for (int round = 0; round < 3; ++round) {
            QTest::mouseClick(
                window, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTRY_VERIFY(owner->property("item").value<QObject *>());
            QPointer<QObject> dialog = owner->property("item").value<QObject *>();
            QTRY_COMPARE(order.mode(), 1);
            QTRY_COMPARE(order.listId(), quint64(7));
            QCOMPARE(dialog->property("tracksListId").toULongLong(), quint64(7));
            QCOMPARE(dialog->property("tracksTitle").toString(),
                     context->detail()["title"].toString());
            if (round < 2) {
                QVERIFY(QMetaObject::invokeMethod(owner, "close"));
            } else {
                context->songs()->setQuery("1");
                context->songs()->toggleSort(2);
                // Production invalidates the library and detail before saved().
                library.reload();
                emit mutations.tracksInvalidated(7);
                QCOMPARE(context->orderListId(), quint64(0));
                QCOMPARE(dialog->property("tracksListId").toULongLong(), quint64(7));
                emit order.saved();
                QCOMPARE(context->songs()->query(), QString());
                QCOMPARE(context->songs()->sortField(), 0);
            }
            QTRY_VERIFY(!owner->property("item").value<QObject *>());
            QTRY_VERIFY(dialog.isNull());
        }
        root.reset();
        service.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void ownerRemoveSongWritesFileIdThroughSharedMutation()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "owner-remove-token"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api);
        PlaylistMutationService mutations(api);
        RouteController router;
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        PlaylistDetailService service(api, playback, library, mutations, order, songMetadata,
                                      router, false);
        service.registerRoutes(router);
        QVERIFY(router.navigate("playlist-detail", {}, "cover-playlist"));
        auto *context = service.contextFor(router.current());
        PlaylistDetailContextTestAccess::seed(*context, "Owner playlist");
        PlaylistDetailContextTestAccess::cloud(*context);
        auto entry = [](int id, int file) {
            auto v = playlistSong(id);
            v["file_id"] = file;
            v["sort_index"] = id;
            return v;
        };
        const auto reply = [](int page, const QJsonArray &items) {
            return QJsonObject{
                {"user_id", 100},
                {"cloud_list_id", 7},
                {"revision", 9},
                {"page",
                 QJsonObject{{"page", page}, {"page_size", 2}, {"total", 3}, {"items", items}}}};
        };
        PlaylistDetailContextTestAccess::pending(*context, 90011, 1);
        PlaylistDetailContextTestAccess::accept(*context, 90011,
                                                reply(1, {entry(2, 20), entry(1, 10)}));
        PlaylistDetailContextTestAccess::pending(*context, 90012, 2);
        PlaylistDetailContextTestAccess::accept(*context, 90012, reply(2, {entry(3, 30)}));
        QVERIFY(context->complete());
        QVERIFY(context->owned());
        QCOMPARE(service.order(), &order);
        QCOMPARE(context->orderListId(), quint64(7));
        UserPlaylistModelTestAccess::changeOwner(library, 100, 2);
        QCOMPARE(context->orderListId(), quint64(0));
        UserPlaylistModelTestAccess::changeOwner(library, 200, 1);
        QCOMPARE(context->orderListId(), quint64(0));
        UserPlaylistModelTestAccess::changeOwner(library, 100, 1, 0);
        QCOMPARE(context->orderListId(), quint64(0));
        UserPlaylistModelTestAccess::changeOwner(library, 100, 1, 9007199254740992ULL);
        QCOMPARE(context->orderListId(), quint64(0));
        UserPlaylistModelTestAccess::changeOwner(library, 100, 1);
        QCOMPARE(context->orderListId(), quint64(7));
        // 未知歌曲不产生写入；已知歌曲把服务端 file_id 交给共享单写者。
        QVERIFY(!context->removeSong("999"));
        QVERIFY(context->removeSong("1"));
        const auto request =
            ApiClientTestAccess::requestForOperation(api, QStringLiteral("playlist_tracks_del"));
        QVERIFY(!request.isEmpty());
        const auto query = request.value("query").toObject();
        QCOMPARE(query.value("cloud_list_id").toInteger(), qint64(7));
        const auto files = query.value("file_ids").toArray();
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().toInteger(), qint64(10));
        service.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
};
QTEST_MAIN(PlaylistDetailTest)
#include "PlaylistDetailTest.moc"
