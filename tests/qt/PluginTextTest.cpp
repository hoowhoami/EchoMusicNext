#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "PluginSdkContract.h"
#include "EchoPluginText.h"
#include "plugins/PluginRuntimeTestFixture.h"
#include <QSemaphore>
#include <QThreadPool>
#include <memory>
#include <thread>

namespace
{
struct TextFixture
{
    Host host;
    QJSValueList handles;
    QString installation;
    EchoPluginContext *scope = nullptr;

    bool activate()
    {
        host.runtime.importDirectory(
            package(host.temporary.path(), "export function activate(ctx) {}\n"));
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 7000) {
            installation = host.runtime.installationForId("test.runtime");
            if (!installation.isEmpty() &&
                host.runtime.state(installation) == PluginRuntime::Active) {
                scope = host.runtime.context(installation);
                return scope != nullptr;
            }
            QTest::qWait(1);
        }
        return false;
    }
    EchoPluginOperation *request(const QStringList &texts, const QString &locale = "en",
                                 EchoPluginContext *presentation = nullptr)
    {
        auto *operation =
            (presentation ? presentation : scope)->text()->segmentWords(texts, locale);
        handles.append(host.engine.newQObject(operation));
        return operation;
    }
};

// Hold the real QtConcurrent queue, without replacing the segmentation worker.
// This makes "still pending" and retirement races independent of CPU speed.
// A separate watchdog releases the gate if a faulty retire() blocks the GUI.
class WorkerGate
{
    struct Signals
    {
        QSemaphore entered, release, stopWatchdog;
    };
    QThreadPool *m_pool = QThreadPool::globalInstance();
    int m_previousMaximum = m_pool->maxThreadCount();
    std::shared_ptr<Signals> m_signals = std::make_shared<Signals>();
    std::thread m_watchdog;
    bool m_ready = false;
    bool m_open = false;

public:
    WorkerGate()
    {
        m_pool->setMaxThreadCount(1);
        m_pool->start([gateState = m_signals] {
            gateState->entered.release();
            gateState->release.acquire();
        });
        m_ready = m_signals->entered.tryAcquire(1, 7000);
        m_watchdog = std::thread([gateState = m_signals] {
            if (!gateState->stopWatchdog.tryAcquire(1, 2500))
                gateState->release.release();
        });
    }
    ~WorkerGate()
    {
        open();
        m_signals->stopWatchdog.release();
        m_watchdog.join();
        m_pool->setMaxThreadCount(m_previousMaximum);
    }
    bool ready() const
    {
        return m_ready;
    }
    void open()
    {
        if (!std::exchange(m_open, true))
            m_signals->release.release();
    }
};

QVariant word(const QString &segment, int index, bool wordLike)
{
    return QVariantMap{{"segment", segment}, {"index", index}, {"isWordLike", wordLike}};
}
} // namespace

class PluginTextTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void contractNegotiatesRealWordSegmentation();
    void availabilityAndEmptyBatch();
    void asynchronousIcuAndUtf16Segments();
    void canonicalLocaleLongerThanFixedBuffer();
    void acceptedBatchAtBothLimits();
    void invalidArguments_data();
    void invalidArguments();
    void pendingLimitAndExplicitCancellation();
    void sourceRevisionCancelsWithoutLateSuccess();
    void accountEpochCancelsWithoutLateSuccess();
    void retirementDrainsWithoutBlockingGui();
    void presentationDestructionDrainsBeforeDeletion();
};

void PluginTextTest::initTestCase()
{
    configureUiTypography();
}

void PluginTextTest::contractNegotiatesRealWordSegmentation()
{
    const auto contract = EchoMusic::pluginSdkContract();
    QCOMPARE(contract.value("features").toArray().contains("text.segmentation"),
             EchoPluginText::available());
    const auto types = contract.value("types").toObject();
    QVERIFY(types.contains("EchoPluginText"));
    bool exposesText = false;
    for (const auto &value :
         types.value("EchoPluginContext").toObject().value("properties").toArray()) {
        const auto property = value.toObject();
        exposesText = exposesText || (property.value("name") == "text" &&
                                      property.value("type") == "EchoMusic::EchoPluginText*");
    }
    QVERIFY(exposesText);
}

