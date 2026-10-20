#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::audibleTransitionPublishesOneToastThroughShell()
{
    Session session;
    const auto path = session.directory.filePath("transition-a.wav");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr quint32 bytes = 48000 * 10 * 2;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    for (int i = 0; i < 48000 * 10; ++i)
        stream << qint16(std::sin(i * 2. * 3.141592653589793 * 220 / 48000) * 1000);
    file.close();
    const auto second = session.directory.filePath("transition-b.wav");
    QVERIFY(QFile::copy(path, second));
    PlaybackTracks tracks;
    for (const auto &source : {path, second}) {
        PlaybackTrack track;
        track.id = source;
        track.localPath = source;
        track.title = "Native transition";
        track.durationMs = 10000;
        tracks.append(track);
    }
    PlaybackService playback(session.api, session.preferences);
    playback.setVolume(0);
    QVERIFY(session.create(&playback));
    session.theme.setMotionEnabled(false);
    auto *toasts = session.commands.toasts();
    playback.transition()->setMode("gapless");
    QVERIFY(!toasts->visible());
    playback.playTracks(tracks, 0, "Toast regression", "native-toast");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(TrackTransitionControllerTestAccess::prepared(*playback.transition()),
                             15000);
    QVERIFY(!toasts->visible());
    // Only the actual automatic native boundary publishes an audible-mode
    // notice. A manual next command deliberately bypasses the prepared mix.
    QTRY_COMPARE_WITH_TIMEOUT(playback.currentTrack().key(), tracks[1].key(), 15000);
    QTRY_VERIFY(toasts->visible());
    QCOMPARE(toasts->message(), QString("无缝"));
    QCOMPARE(toasts->tone(), ToastService::Info);
    QCOMPARE(toasts->repetitions(), 1);
    auto *message = visual(session.window->contentItem(), "toastMessage");
    auto *card = visual(session.window->contentItem(), "toastCard");
    QVERIFY(message && card);
    QTRY_VERIFY(card->isVisible());
    QCOMPARE(message->property("text").toString(), QString("无缝"));
    QTRY_VERIFY_WITH_TIMEOUT(!toasts->visible(), 5000);
    session.root.reset();
    session.window = nullptr;
    QSignalSpy closed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}

void DesktopPagesTest::nativeOutputDeviceSelectHasOneDefaultChoice()
{
    Session session;
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    session.theme.setFloatingFrosted(false);
    session.router.navigate("settings");
    auto *settings = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(settings);
    QTRY_VERIFY(settings->property("opened").toBool());
    auto *catalog = settings->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    catalog->select("audioDevice");
    QQuickItem *select = nullptr;
    QTRY_VERIFY((select = visual(session.window->contentItem(), "audioOutputDevice")));
    // This case uses real native enumeration, not a fabricated API sample.
    QTRY_VERIFY_WITH_TIMEOUT(!playback.audioControls()->devices().isEmpty(), 15000);
    const auto capture = qEnvironmentVariable("ECHO_AUDIO_DEVICE_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        QVERIFY(!select->property("popup").value<QObject *>());
        QVERIFY(session.clickItem(select));
        QPointer<QObject> popup;
        QTRY_VERIFY((popup = select->property("popup").value<QObject *>()));
        QTRY_VERIFY(popup->property("opened").toBool());
        const auto names = select->property("model").value<QJSValue>().toVariant().toStringList();
        QCOMPARE(names.count(QStringLiteral("系统默认")), 1);
        QVERIFY(!names.contains("Autoselect device"));
        QVERIFY(visualText(session.window->contentItem(), QStringLiteral("系统默认")));
        QTest::qWait(150);
        for (const auto &device : playback.audioControls()->devices()) {
            const auto name = device.toMap().value("name").toString();
            QVERIFY(!name.isEmpty() && name != "auto" && name != "default" && name != "null");
        }
        if (!capture.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                capture + (mode == ThemeStore::Dark ? "/output-dark.png" : "/output-light.png")));
        QVERIFY(QMetaObject::invokeMethod(popup.data(), "close"));
        QTRY_VERIFY(popup.isNull());
    }
    QCOMPARE(warnings.size(), 0);
    QVERIFY(QMetaObject::invokeMethod(settings, "close"));
    session.root.reset();
    session.window = nullptr;
    QSignalSpy closed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}

void DesktopPagesTest::committedTrackClimaxFromLiveNativeApi()
{
    if (!qEnvironmentVariableIsSet("ECHO_CLIMAX_LIVE_TEST"))
        QSKIP("Explicit live read-only CDN test");
    Session session;
    session.preferences.setValue("playback/volume", 0);
    QVERIFY(session.api.setNetworkTimeout(10));
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1150, 750);
    QVERIFY(Test::prepareNativeInput(session.window));
    const auto path = session.directory.filePath("climax-diagnostic-silence.wav");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    const quint32 bytes = 48000 * 4 * 240;
    out.writeRawData("RIFF", 4);
    out << quint32(36 + bytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
        << quint16(4) << quint16(16);
    out.writeRawData("data", 4);
    out << bytes;
    QVERIFY(file.resize(44 + bytes));
    file.close();
    PlaybackTrack song;
    song.id = "climax-native-diagnostic";
    song.hash = "105A2705BEEE36A8BED029A8E1A6311D";
    song.localPath = path;
    song.title = QStringLiteral("真实高潮元数据 · 诊断静音音轨");
    song.durationMs = 240000;
    playback.playTracks({song}, 0, "Climax verification", "climax-verification");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing() && !playback.loading(), 15000);
    QTRY_COMPARE_WITH_TIMEOUT(playback.climax()->rowCount(), 1, 15000);
    QCOMPARE(
        playback.climax()->data(playback.climax()->index(0), PlaybackClimaxModel::Start).toDouble(),
        81000. / 240000);
    QCOMPARE(
        playback.climax()->data(playback.climax()->index(0), PlaybackClimaxModel::End).toDouble(),
        117100. / 240000);
    auto *page = session.root->findChild<QObject *>("playbackPage");
    QVERIFY(page);
    const auto capture = qEnvironmentVariable("ECHO_CLIMAX_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const bool playingPage : {false, true}) {
            if (playingPage) {
                QVERIFY(QMetaObject::invokeMethod(page, "open"));
                QTRY_VERIFY(page->property("opened").toBool());
            }
            auto *bar = playingPage ? page->property("toastAnchor").value<QQuickItem *>()
                                    : visual(session.window->contentItem(), "playerbar");
            QVERIFY(bar);
            // PlayerBar owns both compact and edge-to-edge progress rows;
            // inspect the active row rather than its hidden sibling.
            QQuickItem *slider = nullptr;
            for (auto *candidate : visualAll(bar, "playbackSeekSlider")) {
                if (candidate->isVisible()) {
                    slider = candidate;
                    break;
                }
            }
            QVERIFY(slider);
            QQuickItem *tick = nullptr;
            QTRY_VERIFY((tick = visual(slider, "progressClimaxStart-0")));
            auto *end = visual(slider, "progressClimaxEnd-0");
            QVERIFY(end && end->isVisible());
            const auto track = slider->property("background").value<QQuickItem *>();
            QVERIFY(track);
            QVERIFY(qAbs(tick->mapToItem(track, {1, 3}).x() / track->width() - 81000. / 240000) <
                    .0001);
            QVERIFY(qAbs(tick->property("color").value<QColor>().alphaF() -
                         (playingPage                 ? .78
                          : theme == ThemeStore::Dark ? .65
                                                      : .8)) < .005);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture +
                    QString("/%1-%2.png").arg(playingPage ? "page" : "bar").arg(int(theme))));
            if (playingPage) {
                QVERIFY(QMetaObject::invokeMethod(page, "close"));
                QTRY_VERIFY(!page->property("visible").toBool());
            }
        }
    }
    // Stop invalidates the model immediately; no old marker survives navigation/replay.
    playback.stop();
    QCOMPARE(playback.climax()->rowCount(), 0);
    QSignalSpy closed(&playback, &PlaybackService::closed),
        apiClosed(&session.api, &ApiClient::closed);
    playback.shutdown();
    session.api.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(apiClosed.size(), 1, 15000);
}

