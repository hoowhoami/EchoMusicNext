#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"

void ControlsUiTest::badgeIconAndCountUseSameInkCenter()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
Window {
    id: window
    visible: true; width: 420; height: 360
    property real sampleScale: 1
    Rectangle { anchors.fill: parent; color: Theme.main }
    Column {
        x: 24; y: 24; spacing: 12
        scale: window.sampleScale; transformOrigin: Item.TopLeft
        Repeater {
            model: ["4344", "9", "1.2w", "中文123", "Artist"]
            delegate: EchoBadge {
                required property int index
                required property string modelData
                objectName: "likeBadge-" + index
                count: modelData; glyph: "thumb-up"; pill: true
                height: 28.5; font: Theme.font(11, Theme.semiboldWeight)
                horizontalPadding: 10; foreground: Theme.accentText
                tone: EchoBadge.Muted
            }
        }
    }
})",
                      QUrl("qrc:/badge-icon-ink.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Icon/text ink pixels require native RHI");
    const auto capture = qEnvironmentVariable("ECHO_BADGE_ICON_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    QStringList failures;
    for (bool dark : {false, true})
        for (qreal scale : {1., 1.25}) {
            session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
            session.root->setProperty("sampleScale", scale);
            for (int i = 0; i < 5; ++i) {
                auto *glyph =
                    findVisual(session.item("likeBadge-" + QString::number(i)), "buttonGlyph");
                QVERIFY(glyph);
                QTRY_COMPARE(glyph->property("status").toInt(), 1);
            }
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(frames.size() > 0);
            const auto image = session.window->grabWindow();
            QVERIFY(!image.isNull());
            const qreal ratio = image.width() / qreal(session.window->width());
            if (!capture.isEmpty())
                QVERIFY(image.save(
                    capture + QString("/badge-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
            for (int i = 0; i < 5; ++i) {
                auto *badge = session.item("likeBadge-" + QString::number(i));
                auto *glyph = findVisual(badge, "buttonGlyph");
                auto *label = findVisual(badge, "buttonLabel");
                QVERIFY(glyph && label);
                const auto color = label->property("color").value<QColor>();
                const auto inkCenter = [&](QQuickItem *item) {
                    const auto bounds =
                        item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                    const auto pixels = QRectF(bounds.topLeft() * ratio, bounds.size() * ratio)
                                            .toAlignedRect()
                                            .intersected(image.rect());
                    int top = image.height(), bottom = -1;
                    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                        for (int x = pixels.left(); x <= pixels.right(); ++x) {
                            const auto p = image.pixelColor(x, y);
                            if (qAbs(p.red() - color.red()) + qAbs(p.green() - color.green()) +
                                    qAbs(p.blue() - color.blue()) <
                                60) {
                                top = std::min(top, y);
                                bottom = std::max(bottom, y);
                            }
                        }
                    return bottom < top ? -1. : (top + bottom + 1) / 2.;
                };
                const auto iconInk = inkCenter(glyph), textInk = inkCenter(label);
                QVERIFY(iconInk >= 0 && textInk >= 0);
                const auto delta = iconInk - textInk;
                qInfo() << "badge_ink" << dark << scale << badge->property("displayText")
                        << "icon_minus_text_pixels" << delta;
                if (qAbs(delta) > 1.)
                    failures.append(QString("%1 scale %2 dark %3: %4px")
                                        .arg(badge->property("displayText").toString())
                                        .arg(scale)
                                        .arg(dark)
                                        .arg(delta));
            }
        }
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::textTagAlignmentAcrossProductionRecipes()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("TextTagAlignmentHarness.qml")));
    const auto customFamily = QFontDatabase::hasFamily(QStringLiteral("Helvetica"))
                                  ? QStringLiteral("Helvetica")
                                  : QFontDatabase::families().value(0);
    const auto capture = qEnvironmentVariable("ECHO_TEXT_TAG_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    int captured = 0;
    // Pixels, not just Item centers: the previous implementation passed
    // geometry-only checks while the large Chinese title still sat too low.
    auto verifyInk = [&]() -> bool {
        QTest::qWait(60);
        const auto image = session.window->grabWindow();
        if (image.isNull())
            return false;
        const qreal ratio = image.width() / qreal(session.window->width());
        for (int index = 0; index < 5; ++index) {
            auto *row = session.item(QString("alignmentRow-%1").arg(index));
            if (!row)
                return false;
            auto *label = row->property("textItem").value<QQuickItem *>();
            auto *box = row->property("textBox").value<QQuickItem *>();
            auto *tag = findVisual(row, index < 3 ? "alignmentTag" : "alignmentBadge");
            if (!label || !box || !tag)
                return false;
            const auto bounds = box->mapRectToScene(QRectF(0, -3, box->width(), box->height() + 6));
            const auto pixels = QRectF(bounds.topLeft() * ratio, bounds.size() * ratio)
                                    .toAlignedRect()
                                    .intersected(image.rect());
            int top = image.height(), bottom = -1;
            for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
                for (int x = pixels.left(); x <= pixels.right(); ++x) {
                    const auto color = image.pixelColor(x, y);
                    if (color.red() > 120 && color.blue() > 70 &&
                        color.red() > color.green() * 1.8 && color.blue() > color.green() * 1.4) {
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            }
            if (bottom < top)
                return false;
            const qreal actual = (top + bottom + 1) / (2 * ratio);
            const qreal expected = box->mapToScene({0, box->height() / 2}).y();
            const qreal tolerance = 1.0 * session.root->property("sampleScale").toReal();
            if (qAbs(actual - expected) > tolerance) {
                qWarning() << "ink alignment" << index << label->property("text") << "actual"
                           << actual << "expected" << expected;
                return false;
            }
            if (tag->isVisible()) {
                QQuickItem *tagText = nullptr;
                std::function<void(QQuickItem *)> locate = [&](QQuickItem *item) {
                    if (item->property("font").canConvert<QFont>() &&
                        !item->property("text").toString().isEmpty())
                        tagText = item;
                    for (auto *child : item->childItems())
                        locate(child);
                };
                locate(tag);
                if (!tagText)
                    return false;
                const QColor foreground = tagText->property("color").value<QColor>();
                const auto interior =
                    tag->mapRectToScene(QRectF(3, 2, tag->width() - 6, tag->height() - 4));
                const auto tagPixels = QRectF(interior.topLeft() * ratio, interior.size() * ratio)
                                           .toAlignedRect()
                                           .intersected(image.rect());
                int inkTop = image.height(), inkBottom = -1;
                for (int y = tagPixels.top(); y <= tagPixels.bottom(); ++y) {
                    for (int x = tagPixels.left(); x <= tagPixels.right(); ++x) {
                        const auto pixel = image.pixelColor(x, y);
                        const int distance = qAbs(pixel.red() - foreground.red()) +
                                             qAbs(pixel.green() - foreground.green()) +
                                             qAbs(pixel.blue() - foreground.blue());
                        if (distance < 45) {
                            inkTop = std::min(inkTop, y);
                            inkBottom = std::max(inkBottom, y);
                        }
                    }
                }
                if (inkBottom < inkTop)
                    return false;
                const auto center = tag->mapToScene({0, tag->height() / 2}).y();
                if (qAbs((inkTop + inkBottom + 1) / (2 * ratio) - center) > tolerance)
                    return false;
                // Inline tags center on rendered text; wrapped groups retain their own line
                // height.
                const auto tagTop = tag->mapToScene({0, 0}).y();
                if (tagTop < box->mapToScene({0, box->height()}).y()) {
                    const auto tagCenter = tag->mapToScene({0, tag->height() / 2}).y();
                    if (qAbs(expected - tagCenter) > tolerance)
                        return false;
                }
            }
        }
        if (!capture.isEmpty())
            return image.save(capture + QString("/matrix-%1.png").arg(captured++));
        return true;
    };
    for (const auto &family : {QStringLiteral("system-ui"), customFamily}) {
        session.theme.setGlobalFont(family);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const qreal scale : {1.0, 1.25}) {
                session.root->setProperty("sampleScale", scale);
                session.root->setProperty("availableWidth", 560);
                for (const auto &text : {QStringLiteral("一天"), QStringLiteral("EchoMusic"),
                                         QStringLiteral("一天 EchoMusic 123 gyp")}) {
                    session.root->setProperty("sampleText", text);
                    QVERIFY2(verifyInk(), qPrintable(family + " / " + text));
                }
            }
        }
    }
    session.root->setProperty("sampleScale", 1.0);
    session.root->setProperty("availableWidth", 200);
    session.root->setProperty("sampleText", QStringLiteral("一天 EchoMusic 一天 EchoMusic 一天"));
    QVERIFY(verifyInk());
    for (const auto index : {0, 4}) {
        auto *row = session.item(QString("alignmentRow-%1").arg(index));
        auto *label = row->property("textItem").value<QQuickItem *>();
        QVERIFY(label->property("lineCount").toInt() > 1);
    }
    session.root->setProperty("showTags", false);
    session.root->setProperty("sampleText", QStringLiteral("一天"));
    QVERIFY(verifyInk());
    auto *row = session.item("alignmentRow-0");
    auto *box = row->property("textBox").value<QQuickItem *>();
    QTRY_COMPARE(row->implicitWidth(), box->width());
    session.root->setProperty("showTags", true);
    QTRY_VERIFY2(row->implicitWidth() > box->width() + 8,
                 qPrintable(QString("tags restored: row=%1 text=%2 tag=%3")
                                .arg(row->implicitWidth())
                                .arg(box->width())
                                .arg(findVisual(row, "alignmentTag")->implicitWidth())));
    QVERIFY(verifyInk());
    // Flex labels keep their tags at the far edge when shortened, elided,
    // or asynchronously hidden; the other production recipes stay natural.
    auto *flexRow = session.item("alignmentRow-1");
    auto *flexTag = findVisual(flexRow, "alignmentTag");
    auto *flexBox = flexRow->property("textBox").value<QQuickItem *>();
    QVERIFY(flexTag && flexBox);
    flexRow->setProperty("fillTextWidth", true);
    for (const auto text :
         {QStringLiteral("一天"), QStringLiteral("一天 EchoMusic 一天 EchoMusic 一天")}) {
        session.root->setProperty("sampleText", text);
        QTRY_VERIFY(qAbs(flexTag->mapToItem(flexRow, QPointF(flexTag->width(), 0)).x() -
                         flexRow->width()) < .5);
        QVERIFY(verifyInk());
    }
    session.root->setProperty("showTags", false);
    QTRY_COMPARE(flexBox->width(), flexRow->width());
    session.root->setProperty("showTags", true);
    QTRY_VERIFY(qAbs(flexTag->mapToItem(flexRow, QPointF(flexTag->width(), 0)).x() -
                     flexRow->width()) < .5);
    QVERIFY(verifyInk());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::badgesAndTagsKeepDistinctContracts()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("BadgeTagHarness.qml")));
    auto *badge = session.item("countBadge");
    auto *tag = session.item("statusTag");
    auto *floating = session.item("floatingCount");
    auto *owner = session.item("badgeOwner");
    auto *content = session.item("contentTag");
    QVERIFY(badge && tag && floating && owner && content);
    // Zero is a real count; status text has an independent public contract.
    QVERIFY(badge->isVisible());
    QCOMPARE(badge->property("count").toInt(), 0);
    QVERIFY(badge->metaObject()->indexOfProperty("text") < 0);
    QVERIFY(tag->metaObject()->indexOfProperty("count") < 0);
    QTRY_COMPARE(badge->size(), tag->size());
    QCOMPARE(badge->height(), 18.);
    tag->setProperty("text", QStringLiteral("当前登录"));
    QTRY_VERIFY(tag->width() > badge->width());
    badge->setProperty("count", QVariant());
    QTRY_VERIFY(!badge->isVisible());
    QVERIFY(tag->isVisible());
    badge->setProperty("count", QStringLiteral("99+"));
    QTRY_VERIFY(badge->isVisible());
    QVERIFY(floating->x() + floating->width() > owner->width());
    QVERIFY(floating->y() < 0);
    const auto originalX = floating->x();
    owner->setWidth(owner->width() + 50);
    QTRY_COMPARE(floating->x(), originalX + 50);
    QTRY_COMPARE(content->width(), 50.);
    QCOMPARE(content->height(), 18.);
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::globalTypographyRetainsFallbackAndLocalStyles()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("TypographyHarness.qml")));
    const auto initialFamily = session.theme.applicationFont().families().first();
    const auto selected = QFontDatabase::hasFamily(QStringLiteral("Helvetica"))
                              ? QStringLiteral("Helvetica")
                              : QFontDatabase::families().value(0);
    QVERIFY(!selected.isEmpty());
    const QList<QPair<QString, int>> samples{{"weight400", 400},
                                             {"weight500", 500},
                                             {"weight600", 600},
                                             {"weight650", 650},
                                             {"weight700", 700},
                                             {"weight800", 800},
                                             {"weight900", 900},
                                             {"typographySoftButton", 600},
                                             {"typographyFormButton", 900},
                                             {"typographyInput", 700},
                                             {"typographyButtonContent", 600},
                                             {"typographyDecoratedText", 600}};
    // Changing preferences must update already-created controls, including
    // overrides on derived controls, without rebuilding the window.
    for (const auto &preference :
         {QStringLiteral("system-ui"), selected, QStringLiteral("system-ui")}) {
        session.theme.setGlobalFont(preference);
        for (const auto &[name, weight] : samples) {
            auto *item = session.item(name);
            QVERIFY2(item, qPrintable(name));
            QTRY_COMPARE(item->property("font").value<QFont>().families(),
                         session.theme.applicationFont().families());
            const auto font = item->property("font").value<QFont>();
            QCOMPARE(font.weight(), weight);
            if (name == "typographySoftButton" || name == "typographyInput")
                QCOMPARE(font.pixelSize(), 14);
            else if (name == "typographyFormButton")
                QCOMPARE(font.pixelSize(), 16);
            else if (name.startsWith("weight")) {
                QCOMPARE(font.pixelSize(), 20);
                // QFont stores spacing on a 1/64px fixed-point grid.
                QVERIFY(qAbs(font.letterSpacing() + 0.2) <= 1.0 / 64);
            }
            if (name == "typographyDecoratedText") {
                QVERIFY(font.italic());
                QVERIFY(font.underline());
                QCOMPARE(font.letterSpacing(), 0.5);
            }
#ifdef Q_OS_MACOS
            if (weight > 400 && QFontDatabase::hasFamily(QStringLiteral("PingFang SC"))) {
                auto regularFont = font;
                regularFont.setWeight(QFont::Normal);
                QTextLayout latinNormal("Y", regularFont), latinStyled("Y", font);
                for (auto *layout : {&latinNormal, &latinStyled}) {
                    layout->beginLayout();
                    layout->createLine();
                    layout->endLayout();
                }
                const auto latinRun = latinStyled.glyphRuns().first();
                const auto latinRegularRun = latinNormal.glyphRuns().first();
                const bool hasLatinVariant =
                    latinRun.rawFont().pathForGlyph(latinRun.glyphIndexes().first()) !=
                    latinRegularRun.rawFont().pathForGlyph(latinRegularRun.glyphIndexes().first());
                // Numeric QFont/QRawFont weights can report 600 while the
                // actual Chinese face remains Regular. Check glyph outlines.
                for (const auto &text :
                     {QStringLiteral("Y"), QStringLiteral("茶"), QStringLiteral("《")}) {
                    auto regular = font;
                    regular.setWeight(QFont::Normal);
                    QTextLayout normal(text, regular), styled(text, font);
                    for (auto *layout : {&normal, &styled}) {
                        layout->beginLayout();
                        layout->createLine();
                        layout->endLayout();
                    }
                    QCOMPARE(normal.glyphRuns().size(), 1);
                    QCOMPARE(styled.glyphRuns().size(), 1);
                    const auto run = styled.glyphRuns().first();
                    const auto normalRun = normal.glyphRuns().first();
                    QCOMPARE(run.glyphIndexes().size(), 1);
                    QVERIFY(run.glyphIndexes().first() != 0);
                    const auto outline = run.rawFont().pathForGlyph(run.glyphIndexes().first());
                    QVERIFY(!outline.isEmpty());
                    if ((name == "weight500" || name == "weight600") && preference == "system-ui")
                        qInfo().noquote() << name << text << run.rawFont().familyName()
                                          << run.rawFont().styleName();
                    // Some installed families (e.g. Helvetica at 500) have
                    // no Medium variant; Qt resolves both scripts to Regular.
                    // Require heavier CJK when Latin is heavier, and real
                    // variants for the Cocoa system-ui baseline.
                    if (!hasLatinVariant &&
                        (QGuiApplication::platformName() != "cocoa" || preference != "system-ui"))
                        continue;
                    QVERIFY2(outline !=
                                 normalRun.rawFont().pathForGlyph(normalRun.glyphIndexes().first()),
                             qPrintable(name + ": " + text + " under " + preference + " uses " +
                                        run.rawFont().familyName() + " " +
                                        run.rawFont().styleName() +
                                        " instead of a heavier outline than " +
                                        normalRun.rawFont().familyName() + " " +
                                        normalRun.rawFont().styleName()));
                }
            }
#endif
        }
    }
    QCOMPARE(session.theme.applicationFont().families().first(), initialFamily);
    const auto capture = qEnvironmentVariable("ECHO_TYPOGRAPHY_CAPTURE_DIR");
    if (!capture.isEmpty() && QGuiApplication::platformName() != "offscreen") {
        QVERIFY(QDir().mkpath(capture));
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
            session.window->update();
            QTRY_VERIFY(!frames.isEmpty());
            const auto image = session.window->grabWindow();
            QVERIFY(!image.isNull());
            QVERIFY(image.save(capture + (mode == ThemeStore::Dark ? "/dark.png" : "/light.png")));
        }
    }
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::centeredContentAcrossSizesAndPadding()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    QVERIFY(session.engine.imageProvider("echo-icons"));
    for (const int height : {32, 36, 48})
        for (const int fontSize : {12, 20})
            for (const int padding : {0, 6, 12}) {
                session.root->setProperty("buttonHeight", height);
                session.root->setProperty("labelSize", fontSize);
                session.root->setProperty("verticalPadding", padding);
                for (const auto *name : {"accentButton", "neutralButton", "ordinaryButton"}) {
                    auto *button = session.item(name);
                    auto *glyph = findVisual(button, "buttonGlyph");
                    auto *label = findVisual(button, "buttonLabel");
                    QVERIFY(glyph && label);
                    QTRY_VERIFY(qAbs(glyph
                                         ->mapToItem(button, QPointF(glyph->width() / 2,
                                                                     glyph->height() / 2))
                                         .y() -
                                     button->height() / 2) < 0.6);
                    QTRY_VERIFY(qAbs(label
                                         ->mapToItem(button, QPointF(label->width() / 2,
                                                                     label->height() / 2))
                                         .y() -
                                     button->height() / 2) < 0.6);
                    QCOMPARE(glyph->property("color").value<QColor>(),
                             label->property("color").value<QColor>());
                }
            }
    // The locked Qt SDK exports RowReverse (the online preview docs use a
    // different spelling). Exercise the real reversed-content path used by
    // account actions so an unresolved enum cannot silently keep Row order.
    for (const auto *name : {"accentButton", "neutralButton", "ordinaryButton"}) {
        auto *button = session.item(name);
        auto *content = button->property("contentItem").value<QQuickItem *>();
        auto *glyph = findVisual(button, "buttonGlyph");
        auto *label = findVisual(button, "buttonLabel");
        QVERIFY(content && glyph && label);
        content->setProperty("glyphAfterText", true);
        QTRY_VERIFY(glyph->mapToItem(content, QPointF()).x() >=
                    label->mapToItem(content, QPointF(label->width(), 0)).x());
        content->setProperty("glyphAfterText", false);
        QTRY_VERIFY(label->mapToItem(content, QPointF()).x() >=
                    glyph->mapToItem(content, QPointF(glyph->width(), 0)).x());
    }
    session.root->setProperty("label", QStringLiteral("中文与 Latin 长按钮文字，必须省略"));
    auto *content = session.item("accentButton")->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QTRY_VERIFY(content->property("truncated").toBool());
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::iconTextContentCentersRenderedInk()
{
    ControlsSession session;
    QVERIFY(session.create());
    session.window->resize(900, 400);
    session.window->contentItem()->setTransformOrigin(QQuickItem::TopLeft);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    const auto capture = qEnvironmentVariable("ECHO_ICON_TEXT_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    const auto customFamily = QFontDatabase::hasFamily(QStringLiteral("Helvetica"))
                                  ? QStringLiteral("Helvetica")
                                  : QFontDatabase::families().value(0);
    int captured = 0;
    session.root->setProperty("buttonHeight", 38);
    session.root->setProperty("labelSize", 13);
    // Check rendered Chinese/fallback/Latin ink against the icon box, not
    // just Text's ascent rectangle. That geometry passed before this fix.
    for (const auto &family : {QStringLiteral("system-ui"), customFamily}) {
        session.theme.setGlobalFont(family);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            session.theme.setMode(mode);
            for (const qreal scale : {1.0, 1.25}) {
                session.window->contentItem()->setScale(scale);
                for (const auto &text :
                     {QStringLiteral("任务中心"), QStringLiteral("更多应用 EchoMusic gyp")}) {
                    QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                    session.root->setProperty("label", text);
                    session.window->requestUpdate();
                    QTRY_VERIFY(frames.size() > 0);
                    const auto image = session.window->grabWindow();
                    QVERIFY(!image.isNull());
                    if (!capture.isEmpty())
                        QVERIFY(image.save(capture + QString("/buttons-%1.png").arg(captured++)));
                    const auto ratio = image.width() / qreal(session.window->width());
                    for (const auto *name : {"accentButton", "neutralButton", "ordinaryButton"}) {
                        auto *button = session.item(name);
                        auto *label = findVisual(button, "buttonLabel");
                        auto *glyph = findVisual(button, "buttonGlyph");
                        auto *box = label->property("textBox").value<QQuickItem *>();
                        QVERIFY(box && glyph);
                        const auto foreground = label->property("color").value<QColor>();
                        const auto rect =
                            box->mapRectToScene(QRectF(0, -2, box->width(), box->height() + 4));
                        const auto pixels = QRectF(rect.topLeft() * ratio, rect.size() * ratio)
                                                .toAlignedRect()
                                                .intersected(image.rect());
                        int top = image.height(), bottom = -1;
                        for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                            for (int x = pixels.left(); x <= pixels.right(); ++x) {
                                const auto color = image.pixelColor(x, y);
                                if (qAbs(color.red() - foreground.red()) +
                                        qAbs(color.green() - foreground.green()) +
                                        qAbs(color.blue() - foreground.blue()) <
                                    80) {
                                    top = std::min(top, y);
                                    bottom = std::max(bottom, y);
                                }
                            }
                        QVERIFY2(
                            bottom >= top,
                            qPrintable(QString("%1 %2 %3 scale=%4 pixels=%5,%6,%7,%8 labelWidth=%9 "
                                               "color=%10")
                                           .arg(name, family, text)
                                           .arg(scale)
                                           .arg(pixels.x())
                                           .arg(pixels.y())
                                           .arg(pixels.width())
                                           .arg(pixels.height())
                                           .arg(label->width())
                                           .arg(label->property("color").value<QColor>().name())));
                        const auto center = glyph->mapToScene(QPointF(0, glyph->height() / 2)).y();
                        QVERIFY2(
                            qAbs((top + bottom + 1) / (2 * ratio) - center) <= scale,
                            qPrintable(QString("%1 %2 %3 scale=%4 ink=%5 icon=%6 top=%7 bottom=%8")
                                           .arg(name, family, text)
                                           .arg(scale)
                                           .arg((top + bottom + 1) / (2 * ratio))
                                           .arg(center)
                                           .arg(top)
                                           .arg(bottom)));
                    }
                }
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::allBundledGlyphsTintAndPreserveAlpha()
{
    ControlsSession session; // Load production module resources and provider.
    QVERIFY(session.create());
    EchoIconImageProvider provider;
    const auto files = QDir(":/qt/qml/EchoMusic/Controls/icons").entryList({"*.svg"}, QDir::Files);
    const auto sourceFiles = QDir(ECHO_ICON_SOURCE_DIR).entryList({"*.svg"}, QDir::Files);
    QVERIFY(!sourceFiles.isEmpty());
    // Check the actual packaging inventory rather than freezing a historical
    // count; every shipped glyph below still has tint/alpha assertions.
    QCOMPARE(files, sourceFiles);
    QVERIFY(files.contains("check.svg"));
    const QColor tint(40, 160, 210, 166);
    for (const auto &file : files) {
        QSize original;
        const auto id = file.chopped(4) + "/" + QString::number(tint.rgba(), 16);
        const auto image = provider.requestImage(id, &original, QSize(40, 40));
        QVERIFY2(!image.isNull(), qPrintable(file));
        QCOMPARE(original, file == "verification-spinner.svg" ? QSize(28, 28) : QSize(24, 24));
        QCOMPARE(image.size(), QSize(40, 40));
        QColor strongest(Qt::transparent);
        int transparent = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixelColor(x, y);
                if (pixel.alpha() > strongest.alpha())
                    strongest = pixel;
                transparent += pixel.alpha() == 0;
            }
        QVERIFY(transparent > 0);
        QVERIFY2(qAbs(strongest.alpha() - tint.alpha()) <= 1,
                 qPrintable(file + " alpha=" + QString::number(strongest.alpha())));
        QVERIFY(qAbs(strongest.red() - tint.red()) <= 1);
        QVERIFY(qAbs(strongest.green() - tint.green()) <= 1);
        QVERIFY(qAbs(strongest.blue() - tint.blue()) <= 1);
    }
    QVERIFY(provider.requestImage("../music/ffffffff", nullptr, QSize(40, 40)).isNull());
    QVERIFY(provider.requestImage("music/invalid", nullptr, QSize(40, 40)).isNull());
}
