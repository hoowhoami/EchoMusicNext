// SPDX-License-Identifier: AGPL-3.0-only
#include "NativeGraph.h"
#include "ModelSnapshot.h"
#include "SignalProfile.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <cstdio>

using namespace AudioAnalysisTool;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    for (const auto &name : {"model", "model-file", "runtime", "frames", "input", "right", "output",
                             "kind", "models", "checkpoint-key"})
        parser.addOption(
            QCommandLineOption(QString::fromLatin1(name), {}, QStringLiteral("value")));
    parser.addOption(QCommandLineOption("probe"));
    parser.process(app);
    QElapsedTimer timer;
    timer.start();
    QString error;
    if (parser.isSet("probe")) {
        QJsonArray profiles;
        bool anyModel = false;
        QString missing;
        for (const auto &model : {QStringLiteral("beat_this"), QStringLiteral("htdemucs")}) {
            if (parser.isSet("model") && parser.value("model") != model)
                continue;
            readModelSnapshot(model, parser.value("models") + '/' + model + "-wgsl.bin", &error);
            profiles.append(QJsonObject{{"kind", model == "beat_this" ? "beats" : "drumHits"},
                                        {"model", model},
                                        {"modelAvailable", error.isEmpty()},
                                        {"error", error}});
            anyModel |= error.isEmpty();
            if (missing.isEmpty())
                missing = error;
        }
        error = anyModel
                    ? QString{}
                    : (missing.isEmpty() ? QStringLiteral("ModelUnavailable: unsupported model")
                                         : missing);
        if (error.isEmpty())
            for (const auto &name : {"graph-runtime.js", "signal-runtime.js"}) {
                QFile file(parser.value("runtime") + '/' + name);
                if (!file.open(QIODevice::ReadOnly) || file.size() < 1 ||
                    file.size() > 4 * 1024 * 1024) {
                    error = "RuntimeUnavailable: bounded runtime file";
                    break;
                }
            }
        const auto gpu = error.isEmpty() ? probeNativeGpu() : ComputeResult{};
        if (error.isEmpty())
            error = gpu.error;
        const auto data = QJsonDocument(QJsonObject{{"available", error.isEmpty()},
                                                    {"error", error},
                                                    {"backend", gpu.backend},
                                                    {"adapter", gpu.adapter},
                                                    {"cpuFallback", false},
                                                    {"profiles", profiles}})
                              .toJson(QJsonDocument::Compact) +
                          '\n';
        std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
        return error.isEmpty() ? 0 : 1;
    }
    if (parser.isSet("kind")) {
        const auto readPcm = [&error](const QString &name) {
            QFile file(name);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) {
                error = QStringLiteral("InvalidArgument: PCM file budget");
                return QByteArray();
            }
            return file.readAll();
        };
        const auto left = readPcm(parser.value("input"));
        const auto right = parser.isSet("right") ? readPcm(parser.value("right")) : QByteArray();
        DrumCheckpoint checkpoint;
        QString checkpointOutput;
        if (parser.isSet("checkpoint-key") && error.isEmpty()) {
            const auto key = parser.value("checkpoint-key").toLatin1();
            bool validKey = key.size() == 64;
            for (const char value : key)
                validKey &= (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
            const QFileInfo input(parser.value("input"));
            const auto directory = input.canonicalPath();
            if (parser.value("kind") != "drumHits" || !validKey || input.isSymLink() ||
                input.fileName() != "left.f32" || directory.isEmpty()) {
                error = "InvalidArgument: private checkpoint staging/key";
            } else {
                checkpoint.identity = key;
                const auto path = directory + "/drum-resume-input.bin";
                const QFileInfo prior(path);
                if (prior.exists() || prior.isSymLink()) {
                    QFile file(path);
                    if (prior.isSymLink() || !prior.isFile() || !file.open(QIODevice::ReadOnly) ||
                        file.size() < 204 || file.size() > 2 * 1024 * 1024 + 140)
                        error = "ResumeInvalid: private checkpoint input file";
                    else {
                        checkpoint.input = file.read(2 * 1024 * 1024 + 141);
                        if (checkpoint.input.size() < 204 ||
                            checkpoint.input.size() > 2 * 1024 * 1024 + 140)
                            error = "ResumeInvalid: private checkpoint read budget";
                    }
                }
                checkpointOutput = directory + "/drum-resume-output.bin";
                const QFileInfo target(checkpointOutput);
                if (target.isSymLink() || (target.exists() && !target.isFile()))
                    error = "ResumeInvalid: private checkpoint output file";
                checkpoint.save = [checkpointOutput](const QByteArray &bytes,
                                                     const QJsonObject &partial) {
                    // Only a host-selected private PCM directory and fixed file name are accepted.
                    // No stdout path can redirect this publication.
                    QSaveFile file(checkpointOutput);
                    file.setDirectWriteFallback(false);
                    if (!file.open(QIODevice::WriteOnly) ||
                        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
                        file.write(bytes) != bytes.size() || !file.commit())
                        return QString("IoError: checkpoint atomic publication");
                    const auto data =
                        QJsonDocument(
                            QJsonObject{{"type", "checkpoint"},
                                        {"bytes", double(bytes.size())},
                                        {"sha256",
                                         QString::fromLatin1(QCryptographicHash::hash(
                                                                 bytes, QCryptographicHash::Sha256)
                                                                 .toHex())},
                                        {"result", partial}})
                            .toJson(QJsonDocument::Compact) +
                        '\n';
                    std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
                    std::fflush(stdout);
                    return QString{};
                };
            }
        }
        QJsonObject output;
        if (error.isEmpty())
            output = analysePcm(
                parser.value("kind"), parser.value("models"), parser.value("runtime"), left, right,
                &error,
                [](int done, int total, double until) {
                    const auto data = QJsonDocument(QJsonObject{{"type", "progress"},
                                                                {"done", done},
                                                                {"total", total},
                                                                {"until", until}})
                                          .toJson(QJsonDocument::Compact) +
                                      '\n';
                    std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
                    std::fflush(stdout);
                },
                [](QJsonObject result) {
                    const auto data =
                        QJsonDocument(QJsonObject{{"type", "partial"}, {"result", result}})
                            .toJson(QJsonDocument::Compact) +
                        '\n';
                    std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
                    std::fflush(stdout);
                },
                {}, nullptr, parser.isSet("checkpoint-key") ? &checkpoint : nullptr);
        if (error.isEmpty() && !checkpointOutput.isEmpty())
            QFile::remove(checkpointOutput);
        const auto data = QJsonDocument(QJsonObject{{"type", "result"},
                                                    {"ok", error.isEmpty()},
                                                    {"error", error},
                                                    {"result", output},
                                                    {"elapsedMs", timer.elapsed()}})
                              .toJson(QJsonDocument::Compact) +
                          '\n';
        std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
        return error.isEmpty() ? 0 : 1;
    }
    const auto model = parser.value("model");
    const int frames = parser.value("frames").isEmpty() ? 1500 : parser.value("frames").toInt();
    auto graph =
        makeSourceGraph(parser.value("runtime"), parser.value("model-file"), model, frames, &error);
    QMap<QString, QByteArray> inputs;
    const auto read = [&error](const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) {
            error = QStringLiteral("InvalidArgument: input file size/read");
            return QByteArray();
        }
        return file.readAll();
    };
    if (error.isEmpty()) {
        inputs.insert(model == "htdemucs" ? "left" : "mel", read(parser.value("input")));
        if (model == "htdemucs")
            inputs.insert("right", read(parser.value("right")));
    }
    ComputeResult result;
    if (error.isEmpty()) {
        result = executeGraph(graph, parser.value("model-file"), inputs);
        error = result.error;
    }
    if (error.isEmpty()) {
        QSaveFile output(parser.value("output"));
        if (!output.open(QIODevice::WriteOnly) || output.write(result.data) != result.data.size() ||
            !output.commit())
            error = QStringLiteral("IoError: output could not be atomically published");
    }
    const QJsonObject reply{{"ok", error.isEmpty()},
                            {"error", error},
                            {"backend", result.backend},
                            {"adapter", result.adapter},
                            {"allocatedBytes", result.allocatedBytes},
                            {"outputBytes", result.data.size()},
                            {"elapsedMs", timer.elapsed()}};
    const auto data = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
    return error.isEmpty() ? 0 : 1;
}
