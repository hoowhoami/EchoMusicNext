#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"
#include "EchoPixelSnap.h"
#include <cmath>

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

    // Ancestor moves and scale changes both flow through the chain watchers.
    hostA->setX(hostA->x() + 4.4);
    QTRY_COMPARE(attached->scenePosition(), probe->mapToScene(QPointF()));
    QCOMPARE(moved.size(), 1);
    hostA->setX(hostA->x());
    QCOMPARE(moved.size(), 1);
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
    for (auto *control : {icon, cover}) {
        auto *attachedControl = qobject_cast<EchoMusic::EchoPixelSnapAttached *>(
            qmlAttachedPropertiesObject<EchoMusic::EchoPixelSnap>(control));
        QVERIFY(attachedControl);
        QTRY_VERIFY(qAbs(attachedControl->densityX() - density) < 1e-6);
        auto *surface =
            findVisual(control, control == icon ? "iconAlignedSurface" : "coverAlignedSurface");
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
    for (auto *control : {icon, cover}) {
        auto *attached = qobject_cast<EchoMusic::EchoPixelSnapAttached *>(
            qmlAttachedPropertiesObject<EchoMusic::EchoPixelSnap>(control));
        QVERIFY(attached);
        QVERIFY(!attached->enabled());
        auto *surface =
            findVisual(control, control == icon ? "iconAlignedSurface" : "coverAlignedSurface");
        QVERIFY(surface);
        QTRY_COMPARE(surface->x(), 0.);
        QTRY_COMPARE(surface->y(), 0.);
        QTRY_COMPARE(surface->width(), control->width());
        QTRY_COMPARE(surface->height(), control->height());
    }
}