void DesktopPagesTest::productionEffectPanelAlignmentAndCompactContentTabs()
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
    session.window->resize(1150, 850);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(!session.root->findChild<QObject *>("playbackEffectPopup"));
    const auto provider = qEnvironmentVariable("ECHO_DSP_TEST_PROVIDER");
    if (!provider.isEmpty()) {
        playback.audioControls()->providers()->importFile(QUrl::fromLocalFile(provider));
        QTRY_VERIFY_WITH_TIMEOUT(!playback.audioControls()->effectPending() &&
                                     !playback.audioControls()->providerId().isEmpty(),
                                 15000);
    }
    const auto capture = qEnvironmentVariable("ECHO_EFFECT_ALIGNMENT_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    const auto centerY = [](QQuickItem *item) {
        return item->mapToScene(QPointF(0, item->height() / 2)).y();
    };
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1.0, 1.25}) {
            session.shell.resetZoom();
            if (scale > 1)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            auto popup = session.openPlayerPopup("effect", "playbackEffectPopup");
            QVERIFY(popup);
            for (int tab = 0; tab < 4; ++tab) {
                popup->setProperty("tab", tab);
                QTest::qWait(150);
                const auto image = session.window->grabWindow();
                QVERIFY(!image.isNull());
                auto *navigation =
                    visual(session.window->contentItem(), "effectTab-" + QString::number(tab));
                QVERIFY(navigation);
                QCOMPARE(navigation->height(), 32.);
                QCOMPARE(navigation->property("font").value<QFont>().pixelSize(), 13);
                if (tab != 3) {
                    auto *heading = visual(session.window->contentItem(), "playbackEffectHeading");
                    QVERIFY(heading);
                    QVERIFY(qAbs(glyphCenterY(session.window, image, heading) - centerY(heading)) <
                            1.5 * scale);
                }
                if (tab == 0) {
                    auto *preset = visual(session.window->contentItem(), "songEffect-none");
                    QVERIFY(preset);
                    auto *label = visual(preset, "effectPresetLabel");
                    QVERIFY(label);
                    QVERIFY(qAbs(glyphCenterY(session.window, image, label) - centerY(preset)) <
                            1.5 * scale);
                }
                if (tab == 2) {
                    auto *tabs = visual(session.window->contentItem(), "effectLibraryTabs");
                    QVERIFY(tabs);
                    QCOMPARE(tabs->height(), 32.);
                    QVERIFY(tabs->property("compact").toBool());
                    auto *button = visual(tabs, "tab-0");
                    auto *text = visualText(button, QStringLiteral("引擎预设"));
                    QVERIFY(button && text);
                    QCOMPARE(text->property("font").value<QFont>().pixelSize(), 12);
                    QVERIFY(qAbs(glyphCenterY(session.window, image, text) - centerY(button)) <
                            1.5 * scale);
                    if (!provider.isEmpty()) {
                        auto *name = visual(session.window->contentItem(), "dspProviderNameRow");
                        auto *version = visual(name, "dspProviderVersion");
                        auto *status =
                            visual(session.window->contentItem(), "dspProviderStatusText");
                        QVERIFY(name && version && status);
                        QVERIFY(qAbs(centerY(name) - centerY(status)) < .5);
                        for (auto *row : {name, version, status})
                            QVERIFY(qAbs(glyphCenterY(session.window, image, row) - centerY(name)) <
                                    1.5 * scale);
                        auto *outputTabs =
                            visual(session.window->contentItem(), "dspOutputDeviceTabs");
                        QVERIFY(outputTabs);
                        QCOMPARE(outputTabs->size(), QSizeF(160, 32));
                        QVERIFY(outputTabs->property("compact").toBool());
                        for (int output = 0; output < 2; ++output) {
                            auto *choice = visual(outputTabs, "tab-" + QString::number(output));
                            QVERIFY(choice);
                            auto *text = visualText(choice, output == 0 ? QStringLiteral("扬声器")
                                                                        : QStringLiteral("耳机"));
                            QVERIFY(text);
                            QCOMPARE(text->property("font").value<QFont>().pixelSize(), 12);
                            QVERIFY(qAbs(glyphCenterY(session.window, image, text) -
                                         centerY(choice)) < 1.5 * scale);
                        }
                        auto *engine =
                            visual(session.window->contentItem(), "dspProviderEngineCard");
                        auto *heading =
                            visual(session.window->contentItem(), "dspPresetSectionHeading");
                        QVERIFY(engine && heading);
                        const auto bounds = [](QQuickItem *item) {
                            return item->mapRectToScene(QRectF(QPointF(), item->size()));
                        };
                        const auto presets = playback.audioControls()->providerPresets();
                        QVERIFY(presets.size() >= 6);
                        QList<QRectF> options;
                        int headphoneTags = 0, settingsButtons = 0;
                        for (int index = 0; index < 6; ++index) {
                            const auto data = presets[index].toMap();
                            const auto id = data.value("id").toString();
                            auto *option =
                                visual(session.window->contentItem(), "dspPresetOption-" + id);
                            QVERIFY(option);
                            QCOMPARE(option->height(), 38.);
                            options.append(bounds(option));
                            auto *button = visual(option, "dspPresetButton-" + id);
                            auto *label = visual(option, "dspPresetLabel");
                            auto *tag = visual(option, "dspPresetDeviceTag");
                            auto *settings = visual(option, "dspPresetSettings-" + id);
                            QVERIFY(button && label && tag && settings);
                            QCOMPARE(label->property("font").value<QFont>().pixelSize(), 11);
                            QVERIFY(qAbs(glyphCenterY(session.window, image, label) -
                                         centerY(option)) < 1.5 * scale);
                            const bool headphone =
                                data.value("recommendedDevice").toString() == "headphone";
                            QCOMPARE(tag->isVisible(), headphone);
                            if (headphone) {
                                ++headphoneTags;
                                QCOMPARE(tag->height(), 18.);
                                QCOMPARE(tag->property("font").value<QFont>().pixelSize(), 10);
                                QVERIFY(qAbs(bounds(button).right() - bounds(tag).right() -
                                             9 * scale) < .5);
                                QVERIFY(qAbs(centerY(tag) - centerY(option)) < .5);
                                auto *text = visualText(tag, QStringLiteral("耳机"));
                                QVERIFY(text);
                                QVERIFY(qAbs(glyphCenterY(session.window, image, text) -
                                             centerY(option)) < 1.5 * scale);
                            }
                            if (settings->isVisible()) {
                                ++settingsButtons;
                                QCOMPARE(settings->size(), QSizeF(28, 28));
                                QVERIFY(qAbs(bounds(option).right() - bounds(settings).right() -
                                             4 * scale) < .5);
                                QVERIFY(qAbs(centerY(settings) - centerY(option)) < .5);
                            }
                        }
                        QVERIFY(headphoneTags > 0 && settingsButtons > 0);
                        for (int row = 0; row < 3; ++row) {
                            const auto &left = options[row * 2];
                            const auto &right = options[row * 2 + 1];
                            QVERIFY(qAbs(left.top() - right.top()) < .5);
                            QVERIFY(qAbs(left.left() - bounds(engine).left()) < .5);
                            QVERIFY(qAbs(right.right() - bounds(engine).right()) < .5);
                            QVERIFY(qAbs(right.left() - left.right() - 7 * scale) < .5);
                            if (row > 0)
                                QVERIFY(qAbs(left.top() - options[(row - 1) * 2].bottom() -
                                             7 * scale) < .5);
                        }
                        QVERIFY(qAbs(options[0].top() - bounds(heading).bottom() - 8 * scale) < .5);
                    }
                }
                if (tab == 3) {
                    auto *categories =
                        visual(session.window->contentItem(), "effectPlazaCategoryTabs");
                    auto *sorts = visual(session.window->contentItem(), "effectPlazaSortTabs");
                    QVERIFY(categories && sorts);
                    const QStringList ids{"artist", "headphone", "market"};
                    for (int category = 0; category < ids.size(); ++category) {
                        auto *choice = visual(categories, "tab-" + QString::number(category));
                        QVERIFY(choice);
                        QVERIFY(QMetaObject::invokeMethod(choice, "clicked"));
                        QTRY_COMPARE(playback.effectCatalog()->category(), ids[category]);
                        QTRY_COMPARE(categories->height(), 32.);
                        QCOMPARE(categories->property("currentIndex").toInt(), category);
                        auto *text = visualText(choice, choice->property("text").toString());
                        QVERIFY(text);
                        QCOMPARE(text->property("font").value<QFont>().pixelSize(), 12);
                        if (category == 2) {
                            QTRY_VERIFY(sorts->isVisible());
                            QTRY_COMPARE(sorts->height(), 32.);
                            QTRY_COMPARE(sorts->width(), 220.);
                            auto *latest = visual(sorts, "tab-2");
                            QVERIFY(latest && QMetaObject::invokeMethod(latest, "clicked"));
                            QTRY_COMPARE(sorts->property("currentIndex").toInt(), 2);
                            auto *label = visualText(latest, QStringLiteral("最新"));
                            QVERIFY(label);
                            QCOMPARE(label->property("font").value<QFont>().pixelSize(), 12);
                        } else {
                            QVERIFY(!sorts->isVisible());
                        }
                        QTest::qWait(100);
                        const auto categoryImage = session.window->grabWindow();
                        QVERIFY(qAbs(glyphCenterY(session.window, categoryImage, text) -
                                     centerY(choice)) < 1.5 * scale);
                        if (!capture.isEmpty())
                            QVERIFY(categoryImage.save(
                                capture + QString("/plaza-%1-%2-%3.png")
                                              .arg(ids[category])
                                              .arg(theme == ThemeStore::Light ? "light" : "dark")
                                              .arg(scale)));
                    }
                    playback.effectCatalog()->selectCategory("artist");
                }
                if (!capture.isEmpty())
                    QVERIFY(
                        image.save(capture + QString("/effect-%1-%2-%3.png")
                                                 .arg(tab)
                                                 .arg(theme == ThemeStore::Light ? "light" : "dark")
                                                 .arg(scale)));
            }
            QVERIFY(QMetaObject::invokeMethod(popup.data(), "close"));
            QTRY_VERIFY(popup.isNull());
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionResolvedQualityMarksPickerAndToolbar()
{
    Session session;
    session.preferences.setValue("playback/volume", 0);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(!session.root->findChild<QObject *>("playbackOptionsPopup"));
    auto *actions = session.commands.playerbarActions();
    int qualityRow = -1;
    for (int i = 0; i < actions->rowCount(); ++i)
        if (actions->index(i, 0).data(ShellActionModel::IdRole).toString() == "quality")
            qualityRow = i;
    QVERIFY(qualityRow >= 0);
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        PlaybackServiceTestAccess::quality(playback, "hi_res");
        auto popup = session.openPlayerPopup("quality", "playbackOptionsPopup");
        QVERIFY(popup);
        auto *hiRes = visual(session.window->contentItem(), "playbackOption-hi_res");
        auto *tape = visual(session.window->contentItem(), "playbackOption-viper_tape");
        QVERIFY(hiRes && tape);
        QTRY_VERIFY(hiRes->property("selected").toBool());
        QVERIFY(!tape->property("selected").toBool());
        QCOMPARE(actions->index(qualityRow, 0).data(ShellActionModel::BadgeRole).toString(),
                 QString("HR"));
        PlaybackServiceTestAccess::quality(playback, "flac");
        auto *flac = visual(session.window->contentItem(), "playbackOption-flac");
        QVERIFY(flac);
        QTRY_VERIFY(flac->property("selected").toBool());
        QVERIFY(!hiRes->property("selected").toBool());
        QCOMPARE(actions->index(qualityRow, 0).data(ShellActionModel::BadgeRole).toString(),
                 QString("SQ"));
        QCOMPARE(playback.quality(), QString("viper_tape"));
        QCOMPARE(playback.preferences()->defaultQuality(), QString("hi_res"));
        QVERIFY(QMetaObject::invokeMethod(popup.data(), "close"));
        QTRY_VERIFY(popup.isNull());
    }
    QSignalSpy closed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}

