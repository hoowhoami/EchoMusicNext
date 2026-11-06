#pragma once
#include <QString>
namespace EchoMusic
{
QString safeLogMessage(const QString &message);
// Redact a field before composing a record so diagnostics retain their identity.
QString safeLogField(const QString &message, qsizetype limit = 1536);
} // namespace EchoMusic
