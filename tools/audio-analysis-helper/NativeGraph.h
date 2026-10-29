// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <functional>
#include <memory>

namespace AudioAnalysisTool
{
struct ComputeResult
{
    QByteArray data;
    QString backend;
    QString adapter;
    QString error;
    qint64 allocatedBytes = 0;
};
struct ComputeStatistics
{
    quint64 deviceCreations = 0, deviceReleases = 0, pipelineCreations = 0;
    quint64 graphPreparations = 0, executions = 0, modelVerifications = 0;
    quint64 immutableUploadBytes = 0, modelUploadBytes = 0, inputUploadBytes = 0;
    qint64 liveBufferBytes = 0, peakBufferBytes = 0;
    int residentGraphs = 0, liveBuffers = 0, livePipelines = 0, liveBindGroups = 0;
};
/// One sequential helper analysis. No process-global or filename-keyed cache.
class ComputeSession final
{
public:
    explicit ComputeSession(QString model = {}, QString modelFile = {},
                            qint64 bufferBudget = 1024LL * 1024 * 1024);
    ~ComputeSession();
    ComputeSession(const ComputeSession &) = delete;
    ComputeSession &operator=(const ComputeSession &) = delete;
    QJsonObject sourceGraph(const QString &runtimeScript, const QString &model, int frames,
                            QString *error);
    ComputeResult execute(const QJsonObject &graph, const QMap<QString, QByteArray> &inputs,
                          const std::function<bool()> &canceled = {});
    void close();
    ComputeStatistics statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
ComputeResult probeNativeGpu();
ComputeResult executeGraph(const QJsonObject &graph, const QString &modelFile,
                           const QMap<QString, QByteArray> &inputs);
QJsonObject makeSourceGraph(const QString &runtimeScript, const QString &modelFile,
                            const QString &model, int frames, QString *error);
} // namespace AudioAnalysisTool