void DesktopPagesTest::productionAddToReopenStableHeight()
{
    Session session;
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    PlaybackTrack track;
    track.id = "add-reopen";
    track.localPath = "/ui-test/add-reopen.wav";
    playback.appendTracks({track});
    playback.queueCatalog()->activate("test-second", "Second queue", {track}, 0);
    playback.queueCatalog()->activate("test-third", "Third queue", {track}, 0);
    for (int cycle = 0; cycle < 4; ++cycle) {
        // 弹层按需创建：由服务入口打开后取得新实例，关闭即释放。
        playback.libraryActions()->prepareViewedQueue();
        QPointer<QObject> dialog = session.root->findChild<QObject *>("addToPlaylistDialog");
        QTRY_VERIFY(!dialog.isNull());
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *targets = visual(session.window->contentItem(), "playlistAddTargets");
        QVERIFY(targets);
        QTRY_COMPARE(targets->property("count").toInt(), 3);
        QTest::qWait(100);
        qInfo() << "Add dialog cycle" << cycle << "height" << dialog->property("height")
                << "viewport" << targets->height() << "content"
                << targets->property("contentHeight");
        QVERIFY2(targets->height() >= 155,
                 "Three targets and their section headers must fit on every open");
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(dialog.isNull());
    }
}

void DesktopPagesTest::productionAddToHeaderAlignment()
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
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    PlaybackTrack track;
    track.id = "header-layout";
    track.localPath = "/ui-test/header.wav";
    track.title = "标题行布局";
    playback.appendTracks({track});
    QVERIFY(!session.root->findChild<QObject *>("addToPlaylistDialog"));
    const auto capture = qEnvironmentVariable("ECHO_ADD_TO_HEADER_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1.0, 1.25}) {
            session.shell.resetZoom();
            if (scale > 1)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            playback.libraryActions()->prepareViewedQueue();
            QPointer<QObject> dialog;
            QTRY_VERIFY((dialog = session.root->findChild<QObject *>("addToPlaylistDialog")));
            QTRY_VERIFY(dialog->property("opened").toBool());
            auto *title = visual(session.window->contentItem(), "dialogHeadingTitle");
            auto *label = visual(session.window->contentItem(), "addToMultipleLabel");
            auto *toggle = visual(session.window->contentItem(), "addToMultipleSwitch");
            auto *close = visual(session.window->contentItem(), "dialogCloseButton");
            QVERIFY(title && label && toggle && close);
            const auto center = [](QQuickItem *item) {
                return item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
            };
            QTRY_VERIFY(qAbs(center(title).y() - center(toggle).y()) < .5);
            QTRY_VERIFY(qAbs(center(label).y() - center(toggle).y()) < .5);
            QTRY_VERIFY(qAbs(center(close).y() - center(toggle).y()) < .5);
            QCOMPARE(toggle->size(), QSizeF(36, 20));
            QTest::qWait(100);
            const auto image = session.window->grabWindow();
            QVERIFY(!image.isNull());
            const qreal ratio = image.width() / qreal(session.window->width());
            // Check actual glyph pixels as well as the shared line boxes.
            // A centered Text item can still place fallback Chinese ink too low.
            for (auto *row : {title, label}) {
                auto *text = row->property("textItem").value<QQuickItem *>();
                QVERIFY(text);
                const auto bounds =
                    text->mapRectToScene(QRectF(0, 0, text->width(), text->height()));
                const auto pixels = QRectF(bounds.topLeft() * ratio, bounds.size() * ratio)
                                        .toAlignedRect()
                                        .intersected(image.rect());
                int top = image.height(), bottom = -1;
                for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                    for (int x = pixels.left(); x <= pixels.right(); ++x) {
                        const auto pixel = image.pixelColor(x, y);
                        if (theme == ThemeStore::Dark ? pixel.lightness() > 150
                                                      : pixel.lightness() < 100) {
                            top = std::min(top, y);
                            bottom = std::max(bottom, y);
                        }
                    }
                QVERIFY(bottom >= top);
                QVERIFY(qAbs((top + bottom + 1) / (2 * ratio) - center(toggle).y()) <= 1.5);
            }
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                              center(toggle).toPoint());
            QTRY_VERIFY(playback.libraryActions()->multiple());
            QTest::qWait(150);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/add-to-%1-%2.png")
                                  .arg(theme == ThemeStore::Light ? "light" : "dark")
                                  .arg(scale)));
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(dialog.isNull());
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::preferencesPreserveUnknownTagsValidateAndKeepFailedDraft()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService accountService(api);
    ListeningPreferencesService service(api, accountService, nullptr);
    QAbstractItemModelTester strengthTester(service.strengths(),
                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(service.strengths(), &QAbstractItemModel::modelReset);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    const auto read = ListeningPreferencesTestAccess::read(service, api);
    ListeningPreferencesTestAccess::complete(service, read, preferencesDomain());
    QVERIFY(service.loaded());
    QVERIFY(!service.dirty());
    QCOMPARE(service.modeIndex(), 1);
    QCOMPARE(service.genderOptions().last().toMap().value("value").toString(),
             QString("other-version"));
    service.setMode(1);
    QVERIFY(!service.dirty());
    service.resetRecommendation();
    QVERIFY(!service.dirty()); // Explicit 50 equals an omitted strength.
    service.setStrength("L1", 20);
    QCOMPARE(resets.size(), 0); // A pressed slider must not lose its delegate.
    auto patch = ListeningPreferencesTestAccess::patch(service);
    QVERIFY(patch.recommendation);
    const auto weights = QJsonDocument::fromJson(patch.recommendation->toUtf8()).object();
    QCOMPARE(weights.value("125").toInt(), 100);
    QCOMPARE(weights.value("L1").toInt(), 20);
    QVERIFY(!patch.languages);
    service.load();
    QVERIFY(!service.loading()); // Never replace an unsaved draft.
    for (const auto &id : QStringList{"L1", "L2", "L5", "L4", "L3", "L6"})
        service.setStrength(id, 0);
    service.save();
    QVERIFY(!service.saving());
    QCOMPARE(service.error(), QStringLiteral("语种不能全部屏蔽，请至少保留一种。"));
    service.setStrength("L1", 50);
    service.setStrength("S9", 100);
    service.setStrength("T1", 0);
    service.save();
    QCOMPARE(service.error(),
             QStringLiteral("加大 DJ 推荐强度时，请保留翻唱推荐；许多 DJ 歌曲属于翻唱。"));
    service.setStrength("T1", 50);
    const auto write = ListeningPreferencesTestAccess::submitted(service, api);
    ListeningPreferencesTestAccess::fail(service, write);
    QVERIFY(!service.saving());
    QVERIFY(service.dirty());
    QVERIFY(!service.error().isEmpty());
    const auto retried = ListeningPreferencesTestAccess::submitted(service, api);
    QSignalSpy saved(&service, &ListeningPreferencesService::saved);
    ListeningPreferencesTestAccess::complete(service, retried, R"({"updated":true})");
    QCOMPARE(saved.size(), 1);
    QVERIFY(!service.dirty());
    const auto stale = ListeningPreferencesTestAccess::read(service, api);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900002)));
    ListeningPreferencesTestAccess::complete(service, stale, "{}");
    QVERIFY(!service.loaded());
    service.shutdown();
    api.shutdown();
}

