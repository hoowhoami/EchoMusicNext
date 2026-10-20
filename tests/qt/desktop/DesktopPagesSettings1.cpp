#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::startupUsesRealFailuresAndReleasesItsEntirePage()
{
    const auto artifacts = qEnvironmentVariable("ECHO_STARTUP_ARTIFACTS");
    if (!artifacts.isEmpty())
        QVERIFY(QDir().mkpath(artifacts));
    for (const bool deviceFailure : {false, true}) {
        Session session;
        if (!deviceFailure) {
            auto identity = session.api.deviceIdentitySnapshot();
            identity.dfid = "stored-device-state-fixture";
            QVERIFY(session.api.setDeviceIdentity(identity));
        }
        NetworkPreferences network(session.preferences, session.api);
        if (deviceFailure) {
            network.apply("fixed_servers", {}, "http://127.0.0.1:1", {}, {}, false, {}, 1, 1);
            QTRY_VERIFY(!network.busy());
            bool applied = false;
            network.afterPendingMediaWork([&] { applied = true; });
            QTRY_VERIFY(applied);
        }
        PlaybackService playback(session.api, session.preferences, nullptr,
                                 session.directory.filePath("playback.sqlite"), &network);
        StorageService storage;
        DeviceRegistrationService device(session.api, session.preferences);
        const auto blockerPath = session.directory.filePath("blocked");
        QFile blocker(blockerPath);
        if (!deviceFailure) {
            QVERIFY(blocker.open(QIODevice::WriteOnly));
            blocker.close();
        }
        StartupController startup(storage, session.api, device, playback, network,
                                  session.accounts.saved,
                                  deviceFailure ? session.directory.filePath("music.sqlite")
                                                : blockerPath + "/music.sqlite");
        QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
        QVERIFY(session.create(&playback, &network, nullptr, nullptr, &startup));
        const auto cleanup = qScopeGuard([&session] {
            session.root.reset();
            session.window = nullptr;
        });
        session.window->resize(1150, 750);
        QVERIFY(Test::prepareNativeInput(session.window));
        QPointer<QQuickItem> page = visual(session.window->contentItem(), "startupPage");
        QVERIFY(page);
        QVERIFY(!visual(session.window->contentItem(), "shellFrame"));
        auto *glow = visual(page, "startupGlowLoader");
        QVERIFY(glow);
        QTRY_VERIFY(glow->property("item").value<QObject *>());
        session.window->hide();
        QTRY_VERIFY(!glow->property("item").value<QObject *>());
        session.window->show();
        QVERIFY(Test::prepareNativeInput(session.window));
        if (!artifacts.isEmpty())
            QVERIFY(session.window->grabWindow().save(artifacts + "/loading.png"));
        startup.start();
        QTRY_VERIFY_WITH_TIMEOUT(startup.failed(), 15000);
        QCOMPARE(startup.canForceEnter(), deviceFailure);
        auto *force = visual(page, "startupForceEnter");
        auto *retry = visual(page, "startupRetry");
        auto *exit = visual(page, "startupExit");
        QVERIFY(force && retry && exit);
        QCOMPARE(force->isVisible(), deviceFailure);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (int zoom : {100, 125}) {
                session.shell.resetZoom();
                for (int i = 100; i < zoom; i += 5)
                    session.shell.zoomIn();
                QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                session.window->update();
                QTRY_VERIFY(!frames.isEmpty());
                // A nested text maximum must not left-align the entire
                // error group against the wider centered logo column.
                auto *mark = visual(page, "startupMark");
                QVERIFY(mark);
                const auto centerX = mark->mapRectToScene(mark->boundingRect()).center().x();
                QVERIFY(qAbs(centerX - session.window->width() / 2.) < 0.5);
                for (const auto *name :
                     {"startupErrorGroup", "startupErrorIcon", "startupErrorHeading",
                      "startupErrorMessage", "startupErrorActions"}) {
                    auto *item = visual(page, name);
                    QVERIFY(item);
                    const auto actual = item->mapRectToScene(item->boundingRect()).center().x();
                    QVERIFY2(qAbs(actual - centerX) < 0.5, name);
                }
                const auto retryBounds = retry->mapRectToScene(retry->boundingRect());
                QVERIFY(retryBounds.left() >= 0 && retryBounds.right() <= session.window->width());
                if (!artifacts.isEmpty())
                    QVERIFY(session.window->grabWindow().save(
                        artifacts + QString("/%1-%2-%3.png")
                                        .arg(deviceFailure ? "device-failure" : "storage-failure")
                                        .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                        .arg(zoom)));
            }
        }
        auto *action = deviceFailure ? force : retry;
        if (!deviceFailure)
            QVERIFY(blocker.remove());
        const auto point =
            action->mapToScene({action->width() / 2, action->height() / 2}).toPoint();
        Test::moveNativePointer(session.window, point);
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY_WITH_TIMEOUT(startup.entered(), 15000);
        QTRY_VERIFY(page.isNull());
        QTRY_VERIFY(visual(session.window->contentItem(), "shellFrame"));
        startup.shutdown();
        QVERIFY(startup.entered());
        QVERIFY(!visual(session.window->contentItem(), "startupPage"));
        QCOMPARE(warnings.size(), 0);
        QSignalSpy storageClosed(&storage, &StorageService::closed);
        storage.shutdown();
        QTRY_COMPARE(storageClosed.size(), 1);
        device.shutdown();
        playback.shutdown();
    }
}

void DesktopPagesTest::recoveryStartupWindowRetriesAndExitsWithoutBusinessOwners()
{
    QTemporaryDir directory;
    QSettings preferences(directory.filePath("settings.ini"), QSettings::IniFormat);
    ThemeStore theme(preferences);
    ShellSettings shell(preferences);
    WindowController windows(preferences, false);
    StartupController startup(QStringLiteral("restore transaction blocked"));
    QQmlEngine engine;
    theme.attachToEngine(engine);
    QQmlComponent component(&engine, QUrl("qrc:/qt/qml/EchoMusic/App/StartupWindow.qml"));
    QScopedPointer<QObject> root(
        component.createWithInitialProperties({{"startup", QVariant::fromValue(&startup)},
                                               {"windowService", QVariant::fromValue(&windows)},
                                               {"shellSettings", QVariant::fromValue(&shell)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.data());
    QVERIFY(window);
    windows.attach(window);
    QVERIFY(Test::prepareNativeInput(window));
    startup.start();
    QTRY_VERIFY(startup.failed());
    QSignalSpy retries(&startup, &StartupController::restartApplicationRequested);
    auto *retry = visual(window->contentItem(), "startupRetry");
    QVERIFY(retry);
    retry->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Space);
    QCOMPARE(retries.size(), 1);
    startup.reportRestartFailure(QStringLiteral("helper launch failed"));
    QVERIFY(startup.failed());
    QSignalSpy quit(&startup, &StartupController::quitRequested);
    auto *exit = visual(window->contentItem(), "startupExit");
    QVERIFY(exit);
    exit->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Space);
    QCOMPARE(quit.size(), 1);
    startup.shutdown();
    windows.shutdown();
    window->close();
}

void DesktopPagesTest::settingsSearchClearRestoresCategoriesAndKeepsFocus()
{
    Session session;
    NetworkPreferences network(session.preferences, session.api);
    QVERIFY(session.create(nullptr, &network));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.theme.setMotionEnabled(false);
    session.shell.setMotionEnabled(false);
    session.window->resize(1250, 900);
    const auto artifacts = qEnvironmentVariable("ECHO_TEXT_FIELD_ARTIFACTS");
    if (!artifacts.isEmpty())
        QVERIFY(QDir().mkpath(artifacts));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const int zoom : {100, 125}) {
            session.shell.resetZoom();
            for (int step = 100; step < zoom; step += 5)
                session.shell.zoomIn();
            QVERIFY(Test::prepareNativeInput(session.window));
            session.router.navigate("settings");
            QPointer<QObject> dialog = session.root->findChild<QObject *>("settingsDialog");
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->property("opened").toBool());
            auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
            QVERIFY(catalog);
            const auto fullCount = catalog->rowCount();
            QVERIFY(fullCount > 1);
            auto *trigger = visual(session.window->contentItem(), "settingsSearchTrigger");
            QVERIFY(trigger);
            trigger->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(session.window, Qt::Key_Space);
            auto *input = visual(session.window->contentItem(), "settingsSearchInput");
            QVERIFY(input);
            QTRY_VERIFY(input->isVisible() && input->hasActiveFocus());
            auto *clear = visual(input, "clearTextAction");
            QVERIFY(clear);
            QVERIFY(!clear->isVisible());
            for (const auto key : QByteArray("bypass"))
                QTest::keyClick(session.window, key);
            QTRY_COMPARE(catalog->filter(), QString("bypass"));
            QCOMPARE(catalog->rowCount(), 1);
            QTRY_VERIFY(clear->isVisible());
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
            const auto capture = [&](const QString &phase) {
                return artifacts.isEmpty() ||
                       session.window->grabWindow().save(
                           artifacts + QString("/settings-search-%1-%2-%3.png")
                                           .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                           .arg(zoom)
                                           .arg(phase));
            };
            QVERIFY(capture("filtered"));
            const auto point =
                clear->mapToScene({clear->width() / 2, clear->height() / 2}).toPoint();
            Test::moveNativePointer(session.window, point);
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_COMPARE(catalog->filter(), QString());
            QCOMPARE(catalog->rowCount(), fullCount);
            QCOMPARE(input->property("text").toString(), QString());
            QVERIFY(input->isVisible() && input->hasActiveFocus());
            QVERIFY(!clear->isVisible());
            if (!artifacts.isEmpty()) {
                frames.clear();
                session.window->update();
                QTRY_VERIFY(!frames.isEmpty());
                QVERIFY(capture("cleared"));
            }
            for (const auto key : QByteArray("font"))
                QTest::keyClick(session.window, key);
            QTRY_COMPARE(catalog->filter(), QString("font"));
            QVERIFY(catalog->rowCount() < fullCount);
            QTest::keyClick(session.window, Qt::Key_Escape);
            QTRY_COMPARE(catalog->filter(), QString());
            QTRY_VERIFY(!input->isVisible());
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(dialog.isNull());
        }
    }
}

