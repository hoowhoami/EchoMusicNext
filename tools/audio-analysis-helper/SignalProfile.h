// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>

namespace AudioAnalysisTool
{
struct ComputeStatistics;
// Private helper transport only. The broker owns identity and fixed checkpoint file names.
struct DrumCheckpoint
{
    QByteArray identity, input;
    // Called only for a complete segment; empty return means atomic publication succeeded.
    std::function<QString(const QByteArray &, const QJsonObject &)> save;
};
QJsonObject analysePcm(const QString &kind, const QString &modelDirectory,
                       const QString &runtimeDirectory, const QByteArray &left,
                       const QByteArray &right, QString *error,
                       std::function<void(int, int, double)> progress = {},
                       std::function<void(QJsonObject)> partial = {},
                       std::function<bool()> canceled = {}, ComputeStatistics *statistics = nullptr,
                       const DrumCheckpoint *checkpoint = nullptr);
} // namespace AudioAnalysisTool
