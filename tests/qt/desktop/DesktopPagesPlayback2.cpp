#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::playbackQueueLocationSkipsVisibleCurrentSong()
{
    Session session;
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    PlaybackTracks tracks;
    for (int i = 0; i < 1000; ++i) {
        PlaybackTrack track;
        track.id = QString::number(i + 1);
        track.localPath = QStringLiteral("/ui-test/locate-%1.wav").arg(i);
        track.title = QStringLiteral("定位歌曲 %1").arg(i);
        tracks.append(track);
    }
    playback.appendTracks(tracks);
    playback.queue()->setCurrent(10);
    session.theme.setMotionEnabled(false);
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (bool zoom : {false, true}) {
            session.theme.setMode(mode);
            session.shell.resetZoom();
            if (zoom)
                for (int i = 0; i < 5; ++i)
                    session.shell.zoomIn();
            QPointer<QObject> drawer = session.openPlayerPopup("queue", "playbackQueueDrawer");
            QVERIFY(drawer);
            QTRY_VERIFY(drawer->property("opened").toBool());
            auto *list = visual(session.window->contentItem(), "playbackQueueList");
            auto *button = visual(session.window->contentItem(), "queueLocateButton");
            QVERIFY(list && button);
            QTRY_COMPARE(list->property("count").toInt(), 1000);
            list->setProperty("contentY", 504.); // Current row is at y=56, away from the center.
            QVERIFY(session.settleScene());
            QSignalSpy changes(list, SIGNAL(contentYChanged()));
            QVERIFY(session.clickItem(button));
            QCOMPARE(changes.count(), 0);
            QCOMPARE(list->property("contentY").toReal(), 504.);
            // Cached row above the viewport is not visible.
            list->setProperty("contentY", 616.);
            QVERIFY(session.settleScene());
            QVERIFY(session.clickItem(button));
            QVERIFY(list->property("contentY").toReal() < 560.);
            const qreal located = list->property("contentY").toReal();
            changes.clear();
            QVERIFY(session.clickItem(button));
            QCOMPARE(changes.count(), 0);
            QCOMPARE(list->property("contentY").toReal(), located);
            QVERIFY(visualAll(list, "queueRow").size() < 80);
            QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
            QTRY_VERIFY(drawer.isNull());
        }
}

void DesktopPagesTest::playbackQueueEmptyIndicatorAndCloseIcon()
{
    const auto cursorPosition = QCursor::pos();
    const auto restoreCursor = qScopeGuard([cursorPosition] { QCursor::setPos(cursorPosition); });
    Session session;
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    QPointer<QObject> drawer;
    QVERIFY(!session.root->findChild<QObject *>("playbackQueueDrawer"));
    const auto captureDirectory = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
    if (!captureDirectory.isEmpty())
        QVERIFY(QDir().mkpath(captureDirectory));
    const auto headerCaptures = qEnvironmentVariable("ECHO_QUEUE_HEADER_CAPTURE_DIR");
    if (!headerCaptures.isEmpty())
        QVERIFY(QDir().mkpath(headerCaptures));
    session.theme.setMotionEnabled(false);
    const auto verifyHeader = [&](const QString &state) {
        auto *body = drawer->property("contentItem").value<QQuickItem *>();
        auto *actions = visual(body, "queueHeaderActions");
        auto *arrows = visual(body, "queueSwitcherArrows");
        auto *close = visual(body, "queueCloseButton");
        QVERIFY(body && actions && arrows && close);
        const auto originalWidth = session.window->width();
        const auto originalMinimum = session.window->minimumWidth();
        session.window->setMinimumWidth(320);
        const auto restore = qScopeGuard([&] {
            session.shell.resetZoom();
            session.window->setWidth(originalWidth);
            session.window->setMinimumWidth(originalMinimum);
        });
        for (qreal scale : {1.0, 1.25}) {
            session.shell.resetZoom();
            while (session.shell.uiScale() < scale - .001)
                session.shell.zoomIn();
            QTRY_COMPARE(drawer->property("uiScale").toReal(), scale);
            for (int width : {1150, 420}) {
                session.window->setWidth(width);
                QTRY_VERIFY(qAbs(actions->mapToItem(body, {actions->width(), 0}).x() -
                                 (body->width() - 16)) < .5);
                QTRY_COMPARE(actions->width(), 4 * 32.0 + 3 * 4);
                QQuickItem *previous = nullptr;
                for (const auto *name : {"queueScrollTopButton", "queueLocateButton",
                                         "queueAddButton", "queueClearButton"}) {
                    auto *button = visual(actions, name);
                    QVERIFY(button && button->isVisible());
                    QCOMPARE(button->width(), 32.0);
                    QCOMPARE(button->height(), 32.0);
                    if (previous) {
                        QCOMPARE(button->x() - previous->x() - previous->width(), 4.0);
                        QCOMPARE(button->y(), previous->y());
                    }
                    previous = button;
                }
                QTRY_VERIFY(qAbs(previous->mapToItem(body, {16, 16}).x() -
                                 close->mapToItem(body, {16, 16}).x()) < .5);
                QCOMPARE(arrows->isVisible(), playback.queueCatalog()->previewCount() > 1);
                if (arrows->isVisible()) {
                    QCOMPARE(arrows->width(), 2 * 24.0 + 12);
                    QVERIFY(arrows->mapToItem(body, {arrows->width(), 0}).x() <=
                            actions->mapToItem(body, {0, 0}).x() - 10);
                }
                if (QGuiApplication::platformName() != "offscreen") {
                    QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                    session.window->update();
                    QTRY_VERIFY(!frames.isEmpty());
                    if (!headerCaptures.isEmpty())
                        QVERIFY(session.window->grabWindow().save(
                            headerCaptures + QString("/queue-%1-%2-%3-%4.png")
                                                 .arg(int(session.theme.mode()))
                                                 .arg(scale)
                                                 .arg(width)
                                                 .arg(state)));
                }
            }
        }
    };
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        QTest::mouseMove(session.window, QPoint(20, 400));
        QCOMPARE(playback.queueCatalog()->previewCount(), 0);
        drawer = session.openPlayerPopup("queue", "playbackQueueDrawer");
        QVERIFY(drawer);
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto *track = visual(session.window->contentItem(), "queueProgressTrack");
        auto *thumb = visual(session.window->contentItem(), "queueProgressThumb");
        auto *close = visual(session.window->contentItem(), "queueCloseButton");
        QVERIFY(track && thumb && close);
        QVERIFY(!track->isVisible());
        QVERIFY(!thumb->isVisible());
        verifyHeader("empty");
        auto *icon = visual(close, "iconImage");
        QVERIFY(icon);
        QTRY_VERIFY(icon->property("source").toUrl().path().startsWith("/x/"));
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        auto expectedInk = session.theme.floatingText();
        expectedInk.setAlphaF(.8);
        QCOMPARE(close->property("foreground").value<QColor>(), expectedInk);
        QCOMPARE(close->width() - close->property("padding").toReal() * 2, 20.);
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                captureDirectory +
                (mode == ThemeStore::Dark ? "/queue-empty-dark.png" : "/queue-empty-light.png")));

        // The catalogue retains an internal empty default model, but it is
        // not a visible queue. Adding/removing songs updates the projection.
        PlaybackTrack song;
        song.id = "queue-icon-domain";
        song.localPath = "/ui-test/queue-icon.wav";
        song.title = QStringLiteral("队列图标检查");
        playback.appendTracks({song});
        QTRY_COMPARE(playback.queueCatalog()->previewCount(), 1);
        QTRY_VERIFY(!track->isVisible() && !thumb->isVisible());
        auto *list = visual(session.window->contentItem(), "playbackQueueList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 1);
        verifyHeader("single");
        QVERIFY(Test::prepareNativeInput(session.window));
        QQuickItem *row = nullptr;
        QTRY_VERIFY(([&] {
            for (auto *candidate : visualAll(list, "queueRow"))
                if (candidate->isVisible() && !candidate->property("pooled").toBool() &&
                    candidate->property("index").toInt() == 0 &&
                    candidate->mapRectToItem(list, candidate->boundingRect())
                        .intersects(list->boundingRect())) {
                    row = candidate;
                    return true;
                }
            return false;
        })());
        QVERIFY(session.settleScene());
        const auto hoverPoint = row->mapToScene(QPointF(100, 28)).toPoint();
        Test::moveNativePointer(session.window, hoverPoint);
        QTRY_VERIFY2(row->property("hovered").toBool(),
                     qPrintable(QString("point=%1,%2 visible=%3 enabled=%4 hoverEnabled=%5 "
                                        "active=%6 pooled=%7 globalTarget=%8,%9 cursor=%10,%11 "
                                        "window=%12,%13 %14x%15")
                                    .arg(hoverPoint.x())
                                    .arg(hoverPoint.y())
                                    .arg(row->isVisible())
                                    .arg(row->isEnabled())
                                    .arg(row->property("hoverEnabled").toBool())
                                    .arg(session.window->isActive())
                                    .arg(row->property("pooled").toBool())
                                    .arg(session.window->mapToGlobal(hoverPoint).x())
                                    .arg(session.window->mapToGlobal(hoverPoint).y())
                                    .arg(QCursor::pos().x())
                                    .arg(QCursor::pos().y())
                                    .arg(session.window->x())
                                    .arg(session.window->y())
                                    .arg(session.window->width())
                                    .arg(session.window->height())));
        QVERIFY(playback.queueCatalog()->viewActive());
        auto *remove = visual(row, "queueRemoveButton");
        QVERIFY(remove);
        QTRY_VERIFY(remove->isVisible());
        auto *removeIcon = visual(remove, "iconImage");
        QVERIFY(removeIcon);
        QTRY_COMPARE(removeIcon->property("status").toInt(), 1);
        QVERIFY(removeIcon->property("source").toUrl().path().startsWith("/x/"));

        // The indicator counts browsable queues, not songs in a queue.
        auto secondSong = song;
        secondSong.id = "queue-indicator-second-song";
        secondSong.localPath = "/ui-test/queue-indicator-second.wav";
        playback.appendTracks({secondSong});
        QTRY_COMPARE(list->property("count").toInt(), 2);
        QCOMPARE(playback.queueCatalog()->previewCount(), 1);
        QVERIFY(!track->isVisible() && !thumb->isVisible());
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                captureDirectory +
                (mode == ThemeStore::Dark ? "/queue-single-dark.png" : "/queue-single-light.png")));

        playback.queueCatalog()->activate("queue-indicator/source", "Second queue", {song}, 0);
        QTRY_COMPARE(playback.queueCatalog()->previewCount(), 2);
        QTRY_VERIFY(track->isVisible() && thumb->isVisible());
        verifyHeader("multiple");
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                captureDirectory + (mode == ThemeStore::Dark ? "/queue-multiple-dark.png"
                                                             : "/queue-multiple-light.png")));
        playback.queueCatalog()->removeViewed();
        QTRY_COMPARE(playback.queueCatalog()->previewCount(), 1);
        QTRY_VERIFY(!track->isVisible() && !thumb->isVisible());
        playback.queueCatalog()->setViewIndex(0);
        playback.queueCatalog()->removeViewed();
        QTRY_COMPARE(playback.queueCatalog()->previewCount(), 0);
        QTRY_VERIFY(!track->isVisible() && !thumb->isVisible());
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                          close->mapToScene(QPointF(16, 16)).toPoint());
        QTRY_VERIFY(drawer.isNull());
        playback.queueCatalog()->clear();
    }
    QCOMPARE(warnings.count(), 0);
}

