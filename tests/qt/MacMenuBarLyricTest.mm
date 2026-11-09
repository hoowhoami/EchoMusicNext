#include "MenuBarLyric.h"
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <ctime>
#include <cmath>
#include <QtTest>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
using namespace EchoMusic;
namespace
{
NSView *findView(NSView *root)
{
    if ([NSStringFromClass(root.class) isEqualToString:@"EchoMenuBarLyricView"])
        return root;
    for (NSView *child in root.subviews)
        if (auto *found = findView(child))
            return found;
    return nil;
}
NSView *nativeView(const MenuBarLyric &item)
{
    return findView(
        [NSApp windowWithWindowNumber:item.diagnostics().value("windowNumber").toLongLong()]
            .contentView);
}
MenuBarLyricState timedState()
{
    MenuBarLyricState state;
    state.title = "Menu lyric native probe";
    state.hasTrack = state.playing = state.advancing = state.favoriteEnabled = true;
    state.content.primary = "你好，世界 Agjp";
    state.content.secondary = "Hello, world";
    state.content.wordTimed = true;
    state.content.words = {
        QVariantMap{{"text", "你好，"}, {"start_ms", 0}, {"duration_ms", 2000}},
        QVariantMap{{"text", "世界 Agjp"}, {"start_ms", 2000}, {"duration_ms", 2000}}};
    return state;
}
QImage capture(NSView *view)
{
    if (!view)
        return {};
    [view.window displayIfNeeded];
    const auto width = int(std::ceil(view.bounds.size.width * 2));
    const auto height = int(std::ceil(view.bounds.size.height * 2));
    QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto context = CGBitmapContextCreate(image.bits(), width, height, 8, image.bytesPerLine(),
                                         space, kCGImageAlphaPremultipliedLast);
    CGContextScaleCTM(context, 2, 2);
    [view.layer renderInContext:context];
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    return image;
}
QImage hostSnapshot(NSView *view)
{
    auto *bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
    auto *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return QImage::fromData(static_cast<const uchar *>(png.bytes), png.length);
}
} // namespace
class MacMenuBarLyricTest : public QObject
{
    Q_OBJECT
private slots:
    void originalModeCentersSingleLineEvenAfterBilingual()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        item.create([](auto) {}, [] { return 0.; });
        auto state = timedState();
        state.content.wordTimed = false;
        state.content.words.clear();
        state.advancing = false;
        MenuBarLyricStyle style;
        style.colorMode = 2;
        style.secondaryColorMode = 2;
        style.primaryColor = QColor("#ee3030");
        style.secondaryColor = QColor("#3060ee");
        for (const auto height : {22., 24., 37.}) {
            style.displayMode = 3;
            item.update(state, style);
            auto *view = nativeView(item);
            QVERIFY(view);
            [view setFrameSize:NSMakeSize(view.frame.size.width, height)];
            style.displayMode = 0;
            item.update(state, style);
            const auto pixels = hostSnapshot(view);
            QRect ink;
            int translated = 0;
            for (int y = 0; y < pixels.height(); ++y)
                for (int x = 0; x < pixels.width(); ++x) {
                    const auto color = pixels.pixelColor(x, y);
                    if (color.alpha() < 40)
                        continue;
                    if (color.red() > 150 && color.green() < 100 && color.blue() < 100)
                        ink |= QRect(x, y, 1, 1);
                    if (color.blue() > 150 && color.red() < 100)
                        ++translated;
                }
            QVERIFY(!ink.isEmpty());
            QCOMPARE(translated, 0);
            const auto center = (ink.top() + ink.bottom() + 1.) / 2;
            qInfo() << "Original row ink center" << height << center << "viewport"
                    << pixels.height();
            QVERIFY(qAbs(center - pixels.height() / 2.) <= 1.);
            const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
            if (!artifacts.isEmpty())
                QVERIFY(pixels.save(artifacts + QString("/original-centered-%1.png").arg(height)));
        }
    }
    void secondaryInkIsIndependentInEveryDisplayMode()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        item.create([](auto) {}, [] { return 0.; });
        auto state = timedState();
        state.advancing = false;
        state.content.words.clear();
        state.content.wordTimed = false;
        MenuBarLyricStyle style;
        style.colorMode = style.secondaryColorMode = 2;
        style.primaryColor = QColor("#ee3030");
        style.secondaryColor = QColor("#3060ee");
        style.wordByWord = false;
        for (const int mode : {3, 2, 1}) {
            style.displayMode = mode;
            state.content.primary = mode == 1   ? QString::fromUtf8("译文")
                                    : mode == 2 ? QString::fromUtf8("原文 · 译文")
                                                : QString::fromUtf8("原文");
            state.content.secondary = mode == 1 ? QString() : QString::fromUtf8("译文");
            state.content.primaryIsSecondary = mode == 1;
            item.update(state, style);
            auto *view = nativeView(item);
            QVERIFY(view);
            const auto pixels = hostSnapshot(view);
            QRect primary, secondary;
            for (int y = 0; y < pixels.height(); ++y)
                for (int x = 0; x < pixels.width(); ++x) {
                    const auto c = pixels.pixelColor(x, y);
                    if (c.alpha() < 80)
                        continue;
                    if (c.red() > 150 && c.blue() < 100)
                        primary |= QRect(x, y, 1, 1);
                    if (c.blue() > 150 && c.red() < 100)
                        secondary |= QRect(x, y, 1, 1);
                }
            QVERIFY(!secondary.isEmpty());
            if (mode == 1)
                QVERIFY(primary.isEmpty());
            else {
                QVERIFY(!primary.isEmpty());
                QVERIFY(secondary.height() < primary.height());
                const auto mainSize = item.diagnostics().value("primaryFontSize").toDouble();
                QVERIFY(qAbs(item.diagnostics().value("secondaryFontSize").toDouble() -
                             mainSize * .8) < .01);
                if (mode == 3) {
                    const auto gap = secondary.top() - primary.bottom() - 1;
                    qInfo() << "Bilingual ink gap in backing pixels" << gap;
                    QVERIFY(gap >= 3);
                    const auto ink = primary.united(secondary);
                    QVERIFY(qAbs((ink.top() + ink.bottom() + 1.) / 2 - pixels.height() / 2.) <= 1.);
                } else {
                    QVERIFY(primary.right() < secondary.left());
                    QVERIFY(qAbs(primary.bottom() - secondary.bottom()) <= 1);
                }
            }
            const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
            if (!artifacts.isEmpty())
                QVERIFY(pixels.save(artifacts + QString("/secondary-mode-%1.png").arg(mode)));
        }
    }
    void rubyFitsBarAndRetainsCachedWordProgress()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        double now = 1200;
        MenuBarLyric item;
        item.create([](auto) {}, [&] { return now; });
        auto state = timedState();
        state.advancing = false;
        state.content.primary = QString::fromUtf8("你好世界");
        state.content.secondary.clear();
        state.content.words = {
            QVariantMap{
                {"text", QString::fromUtf8("你好")}, {"start_ms", 0}, {"duration_ms", 2000}},
            QVariantMap{
                {"text", QString::fromUtf8("世界")}, {"start_ms", 2000}, {"duration_ms", 2000}}};
        state.content.ruby = {
            QVariantMap{{"text", QString::fromUtf8("你好")}, {"ruby", "ni hao"}},
            QVariantMap{{"text", QString::fromUtf8("世界")}, {"ruby", "shi jie"}}};
        MenuBarLyricStyle style;
        style.colorMode = style.secondaryColorMode = 2;
        style.primaryColor = QColor("#ee3030");
        style.secondaryColor = QColor("#3060ee");
        style.wordByWord = false;
        for (const double height : {22., 24., 37.}) {
            for (const int mode : {3, 2}) {
                style.displayMode = mode;
                item.update(state, style);
                auto *view = nativeView(item);
                QVERIFY(view);
                [view setFrameSize:NSMakeSize(view.frame.size.width, height)];
                const auto pixels = hostSnapshot(view);
                QRect all;
                int bands = 0, translated = 0;
                bool previous = false;
                for (int y = 0; y < pixels.height(); ++y) {
                    bool row = false;
                    for (int x = 0; x < pixels.width(); ++x) {
                        const auto c = pixels.pixelColor(x, y);
                        if (c.alpha() < 40)
                            continue;
                        if (c.red() > 150 && c.blue() < 100)
                            all |= QRect(x, y, 1, 1);
                        if (c.blue() > 150 && c.red() < 100)
                            ++translated;
                        if (c.red() > 150 && c.blue() < 100)
                            row = true;
                    }
                    if (row && !previous)
                        ++bands;
                    previous = row;
                }
                QVERIFY2(bands >= 2, "Ruby annotation must form visible ink above the original");
                QVERIFY(all.top() > 0 && all.bottom() < pixels.height() - 1);
                QVERIFY(qAbs((all.top() + all.bottom() + 1.) / 2 - pixels.height() / 2.) <= 1.5);
                QCOMPARE(translated, 0);
                QCOMPARE(item.diagnostics().value("secondaryFontSize").toDouble(), 0.);
                QCOMPARE(item.diagnostics().value("rubyGroups").toInt(), 2);
                const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
                if (!artifacts.isEmpty())
                    QVERIFY(
                        pixels.save(artifacts + QString("/ruby-%1-%2.png").arg(mode).arg(height)));
            }
        }
        style.displayMode = 3;
        style.wordByWord = true;
        state.content.primary = QString::fromUtf8("你好世界");
        item.update(state, style);
        const auto builds = item.diagnostics().value("textBuilds").toULongLong();
        const auto cutoff = item.diagnostics().value("cutoff").toDouble();
        now += 1000;
        item.update(state, style);
        QCOMPARE(item.diagnostics().value("textBuilds").toULongLong(), builds);
        QVERIFY(item.diagnostics().value("cutoff").toDouble() > cutoff);
    }
    void wordProgressSchedulesHostDisplayAndAdvancesThroughAllGlyphs()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        double now = 1200;
        MenuBarLyric item;
        item.create([](auto) {}, [&] { return now; });
        auto state = timedState();
        state.advancing = false;
        state.content.primary = "甲乙丙丁";
        state.content.secondary.clear();
        state.content.offsetMs = 200;
        state.content.words.clear();
        for (int i = 0; i < 4; ++i)
            state.content.words << QVariantMap{{"text", state.content.primary.mid(i, 1)},
                                               {"start_ms", 1000 + i * 1000},
                                               {"duration_ms", 1000}};
        MenuBarLyricStyle style;
        style.displayMode = 0;
        style.colorMode = 2;
        style.secondaryColorMode = 2;
        style.primaryColor = QColor("#ee3030");
        item.update(state, style);
        auto *view = nativeView(item);
        QVERIFY(view);
        QCoreApplication::processEvents();
        [view displayIfNeeded];
        const auto builds = item.diagnostics().value("textBuilds").toULongLong();
        auto requests = item.diagnostics().value("displayRequests").toULongLong();
        qsizetype previousInk = 0;
        QImage previous;
        QImage finished;
        for (int word = 0; word < 4; ++word) {
            now = 2200 + word * 1000;
            item.update(state, style);
            QVERIFY2(item.diagnostics().value("displayRequests").toULongLong() > requests,
                     "Changed mask must request a hosted NSView display");
            requests = item.diagnostics().value("displayRequests").toULongLong();
            const auto pixels = hostSnapshot(view);
            if (!previous.isNull())
                for (int y = 0; y < pixels.height(); ++y)
                    for (int x = 0; x < pixels.width(); ++x)
                        if (previous.pixelColor(x, y).alpha() > 200)
                            QVERIFY2(pixels.pixelColor(x, y).alpha() > 200,
                                     "Already sung glyph pixels must not disappear");
            qsizetype sungInk = 0;
            QRect sungBounds;
            for (int y = 0; y < pixels.height(); ++y)
                for (int x = 0; x < pixels.width(); ++x) {
                    const auto color = pixels.pixelColor(x, y);
                    if (color.alpha() > 200 && color.red() > 150 && color.green() < 100) {
                        ++sungInk;
                        sungBounds |= QRect(x, y, 1, 1);
                    }
                }
            qInfo() << "Sung glyph pixels" << word + 1 << sungInk << sungBounds;
            QVERIFY(sungInk > previousInk + 20);
            previousInk = sungInk;
            previous = pixels;
            finished = pixels;
            QCOMPARE(item.diagnostics().value("textBuilds").toULongLong(), builds);
            const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
            if (!artifacts.isEmpty()) {
                QVERIFY(pixels.save(artifacts + QString("/word-progress-%1.png").arg(word + 1)));
                QImage opaque(pixels.size(), QImage::Format_RGB32);
                opaque.fill(Qt::black);
                {
                    QPainter painter(&opaque);
                    painter.drawImage(0, 0, pixels);
                }
                QVERIFY(opaque.save(artifacts +
                                    QString("/word-progress-%1-preview.png").arg(word + 1)));
            }
            [view displayIfNeeded];
        }
        style.wordByWord = false;
        item.update(state, style);
        const auto solid = hostSnapshot(view);
        for (int y = 0; y < solid.height(); ++y)
            for (int x = 0; x < solid.width(); ++x)
                if (solid.pixelColor(x, y).alpha() > 200)
                    QVERIFY2(finished.pixelColor(x, y).alpha() > 200,
                             "Completed highlight must include every whole-line glyph pixel");
    }
    void systemHostSnapshotContainsActualLyrics()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        item.create([](auto) {}, [] { return 0.; });
        auto state = timedState();
        state.content.wordTimed = false;
        state.content.words.clear();
        state.advancing = false;
        MenuBarLyricStyle style;
        style.colorMode = 2;
        style.secondaryColorMode = 2;
        style.displayMode = 3;
        style.primaryColor = QColor("#ee3030");
        style.secondaryColor = QColor("#3060ee");
        item.update(state, style);
        QCoreApplication::processEvents();
        auto *view = nativeView(item);
        QVERIFY(view);
        auto *bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        QVERIFY(bitmap);
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        auto *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        const auto pixels = QImage::fromData(static_cast<const uchar *>(png.bytes), png.length);
        QVERIFY(!pixels.isNull());
        int original = 0, translation = 0;
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 0; x < pixels.width(); ++x) {
                const auto color = pixels.pixelColor(x, y);
                if (color.alpha() < 40)
                    continue;
                if (color.red() > 150 && color.green() < 100 && color.blue() < 100)
                    ++original;
                if (color.blue() > 150 && color.red() < 100)
                    ++translation;
            }
        const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(pixels.save(artifacts + "/system-host-snapshot.png"));
        qInfo() << "AppKit view snapshot ink:" << original << translation;
        QVERIFY(original > 20);
        QVERIFY(translation > 20);
    }
    void cachedTextRendersBilingualAndProgressWithoutRasterizingEachTick()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        QElapsedTimer clock;
        clock.start();
        item.create([](auto) {}, [&] { return double(clock.elapsed()); });
        auto state = timedState();
        MenuBarLyricStyle style;
        style.displayMode = 3;
        item.update(state, style);
        QTRY_VERIFY(item.diagnostics().value("timerActive").toBool());
        QVERIFY(item.diagnostics().value("bitmapBytes").toInt() > 0);
        // AppKit delivers the initial backing/appearance notifications after
        // registration; measure cached progression after those settle.
        QTest::qWait(250);
        const auto builds = item.diagnostics().value("textBuilds").toULongLong();
        const auto start = item.diagnostics().value("cutoff").toDouble();
        QTest::qWait(250);
        QVERIFY(item.diagnostics().value("cutoff").toDouble() > start);
        QCOMPARE(item.diagnostics().value("textBuilds").toULongLong(), builds);
        auto *view = nativeView(item);
        QVERIFY(view);
        auto image = capture(view);
        QVERIFY(!image.isNull());
        int upper = 0, lower = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).alpha() > 20)
                    (y < image.height() / 2 ? upper : lower)++;
        QVERIFY(upper > 20 && lower > 20);
        const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
        for (const auto name : {NSAppearanceNameAqua, NSAppearanceNameDarkAqua}) {
            view.appearance = [NSAppearance appearanceNamed:name];
            QCoreApplication::processEvents();
            image = capture(view);
            if (!artifacts.isEmpty()) {
                QVERIFY(QDir().mkpath(artifacts));
                QVERIFY(
                    image.save(artifacts + (name == NSAppearanceNameAqua ? "/native-light.png"
                                                                         : "/native-dark.png")));
            }
        }
        const auto cpuStart = std::clock();
        QElapsedTimer wall;
        wall.start();
        QTest::qWait(1000);
        qInfo() << "Native menu bar lyric steady-state:" << wall.elapsed() << "ms wall,"
                << 1000. * (std::clock() - cpuStart) / CLOCKS_PER_SEC << "ms process CPU,"
                << item.diagnostics().value("bitmapBytes").toLongLong() << "cached bitmap bytes";
        state.advancing = state.playing = false;
        item.update(state, style);
        QVERIFY(!item.diagnostics().value("timerActive").toBool());
        const auto paused = item.diagnostics().value("ticks");
        QTest::qWait(100);
        QCOMPARE(item.diagnostics().value("ticks"), paused);
        // Plain LRC is solid; no timer or estimated word progress.
        state.content.wordTimed = false;
        state.content.words.clear();
        state.playing = state.advancing = true;
        item.update(state, style);
        QVERIFY(!item.diagnostics().value("timerActive").toBool());
    }
    void hoverButtonsDispatchNativeTrackingAndAccessibleActions()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        QList<int> commands;
        item.create([&](auto action) { commands << int(action); }, [] { return 1000.; });
        auto state = timedState();
        item.update(state, {});
        QCoreApplication::processEvents();
        auto *view = nativeView(item);
        QVERIFY(view);
        [view mouseEntered:[NSEvent new]];
        QVERIFY(item.diagnostics().value("hovered").toBool());
        QVERIFY(!item.diagnostics().value("timerActive").toBool());
        QList<NSButton *> buttons;
        for (NSView *child in view.subviews)
            if ([child isKindOfClass:NSButton.class] && !child.hidden)
                buttons << (NSButton *)child;
        QCOMPARE(buttons.size(), 5);
        for (int i = 0; i < 5; ++i) {
            auto *button = buttons[i];
            QVERIFY(button.accessibilityLabel.length > 0);
            [button performClick:nil];
            QCOMPARE(commands.last(), i);
        }
        const auto before = commands.size();
        auto *play = buttons[2];
        const auto point = [view convertPoint:NSMakePoint(NSMidX(play.frame), NSMidY(play.frame))
                                       toView:nil];
        for (const auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
            auto *event = [NSEvent mouseEventWithType:type
                                             location:point
                                        modifierFlags:0
                                            timestamp:NSProcessInfo.processInfo.systemUptime
                                         windowNumber:view.window.windowNumber
                                              context:nil
                                          eventNumber:1
                                           clickCount:1
                                             pressure:1];
            // NSStatusItem may live in a system-hosted proxy window with no
            // sendEvent routing. Exercise the native view's actual handlers.
            if (type == NSEventTypeLeftMouseDown)
                [view mouseDown:event];
            else
                [view mouseUp:event];
        }
        QCOMPARE(commands.size(), before + 1);
        QCOMPARE(commands.last(), int(MenuBarLyric::Action::Toggle));
        state.favoriteEnabled = false;
        item.update(state, {});
        QVERIFY(!buttons.last().enabled);
        [buttons.last() performClick:nil];
        QCOMPARE(commands.size(), before + 1);
        [view mouseExited:[NSEvent new]];
        QVERIFY(!item.diagnostics().value("hovered").toBool());
        const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            [view mouseEntered:[NSEvent new]];
            view.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
            QCoreApplication::processEvents();
            const auto pixels = capture(view);
            QVERIFY(pixels.save(artifacts + "/native-controls.png"));
            QVERIFY(pixels.pixelColor(pixels.width() / 2, 4).alpha() < 40);
            for (int i = 0; i < 5; ++i) {
                int ink = 0;
                const auto cell = buttons[i].frame;
                for (int y = 4; y < pixels.height() - 4; ++y)
                    for (int x = int(NSMinX(cell) * 2); x < int(NSMaxX(cell) * 2); ++x) {
                        const auto color = pixels.pixelColor(x, y);
                        if (color.alpha() > 60 && color.red() > 150 && color.green() > 150 &&
                            color.blue() > 150)
                            ++ink;
                    }
                QVERIFY2(ink > 4, qPrintable(QString("Missing native control glyph %1").arg(i)));
            }
        }
    }
    void hiddenAndRepeatedDisableReleaseNativeResources()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        for (int i = 0; i < 12; ++i) {
            __weak NSView *retired;
            @autoreleasepool {
                item.create([](auto) {}, [] { return 1500.; });
                auto state = timedState();
                MenuBarLyricStyle style;
                style.scrolling = true;
                style.maxWidth = 128;
                state.content.primary = QString(120, QChar(0x56fd));
                item.update(state, style);
                QVERIFY(item.diagnostics().value("bitmapBytes").toInt() > 0);
                QVERIFY(item.diagnostics().value("bitmapBytes").toInt() < 2 * 1024 * 1024);
                retired = nativeView(item);
                QVERIFY(retired);
                state.visible = false;
                item.update(state, style);
                QVERIFY(!item.diagnostics().value("timerActive").toBool());
                QCOMPARE(item.diagnostics().value("bitmapBytes").toInt(), 0);
                state.visible = true;
                item.update(state, style);
                QVERIFY(item.diagnostics().value("bitmapBytes").toInt() > 0);
                item.reset();
                QVERIFY(!item.diagnostics().value("created").toBool());
                QCoreApplication::processEvents();
            }
            QCoreApplication::processEvents();
            QVERIFY(!retired);
        }
    }
};
QTEST_MAIN(MacMenuBarLyricTest)
#include "MacMenuBarLyricTest.moc"
