#pragma once
#include "AccountService.h"
#include "SavedAccountModel.h"
#include "QrLoginService.h"
#include "UserPlaylistModel.h"
#include "VerificationService.h"
#include "LoginCandidateModel.h"
#include <QTimer>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
/// Presentation commands and overlay lifetime; no credentials exposed to QML.
/// Account/catalog/QR services each retain their single domain state owner.
class AccountController final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned account UI")
    Q_PROPERTY(EchoMusic::AccountService *account READ account CONSTANT)
    Q_PROPERTY(EchoMusic::SavedAccountModel *savedAccounts READ savedAccounts CONSTANT)
    Q_PROPERTY(EchoMusic::QrLoginService *qr READ qr CONSTANT)
    Q_PROPERTY(EchoMusic::QrLoginService *qq READ qq CONSTANT)
    Q_PROPERTY(EchoMusic::QrLoginService *wechat READ wechat CONSTANT)
    Q_PROPERTY(EchoMusic::VerificationService *verification READ verification CONSTANT)
    Q_PROPERTY(EchoMusic::LoginCandidateModel *smsCandidates READ smsCandidates CONSTANT)
    Q_PROPERTY(quint64 pendingSmsUser READ pendingSmsUser NOTIFY changed)
    Q_PROPERTY(int smsCountdown READ smsCountdown NOTIFY changed)
    Q_PROPERTY(bool sendingSms READ sendingSms NOTIFY changed)
    Q_PROPERTY(bool loginOpen READ loginOpen NOTIFY changed)
    Q_PROPERTY(int method READ method WRITE setMethod NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString loginPrompt READ loginPrompt NOTIFY changed)
public:
    AccountController(ApiClient &api, AccountService &account, SavedAccountModel &saved,
                      QrLoginService &qr, UserPlaylistModel &playlists, bool online = true,
                      QObject *parent = nullptr);
    AccountService *account() const
    {
        return &m_account;
    }
    SavedAccountModel *savedAccounts() const
    {
        return &m_saved;
    }
    QrLoginService *qr() const
    {
        return &m_qr;
    }
    QrLoginService *qq()
    {
        return &m_qq;
    }
    QrLoginService *wechat()
    {
        return &m_wechat;
    }
    VerificationService *verification()
    {
        return &m_verification;
    }
    LoginCandidateModel *smsCandidates()
    {
        return &m_smsCandidates;
    }
    quint64 pendingSmsUser() const
    {
        return m_pendingSmsUser;
    }
    int smsCountdown() const
    {
        return m_smsCountdown;
    }
    bool sendingSms() const
    {
        return m_sendSms != 0;
    }
    bool loginOpen() const
    {
        return m_open;
    }
    int method() const
    {
        return m_method;
    }
    bool busy() const
    {
        return m_password != 0 || m_smsLogin != 0;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QString loginPrompt() const
    {
        return m_loginPrompt;
    }
    void setMethod(int method);
    Q_INVOKABLE void reauthenticateAccount(quint64 userId);
    Q_INVOKABLE void openLogin();
    Q_INVOKABLE void closeLogin();
    Q_INVOKABLE void submitPassword(const QString &username, const QString &password);
    Q_INVOKABLE void sendSms(const QString &mobile);
    Q_INVOKABLE void submitSms(const QString &mobile, const QString &code);
    Q_INVOKABLE void selectSmsAccount(int row);
    Q_INVOKABLE void resetSmsCandidates();
    Q_INVOKABLE void openAccountPanel();
    void shutdown();
signals:
    void changed();

private:
    friend class AccountControllerTestAccess;
    bool acceptAuthorization(const QByteArray &payload);
    quint64 m_reauthenticationUser = 0;
    QString m_loginPrompt;
    ApiClient &m_api;
    QrLoginService m_qq, m_wechat;
    VerificationService m_verification;
    LoginCandidateModel m_smsCandidates;
    QString m_smsMobile, m_smsCode;
    quint64 m_pendingSmsUser = 0;
    QTimer m_smsTimer;
    int m_smsCountdown = 0;
    quint64 m_sendSms = 0, m_smsLogin = 0;
    AccountService &m_account;
    SavedAccountModel &m_saved;
    QrLoginService &m_qr;
    UserPlaylistModel &m_playlists;
    QString m_error;
    quint64 m_userId = 0, m_password = 0, m_loginRevision = 0;
    int m_method = 0;
    bool m_confirmingLogin = false;
    bool m_open = false, m_online = true, m_shuttingDown = false;
};
} // namespace EchoMusic
