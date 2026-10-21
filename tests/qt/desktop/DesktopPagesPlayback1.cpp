#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

#ifdef Q_OS_MACOS
QVariantMap nativeMacApplicationPresentation();
#endif

void DesktopPagesTest::miniVolumeDismissesAcrossHost_data()
{
    QTest::addColumn<bool>("pinned");
    QTest::newRow("unpinned") << false;
    QTest::newRow("pinned") << true;
}

void DesktopPagesTest::miniVolumeDismissesAcrossHost()
{
    QFETCH(bool, pinned);
    Session session;
    session.theme.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences);
    session.theme.attachToEngine(session.engine);
    const auto cleanup = qScopeGuard([&session] {
        session.router.shutdown();
        session.commands.shutdown();
        session.windows.shutdown();
        session.root.reset();
        session.window = nullptr;
    });
    auto *floating = session.windows.floating();
    floating->mini()->setAlwaysOnTop(pinned);
    floating->showMini();
    QQmlComponent component(&session.engine,
                            QUrl("qrc:/qt/qml/EchoMusic/Shell/MiniPlayerWindow.qml"));
    session.root.reset(
        component.createWithInitialProperties({{"service", QVariant::fromValue(floating)},
                                               {"playback", QVariant::fromValue(&playback)}}));
    QVERIFY2(session.root, qPrintable(component.errorString()));
    auto *mini = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(mini);
    QVERIFY(Test::prepareNativeInput(mini));
    auto *volume = visual(mini->contentItem(), "miniPlayerVolume");
    QVERIFY(volume);
    auto *owner = mini->findChild<QObject *>("miniPlayerVolumePopupOwner");
    QVERIFY(owner);
    bool activationLost = false;
    const auto activationConnection = connect(
        qGuiApp, &QGuiApplication::applicationStateChanged, mini,
        [&](Qt::ApplicationState state) { activationLost |= state != Qt::ApplicationActive; });
    const auto disconnectActivation = qScopeGuard([&] { disconnect(activationConnection); });
    for (const QString panel : {QString(), QString("lyric"), QString("queue")}) {
        floating->setPanel(panel);
        QTRY_COMPARE(mini->height(), panel.isEmpty() ? 64 : 360);
        // Include the resize handles, ordinary controls, and the expanded card.
        for (int target = 0; target < 5; ++target) {
            QVERIFY(!owner->property("item").value<QObject *>());
            QSignalSpy hostFrames(mini, &QQuickWindow::frameSwapped);
            mini->update();
            QTRY_VERIFY(!hostFrames.isEmpty());
            QVERIFY(volume->isVisible() && volume->width() > 0);
            const auto volumePoint =
                volume->mapToScene({volume->width() / 2, volume->height() / 2}).toPoint();
#ifdef Q_OS_MACOS
            const auto beforeOpening = QGuiApplication::platformName() == "cocoa"
                                           ? nativeMacApplicationPresentation()
                                           : QVariantMap();
#endif
            activationLost = false;
            Test::moveNativePointer(mini, volumePoint);
            QTest::mouseClick(mini, Qt::LeftButton, Qt::NoModifier, volumePoint);
            QPointer<QObject> popup;
            QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
            QTRY_VERIFY(popup->property("opened").toBool());
            QVERIFY(!popup->property("modal").toBool());
            auto *content = popup->property("contentItem").value<QQuickItem *>();
            QVERIFY(content && content->window());
            auto *popupWindow = content->window();
            QCOMPARE(popupWindow->modality(), Qt::NonModal);
            QSignalSpy frames(popupWindow, &QQuickWindow::frameSwapped);
            popupWindow->update();
            QTRY_VERIFY(!frames.isEmpty());
            // Do not reactivate Mini after opening: doing so would conceal the
            // reported activation handoff to another application.
#ifdef Q_OS_MACOS
            if (QGuiApplication::platformName() == "cocoa") {
                const auto state = nativeMacApplicationPresentation();
                QCOMPARE(state.value("active"), beforeOpening.value("active"));
                QCOMPARE(state.value("frontmostPid"), beforeOpening.value("frontmostPid"));
                if (beforeOpening.value("active").toBool())
                    QVERIFY(!activationLost);
            }
#endif
            if (panel.isEmpty() && target == 0) {
                auto *slider = visual(content, "playbackVolumeSlider");
                auto *mute = visual(content, "playbackVolumeMute");
                QVERIFY(slider && mute);
                QTest::mouseClick(
                    popupWindow, Qt::LeftButton, Qt::NoModifier,
                    slider->mapToScene({slider->width() * .8, slider->height() / 2}).toPoint());
                QTRY_VERIFY(playback.volume() > 70);
                const auto mutePoint =
                    mute->mapToScene({mute->width() / 2, mute->height() / 2}).toPoint();
                QTest::mouseClick(popupWindow, Qt::LeftButton, Qt::NoModifier, mutePoint);
                QTRY_COMPARE(playback.volume(), 0.);
                QTest::mouseClick(popupWindow, Qt::LeftButton, Qt::NoModifier, mutePoint);
                QTRY_VERIFY(playback.volume() > 70);
            }
            const QPoint point = target == 0   ? QPoint(2, 30)
                                 : target == 1 ? QPoint(mini->width() - 2, 30)
                                 : target == 2 ? volumePoint
                                               : QPoint(mini->width() / 2, mini->height() / 2);
            const auto bounds = mini->geometry();
            if (target == 4) {
                QTest::keyClick(popupWindow, Qt::Key_Escape);
            } else {
                Test::moveNativePointer(mini, point);
                QTest::mouseClick(mini, Qt::LeftButton, Qt::NoModifier, point);
            }
            QTRY_VERIFY(popup.isNull());
            QCOMPARE(mini->geometry(), bounds);
            QCOMPARE(floating->panel(), panel);
        }
    }
}