void DesktopPagesTest::playbackChromeMatchesSource()
{
    const auto cursor = QCursor::pos();
    const auto restore = qScopeGuard([cursor] { QCursor::setPos(cursor); });
    Session session;
    session.theme.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    // 弹层按需创建：经真实入口所有者触发打开后再获取实例。
    auto *pageOwner = session.root->findChild<QObject *>("playbackPageOwner");
    QVERIFY(pageOwner);
    QVERIFY(QMetaObject::invokeMethod(pageOwner, "open", Q_ARG(QVariant, QVariant(QVariantMap{}))));
    QPointer<QObject> page = session.root->findChild<QObject *>("playbackPage");
    QVERIFY(page);
    QTRY_VERIFY(page->property("opened").toBool());
    auto *collapse = visual(session.window->contentItem(), "playbackCollapseAction");
    auto *tools = visual(session.window->contentItem(), "playbackLyricTools");
    auto *source = visual(tools, "lyricSourceAction");
    auto *copy = visual(tools, "lyricTextAction-copy");
    auto *reset = visual(tools, "lyricOffsetAction-0");
    auto *bar = page->property("toastAnchor").value<QQuickItem *>();
    QVERIFY(collapse && tools && source && copy && reset && bar);
    // 添加到歌单要求已登录且有曲目；先准备账号会话和曲目身份。
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    PlaybackServiceTestAccess::quality(playback, "flac");
    QTRY_VERIFY(playback.hasTrack());
    QQuickItem *add = nullptr;
    QTRY_VERIFY((add = visual(bar, "playerbar-action-add-to-playlist")));
    QCOMPARE(add->property("glyph").toString(), QString("text-plus"));
    auto *addImage = visual(add, "iconImage");
    QVERIFY(addImage);
    QTRY_COMPARE(addImage->property("status").toInt(), 1);
    QCOMPARE(addImage->width(), 20.);
    QCOMPARE(add->width(), 28.);
    QCOMPARE(collapse->width(), 34.);
    QCOMPARE(collapse->property("hoverBackground").toBool(), !session.windows.macOS());
    const auto capture = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int i = 0; i < 5; ++i)
                    session.shell.zoomIn();
            QVERIFY(Test::prepareNativeInput(session.window));
            QVERIFY(session.settleScene());
            const auto center = collapse->mapToScene({17, 17});
            if (!session.windows.macOS()) {
                QVERIFY(qAbs(center.y() - 23 * session.shell.uiScale()) < .5);
                QVERIFY(qAbs(collapse->mapToScene({0, 0}).x() - 16 * session.shell.uiScale()) < .5);
            }
            const auto sceneRect = collapse->mapRectToScene(collapse->boundingRect());
            QVERIFY(qAbs(sceneRect.width() - 34 * session.shell.uiScale()) < .5);
            QVERIFY(qAbs(sceneRect.height() - 34 * session.shell.uiScale()) < .5);
            auto *icon = visual(collapse, "iconImage");
            QVERIFY(icon);
            const auto iconRect = icon->mapRectToScene(icon->boundingRect());
            QVERIFY(qAbs(iconRect.width() - 20 * session.shell.uiScale()) < .5);
            QVERIFY(qAbs(iconRect.height() - 20 * session.shell.uiScale()) < .5);
#ifdef Q_OS_MACOS
            if (QGuiApplication::platformName() == "cocoa") {
                const auto bounds = session.windows.nativeCaptionBounds();
                QVERIFY(!bounds.isEmpty());
                QVERIFY(qAbs(collapse->mapToScene({0, 0}).x() - bounds.right() -
                             8 * session.shell.uiScale()) < .5);
                QTRY_VERIFY(qAbs(nativeWindowAppearance(session.window)
                                     .value("buttonFrames")
                                     .toList()
                                     .first()
                                     .toRectF()
                                     .center()
                                     .y() -
                                 center.y()) < .5);
            }
#endif
            const auto hover = [&](QQuickItem *item) {
                Test::moveNativePointer(
                    session.window,
                    item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
            };
            const auto pixel = [&](QQuickItem *item) {
                const auto frame = session.window->grabWindow();
                const auto point =
                    item->mapToScene({4, item->height() / 2}) * frame.devicePixelRatio();
                return frame.pixelColor(point.toPoint());
            };
            Test::moveNativePointer(session.window, QPoint(400, 200));
            QTRY_VERIFY(!collapse->property("hovered").toBool());
            const auto normal = pixel(collapse);
            hover(collapse);
            QTRY_VERIFY(collapse->property("hovered").toBool());
            if (session.windows.macOS())
                QCOMPARE(pixel(collapse), normal);
            else
                QTRY_VERIFY(pixel(collapse) != normal);
            auto *collapseImage = visual(collapse, "iconImage");
            QVERIFY(collapseImage);
            QTRY_COMPARE(collapseImage->property("status").toInt(), 1);
            if (!capture.isEmpty()) {
                QSignalSpy submitted(session.window, &QQuickWindow::frameSwapped);
                session.window->update();
                QTRY_VERIFY(!submitted.isEmpty());
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/collapse-%1-%2.png")
                                  .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                  .arg(zoomed ? 125 : 100)));
