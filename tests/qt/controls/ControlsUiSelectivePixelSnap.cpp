#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"
#include "EchoPixelSnap.h"
#include "EchoDashedFrameSurface.h"
#include <QtQuick/private/qquickitem_p.h>
#include <cmath>

namespace
{
bool onGrid(qreal value)
{
    return qAbs(value - std::round(value)) < .0002;
}
bool commitFrame(QQuickWindow *window)
{
    Test::FrameSpy frame(window);
    window->update();
    return !frame.isEmpty() || frame.wait(2000);
}
} // namespace

void ControlsUiTest::pixelSnapOnlyTouchesExplicitDrawingSurfaces()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    session.theme.setMotionEnabled(false);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Window {
    id: host
    width: 620; height: 400; visible: true; color: "white"
    property real phase: .13
    property bool layered: true
    property bool dynamic: true
    property int clicks: 0
    Item {
        x: 20 + host.phase; y: 15 + host.phase
        width: 450; height: 280; scale: 1.25; transformOrigin: Item.TopLeft
        Text {
            objectName: "naturalText"
            x: 11.3; y: 10.2; text: "自然文字 Native text"; color: "black"
            transform: Translate { x: .17; y: .19 }
        }
        EchoButton {
            objectName: "naturalButton"
            x: 11.2; y: 60.4; width: 100; text: "点击"
            onClicked: host.clicks++
        }
        Rectangle {
            objectName: "naturalLayer"
            x: 190.3; y: 15.7; width: 80; height: 30; color: "red"
            layer.enabled: host.layered
        }
        EchoDivider { objectName: "explicitDivider"; x: 11.2; y: 120.4; width: 220 }
        EchoIcon { objectName: "explicitIcon"; x: 11.2; y: 145.3; width: 23; height: 23; name: "music"; color: "black" }
        EchoCover { objectName: "explicitCover"; x: 60.2; y: 145.3; width: 37; height: 37 }
        Loader {
            objectName: "dynamicTextOwner"
            x: 11.7; y: 205.4; active: host.dynamic
            sourceComponent: Text { objectName: "naturalDynamicText"; text: "Loader文字"; color: "black" }
        }
    }
})",
                      QUrl("qrc:/selective-pixel-snap.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *text = session.item("naturalText");
    auto *button = session.item("naturalButton");
    auto *layer = session.item("naturalLayer");
    QVERIFY(text && button && layer);
    const auto dpr = session.window->effectiveDevicePixelRatio();
    for (int cycle = 0; cycle < 8; ++cycle) {
        session.root->setProperty("phase", .13 + cycle * .17);
        session.root->setProperty("layered", cycle % 2 == 0);
        session.root->setProperty("dynamic", cycle % 2 == 0);
        QVERIFY(commitFrame(session.window));
        QCOMPARE(QQuickItemPrivate::get(text)->transforms.size(), 1);
        QCOMPARE(QQuickItemPrivate::get(button)->transforms.size(), 0);
        QCOMPARE(QQuickItemPrivate::get(layer)->transforms.size(), 0);
        const QPointF expected(20 + .13 + cycle * .17 + (11.3 + .17) * 1.25,
                               15 + .13 + cycle * .17 + (10.2 + .19) * 1.25);
        QVERIFY(QLineF(text->mapToScene({}), expected).length() < .0002);
        if (auto *dynamic = session.item("naturalDynamicText"))
            QCOMPARE(QQuickItemPrivate::get(dynamic)->transforms.size(), 0);
        for (const auto &name : {"dividerAlignedSurface", "coverAlignedSurface"}) {
            auto *surface = session.item(QString::fromLatin1(name));
            QVERIFY(surface);
            const auto origin = surface->mapToScene({}) * dpr;
            QVERIFY(onGrid(origin.x()) && onGrid(origin.y()));
        }
        auto *icon = session.item("explicitIcon");
        auto *iconSurface = session.item("iconAlignedSurface");
        QVERIFY(icon && iconSurface);
        QVERIFY(!qmlAttachedPropertiesObject<EchoPixelSnap>(icon, false));
        QCOMPARE(iconSurface->position(), QPointF());
        QCOMPARE(iconSurface->size(), icon->size());
    }
    QVERIFY(Test::settleNativeItem(button));
    const auto point = button->mapToScene(button->boundingRect().center()).toPoint();
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(session.root->property("clicks").toInt(), 1);
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::formerCanvasDrawingsUseNativePixelsAndRelease()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Window {
    id: host
    width: 460; height: 220; visible: true; color: "white"
    property color tint: "red"
    property real phase: .13
    Item {
        x: 30 + host.phase; y: 30.27; scale: 1.25; transformOrigin: Item.TopLeft
        EchoPopupArrow {
            objectName: "explicitArrow"
            x: 10.3; y: 10.2; width: 73.3; height: 41.7
            fillColor: host.tint
        }
        EchoDashedFrame {
            objectName: "explicitFrame"
            x: 110.3; y: 10.2; width: 73.3; height: 41.7
            fillColor: host.tint; strokeColor: "black"; radius: 4
        }
    }
})",
                      QUrl("qrc:/native-canvas-replacements.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *arrow = session.item("explicitArrow");
    auto *frameRoot = session.item("explicitFrame");
    QVERIFY(arrow && frameRoot);
    const auto dpr = session.window->effectiveDevicePixelRatio();
    const auto captures = qEnvironmentVariable("ECHO_SELECTIVE_SNAP_CAPTURES");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    for (int cycle = 0; cycle < 4; ++cycle) {
        arrow->setProperty("resourceActive", true);
        frameRoot->setProperty("resourceActive", true);
        const bool above = cycle % 2 == 0;
        arrow->setProperty("above", above);
        session.root->setProperty("tint", above ? "red" : "green");
        const QColor expected(above ? "red" : "green");
        QPointer<QQuickItem> shape;
        QPointer<EchoDashedFrameSurface> border;
        QTRY_VERIFY((shape = session.item("popupArrowAlignedSurface")));
        QTRY_VERIFY((border = qobject_cast<EchoDashedFrameSurface *>(
                         session.item("dashedFrameAlignedSurface"))));
        QVERIFY(commitFrame(session.window));
        for (auto *surface : {shape.data(), static_cast<QQuickItem *>(border.data())}) {
            const auto bounds = surface->mapRectToScene(QRectF(QPointF(), surface->size()));
            const auto detail = QString("%1: %2,%3 %4x%5 at DPR %6")
                                    .arg(surface->objectName())
                                    .arg(bounds.x() * dpr)
                                    .arg(bounds.y() * dpr)
                                    .arg(bounds.width() * dpr)
                                    .arg(bounds.height() * dpr)
                                    .arg(dpr);
            QVERIFY2(onGrid(bounds.left() * dpr) && onGrid(bounds.top() * dpr), qPrintable(detail));
            QVERIFY2(onGrid(bounds.right() * dpr) && onGrid(bounds.bottom() * dpr),
                     qPrintable(detail));
        }
        const auto center = shape->mapToScene(shape->boundingRect().center());
        const auto top = shape->mapToScene({shape->width() / 4, shape->height() / 8});
        const auto bottom = shape->mapToScene({shape->width() / 4, shape->height() * 7 / 8});
        const auto fill = border->mapToScene(border->boundingRect().center());
        QImage pixels;
        QTRY_VERIFY(([&] {
            pixels = session.window->grabWindow();
            const auto ratio = pixels.width() / qreal(session.window->width());
            return !pixels.isNull() && pixels.pixelColor((center * ratio).toPoint()) == expected &&
                   pixels.pixelColor((fill * ratio).toPoint()) == expected &&
                   pixels.pixelColor((top * ratio).toPoint()) ==
                       (above ? expected : QColor("white")) &&
                   pixels.pixelColor((bottom * ratio).toPoint()) ==
                       (above ? QColor("white") : expected);
        })());
        const auto size =
            QSize(qRound(border->width() * 1.25 * dpr), qRound(border->height() * 1.25 * dpr));
        QCOMPARE(border->lastUploadSize(), size);
        if (!captures.isEmpty())
            QVERIFY(pixels.save(captures + QString("/native-drawing-%1.png").arg(cycle)));
        const auto uploads = border->textureUploadCount();
        session.root->setProperty("phase", .13 + (cycle + 1) / dpr);
        QVERIFY(commitFrame(session.window));
        QCOMPARE(border->lastUploadSize(), size);
        QCOMPARE(border->textureUploadCount(), uploads);
        arrow->setProperty("resourceActive", false);
        frameRoot->setProperty("resourceActive", false);
        QTRY_VERIFY(shape.isNull() && border.isNull());
    }
    QCOMPARE(warnings.size(), 0);
}
