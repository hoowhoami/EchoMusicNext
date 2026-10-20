#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"

void ControlsUiTest::scrollContentCannotPaintOverRoundedCornersOrBorder()
{
    ControlsSession session;
    QVERIFY(session.create());
    if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Rounded content pixels require native RHI");
    session.window->resize(1100, 800);
    QTRY_COMPARE(session.window->height(), 800);
    const auto captureScene = [&]() -> QImage {
        QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
        if (!QTest::qWaitFor([&] {
                session.window->requestUpdate();
                return frames.size() >= 2;
            }))
            return {};
        QImage image;
        if (!QTest::qWaitFor([&] {
                image = session.window->grabWindow();
                return !image.isNull() &&
                       image.size() ==
                           (QSizeF(session.window->size()) * image.devicePixelRatio()).toSize();
            }))
            return {};
        return image;
    };
    session.theme.setMotionEnabled(false);
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            import EchoMusic.Theme
            Item {
                id: host
                property real panelScale: 1
                property bool padded: false
                property alias drawer: drawerOwner.item
                property alias dialog: dialogOwner.item
                function openDrawer(): void { drawerOwner.open(); }
                function openDialog(): void { dialogOwner.open(); }
                Component {
                    id: scrollingBody
                    Flickable {
                        id: scrollingFixture
                        property bool painted: true
                        contentWidth: width; contentHeight: 2000; clip: true
                        Rectangle { width: parent.width; height: 2000; color: scrollingFixture.painted ? Qt.rgba(147 / 255, 147 / 255, 147 / 255, 1) : "transparent" }
                    }
                }
                EchoPopupLoader {
                    id: drawerOwner; hostItem: host
                    sourceComponent: EchoDrawer {
                        uiScale: host.panelScale
                        contentItem: Loader { sourceComponent: scrollingBody }
                    }
                }
                EchoPopupLoader {
                    id: dialogOwner; hostItem: host
                    sourceComponent: EchoDialog {
                        uiScale: host.panelScale; preferredWidth: 420
                        height: 400; padding: host.padded ? 24 : 0
                        rightPadding: padding
                        contentItem: Loader { sourceComponent: scrollingBody }
                    }
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const qreal scale : {1., 1.25}) {
            host->setProperty("panelScale", scale);
            for (const bool frost : {false, true}) {
                session.theme.setFloatingFrosted(frost);
                for (const auto *kind : {"drawer", "dialog"}) {
                    QVERIFY(QMetaObject::invokeMethod(
                        host, QByteArray(kind) == "drawer" ? "openDrawer" : "openDialog"));
                    QPointer<QObject> popup = host->property(kind).value<QObject *>();
                    QVERIFY(popup);
                    QTRY_VERIFY(popup->property("opened").toBool());
                    QPointer<QQuickItem> surface =
                        popup->property("background").value<QQuickItem *>();
                    auto *content = popup->property("contentItem").value<QQuickItem *>();
                    QVERIFY(surface && content);
                    QPointer<QQuickItem> mask;
                    QTRY_VERIFY(popup && surface &&
                                (mask = popup->findChild<QQuickItem *>("popupContentRoundedMask")));
                    auto *view = content->property("item").value<QQuickItem *>();
                    QVERIFY(view);
                    QVERIFY(!QQmlProperty(content, "layer.enabled").read().toBool());
                    QVERIFY(findVisual(content->parentItem(), "popupContentCapture"));
                    QVERIFY(findVisual(content->parentItem(), "popupContentMaskEffect"));
                    view->setProperty("contentY", 2000. - view->height());
                    view->setProperty("painted", false);
                    const auto empty = captureScene();
                    view->setProperty("painted", true);
                    const auto filled = captureScene();
                    QVERIFY(popup && surface);
                    QVERIFY(!empty.isNull() && !filled.isNull());
                    QCOMPARE(empty.size(), filled.size());
                    const auto pixel = [&](const QImage &image, QPointF p) {
                        p = surface->mapToScene(p) * image.devicePixelRatio();
                        return image.pixelColor(qFloor(p.x()), qFloor(p.y()));
                    };
                    for (const auto &p : {QPointF(1, 1), QPointF(surface->width() - 2, 1),
                                          QPointF(1, surface->height() - 2),
                                          QPointF(surface->width() - 2, surface->height() - 2),
                                          QPointF(.1, surface->height() / 2)}) {
                        const auto a = pixel(empty, p), b = pixel(filled, p);
                        QVERIFY2(qAbs(a.red() - b.red()) < 4 && qAbs(a.green() - b.green()) < 4 &&
                                     qAbs(a.blue() - b.blue()) < 4,
                                 qPrintable(QString("%1 corner/border changed at %2,%3: %4 -> %5")
                                                .arg(kind)
                                                .arg(p.x())
                                                .arg(p.y())
                                                .arg(a.name(), b.name())));
                    }
                    const auto inside =
                        pixel(filled, {surface->width() / 2, surface->height() - 20});
                    QVERIFY(qAbs(inside.red() - 147) < 4);
                    const auto captures = qEnvironmentVariable("ECHO_PANEL_CAPTURES");
                    if (!captures.isEmpty()) {
                        QDir().mkpath(captures);
                        QVERIFY(filled.save(captures + QString("/scroll-corners-%1-%2-%3-%4.png")
                                                           .arg(kind)
                                                           .arg(int(mode))
                                                           .arg(scale)
                                                           .arg(frost)));
                    }
                    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                    QTRY_VERIFY(popup.isNull() && mask.isNull());
                }
            }
        }
    }
    host->setProperty("padded", true);
    QVERIFY(QMetaObject::invokeMethod(host, "openDialog"));
    QPointer<QObject> popup = host->property("dialog").value<QObject *>();
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("opened").toBool());
    QVERIFY(!popup->findChild<QQuickItem *>("popupContentRoundedMask"));
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(popup.isNull());
}

