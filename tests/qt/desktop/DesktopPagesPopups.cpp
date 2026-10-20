#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

void DesktopPagesTest::productionQualityPopupContainsAndScrollsAllOptions()
{
    Session session;
    session.preferences.setValue("playback/volume", 0);
    PlaybackService playback(session.api, session.preferences);
    // The production quality command is enabled only for an actual track.
    PlaybackServiceTestAccess::quality(playback, "hi_res");
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.window->setMinimumHeight(0);
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(!session.root->findChild<QObject *>("playbackOptionsPopup"));
    QCOMPARE(playback.qualities().size(), 7);
    const auto capture = qEnvironmentVariable("ECHO_QUALITY_POPUP_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    QElapsedTimer inputClock;
    inputClock.start();
    for (const auto theme : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(theme);
        for (const auto scale : {1.0, 1.25}) {
            session.shell.resetZoom();
            if (scale > 1)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            for (const auto windowHeight : {800, 440}) {
                session.window->resize(1150, windowHeight);
                QTest::qWait(100);
                auto popup = session.openPlayerPopup("quality", "playbackOptionsPopup");
                QVERIFY(popup);
                QTest::qWait(100);
                auto *view = visual(session.window->contentItem(), "playbackOptionsViewport");
                auto *last = visual(session.window->contentItem(), "playbackOption-viper_atmos");
                QVERIFY(view && last);
                QVERIFY(view->clip());
                QTRY_VERIFY(view->height() > 0);
                QTRY_VERIFY(qAbs(view->property("contentY").toReal()) < .5);
                const auto contentHeight = view->property("contentHeight").toReal();
                if (windowHeight == 800) {
                    // Seven options fit naturally: no arbitrary 420 DIP cutoff.
                    QTRY_VERIFY(qAbs(view->height() - contentHeight) < .5);
                    // Canonical display inputs exercise both two-line states;
                    // these are UI data, not fabricated upstream API samples.
                    for (const auto &quality : playback.qualities()) {
                        auto data = quality.toMap();
                        const auto key = data.value("value").toString();
                        data["detail"] =
                            key == "standard" ? "2.3 MB" : QStringLiteral("暂无该音质");
                        auto *row = visual(session.window->contentItem(), "playbackOption-" + key);
                        QVERIFY(row);
                        row->setProperty("modelData", data);
                    }
                    QTest::qWait(100);
                    const auto image = session.window->grabWindow();
                    QVERIFY(!image.isNull());
                    const qreal ratio = image.width() / qreal(session.window->width());
                    const auto inkBounds = [&](QQuickItem *text) {
                        const auto bounds =
                            text->mapRectToScene(QRectF(0, 0, text->width(), text->height()));
                        const auto pixels = QRectF(bounds.topLeft() * ratio, bounds.size() * ratio)
                                                .toAlignedRect()
                                                .intersected(image.rect());
                        QHash<QRgb, int> frequencies;
                        for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                            for (int x = pixels.left(); x <= pixels.right(); ++x)
                                ++frequencies[image.pixel(x, y)];
                        QRgb background = 0;
                        int count = 0;
                        for (auto it = frequencies.cbegin(); it != frequencies.cend(); ++it)
                            if (it.value() > count) {
                                background = it.key();
                                count = it.value();
                            }
                        int top = image.height(), bottom = -1;
                        for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                            for (int x = pixels.left(); x <= pixels.right(); ++x) {
                                const auto pixel = image.pixel(x, y);
                                if (std::max({qAbs(qRed(pixel) - qRed(background)),
                                              qAbs(qGreen(pixel) - qGreen(background)),
                                              qAbs(qBlue(pixel) - qBlue(background))}) > 25) {
                                    top = std::min(top, y);
                                    bottom = std::max(bottom, y);
                                }
                            }
                        return QPair<int, int>{top, bottom};
                    };
                    qreal tagRight = -1, checkCenter = -1;
                    for (const auto &quality : playback.qualities()) {
                        const auto key = quality.toMap().value("value").toString();
                        auto *row = visual(session.window->contentItem(), "playbackOption-" + key);
                        QVERIFY(row);
                        auto *tag = visual(row, "playbackOptionTag");
                        auto *check = visual(row, "playbackOptionCheck");
                        auto *labels = visual(row, "playbackOptionLabels");
                        auto *title = visual(row, "playbackOptionTitle");
                        auto *detail = visual(row, "playbackOptionDetail");
                        QVERIFY(row && tag && check && labels && title && detail);
                        const auto right = tag->mapToScene(QPointF(tag->width(), 0)).x();
                        const auto center = check->mapToScene(QPointF(check->width() / 2, 0)).x();
                        const auto centerY = [](QQuickItem *item) {
                            return item->mapToScene(QPointF(0, item->height() / 2)).y();
                        };
                        QVERIFY2(
                            qAbs(centerY(labels) - centerY(tag)) < .5,
                            qPrintable(QString("labels y=%1 h=%2 center=%3 tag y=%4 h=%5 center=%6")
                                           .arg(labels->y())
                                           .arg(labels->height())
                                           .arg(centerY(labels))
                                           .arg(tag->y())
                                           .arg(tag->height())
                                           .arg(centerY(tag))));
                        QVERIFY(qAbs(centerY(check) - centerY(tag)) < .5);
                        QCOMPARE(title->height(), 20.);
                        QCOMPARE(detail->height(), 16.);
                        QCOMPARE(check->size(), QSizeF(14, 14));
                        auto *titleText = title->property("textItem").value<QQuickItem *>();
                        auto *detailText = detail->property("textItem").value<QQuickItem *>();
                        auto *tagText = visualText(tag, tag->property("text").toString());
                        QVERIFY(titleText && detailText && tagText);
                        const auto titleInk = inkBounds(titleText);
                        const auto detailInk = inkBounds(detailText);
                        const auto tagInk = inkBounds(tagText);
                        QVERIFY(titleInk.second >= titleInk.first);
                        QVERIFY(detailInk.second >= detailInk.first);
                        QVERIFY(tagInk.second >= tagInk.first);
                        const auto textCenter =
                            (titleInk.first + detailInk.second + 1) / (2 * ratio);
                        const auto tagCenter = (tagInk.first + tagInk.second + 1) / (2 * ratio);
                        QVERIFY(qAbs(textCenter - centerY(tag)) <= 1.5 * scale);
                        QVERIFY(qAbs(tagCenter - centerY(tag)) <= 1.5 * scale);
                        if (tagRight >= 0) {
                            QVERIFY(qAbs(tagRight - right) < .5);
                            QVERIFY(qAbs(checkCenter - center) < .5);
                        }
                        tagRight = right;
                        checkCenter = center;
                    }
                } else {
                    QVERIFY(contentHeight > view->height());
                    const auto point =
                        view->mapToScene(QPointF(view->width() / 2, view->height() / 2));
                    QTest::mouseMove(session.window, point.toPoint());
                    QWheelEvent wheel(point, session.window->mapToGlobal(point.toPoint()), QPoint(),
                                      QPoint(0, -2400), Qt::NoButton, Qt::NoModifier,
                                      Qt::NoScrollPhase, false);
                    // Distinct native impulses need increasing timestamps;
                    // repeated timestamp-zero events distort Flickable velocity.
                    wheel.setTimestamp(inputClock.elapsed());
                    QCoreApplication::sendEvent(session.window, &wheel);
                    QTRY_VERIFY(view->property("contentY").toReal() > 0);
                    QTRY_VERIFY(qAbs(view->property("contentY").toReal() + view->height() -
                                     contentHeight) < 1);
                    QTRY_VERIFY(!view->property("moving").toBool());
                    const QQmlProperty wheelRunning(view, "EchoWheelScroll.running",
                                                    qmlContext(view));
                    QVERIFY(wheelRunning.isValid());
                    QTRY_VERIFY(!wheelRunning.read().toBool());
                }
                const auto viewportBounds =
                    view->mapRectToScene(QRectF(0, 0, view->width(), view->height()));
                const auto lastBounds =
                    last->mapRectToScene(QRectF(0, 0, last->width(), last->height()));
                QVERIFY2(viewportBounds.adjusted(-.5, -.5, .5, .5).contains(lastBounds),
                         qPrintable(
                             QString("viewport=%1,%2 %3x%4 last=%5,%6 %7x%8 contentY=%9 height=%10")
                                 .arg(viewportBounds.x())
                                 .arg(viewportBounds.y())
                                 .arg(viewportBounds.width())
                                 .arg(viewportBounds.height())
                                 .arg(lastBounds.x())
                                 .arg(lastBounds.y())
                                 .arg(lastBounds.width())
                                 .arg(lastBounds.height())
                                 .arg(view->property("contentY").toReal())
                                 .arg(view->property("contentHeight").toReal())));
                QVERIFY(viewportBounds.top() >= 12 * scale - .5);
                QVERIFY(viewportBounds.bottom() <= session.window->height() - 12 * scale + .5);
                QTest::qWait(100);
                if (!capture.isEmpty())
                    QVERIFY(session.window->grabWindow().save(
                        capture + QString("/quality-%1-%2-%3.png")
                                      .arg(theme == ThemeStore::Light ? "light" : "dark")
                                      .arg(scale)
                                      .arg(windowHeight)));
                QVERIFY(QMetaObject::invokeMethod(popup.data(), "close"));
                QTRY_VERIFY(popup.isNull());
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}
