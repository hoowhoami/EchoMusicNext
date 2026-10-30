#include "GlyphRun.h"
#include "GlyphRunCanvas_p.h"
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QtTest>
#include <memory>

using EchoMusic::GlyphRun;
using EchoMusic::GlyphCanvas::Memory;

namespace
{
QVariantList oneRun(const QString &key, const QString &text)
{
    return {QVariantMap{{"key", key}, {"text", text}, {"maxBlur", 0}, {"maxHaloRadius", 160}}};
}
QString firstKey(const QVariantList &values)
{
    return values.isEmpty() ? QString() : values.first().toMap().value("key").toString();
}
void configureCanvas(GlyphRun &item)
{
    item.setMaximumRasterScale(1);
    item.setHaloSampling("canvas");
}
} // namespace

// These are native font/worker tests. No QQuickWindow, scene graph, RHI or GPU
// is created; ready()/GPU allocation are deliberately not acceptance criteria.
class GlyphRunCanvasPreparationTest : public QObject
{
    Q_OBJECT
    qint64 m_initialBytes = 0;
private slots:
    void init()
    {
        m_initialBytes = Memory::total();
    }
    void cleanup()
    {
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), m_initialBytes, 15000);
        QVERIFY(Memory::highWater() <= EchoMusic::GlyphCanvas::NativeBudget);
    }
    void heavyUnknownPayloadIsRejectedAtomically_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("byte-array") << 0;
        QTest::newRow("image") << 1;
        QTest::newRow("string-list") << 2;
    }
    void heavyUnknownPayloadIsRejectedAtomically()
    {
        QFETCH(int, kind);
        GlyphRun item;
        configureCanvas(item);
        item.setRuns(oneRun("original", "Preserved native layout"));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(firstKey(item.metrics()), QString("original"));
        const auto runs = item.runs(), metrics = item.metrics();
        const auto revision = item.maskRevision();
        const auto bytes = item.cacheBytes(), nativeBytes = Memory::total();
        auto invalid = oneRun("rejected", "A").first().toMap();
        if (kind == 0)
            invalid["unknownPayload"] = QByteArray(8 * 1024 * 1024, 'x');
        else if (kind == 1) {
            QImage image(2048, 2048, QImage::Format_RGBA8888_Premultiplied);
            image.fill(Qt::transparent);
            invalid["unknownPayload"] = image;
        } else
            invalid["unknownPayload"] = QStringList{QString(4 * 1024 * 1024, QChar('x'))};
        item.setRuns({invalid});
        QVERIFY2(!item.errorString().isEmpty(),
                 "Unsupported payload was accepted by the GUI entry point");
        QVERIFY(!item.preparing());
        QCOMPARE(item.runs(), runs);
        QCOMPARE(item.metrics(), metrics);
        QCOMPARE(item.maskRevision(), revision);
        QCOMPARE(item.cacheBytes(), bytes);
        QCOMPARE(Memory::total(), nativeBytes);
    }
    void workerPreservesCompleteFontAndContextualMetrics()
    {
        auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        font.setFamilies({font.family(), "PingFang SC", "Apple Color Emoji"});
        font.setPixelSize(31);
        font.setWeight(QFont::DemiBold);
        font.setItalic(true);
        font.setStretch(117);
        font.setKerning(true);
        font.setLetterSpacing(QFont::AbsoluteSpacing, .625);
        font.setWordSpacing(1.75);
        font.setHintingPreference(QFont::PreferFullHinting);
        const QVariantList runs{
            oneRun("latin", "office affine ffi AV").first(),
            oneRun("cjk", QString::fromUtf8("海边的风 中文排版")).first(),
            oneRun("arabic", QString::fromUtf8("سَلَامُ العربية")).first(),
            oneRun("emoji", QString::fromUtf8("😀 👨‍👩‍👧‍👦 🏳️‍🌈"))
                .first()};
        GlyphRun native;
        native.setMeasurementOnly(true);
        native.setFont(font);
        native.setRuns(runs);
        QVERIFY2(native.errorString().isEmpty(), qPrintable(native.errorString()));
        QCOMPARE(native.metrics().size(), runs.size());
        GlyphRun worker;
        worker.setFont(font);
        configureCanvas(worker);
        worker.setRuns(runs);
        QVERIFY(worker.preparing());
        QTRY_VERIFY_WITH_TIMEOUT(!worker.preparing(), 10000);
        QVERIFY2(worker.errorString().isEmpty(), qPrintable(worker.errorString()));
        QCOMPARE(worker.font(), font);
        QCOMPARE(worker.metrics(), native.metrics());
        QVERIFY(worker.cacheBytes() > 0);
        QVERIFY(Memory::highWater() >= 40LL * 1024 * 1024);
    }
    void asynchronousCapacityFailureClearsPreparedResources()
    {
        GlyphRun item;
        item.setHaloSampling("canvas");
        QCOMPARE(item.maximumRasterScale(), qreal(32));
        auto seed = oneRun("seed", "Seed").first().toMap();
        seed["size"] = 16;
        item.setRuns({seed});
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(firstKey(item.metrics()), QString("seed"));
        QVERIFY(item.cacheBytes() > 0);

        // The request passes the GUI's finite scalar/field validation. Its actual
        // worker-shaped ink at the declared scale exceeds the four-target capacity.
        auto oversized = oneRun("oversized", "W").first().toMap();
        oversized["size"] = 256;
        item.setRuns({oversized});
        QVERIFY(item.preparing());
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().contains("four-target source capacity"),
                 qPrintable(item.errorString()));
        QCOMPARE(item.cacheBytes(), 0);
        QVERIFY(!item.ready());
        QVERIFY(item.metrics().isEmpty());
        // An accepted asynchronous request remains the requested configuration;
        // failure clears rendering resources rather than rolling back that request.
        QCOMPARE(firstKey(item.runs()), QString("oversized"));
    }
    void hiddenCanvasRetiresDerivedCpuLayout()
    {
        GlyphRun item;
        configureCanvas(item);
        item.setRuns(oneRun("seed", "Hidden canvas native layout"));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QVERIFY(!item.metrics().isEmpty());
        QVERIFY(item.cacheBytes() > 0);
        item.setVisible(false);
        QVERIFY(!item.preparing());
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), m_initialBytes, 10000);
        QCOMPARE(item.cacheBytes(), 0);
        QVERIFY(!item.ready());
        QVERIFY2(item.metrics().isEmpty(),
                 "A hidden Canvas retained derived layout after its lease drained");
        QCOMPARE(firstKey(item.runs()), QString("seed"));
    }
    void hiddenConfigurationWaitsForVisibilityBeforePreparing()
    {
        // Qt keeps a windowless root item's effective visibility false after hide.
        // A plain visible parent exercises real item hide/show without any window.
        QQuickItem parent;
        GlyphRun item(&parent);
        configureCanvas(item);
        item.setRuns(oneRun("seed", "Initial visible configuration"));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        item.setVisible(false);
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), m_initialBytes, 10000);
        QSignalSpy preparations(&item, &GlyphRun::preparationChanged);
        auto font = item.font();
        font.setPixelSize(29);
        const auto requested = oneRun("new", "Prepared only after showing");
        item.setRuns(requested);
        item.setFont(font);
        item.setMaximumRasterScale(1.5);
        QTest::qWait(100);
        QCOMPARE(item.runs(), requested);
        QCOMPARE(item.font(), font);
        QCOMPARE(item.maximumRasterScale(), qreal(1.5));
        QCOMPARE(preparations.count(), 0);
        QVERIFY(!item.preparing());
        QCOMPARE(item.cacheBytes(), 0);
        QVERIFY(item.metrics().isEmpty());
        QCOMPARE(Memory::total(), m_initialBytes);
        QVERIFY(!item.ready());
        item.setVisible(true);
        QVERIFY(item.preparing());
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(firstKey(item.metrics()), QString("new"));
        QVERIFY(item.cacheBytes() > 0);
    }
    void canceledRunningAndQueuedGenerationsActuallyDrain()
    {
        auto item = std::make_unique<GlyphRun>();
        configureCanvas(*item);
        item->setRuns(oneRun("seed", "Seed"));
        QTRY_VERIFY_WITH_TIMEOUT(!item->preparing(), 10000);
        QVERIFY2(item->errorString().isEmpty(), qPrintable(item->errorString()));
        const auto beforeWork = Memory::total();
        const auto text = QString(1800, QChar('W'));
        item->setRuns(oneRun("running", text));
        // Observe the real worker's scratch reservation before canceling it.
        // Avoid consuming its GUI completion while waiting for this boundary.
        QElapsedTimer deadline;
        deadline.start();
        while (Memory::total() < beforeWork + 40LL * 1024 * 1024 && deadline.elapsed() < 5000)
            QTest::qSleep(1);
        QVERIFY2(Memory::total() >= beforeWork + 40LL * 1024 * 1024,
                 "The real worker did not enter its running scratch reservation");
        QVERIFY(item->preparing());
        for (int generation = 0; generation < 64; ++generation)
            item->setRuns(oneRun(QString("generation-%1").arg(generation), text));
        item->setVisible(false);
        QVERIFY(!item->preparing());
        QCOMPARE(item->cacheBytes(), 0);
        // Cancellation cannot prematurely erase the queued/running native lease.
        QVERIFY(Memory::total() > m_initialBytes);
        qInfo() << "Canvas native ledger bytes: baseline" << m_initialBytes << "retired"
                << Memory::total() << "highWater" << Memory::highWater();
        item.reset();
        QTRY_COMPARE_WITH_TIMEOUT(Memory::total(), m_initialBytes, 15000);
        qInfo() << "Canvas native ledger bytes after actual drain" << Memory::total();
        QVERIFY(Memory::highWater() <= EchoMusic::GlyphCanvas::NativeBudget);
    }
    void preparationChangedReentryCannotCommitAnOlderGeneration()
    {
        GlyphRun item;
        configureCanvas(item);
        item.setRuns(oneRun("seed", "Seed"));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        bool requestedB = false, staleNotification = false, staleCommit = false;
        quint64 revisionWhenBRequested = 0;
        connect(&item, &GlyphRun::preparationChanged, &item, [&] {
            if (!item.preparing() && !requestedB && firstKey(item.runs()) == "A") {
                staleNotification = firstKey(item.metrics()) != "A";
                requestedB = true;
                item.setRuns(oneRun("B", "New generation العربية 😀"));
                revisionWhenBRequested = item.maskRevision();
            }
        });
        connect(&item, &GlyphRun::layoutChanged, &item, [&] {
            if (requestedB && item.preparing() && firstKey(item.runs()) == "B" &&
                item.maskRevision() != revisionWhenBRequested)
                staleCommit = true;
        });
        item.setRuns(oneRun("A", "First generation office 中文"));
        QTRY_VERIFY_WITH_TIMEOUT(requestedB && !item.preparing() && firstKey(item.metrics()) == "B",
                                 10000);
        QVERIFY2(!staleNotification, "Completion notification exposed the previous layout");
        QVERIFY2(!staleCommit, "Generation A committed after a preparationChanged "
                               "handler requested B");
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        QCOMPARE(firstKey(item.runs()), QString("B"));
    }
    void cancellationNotificationCannotOverwriteAReentrantRequest()
    {
        GlyphRun item;
        configureCanvas(item);
        item.setRuns(oneRun("seed", "Seed"));
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing(), 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
        bool replacingA = false, requestedB = false;
        connect(&item, &GlyphRun::preparationChanged, &item, [&] {
            if (replacingA && !item.preparing() && !requestedB && firstKey(item.runs()) == "A") {
                requestedB = true;
                item.setRuns(oneRun("B", "Newest reentrant request"));
            }
        });
        item.setRuns(oneRun("A", QString(1800, QChar('W'))));
        QVERIFY(item.preparing());
        replacingA = true;
        item.setRuns(oneRun("C", "Earlier outer request"));
        replacingA = false;
        // Coalescing the internal false/true notifications is valid. If a real
        // cancellation notification did run B, the older outer C must not win.
        const QString expected = requestedB ? "B" : "C";
        QCOMPARE(firstKey(item.runs()), expected);
        QTRY_VERIFY_WITH_TIMEOUT(!item.preparing() && firstKey(item.metrics()) == expected, 10000);
        QVERIFY2(item.errorString().isEmpty(), qPrintable(item.errorString()));
    }
};

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    GlyphRunCanvasPreparationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "GlyphRunCanvasPreparationTest.moc"
