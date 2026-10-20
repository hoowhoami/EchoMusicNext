#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::playbackZoomShortcutsFollowSharedScale()
{
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
    session.shell.resetZoom();
    QTest::keyClick(session.window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(session.shell.uiScale(), 1.05);
    auto *owner = session.root->findChild<QObject *>("playbackPageOwner");
    QVERIFY(owner);
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariant(QVariantMap{}))));
    QPointer<QObject> page = owner->property("item").value<QObject *>();
    QTRY_VERIFY(page && page->property("opened").toBool());
    auto *collapse = visual(session.window->contentItem(), "playbackCollapseAction");
    auto *cover = visual(session.window->contentItem(), "playbackPageCover");
    QVERIFY(collapse && cover);
#ifdef Q_OS_MACOS
    const auto nativeFrames = nativeWindowAppearance(session.window).value("buttonFrames");
#endif
    for (int step = 0; step < 4; ++step)
        QTest::keyClick(session.window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(session.shell.uiScale(), 1.25);
    QTRY_COMPARE(page->property("uiScale").toReal(), 1.25);
    QTRY_VERIFY(qAbs(page->property("width").toReal() * 1.25 - session.window->width()) < .01);
    QTRY_VERIFY(qAbs(page->property("height").toReal() * 1.25 - session.window->height()) < .01);
    QVERIFY(qAbs(collapse->mapRectToScene(collapse->boundingRect()).width() - 42.5) < .01);
    QVERIFY(qAbs(cover->mapRectToScene(cover->boundingRect()).width() - cover->width() * 1.25) <
            .01);
#ifdef Q_OS_MACOS
    QCOMPARE(nativeWindowAppearance(session.window).value("buttonFrames"), nativeFrames);
#endif
    QTest::keyClick(session.window, Qt::Key_Minus, Qt::ControlModifier);
    QTRY_COMPARE(session.shell.uiScale(), 1.2);
    QTest::keyClick(session.window, Qt::Key_0, Qt::ControlModifier);
    QTRY_COMPARE(session.shell.uiScale(), 1.);
    // A second app window must not change the main window's UI zoom.
    {
        QQuickWindow auxiliary;
        auxiliary.resize(300, 120);
        auxiliary.show();
        QVERIFY(Test::prepareNativeInput(&auxiliary));
        QTRY_VERIFY(!session.window->isActive());
        QTest::keyClick(&auxiliary, Qt::Key_Equal, Qt::ControlModifier);
        QCoreApplication::processEvents();
        QCOMPARE(session.shell.uiScale(), 1.);
    }
    QVERIFY(Test::prepareNativeInput(session.window));
    QTest::keyClick(session.window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(session.shell.uiScale(), 1.05);
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(page.isNull());
    QCOMPARE(session.shell.uiScale(), 1.05);
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariant(QVariantMap{}))));
    page = owner->property("item").value<QObject *>();
    QTRY_VERIFY(page && page->property("opened").toBool());
    QCOMPARE(page->property("uiScale").toReal(), 1.05);
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(page.isNull());
    for (const auto &warning : warnings)
        qWarning() << warning;
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::windowSettingsNeverHideWithoutNativeRecovery()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("window.ini"), QSettings::IniFormat);
    WindowController controller(settings, false);
    QQuickWindow window;
    controller.attach(&window);
    window.show();
    QVERIFY(!controller.trayAvailable());
    controller.setStartMinimized(true);
    controller.applyStartupVisibility();
    QVERIFY(window.isVisible());
    QVERIFY(!controller.background());
    QVERIFY(!controller.systemError().isEmpty());
    controller.setAutoLaunch(true);
    QVERIFY(!controller.autoLaunch());
    QVERIFY(!controller.systemError().isEmpty());
    controller.setHideDockInBackground(true);
    controller.setHideMenuBarInBackground(true);
    settings.sync();
    WindowController reopened(settings, false);
    QVERIFY(reopened.startMinimized());
    QVERIFY(reopened.hideDockInBackground());
    QVERIFY(reopened.hideMenuBarInBackground());
    window.showMaximized();
    controller.showWindow();
    QCOMPARE(window.windowState(), Qt::WindowMaximized);
    // Direct native hide is not the tray/background flow (which stores its
    // state in the controller). Exercise normal hidden/minimized recovery.
    window.showNormal();
    QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
    window.hide();
    controller.showWindow();
    QVERIFY(window.isVisible());
    QCOMPARE(window.windowState(), Qt::WindowNoState);
    window.showMinimized();
    controller.showWindow();
    QVERIFY(window.isVisible());
    QCOMPARE(window.windowState(), Qt::WindowNoState);
    QSignalSpy quit(&controller, &WindowController::quitRequested);
    controller.quit();
    QCOMPARE(quit.size(), 1);
    controller.shutdown();
    QVERIFY(!window.isVisible());
    window.show();
    controller.close();
    QVERIFY(!window.isVisible());
}

void DesktopPagesTest::startupEntryEscapesBothDesktopParsingLayers()
{
    const auto text =
        StartupIntegration::desktopEntry(QStringLiteral("/opt/My App/$name`\\\"%/music"));
    const QString expected = QString("Exec=\"/opt/My App/") + QString(2, '\\') + "$name" +
                             QString(2, '\\') + "`" + QString(6, '\\') + "\"%%/music\"";
    QVERIFY(text.contains(expected));
    QVERIFY(StartupIntegration::desktopEntry("/opt/a=b/music").isEmpty());
    QVERIFY(StartupIntegration::desktopEntry(QString()).isEmpty());
    StartupIntegration unavailable(false);
    QVERIFY(!unavailable.enabled());
    QVERIFY(!unavailable.setEnabled(true).isEmpty());
    QVERIFY(!unavailable.enabled());
}