void DesktopPagesTest::lyricFontSettingsApplyToProductionWindows()
{
    Session session;
    session.initialRoute = "themes";
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
    QString family;
    for (const auto &candidate : {"Courier New", "DejaVu Sans Mono", "Liberation Mono", "Arial"}) {
        if (QFontDatabase::hasFamily(candidate)) {
            family = candidate;
            break;
        }
    }
    QVERIFY(!family.isEmpty());
    auto *owner = session.root->findChild<QObject *>("settingsDialogOwner");
    QVERIFY(owner);
    const auto captures = qEnvironmentVariable("ECHO_LYRIC_FONT_CAPTURES");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const int zoom : {100, 125}) {
            session.shell.resetZoom();
            for (int step = 100; step < zoom; step += 5)
                session.shell.zoomIn();
            const QVariantMap properties{{"initialSection", "font"}};
            QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, properties)));
            QPointer<QObject> dialog = owner->property("item").value<QObject *>();
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->property("opened").toBool());
            QQuickItem *globalReset = nullptr;
            QTRY_VERIFY(
                (globalReset = visual(session.window->contentItem(), "settingsGlobalFontReset")));
            QVERIFY(!globalReset->isVisible());
            session.theme.setGlobalFont(family);
            QTRY_VERIFY(globalReset->isVisible() && globalReset->width() > 20);
            QVERIFY(Test::prepareNativeInput(session.window));
            QSignalSpy resetFrames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!resetFrames.isEmpty());
            QTest::mouseClick(
                session.window, Qt::LeftButton, Qt::NoModifier,
                globalReset->mapToScene({globalReset->width() / 2, globalReset->height() / 2})
                    .toPoint());
            QTRY_COMPARE(session.theme.globalFont(), QString("system-ui"));
            QTRY_VERIFY(!globalReset->isVisible());
            for (const auto &name :
                 {"settingsLyricFont", "settingsDesktopLyricFont", "settingsMiniLyricFont"}) {
                QQuickItem *select = nullptr;
                QTRY_VERIFY((select = visual(session.window->contentItem(), name)));
                QTRY_VERIFY(select->isVisible() && select->width() > 100);
                QCOMPARE(select->property("currentIndex").toInt(), 0);
                QVERIFY(!select->property("popup").value<QObject *>());
                QVERIFY(Test::prepareNativeInput(session.window));
                QTest::mouseClick(
                    session.window, Qt::LeftButton, Qt::NoModifier,
                    select->mapToScene({select->width() / 2, select->height() / 2}).toPoint());
                QPointer<QObject> popup;
                QTRY_VERIFY((popup = select->property("popup").value<QObject *>()));
                QTRY_VERIFY(popup->property("opened").toBool());
                QInputMethodEvent familyInput;
                familyInput.setCommitString(family);
                QVERIFY(session.window->activeFocusItem());
                QCoreApplication::sendEvent(session.window->activeFocusItem(), &familyInput);
                const auto sourceIndex = session.theme.fontFamilies().indexOf(family) + 2;
                QQuickItem *option = nullptr;
                QTRY_VERIFY((option = visual(session.window->contentItem(),
                                             "selectOption-" + QString::number(sourceIndex))));
                QTRY_VERIFY(option->isVisible());
                QTest::mouseClick(
                    session.window, Qt::LeftButton, Qt::NoModifier,
                    option->mapToScene({option->width() / 2, option->height() / 2}).toPoint());
                QTRY_VERIFY(popup.isNull());
                QTRY_COMPARE(select->property("currentText").toString(), family);
            }
            QCOMPARE(session.theme.lyricFont(), family);
            QCOMPARE(session.theme.desktopLyricFont(), family);
            QCOMPARE(session.theme.miniLyricFont(), family);
            if (!captures.isEmpty()) {
                QTest::qWait(100);
                QVERIFY(session.window->grabWindow().save(
                    captures + QString("/settings-%1-%2.png")
                                   .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                   .arg(zoom)));
            }
            for (const auto &name : {"settingsLyricFontReset", "settingsDesktopLyricFontReset",
                                     "settingsMiniLyricFontReset"}) {
                auto *reset = visual(session.window->contentItem(), name);
                QVERIFY(reset && reset->isVisible());
                auto *viewport = visual(session.window->contentItem(), "settingsContentViewport");
                QVERIFY(viewport);
                auto *content = viewport->property("contentItem").value<QQuickItem *>();
                QVERIFY(content);
                const auto bounds = reset->mapRectToItem(content, reset->boundingRect());
                const auto maximum =
                    qMax(0., viewport->property("contentHeight").toReal() - viewport->height());
                viewport->setProperty("contentY",
                                      qBound(0., bounds.bottom() - viewport->height(), maximum));
                QVERIFY(session.clickItem(QString::fromLatin1(name)));
            }
            QCOMPARE(session.theme.lyricFont(), QString("follow"));
            QCOMPARE(session.theme.desktopLyricFont(), QString("follow"));
            QCOMPARE(session.theme.miniLyricFont(), QString("follow"));
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(dialog.isNull());
        }
    }
    PlaybackServiceTestAccess::quality(playback, "flac");
    playback.lyricText()->setWantTranslation(true);
    QVERIFY(playback.lyrics()->replace(
        {{"lines", QJsonArray{QJsonObject{{"text", QStringLiteral("夜色温柔 Music Lyrics")},
                                          {"start_ms", 0},
                                          {"duration_ms", 5000},
                                          {"translation", "Stay close to the music"},
                                          {"words", QJsonArray{}}}}}}));
    session.theme.setGlobalFont(family);
    session.theme.setLyricFont("system-ui");
    session.theme.setDesktopLyricFont(family);
    session.theme.setMiniLyricFont("system-ui");
    auto *pageOwner = session.root->findChild<QObject *>("playbackPageOwner");
    QVERIFY(pageOwner);
    QVERIFY(QMetaObject::invokeMethod(pageOwner, "open", Q_ARG(QVariant, QVariant(QVariantMap{}))));
    QQuickItem *pageLyrics = nullptr;
    QTRY_VERIFY((pageLyrics = visual(session.window->contentItem(), "playbackLyricViewport")));
    QTRY_COMPARE(pageLyrics->property("baseFont").value<QFont>().families(),
                 uiFont("system-ui").families());
    const auto findWindow = [](const QString &name) -> QQuickWindow * {
        for (auto *candidate : QGuiApplication::topLevelWindows())
            if (candidate->objectName() == name)
                return qobject_cast<QQuickWindow *>(candidate);
        return nullptr;
    };
    const auto lyricFontsMatch = [](QQuickItem *root, const QFont &expected) {
        int count = 0;
        bool matches = true;
        const auto visit = [&](auto &&self, QQuickItem *item) -> void {
            const auto font = item->property("font");
            if (item->isVisible() && font.canConvert<QFont>() &&
                !item->property("text").toString().isEmpty()) {
                ++count;
                matches = matches && font.value<QFont>().families() == expected.families();
            }
            for (auto *child : item->childItems())
                self(self, child);
        };
        visit(visit, root);
        return count > 0 && matches;
    };
    QTRY_VERIFY(lyricFontsMatch(pageLyrics, uiFont("system-ui")));
    QTRY_VERIFY(pageOwner->property("item").value<QObject *>()->property("opened").toBool());
    if (!captures.isEmpty()) {
        QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
        session.window->update();
        QTRY_VERIFY(!frames.isEmpty());
        QVERIFY(session.window->grabWindow().save(captures + "/playback.png"));
    }
    session.theme.setLyricFont(family);
    QTRY_VERIFY(lyricFontsMatch(pageLyrics, uiFont(family)));
    session.theme.setLyricFont("system-ui");
    QTRY_VERIFY(lyricFontsMatch(pageLyrics, uiFont("system-ui")));
    auto *floating = session.windows.floating();
    floating->setDesktopEnabled(true);
    QTRY_VERIFY(findWindow("desktopLyricWindow"));
    QPointer<QQuickWindow> desktop = findWindow("desktopLyricWindow");
    auto *desktopLyrics = visual(desktop->contentItem(), "desktopLyricViewport");
    QVERIFY(desktopLyrics);
    QTRY_COMPARE(desktopLyrics->property("baseFont").value<QFont>().families(),
                 uiFont(family).families());
    QTRY_VERIFY(lyricFontsMatch(desktopLyrics, uiFont(family)));
    floating->showMini();
    floating->setPanel("lyric");
    QTRY_VERIFY(findWindow("miniPlayerWindow"));
    QPointer<QQuickWindow> mini = findWindow("miniPlayerWindow");
    QQuickItem *miniLyrics = nullptr;
    QTRY_VERIFY((miniLyrics = visual(mini->contentItem(), "miniLyricViewport")));
    QTRY_COMPARE(miniLyrics->property("baseFont").value<QFont>().families(),
                 uiFont("system-ui").families());
    session.theme.setMiniLyricFont(family);
    QTRY_COMPARE(miniLyrics->property("baseFont").value<QFont>().families(),
                 uiFont(family).families());
    QTRY_VERIFY(lyricFontsMatch(miniLyrics, uiFont(family)));
    session.theme.setGlobalFont("system-ui");
    QCOMPARE(desktopLyrics->property("baseFont").value<QFont>().families(),
             uiFont(family).families());
    if (!captures.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(desktop->grabWindow().save(captures + "/desktop.png"));
        QVERIFY(mini->grabWindow().save(captures + "/mini.png"));
    }
    floating->closeMini();
    floating->setDesktopEnabled(false);
    QTRY_VERIFY(mini.isNull() && desktop.isNull());
    floating->showMini();
    floating->setPanel("lyric");
    QTRY_VERIFY(findWindow("miniPlayerWindow"));
    QTRY_VERIFY(
        (miniLyrics = visual(findWindow("miniPlayerWindow")->contentItem(), "miniLyricViewport")));
    QTRY_COMPARE(miniLyrics->property("baseFont").value<QFont>().families(),
                 uiFont(family).families());
    session.theme.setMiniLyricFont("follow");
    session.theme.setGlobalFont(family);
    QTRY_VERIFY(lyricFontsMatch(miniLyrics, session.theme.applicationFont()));
    session.theme.setGlobalFont("system-ui");
    QTRY_VERIFY(lyricFontsMatch(miniLyrics, uiFont("system-ui")));
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::lyricVariantSettingUsesProductionSelectAndReleasesPopup()
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
    session.window->resize(1250, 900);
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *owner = session.root->findChild<QObject *>("settingsDialogOwner");
    QVERIFY(owner);
    const auto artifacts = qEnvironmentVariable("ECHO_VARIANT_ARTIFACTS");
    if (!artifacts.isEmpty())
        QVERIFY(QDir().mkpath(artifacts));
    for (const auto mode : {ThemeStore::Dark, ThemeStore::Light}) {
        session.theme.setMode(mode);
        for (const int zoom : {100, 125}) {
            session.shell.resetZoom();
            for (int step = 100; step < zoom; step += 5)
                session.shell.zoomIn();
            const QVariantMap properties{{"initialSection", "lyric"}};
            QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, properties)));
            QPointer<QObject> dialog = owner->property("item").value<QObject *>();
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->property("opened").toBool());
            auto *sectionLoader = visual(session.window->contentItem(), "settingsSectionLoader");
            QVERIFY(sectionLoader);
            // Asynchronous Loader children exist before their layout is ready for input.
            QTRY_COMPARE_WITH_TIMEOUT(sectionLoader->property("status").toInt(), 1, 15000);
            QQuickItem *select = nullptr;
            QTRY_VERIFY((select = visual(session.window->contentItem(), "lyricTextConversion")));
            QTRY_VERIFY(select->isVisible());
            QVERIFY(!select->property("popup").value<QObject *>());
            playback.lyricText()->setConversion("traditional");
            QTRY_COMPARE(select->property("currentIndex").toInt(), 2);
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
            auto *viewport = visual(session.window->contentItem(), "settingsContentViewport");
            QVERIFY(viewport);
            const auto local = select->mapToItem(viewport, {0, 0});
            if (local.y() < 0 || local.y() + select->height() > viewport->height()) {
                const auto maximum =
                    qMax(0., viewport->property("contentHeight").toDouble() - viewport->height());
                viewport->setProperty("contentY", qBound(0.,
                                                         viewport->property("contentY").toDouble() +
                                                             local.y() - viewport->height() / 2,
                                                         maximum));
            }
            QVERIFY(Test::prepareNativeInput(session.window));
            const auto point =
                select->mapToScene({select->width() / 2, select->height() / 2}).toPoint();
            Test::moveNativePointer(session.window, point);
            if (!artifacts.isEmpty())
                QVERIFY(
                    session.window->grabWindow().save(artifacts + "/settings-before-select.png"));
            qInfo() << "Conversion select input" << point << "size" << select->size();
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
            QPointer<QObject> popup;
            QTRY_VERIFY((popup = select->property("popup").value<QObject *>()));
            QTRY_VERIFY(popup->property("opened").toBool());
            auto *list = select->property("optionList").value<QObject *>();
            QVERIFY(list);
            QTRY_COMPARE(list->property("count").toInt(), 6);
            QQuickItem *option = nullptr;
            QTRY_VERIFY((option = visual(session.window->contentItem(), "selectOption-3")) &&
                        option->isVisible());
            if (!artifacts.isEmpty()) {
                QTest::qWait(100);
                QVERIFY(session.window->grabWindow().save(
                    artifacts + QString("/variants-%1-%2.png")
                                    .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                    .arg(zoom)));
            }
            QTest::mouseClick(
                session.window, Qt::LeftButton, Qt::NoModifier,
                option->mapToScene({option->width() / 2, option->height() / 2}).toPoint());
            QTRY_COMPARE(playback.lyricText()->conversion(),
                         QStringLiteral("traditional-variants"));
            QTRY_COMPARE(select->property("currentIndex").toInt(), 3);
            QCOMPARE(select->property("currentText").toString(), QStringLiteral("转异体繁体"));
            QCOMPARE(session.preferences.value("lyrics/conversion").toString(),
                     QStringLiteral("traditional-variants"));
            QTRY_VERIFY(popup.isNull());
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(dialog.isNull());
        }
    }
    QCOMPARE(warnings.size(), 0);
    playback.shutdown();
}

