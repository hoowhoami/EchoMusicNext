#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"
#include "EchoPixelSnap.h"
#include "EchoItemObserver.h"
#include "EchoDashedFrameSurface.h"
#include <cmath>

void ControlsUiTest::iconButtonHoverScalesStableRasterAndHitRegion()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls
Window {
    id: host
    width: 440; height: 220; visible: true; color: Theme.player
    property real uiScale: 1
    Item {
        x: 70.3; y: 70.6; scale: host.uiScale
        EchoIconButton {
            objectName: "hoverScaleButton"
            width: 38; height: 38; padding: 9
            text: "播放"; glyph: animatePlayPause ? "play" : "player-skip-forward"
            scaleFeedback: true; hoverBackground: false
        }
    }
})",
                      QUrl("qrc:/hover-scale.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *button = session.item("hoverScaleButton");
    QVERIFY(button);
    const auto capture = qEnvironmentVariable("ECHO_HOVER_SCALE_CAPTURES");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (qreal zoom : {1., 1.25}) {
            session.root->setProperty("uiScale", zoom);
            for (bool playPause : {false, true}) {
                Test::moveNativePointer(session.window, {2, 2});
                QTRY_VERIFY(!button->property("hovered").toBool());
                button->setProperty("animatePlayPause", playPause);
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.);
                QTRY_VERIFY(button->property("scaleAnimationAllowed").toBool());
                QQuickItem *image = nullptr;
                QTRY_VERIFY((image = findVisual(button, "iconImage")) &&
                            image->property("status").toInt() == 1);
                auto *surface = findVisual(button, "iconAlignedSurface");
                QVERIFY(surface);
                const auto raster = image->property("sourceSize");
                const auto origin = surface->position();
                const auto size = surface->size();
                const auto hitRect = button->mapRectToScene(button->boundingRect());
                QSignalSpy rasterChanges(image, SIGNAL(sourceSizeChanged()));
                QSignalSpy scaleChanges(button, SIGNAL(visualScaleChanged()));
                const auto idle = session.window->grabWindow();
                const QPoint center = hitRect.center().toPoint();
                Test::moveNativePointer(session.window, center);
                QTRY_VERIFY(button->property("hovered").toBool());
                QSet<int> samples;
                QElapsedTimer elapsed;
                elapsed.start();
                while (elapsed.elapsed() < 220) {
                    Test::moveNativePointer(session.window, center);
                    const qreal value = button->property("visualScale").toReal();
                    QVERIFY(value >= 1. && value <= 1.1);
                    samples.insert(qRound(value * 10000));
                    QCOMPARE(button->mapRectToScene(button->boundingRect()), hitRect);
                    QCOMPARE(surface->position(), origin);
                    QCOMPARE(surface->size(), size);
                    QCOMPARE(image->property("sourceSize"), raster);
                    const auto frame = session.window->grabWindow();
                    QVERIFY(!frame.isNull());
                    if (!capture.isEmpty())
                        QVERIFY(frame.save(capture + QString("/%1-%2-%3-%4.png")
                                                         .arg(int(mode))
                                                         .arg(zoom)
                                                         .arg(playPause)
                                                         .arg(elapsed.elapsed())));
                    QTest::qWait(8);
                }
                Test::moveNativePointer(session.window, center);
                QVERIFY2(
                    scaleChanges.size() >= 3,
                    qPrintable(QString("mode=%1 zoom=%2 playPause=%3 renderedScales=%4 changes=%5")
                                   .arg(int(mode))
                                   .arg(zoom)
                                   .arg(playPause)
                                   .arg(samples.size())
                                   .arg(scaleChanges.size())));
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.1);
                QCOMPARE(rasterChanges.size(), 0);
                QVERIFY(session.window->grabWindow() != idle);
                // A pointer just beyond the fixed hit area must not be held by
                // the enlarged glyph. Rapid reversal resumes the current scale.
                Test::moveNativePointer(session.window, {qCeil(hitRect.right()) + 1, center.y()});
                QTRY_VERIFY(!button->property("hovered").toBool());
                QTest::qWait(32);
                const qreal reverse = button->property("visualScale").toReal();
                Test::moveNativePointer(session.window, center);
                QVERIFY(qAbs(button->property("visualScale").toReal() - reverse) < .025);
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.1);
                QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, center);
                QTRY_COMPARE(button->property("visualScale").toReal(), .9);
                QCOMPARE(button->mapRectToScene(button->boundingRect()), hitRect);
                Test::moveNativePointer(session.window, {2, 2});
                QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, {2, 2});
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.);
                Test::moveNativePointer(session.window, center);
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.1);
                session.theme.setMotionEnabled(false);
                Test::moveNativePointer(session.window, {2, 2});
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.);
                Test::moveNativePointer(session.window, center);
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.1);
                session.theme.setMotionEnabled(true);
                Test::moveNativePointer(session.window, {2, 2});
                QTest::qWait(32);
                session.window->hide();
                QTRY_VERIFY(!button->property("scaleAnimationAllowed").toBool());
                QTRY_COMPARE(button->property("visualScale").toReal(), 1.);
                const qreal parked = button->property("visualScale").toReal();
                QTest::qWait(220);
                QCOMPARE(button->property("visualScale").toReal(), parked);
                session.window->show();
                QVERIFY(Test::prepareNativeInput(session.window));
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::pixelSnapTracksScenePositionAcrossReparentAndWindows()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Window {
    id: window
    visible: true; width: 420; height: 360
    Item {
        id: hostA
        objectName: "snapHostA"
        x: 10.3; y: 6.6
        Item {
            id: probe
            objectName: "snapProbe"
            x: 1.1; y: 2.2; width: 8; height: 8
        }
        EchoIcon {
            id: snapIcon
            objectName: "snapIcon"
            x: 3.5; y: 2.25; width: 23; height: 23
        }
    }
    Item {
        id: hostB
        objectName: "snapHostB"
        x: 84.2; y: 41.7
        EchoCover {
            id: snapCover
            objectName: "snapCover"
            x: 5.75; y: 1.5; width: 41; height: 41
        }
    }
    Window {
        id: secondWindow
        objectName: "snapSecondWindow"
        width: 300; height: 200
        Item {
            id: otherHost
            objectName: "snapOtherHost"
            x: 17.4; y: 9.9
        }
    }
})",
                      QUrl("qrc:/pixel-snap-tracking.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *probe = session.item("snapProbe");
    auto *icon = session.item("snapIcon");
    auto *cover = session.item("snapCover");
    auto *hostA = session.item("snapHostA");
    auto *hostB = session.item("snapHostB");
    QVERIFY(probe && icon && cover && hostA && hostB);
    auto *attached = qobject_cast<EchoMusic::EchoPixelSnapAttached *>(
        qmlAttachedPropertiesObject<EchoMusic::EchoPixelSnap>(probe));
    QVERIFY(attached);
    QSignalSpy moved(attached, &EchoMusic::EchoPixelSnapAttached::scenePositionChanged);
    QVERIFY(moved.isValid());
    QCOMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    QCOMPARE(attached->devicePixelRatio(), session.window->effectiveDevicePixelRatio());

    // A window DPR notification must invalidate scene consumers too, even
    // when no logical geometry changed (e.g. moving between monitors).
    const auto observer = observeItem(probe);
    QSignalSpy sceneInvalidated(observer.data(), &EchoItemObserver::sceneChanged);
    QEvent densityEvent(QEvent::DevicePixelRatioChange);
    QCoreApplication::sendEvent(session.window, &densityEvent);
    QCOMPARE(sceneInvalidated.size(), 1);

    // Ancestor moves and scale changes both flow through the chain watchers.
    // Values settle immediately, but the notification is queued (coalesced per
    // event-loop turn) so burst geometry churn cannot nest binding re-runs on
    // one stack; the spy therefore needs the loop to turn.
    hostA->setX(hostA->x() + 4.4);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    QTRY_COMPARE(moved.size(), 1);
    // A settled change must produce exactly one notification: after a second
    // move, drain the coalescer's turn and require the count to hold - with
    // the emit queued, a duplicate only lands past the QTRY above.
    hostA->setX(hostA->x() - 1.2);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    QTRY_COMPARE(moved.size(), 2);
    QTest::qWait(0);
    QCOMPARE(moved.size(), 2);
    hostA->setScale(1.25);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    hostA->setScale(1.);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));

    // At rest positions the window grid does not contain, the aligned
    // surface must land on whole device pixels. Run the checks under a
    // fractional ancestor scale as well: the compensation is applied in the
    // control's local coordinates, so its divisor must be DPR × scale rather
    // than the window DPR alone.
    const qreal dpr = session.window->effectiveDevicePixelRatio();
    const auto onDeviceGrid = [](qreal device) { return qAbs(device - std::round(device)) < 1e-6; };
    const qreal ancestorScale = 1.25;
    const qreal density = dpr * ancestorScale;
    hostA->setScale(ancestorScale);
    hostB->setScale(ancestorScale);
    auto *iconSurface = findVisual(icon, "iconAlignedSurface");
    QVERIFY(iconSurface);
    QVERIFY(!qmlAttachedPropertiesObject<EchoMusic::EchoPixelSnap>(icon, false));
    QCOMPARE(iconSurface->position(), QPointF());
    QCOMPARE(iconSurface->size(), icon->size());
    for (auto *control : {cover}) {
        auto *attachedControl = qobject_cast<EchoMusic::EchoPixelSnapAttached *>(
            qmlAttachedPropertiesObject<EchoMusic::EchoPixelSnap>(control));
        QVERIFY(attachedControl);
        QTRY_VERIFY(qAbs(attachedControl->densityX() - density) < 1e-6);
        auto *surface = findVisual(control, "coverAlignedSurface");
        QVERIFY(surface);
        QTRY_VERIFY2(onDeviceGrid(surface->mapToScene(QPointF(0, 0)).x() * dpr) &&
                         onDeviceGrid(surface->mapToScene(QPointF(0, 0)).y() * dpr),
                     qPrintable(QString("surface scene %1,%2 dpr %3 density %4")
                                    .arg(surface->mapToScene(QPointF(0, 0)).x())
                                    .arg(surface->mapToScene(QPointF(0, 0)).y())
                                    .arg(dpr)
                                    .arg(density)));
        QTRY_VERIFY2(
            onDeviceGrid(surface->width() * density),
            qPrintable(QString("surface width %1 density %2").arg(surface->width()).arg(density)));
        QTRY_VERIFY2(
            onDeviceGrid(surface->height() * density),
            qPrintable(
                QString("surface height %1 density %2").arg(surface->height()).arg(density)));
    }
    hostA->setScale(1);
    hostB->setScale(1);

    // Re-parenting rebuilds the watched chain: the new ancestors keep
    // notifying and the attached position follows the item without a window.
    probe->setParentItem(hostB);
    QCOMPARE(probe->parentItem(), hostB);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    hostB->setY(hostB->y() + 3.3);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));

    // Moving into another window (through the chain rebuilt above) re-syncs
    // the device pixel ratio, and moving back into the first window resumes
    // tracking there.
    auto *secondWindow = session.window->findChild<QQuickWindow *>("snapSecondWindow");
    QVERIFY(secondWindow);
    auto *otherHost = findVisual(secondWindow->contentItem(), "snapOtherHost");
    QVERIFY(otherHost);
    probe->setParentItem(otherHost);
    QVERIFY(probe->window() && probe->window() != session.window);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    QCOMPARE(attached->devicePixelRatio(), probe->window()->devicePixelRatio());
    otherHost->setX(otherHost->x() + 2.2);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    probe->setParentItem(hostA);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    QCOMPARE(attached->devicePixelRatio(), session.window->effectiveDevicePixelRatio());
    hostA->setY(hostA->y() + 1.7);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::thinSurfacesKeepDeviceGrid()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Theme
