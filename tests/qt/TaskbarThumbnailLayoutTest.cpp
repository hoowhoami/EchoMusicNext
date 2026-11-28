#include "TaskbarThumbnailLayout.h"
#include <QtTest>
using namespace EchoMusic;

// DWM lays the hover preview out from the bitmap's own shape and rejects
// anything larger than the bounds it names. The bitmap is pinned square so the
// card is square and the artwork can be shown complete; the tests below also
// guard the two failure modes seen in practice: a cropped cover, and black bars
// beside it on a card that is not square.
class TaskbarThumbnailLayoutTest : public QObject
{
    Q_OBJECT
private slots:
    void requestSplitsHighWordIntoWidth();
    void tileIsSquareAndFitsTheRequestedBounds();
    void tileFallsBackWhenTheRequestNamesNothing();
    void livePreviewIsSquare();
    void coverIsShownWholeWithoutCropping();
    void backdropTrimsTheLongEdgeAndKeepsTheShortOne();
    void backdropStaysInsideTheCoverForExtremeShapes();
    void renderedTileIsAlwaysSquare();
    void renderedTileFillsTheBandsWithArtwork();
    void renderedTileKeepsTheWholeCover();
    void renderedTileHandlesExtremeCoverShapes();
    void compositeTileNeverExceedsTheCoverOrTheCap();
};

void TaskbarThumbnailLayoutTest::requestSplitsHighWordIntoWidth()
{
    // HIWORD is the maximum x-coordinate, LOWORD the maximum y-coordinate.
    const auto packed = qintptr((qintptr(240) << 16) | qintptr(135));
    QCOMPARE(taskbarThumbnailRequest(packed), QSize(240, 135));
    // An empty lParam names no bounds at all.
    QCOMPARE(taskbarThumbnailRequest(0), QSize(0, 0));
}

void TaskbarThumbnailLayoutTest::tileIsSquareAndFitsTheRequestedBounds()
{
    // The shorter requested side is the largest square inside both bounds, so
    // the bitmap never exceeds what DWM named.
    QCOMPARE(taskbarThumbnailTile(QSize(250, 135)), QSize(135, 135));
    QCOMPARE(taskbarThumbnailTile(QSize(135, 250)), QSize(135, 135));
    QCOMPARE(taskbarThumbnailTile(QSize(240, 240)), QSize(240, 240));
    // A square bitmap is what makes the card square instead of landscape.
    const auto tile = taskbarThumbnailTile(QSize(250, 135));
    QCOMPARE(tile.width(), tile.height());
    // The cap still applies to a very large request.
    QCOMPARE(taskbarThumbnailTile(QSize(4000, 4000)), QSize(600, 600));
    QCOMPARE(taskbarThumbnailTile(QSize(4000, 200)), QSize(200, 200));
}

void TaskbarThumbnailLayoutTest::tileFallsBackWhenTheRequestNamesNothing()
{
    // A request with no usable bounds yields the largest allowed square, so a
    // malformed message still produces a valid bitmap.
    const QSize largest(MaximumTaskbarTileSide, MaximumTaskbarTileSide);
    QCOMPARE(taskbarThumbnailTile(QSize()), largest);
    QCOMPARE(taskbarThumbnailTile(QSize(0, 135)), largest);
    QCOMPARE(taskbarThumbnailTile(QSize(135, 0)), largest);
}

void TaskbarThumbnailLayoutTest::livePreviewIsSquare()
{
    // A live preview names no bounds, and the peek window follows the bitmap:
    // it must stay square even when the host window is a parked sliver.
    const QSize preview = taskbarLivePreviewTile();
    QCOMPARE(preview.width(), preview.height());
    QCOMPARE(preview.width(), LivePreviewTileSide);
}

void TaskbarThumbnailLayoutTest::coverIsShownWholeWithoutCropping()
{
    // A square cover fills a square tile exactly.
    QCOMPARE(taskbarThumbnailCoverRect(QSize(240, 240), QSize(512, 512)), QRect(0, 0, 240, 240));
    // A portrait cover keeps its full height and gains bands on both sides:
    // it is fitted, never cropped.
    const QRect portrait = taskbarThumbnailCoverRect(QSize(240, 240), QSize(300, 450));
    QCOMPARE(portrait.height(), 240);
    QCOMPARE(portrait.width(), qRound(240.0 * 300.0 / 450.0));
    QCOMPARE(portrait.left(), (240 - portrait.width()) / 2);
    QCOMPARE(portrait.top(), 0);
    // A landscape cover keeps its full width and gains bands above and below.
    const QRect landscape = taskbarThumbnailCoverRect(QSize(240, 240), QSize(400, 200));
    QCOMPARE(landscape.width(), 240);
    QCOMPARE(landscape.height(), qRound(240.0 * 200.0 / 400.0));
    QCOMPARE(landscape.top(), (240 - landscape.height()) / 2);
    // A cover smaller than the tile is scaled up rather than left in a corner.
    QCOMPARE(taskbarThumbnailCoverRect(QSize(240, 240), QSize(1, 1)), QRect(0, 0, 240, 240));
    QVERIFY(taskbarThumbnailCoverRect(QSize(240, 240), QSize()).isEmpty());
    QVERIFY(taskbarThumbnailCoverRect(QSize(), QSize(512, 512)).isEmpty());
}