void DesktopPagesTest::miniWidthPreferencesClampAndRestore_data()
{
    QTest::addColumn<int>("savedWidth");
    QTest::addColumn<int>("expectedWidth");
    QTest::newRow("below-minimum") << 200 << 360;
    QTest::newRow("within-range") << 480 << 480;
    QTest::newRow("above-maximum") << 900 << 560;
}

void DesktopPagesTest::miniWidthPreferencesClampAndRestore()
{
    QFETCH(int, savedWidth);
    QFETCH(int, expectedWidth);
    QTemporaryDir directory;
    QSettings settings(directory.filePath("mini.ini"), QSettings::IniFormat);
    const auto area = QGuiApplication::primaryScreen()->availableGeometry();
    settings.setValue("floating/mini/anchor",
                      QRect(area.topLeft() + QPoint(40, 40), QSize(savedWidth, 64)));
    {
        QQuickWindow mini;
        FloatingPlayerController floating(settings, true);
        floating.toggleMini();
        floating.attachMini(&mini);
        QTRY_COMPARE(mini.width(), expectedWidth);
        QCOMPARE(mini.height(), 64);
        floating.setPanel("lyric");
        QTRY_COMPARE(mini.height(), 360);
        QCOMPARE(mini.width(), expectedWidth);
        floating.setPanel("");
        QTRY_COMPARE(mini.height(), 64);
        QCOMPARE(mini.width(), expectedWidth);
        mini.setWidth(500);
        floating.attachMini(nullptr);
        floating.shutdown();
    }
    QCOMPARE(settings.value("floating/mini/anchor").toRect().width(), 500);
    {
        QQuickWindow mini;
        FloatingPlayerController floating(settings, true);
        floating.toggleMini();
        floating.attachMini(&mini);
        QTRY_COMPARE(mini.width(), 500);
        QCOMPARE(mini.height(), 64);
        floating.attachMini(nullptr);
        floating.shutdown();
    }
}