#ifdef Q_OS_MACOS
                if (QGuiApplication::platformName() == "cocoa") {
                    const auto path =
                        capture + QString("/collapse-native-%1-%2.png")
                                      .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                      .arg(zoomed ? 125 : 100);
                    QCOMPARE(QProcess::execute("/usr/sbin/screencapture",
                                               {"-x", "-o", "-l",
                                                nativeWindowAppearance(session.window)
                                                    .value("windowNumber")
                                                    .toString(),
                                                path}),
                             0);
                }
#endif
            }
            // Domain-only presentation input; no upstream response or audio fixture.
            playback.lyricText()->clear();
            const auto sourceTop = source->mapToScene(QPointF{}).y();
            QVERIFY(qAbs(tools->y() - qMax(104., 35 / session.shell.uiScale() + 58)) < .5);
            const auto toolsHeight = tools->height();
            playback.lyricText()->replace(
                {{"offset_ms", 0},
                 {"lines", QJsonArray{QJsonObject{{"text", QStringLiteral("歌词 Song")},
                                                  {"start_ms", 0},
                                                  {"duration_ms", 4000},
                                                  {"words", QJsonArray{}}}}}},
                "chrome-layout");
            QTRY_COMPARE(tools->height(), toolsHeight);
            QCOMPARE(source->mapToScene(QPointF{}).y(), sourceTop);
            QTRY_COMPARE(tools->property("contentHeight").toReal(), 228.);
            QCOMPARE(reset->opacity(), 0.);
            const auto copyY = copy->mapToItem(tools, QPointF{}).y();
            playback.lyricText()->adjustTrackOffset(1);
            QTRY_COMPARE(reset->opacity(), 1.);
            QCOMPARE(copy->mapToItem(tools, QPointF{}).y(), copyY);
            playback.lyricText()->resetTrackOffset();
            QTRY_COMPARE(reset->opacity(), 0.);
            QCOMPARE(source->width(), 36.);
            QCOMPARE(source->property("glyphSize").toReal(), 14.);
            QCOMPARE(source->property("plain").toBool(), false);
            auto *offset = visual(tools, "lyricOffsetAction--1");
            QVERIFY(offset);
            QCOMPARE(offset->property("glyphSize").toReal(), 15.);
            hover(copy);
            QTRY_VERIFY(copy->property("hovered").toBool());
            auto *background = copy->property("background").value<QQuickItem *>();
            QVERIFY(background && background->isVisible());
            QTRY_VERIFY(background->property("highlighted").toBool());
            auto *copyGlyph = visual(copy, "buttonGlyph");
            QVERIFY(copyGlyph);
            auto *copyImage = visual(copyGlyph, "iconImage");
            QVERIFY(copyImage);
            QTRY_COMPARE(copyImage->property("status").toInt(), 1);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/tools-%1-%2.png")
                                  .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                  .arg(zoomed ? 125 : 100)));
            playback.lyricText()->replace(
                {{"offset_ms", 0},
                 {"lines", QJsonArray{QJsonObject{{"text", QStringLiteral("歌词 Song")},
                                                  {"start_ms", 0},
                                                  {"duration_ms", 4000},
                                                  {"words", QJsonArray{}},
                                                  {"translation", "Translation"},
                                                  {"romanization", "yin yi"}}}}},
                "chrome-layout-translated");
            QTRY_COMPARE(tools->height(), toolsHeight);
            QTRY_COMPARE(tools->property("contentHeight").toReal(), 316.);
            QCOMPARE(source->mapToScene(QPointF{}).y(), sourceTop);
            auto *translation = visual(tools, "lyricTextAction-translation");
            QVERIFY(translation && translation->isVisible());
            playback.lyricText()->setWantTranslation(true);
            QTRY_VERIFY(translation->property("accent").toBool());
            playback.lyricText()->setWantTranslation(false);
            QTRY_VERIFY(!translation->property("accent").toBool());
        }
    }
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(page.isNull()); // 按需弹层关闭后整体释放
    for (const auto &warning : warnings)
        qWarning() << warning;
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::playbackLoadingRevealsFinalPlayPauseIcon()
{
    Session session;
    session.preferences.setValue("playback/volume", 0);
    const auto path = session.directory.filePath("loading-icon.wav");
    QVERIFY(writeFloatingTestWave(path));
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    QQuickItem *button = nullptr;
    QTRY_VERIFY((button = visual(session.window->contentItem(), "playerbar-action-play-toggle")));
    auto *initialIcon = visual(button, "playPauseIcon");
    QVERIFY(initialIcon);
    QTRY_VERIFY(initialIcon->property("ready").toBool());
    int revealedFrames = 0;
    QStringList intermediateFrames;
    const auto observation =
        connect(session.window, &QQuickWindow::afterAnimating, session.window, [&] {
            auto *icon = visual(button, "playPauseIcon");
            if (!icon || !icon->isVisible() || playback.loading() ||
                button->property("glyph").toString() != "pause")
                return;
            ++revealedFrames;
            const auto phase = icon->property("phase").toReal();
            if (phase < 1.)
                intermediateFrames.append(QString::number(phase));
        });
    const auto disconnectObservation = qScopeGuard([&] { disconnect(observation); });
    for (int cycle = 0; cycle < 3; ++cycle) {
        PlaybackTrack track;
        track.id = QString("loading-icon-%1").arg(cycle);
        track.localPath = path;
        track.title = QStringLiteral("加载完成后的播放图标");
        playback.playTracks({track}, 0, "Loading icon", track.id);
        QVERIFY(playback.loading());
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 15000);
        QTest::qWait(220);
    }
    QVERIFY(revealedFrames > 0);
    QVERIFY2(intermediateFrames.isEmpty(),
             qPrintable("Loading revealed intermediate play/pause phases: " +
                        intermediateFrames.join(',')));
    qInfo() << "Post-loading pause frames" << revealedFrames;
}

