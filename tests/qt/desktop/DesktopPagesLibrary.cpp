#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::livePlaylistImportsThroughQtService()
{
    if (qEnvironmentVariable("ECHO_LIVE_PLAYLIST_IMPORTS") != "1")
        QSKIP("Explicit current-account import and cleanup probe only");
    std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
    DeviceIdentity device;
    device.mid = identity.value("mid").toString();
    device.dfid = identity.value("dfid").toString();
    device.guid = identity.value("guid").toString();
    device.mac = identity.value("mac").toString();
    device.deviceName = identity.value("device_name").toString();
    ApiClient api(device);
    QVERIFY(api.setAccount(quint64(identity.value("userid").toInteger()),
                           identity.value("token").toString(), identity.value("t1").toString()));
    UserPlaylistModel source(api);
    PlaylistMutationService mutations(api);
    PlaylistImportService importer(api, source, mutations);
    auto read = [&](const std::function<quint64()> &submit, QJsonObject &result) {
        QSignalSpy ok(&api, &ApiClient::succeeded), failed(&api, &ApiClient::failed);
        const auto id = submit();
        if (!id)
            return false;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 30000) {
            for (const auto &row : ok)
                if (row[0].toULongLong() == id) {
                    result = QJsonDocument::fromJson(row[1].toByteArray()).object();
                    return !result.isEmpty();
                }
            for (const auto &row : failed)
                if (row[0].toULongLong() == id)
                    return false;
            QTest::qWait(10);
        }
        api.cancel(id);
        return false;
    };
    auto catalog = [&](QJsonArray &rows) {
        rows = {};
        qint64 version = -1, total = -1;
        for (quint32 page = 1; page <= 500; ++page) {
            QJsonObject result;
            if (!read([&] { return api.userPlaylists(page, 200); }, result))
                return false;
            const auto data = result.value("page").toObject();
            const auto batch = data.value("items").toArray();
            if (page > 1 && (version != result.value("total_revision").toInteger() ||
                             total != data.value("total").toInteger()))
                return false;
            version = result.value("total_revision").toInteger();
            total = data.value("total").toInteger();
            for (const auto &row : batch)
                rows.append(row);
            if (rows.size() == total)
                return true;
            if (batch.size() < 200)
                return false;
        }
        return false;
    };
    QJsonArray before;
    QVERIFY(catalog(before));
    QSet<quint64> baseline;
    for (const auto &row : before)
        baseline.insert(quint64(row.toObject().value("cloud_list_id").toInteger()));
    const auto cleanup = [&] {
        QJsonArray rows;
        if (!catalog(rows)) {
            QTest::qFail("Import cleanup catalog unavailable", __FILE__, __LINE__);
            return;
        }
        const auto target = PlaylistImportServiceTestAccess::target(importer);
        QList<quint64> ids;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const auto id = quint64(row.value("cloud_list_id").toInteger());
            if (baseline.contains(id))
                continue;
            if (id != target || row.value("kind").toString() != "owned" ||
                quint64(row.value("creator_id").toInteger()) != api.currentAccountUserId() ||
                row.value("title").toString() != importer.title()) {
                QTest::qFail("Import cleanup refused unrelated resource", __FILE__, __LINE__);
                return;
            }
            ids.append(id);
        }
        for (auto id : ids) {
            QJsonObject deleted;
            read([&] { return api.deletePlaylist({id}); }, deleted);
        }
        if (!catalog(rows)) {
            QTest::qFail("Import cleanup confirmation unavailable", __FILE__, __LINE__);
            return;
        }
        QSet<quint64> after;
        for (const auto &row : rows)
            after.insert(quint64(row.toObject().value("cloud_list_id").toInteger()));
        if (after != baseline)
            QTest::qFail("Import cleanup IDs differ from baseline", __FILE__, __LINE__);
    };
    QFile links(qEnvironmentVariable("ECHO_IMPORT_LINKS"));
    QVERIFY(links.open(QIODevice::ReadOnly));
    const auto inputs = QJsonDocument::fromJson(links.readAll()).array();
    QCOMPARE(inputs.size(), 4);
    for (const auto &input : inputs) {
        auto guard = qScopeGuard(cleanup);
        const auto entry = input.toObject();
        QVERIFY(importer.startLink(entry.value("url").toString()));
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 180000);
        if (entry.value("provider").toString() == "qishui")
            QCOMPARE(importer.phase(), PlaylistImportService::Failed);
        else {
            QCOMPARE(importer.phase(), PlaylistImportService::Completed);
            QVERIFY(importer.imported() > 0);
        }
        qInfo("Native import terminal: imported=%llu missed=%llu failed=%d",
              static_cast<unsigned long long>(importer.imported()),
              static_cast<unsigned long long>(importer.missed()),
              int(importer.phase() == PlaylistImportService::Failed));
    }
    {
        auto guard = qScopeGuard(cleanup);
        const auto title =
            "EchoMusicNext screenshot native " + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(importer.startScreenshot(
            {QUrl::fromLocalFile(qEnvironmentVariable("ECHO_IMPORT_IMAGE"))}, {}, title));
        QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(), 180000);
        QCOMPARE(importer.phase(), PlaylistImportService::Completed);
        QVERIFY(importer.imported() > 0);
        qInfo("Native screenshot import terminal: imported=%llu",
              static_cast<unsigned long long>(importer.imported()));
    }
    api.shutdown();
}

