#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::productionPopupsFollowRequestOwnership()
{
    Session session;
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.theme.setMotionEnabled(false);
    session.shell.setMotionEnabled(false);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileEdit"));
    Test::moveNativePointer(session.window, {400, 400});
    const auto popupCount = [&] {
        QSet<QObject *> visited;
        int count = 0;
        const auto visit = [&](auto &&self, QObject *object) -> void {
            if (!object || visited.contains(object))
                return;
            visited.insert(object);
            count += object->inherits("QQuickPopup");
            for (auto *child : object->children())
                self(self, child);
            if (auto *item = qobject_cast<QQuickItem *>(object))
                for (auto *child : item->childItems())
                    self(self, child);
        };
        visit(visit, session.root.data());
        return count;
    };
    QTRY_COMPARE(popupCount(), 0);
    QQuickItem anchor(session.window->contentItem());
    anchor.setSize({32, 32});
    anchor.setPosition({600, 500});
    const QStringList cases{
        "settingsDialog",        "taskCenterDialog",      "playbackPage",
        "playbackQueueDrawer",   "playbackEffectPopup",   "sleepTimerPopup",
        "playbackOptionsPopup",  "playerbarMorePopup",    "sidebarLayoutPopup",
        "sidebarAccountPopover", "accountSwitcherDialog", "logoutConfirmation",
        "playlistCreateDialog",  "playlistOrderDialog",   "playlistRemoveDialog",
        "playlistImportDialog",  "profileEditorDialog",   "profileGradeDialog",
        "profileSocialDrawer",   "profileDeviceDialog",   "listeningPreferencesDialog",
        "contentBlacklistDialog"};
    for (const auto &name : cases) {
        auto *owner = session.root->findChild<QObject *>(name + "Owner");
        if (!owner) {
            auto *page = visual(session.window->contentItem(), "profilePage");
            if (page)
                owner = page->findChild<QObject *>(name + "Owner");
        }
        QVERIFY2(owner, qPrintable(name));
        QVERIFY2(!owner->property("item").value<QObject *>(), qPrintable(name));
        QVariantMap properties;
        if (name.contains("Popup"))
            properties.insert("anchorItem", QVariant::fromValue(&anchor));
        for (int cycle = 0; cycle < 2; ++cycle) {
            QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, properties)));
            QPointer<QObject> popup = owner->property("item").value<QObject *>();
            QVERIFY2(popup, qPrintable(name));
            QTRY_VERIFY(popup->property("opened").toBool());
            QTRY_COMPARE(popupCount(), 1);
            QPointer<QObject> content = popup->property("contentItem").value<QObject *>();
            QVERIFY(content);
            QVERIFY(QMetaObject::invokeMethod(popup, "close"));
            QTRY_VERIFY(popup.isNull());
            QTRY_VERIFY(content.isNull());
            QTRY_COMPARE(popupCount(), 0);
        }
        qInfo() << "Released production popup" << name;
    }
    // Service-side reopen signals must survive the previous UI instance.
    for (int cycle = 0; cycle < 3; ++cycle) {
        session.accounts.library.importer()->reopen();
        QPointer<QObject> popup = session.root->findChild<QObject *>("playlistImportDialog");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(popup.isNull());
        playback.libraryActions()->dialogRequested();
        popup = session.root->findChild<QObject *>("addToPlaylistDialog");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        playback.libraryActions()->completed();
        QTRY_VERIFY(popup.isNull());
        QTRY_COMPARE(popupCount(), 0);
    }
    auto *importer = session.accounts.library.importer();
    PlaylistImportServiceTestAccess::awaitDuplicate(*importer, "生命周期待确认歌单");
    QTRY_COMPARE(popupCount(), 2);
    QPointer<QObject> pendingImport = session.root->findChild<QObject *>("playlistImportDialog");
    QVERIFY(pendingImport);
    session.window->hide();
    QTRY_VERIFY(pendingImport.isNull());
    QTRY_COMPARE(popupCount(), 0);
    QCOMPARE(importer->duplicateName(), QString("生命周期待确认歌单"));
    session.window->show();
    QVERIFY(Test::prepareNativeInput(session.window));
    QTRY_COMPARE(popupCount(), 2);
    pendingImport = session.root->findChild<QObject *>("playlistImportDialog");
    QVERIFY(pendingImport);
    QCOMPARE(pendingImport->property("duplicateDraft").toString(), importer->duplicateName());
    importer->confirmDuplicate(false);
    QTRY_COMPARE(popupCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(pendingImport, "close"));
    QTRY_VERIFY(pendingImport.isNull());
    QTRY_COMPARE(popupCount(), 0);
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::playerMetadataLinksUseProductionRoutes()
{
    Session session;
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("playback.sqlite"));
    playback.setVolume(0);
    const auto path = session.directory.filePath("links.wav");
    QVERIFY(writeFloatingTestWave(path));
    PlaybackTrack track;
    track.id = "123";
    track.songId = 123;
    track.localPath = path;
    track.title = "Song";
    track.artist = QStringLiteral("王菲 / Guest");
    track.album = "Album";
    track.albumId = 789;
    track.catalogMetadata.insert(
        "artist_refs", QJsonArray{QJsonObject{{"id", "1234"}, {"name", QStringLiteral("王菲")}},
                                  QJsonObject{{"id", "0"}, {"name", "Guest"}}});
    ArtistDetailService artistPages(session.api, playback, session.router, session.shell, false);
    artistPages.registerRoutes(session.router);
    AlbumDetailService albumPages(session.api, playback, session.router, false);
    albumPages.registerRoutes(session.router);
    DetailsService details(session.api, playback, session.router, false);
    details.registerRoutes(session.router);
    const auto coverPath = session.directory.filePath("player-cover.png");
    QImage art(120, 120, QImage::Format_RGB32);
    art.fill(QColor("#14a6b8"));
    QVERIFY(art.save(coverPath));
    track.cover = QUrl::fromLocalFile(coverPath).toString();
    QVERIFY(session.create(&playback));
    const auto destroyUi = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1440, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    playback.playTracks({track}, 0, "Metadata", "metadata-links");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
    playback.pause();
    QTRY_VERIFY(!playback.playing());
    const auto click = [&](QQuickItem *item) {
        QVERIFY(item);
        if (item->isEnabled()) {
            QVERIFY(session.clickItem(item));
        } else {
            // Disabled links still receive a real attempted click.
            QVERIFY(session.settleScene());
            const auto point = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
            Test::moveNativePointer(session.window, point);
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
        }
    };
    auto *actions = session.commands.playerbarActions();
    QVERIFY(actions->setPlacement("host/mv", ShellActionModel::Left));
    for (const bool dark : {false, true})
        for (const qreal scale : {1., 1.25}) {
            session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
            session.shell.resetZoom();
            while (session.shell.uiScale() < scale - .001)
                session.shell.zoomIn();
            auto *artists = visual(session.window->contentItem(), "playerbarArtistLinks");
            QTRY_VERIFY((artists = visual(session.window->contentItem(), "playerbarArtistLinks")));
            auto *first = visual(artists, "artistLink-0");
            auto *guest = visual(artists, "artistLink-1");
            QVERIFY(first && guest);
            QVERIFY(first->isEnabled());
            QVERIFY(!guest->isEnabled());
            click(first);
            QTRY_COMPARE(session.router.current()->name(), QString("artist-detail"));
            QCOMPARE(session.router.current()->resourceId(), QString("1234"));
            click(guest);
            QCOMPARE(session.router.current()->name(), QString("artist-detail"));
            click(visual(session.window->contentItem(), "playerbarSongTitle"));
            QTRY_COMPARE(session.router.current()->name(), QString("album-detail"));
            QCOMPARE(session.router.current()->resourceId(), QString("789"));
            auto *mv = visual(session.window->contentItem(), "playerbar-action-mv");
            QTRY_VERIFY((mv = visual(session.window->contentItem(), "playerbar-action-mv")));
            QCOMPARE(mv->property("actionIcon").toString(), QString("brand-youtube"));
            QVERIFY(!playback.playing());
            QCOMPARE(playback.queue()->rowCount(), 1);
            const auto captures = qEnvironmentVariable("ECHO_COVER_CAPTURE_DIR");
            if (!captures.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captures +
                    QString("/player-links-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
        }
}

void DesktopPagesTest::profileWritesRejectInvalidDraftAndCancelOlderRead()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService accountService(api);
    ProfileService profile(api, accountService);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    QSignalSpy editNotices(&profile, &ProfileService::editNotice);
    profile.saveProfile("  ", 2, "", "", "", "");
    QVERIFY(!profile.saving());
    QCOMPARE(profile.editorError(), QStringLiteral("昵称不能为空"));
    QCOMPARE(editNotices.size(), 1);
    QCOMPARE(editNotices.last().at(1).toInt(), int(ToastService::Warning));
    profile.saveProfile(accountService.nickname(), 2, "2099-01-01", "", "", "");
    QCOMPARE(profile.editorError(), QStringLiteral("请选择有效生日"));
    QCOMPARE(editNotices.size(), 2);
    QCOMPARE(editNotices.last().at(1).toInt(), int(ToastService::Warning));
    profile.saveProfile(accountService.nickname(), 2, "", QString(121, 'a'), "", "");
    QCOMPARE(profile.editorError(), QStringLiteral("个性签名最多 120 个字符"));
    QCOMPARE(editNotices.size(), 3);
    QCOMPARE(editNotices.last().at(1).toInt(), int(ToastService::Warning));
    profile.saveProfile(accountService.nickname(), 2, "", "", "", "");
    QCOMPARE(profile.editorError(), QStringLiteral("资料没有变化"));
    QCOMPARE(editNotices.size(), 4);
    QCOMPARE(editNotices.last().at(1).toInt(), int(ToastService::Info));
    const auto olderRead = AccountServiceTestAccess::profileRequest(accountService, api);
    ApiClient::ProfilePatch patch;
    patch.nickname = QStringLiteral("更新后的昵称");
    patch.gender = 0;
    patch.signature = QString();
    const auto write = ProfileServiceTestAccess::saveRequest(profile, api, patch);
    QSignalSpy saved(&profile, &ProfileService::profileSaved);
    ProfileServiceTestAccess::complete(profile, write, R"({"updated":true})");
    QCOMPARE(saved.size(), 1);
    QCOMPARE(editNotices.size(), 5);
    QCOMPARE(editNotices.last().at(1).toInt(), int(ToastService::Success));
    QCOMPARE(accountService.nickname(), QStringLiteral("更新后的昵称"));
    QCOMPARE(accountService.gender(), 0);
    AccountServiceTestAccess::complete(accountService, olderRead,
                                       R"({"id":900001,"nickname":"写入前的旧昵称"})");
    QCOMPARE(accountService.nickname(), QStringLiteral("更新后的昵称"));
    const auto lateWrite = ProfileServiceTestAccess::saveRequest(profile, api, patch);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900002)));
    ProfileServiceTestAccess::complete(profile, lateWrite, R"({"updated":true})");
    QCOMPARE(saved.size(), 1);
    QCOMPARE(accountService.nickname(), QStringLiteral("Qt 测试账号"));
    QVERIFY(!profile.saving());
    profile.shutdown();
    api.shutdown();
}