void DesktopPagesTest::playbackFallbackControlsUseProductionUi()
{
    Session session;
    session.initialRoute = "search";
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.router.shutdown();
        session.commands.shutdown();
        session.windows.shutdown();
        session.root.reset();
        session.window = nullptr;
    });
    session.theme.setMotionEnabled(false);
    session.shell.setMotionEnabled(false);
    session.window->resize(1250, 900);
    auto *settings = session.root->findChild<QObject *>("settingsDialogOwner");
    auto *quality = session.root->findChild<QObject *>("playbackOptionsPopupOwner");
    QVERIFY(settings && quality);
    QVERIFY(!settings->property("item").value<QObject *>());
    QVERIFY(!quality->property("item").value<QObject *>());
    const auto captures = qEnvironmentVariable("ECHO_SOURCE_UI_CAPTURES");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    QVERIFY(!playback.preferences()->mvAudioFallbackEnabled());
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const int zoom : {100, 125}) {
            session.shell.resetZoom();
            for (int z = 100; z < zoom; z += 5)
                session.shell.zoomIn();
            QVERIFY(QMetaObject::invokeMethod(
                settings, "open", Q_ARG(QVariant, (QVariantMap{{"initialSection", "playback"}}))));
            QPointer<QObject> dialog = settings->property("item").value<QObject *>();
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->property("opened").toBool());
            auto *sectionLoader = visual(session.window->contentItem(), "settingsSectionLoader");
            QVERIFY(sectionLoader);
            QTRY_COMPARE_WITH_TIMEOUT(sectionLoader->property("status").toInt(), 1, 15000);
            QQuickItem *toggle = nullptr;
            QTRY_VERIFY(
                (toggle = visual(session.window->contentItem(), "settingsMvAudioFallback")));
            QTRY_VERIFY(toggle->width() > 0 && toggle->isVisible());
            auto *viewport = visual(session.window->contentItem(), "settingsContentViewport");
            QVERIFY(viewport);
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
            QCOMPARE(toggle->property("checked").toBool(), false);
            QVERIFY(Test::prepareNativeInput(session.window));
            QTRY_VERIFY([&] {
                const auto local = toggle->mapToItem(viewport, {0, 0});
                const auto maximum =
                    qMax(0., viewport->property("contentHeight").toReal() - viewport->height());
                viewport->setProperty("contentY",
                                      qBound(0.,
                                             local.y() + viewport->property("contentY").toReal() -
                                                 viewport->height() / 2,
                                             maximum));
                const auto current = toggle->mapToItem(viewport, {0, 0});
                return current.y() >= 0 && current.y() + toggle->height() <= viewport->height();
            }());
            QTest::mouseClick(
                session.window, Qt::LeftButton, Qt::NoModifier,
                toggle->mapToScene({toggle->width() / 2, toggle->height() / 2}).toPoint());
            QTRY_VERIFY(playback.preferences()->mvAudioFallbackEnabled());
            QCOMPARE(session.preferences.value("playback/mvAudioFallbackEnabled").toBool(), true);
            if (!captures.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/settings-%1-%2.png").arg(int(mode)).arg(zoom)));
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(dialog.isNull());
            playback.preferences()->setMvAudioFallbackEnabled(false);
            for (const auto &kind : {QString("cloud"), QString("mv")}) {
                PlaybackServiceTestAccess::sourcePresentation(playback, kind);
                QVERIFY(QMetaObject::invokeMethod(
                    quality, "open", Q_ARG(QVariant, (QVariantMap{{"kind", "quality"}}))));
                QPointer<QObject> popup = quality->property("item").value<QObject *>();
                QVERIFY(popup);
                QTRY_VERIFY(popup->property("opened").toBool());
                QQuickItem *cloud = nullptr;
                QTRY_VERIFY(
                    (cloud = visual(session.window->contentItem(), "playbackOption-cloud")));
                QVERIFY(cloud->isVisible());
                QCOMPARE(cloud->property("selected").toBool(), kind == "cloud");
                QVERIFY(playback.sourceNotice().contains(kind == "mv" ? "MV" : "云盘"));
                if (!captures.isEmpty()) {
                    QSignalSpy redraw(session.window, &QQuickWindow::frameSwapped);
                    session.window->update();
                    QTRY_VERIFY(!redraw.isEmpty());
                    QVERIFY(session.window->grabWindow().save(
                        captures + QString("/%1-%2-%3.png").arg(kind).arg(int(mode)).arg(zoom)));
                }
                QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                QTRY_VERIFY(popup.isNull());
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::floatingLyricsFollowRealAudioAndStopMotionOnPause()
{
    Session session;
    session.theme.setMotionEnabled(true);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.router.shutdown();
        session.commands.shutdown();
        session.windows.shutdown();
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    const auto path = session.directory.filePath("floating-motion.wav");
    QVERIFY(writeFloatingTestWave(path));
    PlaybackTrack track;
    track.id = path;
    track.localPath = path;
    track.title = QStringLiteral("晚风里的旋律");
    track.artist = QStringLiteral("EchoMusicNext 原生播放验证");
    track.durationMs = 16000;
    playback.setVolume(0);
    playback.playTracks({track}, 0, "Native motion", "floating-motion");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
    QJsonArray rows;
    const QStringList lyrics{
        QStringLiteral("让音乐留在身边"),      QStringLiteral("像晚风 轻轻掠过心间"),
        QStringLiteral("每一刻 都清晰可见"),   QStringLiteral("下一句 温柔地浮现"),
        QStringLiteral("暂停后 停在这一瞬间"), QStringLiteral("随着旋律 继续向前")};
    for (int i = 0; i < lyrics.size(); ++i)
        rows.append(QJsonObject{{"text", lyrics[i]},
                                {"start_ms", i * 1500},
                                {"duration_ms", 1500},
                                {"words", QJsonArray{}},
                                {"translation", "Let the music stay with you"},
                                {"romanization", "rang yin yue liu zai shen bian"}});
    QVERIFY(playback.lyrics()->replace({{"lines", rows}}));
    auto *floating = session.windows.floating();
    floating->setDesktopEnabled(true);
    QQuickWindow *desktop = nullptr;
    QTRY_VERIFY(([&] {
        for (auto *candidate : QGuiApplication::topLevelWindows())
            if (candidate->objectName() == "desktopLyricWindow")
                desktop = qobject_cast<QQuickWindow *>(candidate);
        return desktop != nullptr;
    })());
    auto *viewport = visual(desktop->contentItem(), "desktopLyricViewport");
    QVERIFY(viewport);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    auto *probe = new FloatingFrameProbe(desktop);
    QTRY_VERIFY_WITH_TIMEOUT(viewport->property("arrival").toReal() > .15, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(viewport->property("arrival").toReal() < .15, 1500);
    QVERIFY(playback.playing());
    const auto before = playback.positionMs();
    floating->toggleMini();
    QTRY_VERIFY(floating->miniActive());
    floating->setPanel("lyric");
    QTest::qWait(350);
    if (!qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR").isEmpty()) {
        for (auto *candidate : QGuiApplication::topLevelWindows())
            if (candidate->objectName() == "miniPlayerWindow")
                QVERIFY(qobject_cast<QQuickWindow *>(candidate)->grabWindow().save(
                    qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR") + "/mini-real-audio.png"));
    }
    QVERIFY(playback.playing());
    QVERIFY(playback.positionMs() > before);
    floating->showMain();
    playback.pause();
    QTRY_VERIFY(!playback.playing());
    QTRY_VERIFY(qAbs(viewport->property("arrival").toReal()) < .001);
    // A final engine telemetry sample can arrive after the pause command.
    QTest::qWait(300);
    const auto paused = viewport->property("renderTime").toReal();
    QTest::qWait(100);
    QVERIFY(qAbs(viewport->property("renderTime").toReal() - paused) < 1);
    session.theme.setMotionEnabled(false);
    playback.play();
    QTRY_VERIFY(playback.playing());
    playback.seek(6000);
    QTRY_VERIFY(playback.positionMs() >= 5900);
    QTRY_VERIFY(qAbs(viewport->property("arrival").toReal()) < .001);
    const auto capture = qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(desktop->grabWindow().save(capture + "/desktop-real-audio.png"));
    if (qEnvironmentVariableIntValue("ECHO_FLOATING_PERF"))
        qInfo() << "floating-audio-render frames=" << probe->frames()
                << "max-sync-ms=" << probe->maximumSyncMs()
                << "max-render-record-ms=" << probe->maximumMs() << "rss-kib=" << floatingRssKiB();
    floating->setDesktopEnabled(false);
    playback.stop();
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::miniProgressVolumeBackdropFavoriteAndDesktopBadge_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void DesktopPagesTest::miniProgressVolumeBackdropFavoriteAndDesktopBadge()
{
    QFETCH(bool, dark);
    Session session;
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    session.theme.setMotionEnabled(false);
    session.theme.setAccent(QColor("#0088ff"));
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.router.shutdown();
        session.commands.shutdown();
        session.windows.shutdown();
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    const auto path = session.directory.filePath("mini-controls.wav");
    QVERIFY(writeFloatingTestWave(path));
    QImage art(800, 800, QImage::Format_RGB32);
    art.fill(Qt::red);
    const auto artPath = session.directory.filePath("mini-cover.png");
    QVERIFY(art.save(artPath));
    PlaybackTrack track;
    track.id = path;
    track.localPath = path;
    track.title = QStringLiteral("半壶纱.mp3 Mini 原生交互与长标题回归");
    track.artist = QStringLiteral("刘珂矣 Artist 字体回退与长名称回归");
    track.durationMs = 16000;
    track.cover = QUrl::fromLocalFile(artPath).toString();
    playback.setVolume(0);
    playback.playTracks({track}, 0, "Mini controls", "mini-controls");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
    playback.pause();
    QTRY_VERIFY(!playback.playing());
    auto *floating = session.windows.floating();
    QQuickItem *desktopAction = nullptr;
    QTRY_VERIFY(
        (desktopAction = visual(session.window->contentItem(), "playerbar-action-desktop-lyric")));
    auto *badge = visual(desktopAction, "actionBadge");
    QVERIFY(badge);
    QTRY_COMPARE(badge->property("count").toString(), QString("OFF"));
    floating->setDesktopEnabled(true);
    QTRY_COMPARE(badge->property("count").toString(), QString("ON"));
    QVERIFY(badge->isVisible());
    QVERIFY(desktopAction->property("actionActive").toBool());
    QVERIFY(session.commands.playerbarActions()->setBadgeVisible("host/desktop-lyric", false));
    QTRY_VERIFY(!badge->isVisible());
    QVERIFY(session.commands.playerbarActions()->setBadgeVisible("host/desktop-lyric", true));
    QTRY_VERIFY(badge->isVisible());

    floating->setDesktopEnabled(false);
    floating->toggleMini();
    QQuickWindow *mini = nullptr;
    QTRY_VERIFY(([&] {
        for (auto *candidate : QGuiApplication::topLevelWindows())
            if (candidate->objectName() == "miniPlayerWindow")
                mini = qobject_cast<QQuickWindow *>(candidate);
        return mini != nullptr;
    })());
    QVERIFY(Test::prepareNativeInput(mini));
    QCOMPARE(mini->minimumWidth(), 360);
    QCOMPARE(mini->maximumWidth(), 560);
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa") {
        QCOMPARE(nativeFloatingAppearance(mini).value("minimumContentWidth").toInt(), 360);
        QCOMPARE(nativeFloatingAppearance(mini).value("maximumContentWidth").toInt(), 560);
    }
#endif
    if (QGuiApplication::platformName() == "cocoa") {
        const auto dragWidth = [&](Qt::Edge edge, int delta) {
            auto *handle =
                visual(mini->contentItem(), "miniPlayerResize-" + QString::number(int(edge)));
            QVERIFY(handle);
            const auto start = handle->mapToScene({2, 24}).toPoint();
            const auto endGlobal = mini->mapToGlobal(start) + QPoint(delta, 0);
            Test::moveNativePointer(mini, start);
            QTest::mousePress(mini, Qt::LeftButton, Qt::NoModifier, start);
            Test::moveNativePointer(mini, mini->mapFromGlobal(endGlobal));
            QTest::mouseRelease(mini, Qt::LeftButton, Qt::NoModifier,
                                mini->mapFromGlobal(endGlobal));
        };
        dragWidth(Qt::RightEdge, 300);
        QTRY_COMPARE(mini->width(), 560);
        dragWidth(Qt::RightEdge, -400);
        QTRY_COMPARE(mini->width(), 360);
        const auto right = mini->geometry().right();
        dragWidth(Qt::LeftEdge, -300);
        QTRY_COMPARE(mini->width(), 560);
        QCOMPARE(mini->geometry().right(), right);
        dragWidth(Qt::LeftEdge, 400);
        QTRY_COMPARE(mini->width(), 360);
        QCOMPARE(mini->geometry().right(), right);
        QCOMPARE(mini->height(), 64);
    }
    mini->setWidth(480);
    QSignalSpy resizedFrame(mini, &QQuickWindow::frameSwapped);
    mini->update();
    QTRY_VERIFY(!resizedFrame.isEmpty());
    QTRY_COMPARE(mini->width(), 480);
    auto *controls = visual(mini->contentItem(), "miniPlayerControls");
    auto *progress = visual(mini->contentItem(), "miniPlayerProgress");
    QVERIFY(controls && progress);
    Test::moveNativePointer(mini, {-12, -12});
    QTRY_VERIFY(!mini->property("showControls").toBool());
    auto *metadata = visual(mini->contentItem(), "miniPlayerMetadata");
    auto *title = visual(mini->contentItem(), "miniPlayerTitle");
    auto *artist = visual(mini->contentItem(), "miniPlayerArtist");
    QVERIFY(metadata && title && artist);
    for (int width : {360, 560}) {
        mini->setWidth(width);
        QSignalSpy frame(mini, &QQuickWindow::frameSwapped);
        mini->update();
        QTRY_VERIFY(!frame.isEmpty());
        QCOMPARE(metadata->height(), 34.);
        QCOMPARE(title->height(), 18.);
        QCOMPARE(artist->height(), 15.);
        QCOMPARE(title->property("font").value<QFont>().pixelSize(), 13);
        QCOMPARE(artist->property("font").value<QFont>().pixelSize(), 11);
        const auto image = mini->grabWindow();
        const qreal ratio = image.width() / qreal(mini->width());
        const auto rowBounds = artist->mapRectToScene(artist->boundingRect());
        const auto trackTop = progress->mapToScene({0, 6}).y();
        // Include the gap: fallback ink overflowing its line box must be
        // detected too. Stop before the rail's antialiased top edge.
        const auto pixels = QRectF(rowBounds.topLeft() * ratio,
                                   QSizeF(rowBounds.width(), trackTop - rowBounds.y() - 1) * ratio)
                                .toAlignedRect()
                                .intersected(image.rect());
        const auto base = session.theme.elevated();
        int inkBottom = -1;
        for (int y = pixels.top(); y <= pixels.bottom(); ++y)
            for (int x = pixels.left(); x <= pixels.right(); ++x) {
                const auto p = image.pixelColor(x, y);
                if (std::max({qAbs(p.red() - base.red()), qAbs(p.green() - base.green()),
                              qAbs(p.blue() - base.blue())}) > 25)
                    inkBottom = qMax(inkBottom, y);
            }
        QVERIFY(inkBottom >= 0);
        QVERIFY2(trackTop - inkBottom / ratio >= 4., "Artist ink must clear the progress rail");
        const auto capture = qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR");
        if (!capture.isEmpty())
            QVERIFY(image.save(
                capture +
                QString("/mini-metadata-%1-%2.png").arg(dark ? "dark" : "light").arg(width)));
    }
    mini->setWidth(480);
    QSignalSpy metadataFrame(mini, &QQuickWindow::frameSwapped);
    mini->update();
    QTRY_VERIFY(!metadataFrame.isEmpty());
    auto *seek = visual(progress, "playbackSeekSlider");
    QVERIFY(seek);
    QCOMPARE(mini->height(), 64);
    QCOMPARE(progress->x(), 91.);
    QCOMPARE(progress->y(), 43.);
    QVERIFY(progress->y() + progress->height() <= 59.);
    const auto scrub = [&] {
        const auto start = seek->mapToScene({seek->width() * .2, seek->height() / 2}).toPoint();
        const auto end = seek->mapToScene({seek->width() * .7, seek->height() / 2}).toPoint();
        Test::moveNativePointer(mini, start);
        QTest::mousePress(mini, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(mini, end);
        QTest::mouseRelease(mini, Qt::LeftButton, Qt::NoModifier, end);
    };
    scrub();
    QTRY_VERIFY2_WITH_TIMEOUT(
        playback.positionMs() > 9000 && playback.positionMs() < 12500,
        qPrintable(QString("position=%1 width=%2 progressWidth=%3 sliderWidth=%4 pressed=%5")
                       .arg(playback.positionMs())
                       .arg(mini->width())
                       .arg(progress->width())
                       .arg(seek->width())
                       .arg(seek->property("pressed").toBool())),
        5000);
    QCOMPARE(mini->height(), 64);
    auto *favorite = visual(mini->contentItem(), "miniPlayerFavorite");
    QVERIFY(favorite);
    PlaybackLibraryActionsTestAccess::favorite(*playback.libraryActions(), playback.currentTrack(),
                                               true);
    QTRY_COMPARE(favorite->property("foreground").value<QColor>(), QColor("#ef4444"));
    Test::moveNativePointer(mini, favorite->mapToScene({12, 15}).toPoint());
    QTRY_COMPARE(favorite->property("foreground").value<QColor>(), QColor("#ef4444"));
    auto *volume = visual(mini->contentItem(), "miniPlayerVolume");
    QPointer<QObject> popup;
    QVERIFY(volume);
    QVERIFY(!mini->findChild<QObject *>("miniPlayerVolumePopup"));
    QQuickItem *popupContent = nullptr, *volumeSlider = nullptr, *mute = nullptr;
    QQuickWindow *popupWindow = nullptr;
    const auto volumeOpened = [&] {
        popup = mini->findChild<QObject *>("miniPlayerVolumePopup");
        if (!popup || !popup->property("opened").toBool())
            return false;
        popupContent = popup->property("contentItem").value<QQuickItem *>();
        volumeSlider = visual(popupContent, "playbackVolumeSlider");
        mute = visual(popupContent, "playbackVolumeMute");
        popupWindow = volumeSlider ? volumeSlider->window() : nullptr;
        return volumeSlider && mute && popupWindow;
    };
    playback.setVolume(50);
    const auto click = [](QQuickWindow *window, QQuickItem *item) {
        QVERIFY(item);
        QVERIFY(Test::prepareNativeInput(window));
        QSignalSpy frame(window, &QQuickWindow::frameSwapped);
        window->requestUpdate();
        QTRY_VERIFY(!frame.isEmpty());
        const auto p = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
        QVERIFY(window->contentItem()->boundingRect().contains(p));
        Test::moveNativePointer(window, p);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p);
    };
    Test::moveNativePointer(mini, volume->mapToScene({12, 15}).toPoint());
    QTest::qWait(80);
    QVERIFY(popup.isNull());
    click(mini, volume);
    QTRY_VERIFY(volumeOpened());
    QCOMPARE(playback.volume(), 50.);
    QCOMPARE(mini->height(), 64);
    QCOMPARE(popup->property("height").toReal(), 57.);
    QCOMPARE(popup->property("width").toReal(), 176.);

    QVERIFY(volumeSlider && mute);
    QCOMPARE(volumeSlider->property("orientation").toInt(), int(Qt::Horizontal));
    popupWindow = volumeSlider->window();
    QVERIFY(popupWindow);
    QCOMPARE(popupWindow->modality(), Qt::NonModal);
    if (QGuiApplication::platformName() != "offscreen") {
        QVERIFY(popupWindow != mini);
        QTRY_VERIFY(QTest::qWaitForWindowExposed(popupWindow));
        QVERIFY(popupWindow->screen()->availableGeometry().contains(popupWindow->geometry()));
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            for (bool pinned : {true, false, true}) {
                floating->mini()->setAlwaysOnTop(pinned);
                if (popup) {
                    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                    QTRY_VERIFY(popup.isNull());
                }
                click(mini, volume);
                QTRY_VERIFY(volumeOpened());
                popupWindow = volumeSlider->window();
                QVERIFY(popupWindow);
                QCOMPARE(popupWindow->modality(), Qt::NonModal);
                QCOMPARE(popupWindow->transientParent(), mini);
                mini->raise();
                const auto hostNumber =
                    nativeFloatingAppearance(mini).value("windowNumber").toLongLong();
                QTRY_VERIFY(nativeFloatingAppearance(popupWindow).value("level").toLongLong() >
                            nativeFloatingAppearance(mini).value("level").toLongLong());
                QTRY_VERIFY(
                    nativeMacComposition(popupWindow, hostNumber).value("aboveHost").toBool());
                QCOMPARE(nativeFloatingAppearance(popupWindow).value("allApplications").toBool(),
                         pinned);
                if (!pinned)
                    QVERIFY(nativeFloatingAppearance(popupWindow).value("level").toLongLong() <
                            1000);
                Test::moveNativePointer(mini, {16, 55});
                QTest::mouseClick(mini, Qt::LeftButton, Qt::NoModifier, {16, 55});
                QTRY_VERIFY(popup.isNull());
            }
            click(mini, volume);
            QTRY_VERIFY(volumeOpened());
            popupWindow = volumeSlider->window();
            QVERIFY(popupWindow);
            QCOMPARE(popupWindow->modality(), Qt::NonModal);
        }
#endif
    }
    const auto volumePoint =
        volumeSlider->mapToScene({volumeSlider->width() * .8, volumeSlider->height() / 2})
            .toPoint();
    Test::moveNativePointer(popupWindow, volumePoint);
    QTest::mouseClick(popupWindow, Qt::LeftButton, Qt::NoModifier, volumePoint);
    QTRY_VERIFY(playback.volume() > 70);
    const auto capture = qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(popupWindow->grabWindow().save(
            capture + (dark ? "/mini-volume-dark.png" : "/mini-volume-light.png")));
    click(popupWindow, mute);
    QTRY_COMPARE(playback.volume(), 0.);
    click(popupWindow, mute);
    QTRY_VERIFY(playback.volume() > 70);
    Test::moveNativePointer(mini, {16, 55});
    QTest::mouseClick(mini, Qt::LeftButton, Qt::NoModifier, {16, 55});
    QTRY_VERIFY(popup.isNull());
    click(mini, volume);
    QTRY_VERIFY(volumeOpened());
    mini->showMinimized();
    QTRY_VERIFY(popup.isNull());
    mini->showNormal();
    QVERIFY(Test::prepareNativeInput(mini));
    const auto originalPosition = mini->position();
    const auto area = mini->screen()->availableGeometry();
    mini->setPosition(area.left() + 80, area.top() + 4);
    click(mini, volume);
    QTRY_VERIFY(volumeOpened());
    QTRY_VERIFY(!popup->property("placedAbove").toBool());
    popupWindow = volumeSlider->window();
    QVERIFY(popupWindow);
    QTest::keyClick(popupWindow, Qt::Key_Escape);
    QTRY_VERIFY(popup.isNull());
    mini->setPosition(area.left() + 80, area.bottom() - 70);
    click(mini, volume);
    QTRY_VERIFY(volumeOpened());
    QTRY_VERIFY(popup->property("placedAbove").toBool());
    popupWindow = volumeSlider->window();
    QVERIFY(popupWindow);
    QTest::keyClick(popupWindow, Qt::Key_Escape);
    QTRY_VERIFY(popup.isNull());
    mini->setPosition(originalPosition);
    auto *desktopButton = visual(mini->contentItem(), "miniPlayerDesktopLyric");
    QVERIFY(desktopButton);
    click(mini, desktopButton);
    QTRY_VERIFY(floating->desktopEnabled());
    QTRY_COMPARE(badge->property("count").toString(), QString("ON"));
    click(mini, desktopButton);
    QTRY_COMPARE(badge->property("count").toString(), QString("OFF"));
    floating->setPanel("lyric");
    QTRY_COMPARE(mini->height(), 360);
    QCOMPARE(mini->width(), 480);
    mini->setWidth(560);
    QTRY_COMPARE(mini->width(), 560);
    playback.seek(2000);
    QTRY_VERIFY(playback.positionMs() >= 1900 && playback.positionMs() < 3000);
    scrub();
    QTRY_VERIFY(playback.positionMs() > 9000 && playback.positionMs() < 12500);
    auto *menuAction = visual(mini->contentItem(), "miniPlayerLyricMenuAction");
    QVERIFY(menuAction);
    QVERIFY(!mini->findChild<QObject *>("miniPlayerLyricMenu"));
    click(mini, menuAction);
    QPointer<QObject> menu;
    QTRY_VERIFY((menu = mini->findChild<QObject *>("miniPlayerLyricMenu")));
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *menuContent = menu->property("contentItem").value<QQuickItem *>();
    QVERIFY(menuContent && menuContent->window());
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa") {
        auto *menuWindow = menuContent->window();
        QCOMPARE(menuWindow->transientParent(), mini);
        const auto hostNumber = nativeFloatingAppearance(mini).value("windowNumber").toLongLong();
        QTRY_VERIFY(nativeMacComposition(menuWindow, hostNumber).value("aboveHost").toBool());
    }
#endif
    QTest::keyClick(menuContent->window(), Qt::Key_Escape);
    QTRY_VERIFY(menu.isNull());
    QQuickItem *backdrop = nullptr;
    QTRY_VERIFY((backdrop = visual(mini->contentItem(), "miniLyricBackdrop")));
    auto *backdropImage = visual(backdrop, "miniLyricBackdropCover");
    QVERIFY(backdropImage);
    QTRY_COMPARE(backdropImage->property("status").toInt(), 1);
    if (mini->rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
        QTRY_VERIFY(visual(backdrop, "miniLyricBackdropComposite"));
        const auto image = mini->grabWindow();
        const auto ratio = image.width() / qreal(mini->width());
        const auto point = backdrop->mapToScene({2, 80});
        const auto edge = image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
        const auto center =
            image.pixelColor(qRound((point.x() + 18) * ratio), qRound(point.y() * ratio));
        QVERIFY(qAbs(edge.red() - center.red()) <= 2 && qAbs(edge.green() - center.green()) <= 2);
        const auto base = session.theme.elevated();
        const auto coverAlpha = .22 * (1 - (.84 + .11 * 80 / backdrop->height()));
        QVERIFY(qAbs(edge.green() - qRound(base.green() * (1 - coverAlpha))) <= 3);
        const auto capture = qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR");
        if (!capture.isEmpty())
            QVERIFY(image.save(capture + (dark ? "/mini-fixed-expanded-dark.png"
                                               : "/mini-fixed-expanded-light.png")));
    }
    floating->mini()->setBackgroundBlur(false);
    QTRY_VERIFY(!visual(mini->contentItem(), "miniLyricBackdrop"));
    floating->mini()->setBackgroundBlur(true);
    QTRY_VERIFY(visual(mini->contentItem(), "miniLyricBackdrop"));
    floating->setPanel("");
    QTRY_COMPARE(mini->height(), 64);
    QCOMPARE(mini->width(), 560);
    QTRY_VERIFY(!visual(mini->contentItem(), "miniLyricBackdrop"));
    click(mini, volume);
    QTRY_VERIFY(volumeOpened());
    QPointer<QObject> popupGuard = popup;
    QPointer<QQuickWindow> popupWindowGuard = volumeSlider->window();
    floating->showMain();
    QTRY_VERIFY(popupGuard.isNull());
    QTRY_VERIFY(popupWindowGuard.isNull() || !popupWindowGuard->isVisible());
    floating->toggleMini();
    mini = nullptr;
    QTRY_VERIFY(([&] {
        for (auto *candidate : QGuiApplication::topLevelWindows())
            if (candidate->objectName() == "miniPlayerWindow")
                mini = qobject_cast<QQuickWindow *>(candidate);
        return mini != nullptr;
    })());
    QTRY_COMPARE(mini->width(), 560);
    floating->showMain();
    playback.stop();
}

void DesktopPagesTest::floatingPreferencesAndFilteredTimelineKeepIdentity()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("floating.ini"), QSettings::IniFormat);
    FloatingLyricPreferences desktop(settings, true), mini(settings, false);
    QCOMPARE(desktop.alignment(), QString("both"));
    // Real QWindow logical geometry round-trips, including odd sizes at
    // fractional QT_SCALE_FACTOR. Native mouse/DPI transitions remain a
    // separate Windows device check; setPosition is not a drag simulation.
    const auto geometryArea = QGuiApplication::primaryScreen()->availableGeometry();
    const QRect expected(geometryArea.topLeft() + QPoint(16, 16), QSize(321, 147));
    settings.setValue("floating/desktop/geometry", expected);
    for (int cycle = 0; cycle < 24; ++cycle) {
        QQuickWindow window;
        window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        FloatingPlayerController controller(settings, true);
        controller.setDesktopEnabled(true);
        controller.attachDesktop(&window);
        QCoreApplication::processEvents();
        QCOMPARE(window.size(), expected.size());
        for (int step = 0; step < 40; ++step) {
            window.setPosition(expected.topLeft() + QPoint(step % 9, step % 7));
            QCoreApplication::processEvents();
            QCOMPARE(window.size(), expected.size());
        }
        window.setPosition(expected.topLeft());
        controller.setLocked(true);
        controller.setLocked(false);
        QCOMPARE(window.size(), expected.size());
        controller.shutdown();
        controller.attachDesktop(nullptr);
        QCOMPARE(settings.value("floating/desktop/geometry").toRect(), expected);
    }
    QCOMPARE(mini.fontSize(), 14);
    mini.setFontSize(99);
    QCOMPARE(mini.fontSize(), 22);
    mini.setFontWeight(900);
    QCOMPARE(mini.fontWeight(), 780);
    mini.setAlignment("both");
    QCOMPARE(mini.alignment(), QString("center"));
    mini.setFollowCoverColor(true);
    mini.setPlayedColor("#FF00AA");
    FloatingLyricPreferences restored(settings, false);
    QCOMPARE(restored.playedColor(), QString("#ff00aa"));
    QVERIFY(restored.followCoverColor());
    restored.resetColors();
    QCOMPARE(restored.playedColor(), QString());
    QVERIFY(!restored.followCoverColor());
    LyricModel model(nullptr);
    QJsonArray rows;
    for (int i = 0; i < 4; ++i)
        rows.append(QJsonObject{
            {"text", i == 1 ? QStringLiteral("作词：测试") : QStringLiteral("歌词 %1").arg(i)},
            {"start_ms", i * 1000},
            {"duration_ms", 1000},
            {"words", QJsonArray{}}});
    QVERIFY(model.replace({{"lines", rows}}));
    model.setFilter(true, QStringLiteral("^作词"));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.indexAt(1500), 1);
    QCOMPARE(model.visibleIndex(1), 0);
    QCOMPARE(model.nextVisible(0), 2);
    QCOMPARE(model.startAt(2), 2000);
    QVERIFY(model.lineAt(1).value("filtered").toBool());
    model.setFilter(true, "[");
    QVERIFY(!model.lineAt(1).value("filtered").toBool());
    const QJsonArray words{
        QJsonObject{{"text", QStringLiteral("你好")}, {"start_ms", 0}, {"duration_ms", 1000}},
        QJsonObject{{"text", " world"}, {"start_ms", 1000}, {"duration_ms", 1000}}};
    QVERIFY(model.replace({{"lines", QJsonArray{QJsonObject{{"text", QStringLiteral("你好 world")},
                                                            {"start_ms", 0},
                                                            {"duration_ms", 2000},
                                                            {"words", words},
                                                            {"romanization", "ni hao world"}}}}}));
    const auto ruby = model.rubyAt(0);
    QCOMPARE(ruby.size(), 2);
    QCOMPARE(ruby.first().toMap().value("start_ms").toLongLong(), 0);
    QCOMPARE(ruby.last().toMap().value("start_ms").toLongLong(), 1000);
    QVERIFY(!model.verticalAt(0).isEmpty());
    const QRect area(0, 0, 1200, 800);
    QVERIFY(FloatingPlayerController::chooseUp({400, 700, 360, 64}, area));
    QVERIFY(!FloatingPlayerController::chooseUp({400, 100, 360, 64}, area));
    QVERIFY(area.contains(
        FloatingPlayerController::fitBounds({5000, 5000, 360, 64}, {area}, area, {360, 64})));
}

