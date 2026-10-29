#pragma once
#include <QJsonObject>
#include <QVariantMap>
namespace EchoMusic
{
struct PortableSettingsCodec
{
    static QJsonObject encode(const QVariantMap &, QString *error, const QString &filesRoot = {});
    static QVariantMap decode(const QJsonObject &, QString *error, const QString &filesRoot = {});
};
} // namespace EchoMusic