void DesktopPagesTest::avatarUploadRejectsInvalidFilesAndIgnoresOldAccountCompletion()
{
    ApiClient api;
    AccountService accountService(api);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    ProfileService profile(api, accountService);
    QSignalSpy finished(&profile, &ProfileService::avatarFinished);
    profile.uploadAvatar(QUrl("https://example.invalid/avatar.png"));
    QCOMPARE(finished.size(), 1);
    QVERIFY(!profile.uploadingAvatar());
    QTemporaryDir directory;
    QFile invalid(directory.filePath("invalid.png"));
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    invalid.write("not an image");
    invalid.close();
    profile.uploadAvatar(QUrl::fromLocalFile(invalid.fileName()));
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(!profile.uploadingAvatar());
    QVERIFY(!finished.last().at(0).toBool());
    const auto read = AccountServiceTestAccess::profileRequest(accountService, api);
    const auto upload = ProfileServiceTestAccess::avatarRequest(profile, api);
    ProfileServiceTestAccess::complete(profile, upload, R"({"file_name":"sample.jpg"})");
    QVERIFY(profile.uploadingAvatar());
    QCOMPARE(finished.size(), 2);
    const auto submit = ProfileServiceTestAccess::avatarPending(profile);
    QVERIFY(submit && submit != upload);
    ProfileServiceTestAccess::complete(profile, submit,
                                       "{\"avatar_url\":\"http://c1.kgimg.com/v2/kugouicon/"
                                       "sample.jpg\",\"review_pending\":false}");
    QCOMPARE(finished.size(), 3);
    AccountServiceTestAccess::complete(
        accountService, read,
        "{\"id\":900001,\"avatar_url\":\"https://example.invalid/older.jpg\"}");
    QCOMPARE(accountService.avatarUrl(), QString("http://c1.kgimg.com/v2/kugouicon/sample.jpg"));
    const auto stale = ProfileServiceTestAccess::avatarRequest(profile, api);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900002)));
    const auto avatar = accountService.avatarUrl();
    ProfileServiceTestAccess::complete(
        profile, stale,
        "{\"avatar_url\":\"https://example.invalid/stale.jpg\",\"review_pending\":false}");
    QCOMPARE(accountService.avatarUrl(), avatar);
    QCOMPARE(finished.size(), 3);
    api.shutdown();
}

