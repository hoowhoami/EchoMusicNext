#include "TaskbarLyricGeometry.h"
#include <QtTest>

// The taskbar lyric position modes are pure geometry: the platform reports
// where the taskbar's free areas are and the resolvers turn that into the
// rectangle the strip occupies. No OS call takes part, so every mode and every
// fallback boundary is pinned here.
namespace geometry = EchoMusic::TaskbarLyricGeometry;
using EchoMusic::TaskbarLayout;

namespace
{
/// A taskbar with a free area either side of the button cluster: 2560 x 40
/// physical pixels, buttons on the left, tray on the right.
TaskbarLayout typicalLayout(qreal scale = 1.0)
{
    TaskbarLayout layout;
    layout.left = QRect(qRound(680 * scale), 0, qRound(800 * scale), qRound(40 * scale));
    layout.right = QRect(qRound(1800 * scale), 0, qRound(700 * scale), qRound(40 * scale));
    layout.barWidth = qRound(2560 * scale);
    layout.scaleFactor = scale;
    layout.available = true;
    layout.reliable = true;
    layout.dark = true;
    return layout;
}
} // namespace

class TaskbarLyricGeometryTest : public QObject
{
    Q_OBJECT
private slots:
    void sideSeatsTheStripInItsArea();
    void sideRejectsNarrowOrMisshapenAreas();
    void sideClampsToTheAreaAndRejectsUnknownScale();
    void autoHugsTheAreaItLandedIn();
    void fixedPinsToTheTaskbarEdges();
    void draggedClampsInsideTheTaskbar();
    void physicalLayoutConvertsToDip();
};

void TaskbarLyricGeometryTest::sideSeatsTheStripInItsArea()
{
    const auto layout = typicalLayout();
    const auto left = geometry::resolveSide(layout, geometry::Side::Left, 400);
    QVERIFY(left.has_value());
    QVERIFY(left->side == geometry::Side::Left);
    QCOMPARE(left->bounds.x(), 680.0 + geometry::kEdgeInset);
    QCOMPARE(left->bounds.y(), 0.0);
    QCOMPARE(left->bounds.width(), 400.0);
    QCOMPARE(left->bounds.height(), 40.0);

    // Hugging the right edge of the area is what puts the strip next to the
    // tray instead of next to the icons.
    const auto right = geometry::resolveSide(layout, geometry::Side::Right, 400);
    QVERIFY(right.has_value());
    QVERIFY(right->side == geometry::Side::Right);
    QCOMPARE(right->bounds.x(), 1800.0 + 700.0 - 400.0 - geometry::kEdgeInset);
    QCOMPARE(right->bounds.width(), 400.0);
}

void TaskbarLyricGeometryTest::sideRejectsNarrowOrMisshapenAreas()
{
    auto layout = typicalLayout();
    // The area must still leave the minimum width once the margin is removed.
    layout.left = QRect(680, 0, geometry::kMinWidth + geometry::kWidthMargin - 1, 40);
    QVERIFY(!geometry::resolveSide(layout, geometry::Side::Left, 400).has_value());
    layout.left = QRect(680, 0, geometry::kMinWidth + geometry::kWidthMargin, 40);
    QVERIFY(geometry::resolveSide(layout, geometry::Side::Left, 400).has_value());

    // The height has to sit inside [kMinHeight, kMaxHeight]: a shorter side is
    // not the taskbar row and a taller one is some other surface.
    layout.left = QRect(680, 0, 800, geometry::kMinHeight - 1);
    QVERIFY(!geometry::resolveSide(layout, geometry::Side::Left, 400).has_value());
    layout.left = QRect(680, 0, 800, geometry::kMaxHeight + 1);
    QVERIFY(!geometry::resolveSide(layout, geometry::Side::Left, 400).has_value());
    layout.left = QRect(680, 0, 800, geometry::kMinHeight);
    QVERIFY(geometry::resolveSide(layout, geometry::Side::Left, 400).has_value());
    layout.left = QRect(680, 0, 800, geometry::kMaxHeight);
    QVERIFY(geometry::resolveSide(layout, geometry::Side::Left, 400).has_value());

    // An empty side cannot seat the strip at all.
    layout.left = QRect();
    QVERIFY(!geometry::resolveSide(layout, geometry::Side::Left, 400).has_value());
}

void TaskbarLyricGeometryTest::sideClampsToTheAreaAndRejectsUnknownScale()
{
    auto layout = typicalLayout();
    // A maximum wider than the area shrinks to what the area can give; the
    // margin keeps the strip clear of the icons beside it.
    layout.left = QRect(680, 0, 300, 40);
    const auto clamped = geometry::resolveSide(layout, geometry::Side::Left, 800);
    QVERIFY(clamped.has_value());
    QCOMPARE(clamped->bounds.width(), 300.0 - geometry::kWidthMargin);

    // Scale 0 means the layout is unknown; dividing by it would produce an
    // infinite width instead of the hide fallback.
    auto unknown = typicalLayout();
    unknown.scaleFactor = 0;
    QVERIFY(!geometry::resolveSide(unknown, geometry::Side::Left, 400).has_value());
}

