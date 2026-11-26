#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"
#include "MainWindowHost.h"
#include "ShortcutActions.h"
#include "EchoPluginUi.h"
#include "TrackTransitionController.h"
#include "SettingsKeys.h"
#include <QScreen>

namespace
{
QVariantMap detachMain(Session &session)
{
    QVariantMap properties;
    for (const auto *name :
         {"startup", "playbackService", "libraryController", "accountController", "shellController",
          "searchService", "router", "shellSettings", "windowService"})
        properties.insert(QString::fromLatin1(name), session.root->property(name));
    properties.insert("visible", false);
    session.windows.detach();
    session.root.reset();
    session.window = nullptr;
    return properties;
}
QQuickWindow *createMain(Session &session, const QVariantMap &properties)
{
    QQmlComponent component(&session.engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
    auto *object = component.createWithInitialProperties(properties);
    if (!object)
        qWarning().noquote() << component.errorString();
    session.window = qobject_cast<QQuickWindow *>(object);
    return session.window;
}
bool writeShortWave(const QString &path, quint32 seconds = 3)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    const quint32 bytes = 48000 * seconds * 2;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    stream.writeRawData(QByteArray(bytes, 0).constData(), bytes);
    return stream.status() == QDataStream::Ok;
}
bool captureSurface(QQuickWindow *surface, const QString &name)
{
    const auto directory = qEnvironmentVariable("ECHO_BACKGROUND_CAPTURES");
    if (directory.isEmpty())
        return true;
    if (!surface || !QDir().mkpath(directory) || !QTest::qWaitForWindowExposed(surface))
        return false;
    QSignalSpy rendered(surface, &QQuickWindow::frameSwapped);
    surface->update();
    if (rendered.isEmpty() && !rendered.wait(2000))
        return false;
    return surface->grabWindow().save(directory + '/' + name + ".png");
}
} // namespace

void DesktopPagesTest::mainWindowUnloadRestoresProductionUi_data()
{
    QTest::addColumn<bool>("cacheEnabled");
    QTest::newRow("cache-on") << true;
    QTest::newRow("cache-off") << false;
}

