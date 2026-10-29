// SPDX-License-Identifier: AGPL-3.0-only
#include "SignalProfile.h"
#include "NativeGraph.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QThread>
#include <QTemporaryFile>
#include <QtEndian>

namespace AudioAnalysisTool
{
namespace
{
constexpr qsizetype ResumeLimit = 2 * 1024 * 1024;
constexpr qsizetype ResumeHeader = 8 + 32 * 4 + 4;
QByteArray hash(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
QByteArray envelope(const QByteArray &identity, const QByteArray &pcm, const QByteArray &runtime,
                    const QByteArray &payload)
{
    QByteArray out = "AUDIORS1";
    out += identity;
    out += pcm;
    out += runtime;
    out += hash(payload);
    const quint32 size = qToBigEndian(quint32(payload.size()));
    out.append(reinterpret_cast<const char *>(&size), 4);
    out += payload;
    return out;
}
} // namespace

class ProfileBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool checkpointEnabled READ checkpointEnabled CONSTANT)
public:
    QJSEngine *engine = nullptr;
    QString models, runtime, graphRuntime;
    QByteArray checkpointIdentity, checkpointPcm, checkpointRuntime;
    std::function<QString(const QByteArray &, const QJsonObject &)> checkpointCallback;
    bool checkpointEnabled() const
    {
        return bool(checkpointCallback);
    }
    Q_INVOKABLE void checkpoint(const QByteArray &payload, const QJSValue &partial)
    {
        if (!checkpointCallback)
            return;
        if (payload.size() < 64 || payload.size() > ResumeLimit) {
            qWarning("Audio analysis resume ignored: checkpoint byte budget");
            return;
        }
        const auto head = partial.toVariant().toJsonObject();
        if (head.value("complete").toBool(true) ||
            QJsonDocument(head).toJson(QJsonDocument::Compact).size() > 1024 * 1024) {
            qWarning("Audio analysis resume ignored: checkpoint partial result budget");
            return;
        }
        const auto error = checkpointCallback(
            envelope(checkpointIdentity, checkpointPcm, checkpointRuntime, payload), head);
        if (!error.isEmpty())
            qWarning().noquote() << "Audio analysis resume ignored:" << error;
    }
    std::unique_ptr<ComputeSession> session;
    std::function<bool()> canceled;
    std::function<void(int, int, double)> callback;
    std::function<void(QJsonObject)> partialCallback;
    Q_INVOKABLE QByteArray compute(const QString &model, int frames, const QByteArray &a,
                                   const QByteArray &b)
    {
        if (canceled && canceled()) {
            session->close();
            engine->throwError(QStringLiteral("Canceled: analysis retired"));
            return {};
        }
        QString error;
        auto graph = session->sourceGraph(graphRuntime, model, frames, &error);
        ComputeResult result;
        if (error.isEmpty()) {
            result = session->execute(graph,
                                      model == "htdemucs"
                                          ? QMap<QString, QByteArray>{{"left", a}, {"right", b}}
                                          : QMap<QString, QByteArray>{{"mel", a}},
                                      canceled);
            error = result.error;
        }
        if (!error.isEmpty()) {
            engine->throwError(error);
            return {};
        }
        return result.data;
    }
    Q_INVOKABLE void progress(int done, int total, double until)
    {
        if (callback)
            callback(done, total, until);
    }
    Q_INVOKABLE void partial(const QJSValue &value)
    {
        if (partialCallback)
            partialCallback(value.toVariant().toJsonObject());
    }
};