void PluginTextTest::availabilityAndEmptyBatch()
{
    TextFixture fixture;
    QVERIFY(fixture.activate());
    QCOMPARE(fixture.scope->text()->property("available").toBool(), EchoPluginText::available());
    auto *operation = fixture.request({});
    if (!EchoPluginText::available()) {
        QCOMPARE(operation->state(), EchoPluginOperation::Failed);
        QCOMPARE(operation->errorCode(), QString("Unavailable"));
        QVERIFY(!operation->result().isValid());
        QVERIFY(EchoPluginText::backend().isEmpty());
        QVERIFY(EchoPluginText::unicodeVersion().isEmpty());
        QVERIFY(fixture.scope->text()->isDrained());
        return;
    }
    QCOMPARE(operation->state(), EchoPluginOperation::Running);
    QTRY_VERIFY(operation->finished());
    QCOMPARE(operation->state(), EchoPluginOperation::Succeeded);
    const auto result = operation->result().toMap();
    QVERIFY(result.contains("lines"));
    QCOMPARE(result.value("lines").toList(), QVariantList());
    QCOMPARE(result.value("backend").toString(), EchoPluginText::backend());
    QCOMPARE(result.value("unicodeVersion").toString(), EchoPluginText::unicodeVersion());
    QVERIFY(result.value("backend").toString().startsWith("icu4c-78."));
    QVERIFY(result.value("unicodeVersion").toString().startsWith("17."));
    QVERIFY(fixture.scope->text()->isDrained());
}

void PluginTextTest::asynchronousIcuAndUtf16Segments()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    WorkerGate gate;
    QVERIFY(gate.ready());
    const QStringList texts{QString::fromUtf8("A😀 B e\u0301, 42!\n"),
                            QString::fromUtf8("👩‍👩‍👧‍👦 x"),
                            QString::fromUtf8("ภาษาไทย"), QString()};
    auto *operation = fixture.request(texts);
    QSignalSpy completed(operation, &EchoPluginOperation::completed);
    bool guiPulse = false;
    QTimer::singleShot(0, &fixture.host.engine, [&guiPulse] { guiPulse = true; });
    QTest::qWait(20);
    QVERIFY(guiPulse);
    QCOMPARE(operation->state(), EchoPluginOperation::Running);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!fixture.scope->text()->isDrained());
    gate.open();
    QTRY_COMPARE(operation->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(completed.count(), 1);
    const auto lines = operation->result().toMap().value("lines").toList();
    QCOMPARE(lines.size(), texts.size());
    // ICU 78.x / Node 24.15.0 Intl.Segmenter golden corpus. Offsets count
    // UTF-16 units: the emoji occupies two, and e + combining acute stays whole.
    const QVariantList latin{word("A", 0, true),   word(QString::fromUtf8("😀"), 1, false),
                             word(" ", 3, false),  word("B", 4, true),
                             word(" ", 5, false),  word(QString::fromUtf8("e\u0301"), 6, true),
                             word(",", 8, false),  word(" ", 9, false),
                             word("42", 10, true), word("!", 12, false),
                             word("\n", 13, false)};
    QCOMPARE(lines[0].toList(), latin);
    const QVariantList emoji{word(QString::fromUtf8("👩‍👩‍👧‍👦"), 0, false),
                             word(" ", 11, false), word("x", 12, true)};
    QCOMPARE(lines[1].toList(), emoji);
    // Dictionary breaking is essential: a generic script run is insufficient.
    const QVariantList thai{word(QString::fromUtf8("ภาษา"), 0, true),
                            word(QString::fromUtf8("ไทย"), 4, true)};
    QCOMPARE(lines[2].toList(), thai);
    QCOMPARE(lines[3].toList(), QVariantList());
    for (qsizetype line = 0; line < lines.size(); ++line) {
        QString joined;
        for (const auto &value : lines[line].toList()) {
            const auto token = value.toMap();
            QCOMPARE(token.value("index").metaType().id(), QMetaType::Int);
            QCOMPARE(token.value("isWordLike").metaType().id(), QMetaType::Bool);
            QCOMPARE(token.value("index").toInt(), joined.size());
            joined += token.value("segment").toString();
        }
        QCOMPARE(joined, texts[line]);
    }
    QVERIFY(fixture.scope->text()->isDrained());
}

