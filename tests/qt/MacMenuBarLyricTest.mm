#include "MenuBarLyric.h"
#include "MenuBarLyricLayout.h"
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QProcess>
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
    // Initial compact probing is queued across AppKit's status-bar layout.
    if (item.diagnostics().value("layoutProbes").toULongLong() == 0)
        QTest::qWait(150);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
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
    [view layoutSubtreeIfNeeded];
    [view displayIfNeeded];
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
    void proxyOriginsCannotCollapseContentWidth()
    {
        using MenuBarLyricLayout::availableWidth;
        QCOMPARE(availableWidth(QRectF(0, 0, 1512, 982), 280), 280.);
        QCOMPARE(availableWidth(QRectF(-1920, 0, 1920, 1080), 800), 800.);
        QCOMPARE(availableWidth({}, 280), 280.);
        QCOMPARE(availableWidth(QRectF(0, 0, 600, 800), 800), 568.);
        QCOMPARE(availableWidth({}, 24), 128.);
    }
    void longLyricsRespectViewportWithAndWithoutScrolling()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        item.create([](auto) {}, [] { return 1000.; });
        auto state = timedState();
        state.content.primary = QString(160, QChar(0x56fd));
        state.content.secondary.clear();
        state.content.wordTimed = false;
        state.content.words.clear();
        MenuBarLyricStyle style;
        style.maxWidth = 800;
        style.colorMode = 2;
        style.primaryColor = QColor("#ee3030");
        item.update(state, style);
        auto *view = nativeView(item);
        QVERIFY(view);
        QTRY_VERIFY(item.diagnostics().value("layoutProbes").toULongLong() > 0);
        QTest::qWait(700);
        const auto width = item.diagnostics().value("width").toDouble();
        qInfo() << "Native lyric width/budget" << width
                << item.diagnostics().value("availableWidth");
        QVERIFY(width <= item.diagnostics().value("availableWidth").toDouble());
        QVERIFY2(width >= 128., "A proxy status host must not force a 24-point lyric fragment");
        QVERIFY(!item.diagnostics().value("timerActive").toBool());
        auto pixels = hostSnapshot(view);
        QVERIFY(!pixels.isNull());
        int ink = 0;
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 0; x < pixels.width(); ++x)
                ink += pixels.pixelColor(x, y).alpha() > 40;
        QVERIFY2(ink > 10, "A constrained non-scrolling lyric must retain visible text/ellipsis");
        const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(pixels.save(artifacts + "/constrained-static.png"));
        }
        style.scrolling = true;
        item.update(state, style);
        QTRY_VERIFY(item.diagnostics().value("timerActive").toBool());
        QCOMPARE(item.diagnostics().value("width").toDouble(), width);
        // AppKit can finish adopting the scrolling surface's frame/backing
        // after update(). Measure steady-state reuse after that adoption.
        QTest::qWait(250);
        const auto builds = item.diagnostics().value("textBuilds");
        const auto scrollingProbes = item.diagnostics().value("layoutProbes");
        const auto compactions = item.diagnostics().value("layoutCompactions");
        const auto requests = item.diagnostics().value("displayRequests").toULongLong();
        QTest::qWait(1800);
        QCOMPARE(item.diagnostics().value("layoutCompactions"), compactions);
        QVERIFY(item.diagnostics().value("displayRequests").toULongLong() > requests);
        // Other applications can resize status items during this real-bar test
        // (for example network-speed digits). Only unchanged geometry promises
        // reuse of the exact same text surface.
        if (item.diagnostics().value("layoutProbes") == scrollingProbes)
            QCOMPARE(item.diagnostics().value("textBuilds"), builds);
        if (!artifacts.isEmpty())
            QVERIFY(hostSnapshot(view).save(artifacts + "/constrained-scrolling.png"));
        const auto probes = item.diagnostics().value("layoutProbes").toULongLong();
        [[NSNotificationCenter defaultCenter]
            postNotificationName:NSApplicationDidChangeScreenParametersNotification
                          object:NSApp];
        QTRY_VERIFY(item.diagnostics().value("layoutProbes").toULongLong() > probes);
        QTest::qWait(350);
        QVERIFY(item.diagnostics().value("width").toDouble() <=
                item.diagnostics().value("availableWidth").toDouble());
        item.reset();
        QCoreApplication::processEvents();
    }
    void iconLyricsAndPlainHoverControlsShareOneItem()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        QList<int> commands;
        item.create([&](auto action) { commands << int(action); }, [] { return 0.; });
        auto state = timedState();
        state.icon = QImage(QFINDTESTDATA("../../modules/EchoMusic/Shell/assets/mac-tray.png"));
        QVERIFY(!state.icon.isNull());
        state.content.primary = "让青春吹动了你的长发，让它牵引你的梦，走过很长很长的旅途";
        state.content.secondary.clear();
        state.content.wordTimed = false;
        state.content.words.clear();
        MenuBarLyricStyle style;
        style.maxWidth = 280;
        item.update(state, style);
        auto *view = nativeView(item);
        QVERIFY(view);
        view.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        QTest::qWait(250);
        QCOMPARE(item.diagnostics().value("width").toDouble(), 280.);
        QVERIFY(item.diagnostics().value("iconVisible").toBool());
        const auto text = hostSnapshot(view);
        QVERIFY(!text.isNull());
        const auto scale = double(text.width()) / view.bounds.size.width;
        const QRect iconRect(int((view.bounds.size.width - 24) * scale), 0, int(24 * scale),
                             text.height());
        const auto iconPixels = text.copy(iconRect);
        int ink = 0;
        for (int y = 0; y < iconPixels.height(); ++y)
            for (int x = 0; x < iconPixels.width(); ++x)
                ink += iconPixels.pixelColor(x, y).alpha() > 40;
        QVERIFY(ink > 20);
        [view mouseEntered:[NSEvent new]];
        const auto controls = hostSnapshot(view);
        QCOMPARE(controls.copy(iconRect), iconPixels);
        // Between the top edge and the glyphs there is no custom hover surface.
        QCOMPARE(controls.pixelColor(controls.width() / 2, int(3 * scale)).alpha(), 0);
        NSButton *tray = nil;
        for (NSView *child in view.subviews)
            if ([child isKindOfClass:NSButton.class] &&
                [child.accessibilityLabel isEqualToString:@"EchoMusicNext"])
                tray = (NSButton *)child;
        QVERIFY(tray);
        [tray performClick:nil];
        QCOMPARE(commands.last(), int(MenuBarLyric::Action::TrayActivate));
        const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(text.save(artifacts + "/unified-lyrics.png"));
            QVERIFY(controls.save(artifacts + "/unified-controls.png"));
        }
        [view mouseExited:[NSEvent new]];
        if (const auto hold = qEnvironmentVariableIntValue("ECHO_MENU_BAR_INSPECT_MS"); hold > 0)
            QTest::qWait(std::min(hold, 60000));
        state.lyricsVisible = false;
        item.update(state, style);
        QCOMPARE(item.diagnostics().value("width").toDouble(), 24.);
        QVERIFY(item.diagnostics().value("iconVisible").toBool());
        QCOMPARE(item.diagnostics().value("bitmapBytes").toLongLong(), 0);
        QVERIFY(!item.diagnostics().value("timerActive").toBool());
        state.lyricsVisible = true;
        item.update(state, style);
        QCOMPARE(item.diagnostics().value("width").toDouble(), 280.);
        state.visible = false;
        item.update(state, style);
        QVERIFY(!item.diagnostics().value("iconVisible").toBool());
        item.reset();
        QVERIFY(!item.diagnostics().value("created").toBool());
    }
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
        for (const auto height : {NSStatusBar.systemStatusBar.thickness}) {
            style.displayMode = 3;
            item.update(state, style);
            auto *view = nativeView(item);
            QVERIFY(view);
            if (view.bounds.size.width < 128)
                QSKIP("This pixel fixture needs 128 points of physical menu-bar space");
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
                    << pixels.height() << "view/layer" << view.bounds.size.height
                    << view.layer.bounds.size.height;
            const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
            if (!artifacts.isEmpty())
                QVERIFY(pixels.save(artifacts + QString("/original-centered-%1.png").arg(height)));
            QVERIFY(qAbs(center - pixels.height() / 2.) <= 1.);
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
            if (view.bounds.size.width < 128)
                QSKIP("This pixel fixture needs 128 points of physical menu-bar space");
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
    void systemSubtitleUsesStatusForegroundThroughVibrantAppearanceChanges()
    {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Native AppKit display required");
        MenuBarLyric item;
        item.create([](auto) {}, [] { return 0.; });
        auto state = timedState();
        state.advancing = state.playing = false;
        state.content.primary = "请你，我的爱，接受幸福吧 Agjp";
        state.content.secondary.clear();
        state.content.words.clear();
        state.content.wordTimed = false;
        state.content.primaryIsSecondary = true;
        MenuBarLyricStyle style;
        style.displayMode = 1;
        style.maxWidth = 500;
        style.colorMode = 2;
        style.primaryColor = Qt::red;
        item.update(state, style);
        auto *view = nativeView(item);
        QVERIFY(view);
        const auto densestPixel = [](const QImage &pixels) {
            QColor result(Qt::transparent);
            for (int y = 0; y < pixels.height(); ++y)
                for (int x = 0; x < pixels.width(); ++x)
                    if (const auto color = pixels.pixelColor(x, y); color.alpha() > result.alpha())
                        result = color;
            return result;
        };
        for (NSString *name in @[
                 NSAppearanceNameAqua, NSAppearanceNameVibrantLight, NSAppearanceNameDarkAqua,
                 NSAppearanceNameVibrantDark
             ]) {
            view.appearance = [NSAppearance appearanceNamed:name];
            // Change only the native appearance, including Aqua -> Vibrant
            // within the same light/dark family. The cached surface must update.
            QTest::qWait(50);
            __block NSColor *expected;
            [view.effectiveAppearance performAsCurrentDrawingAppearance:^{
              auto foreground =
                  [NSColor.labelColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
              expected = [foreground colorWithAlphaComponent:foreground.alphaComponent * .8];
            }];
            const auto pixels = hostSnapshot(view);
            const auto ink = densestPixel(pixels);
            QVERIFY(ink.alpha() > 100);
            QVERIFY(qAbs(ink.alpha() - qRound(expected.alphaComponent * 255)) <= 2);
            QVERIFY(qAbs(ink.redF() - expected.redComponent) < .02);
            QVERIFY(qAbs(ink.greenF() - expected.greenComponent) < .02);
            QVERIFY(qAbs(ink.blueF() - expected.blueComponent) < .02);
            if (name == NSAppearanceNameVibrantDark) {
                // A vibrant material's opaque gray is not a valid flat subtitle.
                QVERIFY(ink.red() > 245 && ink.alpha() < 230);
                const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
                if (!artifacts.isEmpty()) {
                    QImage onBlue(pixels.size(), QImage::Format_ARGB32_Premultiplied);
                    onBlue.fill(QColor("#287eaf"));
                    QPainter painter(&onBlue);
                    painter.drawImage(0, 0, pixels);
                    painter.end();
                    QVERIFY(onBlue.save(artifacts + "/system-subtitle-on-blue.png"));
                }
            }
        }
        style.secondaryColorMode = 2;
        style.secondaryColor = QColor(30, 180, 110, 128);
        item.update(state, style);
        // Check explicit sRGB values in an sRGB context, rather than the host
        // bitmap's display ICC space. Native appearance pixels were checked above.
        auto ink = densestPixel(capture(view));
        QVERIFY(qAbs(ink.alpha() - 128) <= 2);
        QVERIFY(qAbs(ink.green() - 180) <= 2);
        style.secondaryColorMode = 1;
        state.coverColor = QColor("#28cc88");
        item.update(state, style);
        ink = densestPixel(capture(view));
        QCOMPARE(ink.alpha(), 255);
        QCOMPARE(ink.rgb(), state.coverColor.rgb());
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
        for (const double height : {NSStatusBar.systemStatusBar.thickness}) {
            for (const int mode : {3, 2}) {
                style.displayMode = mode;
                item.update(state, style);
                auto *view = nativeView(item);
                QVERIFY(view);
                if (view.bounds.size.width < 128)
                    QSKIP("This pixel fixture needs 128 points of physical menu-bar space");
                const auto pixels = hostSnapshot(view);
                QRect all;
                int bands = 0, translated = 0;
                QList<QRect> inkBands;
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
                    if (row && !previous) {
                        ++bands;
                        inkBands.append(QRect(0, y, 1, 1));
                    } else if (row) {
                        inkBands.last().setBottom(y);
                    }
                    previous = row;
                }
                QVERIFY2(bands >= 2, "Ruby annotation must form visible ink above the original");
                QVERIFY2(inkBands.first().height() >= inkBands.last().height() * .6,
                         "Actual annotation ink must remain readable beside the original");
                QVERIFY(all.top() > 0 && all.bottom() < pixels.height() - 1);
                QVERIFY(qAbs((all.top() + all.bottom() + 1.) / 2 - pixels.height() / 2.) <= 1.5);
                QCOMPARE(translated, 0);
                QCOMPARE(item.diagnostics().value("secondaryFontSize").toDouble(), 0.);
                QCOMPARE(item.diagnostics().value("rubyGroups").toInt(), 2);
                QVERIFY(item.diagnostics().value("rubyFontSize").toDouble() >= 7);
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
        // A long reading must gain horizontal space, never shrink back into
        // the old tiny base-word box. The status bar still has only mask/scroll
        // updates, with no word transforms or implicit CoreAnimation effects.
        state.content.ruby[0] =
            QVariantMap{{"text", QString::fromUtf8("你好")}, {"ruby", "sayonaramata"}};
        style.wordByWord = false;
        style.maxWidth = 800;
        item.update(state, style);
        auto *view = nativeView(item);
        [view setFrameSize:NSMakeSize(view.frame.size.width, 24)];
        const auto longReading = hostSnapshot(view);
        QVERIFY(item.diagnostics().value("rubyFontSize").toDouble() >= 7);
        const auto artifacts = qEnvironmentVariable("ECHO_MENU_BAR_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(longReading.save(artifacts + "/ruby-long-readable.png"));
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
        if (view.bounds.size.width < 128)
            QSKIP("This pixel fixture needs 128 points of physical menu-bar space");
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
            if ([child isKindOfClass:NSButton.class] &&
                ![child.accessibilityLabel isEqualToString:@"EchoMusicNext"])
                buttons << (NSButton *)child;
        QCOMPARE(buttons.size(), 5);
        const bool compact = view.bounds.size.width < 128;
        for (int i = 0; i < 5; ++i)
            QCOMPARE(bool(buttons[i].hidden), compact && i != 2);
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
        if (compact) {
            auto *menu = [view menuForEvent:[NSEvent new]];
            QCOMPARE(menu.numberOfItems, 9); // Five controls, separator, three shell actions.
            for (int i = 0; i < 5; ++i) {
                auto *entry = [menu itemAtIndex:i];
                QCOMPARE(entry.tag, i);
                QCOMPARE(bool(entry.enabled), i != 4);
                QVERIFY(entry.title.length > 0);
            }
        }
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
                if (buttons[i].hidden)
                    continue;
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