void DesktopPagesTest::playlistImportBackgroundResultsStopAndSessionIsolation()
{
    ApiClient api(DeviceIdentity::temporary());
    api.setAccount(900001, "domain-test-token");
    UserPlaylistModel source(api);
    PlaylistMutationService mutations(api);
    PlaylistImportService importer(api, source, mutations);
    QAbstractItemModelTester tester(&importer,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    QTemporaryDir temp;
    QSettings preferences(temp.filePath("preferences.ini"), QSettings::IniFormat);
    TaskCenterModel tasks;
    PlaylistImportTaskCoordinator coordinator(importer, tasks, preferences);
    QVERIFY(importer.startLink("https://example.invalid/playlist"));
    QCOMPARE(tasks.count(), 1);
    PlaylistImportServiceTestAccess::complete(
        importer, R"({"task_id":71,"user_id":900001,"cloud_list_id":0})");
    QCOMPARE(importer.phase(), PlaylistImportService::Cloud);
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"([{"task_id":71,"user_id":900001,"cloud_list_id":101,"status":3,"title":"领域导入结果","total":3,"imported":2,"missed":1}])");
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"({"task":{"task_id":71,"user_id":900001,"cloud_list_id":101},"page":1,"page_size":100,"missed_tracks":[{"title":"未匹配歌曲","artist":"歌手","reason":"没有结果"}]})");
    QCOMPARE(importer.phase(), PlaylistImportService::Completed);
    QCOMPARE(importer.rowCount(), 1);
    QCOMPARE(importer.imported(), quint64(2));
    QCOMPARE(tasks.data(tasks.index(0), TaskCenterModel::StatusRole).toInt(),
             int(TaskCenterModel::Completed));
    QSignalSpy reopen(&importer, &PlaylistImportService::showRequested);
    QVERIFY(tasks.invokeAction("playlist-import",
                               tasks.data(tasks.index(0), TaskCenterModel::TokenRole).toString(),
                               "view"));
    QCOMPARE(reopen.size(), 1);
    QVERIFY(importer.startLink("https://example.invalid/playlist"));
    PlaylistImportServiceTestAccess::complete(
        importer, R"({"task_id":72,"user_id":900001,"cloud_list_id":0})");
    const auto stopped = PlaylistImportServiceTestAccess::request(importer);
    importer.stop();
    PlaylistImportServiceTestAccess::stale(importer, stopped,
                                           R"([{"task_id":72,"user_id":900001,"status":3}])");
    QCOMPARE(importer.phase(), PlaylistImportService::Stopped);
    QVERIFY(importer.startLink("https://example.invalid/playlist"));
    const auto old = PlaylistImportServiceTestAccess::request(importer);
    api.setAccount(900002, "other-domain-token");
    PlaylistImportServiceTestAccess::stale(importer, old, R"({"task_id":73,"user_id":900001})");
    QCOMPARE(importer.phase(), PlaylistImportService::Idle);
    QCOMPARE(tasks.count(), 0);
    api.shutdown();
}