void DesktopPagesTest::missingMembershipCodesKeepProductsWithoutInventingKnownRights()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService accountService(api);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    const auto read = AccountServiceTestAccess::vipRequest(accountService, api);
    AccountServiceTestAccess::complete(
        accountService, read,
        R"({"user_id":900001,"user_type_code":null,"vip_type_code":null,"products":[{"product":"svip","business":"concept","active":null,"active_code":99}]})");
    QVERIFY(accountService.profileError().isEmpty());
    const auto membership = AccountServiceTestAccess::membershipDomain(accountService);
    QCOMPARE(membership["products"].toArray().size(), 1);
    QVERIFY(!AccountServiceTestAccess::membershipKnown(accountService));
    QVERIFY(accountService.memberships().isEmpty());
    QCOMPARE(accountService.membershipLabel(), QString("未确认"));
    api.shutdown();
}

void DesktopPagesTest::missingPreferenceFactsDoNotBecomeExplicitClears()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService accountService(api);
    ListeningPreferencesService service(api, accountService, nullptr);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    const auto read = ListeningPreferencesTestAccess::read(service, api);
    ListeningPreferencesTestAccess::complete(
        service, read,
        R"({"recommendation_mode":null,"gender":null,"age":null,"languages":{"encoded":null,"weights":null},"styles":{"encoded":null,"weights":null},"recommendation":{"encoded":null,"weights":null}})");
    QVERIFY(service.loaded());
    QVERIFY(!service.dirty());
    QCOMPARE(service.modeIndex(), -1);
    QCOMPARE(service.genderOptions().first().toMap()["label"].toString(), QString("未提供"));
    service.setGender(1); // Explicit "不设置" differs from missing.
    auto patch = ListeningPreferencesTestAccess::patch(service);
    QVERIFY(patch.gender.has_value());
    QVERIFY(patch.gender->isEmpty());
    QVERIFY(!patch.mode && !patch.age && !patch.languages && !patch.styles &&
            !patch.recommendation);
    service.discard();
    QVERIFY(!service.dirty());
    service.setMode(1);
    patch = ListeningPreferencesTestAccess::patch(service);
    QCOMPARE(patch.mode, std::optional<QString>("0"));
    service.shutdown();
    api.shutdown();
}

