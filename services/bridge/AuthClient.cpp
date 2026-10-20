#include "ApiDomains_p.h"
#include "echo_ffi.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

namespace EchoMusic
{
quint64 ApiClient::AuthClient::refreshLogin()
{
    if (!api.m_userId || api.m_token.isEmpty())
        return 0;
    return api.submit({{"operation", "login_token"}}, true, {AuthenticationMode::CurrentAccount});
}

quint64 ApiClient::AuthClient::registerDevice()
{
    return api.submit({{"operation", "register_dev"}}, true, {AuthenticationMode::DeviceOnly});
}

quint64 ApiClient::AuthClient::createQrLogin()
{
    return api.submit({{"operation", "login_qr_key"}}, true, {AuthenticationMode::DeviceOnly});
}

quint64 ApiClient::AuthClient::checkQrLogin(const QString &key)
{
    return api.submit({{"operation", "login_qr_check"}, {"query", QJsonObject{{"key", key}}}}, true,
                      {AuthenticationMode::DeviceOnly});
}

quint64 ApiClient::AuthClient::createProviderQr(QrProvider provider)
{
    if (provider == QrProvider::Kugou)
        return createQrLogin();
    return api.submit(
        {{"operation", provider == QrProvider::Qq ? "login_qq_qr_create" : "login_wx_create"}},
        false);
}

quint64 ApiClient::AuthClient::checkProviderQr(QrProvider provider, const QJsonObject &session)
{
    if (provider == QrProvider::Kugou)
        return checkQrLogin(session.value("key").toString());
    if (provider == QrProvider::Qq)
        return api.submit(
            {{"operation", "login_qq_qr_check"}, {"query", QJsonObject{{"session", session}}}},
            true, {AuthenticationMode::DeviceOnly});
    return api.submit({{"operation", "login_wx_check"}, {"query", session}}, false);
}

quint64 ApiClient::AuthClient::exchangeWechatCode(const QString &code)
{
    return api.submit({{"operation", "login_openplat"},
                       {"query", QJsonObject{{"stage", "authorize"}, {"code", code}}}},
                      true, {AuthenticationMode::DeviceOnly, false});
}

quint64 ApiClient::AuthClient::finishProviderLogin(QrProvider provider,
                                                   const QJsonObject &authorization)
{
    if (provider == QrProvider::Kugou)
        return 0;
    QJsonObject query{{"authorization", authorization}};
    if (provider == QrProvider::Wechat)
        query.insert("stage", "finish");
    return api.submit(
        {{"operation", provider == QrProvider::Wechat ? "login_openplat" : "login_qq"},
         {"query", query}},
        true, {AuthenticationMode::DeviceOnly});
}

quint64 ApiClient::AuthClient::sendSmsCode(const QString &mobile)
{
    return api.submit({{"operation", "captcha_sent"}, {"query", QJsonObject{{"mobile", mobile}}}},
                      true, {AuthenticationMode::DeviceOnly});
}

quint64 ApiClient::AuthClient::smsLogin(const QString &mobile, const QString &code,
                                        std::optional<quint64> selectedUser)
{
    QJsonObject query{{"mobile", mobile},
                      {"code", code},
                      {"user_id", selectedUser ? QJsonValue(qint64(*selectedUser))
                                               : QJsonValue(QJsonValue::Null)}};
    return api.submit({{"operation", "login_cellphone"}, {"query", query}}, true,
                      {AuthenticationMode::DeviceOnly});
}

quint64 ApiClient::AuthClient::passwordLogin(const QString &username, const QString &password)
{
    return api.submit({{"operation", "login"},
                       {"query", QJsonObject{{"username", username}, {"password", password}}}},
                      true, {AuthenticationMode::DeviceOnly, true});
}

quint64 ApiClient::AuthClient::loginDevices()
{
    return api.m_userId ? api.submit({{"operation", "login_device"}}) : 0;
}

quint64 ApiClient::AuthClient::kickLoginDevice(const LoginDeviceTarget &target)
{
    if (!api.m_userId || target.mid.isEmpty() || target.mid == api.m_device.mid ||
        !target.activeAtSeconds || !target.appId || !target.clientVersion)
        return 0;
    return api.submit({{"operation", "login_device_kick"},
                       {"query", QJsonObject{{"target_mid", target.mid},
                                             {"transport_fingerprint", target.transportFingerprint},
                                             {"active_at_seconds", qint64(target.activeAtSeconds)},
                                             {"app_id", qint64(target.appId)},
                                             {"client_version", qint64(target.clientVersion)}}}},
                      true, {AuthenticationMode::CurrentAccount, false});
}

quint64 ApiClient::AuthClient::userVerify(RequestOptions options)
{
    return api.submit({{"operation", "user_verify"}}, true, options);
}
} // namespace EchoMusic
