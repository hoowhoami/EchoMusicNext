#include "LogRedaction.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStringList>
namespace EchoMusic
{
namespace
{
bool sensitiveKey(const QString &key)
{
    static const QStringList fragments{
        "token",      "cookie",     "authorization", "password",  "passwd",  "secret",
        "ticket",     "randstr",    "proof",         "secu",      "ssecode", "qrsig",
        "access_key", "signature",  "mobile",        "phone",     "email",   "key",
        "pwd",        "verifycode", "qrcode",        "login_sig", "h5sig",   "fingerprint",
        "dev",        "username",   "dfid",          "mid",       "uuid",    "guid",
        "mac"};
    const auto lower = key.toLower();
    if (lower == QStringLiteral("keyword") || lower == QStringLiteral("keywords"))
        return false;
    if (lower == QStringLiteral("sign") || lower == QStringLiteral("auth") ||
        lower == QStringLiteral("ssa-code"))
        return true;
    for (const auto &fragment : fragments)
        if (lower.contains(fragment))
            return true;
    return QStringList{"t1", "mid", "dfid", "uuid", "guid", "mac", "sid", "edt", "dev", "serverdev"}
        .contains(lower);
}
QJsonValue safeJson(const QJsonValue &value, int depth = 0)
{
    if (depth >= 8)
        return QStringLiteral("[truncated]");
    if (value.isObject()) {
        QJsonObject result;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            result.insert(it.key(), sensitiveKey(it.key()) ? QJsonValue("[redacted]")
                                                           : safeJson(it.value(), depth + 1));
        return result;
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const auto &entry : value.toArray())
            result.append(safeJson(entry, depth + 1));
        return result;
    }
    if (value.isString() && (value.toString().contains("://") ||
                             value.toString().startsWith("Bearer ", Qt::CaseInsensitive)))
        return QStringLiteral("[redacted address or credential]");
    return value;
}
} // namespace
QString safeLogMessage(const QString &message)
{
    // Native API diagnostics redact and bound bodies before enqueueing. Apply
    // the same field protection to structured Qt/QML logs, and reject labelled
    // credentials/signed URLs in free text. Unlabelled secrets cannot be inferred;
    // callers must never log raw credentials in the first place.
    if (message.toUtf8().size() > 8192)
        return QStringLiteral("[oversized log omitted]");
    const auto structured = QJsonDocument::fromJson(message.toUtf8());
    if (structured.isObject())
        return QString::fromUtf8(
            QJsonDocument(safeJson(structured.object()).toObject()).toJson(QJsonDocument::Compact));
    if (structured.isArray())
        return QString::fromUtf8(
            QJsonDocument(safeJson(structured.array()).toArray()).toJson(QJsonDocument::Compact));
    static const QStringList keys{
        QStringLiteral("token"),       QStringLiteral("cookie"),  QStringLiteral("authorization"),
        QStringLiteral("password"),    QStringLiteral("passwd"),  QStringLiteral("secret"),
        QStringLiteral("ticket"),      QStringLiteral("randstr"), QStringLiteral("proof"),
        QStringLiteral("secu_params"), QStringLiteral("ssecode"), QStringLiteral("qrsig"),
        QStringLiteral("access_key"),  QStringLiteral("dfid"),    QStringLiteral("device_id"),
        QStringLiteral("username"),    QStringLiteral("mobile"),  QStringLiteral("email")};
    for (const auto &key : keys)
        if (message.contains(key, Qt::CaseInsensitive))
            return QStringLiteral("[redacted sensitive log]");
    if ((message.contains("https://", Qt::CaseInsensitive) ||
         message.contains("http://", Qt::CaseInsensitive)) &&
        message.contains('?'))
        return QStringLiteral("[redacted sensitive log]");
    return message;
}
QString safeLogField(const QString &message, qsizetype limit)
{
    const auto safe = safeLogMessage(message);
    return safe.size() > limit
               ? safe.left(qMax(qsizetype(0), limit)) + QStringLiteral(" [truncated]")
               : safe;
}
} // namespace EchoMusic