void DesktopPagesTest::liveSameAvatarThroughQtService()
{
    if (qEnvironmentVariable("ECHO_LIVE_PROFILE_AVATAR") != "1")
        QSKIP("Explicit current-account same-avatar write probe only");
    const std::string input((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
    const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
    DeviceIdentity device;
    device.mid = identity.value("mid").toString();
    device.dfid = identity.value("dfid").toString();
    device.guid = identity.value("guid").toString();
    device.mac = identity.value("mac").toString();
    device.deviceName = identity.value("device_name").toString();
    ApiClient api(device);
    AccountService accountService(api);
    QJsonObject session{{"user_id", identity.value("userid")},
                        {"token", identity.value("token")},
                        {"refresh_credential", identity.value("t1")},
                        {"nickname", ""},
                        {"avatar_url", ""}};
    QVERIFY(AccountServiceTestAccess::adopt(accountService, QJsonDocument(session).toJson()));
    ProfileService profile(api, accountService);
    accountService.reloadProfile();
    QTRY_VERIFY_WITH_TIMEOUT(!accountService.profileLoading(), 30000);
    QVERIFY(accountService.profileDetailAvailable());
    QVERIFY(!accountService.avatarUrl().isEmpty());
    // Download the same public avatar; never send account credentials to
    // the image CDN. The temporary input is removed with this test scope.
    QNetworkAccessManager images;
    QNetworkRequest request{QUrl(accountService.avatarUrl())};
    request.setTransferTimeout(20000);
    std::unique_ptr<QNetworkReply> image(images.get(request));
    QTRY_VERIFY_WITH_TIMEOUT(image->isFinished(), 25000);
    QVERIFY(image->error() == QNetworkReply::NoError);
    const auto bytes = image->readAll();
    QVERIFY(!bytes.isEmpty() && bytes.size() <= 8 * 1024 * 1024);
    QTemporaryDir directory;
    QFile file(directory.filePath("same-current-avatar"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
    QSignalSpy completed(&profile, &ProfileService::avatarFinished);
    profile.uploadAvatar(QUrl::fromLocalFile(file.fileName()));
    QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 60000);
    QVERIFY(completed.first().at(0).toBool());
    QVERIFY(!profile.uploadingAvatar());
    QSignalSpy closed(&api, &ApiClient::closed);
    profile.shutdown();
    api.shutdown();
    QTRY_COMPARE(closed.size(), 1);
}

void DesktopPagesTest::screenshotTargetSnapshotAndDuplicateConfirmation()
{
    ApiClient api(DeviceIdentity::temporary());
    api.setAccount(900001, "domain-test-token");
    UserPlaylistModel source(api);
    PlaylistMutationService mutations(api);
    PlaylistImportService importer(api, source, mutations);
    QSignalSpy duplicate(&importer, &PlaylistImportService::duplicateConfirmationRequested);
    PlaylistImportServiceTestAccess::prepareScreenshot(importer, 0, "重复歌单");
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"({"user_id":900001,"total_revision":1,"page":{"page":1,"page_size":200,"total":2,"items":[{"cloud_list_id":1,"kind":"owned","source_code":1,"creator_id":900001,"title":"重复歌单"},{"cloud_list_id":2,"kind":"collected","source_code":1,"creator_id":900003,"title":"他人的歌单"}]}})");
    QCOMPARE(importer.targets().size(), 1);
    QCOMPARE(duplicate.size(), 1);
    QCOMPARE(importer.duplicateName(), QString("重复歌单"));
    importer.confirmDuplicate(false, {});
    QCOMPARE(importer.phase(), PlaylistImportService::Stopped);
    PlaylistImportServiceTestAccess::prepareScreenshot(importer, 2, {});
    PlaylistImportServiceTestAccess::complete(
        importer,
        R"({"user_id":900001,"total_revision":1,"page":{"page":1,"page_size":200,"total":1,"items":[{"cloud_list_id":2,"kind":"collected","source_code":1,"creator_id":900003,"title":"他人的歌单"}]}})");
    QCOMPARE(importer.phase(), PlaylistImportService::Failed);
    QVERIFY(importer.errorMessage().contains("目标歌单"));
    api.shutdown();
}

void DesktopPagesTest::incompleteAndRepeatedLibraryIdsKeepEveryRow()
{
    ApiClient api(DeviceIdentity::temporary());
    api.setAccount(900001, "domain-test-token");
    UserPlaylistModel library(api);
    UserPlaylistModelTestAccess::complete(
        library, api,
        R"({"total_revision":12,"page":{"total":null,"items":[{"cloud_list_id":2,"title":"A"},{"cloud_list_id":2,"title":"B"},{"cloud_list_id":null,"title":"资料待补齐"}]}})");
    QCOMPARE(library.rowCount(), 3);
    QCOMPARE(library.data(library.index(1), UserPlaylistModel::TitleRole).toString(), QString("B"));
    QCOMPARE(library.data(library.index(2), UserPlaylistModel::TitleRole).toString(),
             QString("资料待补齐"));
    QVERIFY(!library.libraryIdAt(2));
    QVERIFY(!library.cloudIdAt(2));
    QVERIFY(library.errorMessage().isEmpty());
    api.shutdown();
}

void DesktopPagesTest::libraryConsumersAcceptPageEchoAndCountVariations()
{
    ApiClient api(DeviceIdentity::temporary());
    api.setAccount(900001, "domain-test-token");
    UserPlaylistModel library(api);
    PlaylistMutationService mutations(api);
    PlaylistOrderModel order(api, mutations);
    PlaylistImportService importer(api, library, mutations);
    const QByteArray snapshot =
        R"({"user_id":900001,"total_revision":12,"page":{"page":999,"page_size":1,"total":0,"items":[
            {"cloud_list_id":2,"kind":"owned","title":"A","sort_index":0,"song_count":2,"source_code":1,"creator_id":900001},
            {"cloud_list_id":3,"kind":"owned","title":"B","sort_index":1,"song_count":3,"source_code":1,"creator_id":900001}]}})";
    order.open(0);
    PlaylistOrderModelTestAccess::complete(order, PlaylistOrderModelTestAccess::request(order),
                                           snapshot);
    QVERIFY(order.errorMessage().isEmpty());
    QCOMPARE(order.rowCount(), 2);
    QCOMPARE(PlaylistOrderModelTestAccess::positions(order).size(), 2);
    importer.refreshTargets();
    PlaylistImportServiceTestAccess::complete(importer, snapshot);
    QVERIFY(importer.errorMessage().isEmpty());
    QCOMPARE(importer.targets().size(), 2);
    auto repeated = QJsonDocument::fromJson(snapshot).object();
    auto page = repeated["page"].toObject();
    page["total"] = 999;
    repeated["page"] = page;
    const auto payload = QJsonDocument(repeated).toJson();
    order.open(0);
    PlaylistOrderModelTestAccess::complete(order, PlaylistOrderModelTestAccess::request(order),
                                           payload);
    QVERIFY(order.busy());
    PlaylistOrderModelTestAccess::complete(order, PlaylistOrderModelTestAccess::request(order),
                                           payload);
    QVERIFY(!order.busy());
    QVERIFY(order.errorMessage().isEmpty());
    QCOMPARE(order.rowCount(), 2);
    importer.refreshTargets();
    PlaylistImportServiceTestAccess::complete(importer, payload);
    QVERIFY(importer.loadingTargets());
    PlaylistImportServiceTestAccess::complete(importer, payload);
    QVERIFY(!importer.loadingTargets());
    QVERIFY(importer.errorMessage().isEmpty());
    QCOMPARE(importer.targets().size(), 2);

    api.shutdown();
}