void DesktopPagesTest::lyricSettingsShareCoverPickerAndExcludeTaskbar()
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
    // 弹层按需创建：先经真实入口打开设置，再取得实例。
    session.router.navigate("settings");
    auto *dialog = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "openSection", Q_ARG(QVariant, QString("lyric"))));
    auto *loader = visual(session.window->contentItem(), "settingsSectionLoader");
    QVERIFY(loader);
    QTRY_VERIFY(loader->property("item").value<QObject *>());
    auto *section = qobject_cast<QQuickItem *>(loader->property("item").value<QObject *>());
    QVERIFY(section);
    QTRY_VERIFY(section->implicitHeight() > 1000);
    QSet<QString> headings;
    const auto inspect = [&headings](auto &&self, QObject *object) -> void {
        const auto title = object->property("heading").toString();
        if (!title.isEmpty())
            headings.insert(title);
        for (auto *child : object->children())
            self(self, child);
    };
    inspect(inspect, section);
    for (const auto &title : {"通用", "播放页", "桌面歌词", "Mini 歌词", "显示翻译", "显示音译",
                              "歌词过滤", "下一行预览", "主歌词字号"})
        QVERIFY2(headings.contains(QString::fromUtf8(title)), title);
    QVERIFY(!headings.contains(QStringLiteral("任务栏歌词")));
    const auto visibleItem = [&session](const QString &name) -> QQuickItem * {
        for (auto *item : visualAll(session.window->contentItem(), name))
            if (item->isVisible())
                return item;
        return nullptr;
    };
    auto fields = visualAll(section, "lyricPlayedColor");
    QCOMPARE(fields.size(), 3);
    QList<QObject *> owners{playback.lyricAppearance(), session.windows.floating()->desktop(),
                            session.windows.floating()->mini()};
    const auto capture = qEnvironmentVariable("ECHO_FLOATING_CAPTURE_DIR");
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (double scale : {1., 1.25}) {
            session.shell.resetZoom();
            while (session.shell.uiScale() < scale - .001)
                session.shell.zoomIn();
            for (int i = 0; i < fields.size(); ++i) {
                owners[i]->setProperty("followCoverColor", false);
                QVERIFY(QMetaObject::invokeMethod(fields[i], "clicked"));
                QPointer<QQuickItem> dynamic;
                QTRY_VERIFY((dynamic = visibleItem("colorDynamicAction")) && dynamic->isVisible());
                auto *swatch = visual(dynamic.data(), "colorDynamicSwatch");
                QVERIFY(swatch);
                QTRY_VERIFY(std::abs(swatch->width() - 34) < .01 &&
                            std::abs(swatch->height() - 34) < .01);
                auto *description = visual(dynamic.data(), "colorDynamicDescription");
                QVERIFY(description);
                QCOMPARE(description->property("text").toString(),
                         QStringLiteral("自动使用当前封面提取色"));
                QVERIFY(QMetaObject::invokeMethod(dynamic.data(), "clicked"));
                QVERIFY(!owners[i]->property("followCoverColor").toBool());
                QTest::qWait(35);
                if (i == 0 && !capture.isEmpty())
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/cover-picker-%1-%2.png")
                                      .arg(mode == ThemeStore::Light ? "light" : "dark")
                                      .arg(scale)));
                QQuickItem *cancel = nullptr;
                QTRY_VERIFY((cancel = visibleItem("colorCancelAction")));
                QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
                QTRY_VERIFY(!dynamic || !dynamic->isVisible());
                QVERIFY(!owners[i]->property("followCoverColor").toBool());
                QVERIFY(QMetaObject::invokeMethod(fields[i], "clicked"));
                QTRY_VERIFY((dynamic = visibleItem("colorDynamicAction")) && dynamic->isVisible());
                QVERIFY(QMetaObject::invokeMethod(dynamic.data(), "clicked"));
                QQuickItem *confirm = nullptr;
                QTRY_VERIFY((confirm = visibleItem("colorConfirmAction")));
                QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
                QTRY_VERIFY(owners[i]->property("followCoverColor").toBool());
                QTRY_VERIFY(!dynamic || !dynamic->isVisible());
            }
        }
    }
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::fullscreenShortcutReachesModalPlaybackPage()
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
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *pageOwner = session.root->findChild<QObject *>("playbackPageOwner");
    QVERIFY(pageOwner);
    QVERIFY(QMetaObject::invokeMethod(pageOwner, "open", Q_ARG(QVariant, QVariant(QVariantMap{}))));
    auto *page = pageOwner->property("item").value<QObject *>();
    QTRY_VERIFY(page && page->property("opened").toBool());
    // The modal playback page owns focus; the window-level shortcut scope
    // would stop matching there even though the shell is still the host.
    if (!session.windows.macOS()) {
        QTest::keyClick(session.window, Qt::Key_F11);
        QTRY_VERIFY(session.windows.fullscreen());
        QTest::keyClick(session.window, Qt::Key_F11);
        QTRY_VERIFY(!session.windows.fullscreen());
    }
    QVERIFY(pageOwner->property("item").value<QObject *>());
    QCOMPARE(warnings.size(), 0);
    QMetaObject::invokeMethod(page, "close");
    QTRY_VERIFY(pageOwner->property("item").isNull());
}

void DesktopPagesTest::productionExperimentalIdentitySettings_data()
{
    productionNetworkSettingsDraftApplyResetAndTheme_data();
}