void DesktopPagesTest::mainWindowUnloadRestoresProductionUi()
{
    QFETCH(bool, cacheEnabled);
    Session session;
    session.router.setCachePolicy(cacheEnabled, cacheEnabled ? 20 : 0);
    session.shell.acceptUserAgreement();
    session.shell.setMotionEnabled(false);
    QFile pageFile(session.directory.filePath("BackgroundRoute.qml"));
    QVERIFY(pageFile.open(QIODevice::WriteOnly));
    pageFile.write(R"(
        import QtQuick
        import EchoMusic.Shell
        import EchoMusic.Controls
        RoutePage {
            id: page
            objectName: "backgroundRoute"
            property int draft: 0
            EchoPageState.properties: ["draft"]
            Component.onCompleted: EchoPageState.restore()
            EchoPageScrollContainer {
                anchors.fill: parent
                pageActive: page.pageActive
                scrollView: rows
                ListView {
                    id: rows
                    objectName: "backgroundRows"
                    anchors.fill: parent
                    model: 200
                    delegate: Item {
                        required property int index
                        width: ListView.view.width
                        height: 40
                        Text { text: "Cached row " + index }
                    }
                }
            }
        }
    )");
    pageFile.close();
    QVERIFY(session.router.registerPage({"background",
                                         "/background",
                                         QUrl::fromLocalFile(pageFile.fileName()),
                                         {},
                                         true,
                                         false,
                                         false,
                                         {}}));
    session.initialRoute = "background";
    QVERIFY(session.create());
    const auto properties = detachMain(session);
    auto *context = session.router.current();
    QVERIFY(context);
    QVERIFY(context->viewState()->save("background-probe", {{"selected", 7}}));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    ShortcutService shortcuts(session.preferences, false);
    int creations = 0;
    MainWindowHost host(
        session.engine, session.windows, shortcuts,
        [&] {
            ++creations;
            return createMain(session, properties);
        },
        &session.router);
    const auto cleanup = qScopeGuard([&] {
        host.shutdown();
        session.window = nullptr;
    });
    QSignalSpy activated(&session.router, &RouteController::pageActivated);
    QRect normalGeometry;
    for (int iteration = 0; iteration < 50; ++iteration) {
        host.show();
        QVERIFY(host.window());
        QCOMPARE(host.state(), MainWindowHost::State::Present);
        QTRY_VERIFY(visual(host.window()->contentItem(), "shellFrame"));
        QPointer<QQuickItem> rows;
        QPointer<QQuickItem> page;
        QTRY_VERIFY((rows = visual(host.window()->contentItem(), "backgroundRows")));
        QTRY_VERIFY((page = visual(host.window()->contentItem(), "backgroundRoute")));
        QTRY_VERIFY(visual(host.window()->contentItem(), "shellFrame")
                        ->property("initialPageReady")
                        .toBool());
        if (iteration == 0) {
            const auto area = host.window()->screen()->availableGeometry();
            host.window()->setGeometry({area.topLeft() + QPoint(20, 30),
                                        QSize(qMin(1220, area.width()), qMin(710, area.height()))});
            normalGeometry = host.window()->geometry();
            rows->setProperty("contentY", 3200.);
            page->setProperty("draft", 37);
        }
        QTRY_COMPARE(host.window()->geometry(), normalGeometry);
        QTRY_COMPARE(rows->property("contentY").toDouble(), 3200.);
        QCOMPARE(page->property("draft").toInt(), 37);
        QCOMPARE(session.router.current(), context);
        QCOMPARE(context->viewState()->load("background-probe").value("selected").toInt(), 7);
        QPointer<QQuickWindow> retired = host.window();
        QPointer<QObject> shell = visual(host.window()->contentItem(), "shellFrame");
        session.windows.toggleVisibility();
        QTRY_VERIFY(retired.isNull());
        QVERIFY(shell.isNull());
        QVERIFY(!session.windows.mainWindow());
        QCOMPARE(host.state(), MainWindowHost::State::Absent);
        QVERIFY(!context->presented());
        QVERIFY(!context->viewState()->save("background-probe", {{"selected", 0}}));
    }
    QCOMPARE(creations, 50);
    QCOMPARE(activated.size(), 0); // Visual rebuilding must not replay route activation.
    host.show();
    QVERIFY(captureSurface(host.window(), "restored-main"));
    QPointer<QQuickWindow> closing = host.window();
    session.windows.toggleVisibility();
    QTRY_VERIFY(closing.isNull());
    // This is a user command while no root exists, not a direct call on a
    // pre-existing shell. It must survive creation and first-page readiness.
    session.windows.requestSettings("lyric");
    QPointer<QObject> settings;
    QTRY_VERIFY((settings = host.window()->findChild<QObject *>("settingsDialog")));
    QTRY_VERIFY(settings->property("opened").toBool());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::mainWindowBackgroundKeepsPluginPlaybackAndFloating()
{
    Session session;
    session.shell.acceptUserAgreement();
    session.shell.setMotionEnabled(false);
    // Hold upstream requests: the test uses no account and only local WAV files.
    session.api.addInterceptor(100, [](const QJsonObject &, ApiInterceptNext, ApiInterceptDone) {
        return std::function<void()>([] {});
    });
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("background.sqlite"));
    playback.setVolume(0);
    playback.transition()->setMode("none");
    QVERIFY(session.create(&playback));
    const auto properties = detachMain(session);
    ShortcutService shortcuts(session.preferences, false);
    ShortcutActions actions(shortcuts, playback, session.shell, session.windows, session.commands);
    PluginHost pluginHost{playback,
                          session.theme,
                          session.commands,
                          session.router,
                          session.accounts.account,
                          session.api,
                          session.windows.floating()};
    pluginHost.mainWindow = [&] { return session.windows.mainWindow(); };
    PluginRuntime plugins(session.engine, session.preferences, pluginHost,
                          session.directory.filePath("plugins"));
    plugins.start();
    QTRY_VERIFY(!plugins.loading());
    const auto wav = session.directory.filePath("source.wav");
    QVERIFY(writeShortWave(wav));
    const auto source = session.directory.filePath("source");
    QVERIFY(QDir().mkpath(source));
    const QJsonObject manifest{
        {"schemaVersion", 1},
        {"format", "echomod"},
        {"id", "test.background"},
        {"name", "Background integration"},
        {"version", "1.0.0"},
        {"kind", "qml-js"},
        {"entry", "entry.mjs"},
        {"requires", QJsonObject{{"hostVersion", ">=0.1.0 <1.0.0"},
                                 {"sdkVersion", ">=1.0.0 <2.0.0"},
                                 {"features", QJsonArray{"lifecycle", "playback.audioSource",
                                                         "lyrics.resolvers"}}}},
        {"capabilities", QJsonArray{"audioSource", "lyrics"}},
        {"contexts", QJsonArray{"main"}}};
    QFile manifestFile(source + "/manifest.json");
    QVERIFY(manifestFile.open(QIODevice::WriteOnly));
    manifestFile.write(QJsonDocument(manifest).toJson());
    manifestFile.close();
    QFile entry(source + "/entry.mjs");
    QVERIFY(entry.open(QIODevice::WriteOnly));
    const auto urlLiteral = QJsonDocument(QJsonArray{QUrl::fromLocalFile(wav).toString()})
                                .toJson(QJsonDocument::Compact)
                                .mid(1)
                                .chopped(1);
    entry.write("export function activate(c) { c.ui.setState({ticks:0,resolves:0});"
                "c.setTimer('clock',25,true,function(){c.ui.setState({ticks:c.ui.state.ticks+1,"
                "resolves:c.ui.state.resolves});});"
                "c.player.audioSource.registerResolver('local',{resolve:function(){"
                "c.ui.setState({resolves:c.ui.state.resolves+1,ticks:c.ui.state.ticks});return " +
                urlLiteral +
                ";}});"
                "c.lyrics.registerResolver('lines',{resolve:function(){return {format:'lrc',"
                "content:'[00:00]后台播放测试\\n[00:01.50]主窗口已释放，歌词继续显示'};}});}");
    entry.close();
    plugins.importDirectory(source);
    QPointer<EchoPluginContext> plugin;
    QTRY_VERIFY((plugin = plugins.context(plugins.installationForId("test.background"))));
    const auto installation = plugins.installationForId("test.background");
    QTRY_COMPARE(plugins.state(installation), PluginRuntime::Active);
    MainWindowHost host(
        session.engine, session.windows, shortcuts, [&] { return createMain(session, properties); },
        &session.router);
    QSignalSpy playbackClosed(&playback, &PlaybackService::closed);
    QSignalSpy pluginsClosed(&plugins, &PluginRuntime::closed);
    const auto cleanup = qScopeGuard([&] {
        host.shutdown();
        session.window = nullptr;
        session.auxiliary.reset();
        plugins.shutdown();
        if (!QTest::qWaitFor([&] { return !pluginsClosed.isEmpty(); }, 15000))
            QTest::qFail("Background plugins did not drain", __FILE__, __LINE__);
        playback.shutdown();
        if (!QTest::qWaitFor([&] { return !playbackClosed.isEmpty(); }, 15000))
            QTest::qFail("Background playback did not drain", __FILE__, __LINE__);
    });
    QTRY_VERIFY_WITH_TIMEOUT(playback.engineReady(), 15000);
    PlaybackTracks tracks;
    for (int i = 0; i < 3; ++i) {
        PlaybackTrack track;
        track.id = QString::number(i);
        track.title = QString("Background %1").arg(i);
        track.durationMs = 3000;
        track.catalogMetadata["plugin_source"] = "test.background/local";
        tracks.append(track);
    }
    QSet<QString> played;
    // Retire observation before its result container. Shutdown closes the
    // lyric dialog and drains events after these locals have been destroyed.
    QObject playedObservation;
    connect(&playback, &PlaybackService::changed, &playedObservation, [&] {
        if (playback.playing())
            played.insert(playback.currentTrack().id);
    });
    playback.playTracks(tracks, 0, "Background", "background-test");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
    QVERIFY(playback.sleepTimer()->start(30, "pause"));
    const auto deadline = playback.sleepTimer()->endTime();
    const auto sequence = playback.audioSequence();
    const auto revision = playback.sourceRevision();
    playback.ensureLyrics();
    QTRY_COMPARE(playback.lyrics()->rowCount(), 2);
    session.windows.floating()->setDesktopEnabled(true);
    QPointer<QQuickWindow> desktop;
    QTRY_VERIFY((desktop = session.windows.floating()->presentationWindow()));
    host.show();
    QPointer<QQuickWindow> main = host.window();
    host.unload();
    QTRY_VERIFY(main.isNull());
    // Local and native global bindings share this application-owned command
    // path; detaching the main window must not disconnect playback actions.
    shortcuts.triggered(ShortcutService::VolumeUp);
    QCOMPARE(playback.volume(), 5);
    shortcuts.triggered(ShortcutService::VolumeDown);
    QCOMPARE(playback.volume(), 0);
    QVERIFY(!host.window());
    QVERIFY(desktop && desktop->isVisible());
    QVERIFY(!desktop->transientParent());
    QCOMPARE(playback.audioSequence(), sequence);
    QCOMPARE(playback.sourceRevision(), revision);
    QVERIFY(captureSurface(desktop, "desktop-without-main"));
    const auto ticks = plugin->ui()->state().value("ticks").toInt();
    QTRY_VERIFY(plugin->ui()->state().value("ticks").toInt() > ticks + 2);
    QTRY_COMPARE_WITH_TIMEOUT(played.size(), 3, 20000);
    QVERIFY(plugin->ui()->state().value("resolves").toInt() >= 3);
    QCOMPARE(plugins.context(installation), plugin.data());
    QCOMPARE(plugins.state(installation), PluginRuntime::Active);
    QCOMPARE(playback.sleepTimer()->endTime(), deadline);
    playback.seek(1600);
    QTRY_COMPARE(playback.lyrics()->currentIndex(), 1);
    // Mini is created after the main window is already absent.
    session.windows.floating()->showMini();
    QPointer<QQuickWindow> mini;
    QTRY_VERIFY((mini = session.windows.floating()->miniWindow()));
    QVERIFY(mini->isVisible());
    QVERIFY(!mini->transientParent());
    QVERIFY(!host.window());
    QVERIFY(captureSurface(mini, "mini-without-main"));
    for (int i = 0; i < 5; ++i) {
        shortcuts.triggered(ShortcutService::ToggleWindow);
        QVERIFY(host.window());
        QVERIFY(desktop && desktop->isVisible());
        main = host.window();
        shortcuts.triggered(ShortcutService::ToggleWindow);
        QTRY_VERIFY(main.isNull());
        QCOMPARE(plugins.context(installation), plugin.data());
    }
    QSignalSpy selections(&session.windows, &WindowController::lyricSelectionRequested);
    session.windows.floating()->selectLyrics();
    QTRY_VERIFY(host.window());
    QTRY_COMPARE(selections.size(), 1);
    QPointer<QObject> selection;
    QTRY_VERIFY((selection = host.window()->findChild<QObject *>("lyricSourceDialog")));
    QTRY_VERIFY(selection->property("opened").toBool());
    QVERIFY(playbackClosed.isEmpty());
    QVERIFY(pluginsClosed.isEmpty());
}