void DesktopPagesTest::identityProjectionAndSessionIsolation()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService service(api);
    QVERIFY(AccountServiceTestAccess::adopt(service, account(900001)));
    const auto detail = AccountServiceTestAccess::profileRequest(service, api);
    AccountServiceTestAccess::complete(
        service, detail,
        R"({"id":900001,"identity":{"talent_bits":3,"actor":true,"business":true,"star":true,"singer":true,"authorizations":[],"talent_authorizations":[],"singer_authorizations":[]}})");
    const auto identity = AccountServiceTestAccess::identityRequest(service, api);
    AccountServiceTestAccess::complete(
        service, identity,
        R"({"student_status":1,"student_school":"领域测试学校","student_expire_time":"2030-01-01","tags":["摇滚","旅行"]})");
    QCOMPARE(service.avatarIdentityKind(), QString("singer"));
    QCOMPARE(service.identityBadges().size(), 7);
    QCOMPARE(service.identityBadges().first().toMap()["label"].toString(), QStringLiteral("学生"));
    QCOMPARE(service.identityBadges().last().toMap()["label"].toString(),
             QStringLiteral("评论达人"));
    QCOMPARE(service.identityTags(), (QStringList{QStringLiteral("摇滚"), QStringLiteral("旅行")}));
    const auto stale = AccountServiceTestAccess::identityRequest(service, api);
    QVERIFY(AccountServiceTestAccess::adopt(service, account(900002)));
    AccountServiceTestAccess::complete(service, stale, R"({"student_status":1,"tags":["旧账号"]})");
    QVERIFY(service.identityBadges().isEmpty());
    QVERIFY(service.identityTags().isEmpty());
    QVERIFY(service.avatarIdentityIcon().isEmpty());
    QVERIFY(service.studentSchool().isEmpty());
    // Product projection tests use canonical data, not invented HTTP fixtures.
    AccountIdentity talent;
    talent.talentBits = 1 | (1 << 11);
    talent.authorizations = {QStringLiteral("歌单达人")};
    talent.talentAuthorizations = {QStringLiteral("歌单达人")};
    QCOMPARE(talent.avatarKind(), QString("talent"));
    QCOMPARE(talent.badges(false).size(), 1);
    talent.authorizations.clear();
    talent.talentAuthorizations.clear();
    QCOMPARE(talent.badges(false).size(), 1); // Reserved category has no label.
    api.shutdown();
}

void DesktopPagesTest::productionIdentitySummaryAndAvatarCorners()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    // The page exists before network data arrives in the real application.
    // Exercise empty-to-populated summaries, rather than preloading every tag.
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileIdentitySummary"));
    auto *summary = visual(session.window->contentItem(), "profileIdentitySummary");
    QTest::qWait(50);
    const auto detail =
        AccountServiceTestAccess::profileRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, detail,
        R"({"id":900001,"nickname":"一天 Qt","avatar_url":"","identity":{"talent_bits":3,"actor":false,"business":true,"star":false,"singer":false,"authorizations":[],"talent_authorizations":[],"singer_authorizations":[]}})");
    const auto identity =
        AccountServiceTestAccess::identityRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, identity,
        R"({"student_status":1,"tags":["摇滚","English 中文","用于验证长标签换行和主题颜色不会挤出个人中心边界的领域展示文字"]})");
    QCOMPARE(visual(session.window->contentItem(), "profileIdentitySummary"), summary);
    QTRY_VERIFY(summary->width() > 0);
    QTRY_COMPARE(visualAll(summary, "userIdentityBadge").size(), 4);
    QTRY_COMPARE(visualAll(summary, "userInterestTag").size(), 3);
    // Repeater.count changes before itemAt() is populated on Cocoa.
    // Verify the original page grows as delegates arrive, before a resize,
    // theme change or zoom could incidentally recompute its natural width.
    QTRY_VERIFY(summary->height() >= 18);
    QTRY_VERIFY(summary->property("naturalWidth").toReal() > 10);
    for (auto *tag : visualAll(summary, "userInterestTag"))
        QTRY_VERIFY(tag->width() > 0);
    auto *profileAvatar = visual(session.window->contentItem(), "profileAccountAvatar");
    QVERIFY(profileAvatar);
    QCOMPARE(profileAvatar->property("identityInset").toReal(), 3.0);
    QCOMPARE(profileAvatar->property("identityIcon").toUrl(),
             QUrl(session.accounts.account.avatarIdentityIcon()));
    auto *avatar = visual(session.window->contentItem(), "sidebarAccountAvatar");
    QVERIFY(avatar);
    QCOMPARE(avatar->property("identityIcon").toUrl(),
             QUrl(session.accounts.account.avatarIdentityIcon()));
    // Local icon input exercises the real mark loader without making a
    // network image download a prerequisite of a geometry regression.
    QVERIFY(
        avatar->setProperty("identityIcon", QUrl("qrc:/qt/qml/EchoMusic/Controls/icons/user.svg")));
    auto *mark = avatar->findChild<QQuickItem *>("avatarIdentityMark");
    QVERIFY(mark);
    QTRY_VERIFY(mark->isVisible());
    QCOMPARE(mark->width(), 16.0);
    QCOMPARE(mark->height(), 16.0);
    QCOMPARE(mark->x(), avatar->width() - 17);
    QCOMPARE(mark->y(), avatar->height() - 17);
    avatar->setProperty("resourceActive", false);
    QTRY_VERIFY(mark->property("source").toUrl().isEmpty());
    avatar->setProperty("resourceActive", true);
    QTRY_VERIFY(mark->isVisible());
    const auto capture = qEnvironmentVariable("ECHO_IDENTITY_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            QTRY_VERIFY(summary->height() >= 18);
            for (auto *tag : visualAll(summary, "userInterestTag")) {
                QTRY_VERIFY(tag->width() > 0);
                const auto position = tag->mapToItem(summary, QPointF());
                QVERIFY(position.x() >= -0.5);
                QVERIFY(position.x() + tag->width() <= summary->width() + 0.5);
                QVERIFY(position.y() + tag->height() <= summary->height() + 0.5);
            }
            if (!capture.isEmpty()) {
                QTest::qWait(120);
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/identity-%1-%2.png")
                                  .arg(mode == ThemeStore::Light ? "light" : "dark")
                                  .arg(zoomed ? "scaled" : "normal")));
            }
        }
    }
    QQmlComponent narrowComponent(&session.engine,
                                  QUrl("qrc:/qt/qml/EchoMusic/Profile/ProfileOverview.qml"));
    std::unique_ptr<QObject> narrow(narrowComponent.createWithInitialProperties(
        {{"account", QVariant::fromValue(&session.accounts.account)}, {"width", 360.0}}));
    QVERIFY2(narrow, qPrintable(narrowComponent.errorString()));
    auto *narrowItem = qobject_cast<QQuickItem *>(narrow.get());
    QVERIFY(narrowItem);
    narrowItem->setParentItem(session.window->contentItem());
    auto *narrowSummary = visual(narrowItem, "profileIdentitySummary");
    auto *narrowAvatar = visual(narrowItem, "profileAccountAvatar");
    QVERIFY(narrowSummary && narrowAvatar);
    QTRY_VERIFY(narrowSummary->height() > 50);
    QVERIFY(narrowSummary->mapToItem(narrowItem, QPointF()).y() <
            narrowAvatar->mapToItem(narrowItem, QPointF()).y());
    for (auto *tag : visualAll(narrowSummary, "userInterestTag")) {
        const auto position = tag->mapToItem(narrowSummary, QPointF());
        QVERIFY(position.x() >= -0.5);
        QVERIFY(position.x() + tag->width() <= narrowSummary->width() + 0.5);
        QVERIFY(position.y() + tag->height() <= narrowSummary->height() + 0.5);
    }
    narrow.reset();
    session.shell.toggleSidebar();
    QTRY_COMPARE(avatar->width(), 32.0);
    QCOMPARE(mark->x(), 15.0);
    session.shell.toggleSidebar();
    QVERIFY(!session.root->findChild<QObject *>("sidebarAccountPopover"));
    QVERIFY(session.clickItem("sidebarAccountTrigger"));
    QPointer<QObject> panel;
    QTRY_VERIFY((panel = session.root->findChild<QObject *>("sidebarAccountPopover")));
    QTRY_VERIFY(panel->property("opened").toBool());
    QPointer<QQuickItem> popupAvatar =
        visual(session.window->contentItem(), "accountPopoverAvatar");
    QVERIFY(popupAvatar);
    QCOMPARE(popupAvatar->property("identityIcon").toUrl(),
             QUrl(session.accounts.account.avatarIdentityIcon()));
    QVERIFY(QMetaObject::invokeMethod(panel.data(), "close"));
    QTRY_VERIFY(panel.isNull());
    QTRY_VERIFY(popupAvatar.isNull());
    StoredAccounts rows{{900001, "Current", {}, {}, 2},
                        {900002, "Saved singer", {}, {}, 1, "singer"}};
    SavedAccountModelTestAccess::present(
        session.accounts.saved, rows, AccountServiceTestAccess::snapshot(session.accounts.account));
    QVERIFY(session.clickItem("sidebarAccountTrigger"));
    QVERIFY(session.clickItem("accountSwitchAction"));
    QPointer<QObject> switcher;
    QTRY_VERIFY((switcher = session.root->findChild<QObject *>("accountSwitcherDialog")));
    QTRY_VERIFY(switcher->property("opened").toBool());
    auto *list = visual(session.window->contentItem(), "savedAccountList");
    QVERIFY(list);
    QTRY_COMPARE(visualAll(list, "savedAccountAvatar").size(), 2);
    bool singerFound = false;
    for (auto *savedAvatar : visualAll(list, "savedAccountAvatar")) {
        QCOMPARE(savedAvatar->property("identityInset").toReal(), 0.0);
        singerFound |=
            savedAvatar->property("identityLabel").toString() == QStringLiteral("歌手认证");
    }
    QVERIFY(singerFound);
    QVERIFY(QMetaObject::invokeMethod(switcher.data(), "close"));
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::liveContentBlacklistThroughQtService()
{
    if (qEnvironmentVariable("ECHO_LIVE_CONTENT_BLACKLIST") != "1")
        QSKIP("Explicit current-account temporary blacklist roundtrip only");
    const std::string input((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
    const auto identity = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
    DeviceIdentity device;
    device.mid = identity.value("mid").toString();
    device.dfid = identity.value("dfid").toString();
    device.guid = identity.value("guid").toString();
    device.mac = identity.value("mac").toString();
    device.deviceName = identity.value("device_name").toString();
    ApiClient api(device);
    AccountService accountService(api);
    QVERIFY(AccountServiceTestAccess::adopt(
        accountService, QJsonDocument(QJsonObject{{"user_id", identity.value("userid")},
                                                  {"token", identity.value("token")},
                                                  {"refresh_credential", identity.value("t1")},
                                                  {"nickname", ""},
                                                  {"avatar_url", ""}})
                            .toJson()));
    ContentBlacklistService service(api, accountService);
    QSignalSpy succeeded(&api, &ApiClient::succeeded), failed(&api, &ApiClient::failed);
    const auto waitFor = [&](quint64 id) {
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 30000) {
            for (const auto &args : failed)
                if (args[0].toULongLong() == id)
                    return false;
            for (const auto &args : succeeded)
                if (args[0].toULongLong() == id)
                    return true;
            QTest::qWait(10);
        }
        return false;
    };
    for (int category = 0; category < 2; ++category) {
        service.selectCategory(category);
        service.activate();
        QTRY_VERIFY_WITH_TIMEOUT(service.loaded() && !service.loading(), 30000);
        // Require the freshly observed empty baseline; never overwrite an
        // existing user's blocked resource or delete unrelated entries.
        QCOMPARE(service.entries()->count(), 0);
        QVERIFY(service.error().isEmpty());
        QCOMPARE(category == 0 ? service.songCount() : service.singerCount(), QString("0"));
        const auto kind =
            category == 0 ? ApiClient::BlacklistKind::Song : ApiClient::BlacklistKind::Singer;
        const auto key = category == 0 ? QString("105a2705beee36a8bed029a8e1a6311d")
                                       : identity.value("public_artist_id").toString();
        QVERIFY(!key.isEmpty());
        const auto name = QStringLiteral("EchoMusicNext blacklist verification");
        bool mayExist = true;
        const auto cleanup = qScopeGuard([&] {
            if (mayExist)
                waitFor(api.editContentBlacklist(kind, key, name, "32042818", true));
        });
        const auto add = api.editContentBlacklist(kind, key, name, "32042818", false);
        QVERIFY(add);
        QVERIFY(waitFor(add));
        service.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!service.loading() && !service.entries()->entry(key).isEmpty(),
                                 30000);
        QCOMPARE(service.entries()->count(), 1);
        QVERIFY(service.error().isEmpty());
        service.requestRemoval(key);
        QVERIFY(service.pendingRemoval());
        QSignalSpy removed(&service, &ContentBlacklistService::removalFinished);
        service.confirmRemoval();
        QVERIFY(service.removing());
        QTRY_COMPARE_WITH_TIMEOUT(removed.size(), 1, 30000);
        QVERIFY(removed.front()[0].toBool());
        QVERIFY(!service.pendingRemoval());
        service.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!service.loading(), 30000);
        QCOMPARE(service.entries()->count(), 0);
        QVERIFY(service.entries()->entry(key).isEmpty());
        QVERIFY(service.error().isEmpty());
        mayExist = false;
        service.deactivate();
    }
    service.shutdown();
    api.shutdown();
}