void DesktopPagesTest::sidebarLayoutPersistsAndKeepsGroupsAndOneShortcut()
{
    QTemporaryDir temp;
    QSettings preferences(temp.filePath("preferences.ini"), QSettings::IniFormat);
    ShellSettings shell(preferences);
    auto &layout = *shell.sidebarLayout();
    QAbstractItemModelTester tester(&layout,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    QAbstractItemModelTester cards(layout.shortcutModel(),
                                   QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(layout.rowCount(), 3);
    QCOMPARE(layout.shortcutCatalog().size(), 9);
    for (const auto &value : layout.shortcutCatalog()) {
        const auto entry = value.toMap();
        if (entry["key"] == "recommend")
            QCOMPARE(entry["route"].toString(), QString("recommend-songs"));
    }
    QCOMPARE(layout.sections()[0].toMap()["title"].toString(), QStringLiteral("音乐菜单"));
    QCOMPARE(layout.sections()[1].toMap()["title"].toString(), QStringLiteral("歌单"));
    QCOMPARE(layout.shortcuts().size(), 2);
    layout.toggleHidden("favorites");
    layout.toggleGroup("library");
    layout.toggleGroup("playlists");
    QVERIFY(!layout.menuVisible());
    QVERIFY(!layout.playlistAreaVisible());
    layout.moveBefore("history", "favorites");
    QCOMPARE(layout.data(layout.index(0), SidebarLayoutModel::Key).toString(), QString("history"));
    layout.moveBefore("home", "favorites"); // Card catalog is not a menu group.
    QCOMPARE(layout.rowCount(), 3);
    layout.moveShortcutBefore("explore", "home");
    QCOMPARE(layout.shortcuts()[0].toMap()["key"].toString(), QString("explore"));
    layout.toggleShortcut("home");
    layout.toggleShortcut("explore"); // Must retain one card.
    layout.toggleShortcut("invalid");
    QCOMPARE(layout.shortcuts().size(), 1);
    preferences.sync();
    QSettings reread(temp.filePath("preferences.ini"), QSettings::IniFormat);
    ShellSettings restored(reread);
    auto *loaded = restored.sidebarLayout();
    QCOMPARE(loaded->data(loaded->index(0), SidebarLayoutModel::Key).toString(),
             QString("history"));
    QVERIFY(loaded->data(loaded->index(1), SidebarLayoutModel::Hidden).toBool());
    QVERIFY(loaded->sections()[0].toMap()["hidden"].toBool());
    QVERIFY(loaded->sections()[1].toMap()["hidden"].toBool());
    QCOMPARE(loaded->shortcuts().size(), 1);
    QCOMPARE(loaded->shortcuts()[0].toMap()["key"].toString(), QString("explore"));
    loaded->restoreDefaults();
    QCOMPARE(loaded->shortcuts().size(), 1);
    QVERIFY(loaded->playlistAreaVisible());
    QVERIFY(loaded->menuVisible());
    QCOMPARE(loaded->data(loaded->index(0), SidebarLayoutModel::Key).toString(),
             QString("favorites"));
    loaded->restoreShortcuts();
    QCOMPARE(loaded->shortcuts().size(), 2);
    QVERIFY(!loaded->data(loaded->index(0), SidebarLayoutModel::Hidden).toBool());
}

void DesktopPagesTest::sidebarShortcutCollapseFrames_data()
{
    QTest::addColumn<int>("themeMode");
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<int>("cardCount");
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (const auto scale : {1., 1.25})
            for (const int count : {3, 4})
                QTest::newRow(qPrintable(QString("%1-%2-%3").arg(int(mode)).arg(scale).arg(count)))
                    << int(mode) << qreal(scale) << count;
}

void DesktopPagesTest::sidebarShortcutCollapseFrames()
{
    QFETCH(int, themeMode);
    QFETCH(qreal, scale);
    QFETCH(int, cardCount);
    Session session;
    session.theme.setMode(ThemeStore::Mode(themeMode));
    session.theme.setMotionEnabled(true);
    session.shell.setMotionEnabled(true);
    if (scale > 1.)
        for (int step = 0; step < 5; ++step)
            session.shell.zoomIn();
    auto *layout = session.shell.sidebarLayout();
    layout->toggleShortcut("personal-fm");
    if (cardCount == 4)
        layout->toggleShortcut("discover-flow");
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    const auto item = [&session](const QString &name) {
        return visual(session.window->contentItem(), name);
    };
    auto *cards = item("sidebarShortcuts");
    auto *add = item("sidebarShortcutAdd");
    QVERIFY(cards && add);
    QTRY_COMPARE(cards->width(), 194.);
    QTest::qWait(200);
    QList<QPointer<QQuickItem>> retained;
    for (const auto &entry : layout->shortcuts()) {
        auto *card = item("sidebarShortcut-" + entry.toMap()["key"].toString());
        QVERIFY(card);
        retained.append(card);
    }
    const auto directory = qEnvironmentVariable("ECHO_SIDEBAR_REFLOW_CAPTURE_DIR");
    const QString prefix = directory + '/' + QString::fromLatin1(QTest::currentDataTag());
    QJsonArray samples;
    QList<QPair<QString, QImage>> captures;
    const auto saveSamples = qScopeGuard([&] {
        if (directory.isEmpty())
            return;
        QDir().mkpath(directory);
        QFile file(prefix + ".json");
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(samples).toJson());
        // PNG encoding is deliberately outside the measured animation.
        for (const auto &[path, frame] : std::as_const(captures))
            frame.save(path);
    });
    // Warm the real rendering path before timing its changing geometry.
    QVERIFY(!session.window->grabWindow().isNull());
    // Two ordinary transitions, then rapid reversal while both directions
    // are in flight. Sample rendered frames, not only settled endpoints.
    for (int phase = 0; phase < 3; ++phase) {
        session.shell.toggleSidebar();
        QElapsedTimer elapsed;
        elapsed.start();
        int reversals = 0, frameCount = 0;
        bool intermediateWidth = false;
        while (elapsed.elapsed() < 340 || frameCount < 4) {
            if (phase == 2 && reversals < 2 && elapsed.elapsed() >= 64 * (reversals + 1)) {
                session.shell.toggleSidebar();
                ++reversals;
            }
            session.window->update();
            QTest::qWait(10);
            const auto frame = session.window->grabWindow();
            QVERIFY(!frame.isNull());
            ++frameCount;
            const bool collapsed = session.shell.sidebarCollapsed();
            const qreal cellWidth = collapsed ? cards->width() : (cards->width() - 8) / 2;
            intermediateWidth |= cards->width() > 44 && cards->width() < 194;
            QJsonArray positions;
            QString failure;
            for (int index = 0; index <= retained.size(); ++index) {
                auto *card = index == retained.size() ? add : retained[index].data();
                QVERIFY(card);
                const bool fullAdd = card == add && cardCount % 2 == 0;
                const qreal x = collapsed || fullAdd ? 0 : (index % 2) * (cellWidth + 8);
                const qreal y = collapsed ? index * 48 : (index / 2) * 54;
                positions.append(QJsonObject{{"name", card->objectName()},
                                             {"x", card->x()},
                                             {"y", card->y()},
                                             {"targetX", x},
                                             {"targetY", y}});
                if (qAbs(card->x() - x) > .01 || qAbs(card->y() - y) > .01)
                    failure = QString("%1 phase %2 at %3ms: (%4,%5), target (%6,%7), width %8")
                                  .arg(card->objectName())
                                  .arg(phase)
                                  .arg(elapsed.elapsed())
                                  .arg(card->x())
                                  .arg(card->y())
                                  .arg(x)
                                  .arg(y)
                                  .arg(cards->width());
            }
            samples.append(QJsonObject{{"phase", phase},
                                       {"elapsed", elapsed.elapsed()},
                                       {"collapsed", collapsed},
                                       {"width", cards->width()},
                                       {"positions", positions}});
            if (!directory.isEmpty() && frameCount <= 12 &&
                (frameCount % 2 == 0 || !failure.isEmpty())) {
                const auto origin = cards->mapToScene(QPointF());
                const auto dpr = session.window->devicePixelRatio();
                const QRect crop(qRound(origin.x() * dpr), qRound(origin.y() * dpr),
                                 qRound(220 * scale * dpr), qRound(260 * scale * dpr));
                captures.append({prefix + QString("-%1-%2.png").arg(phase).arg(frameCount),
                                 frame.copy(crop.intersected(frame.rect()))});
            }
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
        }
        QVERIFY(frameCount >= 4);
        QVERIFY(intermediateWidth);
    }
    session.theme.setMotionEnabled(false);
    session.shell.setMotionEnabled(false);
    session.shell.toggleSidebar();
    QTRY_COMPARE(cards->width(), 194.);
    session.window->hide();
    session.shell.toggleSidebar();
    session.window->show();
    QTRY_COMPARE(cards->width(), 44.);
    for (const auto &card : std::as_const(retained))
        QVERIFY(card);
}

void DesktopPagesTest::sidebarShortcutHoverDragAcrossThemesAndScales_data()
{
    QTest::addColumn<int>("themeMode");
    QTest::addColumn<qreal>("scale");
    QTest::newRow("light-100") << int(ThemeStore::Light) << qreal(1.);
    QTest::newRow("light-125") << int(ThemeStore::Light) << qreal(1.25);
    QTest::newRow("dark-100") << int(ThemeStore::Dark) << qreal(1.);
    QTest::newRow("dark-125") << int(ThemeStore::Dark) << qreal(1.25);
}

void DesktopPagesTest::sidebarShortcutHoverDragAcrossThemesAndScales()
{
    QFETCH(int, themeMode);
    QFETCH(qreal, scale);
    const auto mode = ThemeStore::Mode(themeMode);
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    // Sidebar targets need real registered providers; the test only supplies
    // page registration, never fabricated protocol responses.
    for (const auto &name : {"home", "explore", "ranking"})
        QVERIFY(session.router.registerPage(
            {QString::fromLatin1(name), "/main/" + QString::fromLatin1(name),
             QUrl("qrc:/qt/qml/EchoMusic/Discovery/ExplorePage.qml")}));
    session.theme.setMode(mode);
    if (scale > 1.)
        for (int i = 0; i < 5; ++i)
            session.shell.zoomIn();
    // Match Application: restored scale and providers precede QML creation.
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
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
        return session.window->grabWindow().save(directory + "/" + name + ".png");
    };
    layout->restoreShortcuts();
    Test::moveNativePointer(session.window, QPoint(800, 50));
    if (QGuiApplication::platformName() == "cocoa")
        QTRY_COMPARE(QCursor::pos(), session.window->mapToGlobal(QPoint(800, 50)));
    QTest::qWait(220);
    auto *home = item("sidebarShortcut-home");
    auto *explore = item("sidebarShortcut-explore");
    QVERIFY(home && explore);
    auto *label = visual(home, "sidebarShortcutLabel");
    auto *icon = visual(home, "sidebarShortcutIcon");
    auto *remove = item("sidebarShortcutRemove-home");
    QVERIFY(label && icon && remove);
    QTRY_COMPARE(icon->opacity(), 1.);
    QCOMPARE(label->opacity(), 0.);
    QCOMPARE(icon->width(), 20.);
    QCOMPARE(remove->width(), 24.);
    bool iconFaded = false, labelFaded = false;
    const auto iconFade = QObject::connect(icon, &QQuickItem::opacityChanged, home, [&] {
        iconFaded |= icon->opacity() > 0 && icon->opacity() < 1;
    });
    const auto labelFade = QObject::connect(label, &QQuickItem::opacityChanged, home, [&] {
        labelFaded |= label->opacity() > 0 && label->opacity() < 1;
    });
    const auto stopFade = qScopeGuard([&] {
        QObject::disconnect(iconFade);
        QObject::disconnect(labelFade);
    });
    Test::moveNativePointer(session.window, center(home));
    if (QGuiApplication::platformName() == "cocoa")
        QTRY_COMPARE(QCursor::pos(), session.window->mapToGlobal(center(home)));
    QTRY_COMPARE(label->opacity(), 1.);
    QTRY_COMPARE(icon->opacity(), 0.);
    QVERIFY(iconFaded && labelFaded);
    QCOMPARE(label->width(), home->width() - 16 - 24);
    QVERIFY(capture(QString("shortcut-hover-%1-%2").arg(int(mode)).arg(scale)));
    // Pointer removal does not send keyboard focus to the survivor.
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, center(remove));
    QTRY_COMPARE(layout->shortcuts().size(), 1);
    QVERIFY(!item("sidebarShortcut-explore")
                 ->property("activation")
                 .value<QObject *>()
                 ->property("activeFocus")
                 .toBool());
    layout->restoreShortcuts();
    QTest::qWait(200);
    home = item("sidebarShortcut-home");
    explore = item("sidebarShortcut-explore");
    QPointer<QQuickItem> kept(home);
    const auto oldX = home->x();
    const auto targetX = explore->x();
    bool animatedBetweenPositions = false;
    const auto movement = QObject::connect(home, &QQuickItem::xChanged, home, [&] {
        animatedBetweenPositions |= home->x() > oldX && home->x() < targetX;
    });
    const auto stopMovement = qScopeGuard([&] { QObject::disconnect(movement); });
    const auto start = center(home), end = center(explore);
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(session.window, start + QPoint(10, 0), 15);
    QTest::mouseMove(session.window, end, 20);
    // Reorder is already visible before the mouse is released.
    QTRY_COMPARE(layout->homeRoute(), QString("explore"));
    QTest::mouseMove(session.window, end + QPoint(1, 0), 1);
    QCOMPARE(layout->homeRoute(), QString("explore"));
    QTest::qWait(35);
    QVERIFY(capture(QString("shortcut-drag-%1-%2").arg(int(mode)).arg(scale)));
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_COMPARE(home->x(), targetX);
    QVERIFY(animatedBetweenPositions);
    QCOMPARE(item("sidebarShortcut-home"), kept.data());
    QCOMPARE(session.router.current()->name(), QString("profile"));
    Test::moveNativePointer(session.window, QPoint(800, 50));
    QTest::qWait(220);
    QVERIFY(capture(QString("shortcut-rest-%1-%2").arg(int(mode)).arg(scale)));
}