void DesktopPagesTest::playlistOrderKeepsFixedSlotsAndRejectsChangedVersions()
{
    ApiClient api(DeviceIdentity::temporary());
    api.setAccount(900001, "domain-test-token");
    PlaylistMutationService mutations(api);
    PlaylistOrderModel model(api, mutations);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QByteArray snapshot =
        R"({"user_id":900001,"total_revision":12,"page":{"page":1,"page_size":200,"total":6,"items":[
          {"cloud_list_id":1,"kind":"owned","title":"默认收藏","sort_index":0,"song_count":1,"source_code":1,"default_kind":1},
          {"cloud_list_id":2,"kind":"owned","title":"A","sort_index":1,"song_count":2,"source_code":1,"default_kind":null},
          {"cloud_list_id":3,"kind":"owned","title":"我喜欢","sort_index":2,"song_count":3,"source_code":1,"default_kind":2},
          {"cloud_list_id":4,"kind":"owned","title":"B","sort_index":3,"song_count":4,"source_code":1,"default_kind":null},
          {"cloud_list_id":5,"kind":"collected","title":"专辑","sort_index":0,"song_count":5,"source_code":2,"default_kind":null},
          {"cloud_list_id":6,"kind":"collected","title":"收藏歌单","sort_index":1,"song_count":6,"source_code":1,"default_kind":null}
        ]}})";
    model.open(0);
    QVERIFY(model.busy());
    PlaylistOrderModelTestAccess::complete(model, PlaylistOrderModelTestAccess::request(model),
                                           snapshot);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), PlaylistOrderModel::Title).toString(), QString("A"));
    model.move(0, 1);
    QVERIFY(model.dirty());
    const auto positions = PlaylistOrderModelTestAccess::positions(model);
    QCOMPARE(positions.size(), 4);
    QCOMPARE(positions[0].id.value, quint64(1));
    QCOMPARE(positions[1].id.value, quint64(4));
    QCOMPARE(positions[2].id.value, quint64(3));
    QCOMPARE(positions[3].id.value, quint64(2));
    QCOMPARE(positions[3].position, quint32(3));
    model.save();
    PlaylistOrderModelTestAccess::complete(
        model, PlaylistOrderModelTestAccess::request(model),
        R"({"user_id":900001,"total_revision":13,"page":{"total":6}})");
    QVERIFY(!model.saving());
    QVERIFY(model.errorMessage().contains(QStringLiteral("其他位置更新")));
    QVERIFY(model.dirty());
    model.restore();
    QVERIFY(!model.dirty());
    model.open(1);
    PlaylistOrderModelTestAccess::complete(model, PlaylistOrderModelTestAccess::request(model),
                                           snapshot);
    QCOMPARE(model.rowCount(), 1);
    const auto collected = PlaylistOrderModelTestAccess::positions(model);
    QCOMPARE(collected.size(), 2);
    QCOMPARE(collected[0].id.value, quint64(5));
    QCOMPARE(collected[1].id.value, quint64(6));
    model.open(0);
    const auto stale = PlaylistOrderModelTestAccess::request(model);
    api.setAccount(900002, "different-domain-token");
    PlaylistOrderModelTestAccess::complete(model, stale, snapshot);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.busy());
    api.shutdown();
}