void PluginTextTest::canonicalLocaleLongerThanFixedBuffer()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    // This 50-character tag canonicalizes to 157 bytes in ICU 78.3. A
    // ULOC_FULLNAME_CAPACITY buffer receives a successful warning but no NUL.
    auto *operation =
        fixture.request({"word"}, "en-u-kb-kc-kh-kn-kk-kf-upper-ca-gregory-ka-shifted");
    QTRY_COMPARE(operation->state(), EchoPluginOperation::Succeeded);
    const auto lines = operation->result().toMap().value("lines").toList();
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines[0].toList(), QVariantList{word("word", 0, true)});
}

void PluginTextTest::acceptedBatchAtBothLimits()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    const QString text(1024, u'a');
    QStringList texts;
    for (int i = 0; i < 64; ++i)
        texts.append(text);
    auto *operation = fixture.request(texts);
    QTRY_COMPARE(operation->state(), EchoPluginOperation::Succeeded);
    const auto lines = operation->result().toMap().value("lines").toList();
    QCOMPARE(lines.size(), 64);
    for (const auto &line : lines)
        QCOMPARE(line.toList(), QVariantList{word(text, 0, true)});
}

void PluginTextTest::invalidArguments_data()
{
    QTest::addColumn<QStringList>("texts");
    QTest::addColumn<QString>("locale");
    QTest::addColumn<QString>("errorCode");
    QStringList tooMany;
    for (int i = 0; i < 65; ++i)
        tooMany.append(QString());
    QTest::newRow("65-texts") << tooMany << QString("en") << QString("InvalidArgument");
    QTest::newRow("65537-units") << QStringList{QString(65537, u'a')} << QString("en")
                                 << QString("InvalidArgument");
    QTest::newRow("surrogates-count-twice") << QStringList{QString::fromUtf8("😀").repeated(32769)}
                                            << QString("en") << QString("InvalidArgument");
    QTest::newRow("locale-underscore")
        << QStringList{"word"} << QString("en_US") << QString("InvalidArgument");
    QTest::newRow("locale-too-long")
        << QStringList{"word"} << QString(65, u'a') << QString("InvalidArgument");
    QTest::newRow("malformed-bcp47")
        << QStringList{"word"} << QString("en--US") << QString("TextSegmentationError");
}

void PluginTextTest::invalidArguments()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    QFETCH(QStringList, texts);
    QFETCH(QString, locale);
    QFETCH(QString, errorCode);
    TextFixture fixture;
    QVERIFY(fixture.activate());
    auto *operation = fixture.request(texts, locale);
    QTRY_VERIFY(operation->finished());
    QCOMPARE(operation->state(), EchoPluginOperation::Failed);
    QCOMPARE(operation->errorCode(), errorCode);
    QVERIFY(!operation->errorMessage().isEmpty());
    QVERIFY(!operation->result().isValid());
    QTRY_VERIFY(fixture.scope->text()->isDrained());
}

void PluginTextTest::pendingLimitAndExplicitCancellation()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    WorkerGate gate;
    QVERIFY(gate.ready());
    auto *first = fixture.request({"first"});
    auto *second = fixture.request({"second"});
    QSignalSpy firstCompleted(first, &EchoPluginOperation::completed);
    QSignalSpy secondCompleted(second, &EchoPluginOperation::completed);
    auto *busy = fixture.request({"third"});
    QCOMPARE(busy->state(), EchoPluginOperation::Failed);
    QCOMPARE(busy->errorCode(), QString("Busy"));
    QVERIFY(!busy->result().isValid());
    first->cancel();
    first->cancel();
    QCOMPARE(first->state(), EchoPluginOperation::Canceled);
    QCOMPARE(firstCompleted.count(), 1);
    QVERIFY(!fixture.scope->text()->isDrained());
    // Cancellation does not free a worker slot before its real drain.
    QCOMPARE(fixture.request({"still busy"})->errorCode(), QString("Busy"));
    QSignalSpy drained(fixture.scope->text(), &EchoPluginText::drained);
    gate.open();
    QTRY_VERIFY(fixture.scope->text()->isDrained());
    QCOMPARE(first->state(), EchoPluginOperation::Canceled);
    QCOMPARE(firstCompleted.count(), 1);
    QVERIFY(!first->result().isValid());
    QCOMPARE(second->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(secondCompleted.count(), 1);
    QCOMPARE(drained.count(), 1);
    auto *fresh = fixture.request({"fresh"});
    QTRY_COMPARE(fresh->state(), EchoPluginOperation::Succeeded);
}

