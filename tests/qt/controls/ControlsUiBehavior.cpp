#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"

void ControlsUiTest::listLocationPreservesVisibleRowsAndChecksOcclusion()
{
    ControlsSession session;
    QVERIFY(session.create());
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
    width: 400; height: 340
    property bool pageMode: true
    property int positioningMode: ListView.Center
    function locate(index: int): void {
        EchoListPositioning.positionIfNeeded(list, index, positioningMode, pageMode ? page : null);
    }
    function rowAvailable(index: int): bool { return list.itemAtIndex(index) !== null; }
    EchoPageScrollContainer {
        id: page
        anchors.fill: parent
        scrollView: list
        stickyHeaders: [header]
        hideBackToTop: true
        EchoPageStickyHeader { id: header; container: page; height: 80 }
        ListView {
            id: list
            objectName: "locationList"
            y: 20; width: 400; height: 300
            topMargin: 200
            model: 1000
            reuseItems: true
            cacheBuffer: 600
            clip: true
            delegate: Item { required property int index; width: 400; height: 60 }
        }
    }
})",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    host->setParentItem(session.window->contentItem());
    auto *list = findVisual(host, "locationList");
    QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), 1000);
    QVERIFY(QMetaObject::invokeMethod(list, "forceLayout"));
    for (qreal scale : {1., 1.25}) {
        host->setScale(scale);
        const auto locate = [&](int index) {
            return QMetaObject::invokeMethod(host, "locate", Q_ARG(int, index));
        };
        const auto setPosition = [&](qreal y) {
            list->setProperty("contentY", y);
            return QMetaObject::invokeMethod(list, "forceLayout");
        };
        const auto rowAvailable = [&](int index) {
            bool available = false;
            return QMetaObject::invokeMethod(host, "rowAvailable", Q_RETURN_ARG(bool, available),
                                             Q_ARG(int, index)) &&
                   available;
        };
        host->setProperty("pageMode", true);
        QVERIFY(setPosition(600));
        QTRY_VERIFY(rowAvailable(11) && rowAvailable(14));
        // Visible boundaries are y=60..300 in the list's coordinates.
        QSignalSpy changes(list, SIGNAL(contentYChanged()));
        for (int index : {11, 12, 14, -1, 1000}) {
            QVERIFY(locate(index));
            QCOMPARE(list->property("contentY").toReal(), 600.);
        }
        QCOMPARE(changes.count(), 0);
        // These rows are cached but hidden by the sticky header or below
        // the viewport. Partially clipped rows must still be located.
        for (const auto &[index, y] :
             {std::pair{10, 600.}, {15, 600.}, {11, 601.}, {14, 599.}, {900, 600.}}) {
            QVERIFY(setPosition(y));
            QVERIFY(locate(index));
            QVERIFY(list->property("contentY").toReal() != y);
        }
        // Queue and Mini have no page header. Keep their original Center
        // and Contain modes, including rows exactly on either boundary.
        host->setProperty("pageMode", false);
        for (int mode : {1, 4}) { // ListView.Center / ListView.Contain
            host->setProperty("positioningMode", mode);
            QVERIFY(setPosition(600));
            // A distant jump refills virtual delegates on the next polish.
            // Wait for painted rows before asserting that they are visible.
            QTRY_VERIFY(rowAvailable(10) && rowAvailable(14));
            changes.clear();
            for (int index : {10, 12, 14})
                QVERIFY(locate(index));
            QCOMPARE(changes.count(), 0);
            QVERIFY(locate(900));
            QVERIFY(list->property("contentY").toReal() > 50000);
        }
        host->setProperty("positioningMode", 1);
    }
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::iconRasterTracksZoomAndReleasesWhenHidden()
{
    ControlsSession session;
    QVERIFY(session.create());
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
 width: 100; height: 100
 property real zoom: 1
 scale: zoom; transformOrigin: Item.TopLeft
 EchoIcon { objectName: "zoomIcon"; x: 20; y: 20; width: 20; height: 20; name: "settings" }
})",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    host->setParentItem(session.window->contentItem());
    auto *icon = findVisual(host, "zoomIcon");
    auto *image = findVisual(icon, "iconImage");
    QVERIFY(icon && image);
    for (const qreal zoom : {1., 1.25, 1.5, 2.}) {
        host->setProperty("zoom", zoom);
        QTRY_COMPARE(image->property("status").toInt(), 1);
        const auto required = qCeil(20 * zoom * session.window->effectiveDevicePixelRatio());
        QTRY_VERIFY(image->property("sourceSize").toSize().width() >= required);
        if (supportsFloatingEffects(session.window)) {
            const auto actual = renderedTextureSize(session.window, image);
            QVERIFY2(actual.width() >= required, qPrintable(QString::number(actual.width())));
        }
    }
    host->setVisible(false);
    QTRY_VERIFY(image->property("source").toUrl().isEmpty());
    host->setProperty("zoom", 1.);
    host->setVisible(true);
    QTRY_COMPARE(image->property("status").toInt(), 1);
    const auto reset = image->property("sourceSize").toSize().width();
    QVERIFY(reset >= 20 * session.window->effectiveDevicePixelRatio());
    QVERIFY(reset < 40 * session.window->effectiveDevicePixelRatio());
}