void DesktopPagesTest::contentBlacklistPaginationRemovalAndSessionIsolation()
{
    ApiClient api(DeviceIdentity::temporary());
    AccountService accountService(api);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900001)));
    ContentBlacklistService service(api, accountService);
    QSignalSpy finished(&service, &ContentBlacklistService::removalFinished);
    service.activate();
    auto read = ContentBlacklistTestAccess::read(service, api, 0);
    ContentBlacklistTestAccess::complete(
        service, api, read,
        R"({"page":1,"page_size":2,"total":5,"items":[{"key":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","name":"歌曲 A","mix_song_id":"1"},{"key":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","name":"歌曲 B"}]})");
    QVERIFY(service.loaded());
    QVERIFY(service.hasMore());
    QCOMPARE(service.entries()->count(), 2);
    service.loadMore();
    read = ContentBlacklistTestAccess::request(service, 0);
    QCOMPARE(
        ApiClientTestAccess::request(api, read).value("query").toObject().value("page").toInt(), 2);
    ContentBlacklistTestAccess::complete(
        service, api, read,
        R"({"page":2,"page_size":2,"total":5,"items":[{"key":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","name":"歌曲 B 更新"},{"key":"cccccccccccccccccccccccccccccccc","name":"歌曲 C"}]})");
    QCOMPARE(service.entries()->count(), 3);
    service.requestRemoval("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    QVERIFY(service.pendingRemoval());
    service.cancelRemoval();
    QVERIFY(!service.pendingRemoval());
    QVERIFY(!service.removing());
    service.requestRemoval("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    service.confirmRemoval();
    auto write = ContentBlacklistTestAccess::write(service);
    QVERIFY(write);
    QVERIFY(!ApiClientTestAccess::automaticVerification(api, write));
    QVERIFY(ApiClientTestAccess::request(api, write)
                .value("query")
                .toObject()
                .value("remove")
                .toBool());
    service.confirmRemoval();
    QCOMPARE(ContentBlacklistTestAccess::write(service), write);
    ContentBlacklistTestAccess::fail(service, api, write);
    QCOMPARE(service.entries()->count(), 3);
    QVERIFY(service.pendingRemoval());
    QCOMPARE(finished.size(), 1);
    service.confirmRemoval();
    write = ContentBlacklistTestAccess::write(service);
    ContentBlacklistTestAccess::complete(service, api, write, R"({"updated":true})");
    QVERIFY(!service.pendingRemoval());
    QCOMPARE(service.entries()->count(), 2);
    QCOMPARE(service.songCount(), QString("4"));
    // A successful removal from an offset page realigns from page one.
    read = ContentBlacklistTestAccess::request(service, 0);
    QVERIFY(read);
    QCOMPARE(
        ApiClientTestAccess::request(api, read).value("query").toObject().value("page").toInt(), 1);
    ContentBlacklistTestAccess::complete(
        service, api, read,
        R"({"page":1,"page_size":30,"total":2,"items":[{"key":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","name":"歌曲 B"},{"key":"cccccccccccccccccccccccccccccccc","name":"歌曲 C"}]})");
    service.requestRemoval("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    service.confirmRemoval();
    write = ContentBlacklistTestAccess::write(service);
    service.deactivate();
    service.activate();
    const auto overlap = ContentBlacklistTestAccess::request(service, 0);
    QVERIFY(overlap);
    ContentBlacklistTestAccess::complete(service, api, write, R"({"updated":true})");
    ContentBlacklistTestAccess::complete(
        service, api, overlap,
        R"({"page":1,"page_size":30,"total":2,"items":[{"key":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","name":"过期读取不能恢复已移除项"}]})");
    QCOMPARE(service.entries()->count(), 1);
    QCOMPARE(finished.size(), 2); // No late closed-dialog toast.
    service.activate();
    read = ContentBlacklistTestAccess::read(service, api, 0);
    QVERIFY(AccountServiceTestAccess::adopt(accountService, account(900002)));
    ContentBlacklistTestAccess::complete(
        service, api, read,
        R"({"page":1,"page_size":30,"total":1,"items":[{"key":"old-account","name":"迟到的旧账号资料"}]})");
    QCOMPARE(service.entries()->count(), 0);
    QVERIFY(!service.pendingRemoval());
    QCOMPARE(service.songCount(), QString());
    service.shutdown();
    api.shutdown();
}