import EchoMusic.Controls
Window {
    visible: true; width: 416; height: 280; color: Theme.main
    Item {
        objectName: "thinHost"
        x: 20.3; y: 15.2
        EchoDivider {
            objectName: "thinHorizontal"
            x: 4.1; y: 8.2; width: 101.6
            thickness: 2; rounded: true; lineColor: Theme.text
        }
        EchoDivider {
            objectName: "thinVertical"
            x: 140.1; y: 8.2; height: 80.3
            orientation: Qt.Vertical; lineColor: Theme.text
        }
        EchoDashedFrame {
            objectName: "thinFrame"
            x: 4.1; y: 44.3; width: 101.6; height: 46.4
            radius: 8; strokeColor: Theme.text
        }
    }
})",
                      QUrl("qrc:/thin-surfaces.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *host = session.item("thinHost");
    auto *horizontal = session.item("thinHorizontal");
    auto *vertical = session.item("thinVertical");
    auto *frame = session.item("thinFrame");
    QVERIFY(host && horizontal && vertical && frame);
    auto *line = findVisual(horizontal, "dividerAlignedSurface");
    auto *edge = findVisual(vertical, "dividerAlignedSurface");
    auto *outline = findVisual(frame, "dashedFrameAlignedSurface");
    QVERIFY(line && edge && outline);
    auto *raster = qobject_cast<EchoDashedFrameSurface *>(outline);
    QVERIFY(raster);
    const auto capture = qEnvironmentVariable("ECHO_THIN_SURFACE_CAPTURES");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    const auto integer = [](qreal value) { return qAbs(value - std::round(value)) < 1e-6; };
    const qreal dpr = session.window->effectiveDevicePixelRatio();
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const qreal scale : {.75, 1., 1.25, 1.5}) {
            host->setScale(scale);
            for (const qreal phase : {0., .2, .6}) {
                host->setPosition({20.3 + phase, 15.2 + phase});
                const qreal density = dpr * scale;
                for (auto *surface : {line, edge, outline}) {
                    QTRY_VERIFY(integer(surface->mapToScene({}).x() * dpr));
                    QTRY_VERIFY(integer(surface->mapToScene({}).y() * dpr));
                    QTRY_VERIFY(integer(surface->width() * density));
                    QTRY_VERIFY(integer(surface->height() * density));
                }
                QCOMPARE(horizontal->height(), 2.);
                QCOMPARE(vertical->width(), 1.);
                QTRY_COMPARE(qRound(line->height() * density), qMax(1, qRound(2 * density)));
                QTRY_COMPARE(qRound(edge->width() * density), qMax(1, qRound(density)));
                const auto image = session.window->grabWindow();
                QVERIFY(!image.isNull());
                if (!capture.isEmpty())
                    QVERIFY(image.save(
                        capture + QString("/%1-%2-%3.png").arg(int(mode)).arg(scale).arg(phase)));
                // The software backend has its own Rectangle rasterization
                // rules. Verify its geometry/lifetime here; native device-row
                // coverage is asserted on the RHI rendering path below.
                if (!supportsFloatingEffects(session.window))
                    continue;
                // Assert actual pure-color device rows, not just QML geometry.
                const auto origin = line->mapToScene({}) * dpr;
                const int left = qRound(origin.x()), top = qRound(origin.y());
                const int width = qRound(line->width() * density);
                const int rows = qRound(line->height() * density);
                for (int y = top; y < top + rows; ++y)
                    QCOMPARE(image.pixelColor(left + width / 2, y), session.theme.text());
                QCOMPARE(image.pixelColor(left + width / 2, top - 1), session.theme.main());
                QCOMPARE(image.pixelColor(left + width / 2, top + rows), session.theme.main());
                const auto borderOrigin = outline->mapToScene({}) * dpr;
                const int borderTop = qRound(borderOrigin.y());
                const int borderLeft = qRound(borderOrigin.x());
                const int strokeRows = qMax(1, qRound(density));
                int solidColumns = 0;
                for (int x = borderLeft + qCeil(12 * density);
                     x < borderLeft + qFloor(85 * density); ++x) {
                    bool solid = true;
                    for (int y = borderTop; y < borderTop + strokeRows; ++y)
                        solid &= image.pixelColor(x, y) == session.theme.text();
                    if (solid) {
                        ++solidColumns;
                        QCOMPARE(image.pixelColor(x, borderTop - 1), session.theme.main());
                        QCOMPARE(image.pixelColor(x, borderTop + strokeRows), session.theme.main());
                    }
                }
                QVERIFY2(solidColumns >= 5,
                         qPrintable(QString("mode=%1 scale=%2 phase=%3 origin=%4,%5 logical=%6x%7 "
                                            "upload=%8x%9 color=%10")
                                        .arg(int(mode))
                                        .arg(scale)
                                        .arg(phase)
                                        .arg(borderOrigin.x())
                                        .arg(borderOrigin.y())
                                        .arg(outline->width())
                                        .arg(outline->height())
                                        .arg(raster->lastUploadSize().width())
                                        .arg(raster->lastUploadSize().height())
                                        .arg(image.pixelColor(borderLeft + 20, borderTop).name())));
                QCOMPARE(raster->lastUploadSize(), QSize(qRound(outline->width() * density),
                                                         qRound(outline->height() * density)));
            }
        }
    }
    const auto uploads = raster->textureUploadCount();
    const auto origin = outline->mapToScene({});
    host->setX(host->x() + 4 / dpr);
    QTRY_VERIFY(qAbs(outline->mapToScene({}).x() - origin.x() - 4 / dpr) < 1e-6);
    QVERIFY(!session.window->grabWindow().isNull());
    QCOMPARE(raster->textureUploadCount(), uploads);
    frame->setProperty("fillColor", QColor("#4285ab"));
    QTRY_COMPARE(session.window->grabWindow().pixelColor(
                     (frame->mapToScene(frame->boundingRect().center()) * dpr).toPoint()),
                 QColor("#4285ab"));
    QPointer<QQuickItem> retired = outline;
    frame->setVisible(false);
    QTRY_VERIFY(retired.isNull());
    frame->setVisible(true);
    QTRY_VERIFY(findVisual(frame, "dashedFrameAlignedSurface"));
    retired = findVisual(frame, "dashedFrameAlignedSurface");
    session.window->hide();
    QTRY_VERIFY(retired.isNull());
    session.window->show();
    QVERIFY(Test::prepareNativeInput(session.window));
    QTRY_VERIFY(findVisual(frame, "dashedFrameAlignedSurface"));
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::pixelSnapDisabledUsesLogicalGeometry()
{
    if (qEnvironmentVariable("ECHO_PIXEL_SNAP") != QLatin1String("0"))
        QSKIP("Requires ECHO_PIXEL_SNAP=0");
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Window {
    visible: true; width: 420; height: 360
    Item {
        id: host
        x: 10.3; y: 6.6
        scale: 1.25
        EchoIcon {
            id: icon
            objectName: "snapDisabledIcon"
            x: 3.5; y: 2.25; width: 23; height: 23
        }
        EchoCover {
            id: cover
            objectName: "snapDisabledCover"
            x: 5.75; y: 31.5; width: 41; height: 41
        }
        EchoDivider {
            objectName: "snapDisabledLine"
            x: 1.2; y: 78.4; width: 101.6; thickness: 2
        }
        EchoDashedFrame {
            objectName: "snapDisabledFrame"
            x: 1.2; y: 90.4; width: 101.6; height: 46.4
        }
    }
})",
                      QUrl("qrc:/pixel-snap-disabled.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window);
    auto *icon = session.item("snapDisabledIcon");
    auto *cover = session.item("snapDisabledCover");
    QVERIFY(icon && cover);
    QVERIFY(!qmlAttachedPropertiesObject<EchoMusic::EchoPixelSnap>(icon, false));
    auto *iconSurface = findVisual(icon, "iconAlignedSurface");
    QVERIFY(iconSurface);
    QCOMPARE(iconSurface->position(), QPointF());
    QCOMPARE(iconSurface->size(), icon->size());
    for (auto *control : {cover}) {
        auto *attached = qobject_cast<EchoMusic::EchoPixelSnapAttached *>(
            qmlAttachedPropertiesObject<EchoMusic::EchoPixelSnap>(control));
        QVERIFY(attached);
        QVERIFY(!attached->enabled());
        auto *surface = findVisual(control, "coverAlignedSurface");
        QVERIFY(surface);
        QTRY_COMPARE(surface->x(), 0.);
        QTRY_COMPARE(surface->y(), 0.);
        QTRY_COMPARE(surface->width(), control->width());
        QTRY_COMPARE(surface->height(), control->height());
    }
    for (const auto &name : {"snapDisabledLine", "snapDisabledFrame"}) {
        auto *control = session.item(name);
        QVERIFY(control);
        auto *surface = findVisual(control, QString::fromLatin1(name) == "snapDisabledLine"
                                                ? "dividerAlignedSurface"
                                                : "dashedFrameAlignedSurface");
        QVERIFY(surface);
        QCOMPARE(surface->position(), QPointF());
        QCOMPARE(surface->size(), control->size());
    }
}
