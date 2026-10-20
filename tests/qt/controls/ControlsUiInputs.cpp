#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"

void ControlsUiTest::continuousAngleWheelPreservesDistanceAndPixelInput()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
Window {
 visible: true; width: 600; height: 450
 Flickable {
  objectName: "wheelView"; anchors.fill: parent
  contentHeight: 30000; contentWidth: width; boundsBehavior: Flickable.StopAtBounds
  ScrollBar.vertical: EchoScrollBar {}
 }
})",
                      QUrl());
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *view = session.root->findChild<QQuickItem *>("wheelView");
    QVERIFY(view);
    auto *wheel = qobject_cast<EchoWheelScrollAttached *>(
        qmlAttachedPropertiesObject<EchoWheelScroll>(view, false));
    QVERIFY(wheel);
    const auto originalPreference =
        QGuiApplication::styleHints()->accessibility()->motionPreference();
    const auto restoreMotion = qScopeGuard([&] { notifyMotionPreference(originalPreference); });
    QVERIFY(notifyMotionPreference(Qt::MotionPreference::NoPreference));
    QTest::qWait(150); // Finish initial window/attached-scrollbar polish.
    auto send = [&](QPoint pixel, int angle, Qt::ScrollPhase phase = Qt::NoScrollPhase,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPointF point(200, 200);
        QWheelEvent event(point, session.window->mapToGlobal(point.toPoint()), pixel,
                          QPoint(0, angle), Qt::NoButton, modifiers, phase, false,
                          pixel.isNull() ? Qt::MouseEventNotSynthesized
                                         : Qt::MouseEventSynthesizedBySystem);
        static ulong timestamp = 1000;
        event.setTimestamp(timestamp += 16);
        QCoreApplication::sendEvent(session.window, &event);
    };
    const qreal step = QGuiApplication::styleHints()->wheelScrollLines() * 24;
    for (int interval : {0, 16, 33, 100}) {
        view->setProperty("contentY", 1000.);
        for (int i = 0; i < 20; ++i) {
            send({}, -120);
            if (i == 10)
                view->setProperty("contentHeight", interval == 16 ? 31000. : 30000.);
            if (interval)
                QTest::qWait(interval);
        }
        QTRY_VERIFY(!wheel->running());
        QVERIFY2(qAbs(view->property("contentY").toReal() - (1000 + 20 * step)) < 0.1,
                 qPrintable(QString::number(view->property("contentY").toReal())));
        qInfo() << "wheel interval" << interval << "distance"
                << view->property("contentY").toReal() - 1000;
    }
    view->setProperty("contentY", 1000.);
    for (int i = 0; i < 8; ++i)
        send({}, -15);
    QTRY_VERIFY(!wheel->running());
    QVERIFY(qAbs(view->property("contentY").toReal() - 1000 - step) < 0.1);
    auto *hints = QGuiApplication::styleHints();
    const int originalLines = hints->wheelScrollLines();
    const auto restoreLines =
        qScopeGuard([hints, originalLines] { hints->setWheelScrollLines(originalLines); });
    hints->setWheelScrollLines(5);
    view->setProperty("contentY", 1000.);
    send({}, -120);
    QTRY_VERIFY(!wheel->running());
    QVERIFY(qAbs(view->property("contentY").toReal() - 1120) < 0.1);
    hints->setWheelScrollLines(originalLines);
    send({}, -120);
    QTest::qWait(25);
    const auto beforeReverse = view->property("contentY").toReal();
    send({}, 120);
    QTRY_VERIFY(!wheel->running());
    QVERIFY(qAbs(view->property("contentY").toReal() - beforeReverse + step) < 0.1);
    const auto beforeCtrl = view->property("contentY").toReal();
    send({}, -120, Qt::NoScrollPhase, Qt::ControlModifier);
    QTest::qWait(150);
    QCOMPARE(view->property("contentY").toReal(), beforeCtrl);
    view->setProperty("contentY", 10.);
    send({}, 120);
    QTRY_VERIFY(!wheel->running());
    QCOMPARE(view->property("contentY").toReal(), 0.);
    view->setProperty("contentY", 1000.);
    QSignalSpy movement(view, SIGNAL(movementStarted()));
    send({}, 0, Qt::ScrollBegin);
    for (int i = 0; i < 3; ++i)
        send(QPoint(0, -40), -80, Qt::ScrollUpdate);
    QTRY_VERIFY(view->property("contentY").toReal() > 1000);
    QVERIFY(movement.count() > 0);
    QVERIFY(!wheel->running());
    send({}, 0, Qt::ScrollEnd);
    send({}, -120);
    QVERIFY(wheel->running());
    view->setVisible(false);
    QVERIFY(!wheel->running());
    view->setVisible(true);
    send({}, -120);
    QVERIFY(wheel->running());
    session.window->hide();
    QVERIFY(!wheel->running());
}

