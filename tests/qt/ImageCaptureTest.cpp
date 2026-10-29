#include "ImageCapture.h"
#include <QFile>
#include <QImageReader>
#include <QPointer>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSemaphore>
#include <QtTest>
using namespace EchoMusic;

class CapturePattern final : public QQuickItem
{
public:
    explicit CapturePattern(QQuickItem *parent = nullptr) : QQuickItem(parent)
    {
        setFlag(ItemHasContents);
        setSize({320, 180});
    }

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override
    {
        delete old;
        auto *root = new QSGNode;
        const QColor colors[] = {Qt::red, Qt::green, Qt::blue, Qt::white};
        for (int index = 0; index < 4; ++index)
            root->appendChildNode(
                new QSGSimpleRectNode(QRectF(index % 2 * width() / 2, index / 2 * height() / 2,
                                             width() / 2, height() / 2),
                                      colors[index]));
        return root;
    }
};

// Test-only worst-case texture. Production ImageCapture never creates or scans image pixels.
class CaptureNoise final : public QQuickItem
{
public:
    explicit CaptureNoise(QQuickItem *parent)
        : QQuickItem(parent), image(4096, 2048, QImage::Format_RGB32)
    {
        quint32 random = 0x127ac541;
        for (int y = 0; y < image.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                row[x] = 0xff000000 | (random & 0xffffff);
            }
        }
        setFlag(ItemHasContents);
        setSize({320, 180});
    }

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override
    {
        auto *node = static_cast<QSGSimpleTextureNode *>(old);
        if (!node) {
            node = new QSGSimpleTextureNode;
            node->setTexture(window()->createTextureFromImage(image));
            node->setOwnsTexture(true);
        }
        node->setRect(boundingRect());
        return node;
    }

private:
    QImage image;
};