void DesktopPagesTest::productionExperimentalIdentitySettings()
{
    QFETCH(bool, dark);
    QFETCH(qreal, scale);
    Session session;
    QVERIFY(session.api.setNetworkTransport(1)); // Keep synthetic diagnostics offline.
    RelaunchService relaunch({}, {}, false);
    ExperimentalSettingsService experimental(session.api, session.accounts.account,
                                             session.accounts.saved, session.preferences, relaunch);
    QVERIFY(session.accounts.account.adoptLoginAuthorization(QByteArrayLiteral(
        "{\"user_id\":42,\"token\":\"synthetic-visible-token\",\"refresh_credential\":"
        "\"synthetic-visible-t1\",\"nickname\":\"实验室测试账号\"}")));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(nullptr, nullptr, nullptr, &relaunch, nullptr, &experimental));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    session.theme.setFloatingFrosted(false);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    for (int step = 0; step < qRound((scale - 1) / .05); ++step)
        session.shell.zoomIn();
    session.router.navigate("settings");
    auto *settings = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(settings);
    QTRY_VERIFY(settings->property("opened").toBool());
    auto *catalog = settings->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    catalog->setFilter(QStringLiteral("鉴权头"));
    QCOMPARE(catalog->count(), 1);
    QCOMPARE(catalog->current(), QString("experimental"));
    auto *viewport = visual(session.window->contentItem(), "settingsContentViewport");
    QVERIFY(viewport);
    QQuickItem *view = nullptr;
    QTRY_VERIFY((view = visual(session.window->contentItem(), "settingsViewUserInfo")));
    auto *reset = visual(session.window->contentItem(), "settingsResetDeviceIdentity");
    QVERIFY(reset);
    QCOMPARE(view->height(), 36.);
    QCOMPARE(reset->height(), 36.);
    QVERIFY(reset->property("danger").toBool());
    viewport->setProperty(
        "contentY", qMax(0., viewport->property("contentHeight").toReal() - viewport->height()));
    auto *infoOwner = settings->findChild<QObject *>("settingsUserInfoOwner");
    auto *resetOwner = settings->findChild<QObject *>("settingsResetDeviceIdentityOwner");
    QVERIFY(infoOwner && resetOwner);
    QVERIFY(!infoOwner->property("item").value<QObject *>());
    QVERIFY(!resetOwner->property("item").value<QObject *>());
    const auto snapshot = [&](const QString &suffix) {
        const auto dir = qEnvironmentVariable("ECHO_UI_SNAPSHOT_DIR");
        if (dir.isEmpty())
            return true;
        QDir().mkpath(dir);
        return session.window->grabWindow().save(
            dir + "/experimental-identity-" + (dark ? "dark-" : "light-") +
            QString::number(qRound(scale * 100)) + suffix + ".png");
    };
    QTest::qWait(100);
    QVERIFY(snapshot("-rows"));
    view->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QPointer<QObject> info;
    QTRY_VERIFY((info = infoOwner->property("item").value<QObject *>()));
    QTRY_VERIFY(info->property("opened").toBool());
    const auto rawEntries = info->property("entries");
    const auto entries = rawEntries.metaType() == QMetaType::fromType<QJSValue>()
                             ? rawEntries.value<QJSValue>().toVariant().toList()
                             : rawEntries.toList();
    bool hasToken = false, hasT1 = false;
    for (const auto &entry : entries) {
        const auto row = entry.toMap();
        hasToken |=
            row.value("label") == "token" && row.value("value") == "synthetic-visible-token";
        hasT1 |= row.value("label") == "t1" && row.value("value") == "synthetic-visible-t1";
    }
    QVERIFY(hasToken && hasT1);
    QVERIFY(snapshot("-viewer"));
    QVERIFY(QMetaObject::invokeMethod(info, "close"));
    QTRY_VERIFY(info.isNull());
    view->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_VERIFY((info = infoOwner->property("item").value<QObject *>()));
    QTRY_VERIFY(info->property("opened").toBool());
    session.api.advanceSession();
    QTRY_VERIFY(info.isNull());
    reset->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QPointer<QObject> confirmation;
    QTRY_VERIFY((confirmation = resetOwner->property("item").value<QObject *>()));
    QTRY_VERIFY(confirmation->property("opened").toBool());
    QVERIFY(snapshot("-reset"));
    auto *cancel = visual(session.window->contentItem(), "settingsCancelDeviceReset");
    QVERIFY(cancel);
    cancel->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_VERIFY(confirmation.isNull());
    QCOMPARE(session.accounts.account.userId(), quint64(42));
    QVERIFY(!experimental.resetPending());
    reset->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_VERIFY((confirmation = resetOwner->property("item").value<QObject *>()));
    catalog->setFilter({});
    QTRY_VERIFY(confirmation.isNull());
    QTRY_VERIFY(visual(session.window->contentItem(), "appearanceMode"));
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionExperimentalLoggingSettings_data()
{
    productionNetworkSettingsDraftApplyResetAndTheme_data();
}

void DesktopPagesTest::productionExperimentalLoggingSettings()
{
    QFETCH(bool, dark);
    QFETCH(qreal, scale);
    Session session;
    LogService logging(session.directory.filePath("logs"), 2, false, &session.preferences);
    RelaunchService relaunch({}, {}, false);
    QSignalSpy restartNotices(&relaunch, &RelaunchService::notice);
    QVERIFY(logging.active());
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(nullptr, nullptr, &logging, &relaunch));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    session.theme.setFloatingFrosted(false);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    for (int step = 0; step < qRound((scale - 1) / .05); ++step)
        session.shell.zoomIn();
    session.router.navigate("settings");
    auto *dialog = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    catalog->select("experimental");
    QVERIFY(session.settingsSectionReady());
    QQuickItem *level = nullptr;
    QTRY_VERIFY((level = visual(session.window->contentItem(), "settingsLogLevel")));
    level->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_End);
    QTest::keyClick(session.window, Qt::Key_Return);
    QTRY_COMPARE(logging.level(), QString("error"));
    // 弹层按需创建：回车确认后实例随退出释放，只断言其不再可见。
    QPointer<QObject> popup = level->property("popup").value<QObject *>();
    QVERIFY(popup);
    QTRY_VERIFY(!popup || !popup->property("visible").toBool());
    auto *body = visual(session.window->contentItem(), "settingsLogApiResponse");
    QVERIFY(body);
    body->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_VERIFY(logging.apiResponseBody());
    auto *viewport = visual(session.window->contentItem(), "settingsContentViewport");
    auto *enable = visual(session.window->contentItem(), "settingsEnableDiagnostics");
    QVERIFY(viewport && enable);
    viewport->setProperty(
        "contentY", qMax(0., viewport->property("contentHeight").toReal() - viewport->height()));
    QCOMPARE(enable->height(), 36.);
    QCOMPARE(enable->property("text").toString(), QStringLiteral("10 分钟"));
    enable->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_VERIFY(logging.diagnosticActive());
    auto *restart = visual(session.window->contentItem(), "settingsRestartDiagnostics");
    QVERIFY(restart);
    QCOMPARE(restart->property("text").toString(), QStringLiteral("开启启动诊断"));
    QCOMPARE(restart->height(), 36.);
    restart->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_COMPARE(restartNotices.size(), 1);
    auto *disable = visual(session.window->contentItem(), "settingsDisableDiagnostics");
    QVERIFY(disable);
    QTRY_VERIFY(disable->isVisible());
    disable->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_VERIFY(!logging.diagnosticActive());
    catalog->select("interface");
    QVERIFY(session.settingsSectionReady());
    catalog->select("experimental");
    QVERIFY(session.settingsSectionReady());
    QTRY_VERIFY((level = visual(session.window->contentItem(), "settingsLogLevel")));
    QTRY_COMPARE(level->property("value").toString(), QString("error"));
    QTRY_COMPARE(level->property("displayText").toString(), QStringLiteral("仅错误"));
    const auto snapshotDirectory = qEnvironmentVariable("ECHO_UI_SNAPSHOT_DIR");
    if (!snapshotDirectory.isEmpty()) {
        QDir().mkpath(snapshotDirectory);
        const auto image = session.window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(snapshotDirectory + "/logging-" + (dark ? "dark-" : "light-") +
                           QString::number(qRound(scale * 100)) + ".png"));
    }
    viewport = visual(session.window->contentItem(), "settingsContentViewport");
    restart = visual(session.window->contentItem(), "settingsRestartDiagnostics");
    QVERIFY(viewport && restart);
    viewport->setProperty(
        "contentY", qMax(0., viewport->property("contentHeight").toReal() - viewport->height()));
    const auto restartBounds = [&] {
        return restart->mapRectToItem(viewport, restart->boundingRect());
    };
    QTRY_VERIFY(restartBounds().top() >= 0 && restartBounds().bottom() <= viewport->height() + 1);
    if (!snapshotDirectory.isEmpty()) {
        const auto image = session.window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(snapshotDirectory + "/logging-" + (dark ? "dark-" : "light-") +
                           QString::number(qRound(scale * 100)) + "-bottom.png"));
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionNetworkSettingsDraftApplyResetAndTheme_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<qreal>("scale");
    QTest::newRow("light-100") << false << qreal(1);
    QTest::newRow("dark-100") << true << qreal(1);
    QTest::newRow("light-125") << false << qreal(1.25);
    QTest::newRow("dark-125") << true << qreal(1.25);
}