void ControlsUiTest::wheelCancellationAndMotionPreferences()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
Window { visible: true; width: 600; height: 450
 Flickable { objectName: "wheelView"; anchors.fill: parent; contentWidth: width; contentHeight: 30000
  boundsBehavior: Flickable.StopAtBounds
  ScrollBar.vertical: EchoScrollBar {}
 }
})",
                      QUrl());
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *view = session.item("wheelView");
    QVERIFY(view);
    auto *wheel = qobject_cast<EchoWheelScrollAttached *>(
        qmlAttachedPropertiesObject<EchoWheelScroll>(view, false));
    QVERIFY(wheel);
    const auto originalPreference =
        QGuiApplication::styleHints()->accessibility()->motionPreference();
    const auto restore = qScopeGuard([&] { notifyMotionPreference(originalPreference); });
    QVERIFY(notifyMotionPreference(Qt::MotionPreference::NoPreference));
    QTest::qWait(100);
    ulong timestamp = 1000;
    const auto send = [&] {
        const QPointF point(200, 200);
        QWheelEvent event(point, session.window->mapToGlobal(point.toPoint()), {}, {0, -120},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        event.setTimestamp(timestamp += 20);
        QCoreApplication::sendEvent(session.window, &event);
    };
    const auto step = QGuiApplication::styleHints()->wheelScrollLines() * 24;
    view->setProperty("contentY", 1000.);
    send();
    QVERIFY(wheel->running());
    QTest::qWait(25);
    view->setProperty("interactive", false);
    const auto stopped = view->property("contentY").toReal();
    QVERIFY(!wheel->running());
    QTest::qWait(180);
    QCOMPARE(view->property("contentY").toReal(), stopped);
    view->setProperty("interactive", true);
    QVERIFY(!wheel->running());
    view->setProperty("contentY", 1000.);
    send();
    QVERIFY(wheel->running());
    QTest::qWait(25);
    session.theme.setMotionEnabled(false);
    QVERIFY(!wheel->motionEnabled());
    QVERIFY(!wheel->running());
    QCOMPARE(view->property("contentY").toReal(), 1000. + step);
    send();
    QVERIFY(!wheel->running());
    QCOMPARE(view->property("contentY").toReal(), 1000. + 2 * step);
    session.theme.setMotionEnabled(true);
    send();
    QVERIFY(wheel->running());
    QVERIFY(notifyMotionPreference(Qt::MotionPreference::ReducedMotion));
    QVERIFY(!wheel->running());
    QCOMPARE(view->property("contentY").toReal(), 1000. + 3 * step);
    send();
    QVERIFY(!wheel->running());
    QCOMPARE(view->property("contentY").toReal(), 1000. + 4 * step);
    QVERIFY(notifyMotionPreference(Qt::MotionPreference::NoPreference));
    send();
    QVERIFY(wheel->running());
    QTRY_VERIFY(!wheel->running());
    QCOMPARE(view->property("contentY").toReal(), 1000. + 5 * step);
}

