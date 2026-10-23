#pragma once
#include "NetworkPreferences.h"
#include "LogService.h"
#include "RelaunchService.h"
#include "BackupHost.h"
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
struct BackupHostRegistration
{
    Q_GADGET
    QML_FOREIGN(BackupHost)
    QML_ANONYMOUS
};
// Application-owned service metaobjects exposed to typed settings forms.
struct NetworkPreferencesRegistration
{
    Q_GADGET
    QML_FOREIGN(NetworkPreferences)
    QML_NAMED_ELEMENT(NetworkPreferences)
    QML_UNCREATABLE("Application-owned network policy")
};
struct LogServiceRegistration
{
    Q_GADGET
    QML_FOREIGN(LogService)
    QML_NAMED_ELEMENT(LogService)
    QML_UNCREATABLE("Application-owned Qt and Rust logging")
};
struct RelaunchServiceRegistration
{
    Q_GADGET
    QML_FOREIGN(RelaunchService)
    QML_NAMED_ELEMENT(RelaunchService)
    QML_UNCREATABLE("Application-owned restart lifecycle")
};
} // namespace EchoMusic
