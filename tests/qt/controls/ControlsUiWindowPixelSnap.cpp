#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"
#include "EchoWindowPixelSnap.h"
#include <QProcess>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif

namespace
{
bool aligned(qreal value)
{
    return qAbs(value - std::round(value)) < .0002;
}
QPointF textAnchor(QQuickItem *text)
{
    return EchoWindowPixelSnap::contentAnchor(text);
}
qint64 nativeFootprint()
{
#ifdef Q_OS_MACOS
    task_vm_info_data_t info{};
    mach_msg_type_number_t size = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &size) ==
        KERN_SUCCESS)
        return qint64(info.phys_footprint);
#endif
    return -1;
}
} // namespace

void ControlsUiTest::windowPixelSnapCoversTextControlsAndDynamicContent()
{
    installWindowPixelSnap();
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    session.theme.setMotionEnabled(false);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
Window {
    id: hostWindow
    visible: true; width: 640; height: 400; color: "white"
    property real phase: .13
    property real zoom: 1
    property bool showDynamic: false
    property int clicks: 0
    Item {
        objectName: "phaseHost"
        x: 20 + hostWindow.phase; y: 15 + hostWindow.phase
        width: hostWindow.width; height: hostWindow.height
        transformOrigin: Item.TopLeft
        scale: hostWindow.zoom
        Text {
            objectName: "phaseText"
            x: 11.3; y: 10.2; width: 250; height: 44
            text: "像素 Pixel phase AV"; font.pixelSize: 18; color: "black"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        Label {
            objectName: "phaseLabel"
            x: 11.7; y: 65.3; width: 250; height: 32
            text: "Qt Controls Label"; font.pixelSize: 16; color: "black"
        }
        Text {
            objectName: "phaseResizeText"
            x: parent.width * .35; y: parent.height * .55
            width: parent.width * .4; height: 44
            text: "Resize 中文稳定"; font.pixelSize: 18; color: "black"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        EchoButton {
            objectName: "phaseButton"
            x: 11.2; y: 106.2; width: 130; text: "按钮"
            onClicked: hostWindow.clicks++
        }
        EchoTextField {
            objectName: "phaseEditor"
            x: 160.3; y: 106.4; width: 190
            placeholderText: "输入内容"
        }
        EchoIcon {
            objectName: "phaseIcon"
            x: 365.2; y: 105.1; width: 23; height: 23; name: "music"; color: "black"
        }
        Loader {
            x: 11.2; y: 175.6
            active: hostWindow.showDynamic
            sourceComponent: Text {
                objectName: "phaseDynamic"
                text: "动态创建"; font.pixelSize: 17; color: "black"
            }
        }
        Text {
            objectName: "phaseRichText"
            x: 11.4; y: 270.3; width: 175; height: 40
            text: "<b>Rich 中文</b>"; textFormat: Text.RichText
            horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignBottom
            font.pixelSize: 15; color: "black"
        }
        Text {
            objectName: "phaseElidedText"
            x: 210.1; y: 270.2; width: 115; height: 40
            text: "A long elided 中文 title"; elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 15; color: "black"
        }
        Text {
            objectName: "phaseRtlText"
            x: 335.2; y: 270.1; width: 120; height: 40
            text: "مرحبا بالعالم"; horizontalAlignment: Text.AlignRight
            font.pixelSize: 15; color: "black"
        }
        TextEdit {
            objectName: "phaseMultilineEdit"
            x: 400.4; y: 155.2; width: 100; height: 50
            text: "Edit 中文\nSecond line"; font.pixelSize: 15; color: "black"
        }
    }
    Popup {
        objectName: "phaseWindowPopup"
        popupType: Popup.Window
        x: 340; y: 60; width: 220; height: 110
        closePolicy: Popup.NoAutoClose
        contentItem: Text {
            objectName: "phasePopupText"
            text: "独立窗口 Popup"; font.pixelSize: 17; color: "black"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
})",
                      QUrl("qrc:/all-content-pixel-snap.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    if (qEnvironmentVariableIntValue("ECHO_PIXEL_SNAP_ALL") == 1)
        QTRY_VERIFY(session.window->contentItem()->findChild<EchoWindowPixelSnap *>(
            "echoWindowPixelSnap", Qt::FindDirectChildrenOnly));
    auto *scope = EchoWindowPixelSnap::forWindow(session.window);
    auto *text = session.item("phaseText");
    auto *label = session.item("phaseLabel");
    auto *button = session.item("phaseButton");
    auto *editor = session.item("phaseEditor");
    auto *rich = session.item("phaseRichText");
    auto *elided = session.item("phaseElidedText");
    auto *rtl = session.item("phaseRtlText");
    auto *multiline = session.item("phaseMultilineEdit");
    QVERIFY(text && label && button && editor);
    QVERIFY(rich && elided && rtl && multiline);
    const QRectF layout(text->position(), text->size());
    const auto font = text->property("font");
    const auto dpr = session.window->effectiveDevicePixelRatio();
    const auto capture = qEnvironmentVariable("ECHO_WINDOW_SNAP_CAPTURES");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (qreal zoom : {1., 1.25}) {
        session.root->setProperty("zoom", zoom);
        QImage reference;
        for (qreal phase : {.13, .37, .61, .89}) {
            session.root->setProperty("phase", phase);
            for (auto *item : {text, label, editor, rich, elided, rtl, multiline}) {
                QTRY_VERIFY(aligned(item->mapToScene(textAnchor(item)).x() * dpr));
                QTRY_VERIFY(aligned(item->mapToScene(textAnchor(item)).y() * dpr));
            }
            QCOMPARE(QRectF(text->position(), text->size()), layout);
            QCOMPARE(text->property("font"), font);
            const auto image = session.window->grabWindow();
            QVERIFY(!image.isNull());
            if (!capture.isEmpty())
                QVERIFY(image.save(capture + QString("/window-%1-%2.png").arg(zoom).arg(phase)));
            // Compare the same rendered text in baseline-relative coordinates.
            // Parent fractional movement must not change glyph coverage.
            const auto baseline = text->mapToScene(textAnchor(text)) * dpr;
            const auto crop = image.copy(
                QRect(qRound(baseline.x()) - 2, qRound(baseline.y()) - qCeil(24 * zoom * dpr),
                      qCeil((text->property("contentWidth").toReal() + 4) * zoom * dpr),
                      qCeil(30 * zoom * dpr)));
            if (reference.isNull())
                reference = crop;
            else if (supportsFloatingEffects(session.window)) {
                QCOMPARE(crop.size(), reference.size());
                int maximumDifference = 0;
                for (int y = 0; y < crop.height(); ++y)
                    for (int x = 0; x < crop.width(); ++x) {
                        const auto a = crop.pixelColor(x, y), b = reference.pixelColor(x, y);
                        maximumDifference = qMax(maximumDifference, qAbs(a.red() - b.red()));
                    }
                QVERIFY2(maximumDifference <= 1,
                         qPrintable(QString("text coverage difference %1 zoom %2 phase %3")
                                        .arg(maximumDifference)
                                        .arg(zoom)
                                        .arg(phase)));
            }
        }
    }
    // Real window resizes change centered text's internal baseline/left
    // origin, not just a synthetic parent translation.
    for (auto *variant : {rich, elided, rtl, multiline})
        variant->setVisible(false); // Keep the responsive glyph crop unobstructed.
    auto *responsive = session.item("phaseResizeText");
    QVERIFY(responsive);
    // Qt 6.12 glyph recycling resets subpixel AA to gray on its first
    // layout rebuild, independently of phase compensation. Warm that path
    // before comparing coverage under subsequent real resizes; otherwise
    // this would compare two different native antialiasing policies.
    session.window->resize(632, 392);
    QTRY_COMPARE(responsive->width(), 632 * .4);
    QVERIFY(!session.window->grabWindow().isNull());
    session.window->resize(640, 400);
    QTRY_COMPARE(responsive->width(), 640 * .4);
    QVERIFY(!session.window->grabWindow().isNull());
    QImage resizedReference;
    for (int step = 0; step < 8; ++step) {
        session.window->resize(640 + step * 8, 400 + step * 8);
        QTRY_COMPARE(responsive->width(), session.window->width() * .4);
        QTRY_VERIFY(aligned(responsive->mapToScene(textAnchor(responsive)).x() * dpr));
        QTRY_VERIFY(aligned(responsive->mapToScene(textAnchor(responsive)).y() * dpr));
        const auto image = session.window->grabWindow();
        const auto baseline = responsive->mapToScene(textAnchor(responsive)) * dpr;
        const auto crop = image.copy(
            QRect(qRound(baseline.x()) - 2, qRound(baseline.y()) - qCeil(24 * 1.25 * dpr),
                  qCeil((responsive->property("contentWidth").toReal() + 4) * 1.25 * dpr),
                  qCeil(30 * 1.25 * dpr)));
        if (!capture.isEmpty())
            QVERIFY(crop.save(capture + QString("/resize-%1.png").arg(step)));
        if (resizedReference.isNull())
            resizedReference = crop;
        else if (supportsFloatingEffects(session.window)) {
            QCOMPARE(crop.size(), resizedReference.size());
            int maximumDifference = 0;
            for (int y = 0; y < crop.height(); ++y)
                for (int x = 0; x < crop.width(); ++x)
                    maximumDifference =
                        qMax(maximumDifference, qAbs(crop.pixelColor(x, y).red() -
                                                     resizedReference.pixelColor(x, y).red()));
            // A resized render target changes the float projection matrix;
            // allow two 8-bit quantization levels, never a glyph/pixel shift.
            QVERIFY2(maximumDifference <= 2,
                     qPrintable(QString("resized text difference %1 step %2")
                                    .arg(maximumDifference)
                                    .arg(step)));
        }
    }
    session.root->setProperty("showDynamic", true);
    QQuickItem *dynamic = nullptr;
    QTRY_VERIFY((dynamic = session.item("phaseDynamic")));
    QTRY_VERIFY(aligned(dynamic->mapToScene(textAnchor(dynamic)).y() * dpr));
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      button->mapToScene(button->boundingRect().center()).toPoint());
    QTRY_COMPARE(session.root->property("clicks").toInt(), 1);
    editor->forceActiveFocus();
    for (const auto key : {Qt::Key_T, Qt::Key_Y, Qt::Key_P, Qt::Key_E, Qt::Key_D})
        QTest::keyClick(session.window, key);
    QCOMPARE(editor->property("text").toString(), QString("typed"));
    QTRY_VERIFY(aligned(editor->mapToScene(textAnchor(editor)).y() * dpr));
    // Fixed boxes can change their internal origin without changing geometry.
    rich->setVisible(true);
    rich->setProperty("topPadding", 1.37);
    rich->setProperty("verticalAlignment", int(Qt::AlignTop));
    rich->setProperty("text", "<i>Changed 中文</i>");
    QTRY_VERIFY(aligned(rich->mapToScene(textAnchor(rich)).x() * dpr));
    QTRY_VERIFY(aligned(rich->mapToScene(textAnchor(rich)).y() * dpr));
    auto *popup = session.root->findChild<QObject *>("phaseWindowPopup");
    QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QQuickItem *popupText = nullptr;
    QTRY_VERIFY((popupText = popup->findChild<QQuickItem *>("phasePopupText")) &&
                popupText->window());
    auto *popupWindow = popupText->window();
    QVERIFY(popupWindow != session.window);
    QPointer<EchoWindowPixelSnap> popupScope;
    QTRY_VERIFY((popupScope = popupWindow->contentItem()->findChild<EchoWindowPixelSnap *>(
                     "echoWindowPixelSnap", Qt::FindDirectChildrenOnly)));
    QTRY_VERIFY(aligned(popupText->mapToScene(textAnchor(popupText)).y() *
                        popupWindow->effectiveDevicePixelRatio()));
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popupScope || popupScope->statistics().trackedItems == 0);
    QVERIFY(Test::prepareNativeInput(session.window));
    button->forceActiveFocus();
    QTRY_VERIFY(!editor->hasActiveFocus()); // A blinking native caret is an actual scene change.
    QTest::qWait(30);
    const auto settled = scope->statistics().passes;
    QTest::qWait(60);
    QCOMPARE(scope->statistics().passes, settled);
    QPointer<QQuickItem> gone = dynamic;
    session.root->setProperty("showDynamic", false);
    QTRY_VERIFY(gone.isNull());
    scope->setAlignmentEnabled(false);
    QTRY_COMPARE(scope->statistics().trackedItems, 0);
    QCOMPARE(QRectF(text->position(), text->size()), layout);
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::windowPixelSnapPreservesTransformsAndRetiresWindows()
{
    ControlsSession session;
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
Window {
    visible: true; width: 416; height: 280
    Item {
        x: 10.3; y: 16.7; scale: 1.25
        Rectangle {
            objectName: "translatedRectangle"
            x: 8.2; y: 4.3; width: 61; height: 29; color: "red"
            transform: Translate { x: 3.2; y: 1.1 }
        }
    }
    Window { objectName: "otherSnapWindow"; width: 320; height: 240 }
})",
                      QUrl("qrc:/window-snap-lifecycle.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *scope = EchoWindowPixelSnap::forWindow(session.window);
    auto *item = session.item("translatedRectangle");
    QVERIFY(item);
    const QRectF layout(item->position(), item->size());
    const qreal dpr = session.window->effectiveDevicePixelRatio();
    QTRY_VERIFY(aligned(item->mapToScene({}).x() * dpr));
    QTRY_VERIFY(aligned(item->mapToScene({}).y() * dpr));
    auto transforms = item->transform();
    QCOMPARE(transforms.count(&transforms), 2);
    auto *userTransform = transforms.at(&transforms, 0);
    QVERIFY(userTransform);
    QCOMPARE(userTransform->property("x").toReal(), 3.2);
    userTransform->setProperty("x", 4.3);
    QTRY_VERIFY(aligned(item->mapToScene({}).x() * dpr));
    QCOMPARE(QRectF(item->position(), item->size()), layout);
    scope->setAlignmentEnabled(false);
    QTRY_COMPARE(scope->statistics().trackedItems, 0);
    QCOMPARE(transforms.count(&transforms), 1);
    QCOMPARE(transforms.at(&transforms, 0), userTransform);
    QCOMPARE(userTransform->property("x").toReal(), 4.3);
    scope->setAlignmentEnabled(true);
    QTRY_VERIFY(scope->statistics().compensatedItems > 0);
    auto *other = session.root->findChild<QQuickWindow *>("otherSnapWindow");
    QVERIFY(other);
    other->show();
    auto *otherScope = EchoWindowPixelSnap::forWindow(other);
    item->setParentItem(other->contentItem());
    QTRY_VERIFY(aligned(item->mapToScene({}).x() * other->effectiveDevicePixelRatio()));
    QTRY_VERIFY(otherScope->statistics().compensatedItems > 0);
    session.window->hide();
    QTRY_COMPARE(scope->statistics().trackedItems, 0);
    other->hide();
    QTRY_COMPARE(otherScope->statistics().trackedItems, 0);
    QCOMPARE(transforms.count(&transforms), 1);
    other->show();
    QTRY_VERIFY(otherScope->statistics().trackedItems > 0);
    QPointer<QQuickItem> removed = item;
    delete item;
    QTRY_VERIFY(removed.isNull());
    QVERIFY(!other->grabWindow().isNull());
}

void ControlsUiTest::windowPixelSnapResourceBudget()
{
    ControlsSession session;
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
Window {
    id: host
    visible: true; width: 1280; height: 800; color: "white"
    property real phase: .13
    Item {
        x: host.phase; y: host.phase
        Repeater {
            model: 1000
            Text {
                required property int index
                x: (index % 40) * 30; y: Math.floor(index / 40) * 28
                text: "Ab"; font.pixelSize: 13; color: "black"
            }
        }
    }
})",
                      QUrl("qrc:/window-snap-budget.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *scope = EchoWindowPixelSnap::forWindow(session.window);
    scope->setAlignmentEnabled(false);
    QTRY_COMPARE(scope->statistics().trackedItems, 0);
    const auto renderFrame = [&] {
        Test::FrameSpy frame(session.window);
        session.window->update();
        return frame.wait(2000);
    };
    // Warm the same scene and phase changes before the footprint sample;
    // first-use font/renderer caches must not be counted as alignment state.
    for (int frame = 0; frame < 120; ++frame) {
        session.root->setProperty("phase", .13 + (frame % 11) * .071);
        QVERIFY(renderFrame());
    }
    QTest::qWait(150);
    const auto before = nativeFootprint();
    scope->setAlignmentEnabled(true);
    QTRY_VERIFY(scope->statistics().compensatedItems >= 900);
    QVERIFY(renderFrame());
    QTest::qWait(100);
    const auto after = nativeFootprint();
    const auto first = scope->statistics();
    QElapsedTimer wall;
    wall.start();
    for (int frame = 0; frame < 120; ++frame) {
        session.root->setProperty("phase", .13 + (frame % 11) * .071);
        QVERIFY(renderFrame());
    }
    const auto final = scope->statistics();
    qInfo() << "window-pixel-snap-budget items=" << final.trackedItems
            << "transforms=" << final.compensatedItems
            << "entry-payload-bytes=" << final.payloadBytes << "footprint-before=" << before
            << "footprint-after=" << after
            << "footprint-delta=" << (before >= 0 && after >= 0 ? after - before : -1)
            << "passes=" << final.passes - first.passes
            << "cpu-pass-total-ms=" << (final.totalNanoseconds - first.totalNanoseconds) / 1e6
            << "maximum-pass-ms=" << final.maximumNanoseconds / 1e6 << "wall-ms=" << wall.elapsed();
    QVERIFY(final.trackedItems < 1100);
    QVERIFY(final.payloadBytes < 1024 * 1024);
    QVERIFY(final.passes - first.passes <= 125);
    session.window->hide();
    QTRY_COMPARE(scope->statistics().trackedItems, 0);
    QCOMPARE(scope->statistics().payloadBytes, 0);
}