void ControlsUiTest::pageWheelRegionsPreserveNativeEvents()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
Window { id: host; visible: true; width: 600; height: 450
 property int childWheels: 0
 EchoPageScrollContainer {
  id: page; anchors.fill: parent; scrollView: flick; stickyHeaders: [header]
  Flickable {
   id: flick; objectName: "wheelView"; x: 40; width: parent.width - 80; height: parent.height
   contentWidth: width; contentHeight: 30000; topMargin: 60; boundsBehavior: Flickable.StopAtBounds
   ScrollBar.vertical: EchoPageScrollBar { view: flick; container: page }
  }
  EchoPageStickyHeader { id: header; container: page; height: 60
   MouseArea { width: 80; height: parent.height; onWheel: event => { host.childWheels++; event.accepted = true; } }
  }
 }
})",
                      QUrl());
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *view = session.item("wheelView");
    QVERIFY(view);
    auto *wheel = qobject_cast<EchoWheelScrollAttached *>(
        qmlAttachedPropertiesObject<EchoWheelScroll>(view, false));
    QVERIFY(wheel);
    WheelRecorder recorder;
    view->installEventFilter(&recorder);
    ulong timestamp = 5000;
    const auto send = [&](QPointF position, QPoint pixel, QPoint angle, Qt::ScrollPhase phase,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QWheelEvent event(position, session.window->mapToGlobal(position.toPoint()), pixel, angle,
                          Qt::NoButton, modifiers, phase, true, Qt::MouseEventSynthesizedBySystem);
        event.setTimestamp(timestamp += 20);
        QCoreApplication::sendEvent(session.window, &event);
    };
    QTest::qWait(100);
    const auto step = QGuiApplication::styleHints()->wheelScrollLines() * 24;
    for (const auto &position :
         {QPointF(200, 180), QPointF(200, 20), QPointF(15, 180), QPointF(585, 180)}) {
        QMetaObject::invokeMethod(view, "cancelFlick");
        view->setProperty("contentY", 1000.);
        recorder.records.clear();
        send(position, {}, {0, -120}, Qt::NoScrollPhase);
        QCOMPARE(recorder.records.size(), 1);
        QTRY_VERIFY(!wheel->running());
        QCOMPARE(view->property("contentY").toReal(), 1000. + step);
        view->setProperty("contentY", 1000.);
        recorder.records.clear();
        QSignalSpy movement(view, SIGNAL(movementStarted()));
        send(position, {}, {}, Qt::ScrollBegin);
        for (int i = 0; i < 3; ++i)
            send(position, {0, -40}, {0, -80}, Qt::ScrollUpdate);
        send(position, {0, -20}, {0, -40}, Qt::ScrollMomentum);
        send(position, {}, {}, Qt::ScrollEnd);
        QCOMPARE(recorder.records.size(), 6);
        QCOMPARE(recorder.records.first().phase, Qt::ScrollBegin);
        QCOMPARE(recorder.records.at(1).pixel, QPoint(0, -40));
        QCOMPARE(recorder.records.at(1).angle, QPoint(0, -80));
        QCOMPARE(recorder.records.at(1).phase, Qt::ScrollUpdate);
        QCOMPARE(recorder.records.at(1).timestamp, quint64(timestamp - 80));
        QCOMPARE(recorder.records.at(1).inverted, true);
        QCOMPARE(recorder.records.at(1).source, Qt::MouseEventSynthesizedBySystem);
        QCOMPARE(recorder.records.at(1).device, QPointingDevice::primaryPointingDevice());
        QCOMPARE(recorder.records.at(4).phase, Qt::ScrollMomentum);
        QCOMPARE(recorder.records.last().phase, Qt::ScrollEnd);
        QVERIFY(!wheel->running());
        QVERIFY(!movement.isEmpty());
        QVERIFY(view->property("contentY").toReal() > 1000.);
        QMetaObject::invokeMethod(view, "cancelFlick");
        view->setProperty("contentY", 1000.);
        recorder.records.clear();
        // Angle-only events with a phase must also follow native Flickable.
        send(position, {}, {}, Qt::ScrollBegin);
        send(position, {}, {0, -120}, Qt::ScrollUpdate);
        QCOMPARE(recorder.records.size(), 2);
        QCOMPARE(recorder.records.last().phase, Qt::ScrollUpdate);
        QVERIFY(!wheel->running());
        send(position, {}, {}, Qt::ScrollEnd);
        QMetaObject::invokeMethod(view, "cancelFlick");
    }
    recorder.records.clear();
    view->setProperty("contentY", 1000.);
    send({30, 20}, {}, {0, -120}, Qt::NoScrollPhase);
    QCOMPARE(session.root->property("childWheels").toInt(), 1);
    QVERIFY(recorder.records.isEmpty());
    QCOMPARE(view->property("contentY").toReal(), 1000.);
    send({200, 20}, {}, {0, -120}, Qt::NoScrollPhase, Qt::ControlModifier);
    send({200, 20}, {-40, 0}, {-120, 0}, Qt::ScrollUpdate);
    QVERIFY(recorder.records.isEmpty());
    // Page and scrollbar bindings must agree when the app disables motion.
    session.theme.setMotionEnabled(false);
    QVERIFY(!wheel->motionEnabled());
    send({200, 20}, {}, {0, -120}, Qt::NoScrollPhase);
    QVERIFY(!wheel->running());
    QCOMPARE(view->property("contentY").toReal(), 1000. + step);
}