void DesktopPagesTest::productionContentBlacklistKeepsPreferencesDraftAndVirtualScroll()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 900);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profilePage"));
    auto *page = visual(session.window->contentItem(), "profilePage");
    QVERIFY(!page->findChild<QObject *>("listeningPreferencesDialog"));
    QVERIFY(session.clickItem("profilePreferences"));
    QPointer<QObject> preferences;
    QTRY_VERIFY((preferences = page->findChild<QObject *>("listeningPreferencesDialog")));
    QPointer<QObject> blacklist;
    auto &preferenceService = *session.profile.preferences();
    auto prefRead = ListeningPreferencesTestAccess::read(preferenceService, session.api);
    ListeningPreferencesTestAccess::complete(preferenceService, prefRead, preferencesDomain());
    QVERIFY(preferences);
    QTRY_VERIFY(preferences->property("opened").toBool());
    prefRead = ListeningPreferencesTestAccess::read(preferenceService, session.api);
    ListeningPreferencesTestAccess::complete(preferenceService, prefRead, preferencesDomain());
    preferenceService.setStrength("L1", 20);
    QVERIFY(preferenceService.dirty());
    QTRY_VERIFY(visual(session.window->contentItem(), "preferencesBlacklist"));
    auto *trigger = visual(session.window->contentItem(), "preferencesBlacklist");
    QVERIFY(trigger->isEnabled());
    QMetaObject::invokeMethod(trigger, "clicked");
    QTRY_VERIFY((blacklist = page->findChild<QObject *>("contentBlacklistDialog")));
    QTRY_VERIFY(blacklist->property("opened").toBool());
    auto &service = *session.profile.blacklist();
    const auto populate = [&](int category, int count) {
        QJsonArray items;
        for (int i = 0; i < count; ++i)
            items.append(QJsonObject{
                {"key", category == 0 ? QString("%1").arg(i + 1, 32, 16, QLatin1Char('0'))
                                      : QString::number(i + 1)},
                {"name",
                 i == 0 ? QStringLiteral("很长的中文歌曲名称 Long Latin fallback title for elision")
                        : QString("呈现条目 %1").arg(i + 1)},
                {"created_at", "1700000000"}});
        const auto read = ContentBlacklistTestAccess::read(service, session.api, category);
        ContentBlacklistTestAccess::complete(
            service, session.api, read,
            QJsonDocument(
                QJsonObject{{"page", 1}, {"page_size", 30}, {"total", count}, {"items", items}})
                .toJson());
    };
    QPointer<QQuickItem> list = visual(session.window->contentItem(), "contentBlacklistList");
    QVERIFY(list);
    const auto captures = qEnvironmentVariable("ECHO_BLACKLIST_CAPTURE_DIR");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    const auto held = ContentBlacklistTestAccess::hold(service, session.api, 0);
    QTRY_VERIFY(visual(session.window->contentItem(), "contentBlacklistState"));
    QTRY_COMPARE(
        visual(session.window->contentItem(), "contentBlacklistState")->property("text").toString(),
        QStringLiteral("正在加载"));
    if (!captures.isEmpty()) {
        QTest::qWait(80);
        QVERIFY(session.window->grabWindow().save(captures + "/blacklist-loading.png"));
    }
    ContentBlacklistTestAccess::fail(service, session.api, held);
    QTRY_VERIFY(visual(session.window->contentItem(), "contentBlacklistRetry"));
    auto *retry = visual(session.window->contentItem(), "contentBlacklistRetry");
    QTRY_VERIFY(retry->isVisible());
    if (!captures.isEmpty()) {
        QTest::qWait(80);
        QVERIFY(session.window->grabWindow().save(captures + "/blacklist-error.png"));
    }
    QMetaObject::invokeMethod(retry, "clicked");
    QVERIFY(service.loading());
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            service.selectCategory(0);
            populate(0, 1000);
            QTRY_VERIFY(visualCount(list, "contentBlacklistRow") > 0);
            QVERIFY(visualCount(list, "contentBlacklistRow") < 30);
            verifyPopupScrollMapping(session.window, list);
            QVERIFY(!QTest::currentTestFailed());
            if (!captures.isEmpty()) {
                QTest::qWait(80);
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/blacklist-%1-%2.png")
                                   .arg(mode == ThemeStore::Light ? "light" : "dark")
                                   .arg(zoomed ? "125" : "100")));
            }
            QTRY_VERIFY(visual(list, "contentBlacklistUndo"));
            QMetaObject::invokeMethod(visual(list, "contentBlacklistUndo"), "clicked");
            QTRY_VERIFY(service.pendingRemoval());
            QPointer<QObject> confirmation =
                blacklist->findChild<QObject *>("contentBlacklistConfirmation");
            QVERIFY(confirmation);
            QTRY_VERIFY(confirmation->property("opened").toBool());
            if (!captures.isEmpty()) {
                QTest::qWait(80);
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/blacklist-confirm-%1-%2.png")
                                   .arg(mode == ThemeStore::Light ? "light" : "dark")
                                   .arg(zoomed ? "125" : "100")));
            }
            service.cancelRemoval();
            QTRY_VERIFY(confirmation.isNull());
            service.selectCategory(1);
            populate(1, 0);
            QTRY_VERIFY(visual(session.window->contentItem(), "contentBlacklistState"));
            QCOMPARE(service.singerCount(), QString("0"));
            auto *tabs = visual(session.window->contentItem(), "contentBlacklistCategories");
            auto *selection = visual(tabs, "tabSelection");
            QVERIFY(selection);
            QTRY_VERIFY(qAbs(selection->x() - (tabs->property("inset").toReal() +
                                               tabs->property("tabWidth").toReal() + 4)) < 0.5);
            if (!captures.isEmpty()) {
                QTest::qWait(80);
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/blacklist-empty-%1-%2.png")
                                   .arg(mode == ThemeStore::Light ? "light" : "dark")
                                   .arg(zoomed ? "125" : "100")));
            }
        }
    }
    QVERIFY(QMetaObject::invokeMethod(blacklist.data(), "close"));
    QTRY_VERIFY(blacklist.isNull());
    QVERIFY(preferences->property("visible").toBool());
    QVERIFY(preferenceService.dirty());
    QTRY_VERIFY(list.isNull());
    QVERIFY(QMetaObject::invokeMethod(preferences.data(), "close"));
    QCOMPARE(warnings.size(), 0);
}