void DesktopPagesTest::desktopLyricsMatchSourceChromeAndHoverUnlock()
{
    if (QGuiApplication::platformName() == "offscreen")
        QSKIP("Requires native global cursor and transparent window pixels");
    const auto originalCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([&] { QCursor::setPos(originalCursor); });
    Session session;
    session.theme.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.router.shutdown();
        session.commands.shutdown();
        session.windows.shutdown();
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    playback.lyricText()->setWantTranslation(false);
    playback.lyricText()->setWantRomanization(false);
    const QJsonArray rows{QJsonObject{{"text", QStringLiteral("这盛世每一天")},
                                      {"start_ms", 0},
                                      {"duration_ms", 20000},
                                      {"translation", "Peace and ordinary days"},
                                      {"romanization", "zhe sheng shi mei yi tian"},
                                      {"words", QJsonArray{}}},
                          QJsonObject{{"text", QStringLiteral("山河无恙 烟火寻常")},
                                      {"start_ms", 20000},
                                      {"duration_ms", 20000},
                                      {"words", QJsonArray{}}}};
    QVERIFY(playback.lyrics()->replace({{"lines", rows}}));
    const auto findWindow = [](const QString &name) -> QQuickWindow * {
        for (auto *candidate : QGuiApplication::topLevelWindows())
            if (candidate->objectName() == name)
                return qobject_cast<QQuickWindow *>(candidate);
        return nullptr;
    };
    auto *floating = session.windows.floating();
    floating->setLocked(false);
    floating->setDesktopEnabled(true);
    QTRY_VERIFY(findWindow("desktopLyricWindow"));
    auto *desktop = findWindow("desktopLyricWindow");
    desktop->setGeometry(session.window->x() + 80, session.window->y() + 120, 720, 220);
    QTRY_VERIFY(QTest::qWaitForWindowExposed(desktop));
    auto *panel = visual(desktop->contentItem(), "desktopLyricPanel");
    auto *toolbar = visual(desktop->contentItem(), "desktopLyricToolbar");
    auto *viewport = visual(desktop->contentItem(), "desktopLyricViewport");
    auto *lock = visual(desktop->contentItem(), "desktopLyricLock");
    QVERIFY(panel && toolbar && viewport && lock);
    QVERIFY(!visual(desktop->contentItem(), "desktopLyricMore"));
    auto *source = visual(desktop->contentItem(), "desktopLyricSource");
    auto *settings = visual(desktop->contentItem(), "desktopLyricSettings");
    auto *translation = visual(desktop->contentItem(), "desktopLyricText-translation");
    auto *romanization = visual(desktop->contentItem(), "desktopLyricText-romanization");
    QVERIFY(source && settings && translation && romanization);
    const auto iconsReady = [](auto &&self, QQuickItem *item) -> bool {
        if (!item->isVisible())
            return true;
        if (item->objectName() == "iconImage" && item->property("status").toInt() != 1)
            return false;
        for (auto *child : item->childItems())
            if (!self(self, child))
                return false;
        return true;
    };
    const auto capture = qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    const auto save = [&](QQuickWindow *window, const QString &name) {
        return capture.isEmpty() || window->grabWindow().save(capture + "/" + name + ".png");
    };
    const auto prepareDesktop = [&] {
    // This tool must remain above the main window without accepting
    // keyboard focus. Raising the main window would obscure its input.
#ifdef Q_OS_MACOS
        Test::activateTestApplication();
#endif
        desktop->raise();
        return QTest::qWaitForWindowExposed(desktop);
    };
    const auto click = [&](QQuickItem *item) {
        QVERIFY(prepareDesktop());
        QSignalSpy frame(desktop, &QQuickWindow::frameSwapped);
        desktop->requestUpdate();
        QTRY_VERIFY(!frame.isEmpty());
        const auto point = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
        Test::moveNativePointer(desktop, point);
        QTRY_VERIFY(item->isEnabled());
        QSignalSpy clicked(item, SIGNAL(clicked()));
        QTest::mouseClick(desktop, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(clicked.size(), 1);
    };
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        const auto suffix = mode == ThemeStore::Light ? "light" : "dark";
        QVERIFY(prepareDesktop());
        Test::moveNativePointer(desktop, {-40, -40});
        QTRY_COMPARE(toolbar->opacity(), 0.);
        QTRY_COMPARE(panel->property("color").value<QColor>().alpha(), 0);
        QVERIFY(save(desktop, QString("desktop-rest-%1").arg(suffix)));
        Test::moveNativePointer(desktop, {360, 100});
        QTRY_COMPARE(toolbar->opacity(), 1.);
        QTRY_VERIFY(iconsReady(iconsReady, toolbar));
        QTRY_COMPARE(panel->property("color").value<QColor>(), QColor("#99000000"));
        QCOMPARE(toolbar->x(), 12.);
        QCOMPARE(toolbar->y(), 12.);
        QCOMPARE(viewport->x(), 24.);
        QCOMPARE(viewport->y(), 64.);
        QCOMPARE(lock->property("foreground").value<QColor>(), QColor("white"));
        QCOMPARE(lock->property("glyph").toString(), QString("lock"));
        QVERIFY(source->isVisible() && settings->isVisible());
        QVERIFY(translation->isVisible() && romanization->isVisible());
        QVERIFY(source->mapToScene({0, 0}).x() < translation->mapToScene({0, 0}).x());
        QVERIFY(romanization->mapToScene({0, 0}).x() < settings->mapToScene({0, 0}).x());
        QVERIFY(settings->mapToScene({0, 0}).x() < lock->mapToScene({0, 0}).x());
        auto *lockImage = visual(lock, "iconImage");
        QVERIFY(lockImage);
        QTRY_COMPARE(lockImage->property("status").toInt(), 1);
        Test::moveNativePointer(desktop, translation->mapToScene({16, 16}).toPoint());
        click(translation);
        QTRY_VERIFY(playback.lyricText()->wantTranslation());
        click(translation);
        QTRY_VERIFY(!playback.lyricText()->wantTranslation());
        Test::moveNativePointer(desktop, romanization->mapToScene({16, 16}).toPoint());
        click(romanization);
        QTRY_VERIFY(playback.lyricText()->wantRomanization());
        click(romanization);
        QTRY_VERIFY(!playback.lyricText()->wantRomanization());
        Test::moveNativePointer(desktop, {360, 100});
        auto image = desktop->grabWindow();
        QVERIFY(!image.isNull());
        const auto lockBounds = lock->mapRectToScene({0, 0, lock->width(), lock->height()});
        const auto ratio = image.width() / qreal(desktop->width());
        const auto lockPixels =
            image.copy(QRectF(lockBounds.topLeft() * ratio, lockBounds.size() * ratio).toRect());
        int lockInk = 0;
        for (int y = 0; y < lockPixels.height(); ++y)
            for (int x = 0; x < lockPixels.width(); ++x) {
                const auto ink = lockPixels.pixelColor(x, y);
                lockInk +=
                    ink.alpha() > 220 && ink.red() > 220 && ink.green() > 220 && ink.blue() > 220;
            }
        QVERIFY(lockInk > 20);
        auto background = image.pixelColor(image.width() / 2, image.height() - 12);
        QVERIFY2(background.red() < 3 && background.green() < 3 && background.blue() < 3,
                 qPrintable(background.name(QColor::HexArgb)));
        QVERIFY(qAbs(background.alpha() - 153) <= 2);
        QVERIFY(save(desktop, QString("desktop-hover-%1").arg(suffix)));
#ifdef Q_OS_MACOS
        QVERIFY(!nativeFloatingAppearance(desktop).value("titled").toBool());
        QVERIFY(!nativeFloatingAppearance(desktop).value("opaque").toBool());
#endif
        Test::moveNativePointer(desktop, {-40, -40});
        floating->setLocked(true);
        QTRY_VERIFY(findWindow("desktopLyricUnlockWindow"));
        QPointer<QQuickWindow> unlock = findWindow("desktopLyricUnlockWindow");
        QTRY_VERIFY(!unlock->isVisible());
        QVERIFY(!floating->unlockVisible());
        QVERIFY(desktop->flags().testFlag(Qt::WindowTransparentForInput));
        Test::moveNativePointer(desktop, {360, 100});
        QTRY_VERIFY(unlock->isVisible());
        QTRY_VERIFY(floating->unlockVisible());
        QCOMPARE(toolbar->opacity(), 0.);
        QCOMPARE(panel->property("color").value<QColor>().alpha(), 0);
        QTRY_COMPARE(unlock->geometry().topLeft(),
                     desktop->mapToGlobal(lock->mapToScene({0, 0}).toPoint()));
        QCOMPARE(unlock->size(), lock->size().toSize());
        QVERIFY(!unlock->flags().testFlag(Qt::WindowTransparentForInput));
        auto *button = visual(unlock->contentItem(), "desktopLyricUnlock");
        QVERIFY(button);
        QCOMPARE(button->property("foreground").value<QColor>(), QColor("white"));
        image = unlock->grabWindow();
        background = image.pixelColor(image.width() / 2, image.height() - 4);
        QVERIFY(background.red() < 3 && background.green() < 3 && background.blue() < 3);
        QVERIFY(qAbs(background.alpha() - 115) <= 2);
        int whitePixels = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto ink = image.pixelColor(x, y);
                whitePixels +=
                    ink.alpha() > 220 && ink.red() > 220 && ink.green() > 220 && ink.blue() > 220;
            }
        QVERIFY(whitePixels > 20);
        QVERIFY(save(unlock, QString("unlock-%1").arg(suffix)));
        QVERIFY(save(desktop, QString("desktop-locked-%1").arg(suffix)));
        Test::moveNativePointer(desktop, {-40, -40});
        QTRY_VERIFY(!unlock->isVisible());
        // Revealing/hiding must reuse the tiny window, not remount the lyric viewport.
        QCOMPARE(unlock.data(), findWindow("desktopLyricUnlockWindow"));
        Test::moveNativePointer(desktop, {360, 100});
        QTRY_VERIFY(unlock->isVisible());
        floating->desktop()->setShowUnlockButton(false);
        QTRY_VERIFY(unlock.isNull());
        QVERIFY(!floating->unlockAvailable());
        floating->desktop()->setShowUnlockButton(true);
        QTRY_VERIFY(findWindow("desktopLyricUnlockWindow"));
        unlock = findWindow("desktopLyricUnlockWindow");
        QTRY_VERIFY(unlock->isVisible());
        desktop->hide();
        QTRY_VERIFY(!unlock->isVisible());
        desktop->show();
        QTRY_VERIFY(unlock->isVisible());
        // The lock and escape control share geometry, also after a layout switch.
        floating->desktop()->setLayout("vertical");
        desktop->resize(240, 600);
        Test::moveNativePointer(desktop, {80, 100});
        QTRY_VERIFY(unlock->isVisible());
        QTRY_COMPARE(unlock->geometry().topLeft(),
                     desktop->mapToGlobal(lock->mapToScene({0, 0}).toPoint()));
        QVERIFY(viewport->x() + viewport->width() < toolbar->x());
        for (const auto &name : {QString("desktopLyricOffsets"), QString("desktopLyricTransport"),
                                 QString("desktopLyricWindowActions")}) {
            auto *group = visual(desktop->contentItem(), name);
            QVERIFY(group);
            QTRY_VERIFY(qAbs(group->mapToScene({group->width() / 2, 0}).x() -
                             (toolbar->x() + toolbar->width() / 2)) < 1);
        }
        QVERIFY(save(desktop, QString("desktop-vertical-locked-%1").arg(suffix)));
        floating->setLocked(false);
        QTRY_VERIFY(unlock.isNull());
        Test::moveNativePointer(desktop, {80, 100});
        QTRY_COMPARE(toolbar->opacity(), 1.);
        QTRY_VERIFY(iconsReady(iconsReady, toolbar));
        QVERIFY(save(desktop, QString("desktop-vertical-hover-%1").arg(suffix)));
        floating->desktop()->setLayout("horizontal");
        desktop->resize(720, 220);
        // Let the native resize and GridLayout polish finish before using
        // the separate unlock window's geometry from the vertical layout.
        QTRY_VERIFY(toolbar->width() > toolbar->height());
        Test::moveNativePointer(desktop, {360, 100});
        floating->setLocked(true);
        QTRY_VERIFY(findWindow("desktopLyricUnlockWindow"));
        unlock = findWindow("desktopLyricUnlockWindow");
        QTRY_VERIFY(unlock->isVisible());
        QTRY_COMPARE(unlock->geometry().topLeft(),
                     desktop->mapToGlobal(lock->mapToScene({0, 0}).toPoint()));
        QTRY_VERIFY(QRect(QPoint(), desktop->size())
                        .contains(desktop->mapFromGlobal(unlock->mapToGlobal(QPoint(16, 16)))));
        Test::moveNativePointer(unlock, {16, 16});
        QTest::mouseClick(unlock, Qt::LeftButton, Qt::NoModifier, {16, 16});
        QTRY_VERIFY(!floating->locked());
        QTRY_VERIFY(unlock.isNull());
        QVERIFY(!desktop->flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(prepareDesktop());
        // QtTest's synthetic click retires the separate unlock window; route
        // the stationary cursor's next hover to the newly exposed tool.
        Test::moveNativePointer(desktop, desktop->mapFromGlobal(QCursor::pos()));
        QTRY_COMPARE(toolbar->opacity(), 1.);
    }
    floating->setLocked(true);
    floating->setDesktopEnabled(false);
    QTRY_VERIFY(!findWindow("desktopLyricUnlockWindow"));
    QTRY_VERIFY(!findWindow("desktopLyricWindow"));
    QVERIFY(!floating->unlockAvailable());
    QVERIFY(!floating->unlockVisible());
}

