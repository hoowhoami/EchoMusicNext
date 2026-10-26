#include "ApiClient.h"
#include "CloudService.h"
#include "FavoritesService.h"
#include "NetworkPreferences.h"
#include "PlaybackService.h"
#include "PlaylistMutationService.h"
#include "PlaylistOrderModel.h"
#include "PlaylistEditModel.h"
#include "PurchasedService.h"
#include "RouteController.h"
#include "SongCatalogModel.h"
#include "UserPlaylistModel.h"
#include "ThemeStore.h"
#include "ShellSettings.h"
#include "RenderedInk.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDir>
#include <functional>
#include <optional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
using namespace EchoMusic;
namespace EchoMusic
{
class UserPlaylistModelTestAccess
{
public:
    static void seed(UserPlaylistModel &model, ApiClient &api, const QString &title, quint64 source,
                     quint64 creatorListId, quint64 cloudId = 7)
    {
        model.beginResetModel();
        model.m_rows.clear();
        UserPlaylistModel::Row row;
        row.cloudId = {cloudId};
        row.globalId = "collection";
        row.kind = QStringLiteral("owned");
        row.title = title;
        row.cover = "https://example.com/cover.jpg";
        row.sourceCode = quint32(source);
        row.creatorListId = creatorListId;
        row.creatorId = api.currentAccountUserId();
        row.revision = 1;
        model.m_rows.append(row);
        model.m_sessionRevision = api.sessionRevision();
        model.endResetModel();
    }
    static void seedRows(UserPlaylistModel &model, ApiClient &api,
                         const QList<QPair<QString, quint64>> &titlesToIds)
    {
        model.beginResetModel();
        model.m_rows.clear();
        for (const auto &pair : titlesToIds) {
            UserPlaylistModel::Row row;
            row.cloudId = {pair.second};
            row.globalId = "collection";
            row.kind = QStringLiteral("owned");
            row.title = pair.first;
            row.sourceCode = 1;
            row.creatorId = api.currentAccountUserId();
            row.revision = 1;
            model.m_rows.append(row);
        }
        model.m_sessionRevision = api.sessionRevision();
        model.endResetModel();
    }
};
class FavoritesServiceTestAccess
{
public:
    static quint64 metadataTicket(const FavoritesService &s)
    {
        return s.m_metadataRequests.isEmpty() ? 0 : s.m_metadataRequests.cbegin().key();
    }
    static int metadataRequestedCount(const FavoritesService &s)
    {
        return s.m_metadataScheduled.size();
    }
    static void metadata(FavoritesService &s, quint64 ticket, const QJsonArray &values)
    {
        s.acceptMetadata(ticket, values);
    }
    static QJsonObject sourceEntry(const FavoritesService &s, const QString &id)
    {
        for (const auto &value : s.m_songRows)
            if (value.toObject()["song"].toObject()["id"].toString() == id)
                return value.toObject();
        return {};
    }
    static void acceptSongs(FavoritesService &s, const QJsonArray &items, qint64 total,
                            quint64 id = 900103, qint64 revision = 7)
    {
        s.m_pending.insert(id,
                           {FavoritesService::PendingKind::LikedSongs, s.m_api.sessionRevision()});
        s.accept(id, QJsonDocument(
                         QJsonObject{{"revision", revision},
                                     {"page", QJsonObject{{"items", items}, {"total", total}}}})
                         .toJson());
    }
    static bool complete(const FavoritesService &s)
    {
        return s.m_likedLoaded;
    }
    static quint64 songRequest(const FavoritesService &s)
    {
        for (auto it = s.m_pending.cbegin(); it != s.m_pending.cend(); ++it)
            if (it->kind == FavoritesService::PendingKind::LikedSongs)
                return it.key();
        return 0;
    }
    static void enablePaging(FavoritesService &s)
    {
        s.m_network = true;
        s.m_likedId = s.likedListId();
    }
    static int received(const FavoritesService &s)
    {
        return int(s.m_songReceived);
    }
    static bool positionsOrdered(const FavoritesService &s)
    {
        QHash<QString, qint64> positions;
        for (const auto &value : s.m_songRows) {
            const auto entry = value.toObject();
            if (entry.value("sort_index").isDouble())
                positions.insert(entry.value("song").toObject().value("id").toString(),
                                 entry.value("sort_index").toInteger());
        }
        std::optional<qint64> previous;
        for (int i = 0; i < s.m_songs.count(); ++i) {
            const auto id = s.m_songs.item(i).value("resourceId").toString();
            if (!positions.contains(id))
                continue;
            const auto current = positions[id];
            if (previous && current < *previous)
                return false;
            previous = current;
        }
        return previous.has_value();
    }
    static void acceptFollow(FavoritesService &s, const QJsonArray &users)
    {
        const quint64 id = 900101;
        s.m_pending.insert(id, {FavoritesService::PendingKind::Follow, s.m_api.sessionRevision()});
        s.accept(id, QJsonDocument(QJsonObject{{"users", users}}).toJson(QJsonDocument::Compact));
    }
    static void acceptVideos(FavoritesService &s, const QJsonArray &items, qint64 total)
    {
        const quint64 id = 900102;
        s.m_pending.insert(id, {FavoritesService::PendingKind::Video, s.m_api.sessionRevision()});
        s.accept(id, QJsonDocument(QJsonObject{{"page", QJsonObject{{"page", 1},
                                                                    {"page_size", 30},
                                                                    {"total", total},
                                                                    {"items", items}}},
                                               {"revision", 1}})
                         .toJson(QJsonDocument::Compact));
    }
    static void projectAlbums(FavoritesService &s)
    {
        s.projectAlbums();
    }
};
class SongMetadataServiceTestAccess
{
public:
    static void complete(SongMetadataService &service, const QStringList &ids,
                         const QJsonArray &values)
    {
        constexpr quint64 request = 990001;
        service.m_flights.insert(request, ids);
        service.accept(request, QJsonDocument(values).toJson());
    }
};
class CloudServiceTestAccess
{
public:
    static void enableMetadata(CloudService &s)
    {
        s.m_network = true;
    }
    static quint64 metadataTicket(const CloudService &s)
    {
        return s.m_metadataRequests.isEmpty() ? 0 : s.m_metadataRequests.cbegin().key();
    }
    static void metadata(CloudService &s, quint64 ticket, const QJsonArray &values)
    {
        s.acceptMetadata(ticket, values);
    }
    static QJsonObject file(const CloudService &s, quint64 id)
    {
        for (const auto &item : s.m_items)
            if (quint64(item["file_id"].toInteger()) == id)
                return item;
        return {};
    }
    static void quota(CloudService &s)
    {
        s.updateCounters(
            {{"max_size", 1073741824}, {"availble_size", 805306368}, {"list_count", 80}});
        emit s.changed();
    }
    static void acceptPage(CloudService &s, const QJsonObject &page, const QJsonObject &counters)
    {
        const quint64 id = 910101;
        if (s.m_listRequest)
            s.m_api.cancel(s.m_listRequest);
        s.m_listRequest = id;
        s.m_listSession = s.m_api.sessionRevision();
        s.accept(id, QJsonDocument(QJsonObject{{"page", page}, {"counters", counters}})
                         .toJson(QJsonDocument::Compact));
    }
    static void acceptDelete(CloudService &s, const QJsonObject &counters)
    {
        const quint64 id = 910102;
        s.m_deleteRequest = id;
        s.m_deleteSession = s.m_api.sessionRevision();
        s.accept(id,
                 QJsonDocument(QJsonObject{{"counters", counters}}).toJson(QJsonDocument::Compact));
    }
    static quint64 deleteRequest(const CloudService &s)
    {
        return s.m_deleteRequest;
    }
    static QList<ApiClient::CloudDeleteTarget> deleteTargets(const CloudService &s)
    {
        return s.m_deleteTargets;
    }
};
class PurchasedServiceTestAccess
{
public:
    static void acceptSongs(PurchasedService &s, const QJsonArray &items, qint64 total)
    {
        const quint64 id = 920101;
        s.m_pending.insert(id, {PurchasedService::PendingKind::Songs, s.m_api.sessionRevision()});
        s.accept(id, QJsonDocument(QJsonObject{{"page", QJsonObject{{"page", 1},
                                                                    {"page_size", 30},
                                                                    {"total", total},
                                                                    {"items", items}}}})
                         .toJson(QJsonDocument::Compact));
    }
    static void loadTab(PurchasedService &s)
    {
        s.m_active = true;
        s.m_network = true;
        s.loadTab();
    }
    static QList<int> pendingKinds(const PurchasedService &s)
    {
        QList<int> kinds;
        for (const auto &pending : s.m_pending)
            kinds.append(int(pending.kind));
        return kinds;
    }
    static void acceptAlbums(PurchasedService &s, const QJsonArray &items, qint64 total)
    {
        const quint64 id = 920102;
        s.m_pending.insert(id, {PurchasedService::PendingKind::Albums, s.m_api.sessionRevision()});
        s.accept(id, QJsonDocument(QJsonObject{{"page", QJsonObject{{"page", 1},
                                                                    {"page_size", 30},
                                                                    {"total", total},
                                                                    {"items", items}}}})
                         .toJson(QJsonDocument::Compact));
    }
};
class PlaylistOrderModelTestAccess
{
public:
    static quint64 request(const PlaylistOrderModel &model)
    {
        return model.m_request;
    }
    static void complete(PlaylistOrderModel &model, quint64 id, const QByteArray &payload)
    {
        model.succeed(id, payload);
    }
};
class PlaylistEditModelTestAccess
{
public:
    static quint64 request(const PlaylistEditModel &model)
    {
        return model.m_request;
    }
    static void complete(PlaylistEditModel &model, const QByteArray &bytes)
    {
        model.accept(model.m_request, bytes);
    }
    static QByteArray png(const PlaylistEditModel &model)
    {
        return model.m_png;
    }
};
class PlaylistMutationServiceTestAccess
{
public:
    static quint64 request(const PlaylistMutationService &service)
    {
        return service.m_request;
    }
    static void complete(PlaylistMutationService &service, quint64 id, const QByteArray &payload)
    {
        service.succeed(id, payload);
    }
};
class CloudUploadServiceTestAccess
{
public:
    static void seed(CloudUploadService &s, int count, QString phase = "idle", QString path = {})
    {
        s.reset();
        s.m_session = s.m_api.sessionRevision();
        s.m_step = phase == "idle" ? "manual" : "progress";
        s.m_phase = phase;
        s.beginResetModel();
        for (int i = 0; i < count; ++i) {
            CloudUploadService::File f;
            f.title = QString("测试歌曲 %1 Song").arg(i);
            f.artist = "歌手 Artist";
            f.name = "Artist - Song.wav";
            f.path = path;
            f.ext = "wav";
            f.hash = "0123456789abcdef0123456789abcdef";
            f.hashChecked = true;
            f.standardHash = f.hash;
            f.size = 1152044;
            f.duration = 6000;
            if (!path.isEmpty()) {
                QFileInfo info(path);
                f.size = info.size();
                f.modified = info.lastModified().toMSecsSinceEpoch();
            }
            s.m_files << f;
        }
        s.endResetModel();
        s.notify();
    }
    static void candidates(CloudUploadService &s)
    {
        s.m_candidates =
            QJsonArray{QJsonObject{{"id", "32100650"},
                                   {"audio_id", 20505418},
                                   {"hash", "0123456789abcdef0123456789abcdef"},
                                   {"title", "测试歌曲 Song"},
                                   {"album_title", "测试专辑 Album"},
                                   {"duration_ms", 224653},
                                   {"artists", QJsonArray{QJsonObject{{"name", "歌手 Artist"}}}}}};
        s.notify();
    }
    static void readPart(CloudUploadService &service, int part = 1)
    {
        service.m_part = part;
        service.m_transfer = {{"host", "http://bss.kugou.com"},
                              {"authorization", "fixture-authorization"},
                              {"upload_id", "fixture-upload"}};
        service.readUploadPart();
    }
    static bool reading(const CloudUploadService &service)
    {
        return service.m_partRead.isRunning();
    }
    static void acknowledge(CloudUploadService &s)
    {
        s.m_stage = ApiClient::CloudUploadStage::Add;
        s.m_request = 999999;
        s.accept(s.m_request, QByteArrayLiteral("{}"));
    }
    static void auth(CloudUploadService &s)
    {
        s.uploadStage(ApiClient::CloudUploadStage::Auth,
                      {{"hash", "0123456789abcdef0123456789abcdef"}});
    }
    static void matchHash(CloudUploadService &s, bool clearArtist)
    {
        if (clearArtist)
            s.m_files[0].artist.clear();
        s.m_lookupKind = CloudUploadService::Hash;
        s.applyLookup(
            {{"matches",
              QJsonArray{QJsonObject{
                  {"author_name", "Matched Artist"},
                  {"audio_id", 978806},
                  {"album_audio_id", 38232532},
                  {"audio_info", QJsonObject{{"hash", "ABCDEF0123456789ABCDEF0123456789AB"}}}}}}});
    }
    static QString standardHash(CloudUploadService &s)
    {
        return s.m_files[0].standardHash;
    }
    static quint64 request(CloudUploadService &s)
    {
        return s.m_request;
    }
    static qint64 lastLookup(CloudUploadService &s)
    {
        return s.m_lastLookup;
    }
    static void complete(CloudUploadService &s, QJsonObject result)
    {
        // Part dispatch now includes an asynchronous host-owned file read.
        QVERIFY(QTest::qWaitFor([&] { return s.m_request != 0; }, 5000));
        const auto id = s.m_request;
        s.m_api.cancel(id);
        s.accept(id, QJsonDocument(result).toJson());
    }
    static void fail(CloudUploadService &s, QJsonObject error)
    {
        auto id = s.m_request;
        s.m_api.cancel(id);
        s.reject(id, QJsonDocument(error).toJson());
    }
    static void advance(CloudUploadService &s)
    {
        s.uploadNext();
    }
    static int position(CloudUploadService &s)
    {
        return s.m_index;
    }
    static void expireCooldown(CloudUploadService &s)
    {
        s.m_resumeAt = 0;
    }
    static bool acceptable(const QString &title, const QString &artist, QJsonObject candidate)
    {
        CloudUploadService::File f;
        f.title = title;
        f.artist = artist;
        bool ok = false;
        sScore(f, candidate, &ok);
        return ok;
    }
    static double sScore(const CloudUploadService::File &f, const QJsonObject &c, bool *ok)
    {
        return CloudUploadService::score(f, c, ok);
    }
};
class ApiClientTestAccess
{
public:
    static void challenge(ApiClient &api, quint64 id)
    {
        api.deliver(
            id, api.m_revision, 5,
            QByteArrayLiteral(
                "{\"kind\":\"challenge\",\"challenge\":{\"event_id\":\"local-fixture-event\"}}"));
    }
    static void verified(ApiClient &api)
    {
        api.retryVerifiedEvent("local-fixture-event");
    }
    static bool replayExists(const ApiClient &api, quint64 logical)
    {
        for (const auto &pending : api.m_pending)
            if (pending.logicalId == logical)
                return true;
        return false;
    }