void DesktopPagesTest::productionCollectionAndSocialEntrypoints()
{
    Session session;
    QTcpServer sink;
    QVERIFY(sink.listen(QHostAddress::LocalHost));
    connect(&sink, &QTcpServer::newConnection, &sink, [&] {
        while (sink.hasPendingConnections()) {
            auto *socket = sink.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                // Keep the tunnel pending while the test supplies domain acknowledgements.
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QVERIFY(session.api.setNetworkTransport(sink.serverPort()));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    session.theme.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("playback.sqlite"));
    SongMetadataService metadata(session.api);
    LibraryCollectionService collections(session.api, session.accounts.playlists,
                                         session.accounts.mutations);
    PlaylistDetailService playlists(session.api, playback, session.accounts.playlists,
                                    session.accounts.mutations, *session.accounts.library.order(),
                                    metadata, session.router, false);
    playlists.registerRoutes(session.router);
    AlbumDetailService albums(session.api, playback, session.router, false);
    albums.registerRoutes(session.router);
    ArtistDetailService artists(session.api, playback, session.router, session.shell, false);
    artists.registerRoutes(session.router);
    playlists.attachCollections(collections);
    albums.attachCollections(collections);
    artists.attachProfile(session.profile);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    UserPlaylistModelTestAccess::present(session.accounts.playlists, session.api, 0);
    ProfileServiceTestAccess::seed(session.profile, 1, {});
    const auto blacklist =
        ContentBlacklistTestAccess::read(*session.profile.blacklist(), session.api, 1);
    ContentBlacklistTestAccess::complete(*session.profile.blacklist(), session.api, blacklist,
                                         R"({"items":[],"total":0,"page":1,"page_size":500})");
    const auto capture = qEnvironmentVariable("ECHO_COLLECTION_SOCIAL_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    const auto click = [&](QQuickItem *item) {
        QVERIFY(item);
        QVERIFY(item->isEnabled());
        QTRY_VERIFY(item->isVisible() && item->width() > 0 && item->height() > 0);
        QTest::qWait(50);
        QSignalSpy pressed(item, SIGNAL(clicked()));
        QVERIFY(pressed.isValid());
        const auto point = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
        Test::moveNativePointer(session.window, point);
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(pressed.count(), 1);
    };
    QJsonArray songs;
    for (int i = 1; i <= 40; ++i)
        songs.append(QJsonObject{{"id", QString::number(i)},
                                 {"title", QString("Song %1").arg(i)},
                                 {"artists", QJsonArray{}},
                                 {"hash", QString(32, 'a')}});
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (const qreal scale : {1., 1.25}) {
            session.theme.setMode(mode);
            session.shell.resetZoom();
            while (session.shell.uiScale() < scale - .001)
                session.shell.zoomIn();
            for (const auto &name :
                 {QString("playlist-detail"), QString("album-detail"), QString("artist-detail")}) {
                QVERIFY(session.router.navigate(name, {},
                                                name == "playlist-detail" ? "public-playlist"
                                                : name == "album-detail"  ? "960399"
                                                                          : "3060"));
                if (name == "playlist-detail") {
                    auto *c = playlists.contextFor(session.router.current());
                    PlaylistDetailContextTestAccess::seed(*c);
                    c->songs()->replaceSongs(songs);
                } else if (name == "album-detail") {
                    auto *c = albums.contextFor(session.router.current());
                    AlbumDetailContextTestAccess::seed(*c);
                    c->songs()->replaceSongs(songs);
                } else {
                    auto *c = artists.contextFor(session.router.current());
                    ArtistDetailContextTestAccess::seed(*c);
                    c->songs()->replaceSongs(songs);
                }
                QQuickItem *activePage = nullptr;
                const auto pageName = name == "playlist-detail" ? "playlistDetailPage"
                                      : name == "album-detail"  ? "albumDetailPage"
                                                                : "artistDetailPage";
                QTRY_VERIFY((activePage = visual(session.window->contentItem(), pageName)) &&
                            activePage->isVisible());
                QTRY_VERIFY(visual(activePage, "songDetailHeader"));
                auto *songView = visual(activePage, "discoverySongs");
                QVERIFY(songView);
                songView->setProperty("contentY", -songView->property("topMargin").toReal());
                const auto buttonName =
                    name == "artist-detail" ? "artistFollowButton" : "detailCollectButton";
                QQuickItem *button = nullptr;
                QTRY_VERIFY((button = visual(activePage, buttonName)) && button->isVisible());
                QVERIFY(button->isEnabled());
                QTRY_VERIFY(button->width() > 20 && button->height() > 20);
                QTest::qWait(100);
                const auto verifyCommand = [&](QQuickItem *target) {
                    qInfo() << "Entry command" << name << target->objectName();
                    click(target);
                    if (name == "artist-detail") {
                        const auto write = ProfileServiceTestAccess::followWrite(session.profile);
                        QVERIFY(write);
                        const auto request = ApiClientTestAccess::request(session.api, write);
                        QCOMPARE(request["operation"].toString(), QString("artist_follow"));
                        QCOMPARE(request["id"].toInteger(), 3060);
                        ProfileServiceTestAccess::fail(session.profile, write,
                                                       R"({"kind":"business"})");
                        session.api.cancel(write);
                    } else {
                        const auto write =
                            PlaylistMutationServiceTestAccess::request(session.accounts.mutations);
                        QVERIFY(write);
                        const auto request = ApiClientTestAccess::request(session.api, write);
                        QCOMPARE(request["operation"].toString(), QString("playlist_add"));
                        QCOMPARE(
                            request["query"].toObject()["collection"].toObject()["source"].toInt(),
                            name == "album-detail" ? 2 : 1);
                        PlaylistMutationServiceTestAccess::reject(session.accounts.mutations);
                        session.api.cancel(write);
                    }
                };
                verifyCommand(button);
                if (QTest::currentTestFailed())
                    return;
                if (name == "artist-detail") {
                    click(visual(activePage, "artistBlockButton"));
                    const auto write =
                        ContentBlacklistTestAccess::singerWrite(*session.profile.blacklist());
                    QVERIFY(write);
                    const auto query =
                        ApiClientTestAccess::request(session.api, write)["query"].toObject();
                    QCOMPARE(query["label"].toString(), QString("singer"));
                    QCOMPARE(query["key"].toString(), QString("3060"));
                    ContentBlacklistTestAccess::fail(*session.profile.blacklist(), session.api,
                                                     write);
                }
                QTest::qWait(100);
                if (!capture.isEmpty())
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/%1-%2-%3.png").arg(name).arg(int(mode)).arg(scale)));
                auto *list = visual(activePage, "discoverySongs");
                QVERIFY(list);
                list->setProperty("contentY", 500);
                const auto collapsed =
                    name == "artist-detail" ? "artistFollowCollapsed" : "detailCollectCollapsed";
                QTRY_VERIFY(visual(activePage, collapsed) &&
                            visual(activePage, collapsed)->isVisible());
                QTest::qWait(100);
                verifyCommand(visual(activePage, collapsed));
                if (QTest::currentTestFailed())
                    return;
                QTest::qWait(150);
                if (!capture.isEmpty())
                    QVERIFY(session.window->grabWindow().save(
                        capture +
                        QString("/%1-collapsed-%2-%3.png").arg(name).arg(int(mode)).arg(scale)));
            }
            session.router.navigate("profile");
            const auto user = QJsonObject{{"key", "user:77"},
                                          {"user_id", 77},
                                          {"nickname", "中文 Friend"},
                                          {"following", true},
                                          {"mutual", true}};
            ProfileServiceTestAccess::seed(session.profile, 0, {user});
            session.profile.openSocial(ProfileService::SocialTab::Friends);
            auto *page = visual(session.window->contentItem(), "profilePage");
            QTRY_VERIFY(page);
            auto *owner = page->findChild<QObject *>("profileSocialDrawerOwner");
            QVERIFY(owner);
            QVERIFY(
                QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariant(QVariantMap{}))));
            QQuickItem *message = nullptr;
            QTRY_VERIFY((message = visual(session.window->contentItem(), "socialMessageButton")));
            QVERIFY(message->isEnabled());
            auto *follow = visual(session.window->contentItem(), "socialFollowButton");
            QVERIFY(follow && follow->isEnabled());
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/social-%1-%2.png").arg(int(mode)).arg(scale)));
            click(message);
            QTRY_VERIFY(session.profile.chatActive());
            const auto read = ProfileServiceTestAccess::chatRead(session.profile);
            QVERIFY(read);
            ProfileServiceTestAccess::complete(session.profile, read,
                                               R"({"records":[],"ended":true})");
            QTRY_VERIFY(visual(session.window->contentItem(), "profileChatDraft"));
            auto *draft = visual(session.window->contentItem(), "profileChatDraft");
            draft->setProperty("text", QString("Hello / 你好"));
            auto *send = visual(session.window->contentItem(), "profileChatSend");
            QVERIFY(send && send->isEnabled());
            click(send);
            const auto write = ProfileServiceTestAccess::chatWrite(session.profile);
            QVERIFY(write);
            QVERIFY(!ApiClientTestAccess::automaticVerification(session.api, write));
            ProfileServiceTestAccess::fail(
                session.profile, write,
                R"({"kind":"business","message":"Isolated display failure"})");
            QCOMPARE(draft->property("text").toString(), QString("Hello / 你好"));
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/chat-%1-%2.png").arg(int(mode)).arg(scale)));
            QVERIFY(Test::prepareNativeInput(session.window));
            draft->forceActiveFocus(Qt::OtherFocusReason);
            QTRY_VERIFY(draft->hasActiveFocus());
            QTest::keyClick(session.window, Qt::Key_Return);
            const auto accepted = ProfileServiceTestAccess::chatWrite(session.profile);
            QVERIFY(accepted);
            ProfileServiceTestAccess::complete(
                session.profile, accepted,
                R"({"msgid":"local-ui-ack","tag":"local-ui-tag","addtime":1792242093})");
            QTRY_COMPARE(draft->property("text").toString(), QString());
            QTRY_COMPARE(session.profile.chatMessages().size(), 1);
            QQuickItem *bubble = nullptr, *meta = nullptr;
            QTRY_VERIFY((bubble = visual(session.window->contentItem(), "profileChatBubble")) &&
                        (meta = visual(session.window->contentItem(), "profileChatMeta")));
            QTest::qWait(100);
            QVERIFY(qAbs(bubble->mapToScene({bubble->width(), 0}).x() -
                         meta->mapToScene({meta->width(), 0}).x()) < .01);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/chat-ack-%1-%2.png").arg(int(mode)).arg(scale)));
            click(visual(session.window->contentItem(), "profileChatBack"));
            QTRY_VERIFY(!session.profile.chatActive());
            click(visual(session.window->contentItem(), "profileSocialClose"));
            QTRY_VERIFY(!owner->property("item").value<QObject *>());
        }
    for (const auto &warning : warnings)
        qWarning().noquote() << warning[0];
    QCOMPARE(warnings.count(), 0);
}