void DesktopPagesTest::nativeFloatingWindowsShareStateAndReleaseAcrossCycles()
{
    Session session;
    session.theme.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.router.shutdown();
        session.commands.shutdown();
        session.windows.shutdown();
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QJsonArray rows;
    const bool measure = qEnvironmentVariableIntValue("ECHO_FLOATING_PERF");
    const auto initialRss = measure ? floatingRssKiB() : 0;
    const int rowCount = measure ? 10000 : 150;
    for (int i = 0; i < rowCount; ++i)
        rows.append(QJsonObject{{"text", QStringLiteral("夜色温柔 让音乐留在身边 %1").arg(i)},
                                {"start_ms", i * 1000},
                                {"duration_ms", 1000},
                                {"words", QJsonArray{}},
                                {"translation", "Stay close to the music"},
                                {"romanization", "ye se wen rou"}});
    QVERIFY(playback.lyrics()->replace({{"lines", rows}}));
    const auto findWindow = [](const QString &name) -> QQuickWindow * {
        for (auto *candidate : QGuiApplication::topLevelWindows())
            if (candidate->objectName() == name)
                return qobject_cast<QQuickWindow *>(candidate);
        return nullptr;
    };
    auto *floating = session.windows.floating();
    const auto capture = qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (int cycle = 0; cycle < (measure ? 8 : 3); ++cycle) {
        floating->setDesktopEnabled(true);
        QTRY_VERIFY(findWindow("desktopLyricWindow"));
        QPointer<QQuickWindow> desktop = findWindow("desktopLyricWindow");
        QTRY_VERIFY(desktop->isVisible());
        QVERIFY(!desktop->transientParent());
        auto *viewport = visual(desktop->contentItem(), "desktopLyricViewport");
        QVERIFY(viewport);
        if (cycle == 0) {
            playback.lyricText()->setWantTranslation(false);
            playback.lyricText()->setWantRomanization(false);
            QVERIFY(!visual(desktop->contentItem(), "desktopLyricMore"));
            for (const auto &name :
                 {QString("desktopLyricSource"), QString("desktopLyricSettings"),
                  QString("desktopLyricOffset--1"), QString("desktopLyricOffset-1")}) {
                auto *button = visual(desktop->contentItem(), name);
                QVERIFY(button && button->isVisible());
                auto *icon = visual(button, "iconImage");
                QVERIFY(icon);
                QTRY_COMPARE(icon->property("status").toInt(), 1);
            }
            auto *translation = visual(desktop->contentItem(), "desktopLyricText-translation");
            auto *romanization = visual(desktop->contentItem(), "desktopLyricText-romanization");
            QVERIFY(translation && translation->isVisible());
            QVERIFY(romanization && romanization->isVisible());
            QTest::mouseClick(desktop, Qt::LeftButton, Qt::NoModifier,
                              translation->mapToScene({16, 16}).toPoint());
            QTRY_VERIFY(playback.lyricText()->wantTranslation());
            QTest::mouseClick(desktop, Qt::LeftButton, Qt::NoModifier,
                              translation->mapToScene({16, 16}).toPoint());
            QTRY_VERIFY(!playback.lyricText()->wantTranslation());
            QTest::mouseClick(desktop, Qt::LeftButton, Qt::NoModifier,
                              romanization->mapToScene({16, 16}).toPoint());
            QTRY_VERIFY(playback.lyricText()->wantRomanization());
            QTest::mouseClick(desktop, Qt::LeftButton, Qt::NoModifier,
                              romanization->mapToScene({16, 16}).toPoint());
            QTRY_VERIFY(!playback.lyricText()->wantRomanization());
            auto *transport = visual(desktop->contentItem(), "desktopLyricTransport");
            auto *actions = visual(desktop->contentItem(), "desktopLyricWindowActions");
            QVERIFY(transport && actions);
            QTRY_VERIFY(transport->width() > 90 && actions->width() > 90);
            QTRY_VERIFY(qAbs(transport->mapToScene({transport->width() / 2, 0}).x() -
                             desktop->width() / 2.) < 1);
            QTRY_VERIFY(
                qAbs(actions->mapToScene({actions->width(), 0}).x() - (desktop->width() - 12)) < 1);
            auto *title = visual(desktop->contentItem(), "desktopLyricTitle");
            QVERIFY(title);
            title->setProperty(
                "text",
                QStringLiteral("这是很长的曲目信息 Long title does not shift playback controls"));
            for (const int width : {720, 420, 360, 280, 240}) {
                desktop->resize(width, 200);
                QTRY_VERIFY(
                    qAbs(transport->mapToScene({transport->width() / 2, 0}).x() - width / 2.) < 1);
                QTRY_VERIFY(actions->mapToScene({0, 0}).x() >=
                            transport->mapToScene({transport->width(), 0}).x() + 6);
            }
            desktop->resize(720, 200);
            for (const auto size : {QSize(720, 140), QSize(720, 220), QSize(720, 360)}) {
                desktop->resize(size);
                QTRY_COMPARE(viewport->property("fontSize").toInt(), size.height() == 140   ? 20
                                                                     : size.height() == 220 ? 48
                                                                                            : 96);
                QCOMPARE(desktop->size(), size);
            }
            playback.lyricText()->setWantTranslation(true);
            playback.lyricText()->setWantRomanization(true);
            QTRY_COMPARE(viewport->property("fontSize").toInt(), 67);
            floating->desktop()->setLayout("vertical");
            desktop->resize(320, 600);
            QTRY_COMPARE(viewport->property("fontSize").toInt(), 42);
            desktop->resize(520, 600);
            QTRY_COMPARE(viewport->property("fontSize").toInt(), 64);
            floating->desktop()->setLayout("horizontal");
            desktop->resize(80, 100);
            auto *close = visual(desktop->contentItem(), "desktopLyricClose");
            QVERIFY(close);
            QTRY_VERIFY(close->mapToScene({0, 0}).x() >= 6 &&
                        close->mapToScene({close->width(), 0}).x() <= 74);
            desktop->resize(720, 200);
            QCoreApplication::processEvents();
            // Font/content changes must never write a new native size.
            QCOMPARE(desktop->size(), QSize(720, 200));
        }
        viewport->setProperty("renderTime", 1500);
        QTRY_COMPARE(viewport->property("currentIndex").toInt(), 1);
        QTest::qWait(80);
        if (!capture.isEmpty() && cycle == 0) {
            Test::moveNativePointer(desktop, {180, 22});
            auto *toolbar = visual(desktop->contentItem(), "desktopLyricToolbar");
            QVERIFY(toolbar);
            QTRY_VERIFY(toolbar->opacity() > .95);
            QVERIFY(desktop->grabWindow().save(capture + "/desktop-light.png"));
        }
        playback.lyricText()->setWantRomanization(true);
        playback.lyricText()->setRomanizationAsRuby(true);
        QTest::qWait(50);
        if (!capture.isEmpty() && cycle == 0)
            QVERIFY(desktop->grabWindow().save(capture + "/desktop-ruby.png"));
        floating->desktop()->setLayout("vertical");
        desktop->resize(200, 600);
        QTest::qWait(80);
        if (!capture.isEmpty() && cycle == 0)
            QVERIFY(desktop->grabWindow().save(capture + "/desktop-vertical.png"));
        floating->desktop()->setLayout("horizontal");
        desktop->resize(720, 200);
        floating->setLocked(true);
        QTRY_VERIFY(desktop->flags().testFlag(Qt::WindowTransparentForInput));
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            QTRY_VERIFY(nativeFloatingAppearance(desktop).value("ignoresMouseEvents").toBool());
            QVERIFY(nativeFloatingAppearance(desktop).value("allSpaces").toBool());
            QVERIFY(nativeFloatingAppearance(desktop).value("fullscreenAuxiliary").toBool());
            QVERIFY(!nativeFloatingAppearance(desktop).value("hidesOnDeactivate").toBool());
        }
#endif
        QTRY_VERIFY(findWindow("desktopLyricUnlockWindow"));
        floating->setLocked(false);
        QTRY_VERIFY(!desktop->flags().testFlag(Qt::WindowTransparentForInput));
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            QTRY_VERIFY(!nativeFloatingAppearance(desktop).value("ignoresMouseEvents").toBool());
        }
