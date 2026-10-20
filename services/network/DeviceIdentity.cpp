#include "SettingsKeys.h"
#include "DeviceIdentity.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>
#include <QRandomGenerator>

namespace EchoMusic
{
namespace
{
QString createDeviceGuid()
{
    // EchoMusic initApiServer derives its 32-character GUID as MD5(getGuid()).
    const auto source = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Md5).toHex());
}
void removeDevicePreferences(QSettings &settings)
{
    for (const auto key :
         {EchoMusic::SettingsKeys::Network::RegisteredDevice,
          EchoMusic::SettingsKeys::Network::DeviceGuid,
          EchoMusic::SettingsKeys::Network::DeviceFingerprint,
          EchoMusic::SettingsKeys::Network::DeviceMac, EchoMusic::SettingsKeys::Network::DeviceUuid,
          EchoMusic::SettingsKeys::Network::RenderFingerprint})
        settings.remove(QLatin1String(key));
}
} // namespace
bool DeviceIdentity::resetOnRestart(QSettings &settings)
{
    // A registration writer may already hold an old immutable snapshot. The marker
    // survives that write; load() clears it again only after the old process drained.
    settings.setValue(EchoMusic::SettingsKeys::Network::ResetDeviceIdentity, true);
    removeDevicePreferences(settings);
    settings.sync();
    return settings.status() == QSettings::NoError;
}
QString DeviceIdentity::midForGuid(const QString &guid)
{
    const auto digest = QCryptographicHash::hash(guid.toUtf8(), QCryptographicHash::Md5);
    // MID is the full unsigned 128-bit digest rendered in decimal. Use base-256
    // accumulation so MSVC and Clang agree without non-portable __int128.
    QString decimal = QStringLiteral("0");
    for (const auto byte : digest) {
        unsigned carry = static_cast<unsigned char>(byte);
        for (qsizetype i = decimal.size(); i-- > 0;) {
            carry += unsigned(decimal.at(i).digitValue()) * 256;
            decimal[i] = QChar(char('0' + carry % 10));
            carry /= 10;
        }
        while (carry) {
            decimal.prepend(QChar(char('0' + carry % 10)));
            carry /= 10;
        }
    }
    return decimal;
}
DeviceIdentity DeviceIdentity::temporary()
{
    const auto guid = createDeviceGuid();
    return {midForGuid(guid),
            QStringLiteral("-"),
            guid,
            QStringLiteral("02:00:00:00:00:00"),
            QStringLiteral("EchoMusicNext"),
            QStringLiteral("-"),
            QString::number(QRandomGenerator::global()->generate64())};
}
bool DeviceIdentity::saveRegistration(QSettings &settings, const DeviceIdentity &device)
{
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(device.guid).hasMatch() ||
        device.mid != midForGuid(device.guid) || device.dfid.isEmpty() ||
        device.dfid == QStringLiteral("-") || device.mac.isEmpty() || device.deviceName.isEmpty() ||
        device.uuid.isEmpty() || device.renderFingerprint.isEmpty())
        return false;
    // One value preserves the DFID/GUID association as a complete preference snapshot.
    settings.setValue(EchoMusic::SettingsKeys::Network::RegisteredDevice,
                      QVariantMap{{"guid", device.guid},
                                  {"dfid", device.dfid},
                                  {"mid", device.mid},
                                  {"mac", device.mac},
                                  {"uuid", device.uuid},
                                  {"renderFingerprint", device.renderFingerprint}});
    settings.sync();
    return settings.status() == QSettings::NoError;
}
DeviceIdentity DeviceIdentity::load(QSettings &settings)
{
    if (settings.value(EchoMusic::SettingsKeys::Network::ResetDeviceIdentity).toBool()) {
        removeDevicePreferences(settings);
        settings.remove(EchoMusic::SettingsKeys::Network::ResetDeviceIdentity);
        settings.sync();
    }
    const auto registered =
        settings.value(EchoMusic::SettingsKeys::Network::RegisteredDevice).toMap();
    const auto guidValue = registered.value("guid").toString();
    const auto dfidValue = registered.value("dfid").toString();
    if (QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(guidValue).hasMatch() &&
        !dfidValue.isEmpty() && dfidValue != QStringLiteral("-") &&
        registered.value("mid").toString() == midForGuid(guidValue) &&
        !registered.value("mac").toString().isEmpty() &&
        !registered.value("uuid").toString().isEmpty() &&
        !registered.value("renderFingerprint").toString().isEmpty()) {
        return {midForGuid(guidValue),
                dfidValue,
                guidValue,
                registered.value("mac").toString(),
                QStringLiteral("EchoMusicNext"),
                registered.value("uuid").toString(),
                registered.value("renderFingerprint").toString()};
    }

    QString guid = settings.value(EchoMusic::SettingsKeys::Network::DeviceGuid).toString();
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(guid).hasMatch()) {
        guid = createDeviceGuid();
        settings.setValue(EchoMusic::SettingsKeys::Network::DeviceGuid, guid);
        settings.remove(EchoMusic::SettingsKeys::Network::DeviceFingerprint);
    }
    QString fingerprint =
        settings.value(EchoMusic::SettingsKeys::Network::DeviceFingerprint, QStringLiteral("-"))
            .toString();
    if (fingerprint.isEmpty())
        fingerprint = QStringLiteral("-");
    auto value = [&settings](const QString &key, const QString &fallback) {
        auto result = settings.value(key).toString();
        if (result.isEmpty()) {
            result = fallback;
            settings.setValue(key, result);
        }
        return result;
    };
    // These are protocol identities, not credentials or a hardware MAC lookup.
    // SID/EDT uses a persistent native fingerprint surrogate, as the old Node host did.
    return {midForGuid(guid),
            fingerprint,
            guid,
            value(EchoMusic::SettingsKeys::Network::DeviceMac, QStringLiteral("02:00:00:00:00:00")),
            QStringLiteral("EchoMusicNext"),
            value(EchoMusic::SettingsKeys::Network::DeviceUuid, QStringLiteral("-")),
            value(EchoMusic::SettingsKeys::Network::RenderFingerprint,
                  QString::number(QRandomGenerator::global()->generate64()))};
}
} // namespace EchoMusic