void ControlsUiTest::coverKeepsNativeResolutionUnderAncestorZoom()
{
    ControlsSession session;
    QVERIFY(session.create());
    if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Artwork sampling needs native RHI");
    QImage pattern(1024, 1024, QImage::Format_RGB32);
    for (int y = 0; y < pattern.height(); ++y)
        for (int x = 0; x < pattern.width(); ++x)
            pattern.setPixelColor(x, y,
                                  QColor((x / 8 % 2) * 255, (y / 8 % 2) * 255, (x / 12 % 2) * 255));
    const auto path = session.directory.filePath("cover-resolution.png");
    QVERIFY(pattern.save(path));
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
 width: 520; height: 200
 property real zoom: 1
 scale: zoom; transformOrigin: Item.TopLeft
 EchoCover { objectName: "zoomCover"; x: 32; y: 32; width: 100; height: 100 }
 Image { objectName: "directCover"; x: 288; y: 32; width: 100; height: 100; fillMode: Image.PreserveAspectCrop }
})",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    host->setParentItem(session.window->contentItem());
    auto *cover = findVisual(host, "zoomCover");
    auto *direct = findVisual(host, "directCover");
    QVERIFY(cover && direct);
    cover->setProperty("source", QUrl::fromLocalFile(path));
    direct->setProperty("source", QUrl::fromLocalFile(path));
    QTRY_COMPARE(cover->property("status").toInt(), 1);
    auto *image = findVisual(cover, "coverImage");
    QVERIFY(image);
    for (const qreal zoom : {1., 1.25, 1.5}) {
        host->setProperty("zoom", zoom);
        const auto needed = qCeil(100 * zoom * session.window->devicePixelRatio());
        QTRY_VERIFY(image->property("sourceSize").toSize().width() >= needed);
        direct->setProperty("sourceSize", image->property("sourceSize"));
        QTRY_COMPARE(direct->property("status").toInt(), 1);
        auto *capture = findVisual(cover, "coverCapture");
        auto *effect = findVisual(cover, "coverMaskEffect");
        QVERIFY(capture && effect);
        const auto texture = renderedTextureSize(session.window, capture);
        QVERIFY(texture.width() >= needed && texture.height() >= needed);
        QCOMPARE(effect->property("hasProxySource").toBool(), false);
        QTest::qWait(60);
        const auto frame = session.window->grabWindow();
        const auto crop = [&](QQuickItem *item) {
            const auto r = item->mapRectToScene({10, 10, 80, 80});
            return frame.copy(QRect(qRound(r.x() * frame.devicePixelRatio()),
                                    qRound(r.y() * frame.devicePixelRatio()),
                                    qRound(r.width() * frame.devicePixelRatio()),
                                    qRound(r.height() * frame.devicePixelRatio())));
        };
        const auto masked = crop(cover), reference = crop(direct);
        QCOMPARE(masked.size(), reference.size());
        double error = 0;
        for (int y = 0; y < masked.height(); ++y)
            for (int x = 0; x < masked.width(); ++x)
                error += qAbs(qGray(masked.pixel(x, y)) - qGray(reference.pixel(x, y)));
        error /= masked.width() * masked.height();
        qInfo() << "Artwork DPR" << frame.devicePixelRatio() << "zoom" << zoom << "texture"
                << texture << "mean difference" << error;
        QVERIFY2(error < 2., qPrintable(QString::number(error)));
        const auto artifacts = qEnvironmentVariable("ECHO_COVER_CAPTURE_DIR");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            QVERIFY(masked.save(artifacts + QString("/masked-%1.png").arg(zoom)));
            QVERIFY(reference.save(artifacts + QString("/direct-%1.png").arg(zoom)));
        }
    }
}