#endif
        QTRY_VERIFY(!findWindow("desktopLyricUnlockWindow"));
        floating->setDesktopEnabled(false);
        QTRY_VERIFY(desktop.isNull());
        floating->toggleMini();
        QTRY_VERIFY(findWindow("miniPlayerWindow"));
        QPointer<QQuickWindow> mini = findWindow("miniPlayerWindow");
        QTRY_VERIFY(!session.window->isVisible());
        QCOMPARE(mini->size(), QSize(360, 64));
        auto *controls = visual(mini->contentItem(), "miniPlayerControls");
        QVERIFY(controls);
        const auto area = mini->screen()->availableGeometry();
        mini->setPosition(area.x() + 80, area.y() + 80);
        if (cycle == 0) {
            QVERIFY(Test::prepareNativeInput(mini));
            auto *pin = visual(mini->contentItem(), "miniPlayerPin");
            QVERIFY(pin);
            Test::moveNativePointer(mini, {-30, -30});
            QTRY_VERIFY(!mini->property("showControls").toBool());
            QTRY_VERIFY(pin->isVisible() && pin->opacity() == 1 && pin->width() >= 24 &&
                        pin->height() >= 30);
            QCOMPARE(pin->property("checked").toBool(), floating->mini()->alwaysOnTop());
            if (!capture.isEmpty())
                QVERIFY(mini->grabWindow().save(capture + "/mini-pin-rest.png"));
            const auto clickPin = [&] {
                const auto center =
                    pin->mapToScene({pin->width() / 2, pin->height() / 2}).toPoint();
                QTest::mouseClick(mini, Qt::LeftButton, Qt::NoModifier, center);
            };
            const bool initialPin = floating->mini()->alwaysOnTop();
            clickPin();
            QTRY_COMPARE(floating->mini()->alwaysOnTop(), !initialPin);
            QTRY_COMPARE(pin->property("checked").toBool(), !initialPin);
            QTRY_COMPARE(mini->flags().testFlag(Qt::WindowStaysOnTopHint), !initialPin);
            clickPin();
            QTRY_COMPARE(floating->mini()->alwaysOnTop(), initialPin);
            QTRY_COMPARE(pin->property("checked").toBool(), initialPin);
            QTRY_COMPARE(mini->flags().testFlag(Qt::WindowStaysOnTopHint), initialPin);
        }
        const auto before = mini->position() + controls->mapToScene({0, 0}).toPoint();
        floating->setPanel("lyric");
        QTRY_COMPARE(mini->height(), 360);
        QTRY_VERIFY(!floating->expandsUp());
        QTRY_VERIFY((mini->position() + controls->mapToScene({0, 0}).toPoint() - before)
                        .manhattanLength() <= 1);
        QTest::qWait(80);
        const auto countInk = [](auto &&self, QQuickItem *item) -> int {
            int count = item->inherits("EchoMusic::LyricLine") ? 1 : 0;
            for (auto *child : item->childItems())
                count += self(self, child);
            return count;
        };
        const int mounted = countInk(countInk, mini->contentItem());
        QVERIFY(mounted > 0 && mounted < 100);
        if (measure)
            qInfo() << "floating-mini-cycle=" << cycle << "model-rows=" << rowCount
                    << "mounted-lyric-renderers=" << mounted << "rss-kib=" << floatingRssKiB();
        if (!capture.isEmpty() && cycle == 0)
            QVERIFY(mini->grabWindow().save(capture + "/mini-lyrics.png"));
        floating->setPanel("queue");
        QTRY_VERIFY(visual(mini->contentItem(), "miniPlayerQueue")->isVisible());
        floating->setPanel("");
        QTRY_COMPARE(mini->height(), 64);
        mini->setPosition(area.x() + 80, area.bottom() - 90);
        const auto lower = mini->position() + controls->mapToScene({0, 0}).toPoint();
        floating->setPanel("lyric");
        QTRY_VERIFY(floating->expandsUp());
        QTRY_VERIFY(
            (mini->position() + controls->mapToScene({0, 0}).toPoint() - lower).manhattanLength() <=
            1);
        floating->showMain();
        QTRY_VERIFY(mini.isNull());
        QTRY_VERIFY(session.window->isVisible());
    }
    QTest::qWait(150);
    if (measure)
        qInfo() << "floating-closed model-rows=" << rowCount << "initial-rss-kib=" << initialRss
                << "closed-rss-kib=" << floatingRssKiB();
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::nativePlaybackSurvivesRepeatedAppearanceUpdates()
{
#ifndef Q_OS_MACOS
    QSKIP("macOS appearance scheduling regression");
#endif
    Session session;
    const auto path = session.directory.filePath("appearance-silence.wav");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 48000 * 4 * 30;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
           << quint16(4) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    const QByteArray silence(bytes, '\0');
    QCOMPARE(stream.writeRawData(silence.constData(), silence.size()), silence.size());
    file.close();
    // Exercise the real FFmpeg/filter/CoreAudio bridge in the same process
    // as production QML, without account writes or a simulated audio clock.
    AudioEngine audio;
    QSignalSpy events(&audio, &AudioEngine::eventReceived), closed(&audio, &AudioEngine::closed);
    const auto contains = [&](const QString &kind) {
        for (const auto &event : events)
            if (event.front().toJsonObject().value("event").toString() == kind)
                return true;
        return false;
    };
    const auto position = [&] {
        for (auto it = events.crbegin(); it != events.crend(); ++it) {
            const auto event = it->front().toJsonObject();
            if (event.value("event").toString() == "time-update")
                return event.value("time").toDouble();
        }
        return 0.0;
    };
    const auto underruns = [&] {
        for (auto it = events.crbegin(); it != events.crend(); ++it) {
            const auto event = it->front().toJsonObject();
            if (event.value("event").toString() == "audio-output-stats")
                return event.value("output_stats").toObject().value("underruns").toDouble(-1);
        }
        return -1.0;
    };
    QVERIFY(session.create());
    QVERIFY(Test::prepareNativeInput(session.window));
    QTRY_VERIFY_WITH_TIMEOUT(contains("ready"), 15000);
    audio.load(path, 91);
    QTRY_VERIFY_WITH_TIMEOUT(contains("file-loaded"), 15000);
    audio.command(AudioEngine::Play);
    QTRY_VERIFY_WITH_TIMEOUT(position() > 0.5 && underruns() >= 0, 5000);
    const auto initialPosition = position(), initialUnderruns = underruns();
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = 0; i < 32; ++i) {
        session.theme.setMode(i % 2 ? ThemeStore::Light : ThemeStore::Dark);
        session.window->update();
        QTest::qWait(100);
    }
    QTRY_VERIFY_WITH_TIMEOUT(position() > initialPosition + 2.5, 5000);
    QCOMPARE(underruns(), initialUnderruns);
    qInfo() << "32 production appearance updates; audio seconds advanced"
            << position() - initialPosition << "elapsed ms" << elapsed.elapsed()
            << "output underruns before/after" << initialUnderruns << underruns();
    audio.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}