void DesktopPagesTest::playbackToolbarsMatchSourceAcrossWidths()
{
    const auto cursorPosition = QCursor::pos();
    const auto restoreCursor = qScopeGuard([cursorPosition] { QCursor::setPos(cursorPosition); });
    auto clipboard = std::make_unique<QMimeData>();
    const auto *original = QGuiApplication::clipboard()->mimeData();
    if (original) {
        for (const auto &format : original->formats())
            clipboard->setData(format, original->data(format));
    }
    const auto restoreClipboard = qScopeGuard(
        [&clipboard] { QGuiApplication::clipboard()->setMimeData(clipboard.release()); });
    Session session;
    session.preferences.setValue("playback/volume", 0);
    const auto path = session.directory.filePath("toolbar-silence.wav");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 16000 * 2 * 30;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    const QByteArray silence(bytes, '\0');
    QCOMPARE(stream.writeRawData(silence.constData(), silence.size()), silence.size());
    file.close();
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    PlaybackTrack track;
    track.id = "toolbar-local";
    track.localPath = path;
    track.title = QStringLiteral("中文 Song Title — 用于检查长标题和中英文显示的歌曲");
    track.artist = QStringLiteral("Artist 歌手");
    playback.playTracks({track}, 0, "Toolbar", "toolbar-local");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 15000);
    auto *playingToggle = visual(session.window->contentItem(), "playerbar-action-play-toggle");
    QVERIFY(playingToggle);
    QTRY_COMPARE(playingToggle->property("glyph").toString(), QString("pause"));
    auto *playingIcon = visual(visual(playingToggle, "playPausePauseIcon"), "iconImage");
    QVERIFY(playingIcon);
    QTRY_VERIFY(playingIcon->property("source").toUrl().path().startsWith("/pause/"));
    QTRY_COMPARE(playingIcon->property("status").toInt(), 1);
    playback.pause();
    QTRY_VERIFY(!playback.playing());
    QTRY_COMPARE(playingToggle->property("glyph").toString(), QString("play"));
    const auto effectBadge = [&] {
        auto *actions = session.commands.playerbarActions();
        for (int row = 0; row < actions->rowCount(); ++row) {
            const auto index = actions->index(row);
            if (index.data(ShellActionModel::IdRole) == "effect")
                return index.data(ShellActionModel::BadgeRole).toString();
        }
        return QStringLiteral("missing-action");
    };
    QCOMPARE(effectBadge(), QString());
    playback.audioControls()->applyEqualizerPreset(1);
    QTRY_COMPARE(effectBadge(), QString("EQ"));
    const auto dspLibrary = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
    if (!dspLibrary.isEmpty()) {
        auto *audio = playback.audioControls();
        audio->providers()->importFile(QUrl::fromLocalFile(dspLibrary));
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && !audio->providerId().isEmpty(), 15000);
        audio->selectProviderPreset("kugou-vinyl");
        QTRY_VERIFY_WITH_TIMEOUT(
            !audio->effectPending() && audio->providerPresetId() == "kugou-vinyl", 15000);
        QTRY_COMPARE(effectBadge(), QString("FX"));
        audio->deactivateProvider();
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && audio->providerId().isEmpty(), 15000);
        QTRY_COMPARE(effectBadge(), QString("EQ"));
    }
    playback.audioControls()->resetEqualizer();
    QTRY_COMPARE(effectBadge(), QString());
    const auto captureDirectory = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
    if (!captureDirectory.isEmpty())
        QVERIFY(QDir().mkpath(captureDirectory));
    auto *bar = visual(session.window->contentItem(), "playerbar");
    auto *center = visual(session.window->contentItem(), "playerbarCenterArea");
    auto *progress = visual(session.window->contentItem(), "playerbarProgressRow");
    auto *rightArea = visual(bar, "playerbarRightArea");
    auto *rightStrip = visual(bar, "playerbarRightActions");
    QPointer<QObject> page;
    QVERIFY(!session.root->findChild<QObject *>("playbackPage"));
    QVERIFY(bar && center && progress && rightArea && rightStrip);
    const auto centered = [](QQuickItem *area, QQuickItem *owner) {
        return qAbs(area->mapToItem(owner, {area->width() / 2, 0}).x() - owner->width() / 2) < 0.6;
    };
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const int width : {700, 1150, 1600}) {
            QSignalSpy rendered(session.window, &QQuickWindow::frameSwapped);
            session.window->resize(width, 750);
            QTRY_VERIFY(!rendered.isEmpty());
            QTRY_VERIFY(centered(center, bar));
            QTRY_COMPARE(progress->width(), qMax(0., qMin(center->width(), 480.) - 8));
            // The four default right actions and More occupy 202px.
            // Spare room must not hide Queue, and shown actions must fit.
            if (rightArea->width() >= 202) {
                QQuickItem *queueButton = nullptr;
                QTRY_VERIFY((queueButton = visual(rightStrip, "playerbar-action-queue")) &&
                            queueButton->isVisible());
                QTRY_COMPARE(rightStrip->property("count").toInt(), 4);
                QVERIFY(rightStrip->width() + 46 <= rightArea->width() + .5);
            }
        }
        session.window->resize(1150, 750);
        QTest::qWait(250);
        auto *title = visual(session.window->contentItem(), "playerbarSongTitle");
        QVERIFY(title);
        QCOMPARE(title->property("font").value<QFont>().weight(), QFont::Bold);
        auto *toggle = visual(session.window->contentItem(), "playerbar-action-play-toggle");
        QVERIFY(toggle);
        QCOMPARE(toggle->width(), 38.);
        // Capture the rendered playing state as well as the paused layout:
        // an accessible Pause action alone does not prove its SVG is visible.
        playback.play();
        QTRY_VERIFY(playback.playing() && !playback.loading());
        QTRY_COMPARE(toggle->property("glyph").toString(), QString("pause"));
        auto *pauseIcon = visual(visual(toggle, "playPausePauseIcon"), "iconImage");
        QVERIFY(pauseIcon);
        QTRY_COMPARE(pauseIcon->property("status").toInt(), 1);
        QTest::qWait(100);
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                captureDirectory + (mode == ThemeStore::Dark ? "/playerbar-playing-dark.png"
                                                             : "/playerbar-playing-light.png")));
        playback.pause();
        QTRY_VERIFY(!playback.playing());
        QTRY_COMPARE(toggle->property("glyph").toString(), QString("play"));
        auto *seek = visual(progress, "playbackSeekSlider");
        QVERIFY(seek);
        auto *handle = seek->property("handle").value<QQuickItem *>();
        QVERIFY(handle);
        Test::moveNativePointer(session.window, QPoint(400, 200));
        QTRY_COMPARE(handle->opacity(), 0.);
        if (QGuiApplication::platformName() != "offscreen") {
            QVERIFY(Test::prepareNativeInput(session.window));
            // The shared desktop can receive real cursor events between
            // synthetic test events. Verify enter/leave in reduced-motion
            // mode, then restore animations before production captures.
            session.theme.setMotionEnabled(false);
            const auto restoreMotion =
                qScopeGuard([&session] { session.theme.setMotionEnabled(true); });
            const auto enterSeek = [&] {
                const auto position =
                    seek->mapToScene({seek->width() / 2, seek->height() / 2}).toPoint();
                Test::moveNativePointer(session.window, position);
                return seek->property("hovered").toBool() && handle->opacity() == 1.;
            };
            QTRY_VERIFY(enterSeek());
            Test::moveNativePointer(session.window, QPoint(400, 200));
            QTRY_COMPARE(handle->opacity(), 0.);
        }
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                captureDirectory +
                (mode == ThemeStore::Dark ? "/playerbar-dark.png" : "/playerbar-light.png")));
        page = session.openOwnedPopup("playbackPageOwner");
        QVERIFY(page);
        QQuickItem *lyricBar = nullptr;
        QTRY_VERIFY((lyricBar = page->property("toastAnchor").value<QQuickItem *>()));
        auto *shellContent = visual(session.window->contentItem(), "shellContent");
        QVERIFY(shellContent);
        QTRY_VERIFY(page->property("opened").toBool());
        QTRY_VERIFY(!shellContent->isVisible());
        const int profileMs = qEnvironmentVariableIntValue("ECHO_MAIN_PLAYBACK_PROFILE_MS");
        if (profileMs > 0 && mode == ThemeStore::Light) {
            QTRY_VERIFY(page->property("opened").toBool());
            QJsonArray lines;
            for (int i = 0; i < 50; ++i)
                lines.append(
                    QJsonObject{{"text", QStringLiteral("音乐流动 Native lyric animation")},
                                {"start_ms", i * 4000},
                                {"duration_ms", 3500},
                                {"words", QJsonArray{}},
                                {"translation", "Translation"}});
            playback.lyricText()->replace({{"offset_ms", 0}, {"lines", lines}},
                                          "profile-render-only");
            playback.play();
            QTRY_VERIFY(playback.playing() && !playback.loading());
            QTest::qWait(500);
            const auto cpuStart = std::clock();
            QElapsedTimer wall;
            wall.start();
            QTest::qWait(profileMs);
            const qreal cpuMs = 1000. * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
            qInfo() << "Main production page: wall ms" << wall.elapsed() << "CPU ms" << cpuMs
                    << "CPU percent" << cpuMs / wall.elapsed() * 100;
            playback.pause();
            QTRY_VERIFY(!playback.playing());
        }
        auto *lyricCenter = visual(lyricBar, "playerbarCenterArea");
        auto *lyricSeek = visual(lyricBar, "playbackSeekSlider");
        QVERIFY(lyricCenter && lyricSeek);
        playback.play();
        QTRY_VERIFY(playback.playing() && !playback.loading());
        QQuickItem *lyricToggle = nullptr;
        QTRY_VERIFY((lyricToggle = visual(lyricBar, "playerbar-action-play-toggle")));
        QTRY_COMPARE(lyricToggle->property("glyph").toString(), QString("pause"));
        auto *lyricIcon = visual(visual(lyricToggle, "playPausePauseIcon"), "iconImage");
        QVERIFY(lyricIcon);
        QTRY_VERIFY(lyricIcon->property("source").toUrl().path().startsWith("/pause/"));
        QTRY_COMPARE(lyricIcon->property("status").toInt(), 1);
        QTest::qWait(250);
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                captureDirectory + (mode == ThemeStore::Dark ? "/lyric-playing-dark.png"
                                                             : "/lyric-playing-light.png")));
        playback.pause();
        QTRY_VERIFY(!playback.playing());
        QTRY_COMPARE(lyricToggle->property("glyph").toString(), QString("play"));
        QTRY_COMPARE(lyricSeek->width(), lyricBar->width());
        for (const int width : {700, 1150, 1600}) {
            session.window->resize(width, 750);
            QTRY_VERIFY(centered(lyricCenter, lyricBar));
            QTRY_COMPARE(lyricSeek->width(), lyricBar->width());
            auto *cover = visual(session.window->contentItem(), "playbackPageCover");
            auto *coverSide = visual(session.window->contentItem(), "playbackPageCoverSide");
            QVERIFY(cover && coverSide);
            if (width > 768) {
                QTRY_VERIFY(coverSide->width() <= 420.1);
                QTRY_VERIFY(cover->width() <= 380.1);
            } else {
                QTRY_COMPARE(cover->width(), 160.);
            }
        }
        session.window->resize(1150, 750);
        QTest::qWait(250);
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                captureDirectory + (mode == ThemeStore::Dark ? "/lyric-toolbar-dark.png"
                                                             : "/lyric-toolbar-light.png")));
        session.commands.copyCurrentSongInfo();
        QCOMPARE(QGuiApplication::clipboard()->text(), track.title + " - " + track.artist);
        QVERIFY(QMetaObject::invokeMethod(page, "close"));
        QTRY_VERIFY(page.isNull());
        QTRY_VERIFY(shellContent->isVisible());
        QTRY_VERIFY(bar->property("manageCapacity").toBool());
        QTRY_VERIFY(centered(center, bar));
    }
    for (const auto &warning : warnings)
        qWarning() << warning;
    QCOMPARE(warnings.size(), 0);
    session.root.reset();
    session.window = nullptr;
    QSignalSpy closed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}

