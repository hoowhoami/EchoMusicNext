// SPDX-License-Identifier: AGPL-3.0-only
#include "NativeGraph.h"
#include "SignalProfile.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJSEngine>
#include <QFile>
#include <QTemporaryFile>
#include <QtTest>
#include <cstring>
#include <cmath>

using namespace AudioAnalysisTool;
namespace
{
QJsonObject arithmeticGraph()
{
    return QJsonObject{
        {"buffers", QJsonArray{QJsonObject{{"size", 16}, {"usage", 128 | 8 | 4}},
                               QJsonObject{{"size", 16}, {"usage", 1 | 8}}}},
        {"pipelines",
         QJsonArray{QJsonObject{
             {"entryPoint", "main"},
             {"code",
              "@group(0) @binding(0) var<storage,read_write> a:array<f32>;"
              "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) i:vec3<u32>)"
              "{a[i.x]=a[i.x]*2.0+1.0;}"}}}},
        {"groups",
         QJsonArray{QJsonObject{
             {"pipeline", 0},
             {"index", 0},
             {"entries", QJsonArray{QJsonObject{
                             {"binding", 0}, {"buffer", 0}, {"offset", 0}, {"size", 16}}}}}}},
        {"writes",
         QJsonArray{QJsonObject{
             {"buffer", 0}, {"offset", 0}, {"size", 16}, {"input", "values"}, {"inputOffset", 0}}}},
        {"batches", QJsonArray{QJsonArray{QJsonObject{{"kind", "dispatch"},
                                                      {"pipeline", 0},
                                                      {"groups", QJsonArray{0}},
                                                      {"count", QJsonArray{4, 1, 1}}},
                                          QJsonObject{{"kind", "copy"},
                                                      {"from", 0},
                                                      {"fromOffset", 0},
                                                      {"to", 1},
                                                      {"toOffset", 0},
                                                      {"size", 16}}}}},
        {"outputs", QJsonArray{QJsonObject{{"buffer", 1}, {"size", 16}}}}};
}
} // namespace
class NativeGraphTest final : public QObject
{
    Q_OBJECT
private slots:
    void nativeTypedSortCompatibility()
    {
        QJSEngine engine;
        engine.evaluate(
            "Float32Array.prototype.sort=undefined;Float64Array.prototype.sort=undefined;");
        QFile file(QStringLiteral(ANALYSIS_TYPED_ARRAY_SCRIPT));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto loaded = engine.evaluate(QString::fromUtf8(file.readAll()));
        QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
        const auto result = engine.evaluate(
            "var values=new Float32Array([NaN,10,2,-0,0,-1,NaN]);"
            "var same=values.sort()===values;"
            "same && values[0]===-1 && 1/values[1]===-Infinity && 1/values[2]===Infinity"
            "&&values[3]===2&&values[4]===10&&isNaN(values[5])&&isNaN(values[6])");
        QVERIFY2(result.toBool(), qPrintable(result.toString()));
        QCOMPARE(engine.evaluate("new Float64Array([1,9,2]).sort(function(a,b){return b-a})[0]")
                     .toNumber(),
                 9.);
        QVERIFY(engine.evaluate("new Float32Array(1).sort(null)").isError());
    }
    void nativeArrayBufferTransport()
    {
        QJSEngine engine;
        const auto value = engine.evaluate("new Uint8Array([0, 255, 128, 17]).buffer");
        const auto variant = value.toVariant();
        qInfo() << "ArrayBuffer native transport" << variant.typeName()
                << variant.toByteArray().toHex();
        QCOMPARE(variant.toByteArray(), QByteArray::fromHex("00ff8011"));
        engine.globalObject().setProperty("payload",
                                          engine.toScriptValue(QByteArray::fromHex("00ff8011")));
        QCOMPARE(engine.evaluate("new Uint8Array(payload)[1]").toInt(), 255);
    }
    void sourceGraph_data()
    {
        QTest::addColumn<QString>("model");
        QTest::newRow("beat-this-100") << QStringLiteral("beat_this");
        QTest::newRow("htdemucs-full") << QStringLiteral("htdemucs");
    }
    void sourceGraph()
    {
        QFETCH(QString, model);
        QString error;
        const auto graph =
            makeSourceGraph(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT),
                            QStringLiteral(ANALYSIS_MODEL_DIRECTORY) + '/' + model + "-wgsl.bin",
                            model, 100, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(graph.value("model").toString(), model);
        QVERIFY(graph.value("pipelines").toArray().size() >= 6);
        const auto output = graph.value("outputs").toArray().last().toObject();
        QCOMPARE(output.value("size").toInt(), model == "beat_this" ? 800 : 5505024);
    }
    void realBeatThis()
    {
        QString error;
        const auto file = QStringLiteral(ANALYSIS_MODEL_DIRECTORY "/beat_this-wgsl.bin");
        const auto graph = makeSourceGraph(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT), file,
                                           "beat_this", 100, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QByteArray mel(100 * 128 * 4, Qt::Uninitialized);
        for (int index = 0; index < 100 * 128; ++index) {
            const float value = float(3 + 2 * std::sin(index * .017));
            std::memcpy(mel.data() + 4 * index, &value, 4);
        }
        const auto result = executeGraph(graph, file, {{"mel", mel}});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.data.size(), 800);
        double variation = 0;
        float last = 0;
        for (int index = 0; index < 200; ++index) {
            float value;
            std::memcpy(&value, result.data.constData() + 4 * index, 4);
            QVERIFY(std::isfinite(value));
            variation += std::abs(value - last);
            last = value;
        }
        QVERIFY(variation > 1);
        qInfo() << "real BeatThis" << result.backend << result.adapter << "buffers"
                << result.allocatedBytes;
    }
    void uploadMustVerifyTheSnapshotActuallyRead()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QCOMPARE(file.write(QByteArray::fromHex("0000803f")), 4);
        file.flush();
        const QJsonObject graph{
            {"model", "beat_this"},
            {"buffers", QJsonArray{QJsonObject{{"size", 4}, {"usage", 8 | 4}},
                                   QJsonObject{{"size", 4}, {"usage", 1 | 8}}}},
            {"writes", QJsonArray{QJsonObject{
                           {"buffer", 0}, {"offset", 0}, {"size", 4}, {"modelOffset", 0}}}},
            {"batches", QJsonArray{QJsonArray{QJsonObject{{"kind", "copy"},
                                                          {"from", 0},
                                                          {"fromOffset", 0},
                                                          {"to", 1},
                                                          {"toOffset", 0},
                                                          {"size", 4}}}}},
            {"outputs", QJsonArray{QJsonObject{{"buffer", 1}, {"size", 4}}}}};
        const auto result = executeGraph(graph, file.fileName(), {});
        QVERIFY2(result.error.startsWith("ModelInvalid"), qPrintable(result.error));
        QVERIFY(result.data.isEmpty());
    }
    void residentSessionChangesInputsWithoutRebuildingResources()
    {
        const auto graph = arithmeticGraph();
        ComputeSession session;
        const float a[] = {1, 2, 3, 4}, b[] = {-1, 0, 10, 20};
        const auto run = [&](const float *input) {
            return session.execute(
                graph, {{"values", QByteArray(reinterpret_cast<const char *>(input), 16)}});
        };
        const auto first = run(a);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QCOMPARE(session.statistics().deviceCreations, quint64(1));
        const auto second = run(b);
        QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
        float output[4];
        std::memcpy(output, second.data.constData(), 16);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(output[i], b[i] * 2 + 1);
        const auto stats = session.statistics();
        QCOMPARE(stats.deviceCreations, quint64(1));
        QCOMPARE(stats.pipelineCreations, quint64(1));
        QCOMPARE(stats.graphPreparations, quint64(1));
        QCOMPARE(stats.executions, quint64(2));
        QVERIFY(stats.liveBufferBytes > 0);
        session.close();
        QCOMPARE(session.statistics().deviceReleases, quint64(1));
        QCOMPARE(session.statistics().liveBufferBytes, qint64(0));
    }
    void residentRejectsChangedDescriptorsAndReleasesResources()
    {
        ComputeSession session;
        auto graph = arithmeticGraph();
        const float values[] = {1, 2, 3, 4};
        const QMap<QString, QByteArray> inputs{
            {"values", QByteArray(reinterpret_cast<const char *>(values), 16)}};
        QVERIFY(session.execute(graph, inputs).error.isEmpty());
        auto changed = graph;
        auto buffers = changed["buffers"].toArray();
        auto first = buffers[0].toObject();
        first.insert("size", 32);
        buffers[0] = first;
        changed.insert("buffers", buffers);
        const auto failed = session.execute(changed, inputs);
        QVERIFY2(failed.error.startsWith("GraphChanged"), qPrintable(failed.error));
        QCOMPARE(session.statistics().deviceCreations, quint64(1));
        QCOMPARE(session.statistics().deviceReleases, quint64(1));
        QCOMPARE(session.statistics().liveBufferBytes, qint64(0));
        QCOMPARE(session.statistics().livePipelines, 0);
        QCOMPARE(session.statistics().liveBindGroups, 0);
    }
    void residentCancelAndInvalidInputsRetireActualAllocations()
    {
        ComputeSession session;
        const float values[] = {1, 2, 3, 4};
        int checks = 0;
        const auto result = session.execute(
            arithmeticGraph(), {{"values", QByteArray(reinterpret_cast<const char *>(values), 16)}},
            [&] { return ++checks >= 5; });
        QVERIFY2(result.error.startsWith("Canceled"), qPrintable(result.error));
        const auto stats = session.statistics();
        QCOMPARE(stats.deviceCreations, quint64(1));
        QCOMPARE(stats.deviceReleases, quint64(1));
        QVERIFY(stats.peakBufferBytes > 0);
        QCOMPARE(stats.liveBufferBytes, qint64(0));
        QCOMPARE(stats.livePipelines, 0);
        QCOMPARE(stats.liveBindGroups, 0);
        ComputeSession invalid;
        QVERIFY(!invalid.execute(arithmeticGraph(), {}).error.isEmpty());
        QCOMPARE(invalid.statistics().liveBufferBytes, qint64(0));
        QCOMPARE(invalid.statistics().deviceReleases, quint64(1));
    }
    void residentCacheEvictsLruWithoutRejectingAValidNewShape_data()
    {
        QTest::addColumn<qint64>("budget");
        QTest::addColumn<int>("retained");
        QTest::newRow("buffer-byte-limit") << qint64(64) << 2;
        QTest::newRow("four-graph-limit") << qint64(1024LL * 1024 * 1024) << 4;
    }
    void residentCacheEvictsLruWithoutRejectingAValidNewShape()
    {
        QFETCH(qint64, budget);
        QFETCH(int, retained);
        ComputeSession session({}, {}, budget);
        const float values[] = {1, 2, 3, 4};
        const QMap<QString, QByteArray> input{
            {"values", QByteArray(reinterpret_cast<const char *>(values), 16)}};
        for (int i = 1; i <= 6; ++i) {
            auto graph = arithmeticGraph();
            graph.insert("frames", i);
            const auto result = session.execute(graph, input);
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(session.statistics().liveBufferBytes, qint64(std::min(i, retained) * 32));
            QVERIFY(session.statistics().residentGraphs <= retained);
            QCOMPARE(session.statistics().deviceCreations, quint64(1));
            QCOMPARE(session.statistics().pipelineCreations, quint64(1));
        }
        const auto preparations = session.statistics().graphPreparations;
        auto cached = arithmeticGraph();
        cached.insert("frames", 6);
        QVERIFY(session.execute(cached, input).error.isEmpty());
        QCOMPARE(session.statistics().graphPreparations, preparations);
        cached.insert("frames", 1);
        QVERIFY(session.execute(cached, input).error.isEmpty());
        QCOMPARE(session.statistics().graphPreparations, preparations + 1);
        session.close();
        QCOMPARE(session.statistics().liveBufferBytes, qint64(0));
    }
    void residentRejectsHardBudgetsBeforeAllocatingGpuMemory()
    {
        ComputeSession session;
        auto graph = arithmeticGraph();
        QJsonArray buffers;
        for (int i = 0; i < 5; ++i)
            buffers.append(QJsonObject{{"size", 256 * 1024 * 1024}, {"usage", 128 | 8 | 4}});
        graph.insert("buffers", buffers);
        QVERIFY(!session.execute(graph, {}).error.isEmpty());
        QCOMPARE(session.statistics().deviceCreations, quint64(0));
        QCOMPARE(session.statistics().liveBufferBytes, qint64(0));
        ComputeSession single;
        buffers[0] = QJsonObject{{"size", 256 * 1024 * 1024 + 4}, {"usage", 128 | 8 | 4}};
        graph.insert("buffers", buffers);
        QVERIFY(!single.execute(graph, {}).error.isEmpty());
        QCOMPARE(single.statistics().deviceCreations, quint64(0));
    }
    void residentRealSourceModels_data()
    {
        QTest::addColumn<QString>("model");
        QTest::newRow("beat_this") << QString("beat_this");
        QTest::newRow("htdemucs") << QString("htdemucs");
    }
    void residentRealSourceModels()
    {
        QFETCH(QString, model);
        const auto path = QStringLiteral(ANALYSIS_MODEL_DIRECTORY) + '/' + model + "-wgsl.bin";
        ComputeSession session(model, path);
        QString error;
        const auto graph =
            session.sourceGraph(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT), model, 100, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const bool beat = model == "beat_this";
        const int samples = beat ? 100 * 128 : 172032;
        const auto inputs = [&](bool changed) {
            QMap<QString, QByteArray> values;
            for (const auto &channel : beat ? QStringList{"mel"} : QStringList{"left", "right"}) {
                QByteArray data(samples * 4, Qt::Uninitialized);
                for (int i = 0; i < samples; ++i) {
                    const double phase = channel == "right" ? .7 : 0;
                    const float value =
                        beat
                            ? float((changed ? 2.1 : 3) + 2 * std::sin(i * (changed ? .023 : .017)))
                            : float((changed ? .08 : .12) * std::sin(i * .013 + phase) +
                                    .03 * std::sin(i * (changed ? .081 : .071)));
                    std::memcpy(data.data() + 4 * i, &value, 4);
                }
                values.insert(channel, data);
            }
            return values;
        };
        const auto a = inputs(false), b = inputs(true);
        const auto first = session.execute(graph, a);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        const auto prepared = session.statistics();
        const auto second = session.execute(graph, b);
        QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
        QVERIFY(first.data != second.data);
        const auto again = session.execute(graph, a);
        QVERIFY2(again.error.isEmpty(), qPrintable(again.error));
        QCOMPARE(again.data, first.data);
        const auto stats = session.statistics();
        QCOMPARE(stats.deviceCreations, quint64(1));
        QCOMPARE(stats.graphPreparations, quint64(1));
        QCOMPARE(stats.executions, quint64(3));
        QCOMPARE(stats.pipelineCreations, prepared.pipelineCreations);
        QCOMPARE(stats.immutableUploadBytes, prepared.immutableUploadBytes);
        QCOMPARE(stats.modelVerifications, quint64(1));
        QVERIFY(stats.modelUploadBytes > 0);
        QCOMPARE(stats.modelUploadBytes, prepared.modelUploadBytes);
        QVERIFY(stats.peakBufferBytes <= 1024LL * 1024 * 1024);
        const auto fresh = executeGraph(graph, path, b);
        QVERIFY2(fresh.error.isEmpty(), qPrintable(fresh.error));
        QCOMPARE(fresh.data, second.data);
        const auto directory = qEnvironmentVariable("FOLIA_RESIDENT_OUTPUT_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            const auto save = [&](const QString &name, const QByteArray &data) {
                QFile file(directory + '/' + model + '-' + name);
                QVERIFY(file.open(QIODevice::WriteOnly));
                QCOMPARE(file.write(data), data.size());
            };
            for (auto it = a.begin(); it != a.end(); ++it)
                save("a-" + it.key() + ".f32", it.value());
            for (auto it = b.begin(); it != b.end(); ++it)
                save("b-" + it.key() + ".f32", it.value());
            save("a-native.f32", first.data);
            save("b-native.f32", second.data);
            save("again-native.f32", again.data);
            save("resident.json",
                 QJsonDocument(QJsonObject{{"backend", first.backend},
                                           {"adapter", first.adapter},
                                           {"deviceCreations", qint64(stats.deviceCreations)},
                                           {"pipelineCreations", qint64(stats.pipelineCreations)},
                                           {"executions", qint64(stats.executions)},
                                           {"modelVerifications", qint64(stats.modelVerifications)},
                                           {"modelUploadBytes", qint64(stats.modelUploadBytes)},
                                           {"peakBufferBytes", stats.peakBufferBytes}})
                     .toJson());
        }
        session.close();
        QCOMPARE(session.statistics().liveBufferBytes, qint64(0));
        QCOMPARE(session.statistics().liveBindGroups, 0);
        QCOMPARE(session.statistics().livePipelines, 0);
        QCOMPARE(session.statistics().deviceReleases, quint64(1));
    }
    void residentUsesOneImmutableSnapshotAcrossShapesAndFileReplacement()
    {
        QFile source(QStringLiteral(ANALYSIS_MODEL_DIRECTORY "/beat_this-wgsl.bin"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QTemporaryFile replaced;
        QVERIFY(replaced.open());
        const auto bytes = source.readAll();
        QCOMPARE(replaced.write(bytes), bytes.size());
        replaced.flush();
        ComputeSession session("beat_this", replaced.fileName());
        QString error;
        auto graph =
            session.sourceGraph(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT), "beat_this", 100, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto first = session.execute(graph, {{"mel", QByteArray(100 * 128 * 4, 0)}});
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        const auto before = session.statistics();
        QVERIFY(replaced.seek(0));
        QCOMPARE(replaced.write(QByteArray(1, char(bytes[0] ^ 1))), qint64(1));
        replaced.flush();
        graph =
            session.sourceGraph(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT), "beat_this", 101, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto second = session.execute(graph, {{"mel", QByteArray(101 * 128 * 4, 0)}});
        QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
        QCOMPARE(session.statistics().modelVerifications, quint64(1));
        QCOMPARE(session.statistics().modelUploadBytes, before.modelUploadBytes);
        QCOMPARE(session.statistics().pipelineCreations, before.pipelineCreations);
        ComputeSession fresh("beat_this", replaced.fileName());
        fresh.sourceGraph(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT), "beat_this", 100, &error);
        QVERIFY2(error.contains("SHA256"), qPrintable(error));
        QCOMPARE(fresh.statistics().deviceCreations, quint64(0));
    }
    void actualDrumProfileReusesOneDeviceAcrossRealSegments()
    {
        QByteArray left(4 * 44100 * 4, Qt::Uninitialized), right(left.size(), Qt::Uninitialized);
        for (int i = 0; i < 4 * 44100; ++i) {
            const float a = float(.1 * std::sin(i * .031)), b = float(.08 * std::sin(i * .041));
            std::memcpy(left.data() + 4 * i, &a, 4);
            std::memcpy(right.data() + 4 * i, &b, 4);
        }
        QString error;
        ComputeStatistics stats;
        const auto runtime = QFileInfo(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT)).absolutePath();
        const auto result = analysePcm("drumHits", QStringLiteral(ANALYSIS_MODEL_DIRECTORY),
                                       runtime, left, right, &error, {}, {}, {}, &stats);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(result.value("complete").toBool());
        QCOMPARE(stats.deviceCreations, quint64(1));
        QCOMPARE(stats.deviceReleases, quint64(1));
        QCOMPARE(stats.graphPreparations, quint64(1));
        QCOMPARE(stats.executions, quint64(2));
        QCOMPARE(stats.modelVerifications, quint64(1));
        QCOMPARE(stats.liveBufferBytes, qint64(0));
        QCOMPARE(stats.livePipelines, 0);
        QCOMPARE(stats.liveBindGroups, 0);
    }
    void actualDrumCheckpointCancelAndResumeIsBitExact()
    {
        QByteArray left(4 * 44100 * 4, Qt::Uninitialized), right(left.size(), Qt::Uninitialized);
        for (int i = 0; i < 4 * 44100; ++i) {
            const float a = float(.1 * std::sin(i * .031)), b = float(.08 * std::sin(i * .041));
            std::memcpy(left.data() + 4 * i, &a, 4);
            std::memcpy(right.data() + 4 * i, &b, 4);
        }
        QString error;
        const auto runtime =
            qEnvironmentVariable("FOLIA_RESUME_RUNTIME",
                                 QFileInfo(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT)).absolutePath());
        ComputeStatistics fullStats, firstStats, resumedStats;
        const auto full = analysePcm("drumHits", QStringLiteral(ANALYSIS_MODEL_DIRECTORY), runtime,
                                     left, right, &error, {}, {}, {}, &fullStats);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        DrumCheckpoint checkpoint;
        checkpoint.identity = "authorized-account:actual-source:profile";
        QByteArray saved;
        QJsonObject partial;
        checkpoint.save = [&](const QByteArray &bytes, const QJsonObject &head) {
            saved = bytes;
            partial = head;
            return QString{};
        };
        bool stop = false;
        const auto stopped = analysePcm(
            "drumHits", QStringLiteral(ANALYSIS_MODEL_DIRECTORY), runtime, left, right, &error,
            [&](int done, int, double) { stop = done == 1; }, {}, [&] { return stop; }, &firstStats,
            &checkpoint);
        QVERIFY2(error.contains("Canceled"), qPrintable(error));
        QVERIFY(stopped.isEmpty());
        QCOMPARE(firstStats.executions, quint64(1));
        QCOMPARE(firstStats.liveBufferBytes, qint64(0));
        QVERIFY(saved.size() > 140);
        QVERIFY(saved.size() <= 2 * 1024 * 1024 + 140);
        QVERIFY(!partial.value("complete").toBool());
        checkpoint.input = saved;
        checkpoint.save = {};
        bool replayed = false;
        const auto resumed = analysePcm(
            "drumHits", QStringLiteral(ANALYSIS_MODEL_DIRECTORY), runtime, left, right, &error, {},
            [&](QJsonObject head) {
                replayed = true;
                QCOMPARE(head, partial);
            },
            {}, &resumedStats, &checkpoint);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(replayed);
        QCOMPARE(resumedStats.executions, fullStats.executions - firstStats.executions);
        QCOMPARE(resumed, full);
        QCOMPARE(resumedStats.liveBufferBytes, qint64(0));
        // Actual PCM, account/source identity, runtime and checksum mismatches never execute a GPU.
        const auto reject = [&](const QByteArray &pcm, const QString &directory) {
            ComputeStatistics stats;
            const auto result =
                analysePcm("drumHits", QStringLiteral(ANALYSIS_MODEL_DIRECTORY), directory, pcm,
                           right, &error, {}, {}, {}, &stats, &checkpoint);
            QVERIFY2(error.startsWith("ResumeInvalid"), qPrintable(error));
            QVERIFY(result.isEmpty());
            QCOMPARE(stats.executions, quint64(0));
            QCOMPARE(stats.deviceCreations, quint64(0));
        };
        auto changed = left;
        changed[0] = char(changed[0] ^ 1);
        reject(changed, runtime);
        checkpoint.identity += "different-account";
        reject(left, runtime);
        checkpoint.identity = "authorized-account:actual-source:profile";
        checkpoint.input[checkpoint.input.size() - 1] ^= 1;
        reject(left, runtime);
        checkpoint.input = saved;
        QTemporaryDir scripts;
        QVERIFY(scripts.isValid());
        for (const auto &name : {"graph-runtime.js", "signal-runtime.js"})
            QVERIFY(QFile::copy(runtime + '/' + name, scripts.path() + '/' + name));
        QFile edited(scripts.path() + "/signal-runtime.js");
        QVERIFY(edited.open(QIODevice::Append));
        QVERIFY(edited.write("\n// modified runtime\n") > 0);
        edited.close();
        reject(left, scripts.path());
        // A failed/over-budget optimization cannot make otherwise valid whole-track analysis fail.
        checkpoint.input.clear();
        checkpoint.save = [](const QByteArray &, const QJsonObject &) {
            return QString("QuotaExceeded: deliberately unavailable resume storage");
        };
        ComputeStatistics noStorage;
        const auto withoutStorage =
            analysePcm("drumHits", QStringLiteral(ANALYSIS_MODEL_DIRECTORY), runtime, left, right,
                       &error, {}, {}, {}, &noStorage, &checkpoint);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(withoutStorage, full);
        QCOMPARE(noStorage.executions, fullStats.executions);
        QCOMPARE(noStorage.liveBufferBytes, qint64(0));
        const auto evidence = qEnvironmentVariable("FOLIA_RESUME_OUTPUT_DIR");
        if (!evidence.isEmpty()) {
            QDir().mkpath(evidence);
            for (const auto &value : {qMakePair(QString("complete.json"), full),
                                      qMakePair(QString("resumed.json"), resumed),
                                      qMakePair(QString("partial.json"), partial)}) {
                QFile file(evidence + '/' + value.first);
                QVERIFY(file.open(QIODevice::WriteOnly));
                QVERIFY(file.write(QJsonDocument(value.second).toJson(QJsonDocument::Compact)) > 0);
            }
            QFile file(evidence + "/checkpoint.bin");
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(saved), qint64(saved.size()));
        }
    }
    void residentRejectsUnknownResourceDescriptorSemantics_data()
    {
        QTest::addColumn<QString>("table");
        QTest::newRow("mapped-buffer") << QString("buffers");
        QTest::newRow("pipeline-constants") << QString("pipelines");
        QTest::newRow("bind-group-resource") << QString("groups");
    }
    void residentRejectsUnknownResourceDescriptorSemantics()
    {
        QFETCH(QString, table);
        auto graph = arithmeticGraph();
        auto values = graph[table].toArray();
        auto item = values[0].toObject();
        item.insert("unsupportedDescriptor", true);
        values[0] = item;
        graph.insert(table, values);
        ComputeSession session;
        const float data[] = {1, 2, 3, 4};
        const auto result = session.execute(
            graph, {{"values", QByteArray(reinterpret_cast<const char *>(data), 16)}});
        QVERIFY2(result.error.startsWith("GraphInvalid"), qPrintable(result.error));
        QCOMPARE(session.statistics().liveBufferBytes, qint64(0));
    }
    void actualProfileCancellationRetiresTheResidentDevice()
    {
        const QByteArray left(4 * 44100 * 4, 0), right(left.size(), 0);
        bool stop = false;
        QString error;
        ComputeStatistics stats;
        const auto runtime = QFileInfo(QStringLiteral(ANALYSIS_RUNTIME_SCRIPT)).absolutePath();
        const auto result = analysePcm(
            "drumHits", QStringLiteral(ANALYSIS_MODEL_DIRECTORY), runtime, left, right, &error,
            [&](int done, int, double) {
                if (done == 1)
                    stop = true;
            },
            {}, [&] { return stop; }, &stats);
        QVERIFY2(error.contains("Canceled"), qPrintable(error));
        QVERIFY(result.isEmpty());
        QCOMPARE(stats.executions, quint64(1));
        QCOMPARE(stats.deviceCreations, quint64(1));
        QCOMPARE(stats.deviceReleases, quint64(1));
        QCOMPARE(stats.liveBufferBytes, qint64(0));
        QCOMPARE(stats.livePipelines, 0);
        QCOMPARE(stats.liveBindGroups, 0);
    }
    void residentRejectsStaticWritesThatWouldCorruptImmutableWeights()
    {
        const QJsonObject graph{
            {"model", "beat_this"},
            {"frames", 1},
            {"buffers", QJsonArray{QJsonObject{{"size", 4}, {"usage", 4 | 8}},
                                   QJsonObject{{"size", 4}, {"usage", 1 | 8}}}},
            {"writes",
             QJsonArray{
                 QJsonObject{{"buffer", 0}, {"offset", 0}, {"size", 4}, {"modelOffset", 0}},
                 QJsonObject{{"buffer", 0}, {"offset", 0}, {"size", 4}, {"data", "AAAAQA=="}}}},
            {"batches", QJsonArray{QJsonArray{QJsonObject{{"kind", "copy"},
                                                          {"from", 0},
                                                          {"fromOffset", 0},
                                                          {"to", 1},
                                                          {"toOffset", 0},
                                                          {"size", 4}}}}},
            {"outputs", QJsonArray{QJsonObject{{"buffer", 1}, {"size", 4}}}}};
        ComputeSession session("beat_this",
                               QStringLiteral(ANALYSIS_MODEL_DIRECTORY "/beat_this-wgsl.bin"));
        const auto result = session.execute(graph, {});
        QVERIFY2(result.error.startsWith("ModelInvalid"), qPrintable(result.error));
        QVERIFY(result.data.isEmpty());
        QVERIFY(session.statistics().peakBufferBytes > 0);
        QCOMPARE(session.statistics().liveBufferBytes, qint64(0));
        QCOMPARE(session.statistics().deviceReleases, quint64(1));
    }
    void realCompute()
    {
        const auto graph = arithmeticGraph();
        const float input[] = {1, 2, 3, 4};
        const auto result = executeGraph(
            graph, {},
            {{"values", QByteArray(reinterpret_cast<const char *>(input), sizeof input)}});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.backend, QStringLiteral("Metal"));
        QCOMPARE(result.data.size(), 16);
        float actual[4];
        std::memcpy(actual, result.data.constData(), sizeof actual);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(actual[i], input[i] * 2 + 1);
    }
};
QTEST_GUILESS_MAIN(NativeGraphTest)
#include "NativeGraphTest.moc"