class ImageCaptureTest : public QObject
{
    Q_OBJECT
private slots:
    void publicQmlLeaseCanCaptureWithoutPrivateTypes()
    {
        qmlRegisterType<ImageCapture>("EchoMusic.ImageCaptureProbe", 1, 0, "ImageCapture");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import EchoMusic.ImageCaptureProbe 1.0
            Item {
                id: root
                width: 320; height: 180
                property var lease: null
                property bool done: lease !== null && lease.ready
                Rectangle { anchors.fill: parent; color: Qt.rgba(.25, .5, .75, 1) }
                ImageCapture { id: capture; objectName: "capture" }
                function takePicture() { lease = capture.encode(root, 321, 181, "png", 92); }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(item);
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        item->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(QMetaObject::invokeMethod(item, "takePicture"));
        auto *capture = item->findChild<ImageCapture *>("capture");
        QVERIFY(capture);
        QTRY_VERIFY_WITH_TIMEOUT(item->property("done").toBool(), 10000);
        QCOMPARE(capture->readbacks(), 1);
        capture->setActive(false);
        QTRY_VERIFY_WITH_TIMEOUT(capture->isDrained(), 10000);
        item->setParentItem(nullptr);
    }
    void rejectsInvalidInputsWithoutReservingOrPublishing()
    {
        ImageCapture capture;
        CapturePattern detached;
        const auto error = [&](ImageCaptureLease *lease) {
            QVERIFY(lease);
            QCOMPARE(lease->status(), QString("error"));
            QVERIFY(!lease->ready());
            QVERIFY(!lease->errorString().isEmpty());
            QVERIFY(lease->url().isEmpty());
            QCOMPARE(lease->encodedBytes(), 0);
            lease->dispose();
        };
        error(capture.encode(nullptr, 1920, 1080));
        error(capture.encode(&detached, 1920, 1080));
        error(capture.encode(&detached, 0, 10));
        error(capture.encode(&detached, 4097, 10));
        error(capture.encode(&detached, 4096, 4096));
        error(capture.encode(&detached, 10, 10, "gif"));
        error(capture.encode(&detached, 10, 10, "jpeg", 101));
        capture.setActive(false);
        error(capture.encode(&detached, 10, 10));
        QCOMPARE(capture.pendingJobs(), 0);
        QCOMPARE(capture.reservedBytes(), 0);
        QCOMPARE(capture.encodedBytes(), 0);
        QTRY_VERIFY(capture.isDrained());
    }
    void finalPhysicalPixelsIgnoreWindowDpr_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("source poster") << QSize(1920, 1080);
        QTest::newRow("odd poster") << QSize(1919, 1079);
        QTest::newRow("small") << QSize(321, 181);
    }
    void finalPhysicalPixelsIgnoreWindowDpr()
    {
        QFETCH(QSize, size);
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        CapturePattern source(window.contentItem());
        ImageCapture capture;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *lease = capture.encode(&source, size.width(), size.height(), "png");
        QVERIFY(lease);
        QTRY_VERIFY_WITH_TIMEOUT(lease->status() != "pending", 15000);
        QVERIFY2(lease->ready(), qPrintable(lease->errorString()));
        QCOMPARE(lease->width(), size.width());
        QCOMPARE(lease->height(), size.height());
        const auto file = lease->url().toLocalFile();
        const QImage image(file);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), size);
        QCOMPARE(image.pixelColor(size.width() / 4, size.height() / 4), QColor(Qt::red));
        QCOMPARE(image.pixelColor(size.width() * 3 / 4, size.height() / 4), QColor(Qt::green));
        QCOMPARE(image.pixelColor(size.width() / 4, size.height() * 3 / 4), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(size.width() * 3 / 4, size.height() * 3 / 4), QColor(Qt::white));
        QCOMPARE(QFileInfo(file).size(), lease->encodedBytes());
        qInfo() << "ImageCapture exact physical pixels" << window.devicePixelRatio() << size
                << image.size() << lease->encodedBytes();
        const auto encoded = lease->encodedBytes();
        QCOMPARE(capture.readbacks(), 1);
        QCOMPARE(capture.encodes(), 1);
        source.setOpacity(.2);
        window.update();
        QTest::qWait(30);
        QCOMPARE(QImage(file), image);
        QCOMPARE(lease->encodedBytes(), encoded);
        if (size == QSize(321, 181)) {
            QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
            const auto advance = connect(&window, &QQuickWindow::frameSwapped, &window,
                                         [&window] { window.update(); });
            window.update();
            QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 120, 5000);
            disconnect(advance);
        }
        QCOMPARE(capture.readbacks(), 1);
        QCOMPARE(capture.encodes(), 1);
        QPointer<ImageCaptureLease> pointer = lease;
        lease->dispose();
        QTRY_VERIFY(pointer.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(file) && capture.isDrained(), 10000);
        QCOMPARE(capture.reservedBytes(), 0);
        QCOMPARE(capture.encodedBytes(), 0);
    }
    void jpegIsEncodedAsynchronouslyAndImmutable()
    {
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        CapturePattern source(window.contentItem());
        ImageCapture capture;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *lease = capture.encode(&source, 1920, 1080, "jpeg", 92);
        QTRY_VERIFY_WITH_TIMEOUT(lease->status() != "pending", 15000);
        QVERIFY2(lease->ready(), qPrintable(lease->errorString()));
        QFile file(lease->url().toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(2), QByteArray::fromHex("ffd8"));
        const QImage image(lease->url().toLocalFile());
        QCOMPARE(image.size(), QSize(1920, 1080));
        QVERIFY(image.pixelColor(480, 270).red() > 245);
        QVERIFY(image.pixelColor(480, 270).green() < 8);
        QVERIFY(lease->encodedBytes() > 1000);
        QCOMPARE(capture.pendingJobs(), 0);
        QCOMPARE(capture.leaseCount(), 1);
        const auto path = lease->url().toLocalFile();
        capture.setActive(false);
        QTRY_VERIFY_WITH_TIMEOUT(capture.isDrained() && !QFileInfo::exists(path), 10000);
    }
    void sourceOutsideViewportStillCapturesItsLocalGpuContent()
    {
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        CapturePattern source(window.contentItem());
        source.setPosition({-10000, -10000});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ImageCapture capture;
        auto *lease = capture.encode(&source, 320, 180, "png");
        QTRY_VERIFY_WITH_TIMEOUT(lease->status() != "pending", 10000);
        QVERIFY2(lease->ready(), qPrintable(lease->errorString()));
        const QImage image(lease->url().toLocalFile());
        QCOMPARE(image.pixelColor(80, 45), QColor(Qt::red));
        QCOMPARE(image.pixelColor(240, 135), QColor(Qt::white));
        capture.setActive(false);
        QTRY_VERIFY(capture.isDrained());
    }
    void cancelAndOwnerDestructionNeverPublishLateResults()
    {
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        CapturePattern source(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ImageCapture capture;
        QPointer<ImageCaptureLease> lease = capture.encode(&source, 1920, 1080, "jpeg", 92);
        capture.setActive(false);
        QTRY_VERIFY(lease.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(capture.isDrained(), 10000);
        QCOMPARE(capture.pendingJobs(), 0);
        QCOMPARE(capture.reservedBytes(), 0);
        auto *owner = new ImageCapture;
        owner->encode(&source, 1920, 1080, "jpeg", 92);
        QElapsedTimer timer;
        timer.start();
        delete owner;
        QVERIFY2(timer.elapsed() < 50, "scope destruction must not join an encode worker");
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().pendingJobs, 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().reservedBytes, 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().encodedBytes, 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().outputReservedBytes, 0, 10000);
    }
    void sourceWindowChangeAndCrossInstanceBudgets()
    {
        QQuickWindow window, other;
        window.setGeometry(0, 0, 320, 180);
        CapturePattern source(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ImageCapture a, b, c;
        auto *first = a.encode(&source, 1920, 1080, "png");
        auto *second = b.encode(&source, 1920, 1080, "png");
        auto *third = c.encode(&source, 1920, 1080, "png");
        QCOMPARE(third->status(), QString("error"));
        QVERIFY(third->errorString().contains("budget"));
        QVERIFY(ImageCapture::budgetSnapshot().pendingJobs <= 2);
        QVERIFY(ImageCapture::budgetSnapshot().reservedBytes <= ImageCapture::MaxReservedBytes);
        source.setParentItem(other.contentItem());
        QVERIFY(first->status() != "ready");
        QVERIFY(second->status() != "ready");
        a.setActive(false);
        b.setActive(false);
        c.setActive(false);
        QTRY_VERIFY_WITH_TIMEOUT(a.isDrained() && b.isDrained() && c.isDrained(), 10000);
        QCOMPARE(ImageCapture::budgetSnapshot().leaseCount, 0);
    }
    void outputLimitAndEncodingCancellationReleaseEveryReservation()
    {
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        CaptureNoise source(window.contentItem());
        ImageCapture capture;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *lease = capture.encode(&source, 4096, 2048, "png");
        QTRY_VERIFY_WITH_TIMEOUT(lease->status() != "pending", 20000);
        QCOMPARE(lease->status(), QString("error"));
        QVERIFY(lease->errorString().contains("byte budget"));
        QCOMPARE(capture.readbacks(), 1);
        QCOMPARE(capture.encodes(), 1);
        QVERIFY(lease->url().isEmpty());
        QCOMPARE(capture.encodedBytes(), 0);
        lease->dispose();
        QTRY_VERIFY(capture.isDrained());
        QCOMPARE(ImageCapture::budgetSnapshot().outputReservedBytes, 0);
        auto *owner = new ImageCapture;
        QPointer<ImageCaptureLease> cancelled = owner->encode(&source, 4096, 2048, "png");
        QTRY_COMPARE_WITH_TIMEOUT(owner->readbacks(), 1, 10000);
        QVERIFY(owner->pendingJobs() > 0);
        QElapsedTimer timer;
        timer.start();
        delete owner;
        QVERIFY2(timer.elapsed() < 50, "destroying an encoding scope must not join its worker");
        QVERIFY(cancelled.isNull());
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().pendingJobs, 0, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().reservedBytes, 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().encodedBytes, 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().outputReservedBytes, 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().leaseCount, 0, 10000);
    }
    void cancellationAfterSynchronizationKeepsTheReadbackReservation_data()
    {
        QTest::addColumn<bool>("destroyOwner");
        QTest::newRow("inactive") << false;
        QTest::newRow("destroy owner") << true;
    }
    void cancellationAfterSynchronizationKeepsTheReadbackReservation()
    {
        QFETCH(bool, destroyOwner);
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        CapturePattern source(window.contentItem());
        QScopedPointer<ImageCapture> capture(new ImageCapture);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        std::atomic<bool> armed{false}, synced{false}, barrierSucceeded{false};
        int pendingAtCancellation = -1;
        qint64 reservedAtCancellation = 0;
        QSemaphore resumeRender;
        const auto sync = connect(
            &window, &QQuickWindow::beforeSynchronizing, &window,
            [&] {
                if (armed)
                    synced = true;
            },
            Qt::DirectConnection);
        const auto render = connect(
            &window, &QQuickWindow::beforeRendering, &window,
            [&] {
                if (!synced || !armed.exchange(false))
                    return;
                const auto cancel = [&] {
                    if (destroyOwner) {
                        capture.reset();
                        pendingAtCancellation = ImageCapture::budgetSnapshot().pendingJobs;
                        reservedAtCancellation = ImageCapture::budgetSnapshot().reservedBytes;
                    } else {
                        capture->setActive(false);
                        pendingAtCancellation = capture->pendingJobs();
                        reservedAtCancellation = capture->reservedBytes();
                    }
                    resumeRender.release();
                };
                if (QThread::currentThread() == capture->thread()) {
                    cancel();
                    barrierSucceeded = true;
                } else {
                    QMetaObject::invokeMethod(capture.data(), cancel, Qt::QueuedConnection);
                    barrierSucceeded = resumeRender.tryAcquire(1, 2000);
                }
            },
            Qt::DirectConnection);
        capture->encode(&source, 1920, 1080, "png");
        armed = true;
        window.update();
        QTRY_VERIFY_WITH_TIMEOUT(barrierSucceeded.load(), 5000);
        disconnect(sync);
        disconnect(render);
        QCOMPARE(pendingAtCancellation, 1);
        QVERIFY(reservedAtCancellation > 0);
        if (capture) {
            QTRY_VERIFY_WITH_TIMEOUT(capture->isDrained(), 10000);
            QCOMPARE(capture->encodes(), 0);
        }
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().pendingJobs, 0, 10000);
        QCOMPARE(ImageCapture::budgetSnapshot().reservedBytes, 0);
        QTRY_COMPARE(ImageCapture::budgetSnapshot().leaseCount, 0);
    }
    void sixteenSharedLeasesHaveNoUnboundedPendingQueue()
    {
        QQuickWindow window;
        window.setGeometry(0, 0, 320, 180);
        CapturePattern source(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ImageCapture a, b;
        for (int index = 0; index < ImageCapture::MaxLeases; ++index) {
            auto *lease = (index % 2 ? a : b).encode(&source, 32, 18, "png");
            QTRY_VERIFY_WITH_TIMEOUT(lease->status() != "pending", 10000);
            QVERIFY2(lease->ready(), qPrintable(lease->errorString()));
        }
        QCOMPARE(ImageCapture::budgetSnapshot().leaseCount, ImageCapture::MaxLeases);
        auto *overflow = a.encode(&source, 32, 18, "png");
        QCOMPARE(overflow->status(), QString("error"));
        QVERIFY(overflow->errorString().contains("budget"));
        QCOMPARE(ImageCapture::budgetSnapshot().pendingJobs, 0);
        QCOMPARE(ImageCapture::budgetSnapshot().reservedBytes, 0);
        a.setActive(false);
        b.setActive(false);
        QTRY_VERIFY_WITH_TIMEOUT(a.isDrained() && b.isDrained(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(ImageCapture::budgetSnapshot().leaseCount, 0, 10000);
        QCOMPARE(ImageCapture::budgetSnapshot().encodedBytes, 0);
    }
};
QTEST_MAIN(ImageCaptureTest)
#include "ImageCaptureTest.moc"