void DesktopPagesTest::productionPlaybackPopoverSpacingAndEffectMarkers()
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
    session.theme.setFloatingFrosted(false);
    session.window->resize(1150, 1000);
    QVERIFY(Test::prepareNativeInput(session.window));
    QQuickItem anchor(session.window->contentItem());
    anchor.setSize(QSizeF(32, 32));
    anchor.setX(820);
    const auto capture = qEnvironmentVariable("ECHO_PLAYBACK_POPOVER_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    const QStringList cases{"volume", "speed", "quality", "play-mode", "effect", "sleep", "more"};
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1.0, 1.25}) {
            for (const bool above : {true, false}) {
                anchor.setY(above ? session.window->height() - 50 : 30);
                for (const auto &kind : cases) {
                    const auto owner = kind == "effect"  ? "playbackEffectPopupOwner"
                                       : kind == "sleep" ? "sleepTimerPopupOwner"
                                       : kind == "more"  ? "playerbarMorePopupOwner"
                                                         : "playbackOptionsPopupOwner";
                    QVariantMap properties{{"anchorItem", QVariant::fromValue(&anchor)},
                                           {"uiScale", scale}};
                    if (kind == "effect")
                        properties.insert("tab", 2);
                    if (kind != "effect" && kind != "sleep" && kind != "more")
                        properties.insert("kind", kind);
                    auto popup = session.openOwnedPopup(owner, properties);
                    QVERIFY(popup);
                    auto *sleep = popup.data();
                    QVERIFY(!popup->property("showArrow").toBool());
                    QTRY_VERIFY(popup->property("opened").toBool());
                    QTRY_VERIFY(popup->property("opacity").toReal() > .999);
                    auto *body = popup->property("background").value<QQuickItem *>();
                    auto *arrow = body ? visual(body, "anchoredPopupArrow") : nullptr;
                    QVERIFY(arrow && !arrow->isVisible());
                    QCOMPARE(popup->property("sideOffset").toReal(), 8.);
                    QCOMPARE(popup->property("placedAbove").toBool(), above);
                    const auto bodyRect = body->mapRectToScene(body->boundingRect());
                    const auto anchorRect = anchor.mapRectToScene(anchor.boundingRect());
                    const auto gap = above ? anchorRect.top() - bodyRect.bottom()
                                           : bodyRect.top() - anchorRect.bottom();
                    QVERIFY2(
                        qAbs(gap - 8 * scale) < .6,
                        qPrintable(QString("%1 gap=%2 scale=%3").arg(kind).arg(gap).arg(scale)));
                    const auto image = session.window->grabWindow();
                    QVERIFY(!image.isNull());
                    if (kind == "sleep") {
                        for (const auto minutes : {10, 20, 30, 60}) {
                            auto *button = visual(session.window->contentItem(),
                                                  QString("sleepDuration-%1").arg(minutes));
                            auto *copy = button ? visual(button, "sleepDurationContent") : nullptr;
                            QVERIFY(button && copy);
                            const auto middle = button->mapToScene(
                                QPointF(button->width() / 2, button->height() / 2));
                            const auto copyMiddle =
                                copy->mapToScene(QPointF(copy->width() / 2, copy->height() / 2));
                            QVERIFY(qAbs(copyMiddle.y() - middle.y()) < .5);
                            // Check painted number/unit pixels, not only the container.
                            const auto inkCenter = glyphCenterY(session.window, image, copy);
                            QVERIFY2(qAbs(inkCenter - middle.y()) < 2 * scale,
                                     qPrintable(QString("duration=%1 scale=%2 ink=%3 button=%4")
                                                    .arg(minutes)
                                                    .arg(scale)
                                                    .arg(inkCenter)
                                                    .arg(middle.y())));
                        }
                        for (const auto &selected : {"pause", "quit", "shutdown"}) {
                            auto *button = visual(session.window->contentItem(),
                                                  QString("sleepAction-%1").arg(selected));
                            QVERIFY(button);
                            QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
                            QTRY_COMPARE(sleep->property("selectedAction").toString(),
                                         QString(selected));
                            // Every selection changes the trailing check's visibility.
                            // Both text lines must retain the same scene-space left edge.
                            QTest::qWait(20);
                            qreal left = -1;
                            for (const auto &action : {"pause", "quit", "shutdown"}) {
                                auto *row = visual(session.window->contentItem(),
                                                   QString("sleepAction-%1").arg(action));
                                auto *copy = row ? visual(row, "sleepActionContent") : nullptr;
                                QVERIFY(copy);
                                const auto x = copy->mapToScene(QPointF()).x();
                                if (left < 0)
                                    left = x;
                                QVERIFY(qAbs(x - left) < .5);
                                for (auto *text : copy->childItems())
                                    if (text->property("text").isValid())
                                        QVERIFY(qAbs(text->mapToScene(QPointF()).x() - left) < .5);
                            }
                        }
                        sleep->setProperty("selectedAction", "pause");
                        QTest::qWait(20);
                    }
                    if (!capture.isEmpty())
                        QVERIFY(session.window->grabWindow().save(
                            capture + QString("/%1-%2-%3-%4.png")
                                          .arg(kind)
                                          .arg(theme == ThemeStore::Dark ? "dark" : "light")
                                          .arg(scale)
                                          .arg(above ? "above" : "below")));
                    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                    QTRY_VERIFY(popup.isNull());
                }
            }
        }
    }
    anchor.setY(session.window->height() - 50);
    auto effect = session.openOwnedPopup(
        "playbackEffectPopupOwner",
        {{"anchorItem", QVariant::fromValue(&anchor)}, {"uiScale", 1.}, {"tab", 2}});
    QVERIFY(effect);
    QTRY_VERIFY(effect->property("opened").toBool());
    auto *original = visual(session.window->contentItem(), "originalEffectCurrentDot");
    auto *side =
        visual(visual(session.window->contentItem(), "effectTab-2"), "effectSidebarCurrentDot");
    auto *tabs = visual(session.window->contentItem(), "effectLibraryTabs");
    QVERIFY(original && side && tabs);
    auto *engine = visual(visual(tabs, "tab-0"), "tabCurrentDot");
    auto *mine = visual(visual(tabs, "tab-1"), "tabCurrentDot");
    QVERIFY(engine && mine);
    QVERIFY(original->isVisible());
    QVERIFY(!side->isVisible() && !engine->isVisible() && !mine->isVisible());
    QCOMPARE(visual(original, "statusDotHalo")->size(), QSizeF(8, 8));
    const auto provider = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
    if (!provider.isEmpty()) {
        auto *audio = playback.audioControls();
        audio->providers()->importFile(QUrl::fromLocalFile(provider));
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && !audio->providerId().isEmpty(), 15000);
        const auto id = audio->providerPresets().first().toMap().value("id").toString();
        audio->selectProviderPreset(id);
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && audio->providerPresetId() == id, 15000);
        QTRY_VERIFY(side->isVisible() && engine->isVisible());
        QVERIFY(!original->isVisible() && !mine->isVisible());
        for (const auto tab : {2, 0}) {
            effect->setProperty("tab", tab);
            QTest::qWait(230);
            const auto halo = visual(side, "statusDotHalo");
            QVERIFY(halo && halo->isVisible());
            QCOMPARE(halo->size(), QSizeF(11, 11));
            QCOMPARE(side->property("color").value<QColor>(),
                     tab == 2 ? QColor("white") : session.theme.accent());
            const auto image = session.window->grabWindow();
            const auto ratio = image.width() / qreal(session.window->width());
            const auto haloPoint = side->mapToScene(QPointF(6, 2.5));
            const auto outside = side->mapToScene(QPointF(9, 2.5));
            const auto haloPixel = image.pixelColor((haloPoint * ratio).toPoint());
            const auto outsidePixel = image.pixelColor((outside * ratio).toPoint());
            QVERIFY(haloPixel != outsidePixel);
            if (!capture.isEmpty())
                QVERIFY(image.save(capture + QString("/current-engine-tab-%1.png").arg(tab)));
        }
        audio->selectProviderPreset("");
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending(), 15000);
        effect->setProperty("tab", 2);
        QTRY_VERIFY(original->isVisible());
        QVERIFY(!side->isVisible() && !engine->isVisible() && !mine->isVisible());
    }
    // A real, local PCM impulse exercises the other library and source
    // markers without fabricating an upstream response or using user files.
    const auto path = session.directory.filePath("ui-marker-impulse.wav");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream wave(&file);
    wave.setByteOrder(QDataStream::LittleEndian);
    wave.writeRawData("RIFF", 4);
    wave << quint32(36 + 128);
    wave.writeRawData("WAVEfmt ", 8);
    wave << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
         << quint16(2) << quint16(16);
    wave.writeRawData("data", 4);
    wave << quint32(128);
    for (int i = 0; i < 64; ++i)
        wave << qint16(i == 0 ? 32767 : 0);
    file.close();
    auto *audio = playback.audioControls();
    audio->registerImpulseResponse(path, QStringLiteral("本机脉冲"));
    audio->selectImpulseResponse(path);
    QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && audio->impulseEnabled(), 15000);
    effect->setProperty("libraryTab", 1);
    QTRY_VERIFY(side->isVisible() && mine->isVisible());
    QVERIFY(!engine->isVisible() && !original->isVisible());
    auto *local = visual(session.window->contentItem(), "myEffectSource-0");
    auto *sourceDot = local ? visual(local, "myEffectSourceCurrentDot") : nullptr;
    QVERIFY(sourceDot && sourceDot->isVisible());
    QCOMPARE(visual(sourceDot, "statusDotHalo")->size(), QSizeF(11, 11));
    // Browsing a different category/library must retain the applied marker.
    effect->setProperty("libraryTab", 0);
    QTRY_VERIFY(mine->isVisible() && !engine->isVisible());
    effect->setProperty("libraryTab", 1);
    auto *other = visual(session.window->contentItem(), "myEffectSource-1");
    QVERIFY(other);
    QVERIFY(QMetaObject::invokeMethod(other, "clicked"));
    QTRY_VERIFY(sourceDot->isVisible());
    if (!capture.isEmpty()) {
        QTest::qWait(230);
        QVERIFY(session.window->grabWindow().save(capture + "/current-local-other-category.png"));
    }
    audio->setImpulseEnabled(false);
    QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending(), 15000);
    QTRY_VERIFY(original->isVisible());
    QVERIFY(!side->isVisible() && !engine->isVisible() && !mine->isVisible() &&
            !sourceDot->isVisible());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionEmbeddedLyricsFromActualReader()
{
    Session session;
    session.theme.setMotionEnabled(false);
    session.theme.setFloatingFrosted(false);
    session.preferences.setValue("playback/volume", 0);
    PlaybackTrack track;
    track.id = "embedded-local";
    track.localPath =
        QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
            .absoluteFilePath("../../../crates/echo-audio/tests/fixtures/embedded-metadata.flac");
    track.localPath = QDir::cleanPath(track.localPath);
    QVERIFY(QFileInfo::exists(track.localPath));
    const auto key =
        "lyrics/sources/" +
        QString::fromLatin1(
            QCryptographicHash::hash(track.key().toUtf8(), QCryptographicHash::Sha256).toHex());
    session.preferences.setValue(key, "embedded");
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    playback.playTracks({track}, 0, "Embedded reader", "embedded-reader");
    QTRY_COMPARE_WITH_TIMEOUT(playback.lyricSource()->currentId(), QString("embedded"), 15000);
    QCOMPARE(playback.title(), QString("内嵌标题"));
    QCOMPARE(playback.artist(), QString("内嵌歌手"));
    QCOMPARE(playback.lyrics()->rowCount(), 1);
    QCOMPARE(playback.lyrics()->lineAt(0).value("lineText").toString(), QString("内嵌歌词"));
    QVERIFY(playback.lyrics()->scrollable());
    QVERIFY(playback.lyricSource()->manual());
    QTRY_VERIFY(!playback.cover().isEmpty());
    QVERIFY(QFileInfo(QUrl(playback.cover()).toLocalFile()).exists());
#ifdef Q_OS_MACOS
    QTRY_COMPARE_WITH_TIMEOUT(nativeSystemMediaInfo().value("title").toString(),
                              QString("内嵌标题"), 10000);
    QCOMPARE(nativeSystemMediaInfo().value("artist").toString(), QString("内嵌歌手"));
    QCOMPARE(nativeSystemMediaInfo().value("album").toString(), QString("内嵌专辑"));
    QTRY_VERIFY_WITH_TIMEOUT(nativeSystemMediaInfo().value("artwork").toBool(), 10000);
#endif
    // Neither reader snapshots nor temporary artwork paths enter retained queues.
    QCOMPARE(playback.queue()->at(0)->title, QString());
    QCOMPARE(playback.queue()->at(0)->cover, QString());
    // Freeze the short real fixture after its reader metadata is confirmed;
    // a repeating EOF must not replace lyrics while checking picker geometry.
    playback.pause();
    QTRY_VERIFY(!playback.playing() && !playback.loading());
    auto page = session.openOwnedPopup("playbackPageOwner");
    QVERIFY(page);
    QTRY_VERIFY(page->property("opened").toBool());
    auto *tools = visual(session.window->contentItem(), "playbackLyricTools");
    auto *source = visual(tools, "lyricSourceAction");
    QVERIFY(source);
    const auto top = source->mapToScene(QPointF{}).y();
    // Production tools reveal on content hover before accepting input.
    Test::moveNativePointer(
        session.window,
        source->mapToScene(QPointF(source->width() / 2, source->height() / 2)).toPoint());
    QTRY_VERIFY(source->isEnabled());
    QVERIFY(session.clickItem(source));
    QPointer<QObject> dialog;
    QTRY_VERIFY((dialog = page->findChild<QObject *>("lyricSourceDialog")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTRY_VERIFY(playback.lyricSource()->rowCount() > 0);
    auto *embedded = visual(session.window->contentItem(), "lyricSourceChoice-0");
    QVERIFY(embedded);
    QTRY_VERIFY(playback.lyricSource()->selectedIndex() >= 0);
    const auto capture = qEnvironmentVariable("ECHO_EMBEDDED_LYRIC_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        QTest::qWait(100);
        QVERIFY(session.window->grabWindow().save(capture + "/embedded-selection.png"));
    }
    playback.lyricSource()->select(0);
    playback.lyricSource()->applySelected();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(playback.lyricSource()->currentId(), QString("embedded"));
    playback.lyricText()->clear();
    QCOMPARE(source->mapToScene(QPointF{}).y(), top);
    playback.playIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(playback.lyricSource()->currentId(), QString("embedded"), 15000);
    playback.stop();
    QCOMPARE(playback.lyricSource()->rowCount(), 0);
    QSignalSpy closed(&playback, &PlaybackService::closed),
        apiClosed(&session.api, &ApiClient::closed);
    playback.shutdown();
    session.api.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
}

void DesktopPagesTest::productionLyricPreviewKeepsSymmetricInsets()
{
    Session session;
    session.theme.setMotionEnabled(false);
    session.theme.setFloatingFrosted(false);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1150, 850);
    // The style matrix owns the preview timeline; suppress the idle player's
    // asynchronous position notifications while keeping the real view/model.
    const QSignalBlocker previewClock(&playback);
    QQmlComponent component(&session.engine,
                            QUrl("qrc:/qt/qml/EchoMusic/Playback/LyricSourceDialog.qml"));
    QScopedPointer<QObject> popup(component.createWithInitialProperties(
        {{"parent", QVariant::fromValue(session.window->contentItem())},
         {"playback", QVariant::fromValue(&playback)}}));
    QVERIFY2(popup, qPrintable(component.errorString()));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    const auto capture = qEnvironmentVariable("ECHO_LYRIC_PREVIEW_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    // Canonical presentation data, without upstream fixtures or live requests.
    QJsonArray rows;
    for (int i = 0; i < 200; ++i)
        rows.append(
            QJsonObject{{"text", i % 2 ? QStringLiteral("中文 Latin 长句歌词换行 ").repeated(6)
                                       : QStringLiteral("周杰伦 - 龙战骑士")},
                        {"start_ms", i * 1000},
                        {"duration_ms", 1000},
                        {"words", QJsonArray{}},
                        {"translation", i % 2 ? "Translation preview" : ""},
                        {"romanization", i % 2 ? "Romanization preview" : ""}});
    PlaybackTrack presentation;
    presentation.localPath = "/presentation/lyric-preview";
    PlaybackServiceTestAccess::lyricMetadata(playback, presentation);
    playback.lyricSource()->updateEmbedded(
        presentation.key(), {}, {}, {{"scrollable", true}, {"offset_ms", 0}, {"lines", rows}});
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1., 1.25}) {
            popup->setProperty("uiScale", scale);
            for (const int width : {760, 680, 460}) {
                popup->setProperty("preferredWidth", width);
                QVERIFY(QMetaObject::invokeMethod(popup.data(), "open"));
                QTRY_VERIFY(popup->property("opened").toBool());
                QSignalSpy formFrames(session.window, &QQuickWindow::frameSwapped);
                session.window->update();
                QTRY_VERIFY(!formFrames.isEmpty());
                auto *form = visual(session.window->contentItem(), "lyricSourceSearchForm");
                auto *title = visual(form, "lyricSourceSearchTitle");
                auto *artist = visual(form, "lyricSourceSearchArtist");
                auto *search = visual(form, "lyricSourceSearchAction");
                auto *reset = visual(session.window->contentItem(), "lyricSourceRefreshAction");
                QVERIFY(form && title && artist && search && reset);
                QCOMPARE(title->height(), 36.);
                QCOMPARE(artist->height(), 36.);
                QCOMPARE(search->height(), 36.);
                QCOMPARE(reset->height(), 36.);
                QVERIFY(qAbs(title->width() / artist->width() - (width <= 480 ? 1. : 1.15)) < .02);
                // TextInput::boundingRect is its glyph/edit content, not
                // the field background and control rectangle being aligned.
                const auto titleBox =
                    title->mapRectToItem(form, QRectF(0, 0, title->width(), title->height()));
                const auto artistBox =
                    artist->mapRectToItem(form, QRectF(0, 0, artist->width(), artist->height()));
                const auto searchBox = search->mapRectToItem(form, search->boundingRect());
                qInfo() << "Lyric search form" << width << scale << form->size() << titleBox
                        << artistBox << searchBox;
                QVERIFY(qAbs(titleBox.bottom() - artistBox.bottom()) < .5);
                if (width <= 480)
                    QVERIFY(searchBox.top() >= titleBox.bottom() + 8);
                else
                    QVERIFY(qAbs(searchBox.bottom() - titleBox.bottom()) < .5);
                QVERIFY(searchBox.right() <= form->width() + .5);
                auto *model = playback.lyricSource()->preview();
                QVERIFY(model->replace({{"scrollable", true}, {"lines", rows}}));
                auto *view = visual(session.window->contentItem(), "lyricSourcePreview");
                QVERIFY(view);
                QTRY_COMPARE(view->property("count").toInt(), 200);
                QTRY_VERIFY(view->width() > 100 && view->height() > 40);
                auto *versions = visual(session.window->contentItem(), "lyricSourceCandidates");
                QVERIFY(versions);
                if (width > 680)
                    QTRY_VERIFY(qAbs(versions->width() / view->width() - .45 / .55) < .03);

                for (const int index : {0, 51, 198, 0}) {
                    model->setPosition(index * 1000);
                    QTRY_COMPARE(model->currentIndex(), index);
                    // This is a paint/inset test. Settle the native viewport at
                    // each distant row before probing it, independent of follow timing.
                    QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
                    const auto center =
                        view->metaObject()
                            ->enumerator(view->metaObject()->indexOfEnumerator("PositionMode"))
                            .keyToValue("Center");
                    QVERIFY(center >= 0);
                    QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtIndex",
                                                      Q_ARG(int, index), Q_ARG(int, center)));
                    QSignalSpy positioned(session.window, &QQuickWindow::frameSwapped);
                    session.window->update();
                    QTRY_VERIFY(!positioned.isEmpty());
                    QQuickItem *slot = nullptr;
                    const auto findCurrent = [&] {
                        const auto children =
                            view->property("contentItem").value<QQuickItem *>()->childItems();
                        for (auto *child : children)
                            if (child->property("current").isValid() &&
                                child->property("current").toBool() && child->isVisible() &&
                                child->y() + child->height() >
                                    view->property("contentY").toReal() &&
                                child->y() < view->property("contentY").toReal() + view->height())
                                return child;
                        return static_cast<QQuickItem *>(nullptr);
                    };
                    QTRY_VERIFY((slot = findCurrent()));
                    // Read the painted surface, not just the outer delegate's box.
                    auto *surface =
                        slot->property("color").isValid()
                            ? slot
                            : visual(slot, "lyricPreviewLine-" + QString::number(index));
                    QVERIFY(surface);
                    QTRY_VERIFY(qAbs(surface->mapToItem(view, {0, 0}).x() - 12.) < .01);
                    QTRY_VERIFY(qAbs(view->width() -
                                     surface->mapToItem(view, {surface->width(), 0}).x() - 12.) <
                                .01);
                    QTRY_VERIFY(surface->height() > 12);
                    QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                    session.window->update();
                    QTRY_VERIFY(!frames.isEmpty());
                    const auto image = session.window->grabWindow();
                    const auto ratio = image.width() / qreal(session.window->width());
                    const auto surfaceTop = surface->mapToItem(view, {0, 0}).y();
                    const auto visibleTop = qMax(0., surfaceTop);
                    const auto visibleBottom = qMin(view->height(), surfaceTop + surface->height());
                    if (visibleBottom - visibleTop <= 1) {
                        qInfo() << "Preview viewport probe" << int(mode) << scale << width << index
                                << model->currentIndex() << slot->property("index")
                                << slot->property("current") << view->size()
                                << view->property("contentY") << slot->position() << surfaceTop
                                << surface->height();
                        if (!capture.isEmpty())
                            QVERIFY(image.save(capture + "/failed-viewport.png"));
                    }
                    // Vertical clipping is valid in a scroll viewport. Probe its
                    // visible strip away from the surface's rounded corners.
                    QVERIFY(visibleBottom - visibleTop > 1);
                    const auto sampleY = (visibleTop + visibleBottom) / 2;
                    const auto sample = [&](qreal x) {
                        return image.pixelColor((view->mapToScene({x, sampleY}) * ratio).toPoint());
                    };
                    if (sample(8) == sample(32) && !capture.isEmpty()) {
                        qInfo() << "Preview pixel probe" << int(mode) << scale << width << index
                                << view->size() << surfaceTop << surface->height() << sampleY;
                        QVERIFY(image.save(capture + "/failed-pixel.png"));
                    }
                    QCOMPARE(sample(8), sample(view->width() - 8));
                    QVERIFY(sample(8) != sample(32));
                    QVERIFY(visualCount(view, "lyricPreviewSlot-" + QString::number(index)) <= 1);
                    if (!capture.isEmpty() && index == 0)
                        QVERIFY(image.save(
                            capture +
                            QString("/preview-%1-%2-%3.png").arg(int(mode)).arg(scale).arg(width)));
                }
                // Recycling after distant timeline jumps stays bounded.
                int mounted = 0;
                for (auto *child :
                     view->property("contentItem").value<QQuickItem *>()->childItems())
                    mounted += child->property("current").isValid();
                QVERIFY(mounted > 0 && mounted < 50);
                QVERIFY(QMetaObject::invokeMethod(popup.data(), "close"));
                QTRY_VERIFY(!popup->property("visible").toBool());
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionLyricKeywordSearchFromLiveNativeApi()
{
    if (!qEnvironmentVariableIsSet("ECHO_LYRIC_KEYWORD_LIVE"))
        QSKIP("Opt-in read-only lyric keyword API and native input lifecycle");
    Session session;
    session.theme.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1150, 850);
    QVERIFY(Test::prepareNativeInput(session.window));
    PlaybackTrack track;
    track.id = "keyword-original";
    track.hash = "105A2705BEEE36A8BED029A8E1A6311D";
    track.songId = 32042818;
    track.durationMs = 269000;
    track.title = QStringLiteral("龙战骑士");
    track.artist = QStringLiteral("周杰伦");
    PlaybackServiceTestAccess::lyricMetadata(playback, track);
    auto *sources = playback.lyricSource();
    QTRY_VERIFY_WITH_TIMEOUT(!sources->currentId().isEmpty() && !sources->loading() &&
                                 !sources->previewLoading(),
                             20000);
    const auto automatic = sources->currentId();
    QQmlComponent component(&session.engine,
                            QUrl("qrc:/qt/qml/EchoMusic/Playback/LyricSourceDialog.qml"));
    QScopedPointer<QObject> popup(component.createWithInitialProperties(
        {{"parent", QVariant::fromValue(session.window->contentItem())},
         {"playback", QVariant::fromValue(&playback)}}));
    QVERIFY2(popup, qPrintable(component.errorString()));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(QMetaObject::invokeMethod(popup.data(), "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(
        !sources->loading() && !sources->previewLoading() && sources->rowCount() > 1, 20000);
    auto *title = visual(session.window->contentItem(), "lyricSourceSearchTitle");
    auto *artist = visual(session.window->contentItem(), "lyricSourceSearchArtist");
    QVERIFY(title && artist);
    QCOMPARE(title->property("text").toString(), track.title);
    QCOMPARE(artist->property("text").toString(), track.artist);
    for (auto *field : {title, artist}) {
        auto *clear = visual(field, "clearTextAction");
        QVERIFY(clear && clear->isVisible());
        QTest::mouseClick(session.window, Qt::LeftButton, {},
                          clear->mapToScene({clear->width() / 2, clear->height() / 2}).toPoint());
        QTRY_VERIFY(field->property("text").toString().isEmpty());
    }
    QVERIFY(!sources->canSearch());
    auto *searchAction = visual(session.window->contentItem(), "lyricSourceSearchAction");
    QVERIFY(searchAction && !searchAction->isEnabled());
    const auto type = [&](QQuickItem *field, const QString &value) {
        field->forceActiveFocus(Qt::MouseFocusReason);
        QVERIFY(QMetaObject::invokeMethod(field, "selectAll"));
        QInputMethodEvent input;
        input.setCommitString(value);
        QCoreApplication::sendEvent(field, &input);
        QCOMPARE(field->property("text").toString(), value);
    };
    type(title, QStringLiteral(" 晴天 "));
    type(artist, QStringLiteral(" 周杰伦 "));
    QCOMPARE(sources->searchTitle(), QStringLiteral(" 晴天 "));
    QCOMPARE(sources->searchArtist(), QStringLiteral(" 周杰伦 "));
    QTest::keyClick(session.window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(
        !sources->loading() && !sources->previewLoading() && sources->rowCount() > 0, 20000);
    QVERIFY2(sources->errorMessage().isEmpty(), qPrintable(sources->errorMessage()));
    QCOMPARE(sources->currentId(), automatic);
    QVERIFY(!sources->index(0).data(Qt::UserRole + 11).toBool());
    QVERIFY(sources->selectedTitle().contains(QStringLiteral("晴天")));
    const auto chosen = sources->index(sources->selectedIndex()).data(Qt::UserRole + 1).toString();
    QVERIFY(chosen != automatic);
    const auto artifacts = qEnvironmentVariable("ECHO_LYRIC_SOURCE_CAPTURE_DIR");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(session.window->grabWindow().save(artifacts + "/keyword-results.png"));
    }
    sources->applySelected();
    QTRY_VERIFY(!popup->property("visible").toBool());
    QCOMPARE(sources->currentId(), chosen);
    QVERIFY(sources->manual());
    QVERIFY(QMetaObject::invokeMethod(popup.data(), "open"));
    QTRY_VERIFY_WITH_TIMEOUT(!sources->loading() && !sources->previewLoading() &&
                                 sources->selectedTitle().contains(QStringLiteral("晴天")),
                             20000);
    QCOMPARE(title->property("text").toString(), track.title);
    sources->restoreAuto();
    QTRY_VERIFY_WITH_TIMEOUT(!popup->property("visible").toBool(), 20000);
    QCOMPARE(sources->currentId(), automatic);
    QVERIFY(!sources->manual());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionLyricAutomaticFallbackFromLiveNativeApi()
{
    if (!qEnvironmentVariableIsSet("ECHO_LYRIC_FALLBACK_LIVE"))
        QSKIP("Opt-in real empty-hash automatic lyric fallback and dialog lifecycle");
    Session session;
    session.theme.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1150, 850);
    QVERIFY(Test::prepareNativeInput(session.window));
    PlaybackTrack track;
    track.id = "automatic-fallback-original";
    track.hash = "DEADBEEFDEADBEEFDEADBEEFDEADBEEF";
    track.durationMs = 269000;
    track.title = QStringLiteral("晴天");
    track.artist = QStringLiteral("周杰伦");
    PlaybackServiceTestAccess::lyricMetadata(playback, track);
    auto *sources = playback.lyricSource();
    QTRY_VERIFY_WITH_TIMEOUT(!sources->loading() && !sources->previewLoading() &&
                                 !sources->currentId().isEmpty(),
                             20000);
    QVERIFY2(sources->errorMessage().isEmpty(), qPrintable(sources->errorMessage()));
    QVERIFY(sources->selectedTitle().contains(QStringLiteral("晴天")));
    QVERIFY(playback.lyrics()->rowCount() > 20);
    QVERIFY(!sources->manual());
    QVERIFY(session.preferences.allKeys().filter("lyrics/sources/").isEmpty());
    const auto automatic = sources->currentId();
    QQmlComponent component(&session.engine,
                            QUrl("qrc:/qt/qml/EchoMusic/Playback/LyricSourceDialog.qml"));
    QScopedPointer<QObject> popup(component.createWithInitialProperties(
        {{"parent", QVariant::fromValue(session.window->contentItem())},
         {"playback", QVariant::fromValue(&playback)}}));
    QVERIFY2(popup, qPrintable(component.errorString()));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(QMetaObject::invokeMethod(popup.data(), "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(
        !sources->loading() && !sources->previewLoading() && sources->rowCount() > 1, 20000);
    QCOMPARE(sources->currentId(), automatic);
    QCOMPARE(sources->index(0).data(Qt::UserRole + 1).toString(), automatic);
    QVERIFY(sources->index(0).data(Qt::UserRole + 11).toBool());
    const auto artifacts = qEnvironmentVariable("ECHO_LYRIC_SOURCE_CAPTURE_DIR");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(session.window->grabWindow().save(artifacts + "/automatic-fallback.png"));
    }
    sources->select(1);
    QTRY_VERIFY_WITH_TIMEOUT(!sources->previewLoading() && sources->preview()->rowCount() > 0,
                             15000);
    const auto manual = sources->index(1).data(Qt::UserRole + 1).toString();
    sources->applySelected();
    QTRY_VERIFY(!popup->property("visible").toBool());
    QCOMPARE(sources->currentId(), manual);
    QVERIFY(sources->manual());
    PlaybackServiceTestAccess::lyricMetadata(playback, track);
    QTRY_VERIFY_WITH_TIMEOUT(
        !sources->loading() && !sources->previewLoading() && sources->currentId() == manual, 20000);
    QVERIFY(QMetaObject::invokeMethod(popup.data(), "open"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !sources->loading() && !sources->previewLoading() && sources->rowCount() > 1, 20000);
    QCOMPARE(sources->index(sources->selectedIndex()).data(Qt::UserRole + 1).toString(), manual);
    sources->restoreAuto();
    QTRY_VERIFY_WITH_TIMEOUT(!popup->property("visible").toBool(), 20000);
    QCOMPARE(sources->currentId(), automatic);
    QVERIFY(!sources->manual());
    QVERIFY(session.preferences.allKeys().filter("lyrics/sources/").isEmpty());
    QCOMPARE(warnings.size(), 0);
}