void DesktopPagesTest::productionNetworkSettingsDraftApplyResetAndTheme()
{
    QFETCH(bool, dark);
    QFETCH(qreal, scale);
    Session session;
    NetworkPreferences network(session.preferences, session.api);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QSignalSpy applied(&network, &NetworkPreferences::applied);
    QVERIFY(session.create(nullptr, &network));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY2(
        Test::prepareNativeInput(session.window),
        qPrintable(QString("Settings host: visible=%1 exposed=%2 active=%3 geometry=%4,%5 %6x%7")
                       .arg(session.window->isVisible())
                       .arg(session.window->isExposed())
                       .arg(session.window->isActive())
                       .arg(session.window->x())
                       .arg(session.window->y())
                       .arg(session.window->width())
                       .arg(session.window->height())));
    session.theme.setFloatingFrosted(false);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    for (int step = 0; step < qRound((scale - 1) / .05); ++step)
        session.shell.zoomIn();
    session.router.navigate("settings");
    auto *dialog = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    auto *searchTrigger = visual(session.window->contentItem(), "settingsSearchTrigger");
    QVERIFY(searchTrigger);
    searchTrigger->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Space);
    QQuickItem *searchInput = nullptr;
    QTRY_VERIFY((searchInput = visual(session.window->contentItem(), "settingsSearchInput")) &&
                searchInput->isVisible());
    QTRY_VERIFY(searchInput->hasActiveFocus());
    for (const auto key : QByteArray("bypass"))
        QTest::keyClick(session.window, key);
    QTRY_COMPARE(catalog->current(), QString("network"));
    QCOMPARE(catalog->rowCount(), 1);
    QTest::keyClick(session.window, Qt::Key_Escape);
    QTRY_COMPARE(catalog->filter(), QString());
    catalog->select("network");
    QVERIFY(session.settingsSectionReady());
    QQuickItem *mode = nullptr;
    QTRY_VERIFY((mode = visual(session.window->contentItem(), "settingsProxyMode")));
    mode->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_End);
    QTest::keyClick(session.window, Qt::Key_Return);
    QQuickItem *rules = nullptr;
    QTRY_VERIFY((rules = visual(session.window->contentItem(), "settingsProxyRules")) &&
                rules->isVisible());
    // 弹层按需创建：确认选择后实例随退出释放，只断言其不再可见。
    QPointer<QObject> modePopup = mode->property("popup").value<QObject *>();
    QVERIFY(modePopup);
    QTRY_VERIFY(!modePopup || !modePopup->property("visible").toBool());
    QCOMPARE(network.proxyMode(), QString("direct"));
    QVERIFY(visual(session.window->contentItem(), "settingsProxyUsername"));
    QVERIFY(visual(session.window->contentItem(), "settingsProxyPassword"));
    QPointer<QQuickItem> initialRules(rules);
    // Native keyboard editing changes presentation drafts only.
    rules->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_A, Qt::ControlModifier);
    for (const auto key : QByteArray("http://127.0.0.1:1"))
        QTest::keyClick(session.window, key);
    QCOMPARE(network.proxyRules(), QString());
    auto *viewport = visual(session.window->contentItem(), "settingsContentViewport");
    QVERIFY(viewport);
    auto *popupContent = dialog->property("contentItem").value<QQuickItem *>();
    QVERIFY(popupContent);
    qInfo() << "Network UI renderer API" << int(session.window->rendererInterface()->graphicsApi());
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa" &&
        qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND") &&
        qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        QCOMPARE(session.window->rendererInterface()->graphicsApi(), QSGRendererInterface::Metal);
#endif
    const auto popupBounds =
        popupContent->mapRectToScene(QRectF(0, 0, popupContent->width(), popupContent->height()));
    QVERIFY2(popupBounds.left() >= -.5 && popupBounds.top() >= -.5 &&
                 popupBounds.right() <= session.window->width() + .5 &&
                 popupBounds.bottom() <= session.window->height() + .5,
             qPrintable(QString("Scaled settings outside window: %1,%2 %3x%4")
                            .arg(popupBounds.x())
                            .arg(popupBounds.y())
                            .arg(popupBounds.width())
                            .arg(popupBounds.height())));
    const auto snapshotDirectory = qEnvironmentVariable("ECHO_UI_SNAPSHOT_DIR");
    const auto snapshot = [&](const QString &part) {
        if (snapshotDirectory.isEmpty())
            return;
        QDir().mkpath(snapshotDirectory);
        const auto image = session.window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(snapshotDirectory + "/network-" + (dark ? "dark-" : "light-") +
                           QString::number(qRound(scale * 100)) + "-" + part + ".png"));
    };
    snapshot("top");
    auto *apply = visual(session.window->contentItem(), "settingsApplyNetwork");
    QVERIFY(apply);
    viewport->setProperty(
        "contentY", qMax(0., viewport->property("contentHeight").toReal() - viewport->height()));
    QTRY_VERIFY(apply->mapToItem(viewport, QPointF()).y() >= 0);
    QCOMPARE(apply->height(), 36.);
    const auto surface = dialog->property("contentItem").value<QQuickItem *>();
    QVERIFY(surface);
    const auto controlBounds =
        apply->mapRectToItem(surface, QRectF(0, 0, apply->width(), apply->height()));
    QVERIFY(controlBounds.right() <= surface->width() + 1);
    QVERIFY(Test::prepareNativeInput(session.window));
    apply->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(apply->hasActiveFocus());
    QSignalSpy activated(apply, SIGNAL(clicked()));
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_VERIFY2(activated.size() > 0,
                 qPrintable(QString("Apply activation failed: focus=%1 enabled=%2 windowActive=%3")
                                .arg(apply->hasActiveFocus())
                                .arg(apply->isEnabled())
                                .arg(session.window->isActive())));
    QTRY_VERIFY2(applied.size() == 1,
                 qPrintable(QString("Apply not committed: error=%1 busy=%2 rules=%3")
                                .arg(network.errorMessage())
                                .arg(network.busy())
                                .arg(rules->property("text").toString())));
    QCOMPARE(network.proxyMode(), QString("fixed_servers"));
    QCOMPARE(network.proxyRules(), QString("http://127.0.0.1:1"));
    snapshot("bottom");
    viewport->setProperty("contentY", 0.);
    rules->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_A, Qt::ControlModifier);
    for (const auto key : QByteArray("unsaved.example:8888"))
        QTest::keyClick(session.window, key);
    catalog->select("interface");
    QVERIFY(session.settingsSectionReady());
    QTRY_VERIFY(initialRules.isNull());
    catalog->select("network");
    QVERIFY(session.settingsSectionReady());
    QTRY_VERIFY((rules = visual(session.window->contentItem(), "settingsProxyRules")));
    QTRY_COMPARE(rules->property("text").toString(), QString("http://127.0.0.1:1"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionPlayerEngineSettingsDecimalInputAndLifecycle()
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
    QVERIFY(Test::prepareNativeInput(session.window));
    session.theme.setFloatingFrosted(false);
    session.router.navigate("settings");
    auto *dialog = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    catalog->select("player");
    QVERIFY(session.settingsSectionReady());
    QCOMPARE(catalog->current(), QString("player"));
    QQuickItem *buffer = nullptr, *viewport = nullptr;
    QTRY_VERIFY((buffer = visual(session.window->contentItem(), "settingsEngineAudioBufferSecs")));
    QPointer<QQuickItem> retainedBuffer(buffer);
    auto *cache = visual(session.window->contentItem(), "settingsEngineCacheSecs");
    QVERIFY(cache);
    QTRY_COMPARE(visual(cache, "numberInputField")->property("text").toString(),
                 QString("3600000"));
    QVERIFY((viewport = visual(session.window->contentItem(), "settingsContentViewport")));
    auto *field = visual(buffer, "numberInputField");
    QVERIFY(field);
    QTRY_COMPARE(field->property("text").toString(), QString("0.2"));
    auto *increase = QQmlProperty(buffer, "up.indicator").read().value<QQuickItem *>();
    QVERIFY(increase);
    QVERIFY(session.clickItem(increase));
    QTRY_COMPARE(playback.enginePreferences()->audioBufferSecs(), .25);
    QVERIFY(session.clickItem(field));
    QTest::keyClick(session.window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(session.window, Qt::Key_0);
    QTest::keyClick(session.window, Qt::Key_Period);
    QTest::keyClick(session.window, Qt::Key_7);
    QTest::keyClick(session.window, Qt::Key_3);
    QTest::keyClick(session.window, Qt::Key_1);
    QTest::keyClick(session.window, Qt::Key_Return);
    QTRY_COMPARE(playback.enginePreferences()->audioBufferSecs(), .731);
    const auto capture = qEnvironmentVariable("ECHO_ENGINE_SETTINGS_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (int width : {1150, 650}) {
            session.window->resize(width, 800);
            viewport->setProperty("contentY", 0);
            QTest::qWait(150);
            QVERIFY(retainedBuffer);
            QCOMPARE(visual(session.window->contentItem(), "settingsEngineAudioBufferSecs"),
                     retainedBuffer.data());
            QVERIFY(viewport->property("contentWidth").toReal() <= viewport->width() + .5);
            if (!capture.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    capture + QString("/engine-%1-%2.png")
                                  .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                  .arg(width)));
        }
    }
    QPointer<QQuickItem> released(retainedBuffer);
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_VERIFY(released.isNull());
    QCOMPARE(warnings.size(), 0);
    session.root.reset();
    session.window = nullptr;
    QSignalSpy closed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}