void ControlsUiTest::coverCropAndRoundedPixels()
{
    ControlsSession session;
    QVERIFY(session.create());
    if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Cover pixels require native RHI");
    QImage art(240, 120, QImage::Format_RGB32);
    art.fill(Qt::green);
    for (int y = 0; y < 120; ++y)
        for (int x = 0; x < 240; ++x)
            if (x < 60 || x >= 180)
                art.setPixelColor(x, y, x < 60 ? Qt::red : Qt::blue);
    const auto path = session.directory.filePath("wide-cover.png");
    QVERIFY(art.save(path));
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                width: 720; height: 290
                property real zoom: 1
                Rectangle { anchors.fill: parent; color: "black" }
                EchoCover {
                    objectName: "coverProbe"
                    x: 400; y: 60; width: 100; height: 100
                    cornerRadius: circular ? width / 2 : 20; placeholderColor: "transparent"
                    scale: parent.zoom; transformOrigin: Item.TopLeft
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    host->setParentItem(session.window->contentItem());
    auto *cover = findVisual(host, "coverProbe");
    QVERIFY(cover);
    cover->setProperty("source", QUrl::fromLocalFile(path));
    QTRY_COMPARE(cover->property("status").toInt(), int(1));
    for (const qreal scale : {1., 1.25}) {
        host->setProperty("zoom", scale);
        QTest::qWait(80);
        const auto frame = session.window->grabWindow();
        const qreal ratio = frame.devicePixelRatio();
        const auto pixel = [&](qreal x, qreal y) {
            const auto p = cover->mapToScene({x, y}) * ratio;
            return frame.pixelColor(qRound(p.x()), qRound(p.y()));
        };
        QCOMPARE(pixel(1, 1), QColor(Qt::black));
        QVERIFY2(pixel(5, 50).green() > 240 && pixel(5, 50).red() < 10,
                 "MultiEffect must preserve the image's aspect crop, not stretch its raw texture");
        int partial = 0;
        const auto origin = cover->mapToScene({0, 0}) * ratio;
        for (int y = 0; y < qCeil(20 * scale * ratio); ++y)
            for (int x = 0; x < qCeil(20 * scale * ratio); ++x) {
                const auto p = frame.pixelColor(qRound(origin.x()) + x, qRound(origin.y()) + y);
                partial += p.green() > 20 && p.green() < 235;
            }
        qInfo() << "Cover partial edge pixels" << scale << partial;
        QVERIFY2(partial > 12, "Cover corners must retain antialias coverage");
        const auto captures = qEnvironmentVariable("ECHO_COVER_CAPTURE_DIR");
        if (!captures.isEmpty())
            QVERIFY(frame.save(captures + QString("/cover-%1.png").arg(scale)));
    }
    QPointer<QQuickItem> mask = findVisual(cover, "coverRoundedMask");
    QVERIFY(mask);
    cover->setProperty("circular", true);
    QTest::qWait(80);
    auto frame = session.window->grabWindow();
    const auto origin = cover->mapToScene({5, 5}) * frame.devicePixelRatio();
    QCOMPARE(frame.pixelColor(qRound(origin.x()), qRound(origin.y())), QColor(Qt::black));
    cover->setProperty("resourceActive", false);
    QTRY_VERIFY(mask.isNull());
    QTRY_COMPARE(cover->property("status").toInt(), int(0));
    cover->setProperty("resourceActive", true);
    QTRY_COMPARE(cover->property("status").toInt(), int(1));
    QTRY_VERIFY(findVisual(cover, "coverRoundedMask"));
}

void ControlsUiTest::clearActionIsOneUserEditButProgrammaticClearIsNot()
{
    ControlsSession session;
    QVERIFY(session.create());
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                width: 320; height: 100
                EchoTextField {
                    objectName: "clearInputUnderTest"
                    x: 20; y: 20; width: 260
                    clearable: true
                    property string draft: ""
                    onTextEdited: draft = text
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    auto *field = findVisual(host, "clearInputUnderTest");
    QVERIFY(field);
    auto *clear = findVisual(field, "clearTextAction");
    QVERIFY(clear);
    QSignalSpy edited(field, SIGNAL(textEdited()));
    QSignalSpy cleared(field, SIGNAL(cleared()));
    for (const int echoMode : {0, 2}) {
        field->setProperty("echoMode", echoMode);
        field->setProperty("text", QStringLiteral("中文🙂Alpha"));
        QCOMPARE(edited.size(), 0);
        QCOMPARE(cleared.size(), 0);
        field->setProperty("readOnly", true);
        QVERIFY(!clear->isVisible());
        field->setProperty("readOnly", false);
        field->setEnabled(false);
        QVERIFY(!clear->isVisible());
        field->setEnabled(true);
        QTRY_VERIFY(clear->isVisible());
        QVERIFY(Test::prepareNativeInput(session.window));
        const auto point = clear->mapToScene({clear->width() / 2, clear->height() / 2}).toPoint();
        Test::moveNativePointer(session.window, point);
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(field->property("text").toString(), QString());
        QCOMPARE(field->property("draft").toString(), QString());
        QCOMPARE(edited.size(), 1);
        QCOMPARE(cleared.size(), 1);
        QVERIFY(field->hasActiveFocus());
        QVERIFY(!clear->isVisible());
        for (const auto key : QByteArray("AB"))
            QTest::keyClick(session.window, key);
        QTest::keyClick(session.window, Qt::Key_Backspace);
        QCOMPARE(field->property("text").toString(), QString("A"));
        QCOMPARE(field->property("draft").toString(), QString("A"));
        QCOMPARE(edited.size(), 4);
        edited.clear();
        cleared.clear();
        // Code-driven resets retain Qt's native, non-user-edit semantics.
        QVERIFY(QMetaObject::invokeMethod(field, "clear"));
        QCOMPARE(field->property("text").toString(), QString());
        QCOMPARE(field->property("draft").toString(), QString("A"));
        QCOMPARE(edited.size(), 0);
        QCOMPARE(cleared.size(), 0);
    }
}

void ControlsUiTest::boundedListAutoHeightSurvivesReopenAndVariableRows()
{
    ControlsSession session;
    QVERIFY(session.create());
    session.window->resize(900, 800);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            import EchoMusic.Theme
            Item {
                id: host
                property var rows: [42, 87, 53]
                property string rowText: "中文 Latin fallback"
                property real footerHeight: 0
                property alias popup: dialog
                EchoDialog {
                    id: dialog
                    heading: "变高列表"
                    preferredWidth: 420
                    body: EchoBoundedListView {
                        id: list
                        objectName: "boundedTestList"
                        maximumViewportHeight: 280
                        model: dialog.visible ? host.rows : null
                        clip: true
                        reuseItems: true
                        cacheBuffer: 0
                        spacing: 7
                        topMargin: 2
                        bottomMargin: 2
                        footer: Rectangle {
                            width: list.width
                            height: host.footerHeight
                            color: Theme.floating.mutedSurface
                        }
                        delegate: Rectangle {
                            required property var modelData
                            objectName: "boundedRow"
                            width: list.width
                            height: Math.max(modelData, label.implicitHeight + 16)
                            color: Theme.floating.mutedSurface
                            Text {
                                id: label
                                x: 8; y: 8; width: parent.width - 16
                                text: host.rowText
                                font: Theme.font(12, Theme.regularWeight)
                                color: Theme.floating.text
                                wrapMode: Text.Wrap
                            }
                        }
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
    auto *popup = host->property("popup").value<QObject *>();
    QQuickItem *view = nullptr;
    QVERIFY(popup);
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1., 1.25}) {
            popup->setProperty("uiScale", scale);
            qreal height = 0;
            for (int cycle = 0; cycle < 4; ++cycle) {
                QVERIFY(QMetaObject::invokeMethod(popup, "open"));
                QTRY_VERIFY(popup->property("opened").toBool());
                QTRY_VERIFY((view = findVisual(session.window->contentItem(), "boundedTestList")));
                QTRY_COMPARE(view->property("count").toInt(), 3);
                QTRY_COMPARE(view->implicitHeight(), 200.);
                QTRY_COMPARE(view->height(), 200.);
                if (!cycle)
                    height = popup->property("height").toReal();
                QCOMPARE(popup->property("height").toReal(), height);
                QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                QTRY_VERIFY(!popup->property("visible").toBool());
            }
        }
    }
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    // Same row count with different geometry must invalidate the previous extent.
    host->setProperty("rows", QVariantList{42, 120, 60});
    QTRY_COMPARE(view->implicitHeight(), 240.);
    host->setProperty("rows", QVariantList{42, 87, 53, 50});
    QTRY_COMPARE(view->implicitHeight(), 257.);
    host->setProperty("rows", QVariantList{});
    QTRY_COMPARE(view->implicitHeight(), 0.);
    host->setProperty("footerHeight", 44.);
    QTRY_COMPARE(view->implicitHeight(), 44.);
    host->setProperty("rows", QVariantList{42, 87, 53});
    QTRY_COMPARE(view->implicitHeight(), 244.);
    host->setProperty("footerHeight", 100.);
    QTRY_COMPARE(view->implicitHeight(), 280.);
    host->setProperty("footerHeight", 0.);
    QTRY_COMPARE(view->implicitHeight(), 200.);
    // A real Text reflows at narrow width and later returns to its original size.
    host->setProperty("rowText", QStringLiteral("中文 Latin 字体回退 ").repeated(25));
    popup->setProperty("preferredWidth", 240.);
    QTRY_COMPARE(view->implicitHeight(), 280.);
    popup->setProperty("preferredWidth", 420.);
    host->setProperty("rowText", QStringLiteral("中文 Latin fallback"));
    QTRY_COMPARE(view->implicitHeight(), 200.);
    QVariantList many;
    for (int i = 0; i < 1000; ++i)
        many.push_back(35 + (i % 5) * 14);
    host->setProperty("rows", many);
    QTRY_COMPARE(view->property("count").toInt(), 1000);
    QTRY_COMPARE(view->implicitHeight(), 280.);
    auto mounted = [&] {
        int count = 0;
        const auto items = view->property("contentItem").value<QQuickItem *>()->childItems();
        for (auto *item : items)
            count += item->objectName() == "boundedRow";
        return count;
    };
    QVERIFY(mounted() > 0 && mounted() < 30);
    view->setProperty("contentY", view->property("contentHeight").toReal() - view->height());
    QTest::qWait(100);
    QCOMPARE(view->implicitHeight(), 280.);
    QVERIFY(mounted() < 30);
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    host->setProperty("rows", QVariantList{42, 87, 53});
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    QTRY_COMPARE(view->height(), 200.);
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::progressClimaxTicksAndWaitPreview()
{
    ControlsSession session;
    QVERIFY(session.create("ProgressHarness.qml"));
    Test::moveNativePointer(session.window, {700, 140});
    auto *slider = session.item("previewProgress");
    QVERIFY(slider);
    QStandardItemModel marks;
    marks.setItemRoleNames({{Qt::UserRole + 1, "start"}, {Qt::UserRole + 2, "end"}});
    auto *row = new QStandardItem;
    row->setData(.25, Qt::UserRole + 1);
    row->setData(.75, Qt::UserRole + 2);
    marks.appendRow(row); // Rendering model, independent of upstream fixtures.
    session.root->setProperty("markerModel", QVariant::fromValue<QObject *>(&marks));
    session.root->setProperty("waiting", false);
    const auto capture = qEnvironmentVariable("ECHO_PROGRESS_PREVIEW_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1., 1.25}) {
            session.root->setProperty("uiScale", scale);
            QQuickItem *start = nullptr, *end = nullptr;
            QTRY_VERIFY((start = findVisual(slider, "progressClimaxStart-0")));
            QTRY_VERIFY((end = findVisual(slider, "progressClimaxEnd-0")));
            QCOMPARE(start->size(), QSizeF(2, 6));
            QCOMPARE(end->size(), QSizeF(2, 6));
            const auto track = slider->property("background").value<QQuickItem *>();
            QVERIFY(track);
            QVERIFY(qAbs(start->mapToItem(track, {1, 3}).x() - .25 * track->width()) < .01);
            QVERIFY(qAbs(end->mapToItem(track, {1, 3}).x() - .75 * track->width()) < .01);
            const auto image = session.window->grabWindow();
            const auto ratio = image.width() / qreal(session.window->width());
            const auto inside = end->mapToScene({1, .5}) * ratio;
            const auto outside = end->mapToScene({8, .5}) * ratio;
            QVERIFY(image.pixelColor(inside.toPoint()) != image.pixelColor(outside.toPoint()));
            if (!capture.isEmpty())
                QVERIFY(
                    image.save(capture + QString("/ticks-%1-%2.png").arg(int(theme)).arg(scale)));
        }
    }
    row->setData(.25, Qt::UserRole + 2);
    QTRY_VERIFY(!findVisual(slider, "progressClimaxEnd-0")->isVisible());
    marks.clear();
    QTRY_VERIFY(!findVisual(slider, "progressClimaxStart-0"));
    session.root->setProperty("markerModel", QVariant::fromValue<QObject *>(nullptr));
    session.theme.setMode(ThemeStore::Light);
    session.root->setProperty("uiScale", 1.);
    session.root->setProperty("waiting", true);
    QTRY_VERIFY(findVisual(slider, "progressBusyOverlay"));
    if (!capture.isEmpty()) {
        // Export actual native frames. The preview is a test-only waiting
        // state, not evidence of a real network underrun.
        QElapsedTimer clock;
        clock.start();
        for (int frame = 0; frame < 120; ++frame) {
            if (frame == 90)
                session.root->setProperty("waiting", false);
            const auto wait = frame * 33 - clock.elapsed();
            if (wait > 0)
                QTest::qWait(int(wait));
            QVERIFY(session.window->grabWindow().save(
                capture + QString("/frame-%1.png").arg(frame, 3, 10, QChar('0'))));
        }
        QVERIFY(!findVisual(slider, "progressBusyOverlay"));
    }
    session.root->setProperty("markerModel", QVariant::fromValue<QObject *>(nullptr));
}

void ControlsUiTest::playPauseTransitionKeepsEndpointsAndInterrupts()
{
    ControlsSession session;
    QVERIFY(session.create());
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            EchoIconButton {
                width: 38; height: 38; padding: 9
                animatePlayPause: true; glyph: "play"; text: "播放"
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *button = qobject_cast<QQuickItem *>(object.data());
    button->setParentItem(session.window->contentItem());
    button->setPosition({610, 170});
    auto *icon = findVisual(button, "playPauseIcon");
    QVERIFY(icon);
    auto *play = findVisual(icon, "playPausePlayIcon");
    auto *pause = findVisual(icon, "playPausePauseIcon");
    QVERIFY(play && pause);
    QTRY_VERIFY(icon->property("ready").toBool());
    QCOMPARE(icon->property("phase").toReal(), 0.);
    const auto capture = qEnvironmentVariable("ECHO_PLAY_PAUSE_ANIMATION_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1., 1.25}) {
            button->setScale(scale);
            QTRY_VERIFY(icon->property("ready").toBool());
            const auto frame = [&] {
                const auto image = session.window->grabWindow();
                const auto ratio = image.width() / qreal(session.window->width());
                const auto rect = button->mapRectToScene(button->boundingRect());
                return image.copy(
                    QRectF(rect.topLeft() * ratio, rect.size() * ratio).toAlignedRect());
            };
            const auto idle = frame();
            QSignalSpy phases(icon, SIGNAL(phaseChanged()));
            button->setProperty("glyph", "pause");
            QTRY_VERIFY(icon->property("transitioning").toBool());
            QVERIFY(phases.wait(1000));
            const auto phase = icon->property("phase").toReal();
            QVERIFY(phase > 0 && phase < 1);
            const auto middle = frame();
            QVERIFY(middle != idle);
            QTRY_VERIFY(!icon->property("transitioning").toBool());
            QCOMPARE(icon->property("phase").toReal(), 1.);
            QCOMPARE(pause->opacity(), 1.);
            QCOMPARE(pause->scale(), 1.);
            QCOMPARE(play->opacity(), 0.);
            const auto active = frame();
            QVERIFY(active != idle && active != middle);
            if (!capture.isEmpty()) {
                const auto name =
                    QString("/%1-%2").arg(theme == ThemeStore::Dark ? "dark" : "light").arg(scale);
                QVERIFY(idle.save(capture + name + "-play.png"));
                QVERIFY(middle.save(capture + name + "-transition.png"));
                QVERIFY(active.save(capture + name + "-pause.png"));
            }
            button->setProperty("glyph", "play");
            QTest::qWait(40);
            const auto reverse = icon->property("phase").toReal();
            button->setProperty("glyph", "pause");
            QVERIFY(qAbs(icon->property("phase").toReal() - reverse) < .05);
            QTRY_COMPARE(icon->property("phase").toReal(), 1.);
            button->setProperty("glyph", "play");
            session.theme.setMotionEnabled(false);
            QCOMPARE(icon->property("phase").toReal(), 0.);
            QVERIFY(!icon->property("transitioning").toBool());
            button->setProperty("glyph", "pause");
            QCOMPARE(icon->property("phase").toReal(), 1.);
            session.theme.setMotionEnabled(true);
            button->setProperty("glyph", "play");
            button->setVisible(false);
            QCOMPARE(icon->property("phase").toReal(), 0.);
            QVERIFY(!icon->property("transitioning").toBool());
            button->setVisible(true);
            QTRY_VERIFY(icon->property("ready").toBool());
            button->setProperty("glyph", "pause");
            session.window->showMinimized();
            QTRY_VERIFY(!icon->property("transitioning").toBool());
            QCOMPARE(icon->property("phase").toReal(), 1.);
            session.window->showNormal();
            QVERIFY(Test::prepareNativeInput(session.window));
            button->setProperty("glyph", "play");
            QTRY_COMPARE(icon->property("phase").toReal(), 0.);
        }
    }
    for (const bool playingBeforeUncover : {false, true}) {
        button->setProperty("iconCovered", true);
        QVERIFY(!icon->isVisible());
        button->setProperty("glyph", "play");
        QCOMPARE(icon->property("phase").toReal(), 0.);
        if (playingBeforeUncover)
            button->setProperty("glyph", "pause");
        button->setProperty("iconCovered", false);
        if (!playingBeforeUncover)
            button->setProperty("glyph", "pause");
        QCOMPARE(icon->property("phase").toReal(), 1.);
        QVERIFY(!icon->property("transitioning").toBool());
        button->setProperty("glyph", "play");
        QVERIFY(icon->property("transitioning").toBool());
        QTRY_COMPARE(icon->property("phase").toReal(), 0.);
    }
    QScopedPointer<QObject> initiallyPlaying(
        component.createWithInitialProperties({{"glyph", "pause"}}));
    QVERIFY(initiallyPlaying);
    auto *initialIcon =
        findVisual(qobject_cast<QQuickItem *>(initiallyPlaying.data()), "playPauseIcon");
    QVERIFY(initialIcon);
    QCOMPARE(initialIcon->property("phase").toReal(), 1.);
    QVERIFY(!initialIcon->property("transitioning").toBool());
}

void ControlsUiTest::progressBusyRendersAndRetiresNativeAnimation()
{
    ControlsSession session;
    QVERIFY(session.create());
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            EchoSlider {
                width: 500; height: 24; padding: 0; from: 0; to: 100; value: 60
                progress: true; busy: true
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *slider = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(slider);
    slider->setParentItem(session.window->contentItem());
    slider->setX(20);
    slider->setY(240);
    const auto capture = qEnvironmentVariable("ECHO_PROGRESS_BUSY_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1., 1.25}) {
            slider->setScale(scale);
            QTRY_VERIFY(findVisual(slider, "progressBusyOverlay"));
            auto *overlay = findVisual(slider, "progressBusyOverlay");
            auto *wave = findVisual(slider, "progressBusyWave");
            QVERIFY(overlay && wave);
            QCOMPARE(overlay->width(), 300.);
            QTRY_VERIFY(overlay->property("animating").toBool());
            const auto frame = [&] {
                const auto image = session.window->grabWindow();
                const auto dpr = image.width() / qreal(session.window->width());
                auto rect = overlay->mapRectToScene(overlay->boundingRect());
                return image.copy(QRectF(rect.topLeft() * dpr, rect.size() * dpr).toAlignedRect());
            };
            QImage first, second;
            bool changed = false;
            for (int attempt = 0; attempt < 6 && !changed; ++attempt) {
                first = frame();
                QTest::qWait(130);
                second = frame();
                changed = first != second;
            }
            QVERIFY2(changed, "Native rendered progress pixels must change over time");
            if (!capture.isEmpty()) {
                const auto name =
                    QString("/%1-%2").arg(theme == ThemeStore::Dark ? "dark" : "light").arg(scale);
                QVERIFY(first.save(capture + name + "-sweep-a.png"));
                QVERIFY(second.save(capture + name + "-sweep-b.png"));
            }
            session.theme.setMotionEnabled(false);
            QTRY_COMPARE(wave->width(), overlay->width());
            QCOMPARE(wave->x(), 0.);
            const auto initialOpacity = wave->opacity();
            QTest::qWait(350);
            QVERIFY(qAbs(wave->opacity() - initialOpacity) > .01);
            slider->setVisible(false);
            QTRY_VERIFY(!overlay->property("animating").toBool());
            const auto stoppedPhase = overlay->property("phase").toReal();
            const auto stoppedPulse = overlay->property("pulse").toReal();
            QTest::qWait(80);
            QCOMPARE(overlay->property("phase").toReal(), stoppedPhase);
            QCOMPARE(overlay->property("pulse").toReal(), stoppedPulse);
            slider->setVisible(true);
            session.window->showMinimized();
            QTRY_VERIFY(!overlay->property("animating").toBool());
            session.window->showNormal();
            QVERIFY(Test::prepareNativeInput(session.window));
            QTRY_VERIFY(overlay->property("animating").toBool());
            session.theme.setMotionEnabled(true);
            slider->setProperty("busy", false);
            QTRY_VERIFY(!findVisual(slider, "progressBusyOverlay"));
            // The range returns to its static accent and frees the animation.
            slider->setProperty("busy", true);
        }
    }
    slider->setProperty("progress", false);
    QTRY_VERIFY(!findVisual(slider, "progressBusyOverlay"));
}

void ControlsUiTest::sourcePauseKeepsComparableInkToPlay()
{
    ControlsSession session;
    QVERIFY(session.create());
    EchoIconImageProvider provider;
    const auto bounds = [](const QImage &image) {
        QRect ink;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) >= 128)
                    ink |= QRect(x, y, 1, 1);
        return ink;
    };
    for (const int pixels : {40, 50}) {
        const auto play = provider.requestImage("play/ff000000", nullptr, {pixels, pixels});
        const auto pause = provider.requestImage("pause/ff000000", nullptr, {pixels, pixels});
        const auto oldPause =
            provider.requestImage("player-pause/ff000000", nullptr, {pixels, pixels});
        QVERIFY(!play.isNull() && !pause.isNull() && !oldPause.isNull());
        const auto a = bounds(play), b = bounds(pause), old = bounds(oldPause);
        qInfo() << "Play/pause/queue ink at" << pixels << a << b << old;
        QVERIFY(b.height() >= a.height() * .85);
        QVERIFY(b.width() >= a.width() * .9);
        QVERIFY(b.height() > old.height() && b.width() > old.width());
        QVERIFY(qAbs(b.center().y() - a.center().y()) <= 1);
    }
}