void TaskbarThumbnailLayoutTest::backdropTrimsTheLongEdgeAndKeepsTheShortOne()
{
    // A square cover covers a square tile without trimming anything.
    QCOMPARE(taskbarBackdropSourceRect(QSize(240, 240), QSize(512, 512)), QRect(0, 0, 512, 512));
    // A cover wider than the tile fills the height and loses its sides evenly.
    const QRect wide = taskbarBackdropSourceRect(QSize(240, 240), QSize(600, 300));
    QCOMPARE(wide.height(), 300);
    QCOMPARE(wide.width(), 300);
    QCOMPARE(wide.left(), 150);
    QCOMPARE(wide.top(), 0);
    // A taller-than-tile cover fills the width and loses its top and bottom.
    const QRect tall = taskbarBackdropSourceRect(QSize(240, 240), QSize(300, 600));
    QCOMPARE(tall.width(), 300);
    QCOMPARE(tall.height(), 300);
    QCOMPARE(tall.top(), 150);
    QCOMPARE(tall.left(), 0);
    // A cover smaller than the tile is entirely used and enlarged.
    QCOMPARE(taskbarBackdropSourceRect(QSize(240, 240), QSize(1, 1)), QRect(0, 0, 1, 1));
    QVERIFY(taskbarBackdropSourceRect(QSize(240, 240), QSize()).isEmpty());
    QVERIFY(taskbarBackdropSourceRect(QSize(), QSize(512, 512)).isEmpty());
}

void TaskbarThumbnailLayoutTest::backdropStaysInsideTheCoverForExtremeShapes()
{
    // Enlarging used to overflow int for a cover millions of pixels wide and
    // paint the tile black. The source rect is computed instead, so it must
    // always lie inside the cover and never be empty.
    const QSize tile(600, 600);
    for (const QSize cover :
         {QSize(4000000, 1), QSize(8000000, 1), QSize(1, 4000000), QSize(100000, 3)}) {
        const QRect source = taskbarBackdropSourceRect(tile, cover);
        QVERIFY2(!source.isEmpty(), "a valid cover must keep a paintable source rect");
        const QRect bounds(QPoint(0, 0), cover);
        QVERIFY2(bounds.contains(source), "the source rect must lie inside the cover");
        // The short edge is kept whole; the long edge is trimmed to match the
        // tile's shape.
        if (cover.width() > cover.height()) {
            QCOMPARE(source.height(), cover.height());
        } else {
            QCOMPARE(source.width(), cover.width());
        }
    }
    // A cover far wider than the tile must still paint its own colour, not the
    // black the tile is cleared to. The backdrop carries a 50% black veil, so
    // the cover's blue arrives halved but still dominant.
    QImage sliver(4000000, 1, QImage::Format_ARGB32_Premultiplied);
    sliver.fill(QColor(90, 140, 200));
    const QImage rendered = renderTaskbarThumbnail(sliver, tile);
    QCOMPARE(rendered.size(), tile);
    for (const QPoint &at : {QPoint(300, 300), QPoint(0, 0), QPoint(599, 599)}) {
        const QColor pixel = rendered.pixelColor(at);
        QVERIFY2(pixel.blue() > 60 && pixel.blue() > pixel.red(),
                 "the enlarged backdrop must come from the cover, not the black fill");
    }
}

void TaskbarThumbnailLayoutTest::renderedTileIsAlwaysSquare()
{
    QImage cover(512, 512, QImage::Format_ARGB32_Premultiplied);
    cover.fill(Qt::darkBlue);
    const auto rendered = renderTaskbarThumbnail(cover, QSize(135, 135));
    QCOMPARE(rendered.size(), QSize(135, 135));
    QVERIFY(renderTaskbarThumbnail(QImage(), QSize(135, 135)).isNull());
    QVERIFY(renderTaskbarThumbnail(cover, QSize()).isNull());
}