void DesktopPagesTest::productionHeaderWindowActionsStayAtRightEdge()
{
    Session session;
    session.preferences.setValue("playback/volume", 0);
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.shell.setMotionEnabled(false);
    session.window->setMinimumWidth(480);
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *header = visual(session.window->contentItem(), "shellTitleBar");
    auto *actions = visual(header, "titlebarWindowActions");
    auto *more = visual(header, "titlebarMoreAction");
    // 弹层按需创建：经真实入口所有者触发后再取实例，关闭后整体释放。
    auto *pageOwner = session.root->findChild<QObject *>("playbackPageOwner");
    QVERIFY(header && actions && more && pageOwner);
    QPointer<QObject> page;
    auto *fullscreen = visual(actions, "windowFullscreenAction");
    auto *caption = visual(actions, "windowCaptionControls");
    auto *close = visual(caption, "windowCloseAction");
    QVERIFY(fullscreen && caption && close);
    QCOMPARE(actions->property("nativeCaption").toBool(), session.windows.macOS());
    QCOMPARE(caption->isVisible(), !session.windows.macOS());
    QCOMPARE(fullscreen->isVisible(), !session.windows.macOS());
    // Exercise both presentation policies on the host platform. Window manager
    // behavior is still checked separately on actual Windows/X11/Wayland.
    const auto capture = qEnvironmentVariable("ECHO_HEADER_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool nativeCaption : {true, false}) {
            actions->setProperty("nativeCaption", nativeCaption);
            for (int zoom : {0, 5}) {
                session.shell.resetZoom();
                for (int step = 0; step < zoom; ++step)
                    session.shell.zoomIn();
                for (int width : {500, 1150, 1600}) {
                    session.window->resize(width, 750);
                    QTRY_VERIFY(qAbs(actions->width() - actions->implicitWidth()) < .6);
                    QTRY_VERIFY(qAbs(actions->mapToItem(header, {actions->width(), 0}).x() -
                                     (header->width() - 12)) < .6);
                    if (more->isVisible())
                        QTRY_VERIFY(qAbs(actions->x() - (more->x() + more->width()) - 12) < .6);
                    if (!nativeCaption) {
                        QVERIFY(caption->isVisible());
                        QTRY_COMPARE(close->size(), QSizeF(34, 34));
                        const auto sceneRect = close->mapRectToScene(close->boundingRect());
                        QVERIFY(qAbs(sceneRect.width() - 34 * session.shell.uiScale()) < .6);
                        QVERIFY(qAbs(sceneRect.height() - 34 * session.shell.uiScale()) < .6);
                        QTRY_VERIFY(qAbs(close->mapToItem(header, {34, 17}).x() -
                                         (header->width() - 12)) < .6);
                        QTRY_VERIFY(qAbs(close->mapToItem(header, {17, 17}).y() -
                                         header->height() / 2) < .6);
                    }
                }
            }
            session.shell.resetZoom();
            session.window->resize(1150, 750);
            QVERIFY(QMetaObject::invokeMethod(pageOwner, "open",
                                              Q_ARG(QVariant, QVariant(QVariantMap{}))));
            page = pageOwner->property("item").value<QObject *>();
            QTRY_VERIFY(page && page->property("opened").toBool());
            QQuickItem *overlayActions = nullptr;
            QTRY_VERIFY(
                (overlayActions = visual(session.window->contentItem(), "playbackWindowActions")));
            overlayActions->setProperty("nativeCaption", nativeCaption);
            auto *loader = visual(session.window->contentItem(), "playbackWindowActionsLoader");
            QVERIFY(loader);
            QTRY_COMPARE(overlayActions->width(), nativeCaption ? 0. : 148.);
            QVERIFY(!overlayActions->property("showMiniPlayer").toBool());
            QVERIFY(!visual(overlayActions, "windowMiniPlayerAction")->isVisible());
            QVERIFY(actions->property("showMiniPlayer").toBool());
            auto *overlayClose = visual(overlayActions, "windowCloseAction");
            QVERIFY(overlayClose);
            QCOMPARE(overlayClose->isVisible(), !nativeCaption);
            QCOMPARE(overlayClose->property("colors"), page->property("colors"));
            for (int zoom : {0, 5}) {
                session.shell.resetZoom();
                for (int step = 0; step < zoom; ++step)
                    session.shell.zoomIn();
                for (int width : {500, 1150, 1600}) {
                    session.window->resize(width, 750);
                    QTRY_VERIFY(qAbs(loader->mapToScene({loader->width(), 0}).x() -
                                     (session.window->width() - 12 * session.shell.uiScale())) <
                                .6);
                    if (!nativeCaption) {
                        for (const auto *name : {"windowFullscreenAction", "windowMinimizeAction",
                                                 "windowMaximizeAction", "windowCloseAction"}) {
                            auto *button = visual(overlayActions, name);
                            QVERIFY(button && button->isVisible());
                            const auto rect = button->mapRectToScene(button->boundingRect());
                            QVERIFY(qAbs(rect.width() - 34 * session.shell.uiScale()) < .6);
                            QVERIFY(qAbs(rect.height() - 34 * session.shell.uiScale()) < .6);
                            QVERIFY(qAbs(rect.center().y() - 23 * session.shell.uiScale()) < .6);
                        }
                    }
                }
                if (!capture.isEmpty()) {
                    session.window->resize(1150, 750);
                    QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                    session.window->update();
                    QTRY_VERIFY(!frames.isEmpty());
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/playback-%1-%2-%3.png")
                                      .arg(mode == ThemeStore::Dark ? "dark" : "light",
                                           nativeCaption ? "native" : "custom",
                                           zoom ? "125" : "100")));
                }
            }
            session.shell.resetZoom();
            QMetaObject::invokeMethod(page, "close");
            QTRY_VERIFY(page.isNull());
            if (!capture.isEmpty()) {
                QTest::qWait(150);
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/main-%1-%2.png")
                                  .arg(mode == ThemeStore::Dark ? "dark" : "light",
                                       nativeCaption ? "native" : "custom")));
            }
        }
    }
    actions->setProperty("nativeCaption", false);
    session.shell.setShowFullscreenButton(false);
    QTRY_VERIFY(!fullscreen->isVisible());
    QVERIFY(caption->isVisible());
    QCOMPARE(session.preferences.value("interface/showFullscreenButton").toBool(), false);
    ShellSettings reloaded(session.preferences);
    QVERIFY(!reloaded.showFullscreenButton());
    session.shell.setShowFullscreenButton(true);
    QTRY_VERIFY(fullscreen->isVisible());
    session.window->resize(1150, 750);
    auto *blockedInput = actions->property("background").value<QQuickItem *>();
    auto *mini = visual(actions, "windowMiniPlayerAction");
    // Mini 已是可用入口；保留原断言意图——按钮间的间隔按压必须被
    // WindowActionGroup 背景吞掉，不得穿透到窗口拖动层。
    QVERIFY(blockedInput && mini);
    QTest::qWait(100);
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier,
                      actions->mapToScene({36, 17}).toPoint());
    QVERIFY(blockedInput->property("pressed").toBool());
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier,
                        actions->mapToScene({36, 17}).toPoint());
    QTest::mouseDClick(session.window, Qt::LeftButton, Qt::NoModifier,
                       actions->mapToScene({36, 17}).toPoint());
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier,
                        actions->mapToScene({36, 17}).toPoint());
    QVERIFY(!session.windows.maximized());
    auto click = [&](QQuickItem *button) {
        QVERIFY(Test::prepareNativeInput(session.window));
        QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
        session.window->update();
        QTRY_VERIFY(!frames.isEmpty());
        const bool togglesFullscreen = button->objectName() == "windowFullscreenAction";
        const bool expectedFullscreen = !session.windows.fullscreen();
#ifdef Q_OS_MACOS
        // Observe before clicking: Qt state changes before AppKit's Spaces
        // transition finishes, so the inverse action must wait for did-exit
        // or did-enter rather than racing a still-running native transition.
        if (QGuiApplication::platformName() == "cocoa")
            nativeWindowAppearance(session.window);
#endif
        QSignalSpy clicked(button, SIGNAL(clicked()));
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene({17, 17}).toPoint());
        QCOMPARE(clicked.count(), 1);
        if (togglesFullscreen) {
            QTRY_COMPARE(session.windows.fullscreen(), expectedFullscreen);
#ifdef Q_OS_MACOS
            if (QGuiApplication::platformName() == "cocoa")
                QTRY_COMPARE_WITH_TIMEOUT(
                    nativeWindowAppearance(session.window).value("fullscreenSettled").toString(),
                    expectedFullscreen ? QString("full") : QString("normal"), 10000);
#endif
        }
    };
    auto *maximize = visual(actions, "windowMaximizeAction");
    QVERIFY(maximize);
    click(maximize);
    QTRY_VERIFY(session.windows.maximized());
    QTRY_COMPARE(maximize->property("glyph").toString(), QString("copy"));
    click(maximize);
    QTRY_VERIFY(!session.windows.maximized());
    QTRY_COMPARE(maximize->property("glyph").toString(), QString("square"));
    click(fullscreen);
    QTRY_VERIFY(session.windows.fullscreen());
    QTRY_VERIFY(!caption->isVisible());
    QVERIFY(fullscreen->isVisible());
    click(fullscreen);
    QTRY_VERIFY(!session.windows.fullscreen());
    QTRY_VERIFY(caption->isVisible());
    click(maximize);
    QTRY_VERIFY(session.windows.maximized());
    click(fullscreen);
    QTRY_VERIFY(session.windows.fullscreen());
    click(fullscreen);
    QTRY_VERIFY(session.windows.maximized());
    click(maximize);
    QTRY_VERIFY(!session.windows.maximized());
    QVERIFY(QMetaObject::invokeMethod(pageOwner, "open", Q_ARG(QVariant, QVariant(QVariantMap{}))));
    page = pageOwner->property("item").value<QObject *>();
    QTRY_VERIFY(page && page->property("opened").toBool());
    auto *overlayActions = visual(session.window->contentItem(), "playbackWindowActions");
    QVERIFY(overlayActions);
    overlayActions->setProperty("nativeCaption", false);
    auto *overlayFullscreen = visual(overlayActions, "windowFullscreenAction");
    auto *overlayCaption = visual(overlayActions, "windowCaptionControls");
    QVERIFY(overlayFullscreen && overlayCaption);
    click(overlayFullscreen);
    QTRY_VERIFY(session.windows.fullscreen());
    QTRY_VERIFY(!overlayCaption->isVisible());
    click(overlayFullscreen);
    QTRY_VERIFY(!session.windows.fullscreen());
    QTRY_VERIFY(overlayCaption->isVisible());
    QMetaObject::invokeMethod(page, "close");
    QTRY_VERIFY(page.isNull());
    if (!session.windows.macOS()) {
        QTest::keyClick(session.window, Qt::Key_F11);
        QTRY_VERIFY(session.windows.fullscreen());
        QTest::keyClick(session.window, Qt::Key_F11);
        QTRY_VERIFY(!session.windows.fullscreen());
    }
    auto *minimize = visual(actions, "windowMinimizeAction");
    QVERIFY(minimize);
    click(minimize);
    QTRY_COMPARE(session.window->windowState(), Qt::WindowMinimized);
    session.windows.showWindow();
    QVERIFY(Test::prepareNativeInput(session.window));
    QTRY_COMPARE(session.window->windowState(), Qt::WindowNoState);
    QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
    session.window->update();
    QTRY_VERIFY(!frames.isEmpty());
    QTest::mouseMove(session.window, close->mapToScene({17, 17}).toPoint());
    QTRY_VERIFY(close->property("hovered").toBool());
    QTRY_COMPARE(close->property("foreground").value<QColor>(), QColor("#ffffff"));
    auto *closeBackground = close->property("background").value<QQuickItem *>();
    QVERIFY(closeBackground);
    QCOMPARE(closeBackground->property("color").value<QColor>(), QColor("#c42b1c"));
    session.windows.setCloseToTray(false);
    QSignalSpy closeClicked(close, SIGNAL(clicked()));
    click(close);
    QTRY_COMPARE(closeClicked.size(), 1);
    QTRY_VERIFY(!session.window->isVisible());
    QCOMPARE(warnings.size(), 0);
}
