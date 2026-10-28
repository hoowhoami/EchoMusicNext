#include "AccountController.h"
#include <utility>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
namespace EchoMusic
{
AccountController::AccountController(ApiClient &api, AccountService &account,
                                     SavedAccountModel &saved, QrLoginService &qr,
                                     UserPlaylistModel &playlists, bool online, QObject *parent)
    : QObject(parent), m_api(api), m_qq(api, this), m_wechat(api, this), m_verification(api, this),
      m_account(account), m_saved(saved), m_qr(qr), m_playlists(playlists), m_online(online)
{
    connect(&m_verification, &VerificationService::loginRequested, this, [this](quint64 request) {
        const auto candidate = m_saved.switchingUserId();
        if (candidate && m_saved.ownsSwitchRequest(request)) {
            // Candidate login confirmation must not adopt an arbitrary QR user
            // or replay the old token after the new login has committed.
            m_api.cancelLoginVerification();
            reauthenticateAccount(candidate);
            return;
        }
        // A type-38 login now owns the continuation in ApiClient. Do not cancel
        // that original form ID while replacing its presentation with login.
        m_password = m_smsLogin = m_sendSms = 0;
        openLogin();
        m_confirmingLogin = true;
    });
    m_qq.setProvider(ApiClient::QrProvider::Qq);
    m_wechat.setProvider(ApiClient::QrProvider::Wechat);
    m_smsTimer.setInterval(1000);
    connect(&m_smsTimer, &QTimer::timeout, this, [this] {
        if (m_smsCountdown > 0)
            --m_smsCountdown;
        if (!m_smsCountdown)
            m_smsTimer.stop();
        emit changed();
    });
    for (auto *service : {&m_qq, &m_wechat}) {
        connect(service, &QrLoginService::authorized, this, [this, service] {
            auto authorization = service->takeAuthorization();
            const auto expected = service == &m_qq ? 3 : 4;
            if (!m_open || m_method != expected || m_loginRevision != m_api.sessionRevision())
                return;
            if (!acceptAuthorization(authorization)) {
                emit changed();
            }
        });
    }
    connect(&qr, &QrLoginService::authorized, this, [this] {
        auto authorization = m_qr.takeAuthorization();
        if (!m_open || m_method != 0 || m_loginRevision != m_api.sessionRevision())
            return;
        if (!acceptAuthorization(authorization)) {
            emit changed();
        }
    });
    connect(&account, &AccountService::changed, this, [this] {
        if (m_userId == m_account.userId())
            return;
        m_userId = m_account.userId();
        closeLogin();
    });
    connect(&account, &AccountService::sessionAdopted, this, [this] {
        closeLogin();
        if (m_online) {
            m_account.loadProfileOnce();
            m_playlists.reload();
        }
    });
    connect(&account, &AccountService::requestContextChanged, this, [this] {
        const auto revision = m_api.sessionRevision();
        const auto user = m_account.userId();
        // Registration invalidates native snapshots. Resume the same reads
        // after reset listeners finish; direct dispatch would be canceled again.
        QTimer::singleShot(0, this, [this, revision, user] {
            if (!m_online || m_shuttingDown || revision != m_api.sessionRevision() ||
                user != m_account.userId() || !user)
                return;
            m_account.loadProfileOnce();
            m_playlists.reload();
        });
    });
    connect(&playlists, &UserPlaylistModel::changed, this, [this] {
        if (!m_online || m_playlists.state() != UserPlaylistModel::State::Ready ||
            !m_playlists.canFetchMore({}))
            return;
        const auto revision = m_api.sessionRevision();
        QTimer::singleShot(0, this, [this, revision] {
            if (revision == m_api.sessionRevision())
                m_playlists.fetchMore({});
        });
    });
    connect(&api, &ApiClient::succeeded, this, [this](quint64 id, const QByteArray &payload) {
        if (!id || !m_open || m_loginRevision != m_api.sessionRevision())
            return;
        if (id == m_sendSms) {
            m_sendSms = 0;
            m_smsCountdown = 60;
            m_smsTimer.start();
            emit changed();
            return;
        }
        if (id != m_password && id != m_smsLogin)
            return;
        const bool sms = id == m_smsLogin;
        m_password = m_smsLogin = 0;
        m_pendingSmsUser = 0;
        const auto result = QJsonDocument::fromJson(payload).object();
        if (sms && result.value("state") == QStringLiteral("select_account")) {
            if (!m_smsCandidates.replace(result.value("candidates").toArray()))
                m_error = tr("账号列表无效，请重新获取验证码");
            emit changed();
            return;
        }
        if (!acceptAuthorization(payload)) {
            emit changed();
        }
    });
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &payload) {
        if (!id || (id != m_password && id != m_sendSms && id != m_smsLogin))
            return;
        if (id == m_password)
            m_password = 0;
        if (id == m_sendSms)
            m_sendSms = 0;
        if (id == m_smsLogin) {
            m_smsLogin = 0;
            m_pendingSmsUser = 0;
        }
        const auto error = QJsonDocument::fromJson(payload).object();
        m_error = error.value("kind") == QStringLiteral("cancelled")
                      ? tr("已取消安全验证")
                      : error.value("message").toString();
        if (m_error.isEmpty())
            m_error = tr("登录请求失败，请稍后重试");
        // Unknown service codes must remain explicit instead of being guessed
        // to mean a password error. The same code is available in runtime logs.
        if (error.value("code").isDouble())
            m_error += tr("（错误码 %1）").arg(error.value("code").toInteger());
        emit changed();
    });
    connect(&api, &ApiClient::verificationRequired, this, [this](quint64 id, const QString &) {
        if (id != m_password && id != m_sendSms && id != m_smsLogin)
            return;
        m_error = tr("需要完成安全验证");
        emit changed();
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        if (m_open && m_loginRevision != m_api.sessionRevision())
            closeLogin();
    });
}
bool AccountController::acceptAuthorization(const QByteArray &payload)
{
    const auto user = QJsonDocument::fromJson(payload).object().value("user_id").toInteger();
    if (m_reauthenticationUser && user != qint64(m_reauthenticationUser)) {
        m_error = tr("登录的账号与目标账号不一致，请登录目标账号");
        return false;
    }
    if (m_account.adoptLoginAuthorization(payload))
        return true;
    m_error = tr("登录信息不完整，请重新登录");
    return false;
}
void AccountController::reauthenticateAccount(quint64 userId)
{
    if (m_shuttingDown || m_saved.busy() || userId == m_account.userId())
        return;
    QString name;
    bool found = false;
    for (int i = 0; i < m_saved.count(); ++i) {
        const auto index = m_saved.index(i, 0);
        if (m_saved.data(index, SavedAccountModel::UserIdRole).toULongLong() == userId) {
            name = m_saved.data(index, SavedAccountModel::NicknameRole).toString();
            found = true;
            break;
        }
    }
    if (!found)
        return;
    if (name.isEmpty())
        name = QString::number(userId);
    openLogin();
    m_reauthenticationUser = userId;
    m_loginPrompt = tr("重新登录 %1 后切换").arg(name);
    emit changed();
}
void AccountController::openLogin()
{
    if (m_shuttingDown)
        return;
    closeLogin();
    m_saved.cancelSwitch();
    m_saved.suppressStartupRestoration();
    m_open = true;
    m_method = 0;
    m_loginRevision = m_api.sessionRevision();
    m_error.clear();
    if (m_online)
        m_qr.start();
    emit changed();
}
void AccountController::closeLogin()
{
    if (std::exchange(m_confirmingLogin, false))
        m_api.cancelLoginVerification();
    m_open = false;
    m_reauthenticationUser = 0;
    m_loginPrompt.clear();
    m_loginRevision = 0;
    m_error.clear();
    m_qr.cancel();
    m_qq.cancel();
    m_wechat.cancel();
    m_smsTimer.stop();
    m_smsCountdown = 0;
    resetSmsCandidates();
    if (m_sendSms)
        m_api.cancel(std::exchange(m_sendSms, 0));
    if (m_smsLogin)
        m_api.cancel(std::exchange(m_smsLogin, 0));
    if (m_password)
        m_api.cancel(std::exchange(m_password, 0));
    emit changed();
}
void AccountController::setMethod(int method)
{
    if (!m_open || (method < 0 || method > 4) || method == m_method)
        return;
    if (m_password)
        m_api.cancel(std::exchange(m_password, 0));
    if (m_sendSms)
        m_api.cancel(std::exchange(m_sendSms, 0));
    if (m_smsLogin)
        m_api.cancel(std::exchange(m_smsLogin, 0));
    m_qr.pause();
    m_qq.pause();
    m_wechat.pause();
    resetSmsCandidates();
    m_method = method;
    m_error.clear();
    if (m_online) {
        if (method == 0)
            m_qr.resume();
        if (method == 3)
            m_qq.resume();
        if (method == 4)
            m_wechat.resume();
    }
    emit changed();
}
void AccountController::submitPassword(const QString &username, const QString &password)
{
    if (!m_open || m_method != 2 || m_password)
        return;
    if (username.trimmed().isEmpty() || password.trimmed().isEmpty()) {
        m_error = username.trimmed().isEmpty() ? tr("请输入用户名") : tr("请输入密码");
        emit changed();
        return;
    }
    m_error.clear();
    m_password = m_api.passwordLogin(username.trimmed(), password.trimmed());
    emit changed();
}
void AccountController::sendSms(const QString &mobile)
{
    if (!m_open || m_method != 1 || busy() || m_sendSms || m_smsCountdown > 0 || !m_online)
        return;
    const auto number = mobile.trimmed();
    if (!QRegularExpression(QStringLiteral("^1[0-9]{10}$")).match(number).hasMatch()) {
        m_error = tr("请输入正确的手机号");
        emit changed();
        return;
    }
    resetSmsCandidates();
    m_error.clear();
    m_sendSms = m_api.sendSmsCode(number);
    emit changed();
}
void AccountController::submitSms(const QString &mobile, const QString &code)
{
    if (!m_open || m_method != 1 || busy() || m_sendSms || !m_online)
        return;
    const auto number = mobile.trimmed();
    if (!QRegularExpression(QStringLiteral("^1[0-9]{10}$")).match(number).hasMatch() ||
        code.trimmed().isEmpty()) {
        m_error = number.isEmpty() ? tr("请输入手机号")
                  : !QRegularExpression(QStringLiteral("^1[0-9]{10}$")).match(number).hasMatch()
                      ? tr("请输入正确的手机号")
                      : tr("请输入验证码");
        emit changed();
        return;
    }
    resetSmsCandidates();
    m_error.clear();
    m_smsMobile = number;
    m_smsCode = code.trimmed();
    m_smsLogin = m_api.smsLogin(m_smsMobile, m_smsCode);
    emit changed();
}
void AccountController::resetSmsCandidates()
{
    m_smsCandidates.clear();
    m_smsMobile.clear();
    m_smsCode.clear();
    m_pendingSmsUser = 0;
    m_error.clear();
    emit changed();
}
void AccountController::selectSmsAccount(int row)
{
    if (!m_open || m_method != 1 || busy() || m_sendSms || !m_online ||
        m_loginRevision != m_api.sessionRevision())
        return;
    const auto user = m_smsCandidates.userIdAt(row);
    if (!user || m_smsMobile.isEmpty() || m_smsCode.isEmpty())
        return;
    m_error.clear();
    m_pendingSmsUser = user;
    m_smsLogin = m_api.smsLogin(m_smsMobile, m_smsCode, user);
    if (!m_smsLogin) {
        m_pendingSmsUser = 0;
        m_error = tr("无法提交登录请求");
    }
    emit changed();
}
void AccountController::openAccountPanel()
{
    if (m_online) {
        m_account.loadProfileOnce();
        m_account.loadGrade();
    }
}
void AccountController::shutdown()
{
    m_shuttingDown = true;
    closeLogin();
    m_verification.shutdown();
}
} // namespace EchoMusic