void ControlsUiTest::frostedCornersRetainAntialiasCoverage()
{
    ControlsSession session;
    QVERIFY(session.create());
    if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Corner coverage needs native RHI");
    session.window->setColor(Qt::black);
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                id: host
                width: 720; height: 290
                property real panelScale: 1
                Rectangle { anchors.fill: parent; color: "black" }
                EchoBackdropRoot {
                    visible: false
                    anchors.fill: parent
                    Rectangle { anchors.fill: parent; color: "white" }
                }
                EchoFloatingSurface {
                    objectName: "coverageProbe"
                    x: 400; y: 80; width: 100; height: 100; radius: 20
                    scale: host.panelScale; transformOrigin: Item.TopLeft
                    color: "transparent"; border.width: 0
                    shadowEnabled: false; frostedEnabled: true
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    auto *surface = findVisual(host, "coverageProbe");
    QVERIFY(surface);
    QTRY_VERIFY(surface->property("hasBackdrop").toBool());
    for (const qreal scale : {1., 1.25}) {
        host->setProperty("panelScale", scale);
        QTest::qWait(80);
        const auto frame = session.window->grabWindow();
        QVERIFY(!frame.isNull());
        const auto origin = surface->mapToScene({0, 0});
        const auto ratio = frame.devicePixelRatio();
        const int extent = qCeil(20 * scale * ratio);
        int partial = 0;
        for (int y = 0; y < extent; ++y)
            for (int x = 0; x < extent; ++x) {
                const auto pixel = frame.pixelColor(qRound(origin.x() * ratio) + x,
                                                    qRound(origin.y() * ratio) + y);
                partial += pixel.red() > 20 && pixel.red() < 235;
            }
        qInfo() << "Partial corner coverage pixels:" << scale << partial;
        QVERIFY2(partial > 12, "Frosted masks must retain a smooth, partially covered edge");
    }
}

void ControlsUiTest::colorPresetsKeepVisibleEdgesInBothThemes()
{
    ControlsSession session;
    QVERIFY(session.create());
    if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Color swatch pixels require native RHI");
    session.window->resize(800, 700);
    session.theme.setMotionEnabled(false);
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                property alias popup: picker
                EchoColorPickerDialog {
                    id: picker
                    title: "选择歌词颜色"
                    value: "#ffffff"
                    presets: ["#ffffff", "#fafafa", "#000000", "#31cfa1"]
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    auto *popup = host->property("popup").value<QObject *>();
    QVERIFY(popup);
    QSignalSpy confirmed(popup, SIGNAL(confirmed(QString)));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const qreal scale : {1., 1.25}) {
            popup->setProperty("uiScale", scale);
            QVERIFY(QMetaObject::invokeMethod(popup, "open"));
            QTRY_VERIFY(popup->property("opened").toBool());
            QVERIFY(Test::prepareNativeInput(session.window));
            Test::moveNativePointer(session.window, QPoint(10, 10));
            auto *white = session.item("colorPreset-0");
            auto *nearWhite = session.item("colorPreset-1");
            auto *black = session.item("colorPreset-2");
            auto *mint = session.item("colorPreset-3");
            QVERIFY(white && nearWhite && black && mint);
            const auto snapshot = [&] {
                session.window->update();
                QTest::qWait(80);
                return session.window->grabWindow();
            };
            const auto selected = snapshot();
            const auto checkVisibleEdge = [&](const QImage &image, QQuickItem *swatch) {
                const auto dpr = image.width() / qreal(session.window->width());
                const auto sample = [&](qreal x) {
                    const auto point = swatch->mapToScene({x, swatch->height() / 2}) * dpr;
                    return image.pixelColor(qRound(point.x()), qRound(point.y()));
                };
                return qAbs(qGray(sample(.35).rgb()) - qGray(sample(-2).rgb())) > 12;
            };
            QVERIFY(checkVisibleEdge(selected, white));
            QVERIFY(checkVisibleEdge(selected, nearWhite));
            QVERIFY(checkVisibleEdge(selected, black));
            const auto dpr = selected.width() / qreal(session.window->width());
            const auto center = white->mapToScene({white->width() / 2, white->height() / 2}) * dpr;
            int checkInk = 0;
            const int extent = qCeil(8 * scale * dpr);
            for (int y = qRound(center.y()) - extent; y < qRound(center.y()) + extent; ++y)
                for (int x = qRound(center.x()) - extent; x < qRound(center.x()) + extent; ++x)
                    checkInk += qGray(selected.pixel(x, y)) < 90;
            QVERIFY(checkInk > 8);
            const auto position =
                mint->mapToScene({mint->width() / 2, mint->height() / 2}).toPoint();
            Test::moveNativePointer(session.window, position);
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, position);
            QTRY_COMPARE(popup->property("draft").toString(), QString("#31cfa1"));
            const auto unselected = snapshot();
            QVERIFY(checkVisibleEdge(unselected, white));
            const auto captures = qEnvironmentVariable("ECHO_SELECT_COLOR_CAPTURES");
            if (!captures.isEmpty()) {
                QDir().mkpath(captures);
                const auto name = QString("/swatches-%1-%2").arg(int(mode)).arg(scale);
                QVERIFY(selected.save(captures + name + "-selected.png"));
                QVERIFY(unselected.save(captures + name + "-unselected.png"));
            }
            QVERIFY(QMetaObject::invokeMethod(popup, "close"));
            QTRY_VERIFY(!popup->property("visible").toBool());
            QCOMPARE(popup->property("value").toString(), QString("#ffffff"));
            QCOMPARE(confirmed.size(), 0);
        }
    }
}

