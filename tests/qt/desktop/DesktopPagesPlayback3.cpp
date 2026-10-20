#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::productionLyricSelectionFromLiveNativeApi()
{
    if (!qEnvironmentVariableIsSet("ECHO_LYRIC_SOURCE_LIVE_TEST"))
        QSKIP("Explicit read-only live lyric API verification");
    Session session;
    session.theme.setMotionEnabled(false);
    QVERIFY(session.api.setNetworkTimeout(10));
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    PlaybackTrack track;
    track.id = "lyric-read-only-live";
    track.hash = "105A2705BEEE36A8BED029A8E1A6311D";
    track.songId = 32042818;
    track.durationMs = 269000;
    track.title = QStringLiteral("龙战骑士");
    track.artist = QStringLiteral("周杰伦");
    PlaybackServiceTestAccess::lyricMetadata(playback, track);
    auto *sources = playback.lyricSource();
    QTRY_VERIFY_WITH_TIMEOUT(!sources->currentId().isEmpty() && playback.lyrics()->rowCount() > 20,
                             20000);
    const auto automatic = sources->currentId();
    auto *page = session.root->findChild<QObject *>("playbackPage");
    QVERIFY(page);
    QVERIFY(QMetaObject::invokeMethod(page, "open"));
    QTRY_VERIFY(page->property("opened").toBool());
    auto *dialog = session.root->findChild<QObject *>("lyricSourceDialog");
    auto *choose = visual(session.window->contentItem(), "lyricSourceAction");
    QVERIFY(dialog && choose);
    const auto click = [&](QQuickItem *item) {
        const auto position = item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
        Test::moveNativePointer(session.window, position);
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, position);
    };
    Test::moveNativePointer(session.window, choose->mapToScene({18, 18}).toPoint());
    QTRY_VERIFY(choose->isEnabled());
    click(choose);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(
        !sources->loading() && !sources->previewLoading() && sources->rowCount() > 1, 20000);
    QVERIFY2(sources->errorMessage().isEmpty(), qPrintable(sources->errorMessage()));
    QCOMPARE(sources->currentId(), automatic);
    QCOMPARE(sources->index(0).data(Qt::UserRole + 1).toString(), automatic);
    auto *candidates = visual(session.window->contentItem(), "lyricSourceCandidates");
    QVERIFY(candidates);
    QTRY_COMPARE(candidates->property("count").toInt(), sources->rowCount());
    auto *choice = visual(candidates, "lyricSourceChoice-0");
    QVERIFY(choice);
    QCOMPARE(choice->mapToItem(candidates, QPointF{}).x(), 8.);
    int instantiated = 0;
    for (auto *item : candidates->findChildren<QQuickItem *>())
        if (item->objectName().startsWith("lyricSourceCandidate-"))
            ++instantiated;
    QVERIFY(instantiated < sources->rowCount());
    const auto capture = qEnvironmentVariable("ECHO_LYRIC_SOURCE_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1., 1.25}) {
            dialog->setProperty("uiScale", scale);
            QTest::qWait(100);
            qInfo() << "Lyric dialog geometry" << dialog->property("height") << "scale" << scale
                    << "list" << candidates->size() << "panel" << candidates->parentItem()->size();
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(capture + "/geometry.png"));
            QVERIFY(candidates->height() > 100);
            auto *apply = visual(session.window->contentItem(), "lyricSourceApplyAction");
            QVERIFY(apply && apply->isVisible());
            QVERIFY(apply->mapToScene({0, apply->height()}).y() < session.window->height());
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/sources-%1-%2.png")
                                  .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                  .arg(scale)));
        }
    }
    dialog->setProperty("uiScale", 1.);
    session.window->resize(700, 800);
    QTRY_VERIFY(candidates->width() > 500 && candidates->height() > 100);
    if (!capture.isEmpty())
        QVERIFY(session.window->grabWindow().save(capture + "/sources-narrow.png"));
    session.window->resize(1150, 800);
    QTRY_VERIFY(candidates->width() < 400);
    int staticRow = -1;
    for (int row = 0; row < sources->rowCount(); ++row)
        if (!sources->index(row).data(Qt::UserRole + 9).toBool()) {
            staticRow = row;
            break;
        }
    QVERIFY(staticRow >= 0);
    sources->select(staticRow);
    QTRY_VERIFY_WITH_TIMEOUT(!sources->previewLoading() && sources->preview()->rowCount() > 20,
                             15000);
    QVERIFY(!sources->preview()->scrollable());
    QCOMPARE(sources->currentId(), automatic);
    QVERIFY(playback.lyrics()->scrollable());
    click(visual(session.window->contentItem(), "lyricSourceApplyAction"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(!playback.lyrics()->scrollable());
    auto *staticView = visual(session.window->contentItem(), "playbackLyricList");
    auto *resume = session.root->findChild<QObject *>("lyricFollowResume");
    QVERIFY(staticView && resume);
    staticView->setProperty("following", false);
    QVERIFY(QMetaObject::invokeMethod(staticView, "movementEnded"));
    QVERIFY(!resume->property("running").toBool());
    QVERIFY(QMetaObject::invokeMethod(resume, "triggered"));
    QVERIFY(!staticView->property("following").toBool());
    QVERIFY(sources->manual());
    const auto selected = sources->currentId();
    Test::moveNativePointer(session.window, choose->mapToScene({18, 18}).toPoint());
    QTRY_VERIFY(choose->isEnabled());
    click(choose);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(sources->currentId(), selected);
    QTRY_VERIFY(sources->preview()->rowCount() > 20);
    click(visual(session.window->contentItem(), "lyricSourceRestoreAction"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 20000);
    QCOMPARE(sources->currentId(), automatic);
    QVERIFY(playback.lyrics()->scrollable());
    QVERIFY(!sources->manual());
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(!page->property("visible").toBool());
    for (const auto &warning : warnings)
        qWarning() << warning;
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::realPlaybackSurfacesAndBoundedFiftyThousandQueue()
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
    PlaybackTracks tracks;
    for (int i = 0; i < 50000; ++i) {
        PlaybackTrack track;
        track.id = QString::number(i);
        track.localPath = QString("/ui-test/%1.wav").arg(i);
        track.title = QStringLiteral("中文 Title %1").arg(i);
        track.artist = "Artist 歌手";
        tracks.append(track);
    }
    playback.appendTracks(tracks);
    playback.queue()->setCurrent(34567);
    // Offscreen does not deliver native enter/hover activation. Validate
    // hover on Cocoa/Windows/X11/Wayland, alongside the same formal UI case.
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        QVERIFY(Test::prepareNativeInput(session.window));
        QPointer<QQuickItem> volumeAction;
        const auto hoverVolume = [&] {
            volumeAction = visual(session.window->contentItem(), "playerbar-action-volume");
            if (!volumeAction || volumeAction->width() <= 0 || !volumeAction->isVisible())
                return false;
            // Cocoa suppresses a native move when the global cursor is
            // already there after a previous test window. Enter from outside.
            QTest::mouseMove(session.window, QPoint(400, 200));
            QTest::mouseMove(session.window, volumeAction
                                                 ->mapToScene(QPointF(volumeAction->width() / 2,
                                                                      volumeAction->height() / 2))
                                                 .toPoint());
            return volumeAction && volumeAction->property("hovered").toBool();
        };
        QTRY_VERIFY(hoverVolume());
        auto *actionBackground = volumeAction->property("background").value<QQuickItem *>();
        QVERIFY(actionBackground);
        QCOMPARE(actionBackground->property("color").value<QColor>().alpha(), 0);
        QTest::mouseMove(session.window, QPoint(400, 200));
    }
    QPointer<QObject> drawer, effect, sleep, options, page;
    for (const auto *name : {"playbackQueueDrawer", "playbackEffectPopup", "sleepTimerPopup",
                             "playbackOptionsPopup", "playbackPage"})
        QVERIFY(!session.root->findChild<QObject *>(name));
    // Custom artwork chooses white content text while light floating panels
    // must retain their independent foreground, including nested controls.
    const auto captureDirectory = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QVERIFY(Test::prepareNativeInput(session.window));
    }
    session.theme.setMode(ThemeStore::Light);
    QImage artwork(64, 64, QImage::Format_RGB32);
    artwork.fill(QColor("#152c46"));
    const auto artworkFile = session.directory.filePath("playback-artwork.png");
    QVERIFY(artwork.save(artworkFile));
    session.theme.importBackground(QUrl::fromLocalFile(artworkFile));
    QTRY_VERIFY(!session.theme.imageImporting());
    session.theme.setBackgroundText(Qt::white);
    QVERIFY(session.theme.applyBackground());
    QCOMPARE(session.theme.text(), QColor(Qt::white));
    QVERIFY(session.theme.floatingText().lightnessF() < .3);

    drawer = session.openOwnedPopup("playbackQueueDrawerOwner");
    QVERIFY(drawer);
    QQuickItem *list = nullptr;
    QTRY_VERIFY((list = visual(session.window->contentItem(), "playbackQueueList")));
    QTRY_VERIFY(!visualAll(session.window->contentItem(), "queueRow").isEmpty());
    const auto rows = visualAll(session.window->contentItem(), "queueRow");
    QVERIFY(rows.size() < 80);
    QCOMPARE(list->property("count").toInt(), 50000);
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    playback.libraryActions()->prepareViewedQueue();
    auto *addDialog = session.root->findChild<QObject *>("addToPlaylistDialog");
    QVERIFY(addDialog);
    QTRY_VERIFY(addDialog->property("opened").toBool());
    QVERIFY(visual(session.window->contentItem(), "playlistAddTargets"));
    QVERIFY(QMetaObject::invokeMethod(addDialog, "close"));
    effect = session.openOwnedPopup("playbackEffectPopupOwner");
    QVERIFY(effect);
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (int tab : {0, 1, 2, 3}) {
            effect->setProperty("tab", tab);
            QTest::qWait(100);
            if (tab != 3) {
                auto *heading = visual(session.window->contentItem(), "playbackEffectHeading");
                QVERIFY(heading);
                QCOMPARE(heading->property("color").value<QColor>(), session.theme.floatingText());
            }
            auto *sidebar = visual(session.window->contentItem(), "effectSidebar");
            auto *navigation =
                visual(session.window->contentItem(), "effectTab-" + QString::number(tab));
            QVERIFY(sidebar && navigation);
            QCOMPARE(sidebar->width(), 88.);
            QCOMPARE(navigation->height(), 32.);
            if (tab == 0) {
                auto *preset = visual(session.window->contentItem(), "songEffect-none");
                QVERIFY(preset);
                QCOMPARE(preset->height(), 38.);
            }
            if (tab == 2) {
                auto *libraryTabs = visual(session.window->contentItem(), "effectLibraryTabs");
                QVERIFY(libraryTabs);
                QCOMPARE(libraryTabs->height(), 32.);
                effect->setProperty("libraryTab", 1);
                QTRY_VERIFY(visual(session.window->contentItem(), "myEffectSource-3"));
            }
            // Capture after the shared tab-selection transition settles.
            QTest::qWait(300);
            if (!captureDirectory.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captureDirectory + QString("/effect-%1-%2.png")
                                           .arg(tab)
                                           .arg(mode == ThemeStore::Dark ? "dark" : "light")));
        }
    }
    effect->setProperty("tab", 1);
    session.theme.setMode(ThemeStore::Light);
    auto *band = visual(session.window->contentItem(), "equalizerBand-0");
    QVERIFY(band && band->height() > 20);
    playback.audioControls()->applyEqualizerPreset(1);
    QCOMPARE(playback.audioControls()->gains().first().toDouble(), 3.);
    const auto dspLibrary = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
    if (!dspLibrary.isEmpty()) {
        auto *audio = playback.audioControls();
        audio->providers()->importFile(QUrl::fromLocalFile(dspLibrary));
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && !audio->providerId().isEmpty(), 15000);
        effect->setProperty("tab", 1);
        QTRY_VERIFY(!band->isEnabled());
        effect->setProperty("tab", 2);
        effect->setProperty("libraryTab", 0);
        QTest::qWait(300);
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(captureDirectory + "/effect-provider.png"));
        auto *presets = visual(session.window->contentItem(), "dspPresetGrid");
        QVERIFY(presets);
        int presetIndex = 0;
        const auto availablePresets = audio->providerPresets();
        while (presetIndex < availablePresets.size() &&
               availablePresets[presetIndex].toMap().value("id") != "kugou-vinyl")
            ++presetIndex;
        QVERIFY(presetIndex < availablePresets.size());
        QVERIFY(QMetaObject::invokeMethod(presets, "positionViewAtIndex", Q_ARG(int, presetIndex),
                                          Q_ARG(int, 4)));
        QQuickItem *edit = nullptr;
        QTRY_VERIFY((edit = visual(session.window->contentItem(),
                                   QStringLiteral("dspPresetSettings-kugou-vinyl"))) &&
                    edit->isVisible());
        QCOMPARE(edit->size(), QSizeF(28, 28));
        QVERIFY(qAbs(edit->mapToItem(edit->parentItem(), QPointF(0, edit->height() / 2)).y() -
                     edit->parentItem()->height() / 2) < .5);
        QVERIFY(QMetaObject::invokeMethod(edit, "clicked"));
        QQuickItem *editor = nullptr;
        QTRY_VERIFY((editor = visual(session.window->contentItem(), "dspPresetControlsPanel")));
        auto *sidebar = visual(session.window->contentItem(), "effectSidebar");
        QVERIFY(sidebar);
        QTRY_VERIFY(qAbs(editor->mapToScene({0, 0}).x() -
                         sidebar->mapToScene({sidebar->width(), 0}).x()) < 0.5);
        QTRY_VERIFY(qAbs(editor->mapToScene({0, 0}).y() - sidebar->mapToScene({0, 0}).y()) < 0.5);
        const auto controls = audio->providerControls();
        QVERIFY(!controls.isEmpty());
        QQuickItem *control = nullptr;
        QTRY_VERIFY(
            (control = visual(editor, "dspProviderControl-" +
                                          controls.first().toMap().value("id").toString())));
        QTRY_VERIFY(qAbs(control->mapToItem(editor, {0, 0}).x() - 14) < 0.5);
        QTest::qWait(100);
        auto *controlLabel = visual(control, "dspControlLabel");
        auto *controlValue = visual(control, "dspControlValue");
        QVERIFY(controlLabel && controlValue);
        const auto controlCenter =
            controlLabel->mapToScene(QPointF(0, controlLabel->height() / 2)).y();
        QVERIFY(qAbs(controlCenter -
                     controlValue->mapToScene(QPointF(0, controlValue->height() / 2)).y()) < .5);
        const auto controlImage = session.window->grabWindow();
        QVERIFY(qAbs(glyphCenterY(session.window, controlImage, controlLabel) - controlCenter) <
                1.5);
        QVERIFY(qAbs(glyphCenterY(session.window, controlImage, controlValue) - controlCenter) <
                1.5);
        QQuickItem *backIcon = nullptr;
        QTRY_VERIFY((backIcon = visual(editor, "iconImage")));
        QTRY_COMPARE(backIcon->property("status").toInt(), 1);
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(captureDirectory +
                                                      "/effect-provider-editor.png"));
        auto *back = visual(session.window->contentItem(), QStringLiteral("dspPresetEditorBack"));
        QVERIFY(back);
        QVERIFY(QMetaObject::invokeMethod(back, "clicked"));
        audio->deactivateProvider();
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && audio->providerId().isEmpty(), 15000);
        QQuickItem *installed = nullptr;
        QTRY_VERIFY(
            (installed = visual(session.window->contentItem(), "dspInstalledProviderCard")) &&
            installed->isVisible());
        QTest::qWait(200);
        if (!captureDirectory.isEmpty())
            QVERIFY(session.window->grabWindow().save(captureDirectory +
                                                      "/effect-installed-provider.png"));
        auto *activate = visual(installed, "activateInstalledDspProvider");
        QVERIFY(activate);
        QVERIFY(QMetaObject::invokeMethod(activate, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!audio->effectPending() && !audio->providerId().isEmpty(), 15000);
    }
    QVERIFY(QMetaObject::invokeMethod(effect, "close"));
    sleep = session.openOwnedPopup("sleepTimerPopupOwner");
    QVERIFY(sleep);
    QTest::qWait(100);
    QVERIFY(sleep->property("height").toDouble() > 180);
    auto *sleepHeading = visualText(session.window->contentItem(), QStringLiteral("定时关闭"));
    QVERIFY(sleepHeading);
    QCOMPARE(sleepHeading->property("color").value<QColor>(), session.theme.floatingText());
    if (!captureDirectory.isEmpty()) {
        session.theme.setFloatingFrosted(true);
        QSignalSpy frame(session.window, &QQuickWindow::frameSwapped);
        session.window->update();
        QTRY_VERIFY(!frame.isEmpty());
        QTest::qWait(100);
        QVERIFY(session.window->grabWindow().save(captureDirectory +
                                                  "/custom-background-sleep-popup.png"));
    }
    QVERIFY(QMetaObject::invokeMethod(sleep, "close"));
    for (const auto &kind : {"volume", "speed", "play-mode", "quality"}) {
        options = session.openOwnedPopup("playbackOptionsPopupOwner", {{"kind", kind}});
        QVERIFY(options);
        QTest::qWait(70);
        if (QString(kind) == "volume") {
            auto *volume = visual(session.window->contentItem(), "playbackVolumeSlider");
            QVERIFY(volume);
            auto *colors = volume->property("colors").value<QObject *>();
            QVERIFY(colors);
            QCOMPARE(colors->property("text").value<QColor>(), session.theme.floatingText());
            if (!captureDirectory.isEmpty()) {
                const auto originalMode = session.theme.mode();
                for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                    session.theme.setMode(mode);
                    QTest::qWait(100);
                    QVERIFY(session.window->grabWindow().save(
                        captureDirectory + (mode == ThemeStore::Dark ? "/volume-popup-dark.png"
                                                                     : "/volume-popup-light.png")));
                }
                session.theme.setMode(originalMode);
            }
        }
        QVERIFY(QMetaObject::invokeMethod(options, "close"));
        QTRY_VERIFY(options.isNull());
    }
    page = session.openOwnedPopup("playbackPageOwner");
    QVERIFY(page);
    QTest::qWait(100);
    QVERIFY(visual(session.window->contentItem(), "playbackLyricList"));
    auto *sourceAction = visual(session.window->contentItem(), "lyricSourceAction");
    QVERIFY(sourceAction);
    QVERIFY(session.clickItem(sourceAction));
    QPointer<QObject> sourceDialog;
    QTRY_VERIFY((sourceDialog = page->findChild<QObject *>("lyricSourceDialog")));
    QTest::qWait(80);
    QVERIFY(QMetaObject::invokeMethod(sourceDialog, "close"));
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    session.router.navigate("settings");
    auto *settings = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(settings);
    QTRY_VERIFY(settings->property("opened").toBool());
    auto *catalog = settings->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    catalog->select("playback");
    QTRY_VERIFY(visual(session.window->contentItem(), "trackTransitionMode"));
    QTRY_VERIFY(visual(session.window->contentItem(), "normalizationReferenceLufs"));
    playback.transition()->setMode("fade");
    QTest::qWait(80);
    QVERIFY(QMetaObject::invokeMethod(settings, "close"));
    for (const auto &warning : warnings)
        qWarning() << warning;
    QCOMPARE(warnings.size(), 0);
    session.root.reset();
    session.window = nullptr;
    QSignalSpy closed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}
