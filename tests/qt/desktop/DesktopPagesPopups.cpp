#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"

namespace EchoMusic
{
class CastServiceTestAccess
{
public:
    static void setRemoteForVolumeUi(CastService &service, bool remote)
    {
        service.m_active = remote ? QStringLiteral("volume-ui-fixture") : QString();
        emit service.changed();
    }
};
} // namespace EchoMusic

void DesktopPagesTest::productionPopoversSwitchOnOneOutsideClick()
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
    session.theme.setFloatingFrosted(false);
    session.window->resize(1150, 900);
    QQuickItem anchor(session.window->contentItem());
    anchor.setSize(QSizeF(32, 32));
    anchor.setPosition(QPointF(820, 820));
    const auto captures = qEnvironmentVariable("ECHO_POPUP_INPUT_CAPTURES");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1., 1.25}) {
            session.shell.resetZoom();
            if (scale > 1)
                for (int step = 0; step < 5; ++step)
                    session.shell.zoomIn();
            for (const auto &kind :
                 {"volume", "speed", "quality", "play-mode", "effect", "cast", "sleep", "more"}) {
                const auto owner = qstrcmp(kind, "effect") == 0  ? "playbackEffectPopupOwner"
                                   : qstrcmp(kind, "cast") == 0  ? "playbackCastPopupOwner"
                                   : qstrcmp(kind, "sleep") == 0 ? "sleepTimerPopupOwner"
                                   : qstrcmp(kind, "more") == 0  ? "playerbarMorePopupOwner"
                                                                 : "playbackOptionsPopupOwner";
                QVariantMap properties{{"anchorItem", QVariant::fromValue(&anchor)}};
                if (qstrcmp(kind, "effect") == 0)
                    properties.insert("tab", 2);
                if (qstrcmp(owner, "playbackOptionsPopupOwner") == 0)
                    properties.insert("kind", kind);
                auto popup = session.openOwnedPopup(owner, properties);
                QVERIFY2(popup, kind);
                QVERIFY(session.clickItem("titlebarMoreAction"));
                QPointer<QObject> more;
                QTRY_VERIFY((more = session.root->findChild<QObject *>("titlebarMorePopup")));
                QTRY_VERIFY(more->property("opened").toBool());
                QTRY_VERIFY(popup.isNull());
                if (!captures.isEmpty() && qstrcmp(kind, "volume") == 0) {
                    QVERIFY(session.settleScene());
                    QVERIFY(session.window->grabWindow().save(
                        captures +
                        QString("/production-switch-%1-%2.png").arg(int(mode)).arg(scale)));
                }
                QTest::keyClick(session.window, Qt::Key_Escape);
                QTRY_VERIFY(more.isNull());
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::productionQualityPopupContainsAndScrollsAllOptions()
{
    Session session;
    // 蝰蛇音质默认关闭：本测试校验完整音质列表，显式开启。
    session.preferences.setValue("playback/viperQualityEnabled", true);
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
                    // The option control copies its delegate model as a read-only
                    // property, so the delegate owner is what accepts an override.
                    for (const auto &quality : playback.qualities()) {
                        auto data = quality.toMap();
                        const auto key = data.value("value").toString();
                        data["detail"] =
                            key == "standard" ? "2.3 MB" : QStringLiteral("暂无该音质");
                        auto *owner =
                            visual(session.window->contentItem(), "playbackOptionRow-" + key);
                        QVERIFY(owner);
                        owner->setProperty("modelData", data);
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
                        const auto inkMessage = [&](const char *part, const QPair<int, int> &ink,
                                                    const QString &text) {
                            return qPrintable(QString("%1 has no ink for %2 (text=%3, ink=%4..%5)")
                                                  .arg(part)
                                                  .arg(key)
                                                  .arg(text)
                                                  .arg(ink.first)
                                                  .arg(ink.second));
                        };
                        QVERIFY2(
                            titleInk.second >= titleInk.first,
                            inkMessage("title", titleInk, titleText->property("text").toString()));
                        QVERIFY2(detailInk.second >= detailInk.first,
                                 inkMessage("detail", detailInk,
                                            detailText->property("text").toString()));
                        QVERIFY2(tagInk.second >= tagInk.first,
                                 inkMessage("tag", tagInk, tag->property("text").toString()));
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

void DesktopPagesTest::taskIndicatorUsesSingleDot_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<qreal>("scale");
    QTest::newRow("light-100") << false << qreal(1);
    QTest::newRow("dark-100") << true << qreal(1);
    QTest::newRow("light-125") << false << qreal(1.25);
    QTest::newRow("dark-125") << true << qreal(1.25);
}
void DesktopPagesTest::taskIndicatorUsesSingleDot()
{
    QFETCH(bool, dark);
    QFETCH(qreal, scale);
    Session session;
    session.theme.setMotionEnabled(false);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    QVERIFY(session.create());
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    // The offscreen platform screen clamps the ApplicationWindow to 800 wide,
    // which leaves the title bar toolbar without capacity; match the app's
    // real window size so the pinned toolbar actions exist. At 1.25 zoom the
    // zoom-scaled sidebar and window actions consume a 1150-wide window's
    // whole toolbar budget, so give the toolbar the extra room it needs.
    session.window->resize(scale > 1. ? 1400 : 1150, 750);
    QVERIFY(session.settleScene());
    for (int step = 0; step < qRound((scale - 1) / .05); ++step)
        session.shell.zoomIn();
    QVERIFY(session.settleScene());
    auto *button = visual(session.window->contentItem(), "titlebar-action-tasks");
    QVERIFY(button);
    auto *dot = visual(button, "taskIndicatorDot");
    auto *badge = visual(button, "actionBadge");
    QVERIFY(dot && badge);
    // The drag handle owns a dormant duplicate glyph. Wait for the actual
    // visible button image before accepting a rendered toolbar snapshot.
    QTRY_VERIFY(std::ranges::any_of(visualAll(button, "iconImage"), [](QQuickItem *item) {
        return item->isVisible() && item->property("status").toInt() == 1;
    }));
    QVERIFY(!dot->isVisible());
    QObject producer;
    TaskCenterModel::Registration entry;
    entry.id = "dot-check";
    entry.content.name = "插件更新";
    const auto handle = session.commands.tasks()->registerTask(producer, entry);
    QTRY_VERIFY(dot->isVisible());
    QVERIFY(!badge->isVisible());
    QCOMPARE(dot->width(), 5.);
    QCOMPARE(dot->height(), 5.);
    QCOMPARE(dot->x(), button->width() - 10.);
    QCOMPARE(dot->y(), 5.);
    const auto color = dot->property("color").value<QColor>();
    QVERIFY(color.red() > color.green() && color.red() > color.blue());
    const auto output = qEnvironmentVariable("ECHO_TASK_DOT_OUTPUT");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        QTest::qWait(100);
        QVERIFY(session.window->grabWindow().save(
            output + QString("/task-dot-%1-%2.png").arg(dark).arg(scale)));
    }
    QVERIFY(session.commands.titlebarActions()->setBadgeVisible("host/tasks", false));
    QTRY_VERIFY(!dot->isVisible());
    QVERIFY(session.commands.titlebarActions()->setBadgeVisible("host/tasks", true));
    QTRY_VERIFY(dot->isVisible());
    QVERIFY(session.commands.titlebarActions()->setPlacement("host/tasks", ShellActionModel::More));
    QVERIFY(session.clickItem("titlebarMoreAction"));
    QQuickItem *menuAction = nullptr;
    QTRY_VERIFY(menuAction =
                    visual(session.window->contentItem(), "titlebarMenuAction-host/tasks"));
    auto *menuDot = visual(menuAction, "taskIndicatorDot");
    QVERIFY(menuDot);
    QTRY_VERIFY(menuDot->isVisible());
    QCOMPARE(menuDot->width(), 5.);
    QVERIFY(session.commands.tasks()->remove(handle));
    QTRY_VERIFY(!menuDot->isVisible());
}

void DesktopPagesTest::productionVolumeGainSurfaces_data()
{
    QTest::addColumn<bool>("dark");
    QTest::addColumn<int>("zoom");
    QTest::newRow("light-100") << false << 0;
    QTest::newRow("dark-100") << true << 0;
    QTest::newRow("light-125") << false << 5;
    QTest::newRow("dark-125") << true << 5;
}

void DesktopPagesTest::productionVolumeGainSurfaces()
{
    QFETCH(bool, dark);
    QFETCH(int, zoom);
    Session session;
    session.theme.setMotionEnabled(false);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&] {
        session.auxiliary.reset();
        session.root.reset();
        session.window = nullptr;
        session.windows.shutdown();
    });
    for (int i = 0; i < zoom; ++i)
        session.shell.zoomIn();
    session.window->resize(1150, 800);
    QVERIFY(Test::prepareNativeInput(session.window));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    const auto captures = qEnvironmentVariable("ECHO_VOLUME_CAPTURE_DIR");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    const auto click = [](QQuickItem *item) {
        QVERIFY(item && item->window() && item->isVisible());
        QVERIFY(Test::settleNativeItem(item));
        const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
        Test::moveNativePointer(item->window(), point);
        QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier, point);
    };
    const auto visibleItem = [](QQuickItem *root, const QString &name) -> QQuickItem * {
        QList<QQuickItem *> pending{root};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast();
            if (item->objectName() == name && item->isVisible())
                return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    for (const auto &surface : {QString("playerbar"), QString("playback-page"), QString("mini")}) {
        QPointer<QObject> page;
        auto *host = session.window;
        QObject *owner = nullptr;
        if (surface == "playback-page") {
            page = session.openOwnedPopup("playbackPageOwner");
            QVERIFY(page);
        }
        if (surface == "mini") {
            session.windows.floating()->showMini();
            QTRY_VERIFY((host = session.windows.floating()->miniWindow()));
            QVERIFY(Test::prepareNativeInput(host));
            owner = host->findChild<QObject *>("miniPlayerVolumePopupOwner");
        } else {
            owner = session.root->findChild<QObject *>("playbackOptionsPopupOwner");
        }
        QVERIFY(owner);
        QVERIFY(!owner->property("item").value<QObject *>());
        QQuickItem *trigger = nullptr;
        QTRY_VERIFY((trigger = visibleItem(host->contentItem(), surface == "mini"
                                                                    ? "miniPlayerVolume"
                                                                    : "playerbar-action-volume")));
        playback.setVolume(125);
        QQuickItem *mark = nullptr;
        QTRY_VERIFY((mark = visual(trigger, "playbackVolumeBoostMark")));
        QTRY_VERIFY(mark->isVisible());
        QVERIFY(trigger->property("tooltipText").toString().contains(QStringLiteral("软件增益")));
        if (!captures.isEmpty()) {
            Test::FrameSpy frames(host);
            host->update();
            QTRY_VERIFY(!frames.isEmpty());
            QVERIFY(host->grabWindow().save(captures + QString("/%1-%2-%3-icon.png")
                                                           .arg(surface, dark ? "dark" : "light")
                                                           .arg(zoom ? 125 : 100)));
        }
        playback.setVolume(100);
        QTRY_VERIFY(!mark->isVisible());
        click(trigger);
        if (QTest::currentTestFailed())
            return;
        QPointer<QObject> popup;
        QTRY_VERIFY((popup = owner->property("item").value<QObject *>()));
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *content = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QPointer<QQuickItem> panel = visual(content, "playbackVolumePanel");
        QVERIFY(panel);
        auto *slider = visual(panel, "playbackVolumeSlider");
        auto *restore = visual(panel, "playbackVolumeRestore");
        auto *info = visual(panel, "playbackVolumeInfo");
        auto *mute = visual(panel, "playbackVolumeMute");
        QVERIFY(slider && restore && info && mute);
        QCOMPARE(slider->property("to").toReal(), 150.);
        QCOMPARE(slider->property("orientation").toInt(), int(Qt::Horizontal));
        auto *reference = visual(panel, "playbackVolumeReference");
        QVERIFY(reference);
        auto *referenceLine = reference->property("contentItem").value<QQuickItem *>();
        QVERIFY(referenceLine);
        auto *referenceText = referenceLine->property("textItem").value<QQuickItem *>();
        QVERIFY(referenceText);
        QTRY_VERIFY(
            qAbs(referenceText->mapToItem(panel, referenceText->boundingRect().center()).x() -
                 reference->mapToItem(panel, reference->boundingRect().center()).x()) < .51);
        const auto height = panel->implicitHeight();
        for (const int value : {0, 100, 101, 150}) {
            playback.setVolume(value);
            QTRY_COMPARE(slider->property("value").toReal(), qreal(value));
            QTRY_COMPARE(restore->isVisible(), value > 100);
            QTRY_COMPARE(mark->isVisible(), value > 100);
            QTRY_COMPARE(panel->implicitHeight(), height);
            auto *label = visual(panel, "playbackVolumeValue");
            QVERIFY(label);
            QCOMPARE(label->property("text").toString(), QString::number(value));
        }
        {
            // Present a remote output state without opening a provider connection.
            // Apply its volume before exposing the cast state, so no remote command
            // is submitted by the service's playback synchronization observer.
            playback.applyOutputVolume(100);
            EchoMusic::CastServiceTestAccess::setRemoteForVolumeUi(*playback.cast(), true);
            const auto localOutput = qScopeGuard([&] {
                EchoMusic::CastServiceTestAccess::setRemoteForVolumeUi(*playback.cast(), false);
                playback.restoreLocalVolume();
            });
            QTRY_COMPARE(slider->property("to").toReal(), 100.);
            QTRY_VERIFY(!reference->isVisible());
            QTRY_VERIFY(!restore->isVisible());
            QTRY_VERIFY(!mark->isVisible());
        }
        QTRY_COMPARE(slider->property("to").toReal(), 150.);
        playback.setVolume(100);
        slider->forceActiveFocus();
        QTest::keyClick(slider->window(), Qt::Key_Right);
        QTRY_COMPARE(playback.volume(), 101.);
        QTRY_VERIFY(mark->isVisible());
        playback.setVolume(125);
        const auto save = [&](const QString &suffix) {
            if (captures.isEmpty())
                return;
            auto *window = content->window();
            Test::FrameSpy frames(window);
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            QVERIFY(window->grabWindow().save(captures + QString("/%1-%2-%3-%4.png")
                                                             .arg(surface, dark ? "dark" : "light")
                                                             .arg(zoom ? 125 : 100)
                                                             .arg(suffix)));
        };
        save("gain");
        click(restore);
        QTRY_COMPARE(playback.volume(), 100.);
        QTRY_VERIFY(!mark->isVisible());
        QTRY_VERIFY(!restore->isVisible());
        save("standard");
        click(info);
        QTRY_VERIFY(visual(panel, "playbackVolumeExplanation"));
        QTRY_VERIFY(panel->implicitHeight() > height);
        const auto expandedHeight = panel->implicitHeight();
        for (int value : {100, 101, 150}) {
            playback.setVolume(value);
            QTRY_COMPARE(panel->implicitHeight(), expandedHeight);
        }
        playback.setVolume(125);
        save("explanation");
        if (surface == "mini")
            QTRY_VERIFY(content->property("contentHeight").toReal() <= content->height() + .01);
        click(mute);
        QTRY_COMPARE(playback.volume(), 0.);
        QTRY_VERIFY(!mark->isVisible());
        click(mute);
        QTRY_COMPARE(playback.volume(), 125.);
        QTRY_VERIFY(mark->isVisible());
        // Input dismissal is covered by the native Mini regression; retire
        // this shared-content matrix through the explicit owner lifecycle.
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(popup.isNull());
        QVERIFY(panel.isNull());
        if (page) {
            QVERIFY(QMetaObject::invokeMethod(page, "close"));
            QTRY_VERIFY(page.isNull());
        }
    }
    QVERIFY2(warnings.isEmpty(), "Production volume surfaces must load without QML warnings");
}