void ControlsUiTest::backdropSourceIsWindowLocalAndReleasedWhenHidden()
{
    QQuickWindow first, second;
    auto firstRoot = std::make_unique<EchoBackdropRoot>(first.contentItem());
    EchoBackdropRoot secondRoot(second.contentItem());
    QQuickItem popupParent(first.contentItem());
    QQuickItem surface(&popupParent);
    surface.setSize({200, 100});
    surface.setPosition({10, 20});
    popupParent.setPosition({80, 100});
    popupParent.setScale(1.25);
    EchoBackdropSampler sampler;
    sampler.setSurface(&surface);
    QVERIFY(!sampler.source());
    sampler.setEnabled(true);
    QCOMPARE(sampler.source(), firstRoot.get());
    QCOMPARE(sampler.sourceRect(), QRectF(62.5, 95, 310, 185));
    QSignalSpy geometry(&sampler, &EchoBackdropSampler::changed);
    popupParent.setX(100);
    QVERIFY(!geometry.isEmpty());
    QCOMPARE(sampler.sourceRect().x(), 82.5);
    surface.setParentItem(second.contentItem());
    QCOMPARE(sampler.source(), &secondRoot);
    surface.setParentItem(&popupParent);
    auto pageRoot = std::make_unique<EchoBackdropRoot>(first.contentItem());
    QCOMPARE(sampler.source(), pageRoot.get());
    pageRoot->setSamplingActive(false);
    QCOMPARE(sampler.source(), firstRoot.get());
    pageRoot->setSamplingActive(true);
    QCOMPARE(sampler.source(), pageRoot.get());
    firstRoot.reset();
    QCOMPARE(sampler.source(), pageRoot.get());
    pageRoot.reset();
    QVERIFY(!sampler.source());
    sampler.setEnabled(false);
    QVERIFY(sampler.sourceRect().isEmpty());
}