void DesktopPagesTest::socialAndDeviceCachesBelongToAccountAndTab()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService accountService(api);
    ProfileService profile(api, accountService);
    QAbstractItemModelTester socialTester(profile.socialUsers(),
                                          QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester deviceTester(profile.devices(),
                                          QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    const auto friends = ProfileServiceTestAccess::socialRequest(profile, api, 0);
    ProfileServiceTestAccess::complete(profile, friends, socialRows(1000));
    QCOMPARE(profile.socialUsers()->count(), 1000);
    const auto fans = ProfileServiceTestAccess::socialRequest(profile, api, 2);
    profile.openSocial(ProfileService::SocialTab::Fans);
    ProfileServiceTestAccess::complete(profile, fans, socialRows(3));
    QCOMPARE(profile.socialUsers()->count(), 3);
    profile.openSocial(ProfileService::SocialTab::Friends);
    QCOMPARE(profile.socialUsers()->count(), 1000);
    QVERIFY(!profile.socialLoading());
    auto devices = ProfileServiceTestAccess::deviceRequest(profile, api);
    ProfileServiceTestAccess::complete(
        profile, devices,
        R"([{"id":"domain-device","model":"设备","platform":"Android","location":"中国,上海","login_type":1,"active_at_seconds":1700000000,"new_at_seconds":1700000000,"current":true}])");
    QCOMPARE(profile.devices()->count(), 1);
    QVERIFY(
        profile.devices()->data(profile.devices()->index(0), LoginDeviceModel::Current).toBool());
    const auto stale = ProfileServiceTestAccess::socialRequest(profile, api, 0);
    devices = ProfileServiceTestAccess::deviceRequest(profile, api);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900002)));
    ProfileServiceTestAccess::complete(profile, stale, socialRows(200));
    ProfileServiceTestAccess::complete(profile, devices, "[{}]");
    QCOMPARE(profile.socialUsers()->count(), 0);
    QCOMPARE(profile.devices()->count(), 0);
    const auto rejected = ProfileServiceTestAccess::socialRequest(profile, api, 0);
    ProfileServiceTestAccess::fail(profile, rejected, R"({"kind":"business"})");
    QCOMPARE(profile.socialError(), QStringLiteral("列表加载失败，请稍后重试"));
    profile.shutdown();
    api.shutdown();
}