void DesktopPagesTest::productionVariableDialogSizingReopens()
{
    Session session;
    session.shell.setMotionEnabled(false);
    PlaybackService playback(session.api, session.preferences);
    PluginRuntime plugins(session.engine, session.preferences,
                          {playback, session.theme, session.commands, session.router,
                           session.accounts.account, session.api},
                          session.directory.filePath("plugins"), true);
    PluginManager manager(plugins, session.preferences);
    QObject producer;
    TaskCenterModel tasks;
    for (int i = 0; i < 2; ++i) {
        TaskCenterModel::Registration entry;
        entry.id = QString::number(i);
        entry.content.name = i ? "实际变高卡片 中文 Latin" : "后台任务";
        entry.content.percent = i ? 42 : -1;
        tasks.registerTask(producer, entry);
    }
    // Presentation records exercise the production source cards, without network writes.
    manager.sources()->replace({QVariantMap{{"id", "official"},
                                            {"name", "官方源"},
                                            {"url", "https://github.com/example/plugins"},
                                            {"official", true},
                                            {"enabled", true}},
                                QVariantMap{{"id", "extra"},
                                            {"name", "附加源 Latin"},
                                            {"url", "https://github.com/example/extra"},
                                            {"enabled", true},
                                            {"lastError", "测试错误说明"}}});
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.theme.setFloatingFrosted(false);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    const auto capture = qEnvironmentVariable("ECHO_DIALOG_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const bool source : {true, false}) {
        QQmlComponent component(&session.engine,
                                QUrl(source ? "qrc:/qt/qml/EchoMusic/Plugins/PluginSourceDialog.qml"
                                            : "qrc:/qt/qml/EchoMusic/Shell/TaskCenterDialog.qml"));
        QVariantMap properties{{"parent", QVariant::fromValue(session.window->contentItem())}};
        properties.insert(source ? "manager" : "service",
                          source ? QVariant::fromValue<QObject *>(&manager)
                                 : QVariant::fromValue<QObject *>(&tasks));
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const auto scale : {1., 1.25}) {
                qreal stableHeight = 0;
                for (int cycle = 0; cycle < 4; ++cycle) {
                    // Production owners create the whole dialog per activation.
                    // Reopen the same recipe, keeping the task/source models.
                    QScopedPointer<QObject> popup(
                        component.createWithInitialProperties(properties));
                    QVERIFY2(popup, qPrintable(component.errorString()));
                    popup->setProperty("uiScale", scale);
                    QVERIFY(QMetaObject::invokeMethod(popup.data(), "open"));
                    QTRY_VERIFY(popup->property("opened").toBool());
                    auto *body = popup->property("contentItem").value<QQuickItem *>();
                    QVERIFY(body);
                    QPointer<QQuickItem> list =
                        visual(body, source ? "pluginSourceList" : "taskCenterList");
                    QVERIFY(list);
                    QTRY_COMPARE(list->property("count").toInt(), 2);
                    QTRY_VERIFY2(
                        list->property("measuredExtent").toReal() > 0,
                        qPrintable(
                            QString(
                                "source=%1 mode=%2 scale=%3 cycle=%4 extent=%5 "
                                "size=%6x%7 implicit=%8 budget=%9 visible=%10 contentHeight=%11")
                                .arg(source)
                                .arg(int(mode))
                                .arg(scale)
                                .arg(cycle)
                                .arg(list->property("measuredExtent").toReal())
                                .arg(list->width())
                                .arg(list->height())
                                .arg(list->implicitHeight())
                                .arg(list->property("maximumViewportHeight").toReal())
                                .arg(list->isVisible())
                                .arg(list->property("contentHeight").toReal())));
                    QTRY_VERIFY(qAbs(list->height() - list->property("measuredExtent").toReal()) <
                                .6);
                    if (!cycle)
                        stableHeight = popup->property("height").toReal();
                    QTRY_COMPARE(popup->property("height").toReal(), stableHeight);
                    if (cycle == 3 && !capture.isEmpty())
                        QVERIFY(session.window->grabWindow().save(
                            capture + QString("/%1-%2-%3.png")
                                          .arg(source ? "sources" : "tasks")
                                          .arg(int(mode))
                                          .arg(scale)));
                    QVERIFY(QMetaObject::invokeMethod(popup.data(), "close"));
                    QTRY_VERIFY(!popup->property("visible").toBool());
                    popup.reset();
                    QTRY_VERIFY(list.isNull());
                }
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::settingsDefaultsAndSourceEffectFileManager()
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
    QPointer<QObject> settings = session.root->findChild<QObject *>("settingsDialog");
    QVERIFY(settings);
    QTRY_VERIFY(settings->property("opened").toBool());
    auto *catalog = settings->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    QQuickItem *control = nullptr;
    QTRY_VERIFY((control = visual(session.window->contentItem(), "settingsDefaultSearch")));
    QVERIFY(!control->property("checked").toBool());
    catalog->select("playback");
    QTRY_VERIFY((control = visual(session.window->contentItem(), "settingsDefaultQuality")));
    QTRY_COMPARE(control->property("currentText").toString(), QStringLiteral("Hi-Res 品质"));
    QCOMPARE(playback.preferences()->defaultQuality(), QString("hi_res"));
    QQuickItem *compatibility = nullptr;
    QTRY_VERIFY(
        (compatibility = visual(session.window->contentItem(), "settingsCompatibilityMode")));
    QVERIFY(compatibility->property("checked").toBool());
    QVERIFY(playback.preferences()->compatibilityMode());
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(session.revealItem(compatibility));
    QSignalSpy compatibilityFrame(session.window, &QQuickWindow::frameSwapped);
    session.window->update();
    QTRY_VERIFY(!compatibilityFrame.isEmpty());
    Test::moveNativePointer(
        session.window,
        compatibility->mapToScene({compatibility->width() / 2, compatibility->height() / 2})
            .toPoint());
    QTRY_VERIFY(compatibility->property("hovered").toBool());
    QTest::mouseClick(
        session.window, Qt::LeftButton, Qt::NoModifier,
        compatibility->mapToScene(QPointF(compatibility->width() / 2, compatibility->height() / 2))
            .toPoint());
    QTRY_VERIFY(!playback.preferences()->compatibilityMode());
    QTRY_VERIFY(!compatibility->property("checked").toBool());
    QPointer<QQuickItem> retiredCompatibility = compatibility;
    catalog->select("font");
    QTRY_VERIFY(retiredCompatibility.isNull());
    catalog->select("playback");
    QTRY_VERIFY(
        (compatibility = visual(session.window->contentItem(), "settingsCompatibilityMode")));
    QVERIFY(!compatibility->property("checked").toBool());
    QTRY_VERIFY(
        visualText(session.window->contentItem(), QStringLiteral("首选音质不可用时自动尝试备选")));
    catalog->select("spatialAudio");
    QQuickItem *row = nullptr, *divider = nullptr;
    QTRY_VERIFY((row = visual(session.window->contentItem(), "settingsEffectFilesRow")));
    QVERIFY((divider = visual(session.window->contentItem(), "settingsEffectFilesDivider")));
    QVERIFY(visualText(row, QStringLiteral("音效文件")));
    QVERIFY(!visual(session.window->contentItem(), "settingsLocalEffectFiles"));
    const auto gap = [&] {
        return divider->mapToScene(QPointF()).y() - row->mapToScene(QPointF(0, row->height())).y();
    };
    // The card has two management rows; file lists belong to a dialog.
    QTRY_VERIFY(qAbs(gap() - 20) < .5);
    const auto capture = qEnvironmentVariable("ECHO_SETTINGS_DEFAULTS_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        QTest::qWait(150);
        QVERIFY(qAbs(gap() - 20) < .5);
        if (!capture.isEmpty())
            QVERIFY(session.window->grabWindow().save(
                capture + (mode == ThemeStore::Dark ? "/effects-dark.png" : "/effects-light.png")));
    }
    auto *manage = visual(session.window->contentItem(), "manageEffectFilesAction");
    QVERIFY(manage);
    QVERIFY(!manage->property("plain").toBool());
    QCOMPARE(manage->height(), 36.);
    QCOMPARE(manage->property("font").value<QFont>().pixelSize(), 12);
    QCOMPARE(manage->property("font").value<QFont>().weight(), QFont::DemiBold);
    QVERIFY(QMetaObject::invokeMethod(manage, "clicked"));
    QPointer<QObject> dialog = session.root->findChild<QObject *>("audioEffectFilesDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(dialog->property("width").toReal(), 520.);
    QPointer<QQuickItem> files = visual(session.window->contentItem(), "settingsEffectFileList");
    QVERIFY(files);
    QTRY_COMPARE(files->property("count").toInt(), 0);
    QVERIFY(visualText(files, QStringLiteral("暂无本地导入音效")));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        QTest::qWait(150);
        if (!capture.isEmpty())
            QVERIFY(session.window->grabWindow().save(capture + (mode == ThemeStore::Dark
                                                                     ? "/files-empty-dark.png"
                                                                     : "/files-empty-light.png")));
    }
    for (int tab = 1; tab < 4; ++tab) {
        auto *button =
            visual(session.window->contentItem(), QString("settingsEffectFileTab-%1").arg(tab));
        QVERIFY(button);
        QCOMPARE(button->height(), 36.);
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_COMPARE(dialog->property("currentTab").toInt(), tab);
        QVERIFY(visualText(files, QStringLiteral("此分类暂无已下载音效")));
        QVERIFY(visual(session.window->contentItem(), "settingsEffectDownloadHint")->isVisible());
        QVERIFY(!visual(session.window->contentItem(), "importLocalEffectsAction")->isVisible());
    }
    dialog->setProperty("currentTab", 0);
    // A readable catalog resource checks file actions, never audio processing.
    const auto path = session.directory.filePath("layout.irs");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("layout-only local resource") > 0);
    file.close();
    playback.audioControls()->registerImpulseResponse(path, QStringLiteral("本地布局检查"));
    QTRY_COMPARE(files->property("count").toInt(), 1);
    QQuickItem *fileRow = nullptr;
    QTRY_VERIFY((fileRow = visual(files, "settingsEffectFileRow")));
    QCOMPARE(fileRow->height(), 52.);
    QTRY_COMPARE(files->height(), 52.);
    const auto singleFileHeight = dialog->property("height").toReal();
    for (int cycle = 0; cycle < 4; ++cycle) {
        QVERIFY(QMetaObject::invokeMethod(dialog.data(), "close"));
        QTRY_VERIFY(dialog.isNull());
        QTRY_VERIFY(files.isNull());
        QVERIFY(session.clickItem("manageEffectFilesAction"));
        QTRY_VERIFY((dialog = session.root->findChild<QObject *>("audioEffectFilesDialog")));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY((files = visual(session.window->contentItem(), "settingsEffectFileList")));
        QTRY_COMPARE(files->property("count").toInt(), 1);
        QTRY_COMPARE(files->height(), 52.);
        QTRY_COMPARE(dialog->property("height").toReal(), singleFileHeight);
    }
    QTRY_VERIFY((fileRow = visual(files, "settingsEffectFileRow")));
    auto *rename = visual(fileRow, "settingsEffectRename");
    QVERIFY(rename);
    QVERIFY(QMetaObject::invokeMethod(rename, "clicked"));
    QTRY_COMPARE(dialog->property("editingPath").toString(), path);
    dialog->setProperty("draft", QStringLiteral("新名称.wav"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "saveName"));
    QTRY_COMPARE(
        playback.audioControls()->impulseResponses().at(0).toMap().value("name").toString(),
        QStringLiteral("新名称"));
    QTRY_VERIFY((fileRow = visual(files, "settingsEffectFileRow")));
    rename = visual(fileRow, "settingsEffectRename");
    QVERIFY(rename);
    QVERIFY(QMetaObject::invokeMethod(rename, "clicked"));
    dialog->setProperty("currentTab", 1);
    QVERIFY(dialog->property("editingPath").toString().isEmpty());
    dialog->setProperty("currentTab", 0);
    QTRY_VERIFY((fileRow = visual(files, "settingsEffectFileRow")));
    rename = visual(fileRow, "settingsEffectRename");
    auto *remove = visual(fileRow, "settingsEffectRemove");
    QVERIFY(rename && remove);
    QTRY_COMPARE(rename->width(), 14.);
    QTRY_COMPARE(remove->width(), 14.);
    QTRY_COMPARE(visual(rename, "iconImage")->property("status").toInt(), 1);
    QTRY_COMPARE(visual(remove, "iconImage")->property("status").toInt(), 1);
    QTest::qWait(150);
    if (!capture.isEmpty())
        QVERIFY(session.window->grabWindow().save(capture + "/files-renamed.png"));
    QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
    QTRY_COMPARE(files->property("count").toInt(), 0);
    // The shared catalog may be large; only visible and cached rows mount.
    for (int index = 0; index < 150; ++index) {
        const auto resource = session.directory.filePath(QString("layout-%1.irs").arg(index));
        QVERIFY(QFile::copy(path, resource));
        playback.audioControls()->registerImpulseResponse(resource, QString::number(index));
    }
    QTRY_COMPARE(files->property("count").toInt(), 150);
    for (const auto scale : {1., 1.25}) {
        session.shell.resetZoom();
        for (int step = 0; step < qRound((scale - 1) * 20); ++step)
            session.shell.zoomIn();
        QTest::qWait(150);
        QCOMPARE(dialog->property("uiScale").toReal(), scale);
        const int mounted = visualCount(files, "settingsEffectFileRow");
        QVERIFY(mounted > 0 && mounted < 30);
        if (!capture.isEmpty())
            QVERIFY(session.window->grabWindow().save(capture +
                                                      QString("/files-many-%1.png").arg(scale)));
    }
    session.shell.resetZoom();
    QTest::qWait(100);
    QVERIFY(qAbs(gap() - 20) < .5);
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(dialog.isNull());
    auto *engines = visual(session.window->contentItem(), "manageDspProvidersAction");
    QVERIFY(engines && !engines->property("plain").toBool());
    QVERIFY(QMetaObject::invokeMethod(engines, "clicked"));
    QPointer<QObject> providerDialog = session.root->findChild<QObject *>("dspProviderDialog");
    QVERIFY(providerDialog);
    QTRY_VERIFY(providerDialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(providerDialog, "close"));
    QTRY_VERIFY(providerDialog.isNull());
    QCOMPARE(warnings.size(), 0);
    QVERIFY(QMetaObject::invokeMethod(settings, "close"));
    QTRY_VERIFY(settings.isNull());
    QTest::qWait(150);
    session.root.reset();
    session.window = nullptr;
    QSignalSpy closed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
}