void ControlsUiTest::backdropSamplerTracksBothItemTrees()
{
    QQuickWindow window;
    QQuickItem common(window.contentItem()), sourceParent(&common), otherParent(&common);
    auto source = std::make_unique<EchoBackdropRoot>(&sourceParent);
    source->setSize({500, 400});
    QQuickItem popupParent(&common), surface(&popupParent);
    surface.setPosition({150, 80});
    surface.setSize({200, 180});
    EchoBackdropSampler sampler;
    sampler.setSurface(&surface);
    sampler.setPadding(72);
    sampler.setEnabled(true);
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("testSampler", &sampler);
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
ShaderEffectSource { sourceRect: testSampler.sourceRect }
)",
                      QUrl());
    QScopedPointer<QObject> capture(component.create());
    QVERIFY2(capture, qPrintable(component.errorString()));
    const auto captured = [&] { return capture->property("sourceRect").toRectF(); };
    QCOMPARE(captured(), QRectF(78, 8, 344, 324));
    QSignalSpy changed(&sampler, &EchoBackdropSampler::changed);
    source->setX(40);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(captured(), QRectF(38, 8, 344, 324));
    const auto expected = [&] {
        return surface.mapRectToItem(sampler.source(), QRectF(-72, -72, 344, 324));
    };
    sourceParent.setX(17);
    QCOMPARE(captured(), expected());
    sourceParent.setScale(1.25);
    QCOMPARE(captured(), expected());
    source->setScale(1.2);
    source->setRotation(17);
    QCOMPARE(captured(), expected());
    changed.clear();
    source->setTransformOrigin(QQuickItem::TopLeft);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(captured(), expected());
    source->setWidth(700);
    QCOMPARE(captured(), expected());
    popupParent.setPosition({30, 20});
    QCOMPARE(captured(), expected());
    changed.clear();
    common.setX(10);
    QCOMPARE(changed.size(), 1); // The shared ancestor is not connected twice.
    source->setParentItem(&otherParent);
    QCOMPARE(captured(), expected());
    changed.clear();
    sourceParent.setX(50);
    QVERIFY(changed.isEmpty()); // Retired ancestry no longer invalidates captures.
    otherParent.setY(30);
    QVERIFY(!changed.isEmpty());
    QCOMPARE(captured(), expected());
    auto replacement = std::make_unique<EchoBackdropRoot>(&sourceParent);
    QCOMPARE(sampler.source(), replacement.get());
    QCOMPARE(captured(), expected());
    changed.clear();
    source->setX(80);
    QVERIFY(changed.isEmpty());
    replacement->setX(20);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(captured(), expected());
    replacement.reset();
    QCOMPARE(sampler.source(), source.get());
    QCOMPARE(captured(), expected());
    sampler.setEnabled(false);
    QVERIFY(captured().isEmpty());
    changed.clear();
    source->setX(120);
    otherParent.setY(50);
    QVERIFY(changed.isEmpty());
    sampler.setEnabled(true);
    QCOMPARE(captured(), expected());
    source.reset();
    QVERIFY(!sampler.source());
    QVERIFY(captured().isEmpty());
}