void ControlsUiTest::imageDensityTracksScaleWithoutTrackingScroll()
{
    QQuickWindow window;
    QQuickItem parent(window.contentItem());
    QQuickItem item(&parent);
    item.setSize({178, 178});
    EchoImageMetrics metrics;
    metrics.setItem(&item);
    metrics.setSource(QUrl("https://imgessl.kugou.com/custom/240/20210507/art.jpg"));
    parent.setScale(2);
    item.setScale(1.25);
    const auto required = qCeil(178 * 2 * 1.25 * window.effectiveDevicePixelRatio());
    QVERIFY(metrics.pixelSize().width() >= required);
    QVERIFY(metrics.resolvedSource().path().contains("/800/") ||
            metrics.resolvedSource().path().contains("/1600/"));
    const auto full = metrics.pixelSize();
    QSignalSpy density(&metrics, &EchoImageMetrics::metricsChanged);
    parent.setPosition({120, -4000});
    item.setPosition({40, 80});
    QCOMPARE(density.size(), 0); // Scrolling does not drive artwork resolution work.
    item.setScale(.1);
    QCOMPARE(metrics.pixelSize(), full); // Collapsing keeps the loaded high-water copy.
    const QUrl signedArtwork("https://imgessl.kugou.com/custom/240/date/art.jpg?sig=keep");
    metrics.setSource(signedArtwork);
    QCOMPARE(metrics.resolvedSource(), signedArtwork);
    const QUrl foreign("https://images.example/custom/240/date/a%2Fb.jpg?size=240&sig=keep");
    metrics.setSource(foreign);
    QCOMPARE(metrics.resolvedSource(), foreign);
    const auto file = QUrl::fromLocalFile("/tmp/a-{size}.png");
    metrics.setSource(file);
    QCOMPARE(metrics.resolvedSource(), file);
    metrics.setActive(false);
    QVERIFY(metrics.resolvedSource().isEmpty());
    QVERIFY(metrics.pixelSize().isEmpty());
    parent.setScale(4);
    metrics.setActive(true);
    QVERIFY(metrics.pixelSize().width() < full.width()); // Fresh active cycle resets the peak.
    parent.setScale(1.25);
    item.setScale(1);
    item.setSize({56, 56});
    EchoImageMetrics transition;
    transition.setItem(&item);
    transition.setMinimumSize({380, 380});
    QVERIFY(transition.decodeSize().width() >= 380 * 1.25 * window.effectiveDevicePixelRatio());
    QVERIFY(transition.pixelSize().width() < transition.decodeSize().width());
    transition.setActive(false);
    QVERIFY(transition.pixelSize().isEmpty());
    QVERIFY(transition.decodeSize().isEmpty());
}