void ControlsUiTest::disabledIconHoverIsOptInAndNeverActivates()
{
    ControlsSession session;
    QVERIFY(session.create());
    auto *button = session.item("iconButton");
    QVERIFY(button);
    auto *background = button->property("background").value<QQuickItem *>();
    auto *icon = button->property("contentItem").value<QQuickItem *>();
    QVERIFY(background && icon);
    const auto backgroundAlpha = [&] {
        auto *paint = background->property("item").value<QObject *>();
        return paint ? paint->property("color").value<QColor>().alpha() : 0;
    };
    QSignalSpy clicks(button, SIGNAL(clicked()));
    const auto capture = qEnvironmentVariable("ECHO_DISABLED_HOVER_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const double zoom : {1., 1.25}) {
            button->setScale(zoom);
            button->setEnabled(false);
            button->setProperty("hoverWhenDisabled", false);
            Test::moveNativePointer(session.window, button->mapToScene({20, 18}).toPoint());
            QTRY_VERIFY(button->property("hovered").toBool());
            QCOMPARE(backgroundAlpha(), 0);
            const auto before = session.window->grabWindow();
            button->setProperty("hoverWhenDisabled", true);
            QTRY_VERIFY(button->property("hoverHighlighted").toBool());
            QTRY_VERIFY(backgroundAlpha() > 0);
            const auto after = session.window->grabWindow();
            const auto point = button->mapToScene({3, 18}) * after.devicePixelRatio();
            QVERIFY(before.pixelColor(point.toPoint()) != after.pixelColor(point.toPoint()));
            QCOMPARE(icon->opacity(), button->property("disabledOpacity").toReal());
            QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                              button->mapToScene({20, 18}).toPoint());
            QCOMPARE(clicks.size(), 0);
            if (!capture.isEmpty())
                QVERIFY(
                    after.save(capture + QString("/disabled-%1-%2.png").arg(int(mode)).arg(zoom)));
            // A login/modal shield disables an ancestor, not just this action.
            button->parentItem()->setEnabled(false);
            QTRY_VERIFY(!button->property("hoverHighlighted").toBool());
            QCOMPARE(backgroundAlpha(), 0);
            button->parentItem()->setEnabled(true);
        }
    }
}

