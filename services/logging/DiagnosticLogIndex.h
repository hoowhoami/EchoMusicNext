#pragma once
#include <QJsonObject>
#include <QStringList>
#include <atomic>

namespace EchoMusic
{
// Reads only the already-sanitized logs selected for this package. The bounded
// index points back to original lines; it never substitutes for the logs.
QJsonObject diagnosticLogIndex(const QString &root, const QStringList &files,
                               const std::atomic_bool &cancel);
} // namespace EchoMusic
