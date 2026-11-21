#pragma once
#include <QJsonObject>
namespace EchoMusic
{
/// Explicit catalog facts allow an authorization attempt, never grant access.
inline bool isFreeLegacyCatalogFile(const QJsonObject &file)
{
    return file.value("privilege").toInt() == 5 && file.value("payment_type").isDouble() &&
           file.value("payment_type").toInt(-1) == 0 && file.value("price_units").isDouble() &&
           file.value("price_units").toInt(-1) == 0;
}
} // namespace EchoMusic
