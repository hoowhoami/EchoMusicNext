#pragma once
#include <QByteArray>
#include <QString>
#include <atomic>
namespace EchoMusic::CloudUploadFile
{
inline constexpr qint64 PartBytes = 1024 * 1024;
struct Snapshot
{
    QString path;
    qint64 size, modified;
};
struct Part
{
    QByteArray encoded;
    QString error;
};
/// Worker-only, bounded read from a selected-file snapshot. No network or model access.
Part readPart(const Snapshot &, int part, const std::atomic_bool &canceled);
} // namespace EchoMusic::CloudUploadFile
