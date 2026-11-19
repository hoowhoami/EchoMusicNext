#pragma once
#include <QByteArray>
#include <QIODevice>
#include <QString>

namespace EchoMusic::PluginPackageDetail
{
struct FileCapture
{
    QByteArray bytes;
    QString error;
};
// Internal immutable import snapshot. The size is checked before allocating;
// a separate one-byte read detects growth without retaining the package budget.
FileCapture captureFile(QIODevice &file, qint64 remaining);
} // namespace EchoMusic::PluginPackageDetail