void DesktopPagesTest::sidebarShortcutHoverDragAndFocus()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    // Sidebar targets need real registered providers; the test only supplies
    // page registration, never fabricated protocol responses.
    for (const auto &name : {"home", "explore", "ranking"})
        QVERIFY(session.router.registerPage(
            {QString::fromLatin1(name), "/main/" + QString::fromLatin1(name),
             QUrl("qrc:/qt/qml/EchoMusic/Discovery/ExplorePage.qml")}));
    // Match Application: providers are registered before the first QML binding.
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
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
        return session.window->grabWindow().save(directory + "/" + name + ".png");
    };
    // Escape cancels a real preview, including both display and preferences.
    layout->restoreShortcuts();
    QTest::qWait(220);
    auto *cancelHome = item("sidebarShortcut-home");
    const auto cancelStart = center(cancelHome);
    const auto cancelEnd = center(item("sidebarShortcut-explore"));
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, cancelStart);
    QTest::mouseMove(session.window, cancelStart + QPoint(10, 0), 15);
    QTest::mouseMove(session.window, cancelEnd, 20);
    QTRY_COMPARE(layout->homeRoute(), QString("explore"));
    QTest::keyClick(session.window, Qt::Key_Escape);
    QTRY_COMPARE(layout->homeRoute(), QString("home"));
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, cancelEnd);
    QCOMPARE(session.preferences.value("sidebar/shortcuts").toStringList().first(),
             QString("home"));
    QCOMPARE(session.router.current()->name(), QString("profile"));
    session.shell.resetZoom();
    layout->restoreShortcuts();
    QTest::qWait(200);
    auto *home = item("sidebarShortcut-home");
    auto *activate = home->property("activation").value<QQuickItem *>();
    QVERIFY(activate);
    QVERIFY(activate->isEnabled());
    QVERIFY(Test::prepareNativeInput(session.window));
    item("sidebarShortcutAdd")->forceActiveFocus(Qt::TabFocusReason);
    activate->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(activate->hasActiveFocus());
    QTest::keyClick(session.window, Qt::Key_Right, Qt::AltModifier);
    QTRY_COMPARE(layout->homeRoute(), QString("explore"));
    QTRY_VERIFY(activate->hasActiveFocus());
    // Keyboard removal continues at the surviving card, without scrolling.
    auto *remove = item("sidebarShortcutRemove-home");
    // Respect macOS's system Tab-text-only preference; direct focus also
    // exercises the accessibility/keyboard activation path on every platform.
    remove->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(remove->hasActiveFocus());
    QTest::keyClick(session.window, Qt::Key_Space);
    QTRY_COMPARE(layout->shortcuts().size(), 1);
    auto *survivor = item("sidebarShortcut-explore")->property("activation").value<QQuickItem *>();
    QTRY_VERIFY(survivor->hasActiveFocus());
    QVERIFY(!item("sidebarShortcutRemove-explore")->isVisible());
    // The same Alt+arrow conflict also affected the shared shell action.
    auto *tasks = item("titlebar-action-tasks");
    QVERIFY(tasks && tasks->isEnabled());
    tasks->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE(session.preferences.value("shell/titlebar/order").toStringList().value(0),
                 QString("host/tasks"));
    QCOMPARE(session.router.current()->name(), QString("profile"));
    // Reduced motion uses the same target geometry without intermediate frames.
    session.shell.setMotionEnabled(false);
    session.theme.setMotionEnabled(false);
    layout->restoreShortcuts();
    QTest::qWait(50);
    home = item("sidebarShortcut-home");
    Test::moveNativePointer(session.window, center(home));
    QTRY_COMPARE(visual(home, "sidebarShortcutLabel")->opacity(), 1.);
    session.shell.toggleSidebar();
    QTRY_VERIFY(session.shell.sidebarCollapsed());
    QTest::qWait(50);
    home = item("sidebarShortcut-home");
    Test::moveNativePointer(session.window, center(home));
    QTRY_COMPARE(visual(home, "sidebarShortcutIcon")->opacity(), 1.);
    QVERIFY(!visual(home, "sidebarShortcutLabel")->isVisible());
    QVERIFY(!item("sidebarShortcutRemove-home")->isVisible());
    QVERIFY(capture("shortcut-collapsed-reduced-motion"));
    const auto railStart = center(home);
    const auto railEnd = center(item("sidebarShortcut-explore"));
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, railStart);
    QTest::mouseMove(session.window, railStart + QPoint(0, 10), 15);
    QTest::mouseMove(session.window, railEnd, 20);
    QTRY_COMPARE(layout->homeRoute(), QString("explore"));
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, railEnd);
    QCOMPARE(session.router.current()->name(), QString("profile"));

    for (const auto &arguments : warnings)
        for (const auto &warning : qvariant_cast<QList<QQmlError>>(arguments.value(0)))
            QVERIFY2(!warning.description().contains("SidebarShortcut"),
                     qPrintable(warning.toString()));
}

