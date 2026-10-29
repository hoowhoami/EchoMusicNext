// SPDX-License-Identifier: AGPL-3.0-only
#include "NativeGraph.h"
#include "ModelSnapshot.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QtEndian>
#include <cstring>

namespace AudioAnalysisTool
{
QJsonObject makeSourceGraphSnapshot(const QString &runtimeScript, const QByteArray &snapshot,
                                    const QString &model, int frames, QString *error)
{
    const auto reject = [error](QString message) {
        *error = std::move(message);
        return QJsonObject();
    };
    if ((model != "beat_this" && model != "htdemucs" && model != "percussive") || frames < 1 ||
        frames > (model == "percussive" ? 60000 : 1500) || snapshot.size() < 16)
        return reject(QStringLiteral("InvalidArgument: model/frames/snapshot"));
    const bool beat = model != "htdemucs";
    const auto bytes = snapshot.size();
    const auto trailer = snapshot.last(16);
    if (trailer.first(8) != "FOLIABTW")
        return reject(QStringLiteral("ModelInvalid: packed trailer"));
    const auto offset = qFromLittleEndian<quint64>(trailer.constData() + 8);
    if (offset == 0 || offset >= quint64(bytes - 16) || quint64(bytes - 16) - offset > 1024 * 1024)
        return reject(QStringLiteral("ModelInvalid: manifest range"));
    const auto manifest =
        QJsonDocument::fromJson(snapshot.sliced(qsizetype(offset), bytes - 16 - qint64(offset)))
            .object();
    if (manifest.value("bytes").toInteger(-1) != qint64(offset))
        return reject(QStringLiteral("ModelInvalid: tensor byte span"));
    QJsonArray rope;
    if (beat) {
        const auto entry = manifest.value("tensors").toObject().value("rope.freqs").toObject();
        const auto at = entry.value("byte_offset").toInteger(-1);
        if (at < 0 || at > qint64(offset) - 64)
            return reject(QStringLiteral("ModelInvalid: rope span"));
        const auto data = snapshot.sliced(at, 64);
        if (data.size() != 64)
            return reject(QStringLiteral("ModelInvalid: rope bytes"));
        for (int index = 0; index < 16; ++index) {
            float value;
            std::memcpy(&value, data.constData() + 4 * index, 4);
            rope.append(double(value));
        }
    }
    QFile script(runtimeScript);
    if (!script.open(QIODevice::ReadOnly) || script.size() > 4 * 1024 * 1024)
        return reject(QStringLiteral("RuntimeUnavailable: graph script size/file"));
    QJSEngine engine;
    engine.globalObject().setProperty("globalThis", engine.globalObject());
    const auto loaded = engine.evaluate(QString::fromUtf8(script.readAll()), runtimeScript);
    if (loaded.isError())
        return reject(QStringLiteral("RuntimeInvalid: ") + loaded.toString());
    const QJsonObject input{{"manifest", manifest}, {"rope", rope}};
    engine.globalObject().setProperty("analysisModel", model);
    engine.globalObject().setProperty(
        "analysisInput", QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)));
    engine.globalObject().setProperty("analysisFrames", frames);
    const auto started = engine.evaluate(QStringLiteral(
        "var analysisResult=null;var analysisFailure=null;"
        "AnalysisGraph.recordModelGraph(analysisModel,JSON.parse(analysisInput),analysisFrames)"
        ".then(function(value){analysisResult=JSON.stringify(value);},"
        "function(error){analysisFailure=String(error)+' '+String(error.stack||'');});"));
    if (started.isError())
        return reject(QStringLiteral("RuntimeInvalid: ") + started.toString());
    QElapsedTimer timer;
    timer.start();
    while (engine.globalObject().property("analysisResult").isNull() &&
           engine.globalObject().property("analysisFailure").isNull() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    const auto failure = engine.globalObject().property("analysisFailure");
    if (!failure.isNull())
        return reject(QStringLiteral("RuntimeInvalid: ") + failure.toString());
    const auto result = engine.globalObject().property("analysisResult");
    if (!result.isString())
        return reject(QStringLiteral("RuntimeInvalid: graph promise did not finish"));
    const auto data = result.toString().toUtf8();
    if (data.size() > 8 * 1024 * 1024)
        return reject(QStringLiteral("QuotaExceeded: graph protocol"));
    const auto graph = QJsonDocument::fromJson(data).object();
    if (graph.isEmpty())
        return reject(QStringLiteral("RuntimeInvalid: empty graph"));
    error->clear();
    return graph;
}
QJsonObject makeSourceGraph(const QString &runtimeScript, const QString &modelFile,
                            const QString &model, int frames, QString *error)
{
    if ((model != "beat_this" && model != "htdemucs" && model != "percussive") || frames < 1 ||
        frames > (model == "percussive" ? 60000 : 1500)) {
        *error = QStringLiteral("InvalidArgument: model/frames");
        return {};
    }
    const auto snapshot =
        readModelSnapshot(model == "htdemucs" ? "htdemucs" : "beat_this", modelFile, error);
    if (!error->isEmpty())
        return {};
    return makeSourceGraphSnapshot(runtimeScript, snapshot, model, frames, error);
}
} // namespace AudioAnalysisTool