void PluginTextTest::sourceRevisionCancelsWithoutLateSuccess()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    WorkerGate gate;
    QVERIFY(gate.ready());
    auto *operation = fixture.request({"old track"});
    QSignalSpy completed(operation, &EchoPluginOperation::completed);
    const auto previous = fixture.host.playback.sourceRevision();
    fixture.host.playback.stop();
    QVERIFY(fixture.host.playback.sourceRevision() > previous);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    QVERIFY(!fixture.scope->text()->isDrained());
    gate.open();
    QTRY_VERIFY(fixture.scope->text()->isDrained());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    QVERIFY(!operation->result().isValid());
    auto *fresh = fixture.request({"new track"});
    QTRY_COMPARE(fresh->state(), EchoPluginOperation::Succeeded);
}

void PluginTextTest::accountEpochCancelsWithoutLateSuccess()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    WorkerGate gate;
    QVERIFY(gate.ready());
    auto *operation = fixture.request({"old account"});
    QSignalSpy completed(operation, &EchoPluginOperation::completed);
    const auto previous = fixture.scope->accountEpoch();
    QVERIFY(QMetaObject::invokeMethod(&fixture.host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QVERIFY(fixture.scope->accountEpoch() > previous);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    gate.open();
    QTRY_VERIFY(fixture.scope->text()->isDrained());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    QVERIFY(!operation->result().isValid());
    QVERIFY(fixture.scope->active());
    auto *fresh = fixture.request({"new account"});
    QTRY_COMPARE(fresh->state(), EchoPluginOperation::Succeeded);
}

void PluginTextTest::retirementDrainsWithoutBlockingGui()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    WorkerGate gate;
    QVERIFY(gate.ready());
    auto *operation = fixture.request({"retiring"});
    QSignalSpy completed(operation, &EchoPluginOperation::completed);
    QPointer<EchoPluginContext> retired = fixture.scope;
    QPointer<EchoPluginText> text = fixture.scope->text();
    QSignalSpy drained(text, &EchoPluginText::drained);
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(fixture.host.runtime.setEnabled(fixture.installation, false));
    QVERIFY2(elapsed.elapsed() < 1000, "Retirement blocked the GUI waiting for a queued worker");
    QVERIFY(retired && !retired->active());
    QCOMPARE(fixture.host.runtime.state(fixture.installation), PluginRuntime::Deactivating);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    QVERIFY(text && !text->isDrained());
    auto *closed = fixture.request({"late request"});
    QCOMPARE(closed->state(), EchoPluginOperation::Failed);
    QCOMPARE(closed->errorCode(), QString("ScopeClosed"));
    gate.open();
    QTRY_COMPARE(fixture.host.runtime.state(fixture.installation), PluginRuntime::Disabled);
    QTRY_VERIFY(retired.isNull());
    QVERIFY(text.isNull());
    QCOMPARE(drained.count(), 1);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    QVERIFY(!operation->result().isValid());
}

void PluginTextTest::presentationDestructionDrainsBeforeDeletion()
{
    if (!EchoPluginText::available())
        QSKIP("ICU segmentation is absent; availabilityAndEmptyBatch verifies Unavailable");
    TextFixture fixture;
    QVERIFY(fixture.activate());
    auto owner = std::make_unique<QObject>();
    owner->setProperty("active", true);
    QPointer<EchoPluginContext> presentation =
        fixture.scope->createPresentation(owner.get(), "text-test");
    QVERIFY(presentation);
    WorkerGate gate;
    QVERIFY(gate.ready());
    auto *operation = fixture.request({"presentation"}, "en", presentation);
    QSignalSpy completed(operation, &EchoPluginOperation::completed);
    owner.reset();
    QVERIFY(presentation && !presentation->active());
    QVERIFY(!presentation->text()->isDrained());
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    gate.open();
    QTRY_VERIFY(presentation.isNull());
    QCOMPARE(completed.count(), 1);
    QCOMPARE(operation->state(), EchoPluginOperation::Canceled);
    QVERIFY(!operation->result().isValid());
    QVERIFY(fixture.scope->active());
}

QTEST_MAIN(PluginTextTest)
#include "PluginTextTest.moc"
