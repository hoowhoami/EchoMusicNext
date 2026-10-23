#include "PlaylistMutationService.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>
#include <functional>
#include <iostream>
#include <iterator>

using namespace EchoMusic;
namespace EchoMusic
{
class ApiClientTestAccess
{
public:
    static bool replayEnabled(ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).automaticVerification;
    }
    static QJsonObject request(ApiClient &api, quint64 id)
    {
        return api.m_pending.value(id).request;
    }
};
class PlaylistMutationServiceTestAccess
{
public:
    static quint64 writing(PlaylistMutationService &service, ApiClient &api)
    {
        return service.begin(api.capabilities(), PlaylistMutationService::Kind::Create, {0})
                   ? service.m_request
                   : 0;
    }
    static quint64 reading(PlaylistMutationService &service, ApiClient &api, bool remove = false)
    {
        service.m_request = api.capabilities();
        service.m_revision = api.sessionRevision();
        service.m_userId = api.currentAccountUserId();
        service.m_state = PlaylistMutationService::State::Reconciling;
        service.m_id = {7};
        service.m_page = 1;
        service.m_seen = 0;
        service.m_libraryReadIds.clear();
        service.m_found = false;
        service.m_kind =
            remove ? PlaylistMutationService::Kind::Remove : PlaylistMutationService::Kind::Create;
        service.m_expectedCount = remove ? 0 : 1;
        service.m_expectedRevision = 8;
        service.m_acknowledgement = QByteArrayLiteral(
            "{\"user_id\":1,\"cloud_list_id\":7,\"total_revision\":8,\"playlist_count\":1}");
        return service.m_request;
    }
    static void succeed(PlaylistMutationService &service, quint64 id, const QByteArray &payload)
    {
        service.succeed(id, payload);
    }
    static void fail(PlaylistMutationService &service, quint64 id, const QByteArray &payload)
    {
        service.fail(id, payload);
    }
    static quint64 checkingCover(PlaylistMutationService &service, ApiClient &api,
                                 bool afterUpload = false)
    {
        service.m_request = api.capabilities();
        service.m_revision = api.sessionRevision();
        service.m_userId = api.currentAccountUserId();
        service.m_state = PlaylistMutationService::State::CheckingCover;
        service.m_coverStage = afterUpload ? PlaylistMutationService::CoverStage::AfterUpload
                                           : PlaylistMutationService::CoverStage::BeforeUpload;
        service.m_coverUpdate = {{7}, 8, 0, {}, QString(), QString(), {}};
        service.m_page = 1;
        service.m_seen = 0;
        service.m_libraryReadIds.clear();
        service.m_found = false;
        service.m_libraryReadIds.clear();
        return service.m_request;
    }
    static quint64 uploadingCover(PlaylistMutationService &service, ApiClient &api)
    {
        const auto id = checkingCover(service, api);
        service.m_coverStage = PlaylistMutationService::CoverStage::Upload;
        service.m_state = PlaylistMutationService::State::UploadingCover;
        return id;
    }
    static quint64 readingOrder(PlaylistMutationService &service, ApiClient &api)
    {
        const auto id = reading(service, api);
        service.m_kind = PlaylistMutationService::Kind::SortLibrary;
        service.m_positions.clear();
        service.m_positions.insert(7, {{7}, ApiClient::PlaylistKind::Owned, 0});
        service.m_verifiedPositions.clear();
        return id;
    }
};
} // namespace EchoMusic
class PlaylistMutationTest : public QObject
{
    Q_OBJECT
private slots:
    void writesNeverAutomaticallyReplayAndFileIdsRemainTyped()
    {
        ApiClient api;
        QVERIFY(!api.createPlaylist(QStringLiteral("Fixture"), true));
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token"), QStringLiteral("fixture-t1")));
        const QList<quint64> requests{
            api.createPlaylist(QStringLiteral("Fixture"), true),
            api.addPlaylistTracks({7}, {{QStringLiteral("A|B,C"), QString(32, u'A'), 2, 3}}),
            api.removePlaylistTracks({7}, {{4}}),
            api.sortPlaylistTracks({7}, 8, {{{4}, 0}}),
            api.updatePlaylist({{7}, 8, 0, QStringLiteral("Fixture"), QString(), QString()}),
            api.deletePlaylist({7})};
        // Snapshot before cancellation: a cancelled logical request no longer
        // owns its pending body, even if the native callback is not delivered yet.
        const auto add = ApiClientTestAccess::request(api, requests[1]).value("query").toObject();
        for (auto id : requests) {
            QVERIFY(id);
            QVERIFY(!ApiClientTestAccess::replayEnabled(api, id));
            const auto identity =
                ApiClientTestAccess::request(api, id).value("identity").toObject();
            QCOMPARE(identity.value("authentication").toString(),
                     QStringLiteral("current_account"));
            QCOMPARE(identity.value("t1").toString(), QStringLiteral("fixture-t1"));
            api.cancel(id);
        }
        const auto tracks = add.value("tracks").toArray();
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(tracks.first().toObject().value("title").toString(), QStringLiteral("A|B,C"));
        QVERIFY(!api.deletePlaylist({0}));
        QVERIFY(!api.removePlaylistTracks({7}, {}));
        const auto sort = api.sortPlaylists(8, {{{7}, ApiClient::PlaylistKind::Owned, 0},
                                                {{9}, ApiClient::PlaylistKind::Collected, 0}});
        QVERIFY(sort);
        QVERIFY(!ApiClientTestAccess::replayEnabled(api, sort));
        api.cancel(sort);
        QVERIFY(!api.sortPlaylists(8, {{{7}, ApiClient::PlaylistKind::Owned, 0},
                                       {{9}, ApiClient::PlaylistKind::Owned, 0}}));
        // A truncated PNG is rejected by Rust before HTTP. Inspect the binary
        // request's metadata, then cancel without depending on callback timing.
        const auto upload = api.uploadPlaylistCover(QByteArray::fromHex("89504e470d0a1a0a"));
        QVERIFY(upload);
        QVERIFY(!ApiClientTestAccess::replayEnabled(api, upload));
        const auto uploadRequest = ApiClientTestAccess::request(api, upload);
        QVERIFY(!uploadRequest.contains("identity"));
        QVERIFY(!uploadRequest.contains("png_base64"));
        api.cancel(upload);
    }
    void coverConflictCancelAndAccountChangeNeverAdvanceToMetadataWrite()
    {
        ApiClient api;
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token")));
        PlaylistMutationService service(api);
        QSignalSpy completed(&service, &PlaylistMutationService::completed);
        const auto page =
            QJsonDocument(
                QJsonObject{
                    {"user_id", 1},
                    {"total_revision", 9},
                    {"page", QJsonObject{{"page", 1}, {"total", 0}, {"items", QJsonArray{}}}}})
                .toJson();
        for (bool after : {false, true}) {
            const auto id = PlaylistMutationServiceTestAccess::checkingCover(service, api, after);
            PlaylistMutationServiceTestAccess::succeed(service, id, page);
            QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
            QVERIFY(completed.isEmpty());
        }
        PlaylistMutationServiceTestAccess::checkingCover(service, api);
        service.cancel();
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        const auto late = PlaylistMutationServiceTestAccess::uploadingCover(service, api);
        service.cancel();
        QCOMPARE(service.state(), PlaylistMutationService::State::OutcomeUnknown);
        service.acceptRefreshedUnknownOutcome();
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        QVERIFY(api.setAccount(2, QStringLiteral("fixture-other-token")));
        PlaylistMutationServiceTestAccess::succeed(
            service, late,
            QByteArrayLiteral(
                "{\"file_name\":\"sample.png\",\"reference\":\"custom/sample.png\"}"));
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        QVERIFY(completed.isEmpty());
    }
    void libraryOrderReadbackChecksPositionAndCategory()
    {
        ApiClient api;
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token")));
        PlaylistMutationService service(api);
        QSignalSpy completed(&service, &PlaylistMutationService::completed);
        auto page = [](int sort, const QString &kind) {
            return QJsonDocument(
                       QJsonObject{
                           {"user_id", 1},
                           {"total_revision", 8},
                           {"page",
                            QJsonObject{{"page", 1},
                                        {"total", 1},
                                        {"items", QJsonArray{QJsonObject{{"cloud_list_id", 7},
                                                                         {"kind", kind},
                                                                         {"sort_index", sort}}}}}}})
                .toJson();
        };
        auto id = PlaylistMutationServiceTestAccess::readingOrder(service, api);
        PlaylistMutationServiceTestAccess::succeed(service, id, page(1, QStringLiteral("owned")));
        QCOMPARE(service.state(), PlaylistMutationService::State::RefreshRequired);
        QVERIFY(completed.isEmpty());
        id = PlaylistMutationServiceTestAccess::readingOrder(service, api);
        PlaylistMutationServiceTestAccess::succeed(service, id,
                                                   page(0, QStringLiteral("collected")));
        QCOMPARE(service.state(), PlaylistMutationService::State::RefreshRequired);
        QVERIFY(completed.isEmpty());
        id = PlaylistMutationServiceTestAccess::readingOrder(service, api);
        PlaylistMutationServiceTestAccess::succeed(service, id, page(0, QStringLiteral("owned")));
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        QCOMPARE(completed.size(), 1);
    }
    void writeReadbackVerifiesActualPositionsWithoutPageEchoOrCountGates()
    {
        ApiClient api(DeviceIdentity::temporary());
        api.setAccount(1, "domain-test-token");
        PlaylistMutationService service(api);
        QSignalSpy completed(&service, &PlaylistMutationService::completed);
        const auto id = PlaylistMutationServiceTestAccess::readingOrder(service, api);
        PlaylistMutationServiceTestAccess::succeed(
            service, id,
            R"({"user_id":1,"total_revision":8,"page":{"page":999,"page_size":1,"total":0,"items":[{"cloud_list_id":7,"kind":"owned","sort_index":0}]}})");
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        QCOMPARE(completed.size(), 1);
        api.shutdown();
    }
    void unknownOutcomeBlocksReplayAndLateResultsCannotCrossAccounts()
    {
        ApiClient api;
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token")));
        PlaylistMutationService service(api);
        QSignalSpy completed(&service, &PlaylistMutationService::completed);
        const auto writing = PlaylistMutationServiceTestAccess::writing(service, api);
        QVERIFY(writing);
        PlaylistMutationServiceTestAccess::fail(service, writing,
                                                QByteArrayLiteral("{\"kind\":\"transport\"}"));
        QCOMPARE(service.state(), PlaylistMutationService::State::OutcomeUnknown);
        QVERIFY(!service.create(QStringLiteral("Fixture"), true));
        QVERIFY(!service.reconcile());
        service.acceptRefreshedUnknownOutcome();
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        const auto late = PlaylistMutationServiceTestAccess::writing(service, api);
        service.cancel();
        QCOMPARE(service.state(), PlaylistMutationService::State::OutcomeUnknown);
        QVERIFY(api.setAccount(2, QStringLiteral("fixture-other-token")));
        PlaylistMutationServiceTestAccess::succeed(
            service, late, QByteArrayLiteral("{\"user_id\":1,\"cloud_list_id\":7}"));
        QVERIFY(completed.isEmpty());
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        QVERIFY(api.setAccount(0, {}));
        QCOMPARE(service.state(), PlaylistMutationService::State::SignedOut);
    }
    void readbackChecksRevisionPresenceAndReentrantSignout()
    {
        // Host-domain control events, not fabricated upstream response fixtures.
        ApiClient api;
        QVERIFY(api.setAccount(1, QStringLiteral("fixture-token")));
        PlaylistMutationService service(api);
        QSignalSpy completed(&service, &PlaylistMutationService::completed);
        auto page = [](int revision, bool present) {
            return QJsonDocument(
                       QJsonObject{
                           {"user_id", 1},
                           {"total_revision", revision},
                           {"page", QJsonObject{{"page", 1},
                                                {"page_size", 100},
                                                {"total", present ? 1 : 0},
                                                {"items", present ? QJsonArray{QJsonObject{
                                                                        {"cloud_list_id", 7}}}
                                                                  : QJsonArray{}}}}})
                .toJson();
        };
        auto id = PlaylistMutationServiceTestAccess::reading(service, api);
        PlaylistMutationServiceTestAccess::succeed(service, id, page(9, true));
        QCOMPARE(service.state(), PlaylistMutationService::State::RefreshRequired);
        QVERIFY(completed.isEmpty());
        id = PlaylistMutationServiceTestAccess::reading(service, api);
        PlaylistMutationServiceTestAccess::succeed(service, id, page(8, true));
        QCOMPARE(service.state(), PlaylistMutationService::State::Ready);
        QCOMPARE(completed.size(), 1);
        id = PlaylistMutationServiceTestAccess::reading(service, api, true);
        connect(&service, &PlaylistMutationService::libraryInvalidated, &service,
                [&] { api.setAccount(0, {}); });
        PlaylistMutationServiceTestAccess::succeed(service, id, page(8, false));
        QCOMPARE(service.state(), PlaylistMutationService::State::SignedOut);
        QCOMPARE(completed.size(), 1);
    }
    void liveWriteChain()
    {
        if (qEnvironmentVariable("ECHO_LIVE_PLAYLIST_WRITES") != QStringLiteral("1"))
            QSKIP("Explicit opt-in current-account writes to a fresh temporary playlist only");
        std::string input((std::istreambuf_iterator<char>(std::cin)),
                          std::istreambuf_iterator<char>());
        const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
        DeviceIdentity device;
        device.mid = identity.value("mid").toString();
        device.dfid = identity.value("dfid").toString();
        device.guid = identity.value("guid").toString();
        device.mac = identity.value("mac").toString();
        device.deviceName = identity.value("device_name").toString();
        ApiClient api(device);
        QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                               identity.value("token").toString(),
                               identity.value("t1").toString()));
        PlaylistMutationService service(api);
        auto read = [&](const std::function<quint64()> &submit, QJsonObject &result) {
            QSignalSpy success(&api, &ApiClient::succeeded), failure(&api, &ApiClient::failed);
            const auto request = submit();
            if (!request)
                return false;
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 30000) {
                for (const auto &row : success)
                    if (row[0].toULongLong() == request) {
                        result = QJsonDocument::fromJson(row[1].toByteArray()).object();
                        return !result.isEmpty();
                    }
                for (const auto &row : failure)
                    if (row[0].toULongLong() == request)
                        return false;
                QTest::qWait(10);
            }
            api.cancel(request);
            return false;
        };
        auto allLists = [&](QJsonArray &rows, quint64 &revision) {
            rows = {};
            revision = 0;
            for (quint32 page = 1;; ++page) {
                QJsonObject result;
                if (!read([&] { return api.userPlaylists(page, 100); }, result))
                    return false;
                const auto current = quint64(result.value("total_revision").toInteger());
                if (page > 1 && current != revision)
                    return false;
                revision = current;
                const auto data = result.value("page").toObject();
                const auto batch = data.value("items").toArray();
                for (const auto &item : batch)
                    rows.append(item);
                if (quint64(rows.size()) == quint64(data.value("total").toInteger()))
                    return true;
                if (batch.isEmpty())
                    return false;
            }
        };
        QJsonArray before;
        quint64 totalRevision = 0;
        QVERIFY(allLists(before, totalRevision));
        const auto title = QStringLiteral("EchoMusicNext Qt API 验证 ") +
                           QUuid::createUuid().toString(QUuid::Id128);
        ApiClient::CloudPlaylistId id{0};
        // Every early QVERIFY return still reconciles the unique resource and
        // verifies ownership before deleting. Never retry an unknown create.
        const auto cleanup = qScopeGuard([&] {
            service.cancel();
            QJsonArray rows;
            quint64 revision = 0;
            if (!allLists(rows, revision)) {
                QTest::qFail("Cleanup could not read current playlists", __FILE__, __LINE__);
                return;
            }
            QList<ApiClient::CloudPlaylistId> targets;
            for (const auto &value : rows) {
                const auto row = value.toObject();
                if (!row.value("title").toString().startsWith(title))
                    continue;
                if (row.value("kind").toString() != QStringLiteral("owned") ||
                    quint64(row.value("creator_id").toInteger()) != api.currentAccountUserId()) {
                    QTest::qFail("Cleanup ownership check failed", __FILE__, __LINE__);
                    return;
                }
                const auto targetId = quint64(row.value("cloud_list_id").toInteger());
                for (const auto &original : before)
                    if (quint64(original.toObject().value("cloud_list_id").toInteger()) ==
                        targetId) {
                        QTest::qFail("Cleanup refused preexisting resource", __FILE__, __LINE__);
                        return;
                    }
                targets.append(ApiClient::CloudPlaylistId{targetId});
            }
            if (targets.size() > 2) {
                QTest::qFail("Cleanup ambiguous", __FILE__, __LINE__);
                return;
            }
            for (auto targetId : targets) {
                QJsonObject deleted;
                // An unreadable acknowledgement may follow a committed delete.
                // Read afterwards; never automatically replay this write.
                read([&] { return api.deletePlaylist(targetId); }, deleted);
            }
            if (!allLists(rows, revision)) {
                QTest::qFail("Cleanup not confirmed", __FILE__, __LINE__);
                return;
            }
            for (const auto &value : rows)
                if (value.toObject().value("title").toString().startsWith(title))
                    QTest::qFail("Temporary playlist still present", __FILE__, __LINE__);
            if (rows != before)
                QTest::qFail("Original domain library changed", __FILE__, __LINE__);
        });
        auto write = [&](const std::function<bool()> &submit, QJsonObject &result) {
            QSignalSpy completed(&service, &PlaylistMutationService::completed);
            if (!submit())
                return false;
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 60000 &&
                   (service.state() == PlaylistMutationService::State::Writing ||
                    service.state() == PlaylistMutationService::State::Reconciling ||
                    service.state() == PlaylistMutationService::State::CheckingCover ||
                    service.state() == PlaylistMutationService::State::UploadingCover))
                QTest::qWait(10);
            if (completed.size() != 1 || service.state() != PlaylistMutationService::State::Ready)
                return false;
            result = QJsonDocument::fromJson(completed.first()[0].toByteArray()).object();
            return true;
        };
        QJsonObject result;
        QVERIFY(write([&] { return service.create(title, true); }, result));
        id = {quint64(result.value("cloud_list_id").toInteger())};
        QVERIFY(id.value);
        QVERIFY(
            write([&] { return service.create(title + QStringLiteral(" 排序"), true); }, result));
        const ApiClient::CloudPlaylistId secondId{
            quint64(result.value("cloud_list_id").toInteger())};
        QJsonArray ordered;
        QVERIFY(allLists(ordered, totalRevision));
        QVERIFY(write(
            [&] {
                return service.sortLibrary(totalRevision,
                                           {{id, ApiClient::PlaylistKind::Owned, 1},
                                            {secondId, ApiClient::PlaylistKind::Owned, 0}});
            },
            result));
        QJsonObject album;
        QVERIFY(read([&] { return api.albumSongs(960399, 1, 2); }, album));
        QList<ApiClient::PlaylistTrackInput> inputs;
        for (const auto &value : album.value("page").toObject().value("items").toArray()) {
            const auto song = value.toObject().value("song").toObject();
            inputs.append({song.value("title").toString(), song.value("hash").toString(),
                           song.value("album_id").toString().toULongLong(),
                           song.value("id").toString().toULongLong()});
        }
        QCOMPARE(inputs.size(), 2);
        QVERIFY(write([&] { return service.addTracks(id, inputs); }, result));
        QJsonObject tracks;
        QVERIFY(read([&] { return api.cloudPlaylistTracks(id); }, tracks));
        const auto items = tracks.value("page").toObject().value("items").toArray();
        QCOMPARE(items.size(), 2);
        QList<ApiClient::PlaylistTrackPosition> positions;
        for (int i = 0; i < items.size(); ++i)
            positions.append(
                {{quint64(items[i].toObject().value("file_id").toInteger())}, quint32(1 - i)});
        const auto version = quint64(tracks.value("revision").toInteger());
        QVERIFY(write([&] { return service.sortTracks(id, version, positions); }, result));
        QVERIFY(read([&] { return api.cloudPlaylistTracks(id); }, tracks));
        for (const auto &value : tracks.value("page").toObject().value("items").toArray()) {
            const auto row = value.toObject();
            bool matched = false;
            for (auto position : positions)
                if (quint64(row.value("file_id").toInteger()) == position.fileId.value) {
                    matched = true;
                    QCOMPARE(row.value("sort_index").toInteger(), qint64(position.position));
                }
            QVERIFY(matched);
        }
        QJsonArray rows;
        QVERIFY(allLists(rows, totalRevision));
        qint64 sort = 0;
        for (const auto &value : rows)
            if (quint64(value.toObject().value("cloud_list_id").toInteger()) == id.value)
                sort = value.toObject().value("sort_index").toInteger();
        QVERIFY(write(
            [&] {
                return service.update({id, totalRevision, sort, title + QStringLiteral(" 已更新"),
                                       QStringLiteral("Qt Rust 原生写接口验证"), QString()});
            },
            result));
        QVERIFY(allLists(rows, totalRevision));
        bool updated = false;
        for (const auto &value : rows)
            if (quint64(value.toObject().value("cloud_list_id").toInteger()) == id.value) {
                updated = value.toObject().value("title").toString() ==
                              title + QStringLiteral(" 已更新") &&
                          value.toObject().value("description").toString() ==
                              QStringLiteral("Qt Rust 原生写接口验证");
            }
        QVERIFY(updated);
        QImage image(400, 400, QImage::Format_RGB32);
        image.fill(QColor(55, 110, 190));
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        ApiClient::PlaylistUpdate coverUpdate{
            id, totalRevision, sort, {}, QStringLiteral("Qt Rust 原生写接口验证"), QString()};
        QVERIFY(write([&] { return service.saveCover(coverUpdate, png); }, result));
        QVERIFY(allLists(rows, totalRevision));
        bool coverSaved = false;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            if (quint64(row.value("cloud_list_id").toInteger()) == id.value)
                coverSaved =
                    row.value("custom_cover").toBool() &&
                    row.value("cover_url").toString().contains(QStringLiteral("/custom/240/"));
        }
        QVERIFY(coverSaved);
        QVERIFY(write([&] { return service.removeTracks(id, {positions[0].fileId}); }, result));
        QCOMPARE(result.value("song_count").toInteger(), qint64(1));
        QVERIFY(write([&] { return service.remove(id); }, result));
        QVERIFY(allLists(rows, totalRevision));
        for (const auto &value : rows)
            QVERIFY(quint64(value.toObject().value("cloud_list_id").toInteger()) != id.value);
    }
};
QTEST_GUILESS_MAIN(PlaylistMutationTest)
#include "PlaylistMutationTest.moc"