void ControlsUiTest::shadowsReleaseTheirNativeMaterials()
{
    ControlsSession session;
    QVERIFY(session.create());
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
    id: testSurface
    width: 120
    height: 120
    EchoSurfaceShadow {
        objectName: "memoryShadow"
        anchors.fill: parent
        surface: testSurface
        blur: 24
        radius: 10
    }
})",
                      QUrl("memory-shadow.qml"));
    for (const bool attachToWindow : {false, true}) {
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.data());
        QVERIFY(item);
        if (attachToWindow)
            item->setParentItem(session.window->contentItem());
        auto *shadow = findVisual(item, "memoryShadow");
        QVERIFY(shadow);
        QTest::qWait(150);
        QList<QPointer<QQuickItem>> materials;
        for (auto *child :
             shadow->findChildren<QQuickItem *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (child->inherits("QQuickShaderEffect")) {
                materials.append(child);
                QVERIFY(child->parent() == shadow);
            }
        }
        QVERIFY(materials.size() >= 2); // Native default and the shared custom material.
        root.reset();
        QTRY_VERIFY(std::all_of(materials.cbegin(), materials.cend(),
                                [](const auto &material) { return material.isNull(); }));
    }
    QVERIFY(warnings.isEmpty());
}

void ControlsUiTest::sourceControlRolesKeepOrdinaryActionsAndLargeFormsDistinct()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("ControlSizeHarness.qml")));
    const auto capture = qEnvironmentVariable("ECHO_CONTROL_SIZE_CAPTURE_DIR");
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1.0, 1.25}) {
            session.root->setProperty("surfaceScale", scale);
            // Shared size roles: Small, Compact, Standard and Large.
            for (int role = 0; role < 4; ++role) {
                session.root->setProperty("role", role);
                const int height = QList<int>{28, 32, 36, 56}[role];
                for (const auto &name : {"sizedAction", "sizedSoftAction", "sizedField",
                                         "sizedSelect", "sizedNumber", "sizedDate"}) {
                    auto *control = session.item(name);
                    QVERIFY(control);
                    QTRY_COMPARE(control->height(), qreal(height));
                    const auto font = control->property("font").value<QFont>();
                    if (QString(name).contains("Action")) {
                        QCOMPARE(font.pixelSize(), role == 3 ? 15 : 12);
                        QCOMPARE(int(font.weight()), role == 3 ? 900 : 600);
                    } else {
                        QCOMPARE(font.pixelSize(), role == 3 ? 15 : role == 2 ? 13 : 12);
                        QCOMPARE(int(font.weight()), 500);
                    }
                }
                auto *header = session.item("sizedDrawerHeader");
                auto *close = findVisual(header, "drawerCloseButton");
                QVERIFY(close);
                QCOMPARE(close->size(), QSizeF(32, 32));
                QTRY_VERIFY(qAbs(close->mapToItem(header, {32, 0}).x() - (header->width() - 24)) <
                            0.5);
                if (!capture.isEmpty()) {
                    QVERIFY(QDir().mkpath(capture));
                    session.window->update();
                    QTest::qWait(60);
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/roles-%1-%2-%3.png")
                                      .arg(mode == ThemeStore::Light ? "light" : "dark")
                                      .arg(scale)
                                      .arg(role)));
                }
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}