void DesktopPagesTest::signedOutAccountPopoverAndOddShortcutWidths()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *layout = session.shell.sidebarLayout();
    QPointer<QObject> panel;
    QVERIFY(!session.root->findChild<QObject *>("sidebarAccountPopover"));
    const auto item = [&session](const QString &name) {
        return visual(session.window->contentItem(), name);
    };
    const auto capture = qEnvironmentVariable("ECHO_SIDEBAR_COMPLETE_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    // All three grid cases use the production incremental shortcut model.
    for (const int count : {2, 1, 3}) {
        layout->restoreShortcuts();
        if (count == 1)
            layout->toggleShortcut("home");
        else if (count == 3)
            layout->toggleShortcut("free-listen");
        QTRY_COMPARE(layout->shortcuts().size(), count);
        for (const auto &entry : layout->shortcuts()) {
            auto *card = item("sidebarShortcut-" + entry.toMap()["key"].toString());
            QVERIFY(card);
            QTRY_COMPARE(card->width(), 93.0);
        }
        if (count % 2)
            QTRY_COMPARE(item("sidebarShortcutAdd")->width(), 93.0);
    }
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                session.shell.zoomIn();
            for (const bool saved : {false, true}) {
                StoredAccounts rows;
                if (saved)
                    rows.append({900001, "保存账号", {}, {}, 1});
                SavedAccountModelTestAccess::present(session.accounts.saved, rows, {});
                auto *trigger = item("sidebarAccountTrigger");
                QVERIFY(trigger);
                QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                                  trigger->mapToScene({14, 22}).toPoint());
                QTRY_VERIFY((panel = session.root->findChild<QObject *>("sidebarAccountPopover")));
                QTRY_VERIFY(panel->property("opened").toBool());
                auto *login = item("accountLoginAction");
                auto *switchAction = item("accountSignedOutSwitchAction");
                QVERIFY(login && switchAction);
                QCOMPARE(login->height(), 32.0);
                QCOMPARE(switchAction->height(), 32.0);
                QCOMPARE(switchAction->isVisible(), saved);
                auto *content = panel->property("contentItem").value<QQuickItem *>();
                QVERIFY(content);
                QVERIFY(content->width() >= login->width());
                if (!capture.isEmpty()) {
                    session.window->update();
                    QTest::qWait(100);
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/signed-out-%1-%2-%3.png")
                                      .arg(mode == ThemeStore::Light ? "light" : "dark")
                                      .arg(zoomed ? "scaled" : "normal")
                                      .arg(saved ? "saved" : "empty")));
                }
                QVERIFY(QMetaObject::invokeMethod(panel, "close"));
                QTRY_VERIFY(panel.isNull());
            }
        }
    }
    session.shell.resetZoom();
    auto *trigger = item("sidebarAccountTrigger");
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      trigger->mapToScene({14, 22}).toPoint());
    QTRY_VERIFY((panel = session.root->findChild<QObject *>("sidebarAccountPopover")));
    QTRY_VERIFY(panel->property("opened").toBool());
    auto *login = item("accountLoginAction");
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      login->mapToScene({login->width() / 2, 16}).toPoint());
    QTRY_VERIFY(session.accounts.controller.loginOpen());
    QTRY_VERIFY(panel.isNull());
    session.accounts.controller.closeLogin();
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::remainingSettingsProductionSections()
{
    Session session;
    session.shell.setMotionEnabled(false);
    LogService logging(session.directory.filePath("logs"), 2, false, &session.preferences);
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("playback.sqlite"));
    NetworkPreferences network(session.preferences, session.api);
    ShortcutService shortcuts(session.preferences, false);
    AboutService about(session.preferences, network);
    RelaunchService relaunch({}, {}, false);
    PluginRuntime plugins(session.engine, session.preferences,
                          {playback, session.theme, session.commands, session.router,
                           session.accounts.account, session.api},
                          session.directory.filePath("plugins"), true);
    SettingsDataService data(session.preferences, plugins, relaunch, network,
                             session.directory.path(), about.version());
    QVERIFY(session.create(&playback, &network, &logging, &relaunch));
    session.root->setProperty("shortcutService", QVariant::fromValue(&shortcuts));
    session.root->setProperty("aboutService", QVariant::fromValue(&about));
    session.root->setProperty("settingsDataService", QVariant::fromValue(&data));
    shortcuts.attach(session.window);
    // Native Popup Escape handling requires an active platform window;
    // rendering alone also works while the test process is in background.
    session.window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(session.window));
    QStringList errors;
    connect(&session.engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &warnings) {
        for (const auto &warning : warnings)
            if (warning.url().toString().contains("/Settings/"))
                errors.append(warning.toString());
    });
    session.router.navigate("settings");
    const auto captures = qEnvironmentVariable("ECHO_SETTINGS_PAGES_ARTIFACTS");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    for (bool dark : {false, true}) {
        session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
        for (int zoom : {100, 125}) {
            // 每次关闭会销毁完整弹层。重开后重新获取弹层、catalog 和 section loader，
            // 不要跨迭代保留这些对象的裸指针。
            QPointer<QObject> dialog = session.root->findChild<QObject *>("settingsDialog");
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->property("opened").toBool());
            auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
            QVERIFY(catalog);
            auto *loader = visual(session.window->contentItem(), "settingsSectionLoader");
            QVERIFY(loader);
            session.shell.resetZoom();
            if (zoom == 125)
                for (int i = 0; i < 5; ++i)
                    session.shell.zoomIn();
            int actionCount = 0;
            for (const auto *key : {"interface", "window", "font", "playback", "player",
                                    "spatialAudio", "lyric", "shortcuts", "audioDevice", "cast",
                                    "plugins", "data", "network", "experimental", "about"}) {
                catalog->select(key);
                QTRY_COMPARE(loader->property("source").toUrl(), catalog->source());
                QTRY_COMPARE(loader->property("status").toInt(), 1); // Loader.Ready
                auto *section = loader->property("item").value<QQuickItem *>();
                QVERIFY(section);
                QTest::qWait(50);
                // Walk real instantiated controls, including content below the
                // scroll viewport. Check allocated geometry after layout so a
                // row override/shrink cannot pass on implicit size alone.
                QList<QQuickItem *> pending{section};
                while (!pending.isEmpty()) {
                    auto *item = pending.takeLast();
                    pending.append(item->childItems());
                    if (!item->isVisible())
                        continue;
                    bool action = false;
                    for (auto *meta = item->metaObject(); meta; meta = meta->superClass()) {
                        const QByteArray name(meta->className());
                        action |= name == "EchoSoftButton" || name.startsWith("EchoSoftButton_") ||
                                  name == "EchoButton" || name.startsWith("EchoButton_");
                    }
                    if (!action)
                        continue;
                    ++actionCount;
                    const auto context = QString("%1 / %2: %3px")
                                             .arg(key, item->property("text").toString())
                                             .arg(item->height());
                    QVERIFY2(qAbs(item->height() - 36) < .01, qPrintable(context));
                    const auto sceneHeight = item->mapRectToScene(item->boundingRect()).height();
                    QVERIFY2(qAbs(sceneHeight - 36 * session.shell.uiScale()) < .05,
                             qPrintable(context));
                }
                if (!captures.isEmpty()) {
                    const auto pixels = session.window->grabWindow();
                    QVERIFY(!pixels.isNull());
                    QVERIFY(pixels.save(
                        captures +
                        QString("/%1-%2-%3.png").arg(key).arg(dark ? "dark" : "light").arg(zoom)));
                }
            }
            QVERIFY(actionCount >= 25);
            auto *changelog = visual(session.window->contentItem(), "settingsChangelog");
            QVERIFY(changelog);
            QMetaObject::invokeMethod(changelog, "clicked");
            QPointer<QObject> changelogDialog =
                session.root->findChild<QObject *>("settingsChangelogDialog");
            QVERIFY(changelogDialog);
            QTRY_VERIFY(changelogDialog->property("opened").toBool());
            QVERIFY(visual(session.window->contentItem(), "settingsChangelogText"));
            if (!captures.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captures +
                    QString("/changelog-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom)));
            QTest::keyClick(session.window, Qt::Key_Escape);
            QTRY_VERIFY(!changelogDialog || !changelogDialog->property("visible").toBool());
            QVERIFY2(dialog->property("opened").toBool(),
                     "Closing changelog must preserve settings");
            auto *declaration = visual(session.window->contentItem(), "settingsDisclaimer");
            QVERIFY(declaration);
            QMetaObject::invokeMethod(declaration, "clicked");
            QObject *disclaimerDialog = nullptr;
            QTRY_VERIFY((disclaimerDialog =
                             session.root->findChild<QObject *>("settingsDisclaimerDialog")) &&
                        disclaimerDialog->property("opened").toBool());
            if (!captures.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captures +
                    QString("/disclaimer-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom)));
            QTest::keyClick(session.window, Qt::Key_Escape);
            QTRY_VERIFY([&] {
                const auto *disclaimer =
                    session.root->findChild<QObject *>("settingsDisclaimerDialog");
                return disclaimer == nullptr || !disclaimer->property("visible").toBool();
            }());
            QVERIFY2(dialog->property("opened").toBool(),
                     "Closing disclaimer must preserve settings");
            catalog->select("data");
            QTRY_VERIFY(visual(session.window->contentItem(), "settingsCreateBackup"));
            QMetaObject::invokeMethod(visual(session.window->contentItem(), "settingsCreateBackup"),
                                      "clicked");
            QObject *backupDialog = nullptr;
            QTRY_VERIFY(
                (backupDialog = session.root->findChild<QObject *>("settingsBackupExportDialog")) &&
                backupDialog->property("opened").toBool());
            if (!captures.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captures +
                    QString("/backup-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom)));
            QTest::keyClick(session.window, Qt::Key_Escape);
            QMetaObject::invokeMethod(dialog, "close");
            QTRY_VERIFY(!dialog || !dialog->property("visible").toBool());
            QVERIFY(session.commands.invokePlayerCommand(ShellController::Cast));
            QObject *castPopup = nullptr;
            QTRY_VERIFY((castPopup = session.root->findChild<QObject *>("playbackCastPopup")) &&
                        castPopup->property("opened").toBool());
            QVERIFY(playback.cast()->browsing());
            if (!captures.isEmpty())
                QVERIFY(session.window->grabWindow().save(
                    captures +
                    QString("/cast-popup-%1-%2.png").arg(dark ? "dark" : "light").arg(zoom)));
            auto *castSettings = visual(session.window->contentItem(), "playbackCastSettings");
            QVERIFY(castSettings);
            QMetaObject::invokeMethod(castSettings, "clicked");
            // 弹层按需创建：settingsDialog 关闭后已释放，重新获取实例。
            QObject *reopened = nullptr;
            QTRY_VERIFY((reopened = session.root->findChild<QObject *>("settingsDialog")) &&
                        reopened->property("opened").toBool());
            auto *reopenedCatalog = reopened->property("catalog").value<SettingsCatalog *>();
            QVERIFY(reopenedCatalog);
            QTRY_COMPARE(reopenedCatalog->current(), QString("cast"));
            QTRY_VERIFY(visual(session.window->contentItem(), "settingsCastEnabled"));
        }
    }
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    about.shutdown();
    data.shutdown();
    shortcuts.shutdown();
    session.root.reset();
    session.window = nullptr;
    QSignalSpy pluginClosed(&plugins, &PluginRuntime::closed);
    plugins.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(pluginClosed.size(), 1, 15000);
    QSignalSpy playbackClosed(&playback, &PlaybackService::closed);
    playback.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(playbackClosed.size(), 1, 15000);
}

void DesktopPagesTest::settingsFilterAndUnsupportedProviderNeverLoadStub()
{
    SettingsCatalog catalog;
    QAbstractItemModelTester tester(&catalog,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(catalog.rowCount(), 15);
    catalog.setFilter(" 字 体 ");
    QVERIFY(catalog.rowCount() > 0);
    QCOMPARE(catalog.current(), QString("font"));
    catalog.setFilter("音效");
    QCOMPARE(catalog.rowCount(), 0);
    QVERIFY(catalog.source().isEmpty());
    catalog.select("spatialAudio");
    QVERIFY(catalog.source().isEmpty());
    // Check exact setting labels and aliases; generic proxy/log matches
    // do not cover the full settings search.
    for (const auto &keyword : {"proxyRules", "bypass", "代理密码", "播放器网络超时"}) {
        catalog.setFilter(QString::fromUtf8(keyword));
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(catalog.current(), QString("network"));
        QVERIFY(!catalog.source().isEmpty());
    }
    catalog.setFilter("在线插件源");
    QCOMPARE(catalog.rowCount(), 2); // Source lists this in plugins and network.
    QCOMPARE(catalog.current(), QString("plugins"));
    catalog.select("network");
    QCOMPARE(catalog.current(), QString("network"));
    for (const auto &keyword : {"API 响应体日志", "临时诊断日志", "诊断并重启"}) {
        catalog.setFilter(QString::fromUtf8(keyword));
        QCOMPARE(catalog.rowCount(), 1);
        QCOMPARE(catalog.current(), QString("experimental"));
    }
    catalog.setFilter("全屏按钮");
#ifdef Q_OS_MACOS
    QCOMPARE(catalog.rowCount(), 0);
#else
    QCOMPARE(catalog.current(), QString("interface"));
#endif
    catalog.setFilter("not-a-real-setting");
    QCOMPARE(catalog.rowCount(), 0);
    catalog.reset();
    QCOMPARE(catalog.current(), QString("interface"));
}

void DesktopPagesTest::globalFontUsesInstalledFamilyAndRestoresPreference()
{
    QTemporaryDir directory;
    QSettings preferences(directory.filePath("font.ini"), QSettings::IniFormat);
    ThemeStore theme(preferences);
    const auto family = QFontDatabase::families().value(0);
    QVERIFY(!family.isEmpty());
    QSignalSpy changed(&theme, &ThemeStore::fontChanged);
    theme.setGlobalFont(family);
    QCOMPARE(theme.globalFont(), family);
    QCOMPARE(theme.applicationFont().families().first(), family);
    QTRY_VERIFY(!changed.isEmpty());
    theme.setGlobalFont("no-such-installed-family");
    QCOMPARE(theme.globalFont(), family);
    preferences.sync();
    QSettings reopened(directory.filePath("font.ini"), QSettings::IniFormat);
    ThemeStore restored(reopened);
    QCOMPARE(restored.globalFont(), family);
    restored.setGlobalFont("system-ui");
}

void DesktopPagesTest::productionProfileSettingsAndSidebarOverlays()
{
    Session session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QTRY_VERIFY(visual(session.window->contentItem(), "profilePage"));
    QVERIFY(AccountServiceTestAccess::adopt(session.accounts.account, account(900001)));
    const auto profile =
        AccountServiceTestAccess::profileRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, profile,
        R"({"id":900001,"nickname":"Qt 测试账号","avatar_url":"","biography":"正式个人中心组件的领域呈现测试","grade":5,"gender":1,"province":"上海","city":"上海","visitors":9,"registered_at_seconds":1700000000,"listening_minutes":14409,"friends":0,"follows":5,"fans":1})");
    const auto grade =
        AccountServiceTestAccess::gradeRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, grade,
        R"({"user_id":900001,"grade":5,"listening_seconds":864577,"current_points":17120,"next_grade_points":20000,"next_grade":6})");
    const auto membership =
        AccountServiceTestAccess::vipRequest(session.accounts.account, session.api);
    AccountServiceTestAccess::complete(
        session.accounts.account, membership,
        R"({"user_id":900001,"user_type_code":0,"vip_type_code":0,"products":[{"active":true,"business":"concept","product":"svip","expires_at":"2030-01-02T03:04:05"}]})");
    session.router.invalidateAccount();
    session.router.navigate("profile");
    QTRY_VERIFY(visual(session.window->contentItem(), "profileSwitchAccount"));
    auto *profilePage = visual(session.window->contentItem(), "profilePage");
    QVERIFY(profilePage);
    QVERIFY(!profilePage->findChild<QObject *>("profileEditorDialog"));
    QVERIFY(!profilePage->findChild<QObject *>("profileSocialDrawer"));
    QVERIFY(!profilePage->findChild<QObject *>("profileDeviceDialog"));
    QPointer<QObject> gradeDialog;
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const bool zoomed : {false, true}) {
            session.shell.resetZoom();
            if (zoomed)
                session.shell.zoomIn();
            gradeDialog = session.openOwnedPopup("profileGradeDialogOwner");
            QVERIFY(gradeDialog);
            QTRY_VERIFY(gradeDialog->property("opened").toBool());
            auto *card = visual(session.window->contentItem(), "profileGradeCard");
            auto *outline = visual(session.window->contentItem(), "profileGradeCardOutline");
            auto *planet = visual(session.window->contentItem(), "profileGradePlanet");
            auto *track = visual(session.window->contentItem(), "profileGradeTrack");
            auto *refresh = visual(session.window->contentItem(), "profileGradeRefresh");
            auto *close = gradeDialog->property("closeButton").value<QQuickItem *>();
            QVERIFY(card && outline && planet && track && refresh && close);
            QTRY_VERIFY(card->height() >= 176);
            QCOMPARE(outline->size(), card->size());
            QCOMPARE(QQmlProperty(outline, "border.width").read().toReal(), 1.0);
            const auto border = QQmlProperty(outline, "border.color").read().value<QColor>();
            QVERIFY(qAbs(border.alphaF() - 0.72) < 0.01);
            QTRY_COMPARE(track->height(), 5.0);
            const auto refreshBackground = refresh->property("background").value<QQuickItem *>();
            QVERIFY(refreshBackground);
            QCOMPARE(QQmlProperty(refreshBackground, "border.width").read().toReal(), 0.0);
            QCOMPARE(close->size(), QSizeF(32, 32));
            const auto scale = session.shell.uiScale();
            const auto edge = close->mapToScene(QPointF(close->width(), 0));
            QTRY_VERIFY(qAbs(edge.x() - (gradeDialog->property("x").toReal() +
                                         (gradeDialog->property("width").toReal() - 24) * scale)) <
                        1);
            QVERIFY(qAbs(edge.y() - (gradeDialog->property("y").toReal() +
                                     gradeDialog->property("topPadding").toReal() * scale)) < 1);
            QTRY_VERIFY(qAbs(planet->x() + planet->width() - planet->parentItem()->width()) < 1);
            const auto captureDir = qEnvironmentVariable("ECHO_DESKTOP_PAGES_CAPTURE_DIR");
            if (!captureDir.isEmpty()) {
                QVERIFY(QDir().mkpath(captureDir));
                QVERIFY(session.window->grabWindow().save(
                    captureDir + QString("/grade-%1-%2.png")
                                     .arg(mode == ThemeStore::Light ? "light" : "dark")
                                     .arg(zoomed ? "125" : "100")));
            }
            QVERIFY(Test::prepareNativeInput(session.window));
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                              close->mapToScene(QPointF(16, 16)).toPoint());
            QTRY_VERIFY(gradeDialog.isNull());
        }
    }
    session.shell.resetZoom();
    auto editor = session.openOwnedPopup("profileEditorDialogOwner");
    QVERIFY(editor);
    QTRY_VERIFY(editor->property("opened").toBool());
    QCOMPARE(editor->property("draftName").toString(), session.accounts.account.nickname());
    auto *birthday = visual(session.window->contentItem(), "profileEditBirthday");
    QVERIFY(birthday);
    QVERIFY(birthday->property("value").toString().isEmpty());
    QSignalSpy picked(birthday, SIGNAL(picked(QString)));
    auto *year = birthday->findChild<QQuickItem *>("dateYear");
    QVERIFY(year);
    year->forceActiveFocus();
    QVERIFY(QMetaObject::invokeMethod(year, "editingFinished"));
    QCOMPARE(picked.size(), 0); // Focusing an empty birthday must not write today.
    const auto capture = qEnvironmentVariable("ECHO_DESKTOP_PAGES_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QDir().mkpath(capture);
        session.window->update();
        QTest::qWait(120);
        QVERIFY(session.window->grabWindow().save(capture + "/profile-editor.png"));
    }
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QTRY_VERIFY(editor.isNull());
    const auto friends = ProfileServiceTestAccess::socialRequest(session.profile, session.api, 0);
    ProfileServiceTestAccess::complete(session.profile, friends, socialRows(1000));
    auto social = session.openOwnedPopup("profileSocialDrawerOwner");
    QVERIFY(social);
    QTRY_VERIFY(social->property("opened").toBool());
    auto *socialHeader = visual(session.window->contentItem(), "profileSocialHeader");
    QVERIFY(socialHeader);
    QTRY_VERIFY(socialHeader->width() >= social->property("width").toReal());
    auto *socialClose = visual(session.window->contentItem(), "profileSocialClose");
    QVERIFY(socialClose);
    QTRY_VERIFY(qAbs(socialClose->mapToItem(socialHeader, QPointF(socialClose->width(), 0)).x() -
                     (socialHeader->width() - 24)) < 1);
    QPointer<QQuickItem> list = visual(session.window->contentItem(), "profileSocialList");
    QVERIFY(list);
    QTRY_VERIFY(visualCount(list, "profileSocialRow") > 0);
    QVERIFY(visualCount(list, "profileSocialRow") < 40);
    QCOMPARE(list->property("count").toInt(), 1000);
    QTRY_COMPARE(visualAll(list, "userIdentityBadge").size(), 3);
    auto *socialAvatar = visual(list, "socialAccountAvatar");
    QVERIFY(socialAvatar);
    QVERIFY(!socialAvatar->property("identityIcon").toUrl().isEmpty());
    if (!capture.isEmpty()) {
        QTest::qWait(80);
        QVERIFY(session.window->grabWindow().save(capture + "/profile-social-identities.png"));
    }
    list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
    QTest::qWait(60);
    QVERIFY(visualCount(list, "profileSocialRow") < 40);
    auto *reusedCard = visual(list, "profileSocialCard");
    QVERIFY(reusedCard);
    QCOMPARE(reusedCard->x(), 12.);
    QCOMPARE(reusedCard->width(), list->width() - 24.);
    // Retained/pooled delegates are allowed to keep display metadata. They
    // must not keep image work active or leak the first row into reused rows.
    int activeRows = 0;
    for (auto *row : visualAll(list, "profileSocialRow")) {
        auto *reusedAvatar = visual(row, "socialAccountAvatar");
        QVERIFY(reusedAvatar);
        auto *mark = reusedAvatar->findChild<QQuickItem *>("avatarIdentityMark");
        QVERIFY(mark);
        if (row->property("resourceActive").toBool()) {
            ++activeRows;
            QVERIFY(reusedAvatar->property("identityIcon").toUrl().isEmpty());
            QCOMPARE(visualAll(row, "userIdentityBadge").size(), 0);
        } else {
            QTRY_VERIFY(mark->property("source").toUrl().isEmpty());
        }
    }
    QVERIFY(activeRows > 0);
    if (!capture.isEmpty()) {
        // Earlier grade captures finish in dark mode. Explicitly select
        // light here so the two social captures cover distinct palettes.
        session.theme.setMode(ThemeStore::Light);
        QTest::qWait(80);
        QVERIFY(session.window->grabWindow().save(capture + "/profile-social.png"));
        session.theme.setMode(ThemeStore::Dark);
        QTest::qWait(120);
        QVERIFY(session.window->grabWindow().save(capture + "/profile-social-dark.png"));
        session.theme.setMode(ThemeStore::Light);
    }
    QVERIFY(QMetaObject::invokeMethod(social, "close"));
    QTRY_VERIFY(social.isNull());
    QTRY_VERIFY(list.isNull());
    auto devices = session.openOwnedPopup("profileDeviceDialogOwner");
    QVERIFY(devices);
    const auto deviceRead = ProfileServiceTestAccess::deviceRequest(session.profile, session.api);
    ProfileServiceTestAccess::complete(
        session.profile, deviceRead,
        R"([{"id":"domain-local","model":"设备 A","platform":"Android","location":"中国,上海","login_type":6,"active_at_seconds":1700000000,"new_at_seconds":1700000000,"current":true},{"id":"domain-other","model":"设备 B","platform":"Android","location":"中国,北京","login_type":1,"active_at_seconds":1690000000,"new_at_seconds":1680000000,"current":false}])");
    QTRY_VERIFY(devices->property("opened").toBool());
    QTRY_COMPARE(visualCount(session.window->contentItem(), "profileDeviceRow"), 2);
    auto *deviceList = visual(session.window->contentItem(), "profileDeviceList");
    QVERIFY(deviceList);
    QTRY_VERIFY(deviceList->height() >= 160);
    if (!capture.isEmpty()) {
        QTest::qWait(80);
        QVERIFY(session.window->grabWindow().save(capture + "/profile-devices.png"));
    }
    QVERIFY(QMetaObject::invokeMethod(devices, "close"));
    QTRY_VERIFY(devices.isNull());
    auto preferences = session.openOwnedPopup("listeningPreferencesDialogOwner");
    QVERIFY(preferences);
    auto &preferenceService = *session.profile.preferences();
    const auto prefRead = ListeningPreferencesTestAccess::read(preferenceService, session.api);
    ListeningPreferencesTestAccess::complete(preferenceService, prefRead, preferencesDomain());
    preferenceService.setStrength("L1", 20);
    QTRY_VERIFY(preferences->property("opened").toBool());
    QCOMPARE(preferences->property("width").toReal(), 640.);
    auto *preferenceScroll = visual(session.window->contentItem(), "listeningPreferencesScroll");
    QVERIFY(preferenceScroll);
    QTRY_VERIFY(preferenceScroll->height() > 350);
    auto *strength = visual(session.window->contentItem(), "preferenceStrengthL1");
    QVERIFY(strength);
    QVERIFY(strength->width() > 150);
    QVERIFY(strength->isEnabled());
    QPointer<QQuickItem> heldStrength(strength);
    preferenceService.setStrength("L1", 40);
    QTest::qWait(20);
    QVERIFY(heldStrength);
    QCOMPARE(visual(session.window->contentItem(), "preferenceStrengthL1"), strength);
    const auto first =
        strength->mapToScene(QPointF(strength->width() * 0.25, strength->height() / 2)).toPoint();
    const auto second =
        strength->mapToScene(QPointF(strength->width() * 0.75, strength->height() / 2)).toPoint();
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, first);
    QTest::mouseMove(session.window, second, 20);
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, second);
    QVERIFY(heldStrength);
    if (!capture.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(session.window->grabWindow().save(capture + "/listening-preferences.png"));
        session.theme.setMode(ThemeStore::Dark);
        QTest::qWait(120);
        QVERIFY(session.window->grabWindow().save(capture + "/listening-preferences-dark.png"));
        // Exercise the bounded option grid in both palettes. Widths select
        // five, three and two columns without resizing the application.
        preferences->setProperty("moreExpanded", true);
        preferenceService.toggleLanguage("19");
        preferenceService.toggleLanguage("22");
        preferenceService.toggleStyle("2");
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const auto width : {640., 520., 400.}) {
                preferences->setProperty("width", width);
                for (int tab = 0; tab < 2; ++tab) {
                    preferences->setProperty("moreTab", tab);
                    QTest::qWait(100);
                    preferenceScroll->setProperty(
                        "contentY", preferenceScroll->property("contentHeight").toReal() -
                                        preferenceScroll->height());
                    QTest::qWait(150);
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/preference-options-%1-%2-%3.png")
                                      .arg(tab == 0 ? "languages" : "styles")
                                      .arg(mode == ThemeStore::Light ? "light" : "dark")
                                      .arg(width)));
                }
            }
        }
        preferenceService.toggleLanguage("19");
        preferenceService.toggleLanguage("22");
        preferenceService.toggleStyle("2");
        preferences->setProperty("width", 640.);
        preferences->setProperty("moreExpanded", false);
        preferences->setProperty("moreTab", 0);
        preferenceScroll->setProperty("contentY", 0.);
        session.theme.setMode(ThemeStore::Light);
    }
    QVERIFY(strength->property("value").toDouble() > 65);
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QPointer<QObject> discard;
    QTRY_VERIFY((discard = preferences->findChild<QObject *>("listeningDiscardConfirm")));
    QTRY_VERIFY(discard->property("opened").toBool());
    QVERIFY(preferences->property("visible").toBool());
    QTest::keyClick(session.window, Qt::Key_Escape);
    QTRY_VERIFY(discard.isNull());
    preferenceService.discard();
    QVERIFY(QMetaObject::invokeMethod(preferences, "attemptClose"));
    QTRY_VERIFY(preferences.isNull());
    session.router.navigate("settings");
    QPointer<QObject> dialog;
    QTRY_VERIFY((dialog = session.root->findChild<QObject *>("settingsDialog")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    QTRY_VERIFY(visual(session.window->contentItem(), "settingsDefaultSearch"));
    const auto parent = dialog->property("parent").value<QQuickItem *>();
    QVERIFY(parent);
    QCOMPARE(dialog->property("width").toReal(),
             qMin(820.0, parent->width() - (parent->width() <= 640 ? 16 : 48)));
    QCOMPARE(dialog->property("height").toReal(),
             qMin(620.0, parent->height() - (parent->width() <= 640 ? 32 : 96)));
    catalog->select("font");
    QTRY_VERIFY(visual(session.window->contentItem(), "settingsGlobalFont"));
    QTRY_VERIFY(!visual(session.window->contentItem(), "settingsDefaultSearch"));
    catalog->select("experimental");
    catalog->setFilter("no-such-option");
    QTRY_VERIFY(!visual(session.window->contentItem(), "settingsGlobalFont"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(dialog.isNull());
    session.router.navigate("settings");
    QTRY_VERIFY((dialog = session.root->findChild<QObject *>("settingsDialog")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    catalog = dialog->property("catalog").value<SettingsCatalog *>();
    QVERIFY(catalog);
    QTRY_COMPARE(catalog->current(), QString("interface"));
    QTRY_VERIFY(visual(session.window->contentItem(), "settingsDefaultSearch"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(dialog.isNull());
    auto layout = session.openOwnedPopup("sidebarLayoutPopupOwner");
    QVERIFY(layout);
    QTRY_VERIFY(layout->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(layout, "close"));
    QTRY_VERIFY(layout.isNull());
    session.theme.setMode(ThemeStore::Dark);
    const auto directory = qEnvironmentVariable("ECHO_DESKTOP_PAGES_CAPTURE_DIR");
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        session.window->resize(1150, 800);
        session.window->update();
        QTest::qWait(200);
        QVERIFY(session.window->grabWindow().save(directory + "/profile-dark.png"));
        // Cached route pages are visually parented by RouteHost, while the
        // QObject owner belongs to the route loader rather than Main.qml.
        auto *profilePage = visual(session.window->contentItem(), "profilePage");
        QVERIFY(profilePage);
        gradeDialog = session.openOwnedPopup("profileGradeDialogOwner");
        QVERIFY(gradeDialog);
        QTRY_VERIFY(gradeDialog->property("opened").toBool());
        auto *planet = visual(session.window->contentItem(), "profileGradePlanet");
        QVERIFY(planet);
        // The hero uses space-between; SVG intrinsic dimensions must not
        // leave the illustration beside the level label.
        QTRY_VERIFY(qAbs(planet->x() + planet->width() - planet->parentItem()->width()) < 1);
        QTest::qWait(100);
        QVERIFY(session.window->grabWindow().save(directory + "/grade-dark.png"));
        QVERIFY(QMetaObject::invokeMethod(gradeDialog, "close"));
        QTRY_VERIFY(gradeDialog.isNull());
        session.router.navigate("settings");
        QTRY_VERIFY((dialog = session.root->findChild<QObject *>("settingsDialog")));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(visual(session.window->contentItem(), "settingsDefaultSearch"));
        QTest::qWait(80);
        QVERIFY(session.window->grabWindow().save(directory + "/settings-dark.png"));
        session.theme.setMode(ThemeStore::Light);
        QTest::qWait(80);
        QVERIFY(session.window->grabWindow().save(directory + "/settings-light.png"));
    }
    QCOMPARE(warnings.size(), 0);
}