void DesktopPagesTest::mainWindowPausedPlaybackResumesAfterOutputRelease()
{
    Session session;
    // Exercise the real packet file cache even though the fixture is local.
    session.preferences.setValue(SettingsKeys::PlayerEngine::CacheMode, "yes");
    session.preferences.setValue(SettingsKeys::PlayerEngine::CacheOnDisk, true);
    session.shell.acceptUserAgreement();
    session.shell.setMotionEnabled(false);
    session.api.addInterceptor(100, [](const QJsonObject &, ApiInterceptNext, ApiInterceptDone) {
        return std::function<void()>([] {});
    });
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("paused-background.sqlite"));
    playback.setVolume(0);
    playback.transition()->setMode("none");
    QVERIFY(session.create(&playback));
    const auto properties = detachMain(session);
    ShortcutService shortcuts(session.preferences, false);
    MainWindowHost host(
        session.engine, session.windows, shortcuts, [&] { return createMain(session, properties); },
        &session.router);
    QSignalSpy closed(&playback, &PlaybackService::closed);
    const auto cleanup = qScopeGuard([&] {
        host.shutdown();
        session.window = nullptr;
        session.auxiliary.reset();
        playback.shutdown();
        if (!QTest::qWaitFor([&] { return !closed.isEmpty(); }, 15000))
            QTest::qFail("Paused background playback did not drain", __FILE__, __LINE__);
    });
    QJsonObject cacheStats;
    int loadedSources = 0;
    int outputConfigurations = 0;
    QObject observation;
    connect(&PlaybackServiceTestAccess::engine(playback), &AudioEngine::eventReceived, &observation,
            [&](const QJsonObject &event) {
                const auto kind = event.value("event").toString();
                if (kind == "packet-cache-stats")
                    cacheStats = event.value("packet_cache").toObject();
                if (kind == "file-loaded")
                    ++loadedSources;
                if (kind == "output-configuration")
                    ++outputConfigurations;
            });
    const auto path = session.directory.filePath("paused-background.wav");
    QVERIFY(writeShortWave(path, 30));
    PlaybackTrack track;
    track.id = path;
    track.localPath = path;
    track.title = "Paused background";
    track.durationMs = 30000;
    playback.playTracks({track}, 0, "Background resume", "paused-background");
    QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
    QTRY_VERIFY(playback.positionMs() > 100);
    QTRY_VERIFY(cacheStats.value("eof").toBool());
    const auto cachedFileBytes = cacheStats.value("file_cache_bytes").toDouble();
    QVERIFY(cachedFileBytes > 0);
    QVERIFY(cacheStats.value("disk_cache_error").isNull());
    const auto sequence = playback.audioSequence();
    const auto revision = playback.sourceRevision();
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    // Compare the same released audio session with and without retiring the real
    // Main.qml tree. Rebuilding must not reload the source or replace its owner.
    for (const bool retireWindow : {false, true}) {
        host.show();
        QTRY_VERIFY(visual(host.window()->contentItem(), "shellFrame"));
        playback.pause();
        QTRY_VERIFY(!playback.playing());
        QPointer<QQuickWindow> previous = host.window();
        if (retireWindow) {
            host.unload();
            QTRY_VERIFY(previous.isNull());
            QVERIFY(closed.isEmpty());
        }
        QTest::qWait(3500); // Cross the production engine's three-second AO release.
        const auto pausedPosition = playback.positionMs();
        const auto previousOutputConfigurations = outputConfigurations;
        host.show();
        QTRY_VERIFY(visual(host.window()->contentItem(), "shellFrame"));
        QCOMPARE(host.window()->property("playbackService").value<PlaybackService *>(), &playback);
        QVERIFY(!playback.playing());
        QCOMPARE(playback.audioSequence(), sequence);
        QCOMPARE(playback.sourceRevision(), revision);
        playback.play();
        QTRY_VERIFY_WITH_TIMEOUT(playback.playing(), 15000);
        QCOMPARE(outputConfigurations, previousOutputConfigurations + 1);
        QTRY_VERIFY_WITH_TIMEOUT(playback.positionMs() > pausedPosition + 100, 5000);
        QCOMPARE(playback.audioSequence(), sequence);
        QCOMPARE(playback.sourceRevision(), revision);
        QCOMPARE(loadedSources, 1);
        QCOMPARE(cacheStats.value("file_cache_bytes").toDouble(), cachedFileBytes);
        QVERIFY(cacheStats.value("disk_cache_error").isNull());
        QVERIFY(playback.errorMessage().isEmpty());
        playback.pause();
        QTRY_VERIFY(!playback.playing());
        playback.seek(5000);
        QTRY_VERIFY(qAbs(playback.positionMs() - 5000) < 250);
        QCOMPARE(playback.audioSequence(), sequence);
        QCOMPARE(playback.sourceRevision(), revision);
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::mainWindowRecoveryRejectsStaleRetirementAndRetriesFailure()
{
    Session session;
    ShortcutService shortcuts(session.preferences, false);
    bool fail = true;
    int creations = 0;
    MainWindowHost host(session.engine, session.windows, shortcuts, [&]() -> QQuickWindow * {
        ++creations;
        return fail ? nullptr : new QQuickWindow;
    });
    QSignalSpy errors(&host, &MainWindowHost::creationFailed);
    host.show();
    QCOMPARE(errors.size(), 1);
    QCOMPARE(host.state(), MainWindowHost::State::Absent);
    QTest::qWait(30);
    QCOMPARE(creations, 1);
    fail = false;
    host.show();
    QPointer<QQuickWindow> first = host.window();
    QVERIFY(first);
    session.windows.toggleVisibility();
    session.windows.showWindow(); // A real foreground action cancels retirement.
    QTest::qWait(10);
    QCOMPARE(host.window(), first.data());
    QVERIFY(first->isVisible());
    host.unload();
    host.show(); // A newer show supersedes a queued unload.
    QTest::qWait(10);
    QCOMPARE(host.window(), first.data());
    bool requestedDuringRetirement = false;
    const auto reversal = connect(&host, &MainWindowHost::stateChanged, &host, [&] {
        if (host.state() == MainWindowHost::State::Retiring) {
            requestedDuringRetirement = true;
            host.show(); // Cannot create over a window awaiting DeferredDelete.
            QCOMPARE(creations, 2);
        }
    });
    host.unload();
    QTRY_VERIFY(first.isNull());
    QVERIFY(requestedDuringRetirement);
    QTRY_VERIFY(host.window());
    QCOMPARE(creations, 3);
    disconnect(reversal);
    QObject content;
    session.windows.beginContentFullscreen(&content);
    QTRY_VERIFY(session.windows.fullscreen());
    QPointer<QQuickWindow> fullscreen = host.window();
    session.windows.toggleVisibility();
    QTRY_VERIFY(fullscreen.isNull());
    host.show();
    QVERIFY(!session.windows.fullscreen());
    QCOMPARE(creations, 4);
    host.shutdown();
    host.show();
    host.unload();
    QTest::qWait(10);
    QCOMPARE(creations, 4);
    QCOMPARE(host.state(), MainWindowHost::State::Closed);
    int forbiddenCreations = 0;
    MainWindowHost interrupted(session.engine, session.windows, shortcuts, [&] {
        ++forbiddenCreations;
        return new QQuickWindow;
    });
    connect(&interrupted, &MainWindowHost::stateChanged, &interrupted, [&] {
        if (interrupted.state() == MainWindowHost::State::Creating)
            interrupted.shutdown();
    });
    interrupted.show();
    QCOMPARE(forbiddenCreations, 0);
    QCOMPARE(interrupted.state(), MainWindowHost::State::Closed);
}

void DesktopPagesTest::minimizedWindowRestoresPlaybackPage_data()
{
    QTest::addColumn<bool>("motion");
    QTest::addColumn<QString>("skin");
    for (const auto &skin : {QString("cover"), QString("portrait"), QString("lyric")})
        for (bool motion : {true, false})
            QTest::newRow(qPrintable(skin + (motion ? "-animated" : "-reduced"))) << motion << skin;
}

void DesktopPagesTest::minimizedWindowRestoresPlaybackPage()
{
    QFETCH(bool, motion);
    QFETCH(QString, skin);
    Session session(QGuiApplication::platformName() != "offscreen");
    session.shell.acceptUserAgreement();
    session.shell.setMotionEnabled(motion);
    session.theme.setMotionEnabled(motion);
    session.api.addInterceptor(100, [](const QJsonObject &, ApiInterceptNext, ApiInterceptDone) {
        return std::function<void()>([] {});
    });
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.theme.extensions()->chooseSkin(skin));
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    auto *owner = session.root->findChild<QObject *>("playbackPageOwner");
    QVERIFY(owner);
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
    QPointer<QObject> page;
    QTRY_VERIFY((page = owner->property("item").value<QObject *>()) &&
                page->property("opened").toBool());
    auto *route = session.router.current();
    QSignalSpy activated(&session.router, &RouteController::pageActivated);
    bool restoring = false;
    QList<bool> presentedFrames;
    const auto observation =
        connect(session.window, &QQuickWindow::afterAnimating, session.window, [&] {
            if (!restoring || session.window->visibility() == QWindow::Minimized)
                return;
            auto *current = owner->property("item").value<QObject *>();
            auto *pageMotion =
                current ? current->findChild<QObject *>("playbackPageMotion") : nullptr;
            const bool complete = session.root->property("playbackCoversShell").toBool() &&
                                  current && current->property("visible").toBool() && pageMotion &&
                                  pageMotion->property("hostOpacity").toReal() == 1 &&
                                  pageMotion->property("contentOpacity").toReal() == 1 &&
                                  !current->property("coverMoving").toBool();
            if (!complete && !presentedFrames.contains(false))
                qInfo() << "Incomplete restored frame"
                        << session.root->property("restoringPlaybackPresentation")
                        << session.root->property("playbackCoversShell") << current
                        << (current ? current->property("visible") : QVariant{})
                        << (current ? current->property("opened") : QVariant{})
                        << (current ? current->property("restoringPresentation") : QVariant{})
                        << (pageMotion ? pageMotion->property("hostOpacity") : QVariant{})
                        << (pageMotion ? pageMotion->property("contentOpacity") : QVariant{});
            presentedFrames.append(complete);
        });
    const auto stopObservation = qScopeGuard([&] { QObject::disconnect(observation); });
    for (int cycle = 0; cycle < 3; ++cycle) {
        restoring = false;
        session.window->showMinimized();
        QTRY_COMPARE(session.window->visibility(), QWindow::Minimized);
        // Also exercise a popup closed by the platform while minimized,
        // independently of the application retaining the playback-mode intent.
        if (cycle != 0)
            QVERIFY(QMetaObject::invokeMethod(page, "close"));
        if (cycle == 2)
            QTRY_VERIFY(!owner->property("item").value<QObject *>());
        else
            QTest::qWait(30);
        QVERIFY(session.windows.playbackPageOpen());
        qInfo() << "Minimized cycle" << cycle
                << session.root->property("restoringPlaybackPresentation")
                << owner->property("item");
        presentedFrames.clear();
        restoring = true;
        // A native taskbar restore changes window state without calling the
        // application's showWindow() path. Cover both restoration entries.
        if (cycle % 2)
            session.windows.showWindow();
        else
            session.window->showNormal();
        QTRY_COMPARE(session.window->visibility(), QWindow::Windowed);
        QVERIFY(Test::prepareNativeInput(session.window));
        session.window->update();
        QTRY_VERIFY(!presentedFrames.isEmpty());
        QTest::qWait(520);
        restoring = false;
        QVERIFY2(!presentedFrames.contains(false),
                 "A restored frame exposed the shell or replayed playback entry");
        QTRY_VERIFY((page = owner->property("item").value<QObject *>()) &&
                    page->property("opened").toBool());
        QCOMPARE(session.router.current(), route);
        QCOMPARE(activated.size(), 0);
    }
    // Minimize recovery must not suppress a later explicit user entry. Also
    // interrupt that ordinary entry and verify the next restore is settled.
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(page.isNull());
    QVERIFY(!session.windows.playbackPageOpen());
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
    QTRY_VERIFY((page = owner->property("item").value<QObject *>()));
    QVERIFY(!page->property("restoringPresentation").toBool());
    QTest::qWait(16);
    auto *pageMotion = page->findChild<QObject *>("playbackPageMotion");
    QVERIFY(pageMotion);
    if (motion)
        QVERIFY(page->property("coverMoving").toBool() ||
                pageMotion->property("contentOpacity").toReal() < 1);
    session.window->showMinimized();
    QTRY_COMPARE(session.window->visibility(), QWindow::Minimized);
    presentedFrames.clear();
    restoring = true;
    session.window->showNormal();
    QVERIFY(Test::prepareNativeInput(session.window));
    session.window->update();
    QTRY_VERIFY(!presentedFrames.isEmpty());
    QTest::qWait(520);
    restoring = false;
    QVERIFY2(!presentedFrames.contains(false), "Minimize resumed an interrupted playback entry");
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::backgroundRestoresPlaybackPage_data()
{
    QTest::addColumn<bool>("motion");
    QTest::addColumn<QString>("skin");
    for (const auto &skin : {QString("cover"), QString("portrait"), QString("lyric")})
        for (bool motion : {true, false})
            QTest::newRow(qPrintable(skin + (motion ? "-animated" : "-reduced"))) << motion << skin;
}

void DesktopPagesTest::backgroundRestoresPlaybackPage()
{
    QFETCH(bool, motion);
    QFETCH(QString, skin);
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    session.shell.acceptUserAgreement();
    session.shell.setMotionEnabled(motion);
    session.theme.setMotionEnabled(motion);
    session.api.addInterceptor(100, [](const QJsonObject &, ApiInterceptNext, ApiInterceptDone) {
        return std::function<void()>([] {});
    });
    PlaybackService playback(session.api, session.preferences);
    const auto selectedSkin = skin;
    QVERIFY(session.theme.extensions()->chooseSkin(selectedSkin));
    QVERIFY(session.create(&playback));
    const auto properties = detachMain(session);
    ShortcutService shortcuts(session.preferences, false);
    QList<bool> restoredFirstFrames;
    MainWindowHost host(
        session.engine, session.windows, shortcuts,
        [&] {
            auto *window = createMain(session, properties);
            if (window && session.windows.playbackPageOpen()) {
                connect(window, &QQuickWindow::afterAnimating, window,
                        [&, window, firstFrame = true]() mutable {
                            if (!firstFrame)
                                return;
                            firstFrame = false;
                            auto *page = window->findChild<QObject *>("playbackPage");
                            auto *motion =
                                page ? page->findChild<QObject *>("playbackPageMotion") : nullptr;
                            qInfo() << "Restored first frame"
                                    << window->property("restoringPlaybackPresentation")
                                    << window->property("playbackCoversShell") << "page" << page
                                    << "opened" << (page ? page->property("opened") : QVariant{})
                                    << "restoring"
                                    << (page ? page->property("restoringPresentation") : QVariant{})
                                    << "host opacity"
                                    << (motion ? motion->property("hostOpacity") : QVariant{})
                                    << "content opacity"
                                    << (motion ? motion->property("contentOpacity") : QVariant{});
                            restoredFirstFrames.append(
                                window->property("playbackCoversShell").toBool() && page &&
                                page->property("visible").toBool() && motion &&
                                motion->property("hostOpacity").toReal() == 1 &&
                                motion->property("contentOpacity").toReal() == 1 &&
                                !page->property("coverMoving").toBool());
                        });
            }
            return window;
        },
        &session.router);
    const auto cleanup = qScopeGuard([&] {
        host.shutdown();
        session.auxiliary.reset();
        session.window = nullptr;
    });
    host.show();
    QPointer<QObject> owner;
    QTRY_VERIFY((owner = host.window()->findChild<QObject *>("playbackPageOwner")));
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
    QPointer<QObject> page;
    QTRY_VERIFY((page = owner->property("item").value<QObject *>()) &&
                page->property("opened").toBool());
    auto *route = session.router.current();
    QSignalSpy activated(&session.router, &RouteController::pageActivated);
    for (int cycle = 0; cycle < 6; ++cycle) {
        QPointer<QQuickWindow> retired = host.window();
        if (cycle % 2)
            session.windows.floating()->showMini();
        else
            session.windows.toggleVisibility();
        QTRY_VERIFY(retired.isNull() && page.isNull() && owner.isNull());
        session.windows.showWindow();
        QTRY_VERIFY((owner = host.window()->findChild<QObject *>("playbackPageOwner")));
        QTRY_VERIFY_WITH_TIMEOUT((page = owner->property("item").value<QObject *>()) &&
                                     page->property("opened").toBool(),
                                 3000);
        QTRY_COMPARE(restoredFirstFrames.size(), cycle + 1);
        QVERIFY2(restoredFirstFrames.last(),
                 "The first restored frame exposed the shell or playback entry transition");
        QCOMPARE(session.router.current(), route);
        QCOMPARE(activated.size(), 0);
        QCOMPARE(session.theme.extensions()->selectedSkin(), selectedSkin);
        const auto capture = qEnvironmentVariable("ECHO_SKIN_SWITCH_CAPTURES");
        if (!capture.isEmpty() && cycle == 3) {
            QVERIFY(QDir().mkpath(capture));
            QVERIFY(QTest::qWaitForWindowExposed(host.window()));
            Test::FrameSpy rendered(host.window());
            host.window()->update();
            QVERIFY(rendered.wait(2000));
            QVERIFY(host.window()->grabWindow().save(
                capture + QString("/restored-playback-%1.png").arg(QTest::currentDataTag())));
        }
    }
    // Restoration applies to the first entry only. Reopening the same surface
    // during its exit must regain the ordinary user entry animation.
    QVERIFY(!page->property("restoringPresentation").toBool());
    if (motion) {
        QVERIFY(QMetaObject::invokeMethod(page, "close"));
        QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
        QTest::qWait(16);
        auto *pageMotion = page->findChild<QObject *>("playbackPageMotion");
        QVERIFY(pageMotion);
        QVERIFY(page->property("coverMoving").toBool() ||
                pageMotion->property("contentOpacity").toReal() < 1);
        QTRY_VERIFY(page->property("opened").toBool());
    }
    // Explicitly closing the playback page still returns to the ordinary shell.
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QPointer<QQuickWindow> retired = host.window();
    session.windows.toggleVisibility();
    QTRY_VERIFY(retired.isNull() && page.isNull());
    session.windows.showWindow();
    QTRY_VERIFY((owner = host.window()->findChild<QObject *>("playbackPageOwner")));
    QTest::qWait(100);
    QVERIFY(!owner->property("item").value<QObject *>());
    // A toggle command while absent changes the remembered mode exactly once.
    session.windows.requestPlaybackPage();
    QTRY_VERIFY((page = owner->property("item").value<QObject *>()) &&
                page->property("opened").toBool());
    retired = host.window();
    session.windows.toggleVisibility();
    QTRY_VERIFY(retired.isNull() && page.isNull());
    session.windows.requestPlaybackPage();
    QTRY_VERIFY((owner = host.window()->findChild<QObject *>("playbackPageOwner")));
    QTest::qWait(100);
    QVERIFY(!owner->property("item").value<QObject *>());
    QCOMPARE(warnings.size(), 0);
}
