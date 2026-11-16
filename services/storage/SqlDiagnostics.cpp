#include "SqlDiagnostics.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QSqlError>

Q_LOGGING_CATEGORY(sqlDiagnostics, "echo.storage.sql")

namespace EchoMusic::SqlDiagnostics
{
QString failure(const QSqlDatabase &database, const char *operation, const QSqlError &error)
{
    QJsonObject record{{"database", QFileInfo(database.databaseName()).fileName()},
                       {"operation", QString::fromLatin1(operation)},
                       {"error_type", int(error.type())},
                       {"native_code", error.nativeErrorCode()},
                       {"driver_text", error.driverText()},
                       {"database_text", error.databaseText()}};
    bool numeric = false;
    const int code = error.nativeErrorCode().toInt(&numeric);
    if (numeric)
        record.insert("primary_code", code & 0xff);
    if (!error.isValid())
        record.insert("reason", "query_returned_no_row");
    qCCritical(sqlDiagnostics).noquote() << QJsonDocument(record).toJson(QJsonDocument::Compact);
    return error.isValid() ? error.text() : QStringLiteral("数据库查询未返回结果");
}

void unsupportedVersion(const QSqlDatabase &database, int actual, int supported)
{
    const QJsonObject record{{"database", QFileInfo(database.databaseName()).fileName()},
                             {"operation", "check_user_version"},
                             {"actual_version", actual},
                             {"supported_version", supported}};
    qCCritical(sqlDiagnostics).noquote() << QJsonDocument(record).toJson(QJsonDocument::Compact);
}
} // namespace EchoMusic::SqlDiagnostics
