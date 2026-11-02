#pragma once
#include <QNetworkReply>
#include <QVariantMap>
namespace EchoMusic::PluginNetworkFailure
{
QString address(const QUrl &url);
/// Typed transport failures; provider response bodies are never diagnostic text.
QVariantMap classify(QNetworkReply::NetworkError error, int status, const QUrl &url,
                     const QString &message, bool limited = false, bool rateLimited = false,
                     bool offline = false);
QVariantMap content(const QString &message, const QUrl &url);
} // namespace EchoMusic::PluginNetworkFailure