    static QJsonObject request(const ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).request;
    }
};
} // namespace EchoMusic

class LibraryPagesTest : public QObject
{
    Q_OBJECT
private slots:

    void playlistEditorPreservesMetadataGatesVersionAndPreparesCover()
    {
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api, "Original", 1, 0);
        PlaylistMutationService mutations(api);
        PlaylistEditModel editor(api, library, mutations);
        const auto row = QJsonObject{{"cloud_list_id", 7},       {"global_id", "collection"},
                                     {"creator_id", 100},        {"title", "Original"},
                                     {"kind", "owned"},          {"source_code", 1},
                                     {"sort_index", 3},          {"description_present", true},
                                     {"tags_present", true},     {"tags", "保留标签"},
                                     {"description", "保留简介"}};
        const auto payload = [&row](int version) {
            return QJsonDocument(
                       QJsonObject{{"user_id", 100},
                                   {"total_revision", version},
                                   {"page", QJsonObject{{"total", 1}, {"items", QJsonArray{row}}}}})
                .toJson();
        };
        editor.open(7);
        QVERIFY(editor.loading());
        PlaylistEditModelTestAccess::complete(editor, payload(9));
        QVERIFY(editor.loaded());
        QVERIFY(!editor.dirty());
        editor.setTitle("Renamed");
        editor.save();
        QVERIFY(editor.saving());
        const auto check = PlaylistMutationServiceTestAccess::request(mutations);
        PlaylistMutationServiceTestAccess::complete(mutations, check, payload(10));
        QVERIFY(!editor.saving());
        QVERIFY(!editor.errorMessage().isEmpty());
        QCOMPARE(editor.title(), QString("Renamed"));
        QCOMPARE(editor.tags(), QString("保留标签"));
        QCOMPARE(editor.description(), QString("保留简介"));
        QCOMPARE(mutations.state(), PlaylistMutationService::State::Ready);
        // Metadata-only saving must not upload or send a cover reference.
        editor.reload();
        PlaylistEditModelTestAccess::complete(editor, payload(9));
        editor.setTitle("Renamed");
        editor.save();
        PlaylistMutationServiceTestAccess::complete(
            mutations, PlaylistMutationServiceTestAccess::request(mutations), payload(9));
        const auto write = PlaylistMutationServiceTestAccess::request(mutations);
        const auto command = ApiClientTestAccess::request(api, write);
        QCOMPARE(command["operation"].toString(), QString("playlist_update"));
        const auto query = command["query"].toObject();
        QVERIFY(!query.contains("cover_reference"));
        QCOMPARE(query["tags"].toString(), QString("保留标签"));
        QCOMPARE(query["description"].toString(), QString("保留简介"));
        QSignalSpy saved(&editor, &PlaylistEditModel::saved);
        PlaylistMutationServiceTestAccess::complete(
            mutations, write,
            R"({"user_id":100,"cloud_list_id":7,"total_revision":10,"playlist_count":1})");
        PlaylistMutationServiceTestAccess::complete(
            mutations, PlaylistMutationServiceTestAccess::request(mutations), payload(10));
        QCOMPARE(saved.size(), 1);
        QVERIFY(!editor.saving());
        QVERIFY(!editor.dirty());
        QTemporaryDir dir;
        QImage image(800, 400, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(dir.filePath("cover.png")));
        editor.selectCover(QUrl::fromLocalFile(dir.filePath("cover.png")));
        QTRY_VERIFY(!editor.selectingCover());
        const auto prepared = QImage::fromData(PlaylistEditModelTestAccess::png(editor));
        QCOMPARE(prepared.size(), QSize(400, 400));
        const auto stale = PlaylistEditModelTestAccess::request(editor);
        editor.close();
        QVERIFY(!editor.loaded());
        QVERIFY(editor.coverUrl().isEmpty());
        QVERIFY(PlaylistEditModelTestAccess::png(editor).isEmpty());
        editor.open(7);
        const auto request = PlaylistEditModelTestAccess::request(editor);
        QVERIFY(request);
        QVERIFY(api.setAccount(101, "next-account"));
        emit api.succeeded(request, payload(9));
        QVERIFY(!editor.loaded());
        Q_UNUSED(stale);
        api.shutdown();
    }

    void playlistEditorFillsAbsentFieldsAndKeepsCollectedIdentity()
    {
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api, "Original", 1, 0);
        PlaylistMutationService mutations(api);
        PlaylistEditModel editor(api, library, mutations);
        editor.open(7);
        const QByteArray page =
            R"({"user_id":100,"total_revision":9,"page":{"total":1,"items":[{"cloud_list_id":7,"creator_global_id":"public-creator-id","title":"Original","kind":"collected","source_code":1,"sort_index":3,"description_present":false,"tags_present":false}]}})";
        PlaylistEditModelTestAccess::complete(editor, page);
        QVERIFY(!editor.loaded());
        const auto detailRead = PlaylistEditModelTestAccess::request(editor);
        QVERIFY(detailRead);
        QCOMPARE(ApiClientTestAccess::request(api, detailRead)["operation"].toString(),
                 QString("playlist_detail"));
        PlaylistEditModelTestAccess::complete(
            editor,
            R"([{"global_id":"public-creator-id","description":"保留的公开简介","tags":"华语,流行"}])");
        QVERIFY(editor.loaded());
        QCOMPARE(editor.description(), QString("保留的公开简介"));
        editor.setTitle("Personal name");
        editor.save();
        PlaylistMutationServiceTestAccess::complete(
            mutations, PlaylistMutationServiceTestAccess::request(mutations), page);
        const auto command = ApiClientTestAccess::request(
            api, PlaylistMutationServiceTestAccess::request(mutations));
        QCOMPARE(command["operation"].toString(), QString("playlist_update"));
        const auto query = command["query"].toObject();
        QCOMPARE(query["cloud_list_id"].toInteger(), qint64(7));
        QCOMPARE(query["kind"].toString(), QString("collected"));
        QCOMPARE(query["description"].toString(), QString("保留的公开简介"));
        QCOMPARE(query["tags"].toString(), QString("华语,流行"));
        mutations.cancel();
        editor.close();
        api.shutdown();
    }

    void playlistEditorProductionDialogLifecycle()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        ApiClient api(DeviceIdentity::temporary());
        // Keep transport pending while the native window settles; canonical DTO
        // injection below must not race a refused localhost connection.
        QVERIFY(api.setNetworkTransport(proxy.serverPort()));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api, "Original", 1, 0);
        PlaylistMutationService mutations(api);
        PlaylistEditModel editor(api, library, mutations);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &e : errors)
                warnings << e.toString();
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Library
import EchoMusic.Controls
ApplicationWindow {
 required property PlaylistEditModel editor
 visible: true; width: 900; height: 820
 EchoPopupLoader {id: owner; objectName: "editOwner"; hostItem: contentItem; sourceComponent: Component {PlaylistEditDialog {cloudId: 7}}}
 function showEditor() {owner.open({editor: editor, cloudId: 7})}
 function hideEditor() {owner.close()}
})",
                          QUrl("qrc:/playlist-editor-test.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"editor", QVariant::fromValue(&editor)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *owner = root->findChild<QObject *>("editOwner");
        QVERIFY(owner);
        QVERIFY(!owner->property("item").value<QObject *>());
        std::function<QQuickItem *(QQuickItem *, const QString &)> findVisual =
            [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (auto *child : item->childItems())
                if (auto *found = findVisual(child, name))
                    return found;
            return nullptr;
        };
        std::function<QPair<int, bool>(QQuickItem *)> iconState = [&](QQuickItem *item) {
            QPair<int, bool> result{0, true};
            if (!item->isVisible())
                return result;
            if (item->objectName() == "iconImage" && !item->property("source").toUrl().isEmpty()) {
                result.first = 1;
                result.second = item->property("status").toInt() == 1;
            }
            for (auto *child : item->childItems()) {
                const auto state = iconState(child);
                result.first += state.first;
                result.second = result.second && state.second;
            }
            return result;
        };
        const auto artifacts = qEnvironmentVariable("ECHO_PLAYLIST_EDIT_ARTIFACTS");
        if (!artifacts.isEmpty())
            QDir().mkpath(artifacts);
        for (auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (double zoom : {1., 1.25})
                for (int viewportWidth : {900, 420}) {
                    window->setWidth(viewportWidth);
                    theme.setMode(mode);
                    QVERIFY(QMetaObject::invokeMethod(root.data(), "showEditor"));
                    QPointer<QObject> popup;
                    QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
                    popup->setProperty("uiScale", zoom);
                    QTRY_VERIFY(popup->property("opened").toBool());
                    QTRY_VERIFY(popup->property("x").toReal() >= 0 &&
                                popup->property("x").toReal() +
                                        popup->property("width").toReal() * zoom <=
                                    viewportWidth + 1);
                    QSignalSpy frame(window, &QQuickWindow::afterAnimating);
                    window->requestUpdate();
                    QTRY_VERIFY(!frame.isEmpty());
                    PlaylistEditModelTestAccess::complete(
                        editor,
                        R"({"user_id":100,"total_revision":9,"page":{"total":1,"items":[{"cloud_list_id":7,"global_id":"collection","title":"我的测试歌单","kind":"owned","source_code":1,"sort_index":3,"description_present":true,"tags_present":true,"tags":"华语,流行","description":"测试简介"}]}})");
                    QVERIFY(editor.loaded());
                    QTRY_VERIFY(iconState(window->contentItem()).first >= 4 &&
                                iconState(window->contentItem()).second);
                    auto *descriptionScroll =
                        findVisual(window->contentItem(), "playlistEditDescriptionScroll");
                    QVERIFY(descriptionScroll);
                    QTRY_VERIFY(descriptionScroll->property("contentHeight").toReal() <=
                                descriptionScroll->height() + 1);
                    if (!artifacts.isEmpty())
                        QVERIFY(window->grabWindow().save(artifacts + QString("/edit-%1-%2-%3.png")
                                                                          .arg(int(mode))
                                                                          .arg(zoom)
                                                                          .arg(viewportWidth)));
                    editor.setDescription(
                        QStringLiteral("很长的歌单简介 Long description\n").repeated(30));
                    QTRY_VERIFY(descriptionScroll->property("contentHeight").toReal() >
                                descriptionScroll->height());
                    QVERIFY(QMetaObject::invokeMethod(root.data(), "hideEditor"));
                    QTRY_VERIFY(popup.isNull());
                    QVERIFY(!editor.loaded());
                }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        api.shutdown();
    }

    void cloudMetadataCompletesEveryFileAndSurvivesPagination()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        SongMetadataService metadata(api);
        RouteController router;
        CloudService cloud(api, playback, metadata, router, false);
        cloud.registerRoutes(router);
        QVERIFY(router.navigate("cloud"));
        CloudServiceTestAccess::enableMetadata(cloud);
        const auto file = [](int id, const QString &songId) {
            return QJsonObject{
                {"song", QJsonObject{{"id", songId},
                                     {"title", "原始歌曲"},
                                     {"hash", QString(32, QChar('a' + id % 5))},
                                     {"duration_ms", 123000},
                                     {"privilege", 0}}},
                {"metadata",
                 QJsonObject{{"source_file_name", QString("文件%1.mp3").arg(id)}, {"opaque", 42}}},
                {"file_id", id},
                {"sort_index", id},
                {"added_at_seconds", 500 - id}};
        };
        const QJsonArray facts{
            QJsonObject{{"id", "1"},
                        {"title", "正式歌曲"},
                        {"artists", QJsonArray{QJsonObject{{"id", 51}, {"name", "正式歌手"}}}},
                        {"album_id", "81"},
                        {"album_title", "正式专辑"},
                        {"cover_url", "https://example.test/1.jpg"},
                        {"hash", "NOT-A-SOURCE"},
                        {"privilege", 10},
                        {"duration_ms", 999000}}};
        CloudServiceTestAccess::acceptPage(
            cloud,
            {{"page", 1},
             {"total", 4},
             {"items", QJsonArray{file(101, "1"), file(102, "1"), file(103, "")}}},
            {});
        QVERIFY(!cloud.loading());
        QCOMPARE(cloud.songs()->count(), 3);
        QVERIFY(CloudServiceTestAccess::metadataTicket(cloud));
        const auto original = cloud.songs()->track("cloud-file:102").value();
        cloud.songs()->toggleSelected("cloud-file:102");
        cloud.songs()->setQuery("正式歌手");
        QCOMPARE(cloud.songs()->count(), 0);
        SongMetadataServiceTestAccess::complete(metadata, {"1"}, facts);
        QTRY_COMPARE(cloud.songs()->count(), 2);
        QCOMPARE(cloud.songs()->selectedCount(), 1);
        cloud.songs()->setQuery({});
        for (const auto &id : {QString("1"), QString("cloud-file:102")}) {
            const auto track = cloud.songs()->track(id).value();
            QCOMPARE(track.album, QString("正式专辑"));
            QCOMPARE(track.artistLinks()[0].toMap()["id"].toString(), QString("51"));
            QCOMPARE(track.catalogPrivilege, quint32(0));
        }
        const auto completed = cloud.songs()->track("cloud-file:102").value();
        QCOMPARE(completed.hash, original.hash);
        QCOMPARE(completed.durationMs, original.durationMs);
        QCOMPARE(completed.catalogMetadata["source_file_name"],
                 original.catalogMetadata["source_file_name"]);
        QCOMPARE(completed.catalogMetadata["opaque"].toInt(), 42);
        QCOMPARE(CloudServiceTestAccess::file(cloud, 102)["sort_index"].toInt(), 102);
        QVERIFY(cloud.songs()->track("cloud-file:103")->album.isEmpty());
        // A repeated song on a later page consumes shared cached facts, while
        // the first two files retain their completion through re-projection.
        CloudServiceTestAccess::acceptPage(
            cloud, {{"page", 2}, {"total", 4}, {"items", QJsonArray{file(104, "1")}}}, {});
        QVERIFY(!cloud.loading());
        QCOMPARE(cloud.songs()->count(), 4);
        QTRY_COMPARE(cloud.songs()->track("cloud-file:104")->album, QString("正式专辑"));
        QCOMPARE(cloud.songs()->track("cloud-file:102")->album, QString("正式专辑"));
        QCOMPARE(cloud.songs()->selectedCount(), 1);
        QCOMPARE(cloud.songs()->item(0)["resourceId"].toString(), QString("1"));
        cloud.actions()->appendSong("cloud-file:102", false);
        QCOMPARE(playback.queue()->tracks().last().hash, original.hash);
        QCOMPARE(playback.queue()->tracks().last().album, QString("正式专辑"));
        QVERIFY(cloud.removeSongs({"cloud-file:102"}));
        const auto targets = CloudServiceTestAccess::deleteTargets(cloud);
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets[0].kvId, quint64(102));
        QCOMPARE(targets[0].albumAudioId, quint64(1));
        cloud.shutdown();
        metadata.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void cloudMetadataRefreshRouteAndAccountScopes()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs);
        SongMetadataService metadata(api);
        RouteController router;
        CloudService cloud(api, playback, metadata, router, false);
        cloud.registerRoutes(router);
        QVERIFY(router.registerPage({"other",
                                     "/main/other",
                                     QUrl("qrc:/not-instantiated.qml"),
                                     {},
                                     true,
                                     true,
                                     false,
                                     {}}));
        QVERIFY(router.navigate("cloud"));
        CloudServiceTestAccess::enableMetadata(cloud);
        const QJsonObject page{
            {"page", 1},
            {"total", 1},
            {"items",
             QJsonArray{QJsonObject{{"file_id", 101},
                                    {"song", QJsonObject{{"id", "1"}, {"title", "Song"}}}}}}};
        const QJsonArray facts{
            QJsonObject{{"id", "1"}, {"album_id", "8"}, {"album_title", "新专辑"}}};
        CloudServiceTestAccess::acceptPage(cloud, page, {});
        const auto retired = CloudServiceTestAccess::metadataTicket(cloud);
        QVERIFY(retired);
        cloud.refresh();
        CloudServiceTestAccess::metadata(cloud, retired, facts);
        QVERIFY(cloud.songs()->track("1")->album.isEmpty());
        CloudServiceTestAccess::acceptPage(cloud, page, {});
        // Leave cancels both the page read and optional consumers.
        QVERIFY(router.navigate("other"));
        QCOMPARE(CloudServiceTestAccess::metadataTicket(cloud), quint64(0));
        SongMetadataServiceTestAccess::complete(metadata, {"1"}, facts);
        QVERIFY(cloud.songs()->track("1")->album.isEmpty());
        router.back();
        QTRY_COMPARE(cloud.songs()->track("1")->album, QString("新专辑"));
        CloudServiceTestAccess::acceptPage(cloud, page, {});
        const auto oldSession = CloudServiceTestAccess::metadataTicket(cloud);
        QVERIFY(oldSession);
        QVERIFY(api.setAccount(200, "other-offline-domain-fixture"));
        CloudServiceTestAccess::metadata(cloud, oldSession, facts);
        QCOMPARE(cloud.songs()->count(), 0);
        QCOMPARE(CloudServiceTestAccess::metadataTicket(cloud), quint64(0));
        cloud.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }

    void favoritesCompletesMetadataWithoutBlockingOrLosingPageFacts()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        SongMetadataService metadata(api);
        RouteController router;
        FavoritesService favorites(api, playback, library, order, metadata, router, false);
        favorites.registerRoutes(router);
        UserPlaylistModelTestAccess::seed(library, api, "我喜欢的音乐", 1, 0, 7);
        QVERIFY(router.navigate("favorites"));
        FavoritesServiceTestAccess::enablePaging(favorites);
        const auto entry = [](int id, int position) {
            return QJsonObject{
                {"song", QJsonObject{{"id", QString::number(id)},
                                     {"hash", QString(32, QChar('0' + id))},
                                     {"title", QString("Song %1").arg(id)},
                                     {"artists", QJsonArray{QJsonObject{{"name", "临时歌手"}}}},
                                     {"duration_ms", 123000},
                                     {"privilege", 0}}},
                {"metadata", QJsonObject{{"source_file_name", QString("原始 %1.mp3").arg(id)},
                                         {"filename_artist_name", "临时歌手"},
                                         {"opaque", QJsonObject{{"provider_fact", 19}}}}},
                {"file_id", 1000 + id},
                {"sort_index", position}};
        };
        const auto presentation = [](int id) {
            return QJsonObject{
                {"id", QString::number(id)},
                {"title", QString("Song %1").arg(id)},
                {"artists", QJsonArray{QJsonObject{{"id", 500 + id},
                                                   {"name", QString("正式歌手%1").arg(id)}}}},
                {"album_id", QString::number(8000 + id)},
                {"album_title", QString("正式专辑%1").arg(id)},
                {"cover_url", QString("https://example.test/%1.jpg").arg(id)},
                {"hash", "MUST-NOT-REPLACE"},
                {"privilege", 10},
                {"duration_ms", 999000}};
        };
        FavoritesServiceTestAccess::acceptSongs(favorites, {entry(4, 3), entry(3, 2)}, 4);
        const auto nextPage = FavoritesServiceTestAccess::songRequest(favorites);
        QVERIFY(nextPage);
        QVERIFY(FavoritesServiceTestAccess::metadataTicket(favorites));
        const auto original = favorites.songs()->track("3").value();
        favorites.songs()->toggleSelected("3");
        favorites.songs()->setQuery("正式歌手3");
        QCOMPARE(favorites.songs()->count(), 0);
        SongMetadataServiceTestAccess::complete(metadata, {"3", "4"}, {presentation(3)});
        QTRY_COMPARE(favorites.songs()->count(), 1);
        QCOMPARE(favorites.songs()->track("3")->album, QString("正式专辑3"));
        QCOMPARE(favorites.songs()->selectedCount(), 1);
        favorites.songs()->setQuery({});
        api.cancel(nextPage);
        FavoritesServiceTestAccess::acceptSongs(favorites, {entry(2, 1), entry(1, 0)}, 4, nextPage);
        QVERIFY(FavoritesServiceTestAccess::complete(favorites));
        QVERIFY(!favorites.loading()); // Optional completion is not pagination/loading.
        QVERIFY(FavoritesServiceTestAccess::metadataTicket(favorites));
        QCOMPARE(favorites.songs()->count(), 4);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(favorites.songs()->item(i)["resourceId"].toString(), QString::number(i + 1));
        QCOMPARE(favorites.songs()->track("3")->album, QString("正式专辑3"));
        SongMetadataServiceTestAccess::complete(metadata, {"1", "2"},
                                                {presentation(2), presentation(1)});
        QTRY_COMPARE(FavoritesServiceTestAccess::metadataTicket(favorites), quint64(0));
        const auto completed = favorites.songs()->track("3").value();
        QCOMPARE(completed.hash, original.hash);
        QCOMPARE(completed.durationMs, original.durationMs);
        QCOMPARE(completed.catalogPrivilege, original.catalogPrivilege);
        QCOMPARE(completed.catalogMetadata["source_file_name"],
                 original.catalogMetadata["source_file_name"]);
        QCOMPARE(completed.catalogMetadata["opaque"], original.catalogMetadata["opaque"]);
        QCOMPARE(completed.artistLinks()[0].toMap()["id"].toString(), QString("503"));
        QCOMPARE(favorites.songs()->selectedCount(), 1);
        const auto source = FavoritesServiceTestAccess::sourceEntry(favorites, "3");
        QCOMPARE(source["song"].toObject()["album_title"].toString(), QString("正式专辑3"));
        QCOMPARE(source["file_id"].toInt(), 1003);
        QCOMPARE(source["sort_index"].toInt(), 2);
        QCOMPARE(library.data(library.index(0), UserPlaylistModel::CoverUrlRole).toString(),
                 QString("https://example.test/1.jpg")); // Global position, not arrival order.
        favorites.actions()->appendSong("3", false);
        const auto queued = playback.queue()->tracks().last();
        QCOMPARE(queued.album, completed.album);
        QCOMPARE(queued.hash, original.hash);
        QCOMPARE(queued.catalogMetadata["source_file_name"],
                 original.catalogMetadata["source_file_name"]);
        favorites.shutdown();
        metadata.shutdown();
        router.shutdown();
        order.close();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void favoritesMetadataRefreshAccountAndCacheScopes()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(1));
        QVERIFY(api.setAccount(100, "offline-domain-fixture"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        SongMetadataService metadata(api);
        RouteController router;
        FavoritesService favorites(api, playback, library, order, metadata, router, false);
        favorites.registerRoutes(router);
        QVERIFY(router.registerPage({"other",
                                     "/main/other",
                                     QUrl("qrc:/not-instantiated.qml"),
                                     {},
                                     true,
                                     true,
                                     false,
                                     {}}));
        UserPlaylistModelTestAccess::seed(library, api, "我喜欢的音乐", 1, 0, 7);
        QVERIFY(router.navigate("favorites"));
        FavoritesServiceTestAccess::enablePaging(favorites);
        const QJsonObject row{
            {"song", QJsonObject{{"id", "1"}, {"title", "Song"}, {"hash", QString(32, 'a')}}},
            {"metadata", QJsonObject{{"source_file_name", "原始.mp3"}}},
            {"sort_index", 0}};
        const QJsonArray facts{
            QJsonObject{{"id", "1"},
                        {"album_id", "8"},
                        {"album_title", "新专辑"},
                        {"artists", QJsonArray{QJsonObject{{"id", 5}, {"name", "歌手"}}}},
                        {"cover_url", "https://example.test/new.jpg"}}};
        FavoritesServiceTestAccess::acceptSongs(favorites, {row}, 1);
        const auto retired = FavoritesServiceTestAccess::metadataTicket(favorites);
        QVERIFY(retired);
        favorites.refresh();
        const auto request = FavoritesServiceTestAccess::songRequest(favorites);
        QVERIFY(request);
        api.cancel(request);
        FavoritesServiceTestAccess::acceptSongs(favorites, {row}, 1, request);
        QVERIFY(FavoritesServiceTestAccess::metadataTicket(favorites) != retired);
        FavoritesServiceTestAccess::metadata(favorites, retired, facts);
        QVERIFY(favorites.songs()->track("1")->cover.isEmpty());
        QVERIFY(router.navigate("other"));
        QCOMPARE(FavoritesServiceTestAccess::metadataTicket(favorites), quint64(0));
        SongMetadataServiceTestAccess::complete(metadata, {"1"}, facts);
        QVERIFY(favorites.songs()->track("1")->cover.isEmpty());
        router.back();
        QTRY_COMPARE(favorites.songs()->track("1")->album, QString("新专辑"));
        favorites.refresh();
        const auto refreshed = FavoritesServiceTestAccess::songRequest(favorites);
        api.cancel(refreshed);
        FavoritesServiceTestAccess::acceptSongs(favorites, {row}, 1, refreshed);
        const auto oldSession = FavoritesServiceTestAccess::metadataTicket(favorites);
        QVERIFY(oldSession);
        QVERIFY(api.setAccount(200, "other-offline-domain-fixture"));
        FavoritesServiceTestAccess::metadata(favorites, oldSession, facts);
        QCOMPARE(favorites.songs()->count(), 0);
        QCOMPARE(FavoritesServiceTestAccess::metadataTicket(favorites), quint64(0));
        favorites.shutdown();
        metadata.shutdown();
        router.shutdown();
        order.close();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }

    void cloudUploadUnreadableTagsFallBackToFilename()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("Artist - Song.mp3");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("not decodable audio"), qint64(19));
        file.close();
        QTcpServer sink;
        QVERIFY(sink.listen(QHostAddress::LocalHost));
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(123, "local-fixture-token"));
        CloudUploadService upload(api, true);
        upload.selectFiles({QUrl::fromLocalFile(path)}, true);
        QTRY_COMPARE_WITH_TIMEOUT(upload.step(), QString("manual"), 5000);
        QCOMPARE(upload.total(), 1);
        QCOMPARE(upload.failed(), 0);
        QCOMPARE(upload.data(upload.index(0), CloudUploadService::TitleRole).toString(),
                 QString("Song"));
        QCOMPARE(upload.data(upload.index(0), CloudUploadService::ArtistRole).toString(),
                 QString("Artist"));
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        QVERIFY(!sink.hasPendingConnections());
    }
    void cloudUploadLookupLimiterAndAccountIsolation()
    {
        QTcpServer sink;
        QVERIFY(sink.listen(QHostAddress::LocalHost));
        ApiClient api;
        QVERIFY(api.setNetworkTransport(sink.serverPort()));
        QVERIFY(api.setAccount(123, "local-fixture-token"));
        CloudUploadService upload(api, true);
        CloudUploadServiceTestAccess::seed(upload, 1);
        const QJsonObject candidate{{"title", "Song"},
                                    {"hash", "0123456789abcdef0123456789abcdef"},
                                    {"audio_id", 20505418},
                                    {"id", "32100650"},
                                    {"artists", QJsonArray{QJsonObject{{"name", "Artist"}}}}};
        const QJsonObject response{{"page", QJsonObject{{"items", QJsonArray{candidate}}}}};
        upload.searchManual("Song", "Artist");
        QTRY_VERIFY_WITH_TIMEOUT(CloudUploadServiceTestAccess::request(upload) != 0, 1000);
        const auto first = CloudUploadServiceTestAccess::lastLookup(upload);
        CloudUploadServiceTestAccess::complete(upload, response);
        QCOMPARE(upload.candidates().size(), 1);
        upload.searchManual("Song", "Artist");
        QTRY_VERIFY(!upload.searching());
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        QCOMPARE(CloudUploadServiceTestAccess::lastLookup(upload), first);
        upload.searchManual("Another song", "Artist");
        QTest::qWait(500);
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        QTRY_VERIFY_WITH_TIMEOUT(CloudUploadServiceTestAccess::request(upload) != 0, 2500);
        QVERIFY(CloudUploadServiceTestAccess::lastLookup(upload) - first >= 2000);
        CloudUploadServiceTestAccess::fail(upload, {{"kind", "http"},
                                                    {"code", 429},
                                                    {"retry_after_seconds", 90},
                                                    {"message", "rate limited"}});
        QVERIFY(upload.paused());
        QVERIFY(!upload.canResume());
        upload.resume();
        QTest::qWait(100);
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        upload.stop();
        CloudUploadServiceTestAccess::seed(upload, 1);
        upload.searchManual("New batch song", "Artist");
        QTest::qWait(100);
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        QVERIFY(upload.searching());
        api.advanceSession();
        QVERIFY(!upload.busy());
        QCOMPARE(upload.total(), 0);
        QVERIFY(upload.candidates().isEmpty());
    }
    void cloudUploadUnknownWriteNeverReplays()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath("song.wav"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(1152044, 'x'));
        file.close();
        QTcpServer sink;
        QVERIFY(sink.listen(QHostAddress::LocalHost));
        ApiClient api;
        api.setNetworkTransport(sink.serverPort());
        api.setAccount(123, "local-fixture-token");
        CloudUploadService upload(api, true);
        CloudUploadServiceTestAccess::seed(upload, 2, "uploading", file.fileName());
        CloudUploadServiceTestAccess::advance(upload);
        QVERIFY(CloudUploadServiceTestAccess::request(upload));
        CloudUploadServiceTestAccess::complete(upload, {{"authorization", "test-proof"}});
        CloudUploadServiceTestAccess::complete(
            upload, {{"upload_id", "test-session"}, {"external_host", "https://bss.kugou.com"}});
        CloudUploadServiceTestAccess::complete(upload,
                                               {{"partnumber", 1}, {"total_length", 1048576}});
        CloudUploadServiceTestAccess::complete(upload,
                                               {{"partnumber", 2}, {"total_length", 1152044}});
        CloudUploadServiceTestAccess::complete(
            upload, {{"x-bss-filename", "0123456789abcdef0123456789abcdef"}});
        CloudUploadServiceTestAccess::fail(upload, {{"kind", "transport"}, {"message", "timeout"}});
        QVERIFY(upload.paused());
        QCOMPARE(upload.failed(), 1);
        QCOMPARE(CloudUploadServiceTestAccess::position(upload), 1);
        upload.resume();
        QVERIFY(CloudUploadServiceTestAccess::request(upload));
        QCOMPARE(CloudUploadServiceTestAccess::position(upload), 1);
        upload.stop();
        QVERIFY(!upload.busy());
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        QTest::qWait(100);
        QCOMPARE(upload.succeeded(), 0);
    }

    void cloudUploadVerificationCompletesWithoutReplay()
    {
        QTcpServer sink;
        QVERIFY(sink.listen(QHostAddress::LocalHost));
        ApiClient api;
        api.setNetworkTransport(sink.serverPort());
        api.setAccount(123, "local-fixture-token");
        CloudUploadService upload(api, true);
        CloudUploadServiceTestAccess::seed(upload, 1);
        upload.searchManual("Song", "Artist");
        QTRY_VERIFY(CloudUploadServiceTestAccess::request(upload));
        const auto id = CloudUploadServiceTestAccess::request(upload);
        ApiClientTestAccess::challenge(api, id);
        QVERIFY(upload.paused());
        QVERIFY(!upload.canResume());
        ApiClientTestAccess::verified(api);
        QVERIFY(upload.paused());
        QVERIFY(!ApiClientTestAccess::replayExists(api, id));
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        upload.stop();
    }
    void cloudUploadCancellationDuringVerificationStaysCancelled()
    {
        QTcpServer sink;
        QVERIFY(sink.listen(QHostAddress::LocalHost));
        ApiClient api;
        api.setNetworkTransport(sink.serverPort());
        api.setAccount(123, "local-fixture-token");
        CloudUploadService upload(api, true);
        CloudUploadServiceTestAccess::seed(upload, 1, "uploading");
        CloudUploadServiceTestAccess::auth(upload);
        const auto id = CloudUploadServiceTestAccess::request(upload);
        QVERIFY(id);
        ApiClientTestAccess::challenge(api, id);
        QVERIFY(upload.paused());
        upload.stop();
        QVERIFY(!upload.busy());
        QCOMPARE(upload.failed(), 0);
        QCOMPARE(upload.data(upload.index(0), CloudUploadService::StateRole).toString(),
                 QString("cancelled"));
    }
    void cloudUploadVerifiedFileWaitsForExplicitContinue()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath("test.wav"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
        QTcpServer sink;
        QVERIFY(sink.listen(QHostAddress::LocalHost));
        ApiClient api;
        api.setNetworkTransport(sink.serverPort());
        api.setAccount(123, "local-fixture-token");
        CloudUploadService upload(api, true);
        CloudUploadServiceTestAccess::seed(upload, 1, "uploading", file.fileName());
        CloudUploadServiceTestAccess::auth(upload);
        const auto id = CloudUploadServiceTestAccess::request(upload);
        QVERIFY(id);
        ApiClientTestAccess::challenge(api, id);
        ApiClientTestAccess::verified(api);
        QVERIFY(upload.paused());
        QCOMPARE(upload.failed(), 0);
        QCOMPARE(CloudUploadServiceTestAccess::position(upload), 0);
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        QVERIFY(!ApiClientTestAccess::replayExists(api, id));
        QVERIFY(!upload.canResume());
        upload.resume();
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        CloudUploadServiceTestAccess::expireCooldown(upload);
        upload.resume();
        QVERIFY(CloudUploadServiceTestAccess::request(upload) != 0);
        QVERIFY(CloudUploadServiceTestAccess::request(upload) != id);
        upload.stop();
    }
    void cloudUploadHashMatchFillsOnlyMissingFacts()
    {
        ApiClient api;
        CloudUploadService upload(api, false);
        CloudUploadServiceTestAccess::seed(upload, 1);
        const auto name = upload.data(upload.index(0), CloudUploadService::FileNameRole);
        CloudUploadServiceTestAccess::matchHash(upload, false);
        QCOMPARE(upload.data(upload.index(0), CloudUploadService::ArtistRole).toString(),
                 QString("歌手 Artist"));
        CloudUploadServiceTestAccess::matchHash(upload, true);
        QCOMPARE(upload.data(upload.index(0), CloudUploadService::ArtistRole).toString(),
                 QString("Matched Artist"));
        QCOMPARE(CloudUploadServiceTestAccess::standardHash(upload),
                 QString("abcdef0123456789abcdef0123456789ab"));
        QCOMPARE(upload.data(upload.index(0), CloudUploadService::FileNameRole), name);
        QVERIFY(upload.data(upload.index(0), CloudUploadService::MatchedRole).toBool());
    }
    void cloudUploadPartsStayInHostAndRespectSnapshot()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("part.wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes(CloudUploadFile::PartBytes + 13, 'x');
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        const CloudUploadFile::Snapshot snapshot{
            path, bytes.size(), QFileInfo(path).lastModified().toMSecsSinceEpoch()};
        std::atomic_bool canceled{false};
        auto first = CloudUploadFile::readPart(snapshot, 1, canceled);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QCOMPARE(QByteArray::fromBase64(first.encoded), bytes.left(CloudUploadFile::PartBytes));
        QCOMPARE(QByteArray::fromBase64(CloudUploadFile::readPart(snapshot, 2, canceled).encoded),
                 bytes.right(13));
        QVERIFY(!CloudUploadFile::readPart(snapshot, 0, canceled).error.isEmpty());
        QVERIFY(!CloudUploadFile::readPart(snapshot, 3, canceled).error.isEmpty());
        canceled.store(true);
        QVERIFY(CloudUploadFile::readPart(snapshot, 1, canceled).encoded.isEmpty());
        canceled.store(false);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("changed");
        file.close();
        QVERIFY(!CloudUploadFile::readPart(snapshot, 1, canceled).error.isEmpty());
    }
    void cloudUploadBinaryPartReachesApiWithoutLocalPath()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("part.wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray payload(1024 * 1024, 'x');
        QCOMPARE(file.write(payload), payload.size());
        file.close();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray request;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, &server,
                    [&, socket] { request += socket->readAll(); });
        });
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setNetworkTransport(server.serverPort()));
        QVERIFY(api.initializeRuntime());
        QVERIFY(api.setAccount(123, "fixture-token"));
        CloudUploadService upload(api, true);
        CloudUploadServiceTestAccess::seed(upload, 1, "uploading", path);
        CloudUploadServiceTestAccess::readPart(upload);
        QTRY_VERIFY_WITH_TIMEOUT(request.contains("\r\n\r\n") &&
                                     request.size() - request.indexOf("\r\n\r\n") - 4 >=
                                         payload.size(),
                                 10000);
        QCOMPARE(request.mid(request.indexOf("\r\n\r\n") + 4), payload);
        QVERIFY(request.startsWith("POST "));
        QVERIFY(!request.left(request.indexOf("\r\n\r\n")).contains(path.toUtf8()));
        upload.stop();
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
    }
    void cloudUploadPartCancellationAndAccountIsolation()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("part.wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(1024 * 1024, 'x'));
        file.close();
        ApiClient api;
        CloudUploadService upload(api, false);
        CloudUploadServiceTestAccess::seed(upload, 1, "uploading", path);
        CloudUploadServiceTestAccess::readPart(upload);
        upload.stop();
        QTRY_VERIFY(!CloudUploadServiceTestAccess::reading(upload));
        QTest::qWait(30);
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
        QCOMPARE(upload.succeeded(), 0);
        CloudUploadServiceTestAccess::seed(upload, 1, "uploading", path);
        CloudUploadServiceTestAccess::readPart(upload);
        api.setAccount(123, "fixture-session");
        QTRY_VERIFY(!CloudUploadServiceTestAccess::reading(upload));
        QTest::qWait(30);
        QCOMPARE(upload.total(), 0);
        QCOMPARE(CloudUploadServiceTestAccess::request(upload), quint64(0));
    }
    void cloudUploadSuccessDoesNotRequireOptionalRows()
    {
        ApiClient api;
        CloudUploadService upload(api, false);
        CloudUploadServiceTestAccess::seed(upload, 1, "uploading");
        CloudUploadServiceTestAccess::acknowledge(upload);
        QCOMPARE(upload.succeeded(), 1);
        QTRY_COMPARE(upload.phase(), QString("done"));
    }
    void cloudUploadMatchingRejectsUnrelatedVersions()
    {
        const auto song = [](QString title, QString artist) {
            return QJsonObject{{"title", title},
                               {"artists", QJsonArray{QJsonObject{{"name", artist}}}}};
        };
        QVERIFY(CloudUploadServiceTestAccess::acceptable("Song (Live)", "Artist",
                                                         song("Song", "Artist")));
        QVERIFY(!CloudUploadServiceTestAccess::acceptable(
            "Song", "Artist", song("Song extended completely unrelated version", "Someone")));
        QVERIFY(!CloudUploadServiceTestAccess::acceptable("Song", "Artist",
                                                          song("Another work", "Artist")));
    }

    void cloudUploadProductionDialogLifecycle()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api;
        api.setAccount(123, "local-fixture-token");
        CloudUploadService upload(api, false);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &e : errors)
                warnings << e.toString();
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Cloud
import EchoMusic.Controls
import EchoMusic.Theme
ApplicationWindow {
 id: window
 required property CloudUploadService service
 visible: true; width: 900; height: 740
 Rectangle {anchors.fill: parent; color: Theme.main}
 EchoPopupLoader {id: owner; objectName: "uploadOwner"; hostItem: window.contentItem; sourceComponent: Component {CloudUploadDialog {service: window.service}}}
 function showUpload() {owner.open()}
 function hideUpload() {owner.close()}
 Connections {
  target: owner.item
  function onClosed() {console.info("upload surface closed",window.visible,window.visibility,window.active,window.contentItem.enabled,window.service.phase)}
 }
 onVisibleChanged: console.info("upload window visible",visible)
})",
                          QUrl("qrc:/cloud-upload-production.qml"));
        QScopedPointer<QObject> root(
            component.createWithInitialProperties({{"service", QVariant::fromValue(&upload)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *owner = root->findChild<QObject *>("uploadOwner");
        QVERIFY(owner);
        QVERIFY(!owner->property("item").value<QObject *>());
        const auto artifacts = qEnvironmentVariable("ECHO_CLOUD_UPLOAD_ARTIFACTS");
        if (!artifacts.isEmpty())
            QDir().mkpath(artifacts);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (const double zoom : {1., 1.25}) {
                theme.setMode(mode);
                QVERIFY(QMetaObject::invokeMethod(root.data(), "showUpload"));
                QPointer<QObject> popup;
                QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
                popup->setProperty("uiScale", zoom);
                QTRY_VERIFY(popup && popup->property("opened").toBool());
                if (mode == ThemeStore::Light && zoom == 1.) {
                    for (const auto name : {"cloudUploadFileOwner", "cloudUploadFolderOwner"}) {
                        window->raise();
                        window->requestActivate();
                        QVERIFY(QTest::qWaitForWindowActive(window));
                        QVERIFY2(popup,
                                 "Upload popup closed while restoring the native picker host");
                        QPointer<QObject> pickerOwner = popup->findChild<QObject *>(name);
                        QVERIFY(pickerOwner);
                        QVERIFY(!pickerOwner->property("item").value<QObject *>());
                        for (int cycle = 0; cycle < 2; ++cycle) {
                            QVERIFY2(popup && pickerOwner,
                                     "Upload popup was disposed between native picker cycles");
                            QVERIFY(QMetaObject::invokeMethod(pickerOwner, "open"));
                            QPointer<QObject> picker;
                            QTRY_VERIFY(
                                (picker = pickerOwner->property("item").value<QObject *>()));
                            QTRY_VERIFY(picker && picker->property("visible").toBool());
                            QPointer<QObject> released = picker;
                            QVERIFY(QMetaObject::invokeMethod(pickerOwner, "close"));
                            QTRY_VERIFY(released.isNull());
                            QVERIFY2(popup,
                                     "Upload popup closed when its native picker was cancelled");
                        }
                    }
                }
                QTest::qWait(100);
                QVERIFY(popup);
                QVERIFY(popup->property("height").toReal() <= 560);
                QVERIFY(popup->property("width").toReal() <= 460);
                if (!artifacts.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        artifacts + QString("/pick-%1-%2.png").arg(int(mode)).arg(zoom)));
                QPointer<QObject> gone = popup;
                QVERIFY(QMetaObject::invokeMethod(root.data(), "hideUpload"));
                QTRY_VERIFY(gone.isNull());
            }
        CloudUploadServiceTestAccess::seed(upload, 10000, "matching");
        QVERIFY(QMetaObject::invokeMethod(root.data(), "showUpload"));
        QPointer<QObject> popup;
        QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
        QTRY_VERIFY(popup && popup->property("opened").toBool());
        QTest::qWait(100);
        std::function<QQuickItem *(QQuickItem *, QString)> find = [&](QQuickItem *i,
                                                                      QString n) -> QQuickItem * {
            if (i->objectName() == n)
                return i;
            for (auto *c : i->childItems())
                if (auto *v = find(c, n))
                    return v;
            return nullptr;
        };
        auto *list = find(window->contentItem(), "cloudUploadFiles");
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 10000);
        std::function<int(QQuickItem *)> rows = [](QQuickItem *) { return 0; };
        rows = [&](QQuickItem *item) {
            int count = item->property("uploadState").isValid() ? 1 : 0;
            for (auto *child : item->childItems())
                count += rows(child);
            return count;
        };
        const auto delegates = rows(list);
        QVERIFY(delegates > 0 && delegates < 32);
        qInfo() << "Cloud upload model rows=10000 instantiated delegates=" << delegates;
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/progress-virtualized.png"));
        QVERIFY(popup);
        auto *backgroundOwner = popup->findChild<QObject *>("cloudUploadBackgroundOwner");
        QVERIFY(backgroundOwner);
        QVERIFY(!backgroundOwner->property("item").value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(popup, "requestDismiss"));
        QPointer<QObject> background;
        QTRY_VERIFY((background = backgroundOwner->property("item").value<QObject *>()));
        QTRY_VERIFY(background && background->property("opened").toBool());
        QTest::qWait(100); // Allow the nested popup to polish before pointer input.
        auto *check = find(window->contentItem(), "cloudUploadReminderCheck"),
             *label = find(window->contentItem(), "cloudUploadReminderLabel");
        QVERIFY(check && label);
        QTRY_VERIFY(label->width() > 100 && label->height() >= 18);
        QCOMPARE(check->width(), 18.);
        QCOMPARE(label->mapToScene({0, 0}).x() - check->mapToScene({check->width(), 0}).x(), 8.);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/background-before-label.png"));
        qInfo() << "Reminder geometry" << check->size() << label->size()
                << label->mapToScene({label->width() / 2, label->height() / 2})
                << label->isVisible() << label->isEnabled();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          label->mapToScene({label->width() / 2, label->height() / 2}).toPoint());
        QVERIFY(popup->property("backgroundDraft").toBool());
        QPointer<QObject> nested = background;
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/background-confirm.png"));
        QPointer<QObject> gone = popup;
        QVERIFY(QMetaObject::invokeMethod(root.data(), "hideUpload"));
        QTRY_VERIFY(gone.isNull());
        QTRY_VERIFY(nested.isNull());
        QVERIFY(upload.busy());
        QCOMPARE(upload.total(), 10000);
        upload.stop();
        QVERIFY(!upload.busy());
        upload.reset();
        CloudUploadServiceTestAccess::seed(upload, 1);
        CloudUploadServiceTestAccess::candidates(upload);
        QVERIFY(QMetaObject::invokeMethod(root.data(), "showUpload"));
        QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
        QTRY_VERIFY(popup && popup->property("opened").toBool());
        auto *form = find(window->contentItem(), "cloudUploadManualForm");
        QVERIFY(form);
        form->setProperty("querySignature", upload.manualFile().value("title").toString() + "\n" +
                                                upload.manualFile().value("artist").toString());
        auto *results = find(window->contentItem(), "cloudUploadResults");
        QVERIFY(results);
        QTRY_VERIFY(results->isVisible());
        QCOMPARE(results->property("count").toInt(), 1);
        QTest::qWait(100);
        if (!artifacts.isEmpty())
            QVERIFY(window->grabWindow().save(artifacts + "/manual-results.png"));
        gone = popup;
        QVERIFY(QMetaObject::invokeMethod(root.data(), "hideUpload"));
        QTRY_VERIFY(gone.isNull());
        QVERIFY(QMetaObject::invokeMethod(root.data(), "showUpload"));
        QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
        QTRY_VERIFY(popup && popup->property("opened").toBool());
        api.advanceSession();
        gone = popup;
        QTRY_VERIFY(gone.isNull());
        QVERIFY(!upload.busy());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
    }

    void liveCloudUploadThroughProductionService()
    {
        if (!qEnvironmentVariableIsSet("ECHO_LIVE_CLOUD_UPLOAD"))
            QSKIP("Opt-in authorized generated-file upload with cleanup");
        QJsonObject snapshot;
        const QString connection = "cloud-upload-read-only-oracle";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(QDir::homePath() +
                               "/Library/Application Support/echo-music/echomusic.sqlite");
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open() && db.transaction());
            QSqlQuery query(db);
            query.prepare("SELECT value_json FROM app_kv WHERE key=?");
            for (const auto &key : {"pinia:user", "pinia:device"}) {
                query.bindValue(0, key);
                QVERIFY(query.exec() && query.next());
                snapshot.insert(
                    key,
                    QJsonDocument::fromJson(query.value(0).toByteArray()).object().value("info"));
                query.finish();
            }
            db.rollback();
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        const auto user = snapshot.value("pinia:user").toObject(),
                   device = snapshot.value("pinia:device").toObject();
        DeviceIdentity identity{device.value("mid").toVariant().toString(),
                                device.value("dfid").toString(),
                                device.value("guid").toString(),
                                device.value("mac").toString(),
                                "EchoMusicNext",
                                device.value("uuid").toString(),
                                device.value("renderFingerprint").toString()};
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(identity);
        QVERIFY(api.setAccount(user.value("userid").toVariant().toULongLong(),
                               user.value("token").toString(), user.value("t1").toString()));
        QVERIFY(api.setNetworkTimeout(60));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        auto request = [&](std::function<quint64()> submit) -> QJsonObject {
            QSignalSpy ok(&api, &ApiClient::succeeded), bad(&api, &ApiClient::failed);
            const auto id = submit();
            if (!id)
                return {};
            QElapsedTimer deadline;
            deadline.start();
            while (deadline.elapsed() < 65000) {
                for (const auto &row : ok)
                    if (row[0].toULongLong() == id)
                        return QJsonDocument::fromJson(row[1].toByteArray()).object();
                for (const auto &row : bad)
                    if (row[0].toULongLong() == id)
                        return {};
                QTest::qWait(10);
            }
            api.cancel(id);
            return {};
        };
        QByteArray wav(44 + 48000 * 4 * 6, '\0');
        memcpy(wav.data(), "RIFF", 4);
        qToLittleEndian<quint32>(quint32(wav.size() - 8), wav.data() + 4);
        memcpy(wav.data() + 8, "WAVEfmt ", 8);
        qToLittleEndian<quint32>(16, wav.data() + 16);
        qToLittleEndian<quint16>(1, wav.data() + 20);
        qToLittleEndian<quint16>(2, wav.data() + 22);
        qToLittleEndian<quint32>(48000, wav.data() + 24);
        qToLittleEndian<quint32>(192000, wav.data() + 28);
        qToLittleEndian<quint16>(4, wav.data() + 32);
        qToLittleEndian<quint16>(16, wav.data() + 34);
        memcpy(wav.data() + 36, "data", 4);
        qToLittleEndian<quint32>(quint32(wav.size() - 44), wav.data() + 40);
        const auto unique = QUuid::createUuid().toRfc4122();
        memcpy(wav.data() + wav.size() - 16, unique.constData(), 16);
        const QString hash =
            QString::fromLatin1(QCryptographicHash::hash(wav, QCryptographicHash::Md5).toHex());
        const QString title = "EchoMusicNextUpload " + hash.left(8);
        const auto path = dir.filePath(title + ".wav");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(wav), wav.size());
        file.close();
        wav.clear();
        bool cleaned = false;
        auto cleanup = [&] {
            QTest::qWait(2100);
            const auto list = request([&] { return api.userCloud(1, 100); });
            if (list.isEmpty())
                return false;
            QList<ApiClient::CloudDeleteTarget> targets;
            for (const auto &v : list.value("page").toObject().value("items").toArray()) {
                const auto row = v.toObject();

                if (row.value("source_file_name").toString() == title &&
                    row.value("song").toObject().value("hash").toString().compare(
                        hash, Qt::CaseInsensitive) == 0)
                    targets.append(
                        {quint64(row.value("file_id").toInteger()),
                         row.value("song").toObject().value("id").toVariant().toULongLong()});
            }
            if (!targets.isEmpty()) {
                QTest::qWait(2100);
                if (request([&] { return api.userCloudDelete(targets); }).isEmpty())
                    return false;
            }
            QTest::qWait(2100);
            const auto final = request([&] { return api.userCloud(1, 100); });
            if (final.isEmpty())
                return false;
            for (const auto &v : final.value("page").toObject().value("items").toArray()) {
                const auto row = v.toObject();
                if (row.value("source_file_name").toString() == title &&
                    row.value("song").toObject().value("hash").toString().compare(
                        hash, Qt::CaseInsensitive) == 0)
                    return false;
            }
            return true;
        };
        const auto guard = qScopeGuard([&] {
            if (!cleaned && !cleanup())
                QTest::qFail("Generated upload cleanup could not be confirmed", __FILE__, __LINE__);
        });
        CloudUploadService upload(api, true);
        QList<qint64> lookups;
        connect(&upload, &CloudUploadService::changed, this, [&] {
            const auto t = CloudUploadServiceTestAccess::lastLookup(upload);
            if (t >= 0 && (lookups.isEmpty() || lookups.last() != t))
                lookups << t;
        });
        upload.selectFiles({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!upload.busy() || upload.paused(), 180000);
        QVERIFY2(!upload.paused(), qPrintable(upload.errorMessage()));
        QCOMPARE(upload.succeeded(), 1);
        QCOMPARE(upload.failed(), 0);
        QCOMPARE(upload.instant(), 0);
        QVERIFY(lookups.size() >= 2);
        for (int i = 1; i < lookups.size(); ++i)
            QVERIFY(lookups[i] - lookups[i - 1] >= 2000);
        QTest::qWait(2100);
        const auto listed = request([&] { return api.userCloud(1, 100); });
        QVERIFY(!listed.isEmpty());
        bool found = false;
        for (const auto &v : listed.value("page").toObject().value("items").toArray()) {
            const auto row = v.toObject();
            if (row.value("source_file_name").toString() == title &&
                row.value("song").toObject().value("hash").toString().compare(
                    hash, Qt::CaseInsensitive) == 0)
                found = true;
        }
        QVERIFY(found);
        upload.selectFiles({QUrl::fromLocalFile(path)});
        QTRY_VERIFY_WITH_TIMEOUT(!upload.busy() || upload.paused(), 180000);
        QVERIFY2(!upload.paused(), qPrintable(upload.errorMessage()));
        QCOMPARE(upload.succeeded(), 1);
        QCOMPARE(upload.instant(), 1);
        cleaned = cleanup();
        QVERIFY(cleaned);
        qInfo() << "LIVE cloud upload two_parts=true instant=true lookup_interval_ms>=2000 "
                   "cleanup=true";
    }
    void liveFavoritesLoadsWithoutScrolling()
    {
        if (!qEnvironmentVariableIsSet("ECHO_CATALOG_TABS_LIVE"))
            QSKIP("Opt-in current-account read-only favorites chain");
        QJsonObject snapshot;
        const auto connection = QStringLiteral("favorites-native-read-only-oracle");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(QDir::homePath() +
                               "/Library/Application Support/echo-music/echomusic.sqlite");
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            QVERIFY(db.open() && db.transaction());
            QSqlQuery read(db);
            read.prepare("SELECT value_json FROM app_kv WHERE key=?");
            for (const auto &key : {"pinia:user", "pinia:device"}) {
                read.bindValue(0, key);
                QVERIFY(read.exec() && read.next());
                snapshot.insert(key, QJsonDocument::fromJson(read.value(0).toByteArray()).object());
                read.finish();
            }
            db.rollback();
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        const auto user = snapshot.value("pinia:user").toObject().value("info").toObject();
        const auto device = snapshot.value("pinia:device").toObject().value("info").toObject();
        QVERIFY(!user.value("token").toString().isEmpty());
        DeviceIdentity identity{device.value("mid").toVariant().toString(),
                                device.value("dfid").toString(),
                                device.value("guid").toString(),
                                device.value("mac").toString(),
                                "EchoMusicNext",
                                device.value("uuid").toString(),
                                device.value("renderFingerprint").toString()};
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(identity);
        QVERIFY(api.setAccount(user.value("userid").toVariant().toULongLong(),
                               user.value("token").toString(), user.value("t1").toString()));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        RouteController router;
        SongMetadataService songMetadata(api);
        FavoritesService favorites(api, playback, library, order, songMetadata, router, true);
        favorites.registerRoutes(router);
        QSignalSpy metadataReady(&songMetadata, &SongMetadataService::completed);
        QVERIFY(router.navigate("favorites"));
        QTRY_VERIFY_WITH_TIMEOUT(
            !favorites.loading() && FavoritesServiceTestAccess::complete(favorites), 45000);
        QVERIFY2(favorites.errorMessage().isEmpty(), qPrintable(favorites.errorMessage()));
        QVERIFY(!favorites.hasMore());
        QVERIFY(favorites.songs()->count() > 300);
        QCOMPARE(favorites.songs()->count(), FavoritesServiceTestAccess::received(favorites));
        QVERIFY(FavoritesServiceTestAccess::positionsOrdered(favorites));
        QTRY_COMPARE_WITH_TIMEOUT(FavoritesServiceTestAccess::metadataTicket(favorites), quint64(0),
                                  45000);
        QVERIFY(FavoritesServiceTestAccess::metadataRequestedCount(favorites) > 0);
        int completedFacts = 0;
        for (const auto &result : metadataReady)
            completedFacts += result[1].value<QJsonArray>().size();
        QVERIFY(completedFacts > 0);
        qInfo() << "LIVE favorites complete rows=" << favorites.songs()->count()
                << "global_positions_ordered=true without_scroll=true metadata_requested="
                << FavoritesServiceTestAccess::metadataRequestedCount(favorites)
                << "metadata_completed=" << completedFacts;
        favorites.shutdown();
        metadataReady.clear();
        CloudService cloud(api, playback, songMetadata, router, true);
        cloud.registerRoutes(router);
        QVERIFY(router.navigate("cloud"));
        QTRY_VERIFY_WITH_TIMEOUT(!cloud.loading(), 45000);
        QVERIFY2(cloud.errorMessage().isEmpty(), qPrintable(cloud.errorMessage()));
        QVERIFY(cloud.songs()->count() > 0);
        QTRY_COMPARE_WITH_TIMEOUT(CloudServiceTestAccess::metadataTicket(cloud), quint64(0), 45000);
        int cloudFacts = 0;
        for (const auto &result : metadataReady)
            cloudFacts += result[1].value<QJsonArray>().size();
        qInfo() << "LIVE cloud first_page rows=" << cloud.songs()->count()
                << "metadata_consumers=" << metadataReady.size()
                << "metadata_completed=" << cloudFacts;
        cloud.shutdown();
        router.shutdown();
        order.close();
        mutations.cancel();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void favoritesAutomaticallyCompletesAndOrdersAcrossPages()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "control-fixture-never-a-real-credential"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        RouteController router;
        SongMetadataService songMetadata(api);
        FavoritesService favorites(api, playback, library, order, songMetadata, router, false);
        favorites.registerRoutes(router);
        UserPlaylistModelTestAccess::seed(library, api, "我喜欢的音乐", 1, 0, 7);
        QVERIFY(router.navigate("favorites"));
        FavoritesServiceTestAccess::enablePaging(favorites);
        const auto row = [](int id, int position) {
            return QJsonObject{
                {"song",
                 QJsonObject{{"id", QString::number(id)}, {"title", QString("Song %1").arg(id)}}},
                {"sort_index", position},
                {"metadata", QJsonObject{{"source_file_name", QString("Raw %1.mp3").arg(id)}}}};
        };
        FavoritesServiceTestAccess::acceptSongs(favorites, {row(4, 3), row(3, 2)}, 4);
        QCOMPARE(favorites.songs()->count(), 2);
        QCOMPARE(favorites.songs()->item(0).value("resourceId").toString(), QString("3"));
        QVERIFY(!FavoritesServiceTestAccess::complete(favorites));
        // No scrolling/loadMore call: accept schedules the next typed API read.
        auto request = FavoritesServiceTestAccess::songRequest(favorites);
        QVERIFY(request);
        const auto query = ApiClientTestAccess::request(api, request).value("query").toObject();
        QCOMPARE(query.value("page").toInt(), 2);
        QCOMPARE(query.value("page_size").toInt(), 300);
        api.cancel(request);
        favorites.songs()->toggleSelected("3");
        FavoritesServiceTestAccess::acceptSongs(favorites, {row(2, 1), row(1, 0)}, 4, request);
        QVERIFY(FavoritesServiceTestAccess::complete(favorites));
        QVERIFY(!favorites.hasMore());
        QCOMPARE(favorites.songs()->count(), 4);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(favorites.songs()->item(i).value("resourceId").toString(),
                     QString::number(i + 1));
        QCOMPARE(favorites.songs()->selectedCount(), 1);
        api.advanceSession();
        QCOMPARE(favorites.songs()->count(), 0);
        favorites.shutdown();
        router.shutdown();
        QSignalSpy drain(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(drain.size(), 1, 15000);
        api.shutdown();
    }
    void nativeLibraryTabRulesAndRows()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "library-tabs-test-token"));
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"));
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        RouteController router;
        SongMetadataService songMetadata(api);
        FavoritesService favorites(api, playback, library, order, songMetadata, router, false);
        favorites.registerRoutes(router);
        CloudService cloud(api, playback, songMetadata, router, false);
        cloud.registerRoutes(router);
        PurchasedService purchased(api, playback, router, false);
        purchased.registerRoutes(router);
        QJsonArray songs;
        for (int i = 1; i <= 80; ++i)
            songs.append(
                QJsonObject{{"id", QString::number(i)}, {"title", QString("歌曲 %1 Song").arg(i)}});
        favorites.songs()->replaceSongs(songs);
        cloud.songs()->replaceSongs(songs);
        purchased.songs()->replaceSongs(songs);
        CloudServiceTestAccess::quota(cloud);
        ThemeStore theme(prefs);
        theme.setMotionEnabled(false);
        ShellSettings shell(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &values) {
            for (const auto &value : values)
                warnings.append(value.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Shell
import EchoMusic.Theme
Window {
 id: window
 required property RouteController router
 required property ShellSettings settings
 visible: true; width: 1150; height: 800
 Rectangle { anchors.fill: parent; color: Theme.main }
 Item {
  width: parent.width/window.settings.uiScale; height: parent.height/window.settings.uiScale
  scale: window.settings.uiScale; transformOrigin: Item.TopLeft
  RouteHost { anchors.fill: parent; router: window.router; settings: window.settings }
 }
})",
                          QUrl("qrc:/library-tabs-production.qml"));
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"router", QVariant::fromValue(&router)}, {"settings", QVariant::fromValue(&shell)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        const auto requestedWidth = qEnvironmentVariableIntValue("ECHO_LIBRARY_TAB_WIDTH");
        if (requestedWidth > 0)
            window->resize(requestedWidth, 800);
        std::function<QQuickItem *(QQuickItem *, const QString &)> find =
            [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (auto *child : item->childItems())
                if (auto *match = find(child, name))
                    return match;
            return nullptr;
        };
        const auto artifacts = qEnvironmentVariable("ECHO_LIBRARY_TAB_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(QDir().mkpath(artifacts));
        // Begin each route on a nonzero tab too: no click or index change may
        // be required to give its initially selected delegate a rendered rule.
        const QList<QPair<QString, QStringList>> routes{
            {"favorites", {"users", "albums", "videos", "singers", "songs"}},
            {"purchased", {"albums", "songs"}},
            {"cloud", {"songs"}}};
        for (const auto &[route, tabs] : routes)
            for (const auto &tab : tabs) {
                QVERIFY(router.navigate(route, {{"tab", tab}}));
                QQuickItem *page = nullptr;
                QTRY_VERIFY((page = find(window->contentItem(), route + "Page")));
                auto *row = find(page, route + "TabToolbarRow");
                auto *divider = find(page, route + "TabDivider");
                auto *rule = find(page, "detailTabIndicator");
                QVERIFY(row && divider && rule);
                for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                    theme.setMode(mode);
                    for (const auto zoom : {1., 1.25}) {
                        shell.resetZoom();
                        if (zoom > 1.)
                            for (int step = 0; step < 5; ++step)
                                shell.zoomIn();
                        QTRY_COMPARE(row->height(), 56.);
                        QTRY_VERIFY(rule->width() > 20);
                        QTRY_COMPARE(divider->mapToScene(QPointF()).y(),
                                     row->mapToScene(QPointF(0, 55)).y());
                        QVERIFY(rule->mapToScene(QPointF(0, rule->height())).y() <
                                divider->mapToScene(QPointF()).y());
                        const auto tabIndex = route == "favorites"   ? favorites.tab()
                                              : route == "purchased" ? purchased.tab()
                                                                     : 0;
                        auto *label = find(page, QString("detailTabLabel-%1").arg(tabIndex));
                        auto *badge = find(page, QString("detailTabBadge-%1").arg(tabIndex));
                        QVERIFY(label && badge);
                        auto *textBox = label->property("textBox").value<QQuickItem *>();
                        QVERIFY(textBox);
                        if (badge->isVisible()) {
                            QTRY_VERIFY(qAbs(textBox->mapToScene({0, textBox->height() / 2}).y() -
                                             badge->mapToScene({0, badge->height() / 2}).y()) <
                                        .001);
                            QVERIFY(badge->mapToScene({0, 0}).x() >=
                                    textBox->mapToScene({textBox->width(), 0}).x());
                        }
                        if (route == "cloud") {
                            if (mode == ThemeStore::Light && zoom == 1.) {
                                auto *entry = find(page, "cloudUploadEntry");
                                QVERIFY(entry && entry->isVisible());
                                auto *owner = page->findChild<QObject *>("cloudUploadOwner");
                                QVERIFY(owner);
                                QVERIFY(!owner->property("item").value<QObject *>());
                                QTest::mouseClick(
                                    window, Qt::LeftButton, Qt::NoModifier,
                                    entry->mapToScene({entry->width() / 2, entry->height() / 2})
                                        .toPoint());
                                QObject *popup = nullptr;
                                QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
                                QTRY_VERIFY(popup->property("opened").toBool());
                                QPointer<QObject> released = popup;
                                QVERIFY(QMetaObject::invokeMethod(popup, "requestDismiss"));
                                QTRY_VERIFY(released.isNull());
                            }
                            auto *capacity = find(page, "cloudCapacityTrack");
                            QVERIFY(capacity);
                            QTRY_COMPARE(capacity->width(), 232.);
                            auto *count = find(page, "cloudDetailCount");
                            auto *quota = find(page, "cloudCapacity");
                            auto *title = find(page, "songDetailTitleRow");
                            auto *actions = find(page, "songDetailActions");
                            QVERIFY(count && quota && title && actions);
                            const auto top = [](QQuickItem *item) {
                                return item->mapToScene({0, 0}).y();
                            };
                            const auto bottom = [](QQuickItem *item) {
                                return item->mapToScene({0, item->height()}).y();
                            };
                            qInfo()
                                << "Cloud metadata geometry" << zoom << top(title) << bottom(title)
                                << top(count) << bottom(count) << top(quota) << bottom(quota)
                                << top(actions) << "items" << count->size() << quota->size();
                            // Count and quota are independent rows in the evenly spaced area.
                            // Qt Layout rounds spacer sizes to logical pixels.
                            QTRY_VERIFY(qAbs((top(count) - bottom(title)) -
                                             (top(quota) - bottom(count))) < 1.01 * zoom);
                            QTRY_VERIFY(qAbs((top(quota) - bottom(count)) -
                                             (top(actions) - bottom(quota))) < 1.01 * zoom);
                            QVERIFY(top(quota) - bottom(count) > 6 * zoom);
                        }
                        if (route == "favorites" && tab != "songs") {
                            auto *grid = find(page, tab == "users"     ? "favoritesUsersGrid"
                                                    : tab == "singers" ? "favoritesSingersGrid"
                                                    : tab == "albums"  ? "favoritesAlbumsGrid"
                                                                       : "favoritesVideosGrid");
                            QVERIFY(grid);
                            QCOMPARE(grid->property("topMargin").toReal(), 248.);
                        }
                        if (route == "purchased" && tab == "albums") {
                            auto *grid = find(page, "purchasedAlbumsGrid");
                            QVERIFY(grid);
                            QCOMPARE(grid->property("topMargin").toReal(), 248.);
                        }
                        if (!artifacts.isEmpty()) {
                            QTest::qWait(50);
                            QVERIFY(window->grabWindow().save(
                                artifacts + QString("/%1-%2-%3-%4.png")
                                                .arg(route, tab)
                                                .arg(mode == ThemeStore::Light ? "light" : "dark")
                                                .arg(zoom)));
                        }
                        if (QGuiApplication::platformName() != "offscreen" && badge->isVisible()) {
                            const auto image = window->grabWindow();
                            const auto titleInk = Test::renderedInkCenter(
                                image, window, textBox, label->property("color").value<QColor>());
                            const auto badgeInk = Test::renderedInkCenter(
                                image, window, badge, theme.text(), {3, 3, 3, 3});
                            QVERIFY(titleInk && badgeInk);
                            QVERIFY2(qAbs(*titleInk - *badgeInk) <= zoom,
                                     qPrintable(QString("%1/%2 title ink %3 badge ink %4")
                                                    .arg(route, tab)
                                                    .arg(*titleInk)
                                                    .arg(*badgeInk)));
                        }
                        if (tab == "songs") {
                            auto *list = find(page, "discoverySongs");
                            auto *header = find(page, "songDetailHeader");
                            auto *cover = find(page, "themedDetailCover");
                            QVERIFY(list && header && cover);
                            list->setProperty("contentY",
                                              120 - list->property("topMargin").toReal());
                            QTRY_COMPARE(header->property("progress").toReal(), 1.);
                            const auto scale = header->property("coverScale").toReal();
                            QVERIFY(qAbs(cover->property("cornerRadius").toReal() * scale - 6.) <
                                    .01);
                            if (!artifacts.isEmpty())
                                QVERIFY(window->grabWindow().save(artifacts +
                                                                  QString("/%1-collapsed-%2-%3.png")
                                                                      .arg(route)
                                                                      .arg(int(mode))
                                                                      .arg(zoom)));
                            list->setProperty("contentY", -list->property("topMargin").toReal());
                            QTRY_COMPARE(header->property("progress").toReal(), 0.);
                        }
                    }
                }
            }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        root.reset();
        favorites.shutdown();
        cloud.shutdown();
        purchased.shutdown();
        router.shutdown();
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void favoritesSplitFollowProjectsAlbumsAndVideos()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        prefs.setValue("playback/volume", 0.);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "library-pages-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        FavoritesService favorites(api, playback, library, order, songMetadata, router, false);
        favorites.registerRoutes(router);
        QVERIFY(router.available("favorites"));

        // Album cards: only source_code == 2 rows project, id = creatorListId.
        UserPlaylistModelTestAccess::seed(library, api, QStringLiteral("收藏专辑"), 2, 555);
        FavoritesServiceTestAccess::projectAlbums(favorites);
        QCOMPARE(favorites.albums()->count(), 1);
        QCOMPARE(favorites.albums()->item(0).value("resourceId").toString(), QString("555"));
        UserPlaylistModelTestAccess::seed(library, api, QStringLiteral("自建歌单"), 1, 556);
        FavoritesServiceTestAccess::projectAlbums(favorites);
        QCOMPARE(favorites.albums()->count(), 0);

        // Split followed singers using singerid and iden_type == 1.
        const QJsonArray follow{
            QJsonObject{{"key", "user:900001"},
                        {"user_id", 900001},
                        {"nickname", "用户甲"},
                        {"identity_facts", QJsonObject{{"identity_type", 0}}}},
            QJsonObject{{"key", "artist:810483"},
                        {"user_id", 900002},
                        {"artist_id", 810483},
                        {"nickname", "歌手乙"},
                        {"identity_facts", QJsonObject{{"identity_type", 1}}}},
            QJsonObject{{"key", "artist:3520"},
                        {"user_id", 0},
                        {"artist_id", 3520},
                        {"nickname", "歌手丙"},
                        {"identity_facts", QJsonObject{{"identity_type", 1}}}},
        };
        FavoritesServiceTestAccess::acceptFollow(favorites, follow);
        QCOMPARE(favorites.singers()->count(), 2);
        QCOMPARE(favorites.users()->count(), 1);
        QCOMPARE(favorites.users()
                     ->data(favorites.users()->index(0), EchoMusic::SocialUserModel::UserId)
                     .toLongLong(),
                 900001);

        // Collected videos project into the shared card model, duration included.
        const QJsonArray videos{
            QJsonObject{{"id", 590810},
                        {"title", "视频一"},
                        {"artist", "提供方"},
                        {"cover_url", "https://example.com/v1.jpg"},
                        {"duration_ms", 224653}},
            QJsonObject{{"id", 590811},
                        {"title", "视频二"},
                        {"artist", ""},
                        {"cover_url", ""},
                        {"duration_ms", 0}},
        };
        FavoritesServiceTestAccess::acceptVideos(favorites, videos, 2);
        QCOMPARE(favorites.videos()->count(), 2);
        QCOMPARE(favorites.videos()->item(0).value("durationText").toString(), QString("3:44"));
        QVERIFY(!favorites.hasMore());
    }

    void cloudPagesSortDeleteAndReportCounters()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "library-pages-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        SongMetadataService songMetadata(api);
        CloudService cloud(api, playback, songMetadata, router, false);
        cloud.registerRoutes(router);
        QVERIFY(router.available("cloud"));
        QSignalSpy notices(&cloud, &CloudService::notice);

        const auto song = [](const QString &id, const QString &title, qint64 added) {
            return QJsonObject{{"song", QJsonObject{{"id", id},
                                                    {"hash", QString(32, QLatin1Char('A'))},
                                                    {"title", title},
                                                    {"artists", QJsonArray{}},
                                                    {"album_id", "1"},
                                                    {"cover_url", ""},
                                                    {"duration_ms", 224653}}},
                               {"file_id", qint64(id.toULongLong())},
                               {"added_at_seconds", added},
                               {"sort_index", 0},
                               {"metadata", QJsonObject{}}};
        };
        const QJsonObject counters{{"list_count", 3},
                                   {"list_ver", 9},
                                   {"used_size", 865199039},
                                   {"availble_size", qint64(63559310401)},
                                   {"max_size", qint64(64424509440)},
                                   {"user_type", 0},
                                   {"type_size", QJsonObject{}}};
        auto missingFile = song("333", "缺少文件标识", 50);
        missingFile.insert(QStringLiteral("file_id"), 0);
        CloudServiceTestAccess::acceptPage(
            cloud,
            QJsonObject{{"page", 1},
                        {"page_size", 100},
                        {"total", 3},
                        {"items", QJsonArray{song("111", "较早", 100), song("222", "较新", 200),
                                             missingFile}}},
            counters);
        QCOMPARE(cloud.songs()->count(), 3);
        QCOMPARE(cloud.totalCount(), quint64(3));
        QCOMPARE(cloud.storageUsedBytes(), qint64(865199039));
        QCOMPARE(cloud.storageAvailableBytes(), qint64(63559310401));
        QCOMPARE(cloud.storageCapacityBytes(), qint64(64424509440));
        // Default order: added time desc.
        QCOMPARE(cloud.songs()->item(0).value("resourceId").toString(), QString("222"));
        QVERIFY(!cloud.hasMore());

        // Deletion requires a non-zero cloud file ID (kv_id).
        QVERIFY(cloud.canRemoveSong(QStringLiteral("222")));
        QVERIFY(!cloud.canRemoveSong(QStringLiteral("333")));
        QVERIFY(!cloud.canRemoveSong(QStringLiteral("absent")));

        QVERIFY(cloud.removeSongs({QStringLiteral("222")}));
        QVERIFY(CloudServiceTestAccess::deleteRequest(cloud) != 0);
        const auto targets = CloudServiceTestAccess::deleteTargets(cloud);
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.first().kvId, quint64(222));
        QCOMPARE(targets.first().albumAudioId, quint64(222));
        CloudServiceTestAccess::acceptDelete(cloud, QJsonObject{{"list_count", 1},
                                                                {"list_ver", 10},
                                                                {"used_size", 100},
                                                                {"availble_size", 200},
                                                                {"max_size", 300}});
        QCOMPARE(cloud.songs()->count(), 2);
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().first().toString(), QString("已从云盘删除"));

        // Refuse entirely ineligible selections before requesting.
        // Mixed batches skip ineligible rows and report the skip.
        QVERIFY(!cloud.removeSongs({QStringLiteral("333")}));
        QCOMPARE(notices.size(), 2);
        QCOMPARE(notices.last().first().toString(), QString("缺少云盘文件标识，无法删除"));
        QVERIFY(notices.last().at(1).toBool());
        QCOMPARE(cloud.songs()->count(), 2);
        QVERIFY(cloud.removeSongs({QStringLiteral("333"), QStringLiteral("111")}));
        const auto mixedTargets = CloudServiceTestAccess::deleteTargets(cloud);
        QCOMPARE(mixedTargets.size(), 1);
        QCOMPARE(mixedTargets.first().kvId, quint64(111));
        CloudServiceTestAccess::acceptDelete(cloud, QJsonObject{{"list_count", 1},
                                                                {"list_ver", 11},
                                                                {"used_size", 100},
                                                                {"availble_size", 200},
                                                                {"max_size", 300}});
        QCOMPARE(cloud.songs()->count(), 1);
        QCOMPARE(notices.size(), 3);
        QCOMPARE(notices.last().first().toString(), QString("已从云盘删除 1 首，1 首缺少文件标识"));
        QVERIFY(notices.last().at(1).toBool());
    }

    void purchasedContainersStayEmptyWithTotals()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "library-pages-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        PurchasedService purchased(api, playback, router, false);
        purchased.registerRoutes(router);
        QVERIFY(router.available("purchased"));

        PurchasedServiceTestAccess::acceptSongs(purchased, {}, 0);
        QCOMPARE(purchased.songs()->count(), 0);
        QCOMPARE(purchased.songsTotal(), quint64(0));
        QVERIFY(!purchased.hasMore());
        // A non-empty raw goods row must not fabricate a song row.
        PurchasedServiceTestAccess::acceptSongs(
            purchased, QJsonArray{QJsonObject{{"album_audio_id", 38232532}}}, 5);
        QCOMPARE(purchased.songs()->count(), 0);
        QCOMPARE(purchased.songsTotal(), quint64(5));
        QVERIFY(purchased.hasMore());
        PurchasedServiceTestAccess::acceptSongs(purchased, {}, 5);
        QVERIFY(!purchased.hasMore());
        PurchasedServiceTestAccess::acceptAlbums(purchased, {}, 0);
        QCOMPARE(purchased.albums()->count(), 0);
        QCOMPARE(purchased.albumsTotal(), quint64(0));

        // A fresh signed-in page loads both containers and tab totals.
        // The preceding instance has already consumed its setup reads.
        RouteController freshRouter;
        PurchasedService fresh(api, playback, freshRouter, false);
        fresh.registerRoutes(freshRouter);
        PurchasedServiceTestAccess::loadTab(fresh);
        const auto kinds = PurchasedServiceTestAccess::pendingKinds(fresh);
        QCOMPARE(kinds.size(), 2);
        QVERIFY(kinds.contains(0));
        QVERIFY(kinds.contains(1));
    }

    void likedRowFollowsSourceLookupOrder()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "library-pages-token"));
        RouteController router;
        UserPlaylistModel library(api);
        UserPlaylistModelTestAccess::seed(library, api, QStringLiteral("默认收藏"), 1, 0);
        QCOMPARE(library.likedRow(), 0);
        // The exact '我喜欢的音乐' title wins over the pass-3 fallback.
        UserPlaylistModelTestAccess::seedRows(
            library, api, {{QStringLiteral("默认收藏"), 7}, {QStringLiteral("我喜欢的音乐"), 8}});
        QCOMPARE(library.likedRow(), 1);
    }

    void favoritesTracksOrderLoadsSavesAndValidates()
    {
        QTemporaryDir dir;
        QSettings prefs(dir.filePath("prefs.ini"), QSettings::IniFormat);
        ApiClient api(DeviceIdentity::temporary());
        QVERIFY(api.setAccount(100, "library-pages-token"));
        NetworkPreferences network(prefs, api);
        network.installApplicationTransport();
        PlaybackService playback(api, prefs, nullptr, dir.filePath("playback.sqlite"), &network);
        RouteController router;
        UserPlaylistModel library(api);
        PlaylistMutationService mutations(api);
        PlaylistOrderModel order(api, mutations);
        SongMetadataService songMetadata(api);
        FavoritesService favorites(api, playback, library, order, songMetadata, router, false);
        favorites.registerRoutes(router);

        // Hide reordering when no liked playlist resolves.
        QVERIFY(!favorites.canAdjustSongOrder());
        QCOMPARE(favorites.likedListId(), quint64(0));
        UserPlaylistModelTestAccess::seed(library, api, QStringLiteral("收藏专辑"), 2, 555);
        QVERIFY(!favorites.canAdjustSongOrder());
        UserPlaylistModelTestAccess::seed(library, api, QStringLiteral("我喜欢的音乐"), 1, 0, 7);
        QVERIFY(favorites.canAdjustSongOrder());
        QCOMPARE(favorites.likedListId(), quint64(7));
        QCOMPARE(favorites.likedTitle(), QStringLiteral("我喜欢的音乐"));

        // The same editor now also serves other owned playlist detail pages.
        // Their saves must not clear the cached favorites catalog.
        favorites.songs()->replaceSongs(
            {QJsonObject{{"id", "31"}, {"title", "Retained favorite"}}});
        order.openTracks(99, "Other playlist");
        emit order.saved();
        QCOMPARE(favorites.songs()->totalCount(), 1);
        order.close();

        order.openTracks(favorites.likedListId(), favorites.likedTitle());
        QCOMPARE(order.mode(), 1);
        QCOMPARE(order.listId(), quint64(7));
        const auto load = PlaylistOrderModelTestAccess::request(order);
        QVERIFY(load);
        PlaylistOrderModelTestAccess::complete(
            order, load,
            QByteArray(
                R"({"user_id":100,"cloud_list_id":7,"revision":9,"page":{"page":1,"page_size":300,"total":3,"items":[)"
                R"({"song":{"id":"31","title":"甲","artists":[{"id":1,"name":"唱甲"}]},"metadata":{},"file_id":101,"sort_index":0},)"
                R"({"song":{"id":"32","title":"","artists":[]},"metadata":{"source_file_name":"乙.mp3"},"file_id":102,"sort_index":1},)"
                R"({"song":{"id":"33","title":"未知歌曲","artists":[]},"metadata":{},"file_id":103,"sort_index":2}]}})"));
        QVERIFY(order.errorMessage().isEmpty());
        // Hidden (unknown) rows keep their slot but stay out of the editor.
        QCOMPARE(order.rowCount(), 2);
        QCOMPARE(order.data(order.index(0), PlaylistOrderModel::Title).toString(), QString("甲"));
        QCOMPARE(order.data(order.index(0), PlaylistOrderModel::Subtitle).toString(),
                 QString("唱甲"));
        QCOMPARE(order.data(order.index(1), PlaylistOrderModel::Title).toString(), QString("乙"));
        QVERIFY(!order.dirty());

        order.move(1, 0);
        QVERIFY(order.dirty());
        // Version re-read happens before the write; a changed revision aborts.
        order.save();
        const auto stale = PlaylistOrderModelTestAccess::request(order);
        QVERIFY(stale);
        PlaylistOrderModelTestAccess::complete(
            order, stale,
            QByteArray(
                R"({"user_id":100,"cloud_list_id":7,"revision":99,"page":{"page":1,"page_size":1,"total":3,"items":[{"file_id":101}]}})"));
        QVERIFY(order.errorMessage().contains(QStringLiteral("其他位置更新")));
        QVERIFY(order.dirty());
        QVERIFY(!order.saving());

        QSignalSpy saved(&order, &PlaylistOrderModel::saved);
        QSignalSpy notices(&order, &PlaylistOrderModel::notice);
        order.save();
        const auto check = PlaylistOrderModelTestAccess::request(order);
        QVERIFY(check);
        PlaylistOrderModelTestAccess::complete(
            order, check,
            QByteArray(
                R"({"user_id":100,"cloud_list_id":7,"revision":9,"page":{"page":1,"page_size":1,"total":3,"items":[{"file_id":101}]}})"));
        QVERIFY(order.saving());
        const auto sortRequest = PlaylistMutationServiceTestAccess::request(mutations);
        QVERIFY(sortRequest);
        const auto query = ApiClientTestAccess::request(api, sortRequest).value("query").toObject();
        QCOMPARE(query.value("cloud_list_id").toInteger(), qint64(7));
        QCOMPARE(query.value("revision").toInteger(), qint64(9));
        const auto positions = query.value("positions").toArray();
        // Hidden 103 keeps its original slot; visible rows follow the edit.
        QCOMPARE(positions.size(), 3);
        QCOMPARE(positions[0].toObject().value("file_id").toInteger(), qint64(102));
        QCOMPARE(positions[1].toObject().value("file_id").toInteger(), qint64(101));
        QCOMPARE(positions[2].toObject().value("file_id").toInteger(), qint64(103));

        // The shared writer reconciles a fresh read before reporting success.
        PlaylistMutationServiceTestAccess::complete(
            mutations, sortRequest,
            QByteArray(R"({"user_id":100,"cloud_list_id":7,"revision":10,"song_count":3})"));
        QCOMPARE(int(mutations.state()), int(PlaylistMutationService::State::Reconciling));
        const auto reconcile = PlaylistMutationServiceTestAccess::request(mutations);
        QVERIFY(reconcile);
        PlaylistMutationServiceTestAccess::complete(
            mutations, reconcile,
            QByteArray(
                R"({"user_id":100,"cloud_list_id":7,"revision":10,"page":{"page":1,"page_size":300,"total":3,"items":[{"file_id":102},{"file_id":101},{"file_id":103}]}})"));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.last().first().toString(), QString("顺序已保存到酷狗"));
        QVERIFY(!notices.last().at(1).toBool());
        QCOMPARE(int(mutations.state()), int(PlaylistMutationService::State::Ready));
        QVERIFY(!order.dirty());
        QVERIFY(!order.saving());
    }

    void songCatalogClearSortRestoresModelOrder()
    {
        SongCatalogModel model;
        const auto seedSong = [](const QString &id, const QString &title) {
            return QJsonObject{{"id", id},
                               {"hash", QString(32, QLatin1Char('A'))},
                               {"title", title},
                               {"artists", QJsonArray{}},
                               {"album_id", "1"},
                               {"cover_url", ""},
                               {"duration_ms", 224653}};
        };
        model.replaceSongs(QJsonArray{seedSong("1", "Zeta"), seedSong("2", "Alpha")});
        QCOMPARE(model.item(0).value("title").toString(), QString("Zeta"));
        QSignalSpy changed(&model, &SongCatalogModel::changed);
        model.toggleSort(1);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(model.sortField(), 1);
        changed.clear();
        model.clearSort();
        QCOMPARE(model.sortField(), 0);
        QCOMPARE(model.sortDirection(), 0);
        // The model falls back to its own arrival order.
        QCOMPARE(model.item(0).value("title").toString(), QString("Zeta"));
        QVERIFY(!changed.isEmpty());
    }
};

QTEST_MAIN(LibraryPagesTest)
#include "LibraryPagesTest.moc"