void ControlsUiTest::selectProjectionKeepsTypedValuesAndTracksModelChanges()
{
    EchoSelectModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setSource(
        QVariantList{QVariantMap{{"label", "Alpha"}, {"value", 1}},
                     QVariantMap{{"label", "Blocked"}, {"value", "disabled"}, {"disabled", true}},
                     QVariantMap{{"label", "Beta"}, {"value", "1"}}});
    QCOMPARE(model.optionForValue(1).value("label").toString(), QString("Alpha"));
    QCOMPARE(model.optionForValue(QString("1")).value("label").toString(), QString("Beta"));
    QCOMPARE(model.nextEnabled(0, 1), 2);
    model.setQuery("bETA");
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.option(0).value("sourceIndex").toInt(), 2);
    QStringListModel live({"One", "Two"});
    model.setQuery({});
    model.setSource(QVariant::fromValue(static_cast<QObject *>(&live)));
    QCOMPARE(model.count(), 2);
    live.setStringList({"Three"});
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.option(0).value("label").toString(), QString("Three"));
}

void ControlsUiTest::selectSearchMultiKeyboardPlacementAndVirtualization()
{
    ControlsSession session;
    session.theme.setMode(ThemeStore::Light);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create("SelectHarness.qml"));
    auto *selector = session.item("featureSelect");
    QVERIFY(selector);
    QPointer<QObject> popup;
    QVERIFY(!selector->property("popup").value<QObject *>());
    auto *projection = selector->property("optionModel").value<EchoSelectModel *>();
    QVERIFY(projection);
    QTRY_COMPARE(projection->count(), 3);
    auto *bound = session.item("indexBoundSelect");
    QVERIFY(bound);
    QSignalSpy activated(bound, SIGNAL(activated(int)));
    bound->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(session.window, Qt::Key_Down);
    QTest::keyClick(session.window, Qt::Key_Return);
    QTRY_COMPARE(bound->property("currentIndex").toInt(), 2);
    QCOMPARE(activated.count(), 1);
    QTRY_VERIFY(!bound->property("popup").value<QObject *>());
    // Native presentation setters must preserve an application's binding.
    session.root->setProperty("indexSelection", 1);
    QTRY_COMPARE(bound->property("currentIndex").toInt(), 1);
    session.root->setProperty("indexSelection", 0);
    QTRY_COMPARE(bound->property("currentIndex").toInt(), 0);
    session.root->setProperty("selected", 1);
    QTRY_COMPARE(selector->property("currentIndex").toInt(), 0);
    auto *input = findVisual(selector, "selectSearchInput");
    QVERIFY(input);
    input->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(input->hasActiveFocus());
    for (const auto key : QByteArray("Beta"))
        QTest::keyClick(session.window, key);
    QTRY_VERIFY((popup = selector->property("popup").value<QObject *>()));
    QTRY_VERIFY(popup->property("opened").toBool());
    QTRY_COMPARE(selector->property("searchTerm").toString(), QString("Beta"));
    QTRY_COMPARE(projection->count(), 1);
    const auto searchPoint =
        input->mapToScene(QPointF(input->width() / 2, input->height() / 2)).toPoint();
    Test::moveNativePointer(session.window, searchPoint);
    QTRY_VERIFY(selector->property("hovered").toBool());
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, searchPoint);
    QTRY_VERIFY(input->hasActiveFocus());
    // Clicking the search trigger must not consume the underlying input.
    QTRY_VERIFY((popup = selector->property("popup").value<QObject *>()));
    QTRY_VERIFY(popup->property("opened").toBool());
    QCOMPARE(selector->property("searchTerm").toString(), QString("Beta"));
    const auto captures = qEnvironmentVariable("ECHO_SELECT_COLOR_CAPTURES");
    const auto capture = [&](const QString &name) {
        if (captures.isEmpty())
            return true;
        QDir().mkpath(captures);
        return session.window->grabWindow().save(captures + "/" + name + ".png");
    };
    QVERIFY(capture("select-search"));
    const auto selectImage = session.window->grabWindow();
    if (!selectImage.isNull() &&
        session.window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
        auto *surface = popup->property("background").value<QQuickItem *>();
        QVERIFY(surface);
        const auto below =
            surface->mapToScene(QPointF(surface->width() / 2, surface->height() + 3));
        const auto ratio = selectImage.devicePixelRatio();
        const auto shadow =
            selectImage.pixelColor(qRound(below.x() * ratio), qRound(below.y() * ratio));
        QVERIFY2(shadow.red() < 250, "Popover shadow must extend beyond its rounded surface body");
    }
    QTest::keyClick(session.window, Qt::Key_Down);
    QTest::keyClick(session.window, Qt::Key_Return);
    QTRY_COMPARE(selector->property("currentIndex").toInt(), 2);
    QCOMPARE(session.root->property("selected").toString(), QString("1"));
    QTRY_VERIFY(popup.isNull());
    session.root->setProperty("multi", true);
    session.root->setProperty("selected", QVariantList{1});
    QVERIFY(QMetaObject::invokeMethod(selector, "openPopup"));
    QTRY_VERIFY((popup = selector->property("popup").value<QObject *>()));
    QTRY_VERIFY(popup->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(selector, "choose", Q_ARG(int, 1)));
    QCOMPARE(session.root->property("selected").toList().size(), 1);
    QVERIFY(QMetaObject::invokeMethod(selector, "choose", Q_ARG(int, 2)));
    QTRY_COMPARE(session.root->property("selected").toList().size(), 2);
    QCOMPARE(selector->property("overflowCount").toInt(), 1);
    QVERIFY(capture("select-multi"));
    QVERIFY(popup->property("visible").toBool());
    auto *removeTag = findVisual(selector, "selectTagRemoveAction");
    QVERIFY(removeTag);
    const auto removePoint =
        removeTag->mapToScene(QPointF(removeTag->width() / 2, removeTag->height() / 2)).toPoint();
    Test::moveNativePointer(session.window, removePoint);
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, removePoint);
    QTRY_COMPARE(session.root->property("selected").toList().size(), 1);
    QCOMPARE(session.root->property("selected").toList().first().toString(), QString("1"));
    QCOMPARE(selector->property("overflowCount").toInt(), 0);
    QVERIFY(popup->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(selector, "choose", Q_ARG(int, 0)));
    auto *clearAction = findVisual(selector, "selectIndicatorAction");
    QVERIFY(clearAction);
    const auto clearPoint =
        clearAction->mapToScene(QPointF(clearAction->width() / 2, clearAction->height() / 2))
            .toPoint();
    Test::moveNativePointer(session.window, clearPoint);
    QTRY_VERIFY(selector->property("showClear").toBool());
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, clearPoint);
    QTRY_VERIFY(session.root->property("selected").toList().isEmpty());
    QVERIFY(popup->property("visible").toBool());
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(popup.isNull());
    session.root->setProperty("multi", false);
    session.root->setProperty("selected", QString());
    QTRY_VERIFY(!selector->property("showClear").toBool());
    // Unknown nonempty values still expose clear; an empty value does not.
    session.root->setProperty("selected", 0);
    // Popup teardown resets native hover. Re-enter before checking a
    // hover-only affordance, rather than relying on a stale cursor state.
    Test::moveNativePointer(session.window, QPoint(740, 20));
    QTRY_VERIFY(!selector->property("hovered").toBool());
    Test::moveNativePointer(session.window, clearPoint);
    QTRY_VERIFY(selector->property("hovered").toBool());
    QTRY_VERIFY(selector->property("showClear").toBool());
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, clearPoint);
    QTRY_COMPARE(session.root->property("selected").toString(), QString());
    session.root->setProperty("multi", true);
    QVariantList large;
    for (int i = 0; i < 50000; ++i)
        large.append(QVariantMap{{"label", QString("Option %1 中文").arg(i)}, {"value", i}});
    session.root->setProperty("items", large);
    session.root->setProperty("zoom", 1.25);
    selector->setPosition({540, 450});
    QVERIFY(QMetaObject::invokeMethod(selector, "openPopup"));
    QTRY_VERIFY((popup = selector->property("popup").value<QObject *>()));
    QTRY_VERIFY(popup->property("opened").toBool());
    QTRY_COMPARE(projection->count(), 50000);
    const auto scale = popup->property("uiScale").toDouble();
    const auto x = popup->property("x").toDouble(), y = popup->property("y").toDouble();
    QVERIFY(x >= 12 && y >= 12);
    QVERIFY(x + popup->property("width").toDouble() * scale <= session.window->width() - 12);
    QVERIFY(y + popup->property("height").toDouble() * scale <= session.window->height() - 12);
    int delegates = 0;
    const auto count = [&](auto &&self, QQuickItem *item) -> void {
        if (item->objectName().startsWith("selectOption-"))
            ++delegates;
        for (auto *child : item->childItems())
            self(self, child);
    };
    count(count, session.window->contentItem());
    QVERIFY(delegates > 0 && delegates < 30);
    auto *list = popup->property("contentItem").value<QQuickItem *>();
    QVERIFY(list);
    list->setProperty("contentY", 1000000.);
    QTest::qWait(60);
    delegates = 0;
    count(count, session.window->contentItem());
    QVERIFY(delegates < 40);
    selector->setProperty("searchTerm", "no matching option");
    QTRY_COMPARE(projection->count(), 0);
    QMetaObject::invokeMethod(popup, "close");
    QTRY_VERIFY(popup.isNull());
    session.root->setProperty(
        "items", QVariantList{QVariantMap{{"label", QString(160, QChar(u'长'))}, {"value", 9}}});
    session.root->setProperty("wrapped", true);
    QVERIFY(QMetaObject::invokeMethod(selector, "openPopup"));
    QTRY_VERIFY((popup = selector->property("popup").value<QObject *>()));
    QTRY_VERIFY(popup->property("opened").toBool());
    QQuickItem *wrapped = nullptr;
    QTRY_VERIFY((wrapped = session.item("selectOption-0")));
    QTRY_VERIFY(wrapped->height() > 36);
    QVERIFY(capture("select-wrap"));
    selector->setEnabled(false);
    QTRY_VERIFY(popup.isNull());
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::numberInputCommitsAndClampsAcrossAllInputPaths()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    auto *number = session.item("numberInput");
    QVERIFY(number);
    const auto point = [&](qreal x, qreal y) {
        return number->mapToScene(QPointF(x, y)).toPoint();
    };
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      point(number->width() - 14, number->height() / 4));
    QTRY_COMPARE(number->property("value").toInt(), 8);
    QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier,
                      point(number->width() - 14, number->height() / 4));
    QTest::qWait(550);
    QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier,
                        point(number->width() - 14, number->height() / 4));
    QTRY_COMPARE(number->property("value").toInt(), 9);
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      point(number->width() - 14, number->height() * .75));
    QTRY_COMPARE(number->property("value").toInt(), 8);
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point(30, 20));
    QTest::keyClick(session.window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(session.window, Qt::Key_Backspace);
    QTest::keyClick(session.window, Qt::Key_3);
    QTest::keyClick(session.window, Qt::Key_Return);
    QTRY_COMPARE(number->property("value").toInt(), 3);
    number->setEnabled(false);
    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                      point(number->width() - 14, number->height() / 4));
    QCOMPARE(number->property("value").toInt(), 3);
    QCOMPARE(warnings.size(), 0);
}