QJsonObject analysePcm(const QString &kind, const QString &modelDirectory,
                       const QString &runtimeDirectory, const QByteArray &left,
                       const QByteArray &right, QString *error,
                       std::function<void(int, int, double)> progress,
                       std::function<void(QJsonObject)> partial, std::function<bool()> canceled,
                       ComputeStatistics *statistics, const DrumCheckpoint *checkpoint)
{
    const auto reject = [error](QString value) {
        *error = std::move(value);
        return QJsonObject();
    };
    if (statistics)
        *statistics = {};
    if ((kind != "beats" && kind != "drumHits") || left.size() % 4 || left.size() < 4096 ||
        left.size() > 44100 * 600 * 4 || (kind == "drumHits" && right.size() != left.size()))
        return reject(QStringLiteral("InvalidArgument: PCM kind/byte/duration budget"));
    QFile script(runtimeDirectory + "/signal-runtime.js");
    if (!script.open(QIODevice::ReadOnly) || script.size() > 4 * 1024 * 1024)
        return reject(QStringLiteral("RuntimeUnavailable: signal script"));
    const auto scriptBytes = script.readAll();
    // Freeze the graph runtime used by this invocation too: a resume's runtime key describes
    // exactly these bytes, rather than a path reopened after verification.
    QFile graph(runtimeDirectory + "/graph-runtime.js");
    if (!graph.open(QIODevice::ReadOnly) || graph.size() < 1 || graph.size() > 4 * 1024 * 1024)
        return reject(QStringLiteral("RuntimeUnavailable: graph script"));
    const auto graphBytes = graph.readAll();
    QTemporaryFile graphSnapshot;
    if (!graphSnapshot.open() || graphSnapshot.write(graphBytes) != graphBytes.size() ||
        !graphSnapshot.flush())
        return reject(QStringLiteral("IoError: private runtime snapshot"));
    QByteArray payload;
    const bool resuming = checkpoint && !checkpoint->input.isEmpty();
    QByteArray identity, pcmHash, runtimeHash;
    if (checkpoint) {
        if (kind != "drumHits" || checkpoint->identity.isEmpty() ||
            checkpoint->identity.size() > 4096)
            return reject(QStringLiteral("InvalidArgument: private checkpoint identity"));
        identity = hash(checkpoint->identity);
        QCryptographicHash pcm(QCryptographicHash::Sha256);
        pcm.addData(left);
        pcm.addData(right);
        pcmHash = pcm.result();
        runtimeHash =
            hash(scriptBytes + graphBytes +
                 QByteArray("folia-a8da4437-pcm-v1:htdemucs:"
                            "27d5331a74a4e23b18172b3d59d0378e698fd79bc0d0fed3316da77e3a37d8d1"));
        if (resuming) {
            const auto &input = checkpoint->input;
            if (input.size() < ResumeHeader + 64 || input.size() > ResumeHeader + ResumeLimit ||
                input.first(8) != "AUDIORS1" || input.mid(8, 32) != identity ||
                input.mid(40, 32) != pcmHash || input.mid(72, 32) != runtimeHash ||
                qFromBigEndian<quint32>(input.constData() + 136) !=
                    quint32(input.size() - ResumeHeader))
                return reject(QStringLiteral("ResumeInvalid: checkpoint identity/size"));
            payload = input.sliced(ResumeHeader);
            if (hash(payload) != input.mid(104, 32))
                return reject(QStringLiteral("ResumeInvalid: checkpoint payload SHA256"));
        }
    }
    QJSEngine engine;
    engine.globalObject().setProperty("globalThis", engine.globalObject());
    engine.evaluate("var performance={now:function(){return Date.now();}};");
    const auto loaded = engine.evaluate(QString::fromUtf8(scriptBytes), script.fileName());
    if (loaded.isError())
        return reject(loaded.toString());
    ProfileBridge bridge;
    bridge.engine = &engine;
    bridge.models = modelDirectory;
    bridge.runtime = runtimeDirectory;
    bridge.graphRuntime = graphSnapshot.fileName();
    bridge.checkpointIdentity = identity;
    bridge.checkpointPcm = pcmHash;
    bridge.checkpointRuntime = runtimeHash;
    if (checkpoint)
        bridge.checkpointCallback = checkpoint->save;
    const auto model = kind == "drumHits" ? QString("htdemucs") : QString("beat_this");
    bridge.session =
        std::make_unique<ComputeSession>(model, modelDirectory + '/' + model + "-wgsl.bin");
    // Do not replay a cached partial unless today's actual model snapshot still verifies.
    if (resuming) {
        bridge.session->sourceGraph(bridge.graphRuntime, model, 1500, error);
        if (!error->isEmpty())
            return {};
    }
    bridge.canceled = std::move(canceled);
    struct Retirement
    {
        ComputeSession &session;
        ComputeStatistics *output;
        ~Retirement()
        {
            session.close();
            if (output)
                *output = session.statistics();
        }
    } retirement{*bridge.session, statistics};
    bridge.callback = std::move(progress);
    bridge.partialCallback = std::move(partial);
    QJSEngine::setObjectOwnership(&bridge, QJSEngine::CppOwnership);
    engine.globalObject().setProperty("nativeAnalysis", engine.newQObject(&bridge));
    engine.globalObject().setProperty("leftInput", engine.toScriptValue(left));
    engine.globalObject().setProperty("rightInput", engine.toScriptValue(right));
    engine.globalObject().setProperty("resumeInput", engine.toScriptValue(payload));
    const auto start = engine.evaluate(
        QStringLiteral(
            "var signalResult=null;var "
            "signalFailure=null;AnalysisSignal.%1(nativeAnalysis,leftInput%2)"
            ".then(function(value){signalResult=JSON.stringify(value);},"
            "function(error){signalFailure=String(error)+' '+String(error.stack||'');});")
            .arg(kind == "beats" ? "analyseBeats" : "analyseDrums",
                 kind == "beats" ? "" : ",rightInput,resumeInput"));
    if (start.isError())
        return reject(start.toString());
    QElapsedTimer deadline;
    deadline.start();
    while (engine.globalObject().property("signalResult").isNull() &&
           engine.globalObject().property("signalFailure").isNull() &&
           deadline.elapsed() < 5 * 60 * 1000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    const auto failure = engine.globalObject().property("signalFailure");
    if (!failure.isNull())
        return reject(failure.toString());
    const auto result = engine.globalObject().property("signalResult");
    if (!result.isString())
        return reject(QStringLiteral("Timeout: signal analysis"));
    const auto json = result.toString().toUtf8();
    if (json.size() > 8 * 1024 * 1024)
        return reject(QStringLiteral("QuotaExceeded: analysis result bytes"));
    auto output = QJsonDocument::fromJson(json).object();
    if (output.isEmpty())
        return reject(QStringLiteral("InvalidResult: signal result"));
    output.insert("profile", "folia-a8da4437-pcm-v1");
    error->clear();
    return output;
}
} // namespace AudioAnalysisTool
#include "SignalProfile.moc"