void DesktopPagesTest::productionSidebarImportEntranceAndModes()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    const auto click = [&session](const char *name) {
        auto *item = visual(session.window->contentItem(), QString::fromLatin1(name));
        if (!item)
            return false;
        QTest::mouseClick(
            session.window, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        return true;
    };
    QVERIFY(click("sidebarPlaylistAdd"));
    auto *menu = session.root->findChild<QObject *>("sidebarPlaylistAddMenu");
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    const auto capture = qEnvironmentVariable("ECHO_SIDEBAR_ORDER_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1., 1.25}) {
            menu->setProperty("uiScale", scale);
            QTest::qWait(100);
            QList<qreal> leftEdges;
            for (const auto *name : {"sidebarPlaylistNew", "sidebarPlaylistImport"}) {
                auto *row = visual(session.window->contentItem(), QString::fromLatin1(name));
                QVERIFY(row);
                auto *icon = visual(row, "sidebarPlaylistActionIcon");
                auto *title = visual(row, "sidebarPlaylistActionTitle");
                auto *description = visual(row, "sidebarPlaylistActionDescription");
                QVERIFY(icon && title && description);
                const auto titleLeft = title->mapToScene(QPointF()).x();
                const auto descriptionLeft = description->mapToScene(QPointF()).x();
                QVERIFY(qAbs(titleLeft - descriptionLeft) < .5);
                leftEdges.append(titleLeft);
                const auto textCenter =
                    (title->mapToScene(QPointF()).y() +
                     description->mapToScene(QPointF(0, description->height())).y()) /
                    2;
                const auto iconCenter = icon->mapToScene(QPointF(0, icon->height() / 2)).y();
                QVERIFY2(qAbs(textCenter - iconCenter) < .5,
                         qPrintable(QString("%1 text=%2 icon=%3 title=%4 description=%5")
                                        .arg(name)
                                        .arg(textCenter)
                                        .arg(iconCenter)
                                        .arg(title->height())
                                        .arg(description->height())));
            }
            QVERIFY(qAbs(leftEdges[0] - leftEdges[1]) < .5);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/playlist-add-%1-%2.png")
                                  .arg(theme == ThemeStore::Light ? "light" : "dark")
                                  .arg(scale)));
        }
    }
    menu->setProperty("uiScale", 1.);
    QVERIFY(click("sidebarPlaylistImport"));
    auto *dialog = session.root->findChild<QObject *>("playlistImportDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *link = visual(session.window->contentItem(), "playlistImportLink");
    QVERIFY(link);
    auto *start = visual(session.window->contentItem(), "playlistImportStart");
    QVERIFY(start);
    QVERIFY(!start->isEnabled());
    dialog->setProperty("linkText", "https://example.invalid/playlist");
    QTRY_VERIFY(start->isEnabled());
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (int mode = 0; mode < 2; ++mode) {
            dialog->setProperty("mode", mode);
            QTest::qWait(100);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/import-%1-%2.png")
                                  .arg(theme == ThemeStore::Light ? "light" : "dark")
                                  .arg(mode)));
        }
    }
    // Use the production FileDialog accepted handler without uploading or
    // creating a playlist. The discarded tenth file must not be validated.
    QTemporaryDir screenshots;
    QList<QUrl> selectedFiles;
    QImage screenshot(16, 16, QImage::Format_RGB32);
    screenshot.fill(Qt::white);
    for (int i = 0; i < 9; ++i) {
        const auto path = screenshots.filePath(QString("shot-%1.png").arg(i));
        QVERIFY(screenshot.save(path));
        selectedFiles.append(QUrl::fromLocalFile(path));
    }
    const auto oversizedPath = screenshots.filePath("oversized.png");
    QFile oversized(oversizedPath);
    QVERIFY(oversized.open(QIODevice::WriteOnly));
    QVERIFY(oversized.resize(10 * 1024 * 1024 + 1));
    oversized.close();
    selectedFiles.append(QUrl::fromLocalFile(oversizedPath));
    dialog->setProperty("mode", 1);
    dialog->setProperty("targetMode", 1);
    dialog->setProperty("newName", "截图选择回归");
    const auto selectFiles = [&](const QList<QUrl> &files) {
        return QMetaObject::invokeMethod(dialog, "selectScreenshots",
                                         Q_ARG(QVariant, QVariant::fromValue(files)));
    };
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1., 1.25}) {
            dialog->setProperty("uiScale", scale);
            QVERIFY(selectFiles(selectedFiles));
            QCOMPARE(dialog->property("files").value<QList<QUrl>>(), selectedFiles.mid(0, 9));
            auto *selectionMessage =
                visual(session.window->contentItem(), "playlistImportInputError");
            QVERIFY(selectionMessage);
            QTRY_VERIFY(selectionMessage->isVisible());
            QCOMPARE(selectionMessage->property("text").toString(),
                     QString("一次最多选择 9 张截图"));
            QVERIFY(start->isEnabled());
            QTest::qWait(100);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/import-selection-%1-%2.png")
                                  .arg(theme == ThemeStore::Light ? "light" : "dark")
                                  .arg(scale)));
            QVERIFY(selectFiles({selectedFiles.last(), selectedFiles.first()}));
            QVERIFY(dialog->property("files").value<QList<QUrl>>().isEmpty());
            QTRY_VERIFY(selectionMessage->isVisible());
            QCOMPARE(selectionMessage->property("text").toString(),
                     QString("oversized.png 超过 10 MB"));
            QVERIFY(!start->isEnabled());
            QVERIFY(selectFiles({selectedFiles.first()}));
            QCOMPARE(dialog->property("files").value<QList<QUrl>>().size(), 1);
            QTRY_VERIFY(!selectionMessage->isVisible());
            QVERIFY(start->isEnabled());
        }
    }
    dialog->setProperty("uiScale", 1.);
    dialog->setProperty("mode", 0);
    session.theme.setMode(ThemeStore::Light);
    QTest::mouseClick(
        session.window, Qt::LeftButton, Qt::NoModifier,
        start->mapToScene(QPointF(start->width() / 2, start->height() / 2)).toPoint());
    auto &importer = *session.accounts.library.importer();
    QVERIFY(importer.busy());
    PlaylistImportServiceTestAccess::complete(
        importer, R"({"task_id":71,"user_id":900001,"cloud_list_id":0})");
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"([{"task_id":71,"user_id":900001,"cloud_list_id":101,"status":2,"title":"领域导入展示","total":3,"imported":1,"missed":0}])");
    PlaylistImportServiceTestAccess::pausePoll(importer);
    QTest::qWait(100);
    if (!capture.isEmpty())
        QVERIFY(session.window->grabWindow().save(capture + "/import-progress-light.png"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QVERIFY(importer.busy());
    importer.reopen();
    QTRY_VERIFY(dialog->property("opened").toBool());
    PlaylistImportServiceTestAccess::poll(importer);
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"([{"task_id":71,"user_id":900001,"cloud_list_id":101,"status":3,"title":"领域导入展示","total":3,"imported":2,"missed":1}])");
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"({"task":{"task_id":71,"user_id":900001,"cloud_list_id":101},"page":1,"page_size":100,"missed_tracks":[{"title":"未匹配展示歌曲","artist":"歌手","reason":"没有结果"}]})");
    QCOMPARE(importer.phase(), PlaylistImportService::Completed);
    QTest::qWait(100);
    auto *results = visual(session.window->contentItem(), "playlistImportMissed");
    QVERIFY(results);
    QVERIFY(results->height() >= 4 * 58);
    QCOMPARE(results->property("count").toInt(), 1);
    if (!capture.isEmpty())
        QVERIFY(session.window->grabWindow().save(capture + "/import-result-light.png"));
    importer.dismiss();
    QVERIFY(importer.startLink("https://example.invalid/playlist"));
    PlaylistImportServiceTestAccess::complete(
        importer, R"({"task_id":72,"user_id":900001,"cloud_list_id":0})");
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"([{"task_id":72,"user_id":900001,"cloud_list_id":0,"status":11,"title":"","total":0,"imported":0,"missed":0}])");
    QCOMPARE(importer.phase(), PlaylistImportService::Failed);
    QCOMPARE(importer.stages().size(), 1);
    auto *warningImage = visual(session.window->contentItem(), "playlistImportSummaryIcon");
    QVERIFY(warningImage);
    QTRY_COMPARE(warningImage->property("status").toInt(), 1);
    QTest::qWait(100);
    if (!capture.isEmpty())
        QVERIFY(session.window->grabWindow().save(capture + "/import-failed-light.png"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionSidebarOrderEditor()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    auto *button = visual(session.window->contentItem(), "sidebarPlaylistOrder");
    QVERIFY(button && button->isEnabled());
    QTest::mouseClick(
        session.window, Qt::LeftButton, Qt::NoModifier,
        button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
    auto *dialog = session.root->findChild<QObject *>("playlistOrderDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto &model = *session.accounts.library.order();
    QJsonArray items;
    for (int i = 0; i < 1000; ++i)
        items.append(QJsonObject{{"cloud_list_id", i + 1},
                                 {"kind", "owned"},
                                 {"title", QString("歌单 %1").arg(i + 1)},
                                 {"sort_index", i},
                                 {"song_count", i},
                                 {"source_code", 1},
                                 {"default_kind", QJsonValue::Null}});
    for (int page = 1; page <= 5; ++page) {
        QJsonArray chunk;
        for (int i = (page - 1) * 200; i < page * 200; ++i)
            chunk.append(items[i]);
        PlaylistOrderModelTestAccess::complete(
            model, PlaylistOrderModelTestAccess::request(model),
            QJsonDocument(QJsonObject{{"user_id", 900001},
                                      {"total_revision", 20},
                                      {"page", QJsonObject{{"page", page},
                                                           {"page_size", 200},
                                                           {"total", 1000},
                                                           {"items", chunk}}}})
                .toJson());
    }
    QTRY_COMPARE(model.rowCount(), 1000);
    auto *list = visual(session.window->contentItem(), "playlistOrderList");
    QVERIFY(list);
    QTRY_VERIFY(visualCount(list, "playlistOrderRow") > 0);
    QVERIFY(visualCount(list, "playlistOrderRow") < 40);
    auto *position = visual(session.window->contentItem(), "playlistOrderPosition");
    QVERIFY(position);
    model.select(4);
    QTRY_COMPARE(position->property("value").toInt(), 5);
    auto *number = visual(position, "numberInputField");
    QVERIFY(number);
    number->setProperty("text", "7");
    QVERIFY(QMetaObject::invokeMethod(position, "commit"));
    QCOMPARE(position->property("value").toInt(), 7);
    QCOMPARE(model.selected(), 4);
    model.select(20);
    QTRY_COMPARE(position->property("value").toInt(), 21);
    QTRY_COMPARE(number->property("text").toString(), QString("21"));
    model.select(0);
    const auto from = list->mapToScene(QPointF(100, 26)).toPoint();
    const auto to = list->mapToScene(QPointF(100, 182)).toPoint();
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(session.window, from + QPoint(0, 15), 20);
    QTest::mouseMove(session.window, to, 30);
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, to);
    QTRY_COMPARE(model.data(model.index(3), PlaylistOrderModel::Title).toString(),
                 QString("歌单 1"));
    model.restore();

    const auto capture = qEnvironmentVariable("ECHO_SIDEBAR_ORDER_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            QTest::qWait(120);
            QVERIFY(session.window->grabWindow().save(
                capture + (mode == ThemeStore::Light ? "/order-light.png" : "/order-dark.png")));
        }
    }
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QTRY_COMPARE(model.rowCount(), 0);
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::sidebarHomeAndDragTransaction()
{
    QTemporaryDir temp;
    const auto path = temp.filePath("preferences.ini");
    QSettings preferences(path, QSettings::IniFormat);
    preferences.setValue("sidebar/shortcuts", QStringList{"recommend", "explore", "home"});
    SidebarLayoutModel layout(preferences, nullptr);
    QAbstractItemModelTester cards(layout.shortcutModel(),
                                   QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(layout.homeRoute(), QString("recommend-songs"));
    QVERIFY(layout.beginShortcutDrag("home"));
    layout.previewShortcutMove("home", "recommend");
    QCOMPARE(layout.homeRoute(), QString("home"));
    // A preview must neither write the temporary order nor reset delegates.
    QCOMPARE(preferences.value("sidebar/shortcuts").toStringList().first(), QString("recommend"));
    layout.toggleHidden("favorites"); // Another layout write must not commit this drag.
    QCOMPARE(preferences.value("sidebar/shortcuts").toStringList().first(), QString("recommend"));
    layout.endShortcutDrag(false);
    QCOMPARE(layout.homeRoute(), QString("recommend-songs"));
    QVERIFY(layout.beginShortcutDrag("explore"));
    QVERIFY(!layout.beginShortcutDrag("home"));
    layout.previewShortcutMove("home", "recommend"); // Foreign gesture ignored.
    QCOMPARE(layout.homeRoute(), QString("recommend-songs"));
    layout.previewShortcutMove("explore", "recommend");
    layout.endShortcutDrag(true);
    preferences.sync();
    QSettings restoredPreferences(path, QSettings::IniFormat);
    SidebarLayoutModel restored(restoredPreferences, nullptr);
    QCOMPARE(restored.homeRoute(), QString("explore"));
    RouteController router;
    QVERIFY(router.registerPage(
        {"explore", "/main/explore", QUrl("qrc:/qt/qml/EchoMusic/Discovery/ExplorePage.qml")}));
    QVERIFY(router.navigate(restored.homeRoute()));
    QCOMPARE(router.current()->name(), QString("explore"));
    QVERIFY(!router.canGoBack()); // No unwanted recommendation page in startup history.
    const auto guestPage = router.current()->instanceId();
    router.invalidateAccountPreservingRoute();
    QCOMPARE(router.current()->name(), QString("explore"));
    QVERIFY(router.current()->instanceId() != guestPage);
    QVERIFY(!router.canGoBack());
    QCOMPARE(router.mountedCount(), 1);
}

void DesktopPagesTest::sidebarHistoryNavigationRevealsPlaylistSection_data()
{
    QTest::addColumn<bool>("collapsed");
    QTest::addColumn<bool>("darkScaled");
    QTest::newRow("expanded-light-100") << false << false;
    QTest::newRow("expanded-dark-125") << false << true;
    QTest::newRow("rail-light-100") << true << false;
    QTest::newRow("rail-dark-125") << true << true;
}

void DesktopPagesTest::sidebarHistoryNavigationRevealsPlaylistSection()
{
    QFETCH(bool, collapsed);
    QFETCH(bool, darkScaled);
    Session session;
    session.shell.setMotionEnabled(false);
    if (collapsed)
        session.shell.toggleSidebar();
    if (darkScaled)
        while (session.shell.uiScale() < 1.25 - .001)
            session.shell.zoomIn();
    session.theme.setMode(darkScaled ? ThemeStore::Dark : ThemeStore::Light);
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("playback.sqlite"));
    SongMetadataService songMetadata(session.api);
    PlaylistDetailService details(session.api, playback, session.accounts.playlists,
                                  session.accounts.mutations, *session.accounts.library.order(),
                                  songMetadata, session.router, false);
    details.registerRoutes(session.router);
    QVERIFY(session.create(&playback));
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    UserPlaylistModelTestAccess::present(session.accounts.playlists, session.api, 3);
    UserPlaylistModelTestAccess::collectSecond(session.accounts.playlists);
    connect(&session.accounts.library, &LibraryController::playlistRequested, &session.router,
            [&](const QString &id, quint64, bool) {
                session.router.navigate("playlist-detail", {}, id);
            });
    const auto click = [&](const QString &name) {
        auto *item = visual(session.window->contentItem(), name);
        if (!item || !item->isVisible() || !item->isEnabled())
            return false;
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
        return true;
    };
    const auto selected = [&](const QString &id) {
        const auto matches = [&](const auto &self, QQuickItem *row) -> bool {
            if (row->objectName() == "sidebarPlaylistRow" && row->isVisible() &&
                row->property("selected").toBool() &&
                (row->property("kind").toString() == "owned"
                     ? row->property("globalId").toString()
                     : row->property("creatorGlobalId").toString()) == id)
                return true;
            for (auto *child : row->childItems())
                if (self(self, child))
                    return true;
            return false;
        };
        return matches(matches, session.window->contentItem());
    };
    QVERIFY(session.accounts.library.openPlaylist(1));
    QTRY_COMPARE(session.accounts.library.tab(), 0);
    QTRY_VERIFY(visual(session.window->contentItem(), "playlistDetailPage"));
    QTRY_VERIFY(selected("display-playlist-1"));
    QVERIFY(click(collapsed ? "sidebarRailPlaylistTab-1" : "sidebarCollectedTab"));
    QTRY_COMPARE(session.accounts.library.tab(), 1);
    const auto reloadCatalog = [&] {
        const auto routeKey = session.router.current()->key();
        const int selectedTab = session.accounts.library.tab();
        if (collapsed)
            session.accounts.library.reload();
        else if (!click("sidebarPlaylistRefresh"))
            return false;
        if (session.accounts.playlists.rowCount() != 0)
            return false;
        UserPlaylistModelTestAccess::present(session.accounts.playlists, session.api, 3);
        UserPlaylistModelTestAccess::collectSecond(session.accounts.playlists);
        return session.accounts.library.tab() == selectedTab &&
               session.router.current()->key() == routeKey;
    };
    QVERIFY(reloadCatalog());
    QVERIFY(session.accounts.library.openPlaylist(2));
    QTRY_VERIFY(selected("history-collected-public"));
    QVERIFY(click("titlebarBackAction"));
    QTRY_COMPARE(session.router.current()->resourceId(), QString("display-playlist-1"));
    QTRY_COMPARE(session.accounts.library.tab(), 0);
    QTRY_VERIFY(selected("display-playlist-1"));
    QVERIFY(click("titlebarForwardAction"));
    QTRY_COMPARE(session.router.current()->resourceId(), QString("history-collected-public"));
    QTRY_COMPARE(session.accounts.library.tab(), 1);
    QTRY_VERIFY(selected("history-collected-public"));
    QVERIFY(click(collapsed ? "sidebarRailPlaylistTab-0" : "sidebarCreatedTab"));
    QTRY_COMPARE(session.accounts.library.tab(), 0);
    QVERIFY(reloadCatalog());
    QVERIFY(click("titlebarBackAction"));
    QTRY_COMPARE(session.accounts.library.tab(), 0);
    QVERIFY(click("titlebarForwardAction"));
    QTRY_COMPARE(session.accounts.library.tab(), 1);
    QTRY_VERIFY(selected("history-collected-public"));
    const auto captures = qEnvironmentVariable("ECHO_SIDEBAR_HISTORY_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        QVERIFY(QDir().mkpath(captures));
        session.window->update();
        QTest::qWait(50);
        QVERIFY(session.window->grabWindow().save(
            captures + '/' + QString::fromLatin1(QTest::currentDataTag()) + ".png"));
    }
    session.router.shutdown();
    session.root.reset();
    session.window = nullptr;
    details.shutdown();
}

void DesktopPagesTest::productionSidebarScrollbar()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    UserPlaylistModelTestAccess::present(session.accounts.playlists, session.api, 1000);
    auto *scroll = visual(session.window->contentItem(), "sidebarPlaylistScroll");
    auto *bar = visual(scroll, "sidebarScrollBar");
    QVERIFY(scroll && bar);
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            for (const bool collapsed : {false, true}) {
                if (session.shell.sidebarCollapsed() != collapsed)
                    session.shell.toggleSidebar();
                QTRY_COMPARE(scroll->width(), collapsed ? 80. : 230.);
                QTRY_VERIFY(bar->isVisible());
                QVERIFY(bar->property("size").toReal() < 1);
                QTRY_COMPARE(bar->x() + bar->width(), scroll->width());
                QTest::mouseMove(session.window, scroll->mapToScene({40, 200}).toPoint());
                QTRY_VERIFY(bar->property("revealed").toBool());
                const auto captures = qEnvironmentVariable("ECHO_SIDEBAR_SCROLLBAR_CAPTURE_DIR");
                if (!captures.isEmpty()) {
                    QVERIFY(QDir().mkpath(captures));
                    QVERIFY(session.window->grabWindow().save(
                        captures + QString("/%1-%2-%3.png")
                                       .arg(mode == ThemeStore::Light ? "light" : "dark")
                                       .arg(zoomed ? "125" : "100")
                                       .arg(collapsed ? "rail" : "expanded")));
                }
                QTest::mouseMove(session.window, QPoint(700, 50));
                QTRY_VERIFY(!bar->property("revealed").toBool());
            }
        }
    }
    session.window->hide();
    QTRY_VERIFY(!bar->property("resourceActive").toBool());
    session.window->show();
    QTRY_VERIFY(bar->property("resourceActive").toBool());
    UserPlaylistModelTestAccess::present(session.accounts.playlists, session.api, 0);
    session.window->resize(1150, 1200);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() <= scroll->height());
    QTRY_VERIFY(!bar->isVisible());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionSidebarCardsMenusAndLayout()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    UserPlaylistModelTestAccess::present(session.accounts.playlists, session.api, 1000);
    session.theme.setMode(ThemeStore::Light);
    auto *layout = session.shell.sidebarLayout();
    const auto item = [&session](const QString &name) {
        return visual(session.window->contentItem(), name);
    };
    const auto center = [](QQuickItem *target) {
        return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    };
    const auto capture = [&session](const QString &name) {
        const auto directory = qEnvironmentVariable("ECHO_SIDEBAR_COMPLETE_CAPTURE_DIR");
        if (directory.isEmpty())
            return true;
        QDir().mkpath(directory);
        session.window->update();
        QTest::qWait(100);
        return session.window->grabWindow().save(directory + "/" + name + ".png");
    };
    QTRY_VERIFY(item("sidebarPlaylistRow"));
    QVERIFY(visualCount(session.window->contentItem(), "sidebarPlaylistRow") < 30);
    QCOMPARE(session.accounts.library.playlists()->rowCount(), 1000);
    auto *scroll = item("sidebarPlaylistScroll");
    QVERIFY(scroll);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
    QTest::qWait(100);
    const qreal before = scroll->property("contentY").toReal();
    const auto wheelPoint = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
    QTest::mouseMove(session.window, wheelPoint.toPoint());
    QWheelEvent wheel(wheelPoint, session.window->mapToGlobal(wheelPoint.toPoint()), QPoint(),
                      QPoint(0, -480), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(session.window, &wheel);
    QTRY_VERIFY(scroll->property("contentY").toReal() > before);
    QVERIFY(QMetaObject::invokeMethod(scroll, "cancelFlick"));
    // The inline header changes ListView's origin while its delegates
    // polish. Restore the beginning including that header, not stale contentY.
    QVERIFY(QMetaObject::invokeMethod(scroll, "positionViewAtBeginning"));
    QTRY_VERIFY(!scroll->property("moving").toBool());
    auto *sidebarBar = item("sidebarScrollBar");
    QVERIFY(sidebarBar);
    QTRY_VERIFY(sidebarBar->isVisible());
    QVERIFY(sidebarBar->property("size").toReal() < 1);
    QCOMPARE(sidebarBar->x() + sidebarBar->width(), scroll->width());
    QTRY_VERIFY(sidebarBar->property("revealed").toBool());
    QTest::mouseMove(session.window, QPoint(700, 50));
    QTRY_VERIFY(!sidebarBar->property("revealed").toBool());

    QCOMPARE(layout->sections()[1].toMap()["items"].toList().size(), 2);
    for (const auto &key : {"home", "explore", "free-listen", "personal-fm", "purchased"})
        QVERIFY(!item(QString("sidebarMenu-") + key));
    for (const auto &key : {"favorites", "cloud", "history"})
        QVERIFY(item(QString("sidebarMenu-") + key));
    auto *cards = item("sidebarShortcuts");
    auto *menu = item("sidebarMusicMenu");
    auto *header = item("sidebarPlaylistHeader");
    QVERIFY(cards && menu && header);
    QTRY_COMPARE(
        menu->mapToScene(QPointF()).y() - cards->mapToScene(QPointF(0, cards->height())).y(), 24.0);
    QTRY_COMPARE(
        header->mapToScene(QPointF()).y() - menu->mapToScene(QPointF(0, menu->height())).y(), 18.0);
    QCOMPARE(item("sidebarShortcutAdd")->height(), 28.0);
    const auto verifyPlaylistInset = [&item](bool collapsed) {
        auto *row = item("sidebarPlaylistRow");
        auto *cover = item("sidebarPlaylistCover");
        if (!row || !cover || !row->parentItem())
            return false;
        const auto width = row->parentItem()->width();
        const auto expectedX = collapsed ? (width - 42) / 2 : 18;
        if (!collapsed) {
            auto *tab = item("sidebarCreatedTab");
            auto *text = tab ? tab->property("contentItem").value<QQuickItem *>() : nullptr;
            if (!text ||
                qAbs(cover->mapToScene(QPointF()).x() - text->mapToScene(QPointF()).x()) > .01)
                return false;
        }
        return qAbs(row->x() - expectedX) < 0.01 &&
               qAbs(cover->mapToItem(row, QPointF()).x() - (collapsed ? 5 : 14)) < 0.01 &&
               qAbs(row->width() - (collapsed ? 42 : width - 34)) < 0.01;
    };
    QTRY_VERIFY(verifyPlaylistInset(false));
    QVERIFY(capture("sidebar-light"));
    session.theme.setMode(ThemeStore::Dark);
    QVERIFY(capture("sidebar-dark"));
    // Real pointer drag on the card body, without entering an editor.
    auto *home = item("sidebarShortcut-home");
    auto *explore = item("sidebarShortcut-explore");
    QVERIFY(home && explore);
    // Initial responsive sidebar width/Flow polish must finish before a
    // real pointer gesture. Captures used to incidentally supply this wait.
    QTRY_COMPARE(home->width(), 93.);
    QTRY_COMPARE(explore->x(), 101.);

    QPointer<QQuickItem> keptHome(home);
    QPointer<QQuickItem> keptExplore(explore);
    const auto headerStartY = cards->mapToScene(QPointF()).y();
    const auto start = center(home);
    const auto end =
        explore->mapToScene(QPointF(explore->width() * 0.8, explore->height() / 2)).toPoint();
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(session.window, start + QPoint(10, 0), 20);
    QTest::mouseMove(session.window, end, 40);
    auto *cardGhost = visual(cards, "home");
    QVERIFY(cardGhost);
    QVERIFY(cardGhost->isVisible());
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_COMPARE(layout->shortcuts()[0].toMap()["key"].toString(), QString("explore"));
    QCOMPARE(session.router.current()->name(), QString("profile"));
    QVERIFY(keptHome && keptExplore);
    QCOMPARE(item("sidebarShortcut-home"), keptHome.data());
    QCOMPARE(item("sidebarShortcut-explore"), keptExplore.data());
    home = item("sidebarShortcut-home");
    QVERIFY(Test::prepareNativeInput(session.window));
    QTest::mouseMove(session.window, QPoint(700, 50), 20);
    QTest::mouseMove(session.window, center(home), 20);
    QTRY_VERIFY(item("sidebarShortcutRemove-home")->isVisible());
    QVERIFY(capture("sidebar-card-hover"));
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      center(item("sidebarShortcutRemove-home")));
    QTRY_COMPARE(layout->shortcuts().size(), 1);
    QTRY_COMPARE(item("sidebarShortcutAdd")->height(), 46.0);
    // Odd card counts used to allocate nearly all width to the add button.
    QTRY_COMPARE(item("sidebarShortcut-explore")->width(), item("sidebarShortcutAdd")->width());
    QCOMPARE(item("sidebarShortcut-explore")->width(), 93.0);
    QTest::mouseMove(session.window, center(item("sidebarShortcut-explore")), 20);
    QVERIFY(!item("sidebarShortcutRemove-explore")->isVisible());
    layout->toggleShortcut("explore");
    QCOMPARE(layout->shortcuts().size(), 1);
    layout->restoreShortcuts();
    QTRY_COMPARE(cards->mapToScene(QPointF()).y(), headerStartY);
    auto *popupOwner = session.root->findChild<QObject *>("sidebarLayoutPopupOwner");
    auto *popupTrigger = item("sidebarLayoutTrigger");
    QVERIFY(popupOwner && popupTrigger);
    const auto nativePopupCount = [popupOwner] {
        int count = 0;
        for (auto *child : popupOwner->findChildren<QObject *>())
            count += child->inherits("QQuickPopup");
        return count;
    };
    QVERIFY(!popupOwner->property("item").value<QObject *>());
    QVERIFY(!session.root->findChild<QObject *>("sidebarLayoutPopup"));
    QCOMPARE(nativePopupCount(), 0);
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, center(popupTrigger));
    QTRY_VERIFY(popupOwner->property("item").value<QObject *>());
    QPointer<QObject> popup = popupOwner->property("item").value<QObject *>();
    QTRY_VERIFY(popup->property("opened").toBool());
    QCOMPARE(nativePopupCount(), 1);
    QPointer<QObject> popupContent = popup->property("contentItem").value<QObject *>();
    QPointer<QObject> popupBackground = popup->property("background").value<QObject *>();
    QVERIFY(popupContent && popupBackground);
    auto *section = item("sidebarEditorSection-library");
    auto *groupEye = item("sidebarSectionVisibility-library");
    auto *rowEye = item("sidebarVisibility-favorites");
    QVERIFY(section && groupEye && rowEye);
    QCOMPARE(groupEye->mapToScene(QPointF(groupEye->width(), 0)).x(),
             rowEye->mapToScene(QPointF(rowEye->width(), 0)).x());
    QCOMPARE(item("shellDragHandle-favorites")->width(), 16.0);
    QVERIFY(capture("sidebar-editor-dark"));
    auto *historyPointer = item("shellDragPointer-history");
    auto *first = item("sidebarEditorItem-favorites");
    QVERIFY(historyPointer && first);
    QCOMPARE(popup->property("side").toInt(), 1);
    const auto anchor = popup->property("anchorRect").toRectF();
    QCOMPARE(popup->property("x").toReal(), anchor.right() + 8);
    const auto dragStart = center(historyPointer);
    const auto dragEnd = first->mapToScene(QPointF(first->width() / 2, 5)).toPoint();
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(session.window, dragStart + QPoint(0, -10), 20);
    QTest::mouseMove(session.window, dragEnd, 40);
    auto *dragVisual = historyPointer->parentItem()->findChild<QQuickItem *>("history");
    QVERIFY(dragVisual);
    QVERIFY(dragVisual->isVisible());
    QVERIFY(QQmlProperty(dragVisual, "Drag.active", qmlContext(dragVisual)).read().toBool());
    QTest::mouseMove(session.window, dragEnd + QPoint(1, 1), 20);
    QVERIFY(capture("sidebar-editor-dragging"));
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, dragEnd);
    QTRY_COMPARE(layout->data(layout->index(0), SidebarLayoutModel::Key).toString(),
                 QString("history"));
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      center(item("sidebarVisibility-playlist:created:default")));
    QTRY_COMPARE(session.accounts.library.playlists()->rowCount(), 999);
    QCOMPARE(session.accounts.playlists.rowCount(), 1000);
    QVERIFY(item("sidebarEditorItem-playlist:created:default"));
    QVERIFY(capture("sidebar-editor-hidden-default"));
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      center(item("sidebarSectionVisibility-playlists")));
    QTRY_VERIFY(!header->isVisible());
    QCOMPARE(session.accounts.playlists.rowCount(), 1000);
    layout->restoreDefaults();
    session.theme.setMode(ThemeStore::Light);
    QVERIFY(capture("sidebar-editor-light"));
    QTest::keyClick(session.window, Qt::Key_Escape);
    QTRY_VERIFY(popup.isNull());
    QTRY_VERIFY(popupContent.isNull());
    QTRY_VERIFY(popupBackground.isNull());
    QVERIFY(!popupOwner->property("item").value<QObject *>());
    QVERIFY(!session.root->findChild<QObject *>("sidebarLayoutPopup"));
    QCOMPARE(nativePopupCount(), 0);
    // Repeat the source-card gesture and popup anchoring in scaled coordinates.
    for (int i = 0; i < 5; ++i)
        session.shell.zoomIn();
    QCOMPARE(session.shell.uiScale(), 1.25);
    QTest::qWait(100);
    home = item("sidebarShortcut-home");
    explore = item("sidebarShortcut-explore");
    const auto scaledStart = center(home);
    const auto scaledEnd =
        explore->mapToScene(QPointF(explore->width() * 0.8, explore->height() / 2)).toPoint();
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, scaledStart);
    QTest::mouseMove(session.window, scaledStart + QPoint(12, 0), 20);
    QTest::mouseMove(session.window, scaledEnd, 40);
    // Native Cocoa updates Drag enter/leave during the next scene polish.
    QTest::qWait(40);
    QTest::mouseMove(session.window, scaledEnd + QPoint(1, 0), 20);
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, scaledEnd);
    QTRY_COMPARE(layout->shortcuts()[0].toMap()["key"].toString(), QString("explore"));
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, center(popupTrigger));
    QTRY_VERIFY(popupOwner->property("item").value<QObject *>());
    popup = popupOwner->property("item").value<QObject *>();
    QTRY_VERIFY(popup->property("opened").toBool());
    QCOMPARE(nativePopupCount(), 1);
    popupContent = popup->property("contentItem").value<QObject *>();
    popupBackground = popup->property("background").value<QObject *>();
    QVERIFY(popupContent && popupBackground);
    const auto scaledAnchor = popup->property("anchorRect").toRectF();
    QCOMPARE(popup->property("x").toReal(), scaledAnchor.right() + 8 * session.shell.uiScale());
    QTRY_VERIFY(verifyPlaylistInset(false));
    QVERIFY(capture("sidebar-editor-scaled-light"));
    QTest::keyClick(session.window, Qt::Key_Escape);
    QTRY_VERIFY(popup.isNull());
    QTRY_VERIFY(popupContent.isNull());
    QTRY_VERIFY(popupBackground.isNull());
    QVERIFY(!popupOwner->property("item").value<QObject *>());
    QVERIFY(!session.root->findChild<QObject *>("sidebarLayoutPopup"));
    QCOMPARE(nativePopupCount(), 0);
    session.shell.resetZoom();
    session.shell.toggleSidebar();
    QTRY_COMPARE(item("sidebarPlaylistHeader")->height(), 30.0);
    QTRY_VERIFY(sidebarBar->isVisible());
    QTRY_COMPARE(sidebarBar->x() + sidebarBar->width(), scroll->width());
    QTRY_COMPARE(item("sidebarShortcutAdd")->height(), 40.0);
    QTRY_COMPARE(item("sidebarShortcutAdd")->width(), 44.0);
    QCOMPARE(item("sidebarShortcut-home")->width(), 44.0);
    QCOMPARE(item("sidebarShortcuts")->mapToScene(QPointF()).x(), 18.0);
    auto *railMenu = item("sidebarMenu-favorites");
    QVERIFY(railMenu);
    QTRY_COMPARE(railMenu->width(), 38.0);
    QCOMPARE(railMenu->height(), 38.0);
    QCOMPARE(railMenu->mapToScene(QPointF()).x(), 21.0);
    QTRY_VERIFY(verifyPlaylistInset(true));
    auto *avatarButton = item("sidebarUserLink");
    auto *avatar = item("sidebarAccountAvatar");
    auto *avatarBackground = item("sidebarUserLinkBackground");
    QVERIFY(avatarButton && avatar && avatarBackground);
    QTRY_COMPARE(avatarButton->width(), 42.0);
    QCOMPARE(avatarButton->height(), 42.0);
    QCOMPARE(avatar->width(), 32.0);
    QCOMPARE(avatar->height(), 32.0);
    QCOMPARE(avatar->mapToItem(avatarButton, QPointF()), QPointF(5, 5));
    QCOMPARE(avatarBackground->property("radius").toReal(), 10.0);
    QVERIFY(!item("sidebarAccountTrigger")->isVisible());
    QTest::mouseMove(session.window, QPoint(700, 50), 20);
    QCOMPARE(avatarBackground->property("color").value<QColor>(), session.theme.mutedSurface());
    QVERIFY(capture("sidebar-rail-light"));
    QTest::mouseMove(session.window, center(avatarButton), 20);
    QTRY_VERIFY(avatarButton->property("hovered").toBool());
    QCOMPARE(avatarBackground->property("color").value<QColor>(), session.theme.controlHover());
    session.theme.setMode(ThemeStore::Dark);
    QTest::mouseMove(session.window, QPoint(700, 50), 20);
    QVERIFY(capture("sidebar-rail-dark"));
    session.shell.zoomIn();
    QTRY_VERIFY(verifyPlaylistInset(true));
    QCOMPARE(avatar->mapToItem(avatarButton, QPointF()), QPointF(5, 5));
    QVERIFY(capture("sidebar-rail-scaled-dark"));
    UserPlaylistModelTestAccess::present(session.accounts.playlists, session.api, 0);
    session.window->resize(1150, 1200);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() <= scroll->height());
    QTRY_VERIFY(!sidebarBar->isVisible());
    QCOMPARE(warnings.size(), 0);
}