void DesktopPagesTest::songMenuBlacklistUsesIndexLoadAndSharedWriteChannel()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService accountService(api);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    ContentBlacklistService service(api, accountService);
    QSignalSpy finished(&service, &ContentBlacklistService::menuWriteFinished);
    const QString key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    // 索引未加载时为 Unknown；菜单命令先完整加载索引。
    QCOMPARE(service.songStatus(key), ContentBlacklistService::SongStatus::Unknown);
    service.requestSongBlacklist(key, "歌曲 A", "1", false);
    QVERIFY(service.menuBusy());
    const auto read = ContentBlacklistTestAccess::songIndexRequest(service);
    QVERIFY(read);
    const auto readRequest = ApiClientTestAccess::request(api, read);
    QCOMPARE(readRequest.value("operation").toString(), QString("blacklist_list"));
    QCOMPARE(readRequest.value("query").toObject().value("label").toString(), QString("song"));
    QCOMPARE(readRequest.value("query").toObject().value("page").toInt(), 1);
    QCOMPARE(readRequest.value("query").toObject().value("page_size").toInt(), 500);
    // 读取默认允许自动验证；写入路径永远不自动验证（ApiClient 写策略）。
    QVERIFY(ApiClientTestAccess::automaticVerification(api, read));
    ContentBlacklistTestAccess::complete(
        service, api, read,
        R"({"page":1,"page_size":500,"total":1,"items":[{"key":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","name":"歌曲 B","mix_song_id":"2"}]})");
    // 目标不在列表 → 直接进入写入通道。
    const auto write = ContentBlacklistTestAccess::menuWrite(service);
    QVERIFY(write);
    const auto writeRequest = ApiClientTestAccess::request(api, write);
    QCOMPARE(writeRequest.value("operation").toString(), QString("blacklist"));
    const auto query = writeRequest.value("query").toObject();
    QCOMPARE(query.value("label").toString(), QString("song"));
    QCOMPARE(query.value("key").toString(), key);
    QCOMPARE(query.value("name").toString(), QString("歌曲 A"));
    QCOMPARE(query.value("mix_song_id").toString(), QString("1"));
    QVERIFY(!query.value("remove").toBool());
    QVERIFY(!ApiClientTestAccess::automaticVerification(api, write));
    ContentBlacklistTestAccess::complete(service, api, write, R"({"updated":true})");
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(0).toBool(), true);
    QCOMPARE(finished.first().at(1).toBool(), false);
    QCOMPARE(finished.first().at(2).toString(), QString("已标记为不感兴趣"));
    QCOMPARE(service.songStatus(key), ContentBlacklistService::SongStatus::Present);
    QVERIFY(!service.menuBusy());
    // 重复添加只显示提示，不重复写入。
    service.requestSongBlacklist(key, "歌曲 A", "1", false);
    QCOMPARE(finished.size(), 2);
    QCOMPARE(finished.last().at(1).toBool(), true);
    QCOMPARE(finished.last().at(2).toString(), QString("已标记为不感兴趣，可在个人中心管理"));
    QCOMPARE(ContentBlacklistTestAccess::menuWrite(service), quint64(0));
    // 取消不感兴趣走同一写入通道。
    service.requestSongBlacklist(key, "歌曲 A", "1", true);
    const auto remove = ContentBlacklistTestAccess::menuWrite(service);
    QVERIFY(remove);
    QVERIFY(ApiClientTestAccess::request(api, remove)
                .value("query")
                .toObject()
                .value("remove")
                .toBool());
    ContentBlacklistTestAccess::complete(service, api, remove, R"({"updated":true})");
    QCOMPARE(finished.size(), 3);
    QCOMPARE(finished.last().at(2).toString(), QString("已撤销不感兴趣"));
    QCOMPARE(service.songStatus(key), ContentBlacklistService::SongStatus::Absent);
    // 账号切换清空索引与待决意图，不把旧账号判定带入新会话。
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900002)));
    QCOMPARE(service.songStatus(key), ContentBlacklistService::SongStatus::Unknown);
    service.requestSongBlacklist(key, "歌曲 A", "1", false);
    QVERIFY(ContentBlacklistTestAccess::songIndexRequest(service));
    service.shutdown();
    api.shutdown();
}