void TaskbarThumbnailLayoutTest::renderedTileFillsTheBandsWithArtwork()
{
    // A flat cover fills the tile edge to edge, so nothing is left as a bar.
    QImage cover(512, 512, QImage::Format_ARGB32_Premultiplied);
    cover.fill(QColor(200, 40, 40));
    const auto rendered = renderTaskbarThumbnail(cover, QSize(135, 135));
    for (int x : {0, 67, 134}) {
        for (int y : {0, 67, 134}) {
            const QColor pixel = rendered.pixelColor(x, y);
            QCOMPARE(pixel.red(), 200);
            QCOMPARE(pixel.alpha(), 255);
        }
    }
    // A portrait cover leaves side bands, but they carry the dimmed artwork
    // rather than black: the corner shares the cover's hue.
    QImage portrait(300, 450, QImage::Format_ARGB32_Premultiplied);
    portrait.fill(QColor(200, 40, 40));
    const auto banded = renderTaskbarThumbnail(portrait, QSize(135, 135));
    const QColor corner = banded.pixelColor(2, 67);
    QVERIFY(corner.red() > corner.green());
    QVERIFY(corner.red() > 80);
    QCOMPARE(corner.alpha(), 255);
}

void TaskbarThumbnailLayoutTest::renderedTileKeepsTheWholeCover()
{
    // The cover carries a marker in each corner; fitting keeps all four.
    QImage cover(400, 400, QImage::Format_ARGB32_Premultiplied);
    cover.fill(QColor(30, 30, 30));
    const QColor marker(0, 220, 0);
    for (const QPoint &corner : {QPoint(0, 0), QPoint(399, 0), QPoint(0, 399), QPoint(399, 399)}) {
        for (int dx = 0; dx < 8; ++dx) {
            for (int dy = 0; dy < 8; ++dy) {
                const QPoint at = corner + QPoint(corner.x() ? -dx : dx, corner.y() ? -dy : dy);
                cover.setPixelColor(at, marker);
            }
        }
    }
    const auto rendered = renderTaskbarThumbnail(cover, QSize(240, 240));
    QCOMPARE(rendered.size(), QSize(240, 240));
    // A square cover in a square tile maps 1:1, so every marker survives: the
    // artwork was scaled, not cropped away.
    for (const QPoint &corner : {QPoint(0, 0), QPoint(239, 0), QPoint(0, 239), QPoint(239, 239)}) {
        QCOMPARE(rendered.pixelColor(corner), marker);
    }
}

void TaskbarThumbnailLayoutTest::renderedTileHandlesExtremeCoverShapes()
{
    // A one-pixel cover still produces a full tile of its own colour.
    QImage dot(1, 1, QImage::Format_ARGB32_Premultiplied);
    dot.fill(QColor(10, 200, 10));
    const auto filled = renderTaskbarThumbnail(dot, QSize(120, 120));
    QCOMPARE(filled.size(), QSize(120, 120));
    QCOMPARE(filled.pixelColor(60, 60).green(), 200);

    // A 1000x1 sliver keeps its full width and fills the bands with the same
    // artwork; the result is a valid opaque tile, not a null image.
    QImage sliver(1000, 1, QImage::Format_ARGB32_Premultiplied);
    sliver.fill(QColor(200, 10, 10));
    const auto banded = renderTaskbarThumbnail(sliver, QSize(150, 150));
    QCOMPARE(banded.size(), QSize(150, 150));
    QCOMPARE(banded.pixelColor(75, 0).alpha(), 255);
    QCOMPARE(banded.pixelColor(75, 149).alpha(), 255);
    // The fitted strip spans the whole tile width and keeps its single row.
    const QRect source = taskbarThumbnailCoverRect(QSize(150, 150), sliver.size());
    QCOMPARE(source.width(), 150);
    QCOMPARE(source.height(), 1);
}

void TaskbarThumbnailLayoutTest::compositeTileNeverExceedsTheCoverOrTheCap()
{
    // The composite is rendered ahead of DWM's requests and then resampled per
    // tile: keeping it at the cover's own long side means a square cover's
    // render is the identity and no tile resamples the artwork twice.
    QCOMPARE(taskbarCompositeTile(QSize(256, 256)), QSize(256, 256));
    QCOMPARE(taskbarCompositeTile(QSize(300, 200)), QSize(300, 300));
    QCOMPARE(taskbarCompositeTile(QSize(200, 300)), QSize(300, 300));
    QCOMPARE(taskbarCompositeTile(QSize(64, 64)), QSize(64, 64));
    // A cover larger than the cap still yields a bounded square.
    QCOMPARE(taskbarCompositeTile(QSize(1200, 900)), QSize(600, 600));
    QCOMPARE(taskbarCompositeTile(QSize(4000, 4000)), QSize(600, 600));
    // Degenerate sizes stay valid so a null-ish cover cannot crash apply().
    QCOMPARE(taskbarCompositeTile(QSize()), QSize(1, 1));
    QCOMPARE(taskbarCompositeTile(QSize(0, 48)), QSize(1, 1));
}

QTEST_MAIN(TaskbarThumbnailLayoutTest)
#include "TaskbarThumbnailLayoutTest.moc"
