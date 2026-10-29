#pragma once
#include "AccountService.h"
#include "SavedAccountModel.h"
#include "RelaunchService.h"
#include "SettingsStore.h"
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace EchoMusic
{
/// Explicit local diagnostic commands; the transient viewer can display credentials
/// on request. Account SQL writes drain before the normal relaunch helper runs.
class ExperimentalSettingsService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned diagnostics")
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool resetPending READ resetPending NOTIFY changed)
public:
    ExperimentalSettingsService(ApiClient &api, AccountService &account, SavedAccountModel &saved,
                                EchoMusic::SettingsStore &settings, RelaunchService &relaunch,
                                QObject *parent = nullptr);
    bool busy() const
    {
        return m_busy;
    }
    bool resetPending() const;
    Q_INVOKABLE QVariantList userInfo() const;
    Q_INVOKABLE void copyAuthorization();
    Q_INVOKABLE void resetDeviceIdentity();
    void setMaintenanceGuard(std::function<bool()> guard)
    {
        m_guard = std::move(guard);
    }
signals:
    void changed();
    void identityChanged();
    void notice(const QString &message, bool failure);

private:
    void finishLogout();
    ApiClient &m_api;
    AccountService &m_account;
    SavedAccountModel &m_saved;
    EchoMusic::SettingsStore &m_settings;
    RelaunchService &m_relaunch;
    std::function<bool()> m_guard;
    bool m_busy = false, m_waitingForLogout = false;
};
} // namespace EchoMusic