void ControlsUiTest::floatingSurfaceMatchesRendererCapabilities()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("BackdropHarness.qml")));
    auto *surface = session.item("frostProbe");
    QVERIFY(surface);
    const bool supported = supportsFloatingEffects(session.window);
    qInfo() << "Surface graphics API:" << session.window->rendererInterface()->graphicsApi()
            << "shader effects supported:" << supported;
    if (qEnvironmentVariable("QT_QUICK_BACKEND") == "software")
        QCOMPARE(session.window->rendererInterface()->graphicsApi(),
                 QSGRendererInterface::Software);
    QTRY_COMPARE(surface->property("shaderEffectsSupported").toBool(), supported);
    session.theme.setFloatingFrosted(true);
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const qreal scale : {1., 1.25}) {
            surface->setTransformOrigin(QQuickItem::TopLeft);
            surface->setScale(scale);
            auto *body = findVisual(surface, "floatingSurfaceBody");
            QVERIFY(body);
            QCOMPARE(QQmlProperty(body, "layer.enabled").read().toBool(), supported);
            if (!supported) {
                QVERIFY(surface->findChildren<EchoBackdropSampler *>().isEmpty());
                QVERIFY(!findVisual(surface, "floatingBackdropCapture"));
                for (auto *object : surface->findChildren<QObject *>())
                    QVERIFY(!object->inherits("QQuickShaderEffect"));
                QTest::qWait(80);
                const auto image = session.window->grabWindow();
                QVERIFY(!image.isNull());
                const auto fill = surface->property("color").value<QColor>();
                QCOMPARE(fill.alpha(), 255);
                for (const QPointF local : {QPointF(35, 90), QPointF(145, 90)}) {
                    const auto pixel = surface->mapToScene(local) * image.devicePixelRatio();
                    QCOMPARE(image.pixelColor(qRound(pixel.x()), qRound(pixel.y())).rgba(),
                             fill.rgba());
                }
                const auto captures = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
                if (!captures.isEmpty()) {
                    QDir().mkpath(captures);
                    QVERIFY(
                        image.save(captures + QString("/solid-%1-%2.png").arg(mode).arg(scale)));
                }
            }
        }
    }
    surface->setVisible(false);
    QTRY_VERIFY(surface->findChildren<EchoBackdropSampler *>().isEmpty());
    QVERIFY(!surface->property("hasBackdrop").toBool());
    surface->setVisible(true);
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::floatingFrostSamplesTheSceneAndStopsOnHide()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("BackdropHarness.qml")));
    auto *surface = session.item("frostProbe");
    QVERIFY(surface);
    QVERIFY(!surface->property("hasBackdrop").toBool());
    qInfo() << "Backdrop graphics API:" << session.window->rendererInterface()->graphicsApi();
    const bool supported = supportsFloatingEffects(session.window);
    session.theme.setFloatingFrosted(true);
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    if (QGuiApplication::platformName() != "offscreen" && supported) {
        QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
        session.window->update();
        QTRY_VERIFY(!frames.isEmpty());
        QTest::qWait(100);
        const auto image = session.window->grabWindow();
        QVERIFY(!image.isNull());
        const auto dpr = session.window->devicePixelRatio();
        const auto pixel = [&](int x) {
            return image.pixelColor(qRound(x * dpr), qRound(160 * dpr));
        };
        // The 18% exposed backdrop must differ across the red/blue scene;
        // around the boundary it must contain both colors, not a flat tint.
        const auto left = pixel(175), middle = pixel(220), right = pixel(265);
        QVERIFY(left.red() > right.red() + 20);
        QVERIFY(right.blue() > left.blue() + 20);
        QVERIFY(middle.red() > right.red() + 5);
        QVERIFY(middle.blue() > left.blue() + 5);
        // CSS sigma=24 spreads the edge well beyond a 24px radius. A
        // translucent tint or the former narrow MultiEffect blur fails here.
        QVERIFY(pixel(196).blue() > left.blue() + 3);
        QVERIFY(pixel(244).red() > right.red() + 3);
        qInfo() << "Frost edge pixels:" << left << pixel(196) << middle << pixel(244) << right;
        const auto captures = qEnvironmentVariable("ECHO_THEME_SCREENSHOTS");
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            QVERIFY(image.save(captures + "/floating-frost-edge.png"));
        }
        session.root->setProperty("sourceOffset", 40.);
        QTest::qWait(120);
        const auto moved = session.window->grabWindow();
        QVERIFY(!moved.isNull());
        const auto movedMiddle = moved.pixelColor(qRound(220 * dpr), qRound(160 * dpr));
        QVERIFY(movedMiddle.red() > middle.red() + 5);
        QVERIFY(movedMiddle.blue() < middle.blue() - 5);
        auto *sampler = surface->findChild<EchoBackdropSampler *>();
        auto *capture = findVisual(surface, "floatingBackdropCapture");
        QVERIFY(sampler && capture);
        QCOMPARE(capture->property("sourceRect").toRectF(), sampler->sourceRect());
        if (!captures.isEmpty())
            QVERIFY(moved.save(captures + "/floating-frost-moved.png"));
    }
    surface->setVisible(false);
    QTRY_VERIFY(!surface->property("hasBackdrop").toBool());
    surface->setVisible(true);
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    session.theme.setFloatingFrosted(false);
    QTRY_VERIFY(!surface->property("hasBackdrop").toBool());
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::floatingEffectsAreOwnedByVisibleSurfaces()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("BackdropHarness.qml")));
    session.window->resize(1150, 750);
    auto *surface = session.item("frostProbe");
    QVERIFY(surface);
    const bool supported = supportsFloatingEffects(session.window);
    surface->setSize({600, 620});
    surface->setVisible(false);
    session.theme.setFloatingFrosted(true);
    const int holdMs = qBound(0, qEnvironmentVariableIntValue("ECHO_FLOATING_PROFILE_MS"), 30000);
    QTimer sceneMotion;
    if (qEnvironmentVariableIsSet("ECHO_FLOATING_ANIMATED")) {
        auto *backdropRoot = session.root->findChild<EchoBackdropRoot *>();
        QVERIFY(backdropRoot && !backdropRoot->childItems().isEmpty());
        auto *movingItem = backdropRoot->childItems().first();
        connect(&sceneMotion, &QTimer::timeout, movingItem, [movingItem, frame = 0]() mutable {
            const auto position = frame++ % 120;
            movingItem->setX(position < 60 ? position / 3. : (120 - position) / 3.);
        });
        sceneMotion.start(16);
    }
    const auto samplerCount = [&] {
        QSet<QObject *> visited;
        int count = 0;
        const auto visit = [&](auto &&self, QObject *object) -> void {
            if (!object || visited.contains(object))
                return;
            visited.insert(object);
            count += qobject_cast<EchoBackdropSampler *>(object) != nullptr;
            for (auto *child : object->children())
                self(self, child);
            if (auto *item = qobject_cast<QQuickItem *>(object))
                for (auto *child : item->childItems())
                    self(self, child);
        };
        visit(visit, session.root.data());
        return count;
    };
    const auto phase = [&](const char *name) {
        QTest::qWait(150);
        qInfo() << "Floating phase" << name << "pid" << QCoreApplication::applicationPid()
                << "samplers" << samplerCount();
        const auto cpu = std::clock();
        QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
        QElapsedTimer wall;
        wall.start();
        QTest::qWait(qMax(100, holdMs));
        qInfo() << "Floating CPU" << name << "wall-ms" << wall.elapsed() << "cpu-ms"
                << 1000. * (std::clock() - cpu) / CLOCKS_PER_SEC << "frames" << frames.size();
    };
    phase("empty");
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                property int amount: 32
                Repeater {
                    model: parent.amount
                    EchoFloatingSurface { width: 200; height: 180; visible: false }
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> bulk(component.create());
    QVERIFY2(bulk, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(bulk.data());
    QVERIFY(host);
    host->setParent(session.root.data());
    host->setParentItem(session.window->contentItem());
    if (holdMs)
        host->setProperty("amount", 500);
    phase("hidden");
    const auto hiddenSamplers = samplerCount();
    surface->setProperty("frostedEnabled", false);
    surface->setVisible(true);
    phase("plain");
    const auto plainSamplers = samplerCount();
    surface->setProperty("frostedEnabled", true);
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    phase("frost");
    const auto active = surface->findChildren<EchoBackdropSampler *>();
    QCOMPARE(active.isEmpty(), !supported);
    QPointer<EchoBackdropSampler> ownedSampler = active.isEmpty() ? nullptr : active.first();
    QList<QPointer<EchoBackdropSampler>> retiredSamplers;
    for (int i = 0; i < 20; ++i) {
        for (auto *sampler : surface->findChildren<EchoBackdropSampler *>())
            retiredSamplers.append(sampler);
        surface->setVisible(false);
        QTest::qWait(20);
        surface->setVisible(true);
        QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
        QTest::qWait(20);
    }
    surface->setVisible(false);
    phase("closed-after-20");
    bulk.reset();
    session.engine.collectGarbage();
    phase("destroyed");
    QCOMPARE(hiddenSamplers, 0);
    QCOMPARE(plainSamplers, 0);
    QVERIFY(ownedSampler.isNull());
    QCOMPARE(samplerCount(), 0);
    for (const auto &sampler : retiredSamplers)
        QVERIFY(sampler.isNull());
    surface->setVisible(true);
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    session.window->hide();
    QTRY_COMPARE(samplerCount(), 0);
    session.window->show();
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    session.window->showMinimized();
    QTRY_COMPARE(samplerCount(), 0);
    session.window->showNormal();
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    // The window contentItem's visibility is owned by QQuickWindow's
    // asynchronous expose/minimize path. Exercise an ordinary ancestor.
    QQuickItem ancestor(session.window->contentItem());
    surface->setParentItem(&ancestor);
    ancestor.setVisible(false);
    QTRY_COMPARE(samplerCount(), 0);
    ancestor.setVisible(true);
    QTRY_COMPARE(surface->property("hasBackdrop").toBool(), supported);
    surface->setParentItem(session.window->contentItem());
    surface->setProperty("frostedEnabled", false);
    QTRY_COMPARE(samplerCount(), 0);
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::floatingBorderCompositesOverItsOwnFill()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    session.window->setHeight(440);
    session.root->setProperty("surfaceProbe", true);
    auto *surface = session.item("borderProbeSurface");
    QVERIFY(surface);
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const qreal scale : {1.0, 1.25, 1.5}) {
            session.root->setProperty("surfaceScale", scale);
            // An offscreen window has no native RHI framebuffer to sample;
            // keep the real pixel assertions in the Cocoa/device run.
            if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
                continue;
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
            const auto image = session.window->grabWindow();
            QVERIFY(!image.isNull());
            const auto dpr = session.window->devicePixelRatio();
            const auto pixel = [&](const QPointF &local) {
                const auto point = surface->mapToScene(local) * dpr;
                return image.pixelColor(qFloor(point.x()), qFloor(point.y()));
            };
            const auto fill = session.theme.elevated(), tone = session.theme.floatingTone();
            const QColor expected(qRound(fill.red() * 0.9 + tone.red() * 0.1),
                                  qRound(fill.green() * 0.9 + tone.green() * 0.1),
                                  qRound(fill.blue() * 0.9 + tone.blue() * 0.1));
            // The two straight-edge points cover different saturated
            // backgrounds. CSS paints the surface under its border too.
            for (const qreal y : {60.0, 180.0}) {
                const auto edge = pixel({0.5, y}), interior = pixel({3, y});
                QVERIFY2(qAbs(edge.red() - expected.red()) <= 3 &&
                             qAbs(edge.green() - expected.green()) <= 3 &&
                             qAbs(edge.blue() - expected.blue()) <= 3,
                         "Border must composite over the panel fill, not the card behind it");
                QVERIFY(qAbs(interior.red() - fill.red()) <= 2 &&
                        qAbs(interior.green() - fill.green()) <= 2 &&
                        qAbs(interior.blue() - fill.blue()) <= 2);
            }
            const auto directory = qEnvironmentVariable("ECHO_CONTROLS_CAPTURE_DIR");
            if (!directory.isEmpty()) {
                QVERIFY(QDir().mkpath(directory));
                QVERIFY(image.save(directory + QString("/floating-border-%1-%2.png")
                                                   .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                                   .arg(scale)));
            }
        }
    }
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::themeCustomColorHoverAndRenderedPixels()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    auto *accent = session.item("accentButton");
    auto *glyph = findVisual(accent, "buttonGlyph");
    auto *iconButton = session.item("iconButton");
    auto *tinted = session.item("tintGlyph");
    auto *fixed = session.item("fixedGlyph");
    QVERIFY(glyph && iconButton && tinted && fixed);
    const auto artifacts = qEnvironmentVariable("ECHO_CONTROLS_REPORT_DIR");
    for (auto mode : {ThemeStore::Mode::Light, ThemeStore::Mode::Dark}) {
        session.theme.setMode(mode);
        session.theme.setAccent(QColor("#bc4477"));
        QTRY_COMPARE(glyph->property("color").value<QColor>(), session.theme.accentText());
        QCOMPARE(fixed->property("color").value<QColor>(), QColor(Qt::black));
        for (QColor tint : {QColor("#cc3576"), QColor("#318adb"), QColor(40, 160, 210, 166)}) {
            session.root->setProperty("explicitColor", tint);
            QTRY_COMPARE(tinted->property("status").toInt(), 1);
            QCOMPARE(tinted->property("color").value<QColor>(), tint);
            if (QGuiApplication::platformName() == "offscreen")
                continue;
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
            const auto image = session.window->grabWindow();
            QVERIFY(!image.isNull());
            const auto scene = tinted->mapToScene(QPointF());
            const auto dpr = session.window->devicePixelRatio();
            const QRect area(int(scene.x() * dpr), int(scene.y() * dpr), int(tinted->width() * dpr),
                             int(tinted->height() * dpr));
            const auto background = session.theme.main();
            const QColor expected(
                qRound(tint.red() * tint.alphaF() + background.red() * (1 - tint.alphaF())),
                qRound(tint.green() * tint.alphaF() + background.green() * (1 - tint.alphaF())),
                qRound(tint.blue() * tint.alphaF() + background.blue() * (1 - tint.alphaF())));
            int hits = 0;
            for (int y = area.top(); y <= area.bottom(); ++y)
                for (int x = area.left(); x <= area.right(); ++x) {
                    const auto pixel = image.pixelColor(x, y);
                    hits += qAbs(pixel.red() - expected.red()) <= 3 &&
                            qAbs(pixel.green() - expected.green()) <= 3 &&
                            qAbs(pixel.blue() - expected.blue()) <= 3;
                }
            QVERIFY2(hits > 8, "Rendered icon must have the requested color, not merely a "
                               "matching QML property");
            if (!artifacts.isEmpty()) {
                QVERIFY(QDir().mkpath(artifacts));
                QVERIFY(image.save(artifacts + (mode == ThemeStore::Mode::Dark
                                                    ? "/controls-dark.png"
                                                    : "/controls-light.png")));
            }
        }
    }
    Test::moveNativePointer(
        session.window,
        iconButton->mapToScene(QPointF(iconButton->width() / 2, iconButton->height() / 2))
            .toPoint());
    QTRY_VERIFY(iconButton->property("hovered").toBool());
    QCOMPARE(iconButton->property("foreground").value<QColor>(), session.theme.text());
    iconButton->setProperty("enabled", false);
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::sourceActionSurfacesAndSelectStates()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    auto *neutral = session.item("neutralButton");
    auto *accent = session.item("accentButton");
    auto *select = session.item("styledSelect");
    auto *danger = session.item("dangerButton");
    QVERIFY(neutral && accent && select && danger);
    auto background = [](QQuickItem *control) {
        return control->property("background").value<QQuickItem *>();
    };
    const auto color = [&](QQuickItem *control) {
        return background(control)->property("color").value<QColor>();
    };
    auto alpha = [](QColor value, qreal amount) {
        value.setAlphaF(amount);
        return value;
    };
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (bool floating : {false, true}) {
            session.theme.setMode(mode);
            session.theme.selectSolid(QColor("#7da3ce"));
            session.theme.setAccent(QColor("#bc4477"));
            session.root->setProperty("floatingControls", floating);
            const auto tone = floating ? session.theme.floatingTone() : session.theme.tone();
            const auto mouse = [&](QQuickItem *item) {
                Test::moveNativePointer(
                    session.window,
                    item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            };
            Test::moveNativePointer(session.window, QPoint(700, 250));
            QTRY_COMPARE(color(danger), alpha(QColor("#ef4444"), 0.08));
            mouse(danger);
            QTRY_COMPARE(color(danger), alpha(QColor("#ef4444"), 0.12));
            const auto dangerPoint =
                danger->mapToScene(QPointF(danger->width() / 2, danger->height() / 2)).toPoint();
            QVERIFY(Test::prepareNativeInput(session.window));
            QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, dangerPoint);
            QTRY_VERIFY(danger->property("down").toBool());
            QTRY_COMPARE(color(danger), alpha(QColor("#ef4444"), 0.16));
            QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, dangerPoint);
            Test::moveNativePointer(session.window, QPoint(700, 250));
            QTRY_COMPARE(color(neutral), alpha(tone, 0.05));
            QCOMPARE(background(neutral)->property("radius").toReal(), 6.0);
            QCOMPARE(select->implicitHeight(), 36.0);
            QCOMPARE(background(select)->property("radius").toReal(), 6.0);
            // The actual render must preserve the 5% translucent fill: a shadow
            // painted inside would silently add another 6% black over it.
            if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
                QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                session.window->update();
                QTRY_VERIFY(!frames.isEmpty());
                const auto image = session.window->grabWindow();
                const auto dpr = session.window->devicePixelRatio();
                const auto point =
                    neutral->mapToScene(QPointF(neutral->width() / 2, neutral->height() - 7));
                const auto pixel =
                    image.pixelColor(qRound(point.x() * dpr), qRound(point.y() * dpr));
                const auto base = floating ? session.theme.elevated() : session.theme.main();
                const auto fill = alpha(tone, 0.05);
                const QColor expected(
                    qRound(fill.red() * fill.alphaF() + base.red() * (1 - fill.alphaF())),
                    qRound(fill.green() * fill.alphaF() + base.green() * (1 - fill.alphaF())),
                    qRound(fill.blue() * fill.alphaF() + base.blue() * (1 - fill.alphaF())));
                QVERIFY(qAbs(pixel.red() - expected.red()) <= 2);
                QVERIFY(qAbs(pixel.green() - expected.green()) <= 2);
                QVERIFY(qAbs(pixel.blue() - expected.blue()) <= 2);
                const auto shadowPoint =
                    neutral->mapToScene(QPointF(neutral->width() / 2, neutral->height() + 1));
                const auto shadow =
                    image.pixelColor(qRound(shadowPoint.x() * dpr), qRound(shadowPoint.y() * dpr));
                QVERIFY2(shadow.red() < base.red(),
                         "The source's outer action shadow must actually render");
            }
            mouse(neutral);
            QTRY_COMPARE(color(neutral), alpha(tone, 0.10));
            const auto point =
                neutral->mapToScene(QPointF(neutral->width() / 2, neutral->height() / 2)).toPoint();
            QVERIFY(Test::prepareNativeInput(session.window));
            QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_VERIFY(neutral->property("down").toBool());
            QTRY_COMPARE(color(neutral), alpha(tone, 0.14));
            QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, point);
            Test::moveNativePointer(session.window, QPoint(700, 250));
            neutral->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(neutral->property("visualFocus").toBool());
            QTRY_COMPARE(color(neutral), alpha(tone, 0.10));
            mouse(accent);
            QTRY_COMPARE(color(accent), alpha(session.theme.accent(), 0.18));
            mouse(select);
            QTRY_COMPARE(color(select), alpha(tone, 0.10));
            const auto selectPoint =
                select->mapToScene(QPointF(select->width() / 2, select->height() / 2)).toPoint();
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, selectPoint);
            QPointer<QObject> popup = select->property("popup").value<QObject *>();
            QVERIFY(popup);
            QTRY_VERIFY(popup->property("visible").toBool());
            QTRY_COMPARE(color(select), alpha(session.theme.accent(), 0.12));
            QCOMPARE(select->property("foreground").value<QColor>(),
                     floating ? session.theme.floatingAccentText() : session.theme.accentText());
            QCOMPARE(
                popup->property("background").value<QQuickItem *>()->property("radius").toReal(),
                10.0);
            QMetaObject::invokeMethod(popup, "close");
            QTRY_VERIFY(popup.isNull());
            select->forceActiveFocus(Qt::MouseFocusReason);
        }
    QCOMPARE(warnings.count(), 0);
}