void TaskbarLyricGeometryTest::autoHugsTheAreaItLandedIn()
{
    const auto layout = typicalLayout();
    // The preferred side wins whenever it can seat the strip.
    const auto primary = geometry::resolveAuto(layout, geometry::Side::Right, 400);
    QVERIFY(primary.has_value());
    QVERIFY(primary->side == geometry::Side::Right);
    QCOMPARE(primary->bounds.x(), 1800.0 + 700.0 - 400.0 - geometry::kEdgeInset);

    // Too narrow: the other area is used, and the seat hugs THAT area's edge.
    // Keeping the requested right anchor here would right-align the strip
    // inside the left gap, parking it against the button cluster the fallback
    // exists to avoid (shared/taskbar.ts:81).
    auto narrowRight = layout;
    narrowRight.right = QRect(1800, 0, geometry::kMinWidth + geometry::kWidthMargin - 1, 40);
    const auto fallback = geometry::resolveAuto(narrowRight, geometry::Side::Right, 400);
    QVERIFY(fallback.has_value());
    QVERIFY(fallback->side == geometry::Side::Left);
    QCOMPARE(fallback->bounds.x(), 680.0 + geometry::kEdgeInset);

    // Nothing to fall back to when neither area fits.
    auto crowded = layout;
    crowded.left = QRect(680, 0, 100, 40);
    crowded.right = QRect(1800, 0, 100, 40);
    QVERIFY(!geometry::resolveAuto(crowded, geometry::Side::Left, 400).has_value());
}

void TaskbarLyricGeometryTest::fixedPinsToTheTaskbarEdges()
{
    const auto layout = typicalLayout();
    const auto left = geometry::resolveFixed(layout, geometry::Side::Left, 400);
    QVERIFY(left.has_value());
    QVERIFY(left->side == geometry::Side::Left);
    QCOMPARE(left->bounds.x(), qreal(geometry::kEdgeInset));
    QCOMPARE(left->bounds.width(), 400.0);
    QCOMPARE(left->bounds.height(), 40.0);

    const auto right = geometry::resolveFixed(layout, geometry::Side::Right, 400);
    QVERIFY(right.has_value());
    QVERIFY(right->side == geometry::Side::Right);
    QCOMPARE(right->bounds.x(), 2560.0 - 400.0 - geometry::kEdgeInset);

    // The mode deliberately ignores the occupied spans: a taskbar covered by
    // icons still seats the strip as long as some side reports the row height.
    auto crowded = layout;
    crowded.left = QRect();
    crowded.right = QRect(0, 0, 0, 40);
    QVERIFY(geometry::resolveFixed(crowded, geometry::Side::Left, 400).has_value());

    // Without a row height there is nothing to seat the strip on, and an
    // unknown bar width cannot be pinned to.
    auto noRow = layout;
    noRow.left = QRect();
    noRow.right = QRect();
    QVERIFY(!geometry::resolveFixed(noRow, geometry::Side::Left, 400).has_value());
    auto noBar = layout;
    noBar.barWidth = 0;
    QVERIFY(!geometry::resolveFixed(noBar, geometry::Side::Left, 400).has_value());
}

void TaskbarLyricGeometryTest::draggedClampsInsideTheTaskbar()
{
    const auto layout = typicalLayout();
    // The drop point is honoured one to one.
    const auto dropped = geometry::resolveDragged(layout, geometry::Side::Left, 400, 300);
    QVERIFY(dropped.has_value());
    QCOMPARE(dropped->bounds.x(), 300.0);
    QCOMPARE(dropped->bounds.width(), 400.0);
    QVERIFY(dropped->side == geometry::Side::Left);

    // Off either end it clamps to the taskbar instead of leaving the screen.
    const auto beforeStart = geometry::resolveDragged(layout, geometry::Side::Left, 400, -50);
    QVERIFY(beforeStart.has_value());
    QCOMPARE(beforeStart->bounds.x(), 0.0);
    const auto pastEnd = geometry::resolveDragged(layout, geometry::Side::Left, 400, 5000);
    QVERIFY(pastEnd.has_value());
    QCOMPARE(pastEnd->bounds.x(), 2560.0 - 400.0);

    // The side stays the caller's, so the content keeps hugging the edge the
    // strip was placed against.
    const auto rightAnchored = geometry::resolveDragged(layout, geometry::Side::Right, 400, 300);
    QVERIFY(rightAnchored.has_value());
    QVERIFY(rightAnchored->side == geometry::Side::Right);
}

void TaskbarLyricGeometryTest::physicalLayoutConvertsToDip()
{
    // The platform reports physical pixels; every mode works in DIP, so the
    // scale factor is divided out exactly once per value.
    const qreal scale = 2;
    const auto layout = typicalLayout(scale);
    const auto side = geometry::resolveSide(layout, geometry::Side::Left, 400);
    QVERIFY(side.has_value());
    QCOMPARE(side->bounds.x(), 680.0 + geometry::kEdgeInset);
    QCOMPARE(side->bounds.width(), 400.0);
    QCOMPARE(side->bounds.height(), 40.0);

    const auto fixed = geometry::resolveFixed(layout, geometry::Side::Right, 400);
    QVERIFY(fixed.has_value());
    QCOMPARE(fixed->bounds.x(), 2560.0 - 400.0 - geometry::kEdgeInset);

    const auto dragged = geometry::resolveDragged(layout, geometry::Side::Left, 400, 600);
    QVERIFY(dragged.has_value());
    QCOMPARE(dragged->bounds.x(), 300.0);
}

QTEST_GUILESS_MAIN(TaskbarLyricGeometryTest)
#include "TaskbarLyricGeometryTest.moc"
