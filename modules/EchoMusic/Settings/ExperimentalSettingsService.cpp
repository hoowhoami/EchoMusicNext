#include "SettingsKeys.h"
#include "ExperimentalSettingsService.h"
#include <QClipboard>
#include <QGuiApplication>

namespace EchoMusic
{
ExperimentalSettingsService::ExperimentalSettingsService(ApiClient &api, AccountService &account,
                                                         SavedAccountModel &saved,
                                                         EchoMusic::SettingsStore &settings,
                                                         RelaunchService &relaunch, QObject *parent)
    : QObject(parent), m_api(api), m_account(account), m_saved(saved), m_settings(settings),
      m_relaunch(relaunch)
{
    connect(&saved, &SavedAccountModel::changed, this, &ExperimentalSettingsService::finishLogout);
    connect(&api, &ApiClient::sessionChanged, this, &ExperimentalSettingsService::identityChanged);
}
QVariantList ExperimentalSettingsService::userInfo() const
{
    if (m_busy || resetPending())
        return {};
    QVariantList result;
    if (m_account.userId()) {
        result.append(
            QVariantMap{{"label", tr("用户 ID")}, {"value", QString::number(m_account.userId())}});
        if (!m_account.nickname().isEmpty())
            result.append(QVariantMap{{"label", tr("昵称")}, {"value", m_account.nickname()}});
    }
    result.append(m_api.diagnosticIdentityEntries());
    return result;
}
void ExperimentalSettingsService::copyAuthorization()
{
    if (m_busy || resetPending())
        return;
    const auto header = m_api.diagnosticAuthorization();
    auto *clipboard = QGuiApplication::clipboard();
    if (header.isEmpty() || !clipboard) {
        emit notice(tr("当前没有可用的鉴权头，请先登录"), true);
        return;
    }
    clipboard->setText(header);
    emit notice(tr("鉴权头已复制"), false);
}
void ExperimentalSettingsService::resetDeviceIdentity()
{
    if (m_busy || resetPending() || m_relaunch.busy())
        return;
    if (!m_saved.ready() || m_saved.busy() || (m_guard && !m_guard())) {
        emit notice(tr("当前有任务正在处理，请稍后重试"), true);
        return;
    }
    m_busy = true;
    emit changed();
    emit identityChanged();
    // logout() invalidates in-flight requests immediately; its queued SQLite
    // removal must succeed before a packaged application can relaunch.
    m_saved.logout();
    m_waitingForLogout = true;
    finishLogout();
}
void ExperimentalSettingsService::finishLogout()
{
    if (!m_waitingForLogout || m_saved.busy())
        return;
    m_waitingForLogout = false;
    m_busy = false;
    emit changed();
    if (!m_saved.errorMessage().isEmpty()) {
        emit notice(tr("账号退出未能保存，设备身份尚未重置，请重试"), true);
        return;
    }
    if (!DeviceIdentity::resetOnRestart(m_settings)) {
        emit notice(tr("设备身份清除失败，请检查本地存储后重试"), true);
        return;
    }
    emit changed();
    if (m_relaunch.automaticRestart())
        m_relaunch.restart();
    else
        emit notice(tr("设备身份已清除，请手动重启应用后再登录"), false);
}
} // namespace EchoMusic

namespace EchoMusic
{
bool ExperimentalSettingsService::resetPending() const
{
    return m_settings.value(SettingsKeys::Network::ResetDeviceIdentity).toBool();
}
} // namespace EchoMusic
