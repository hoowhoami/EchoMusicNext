#pragma once
#include <QString>

#include "SettingsStore.h"
namespace EchoMusic
{

/// An immutable request snapshot. Account credentials belong to the account
/// service; device preferences contain no login token and use the new app namespace.
struct DeviceIdentity
{
    QString mid;
    QString dfid;
    QString guid;
    QString mac;
    // Fixed application name in registration/login proofs, like SERVER_DEV in
    // EchoMusic; GUID/MID remain independent persistent device identifiers.
    QString deviceName;
    QString uuid;
    QString renderFingerprint;
    static DeviceIdentity load(EchoMusic::SettingsStore &settings);
    static DeviceIdentity temporary();
    static bool saveRegistration(EchoMusic::SettingsStore &settings, const DeviceIdentity &device);
    /// Clear local identity and fence late registration writes until the next launch.
    static bool resetOnRestart(EchoMusic::SettingsStore &settings);
    static QString midForGuid(const QString &guid);
};
} // namespace EchoMusic
