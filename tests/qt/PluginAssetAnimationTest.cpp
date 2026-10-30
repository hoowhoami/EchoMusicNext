#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "EchoPluginAssets.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <algorithm>

namespace
{
const QByteArray gif = QByteArray::fromHex("47494638396101000100800000ff00000000ff"
                                           "21ff0b4e45545343415045322e300301000000"
                                           "21f904000a0000002c0000000001000100000202440100"
                                           "21f90400140000002c00000000010001000002024c01003b");
struct Fixture
{
    Host host;
    EchoPluginContext *scope = nullptr;
    QUrl source;
    bool activate(const QByteArray &bytes = gif)
    {
        const auto directory = package(host.temporary.path(), "export function activate(ctx) {}");
        write(directory + "/animation.gif", bytes);
        host.runtime.importDirectory(directory);
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 7000) {
            const auto id = host.runtime.installationForId("test.runtime");
            if (!id.isEmpty() && host.runtime.state(id) == PluginRuntime::Active) {
                scope = host.runtime.context(id);
                source = QUrl::fromLocalFile(scope->package().root + "/animation.gif");
                return scope;
            }
            QTest::qWait(1);
        }
        return false;
    }
};
} // namespace

class PluginAssetAnimationTest : public QObject
{
    Q_OBJECT
private slots:
    void publicScopeOwnedAnimationLease()
    {
        Host host;
        const auto source = package(host.temporary.path(), "export function activate(ctx) {}");
        host.runtime.importDirectory(source);
        const auto active = [&] {
            const auto id = host.runtime.installationForId("test.runtime");
            return !id.isEmpty() && host.runtime.state(id) == PluginRuntime::Active;
        };
        QTRY_VERIFY_WITH_TIMEOUT(active(), 7000);
        auto *scope = host.runtime.context(host.runtime.installationForId("test.runtime"));
        QVERIFY(scope);
        QVERIFY2(scope->assets()->metaObject()->indexOfMethod("animation(QUrl,int,int)") >= 0,
                 "Animated images need a scope-owned background decoder lease");
    }
    void exactFramesSharedCachePauseAndLastReferenceCleanup()
    {
        Fixture f;
        QVERIFY(f.activate());
        auto *assets = f.scope->assets();
        QPointer<EchoPluginAssetAnimation> a = assets->animation(f.source, 80, 80);
        QPointer<EchoPluginAssetAnimation> b = assets->animation(f.source, 80, 80);
        QVERIFY(a && b && a != b);
        QCOMPARE(a->parent(), assets);
        a->setPlaying(false);
        b->setPlaying(false);
        QTRY_VERIFY_WITH_TIMEOUT(a->ready() || !a->errorString().isEmpty(), 15000);
        QCOMPARE(a->errorString(), QString());
        QVERIFY(b->ready());
        QCOMPARE(a->frameCount(), 2);
        QCOMPARE(a->durationMs(), 300);
        QCOMPARE(a->loopCount(), -1);
        QCOMPARE(a->width(), 1);
        QCOMPARE(a->height(), 1);
        QCOMPARE(a->frameSource(), b->frameSource());
        QVERIFY(a->encodedBytes() > 0 && a->encodedBytes() < 4096);
        const auto first = a->frameSource().toLocalFile();
        const auto frames = QFileInfo(first).absolutePath();
        QImageReader original(f.source.toLocalFile());
        for (int i = 0; i < 2; ++i) {
            const QImage expected = original.read().convertToFormat(QImage::Format_RGBA8888);
            const QImage actual = QImage(QDir(frames).filePath(QString::number(i) + ".png"))
                                      .convertToFormat(QImage::Format_RGBA8888);
            QCOMPARE(actual, expected);
        }
        a->setPlaying(true);
        b->setPlaying(true);
        QTRY_COMPARE_WITH_TIMEOUT(a->frameIndex(), 1, 1000);
        QCOMPARE(a->frameSource(), b->frameSource());
        QCOMPARE(QImage(a->frameSource().toLocalFile()).pixelColor(0, 0), QColor(Qt::blue));
        a->setPlaying(false);
        b->setPlaying(false);
        const auto stopped = a->frameSource();
        QTest::qWait(400);
        QCOMPARE(a->frameSource(), stopped);
        QCOMPARE(b->frameSource(), stopped);
        a->dispose();
        QTRY_VERIFY(a.isNull());
        QVERIFY(QFileInfo::exists(first));
        b->setPlaying(true);
        QTRY_COMPARE_WITH_TIMEOUT(b->frameIndex(), 0, 1000);
        b->dispose();
        QTRY_VERIFY(b.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 10000);
        QVERIFY(!QFileInfo::exists(frames));
    }
    void finiteLoopStopsOnFinalFrame()
    {
        auto finite = gif;
        finite.replace(QByteArray::fromHex("0301000000"), QByteArray::fromHex("0301010000"));
        Fixture f;
        QVERIFY(f.activate(finite));
        auto *lease = f.scope->assets()->animation(f.source, 8, 8);
        QVERIFY(lease);
        QTRY_VERIFY_WITH_TIMEOUT(lease->ready() || !lease->errorString().isEmpty(), 15000);
        QCOMPARE(lease->errorString(), QString());
        QCOMPARE(lease->loopCount(), 1);
        QTest::qWait(800);
        QCOMPARE(lease->frameIndex(), 1);
        QSignalSpy frames(lease, &EchoPluginAssetAnimation::frameChanged);
        QTest::qWait(400);
        QCOMPARE(frames.count(), 0);
        lease->dispose();
        QTRY_VERIFY(f.scope->assets()->isDrained());
    }
    void containAndCoverKeepOriginalAspectAndCropResolution()
    {
        QByteArray png;
        QBuffer output(&png);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QImage original(40, 20, QImage::Format_RGBA8888);
        original.fill(Qt::green);
        QVERIFY(original.save(&output, "PNG"));
        Fixture f;
        QVERIFY(f.activate(png));
        auto *assets = f.scope->assets();
        auto *contain = assets->animation(f.source, 8, 8, "contain");
        auto *cover = assets->animation(f.source, 8, 8, "cover");
        auto *noUpscale = assets->animation(f.source, 4096, 4096, "cover");
        QVERIFY(contain && cover && noUpscale);
        QTRY_VERIFY_WITH_TIMEOUT(contain->ready() || !contain->errorString().isEmpty(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(cover->ready() || !cover->errorString().isEmpty(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(noUpscale->ready() || !noUpscale->errorString().isEmpty(), 15000);
        QCOMPARE(contain->errorString(), QString());
        QCOMPARE(cover->errorString(), QString());
        QCOMPARE(noUpscale->errorString(), QString());
        QCOMPARE(QSize(contain->width(), contain->height()), QSize(8, 4));
        QCOMPARE(QSize(cover->width(), cover->height()), QSize(16, 8));
        QCOMPARE(QSize(noUpscale->width(), noUpscale->height()), original.size());
        QCOMPARE(QImage(cover->frameSource().toLocalFile()).size(), QSize(16, 8));
        QCOMPARE(cover->frameCount(), 1);
        contain->dispose();
        cover->dispose();
        noUpscale->dispose();
        QTRY_VERIFY(assets->isDrained());
    }
    void queueCountAndLeaseBudgetsAreBounded()
    {
        Fixture f;
        QVERIFY(f.activate());
        auto *assets = f.scope->assets();
        QList<QPointer<EchoPluginAssetAnimation>> leases;
        for (int i = 1; i <= 32; ++i) {
            auto *lease = assets->animation(f.source, i, i);
            QVERIFY(lease);
            lease->setPlaying(false);
            leases.append(lease);
        }
        auto *overflow = assets->animation(f.source, 33, 33);
        QVERIFY(overflow && !overflow->errorString().isEmpty());
        overflow->dispose();
        const auto allReady = [&] {
            return std::all_of(leases.cbegin(), leases.cend(), [](const auto &lease) {
                return lease && (lease->ready() || !lease->errorString().isEmpty());
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(allReady(), 15000);
        for (const auto &lease : leases) {
            QCOMPARE(lease->errorString(), QString());
            QVERIFY(lease->ready());
        }
        // Identical sources/dimensions add references, never decoder jobs or disk caches.
        while (leases.size() < 128) {
            auto *lease = assets->animation(f.source, 1, 1);
            QVERIFY(lease);
            leases.append(lease);
        }
        QCOMPARE(assets->animation(f.source, 1, 1), nullptr);
        for (const auto &lease : leases)
            lease->dispose();
        QTRY_VERIFY_WITH_TIMEOUT(assets->isDrained(), 10000);
        for (const auto &lease : leases)
            QVERIFY(lease.isNull());
    }
    void unauthorizedSourcesAndInvalidDimensions()
    {
        Fixture f;
        QVERIFY(f.activate());
        auto *assets = f.scope->assets();
        auto *foreign = assets->animation(QUrl("file:///etc/hosts"), 64, 64);
        auto *remote = assets->animation(QUrl("https://example.invalid/foreign.gif"), 64, 64);
        auto *invalid = assets->animation(f.source, 4097, 1);
        auto *fit = assets->animation(f.source, 64, 64, "stretch");
        QVERIFY(foreign && remote && invalid);
        QTRY_VERIFY(!foreign->errorString().isEmpty());
        QTRY_VERIFY(!remote->errorString().isEmpty());
        QVERIFY(!invalid->errorString().isEmpty());
        QVERIFY(fit && !fit->errorString().isEmpty());
        QVERIFY(!foreign->ready() && !remote->ready() && !invalid->ready());
        QCOMPARE(foreign->encodedBytes(), 0);
        foreign->dispose();
        remote->dispose();
        invalid->dispose();
        fit->dispose();
        QTRY_VERIFY(assets->isDrained());
    }
    void retiringScopeCancelsQueuedFramesWithoutLatePublication()
    {
        Fixture f;
        QVERIFY(f.activate());
        QPointer<EchoPluginContext> scope = f.scope;
        QPointer<EchoPluginAssetAnimation> lease = f.scope->assets()->animation(f.source, 80, 80);
        QVERIFY(lease && !lease->ready());
        QVERIFY(f.host.runtime.setEnabled(f.host.installation(), false));
        QTRY_VERIFY_WITH_TIMEOUT(scope.isNull(), 10000);
        QVERIFY(lease.isNull());
    }
};

QTEST_MAIN(PluginAssetAnimationTest)
#include "PluginAssetAnimationTest.moc"
