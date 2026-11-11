#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <atomic>

namespace EchoMusic
{
struct DiagnosticRecord
{
    QString id, kind, timestamp, version, buildId, os, arch;
    QString session, runLog, dumpPath;
    QJsonObject exception;
    qint64 bytes = 0;
    bool hasDump = false;
};
struct DiagnosticScan
{
    QVector<DiagnosticRecord> records;
    QString error;
};
// Values only. Called on a diagnostic worker, never from fatal handlers or QML.
class DiagnosticRecords final
{
public:
    static DiagnosticScan scan(const QString &directory, const std::atomic_bool &cancel);
    // Copies native evidence under Crashpad's report lease where available.
    // Returned attachment paths are private staging files, never original inputs.
    static QStringList captureNative(const QString &directory, const DiagnosticRecord &record,
                                     const QString &staging, bool dump,
                                     const std::atomic_bool &cancel, QStringList &missing);
    static QJsonObject summary(const DiagnosticRecord &record);
    static void pruneSidecars(const QString &directory);
};
} // namespace EchoMusic
