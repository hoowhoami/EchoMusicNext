#pragma once
#include <QString>

class QSqlDatabase;
class QSqlError;

namespace EchoMusic::SqlDiagnostics
{
/// Called on the connection's owning thread. Logs only the database basename,
/// a static operation label and driver errors; never SQL text or bound values.
/// Returns the driver message for existing UI/error signals, with a fallback
/// for a successful query that unexpectedly returned no row.
QString failure(const QSqlDatabase &database, const char *operation, const QSqlError &error);
void unsupportedVersion(const QSqlDatabase &database, int actual, int supported);
} // namespace EchoMusic::SqlDiagnostics
