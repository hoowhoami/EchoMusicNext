#include "VerificationService.h"
#include "BrowserVerificationSession.h"
#include "BrowserWindowLauncher.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <utility>
namespace EchoMusic
{
VerificationService::VerificationService(ApiClient &api, QObject *parent,
                                         BrowserOpener browserOpener)
    : QObject(parent), m_api(api), m_browser(new BrowserVerificationSession(*this, this)),
      m_browserOpener(browserOpener ? std::move(browserOpener)
                                    : BrowserWindowLauncher::openVerification)
{
    connect(m_browser, &BrowserVerificationSession::proofSubmitted, this,
            &VerificationService::returnToApplicationRequested);
    connect(m_browser, &BrowserVerificationSession::activeChanged, this,
            &VerificationService::changed);
    connect(m_browser, &BrowserVerificationSession::sdkFailed, this, [this](int code) {
        m_error = tr("浏览器验证码加载失败（%1），请重试").arg(code);
        m_state = State::Error;
        emit changed();
    });
    connect(m_browser, &BrowserVerificationSession::expired, this, [this] {
        m_error = tr("浏览器验证已超时，请重新开始");
        m_state = State::Error;
        emit changed();
    });
    connect(&api, &ApiClient::verificationRequired, this, [this](quint64 id, const QString &) {
        clear();
        m_request = id;
        refresh();
    });
    connect(&api, &ApiClient::verificationDismissed, this, &VerificationService::clear);
    connect(&api, &ApiClient::succeeded, this, [this](quint64 id, const QByteArray &payload) {
        if (id != m_info || !m_info || !m_request)
            return;
        m_info = 0;
        const auto data = QJsonDocument::fromJson(payload).object();
        m_appId = data.value("app_id").toString();
        m_type = data.value("verification_type").toInt();
        m_provider = data.value("provider").toString();
        m_vendor = data.value("vendor").toString();
        m_displayMessage = data.value("display_message").toString();
        if (m_provider == QStringLiteral("tencent") &&
            (m_type != 23 ||
             !QRegularExpression(QStringLiteral("^[0-9]{1,20}$")).match(m_appId).hasMatch())) {
            m_appId.clear();
            m_error = tr("验证码配置无效，请重试");
            m_state = State::Error;
        } else {
            m_state = State::Ready;
        }
        emit changed();
    });
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &payload) {
        if (!id || (id != m_info && id != m_submit))
            return;
        if (id == m_info)
            m_info = 0;
        if (id == m_submit)
            m_submit = 0;
        const auto error = QJsonDocument::fromJson(payload).object();
        m_error = error.value("code").toInteger() == 30791 ? tr("验证未通过，请重新完成图形验证")
                                                           : error.value("message").toString();
        if (m_error.isEmpty())
            m_error = tr("安全验证失败，请重新验证");
        m_state = State::Error;
        emit changed();
    });
}
bool VerificationService::browserOpen() const
{
    return m_browser->active();
}
void VerificationService::beginBrowserVerification()
{
    if (m_info || m_submit || !m_request || m_type != 23)
        return;
    const auto generation = m_generation;
    m_error.clear();
    m_state = State::Ready;
    const auto url = m_browser->start();
    if (!open() || generation != m_generation)
        return;
    const bool opened = !url.isEmpty() && m_browserOpener(url);
    if (!open() || generation != m_generation)
        return;
    if (!opened) {
        m_browser->stop();
        m_error = tr("无法打开默认浏览器，请检查系统设置后重试");
        m_state = State::Error;
    }
    emit changed();
}
QString VerificationService::providerName() const
{
    if (m_provider == QStringLiteral("sms"))
        return tr("手机验证码");
    if (m_provider == QStringLiteral("login"))
        return tr("登录确认");
    if (m_provider == QStringLiteral("bind_phone"))
        return tr("绑定手机号");
    if (m_provider == QStringLiteral("real_name"))
        return tr("实名认证");
    if (m_provider == QStringLiteral("account_risk"))
        return tr("账号风控");
    if (m_vendor == QStringLiteral("GT"))
        return tr("极验验证码");
    if (m_vendor == QStringLiteral("KG"))
        return tr("酷狗滑块");
    if (m_vendor == QStringLiteral("KG2"))
        return tr("酷狗旋转");
    if (m_vendor == QStringLiteral("SM"))
        return tr("数美验证码");
    if (m_vendor == QStringLiteral("YD"))
        return tr("网易易盾");
    if (m_provider == QStringLiteral("tencent"))
        return tr("腾讯验证码");
    return tr("未知验证");
}
void VerificationService::clear()
{
    ++m_generation;
    m_request = m_info = m_submit = 0;
    m_type = 0;
    m_appId.clear();
    m_provider.clear();
    m_vendor.clear();
    m_displayMessage.clear();
    m_error.clear();
    m_state = State::Idle;
    emit changed();
}
void VerificationService::refresh()
{
    if (!m_request || m_info || m_submit)
        return;
    ++m_generation;
    m_appId.clear();
    m_error.clear();
    m_state = State::Loading;
    m_info = m_api.verificationInfo(m_request);
    if (!m_info) {
        m_state = State::Error;
        m_error = tr("无法获取安全验证信息");
    }
    emit changed();
}
void VerificationService::submitTencent(quint64 generation, const QString &ticket,
                                        const QString &randstr)
{
    if (!m_request || m_info || m_submit || generation != m_generation || m_type != 23 ||
        m_appId.isEmpty() || ticket.isEmpty() || randstr.isEmpty() || ticket.size() > 16384 ||
        randstr.size() > 1024)
        return;
    const auto proof = QStringLiteral("KGCodeTX|") +
                       QString::fromUtf8(QJsonDocument(QJsonObject{{"ticket", ticket},
                                                                   {"randstr", randstr},
                                                                   {"txappid", m_appId}})
                                             .toJson(QJsonDocument::Compact));
    m_error.clear();
    m_state = State::Verifying;
    m_submit = m_api.submitVerification(m_request, m_type, proof);
    if (!m_submit) {
        m_state = State::Error;
        m_error = tr("无法提交安全验证，请重试");
    }
    emit changed();
}
void VerificationService::submitSms(const QString &code)
{
    if (!m_request || m_info || m_submit || m_type != 32 || code.trimmed().isEmpty() ||
        code.size() > 128)
        return;
    m_error.clear();
    m_state = State::Verifying;
    m_submit = m_api.submitVerification(m_request, 32, code.trimmed());
    if (!m_submit) {
        m_state = State::Error;
        m_error = tr("无法提交安全验证，请重试");
    }
    emit changed();
}
void VerificationService::beginLogin()
{
    if (!m_request || m_type != 38 || m_info || m_submit)
        return;
    const auto request = m_request;
    if (m_api.awaitLoginVerification(request))
        emit loginRequested(request);
}
void VerificationService::cancel()
{
    const auto id = m_request;
    if (id)
        m_api.cancel(id); // Clears all suspended requests sharing this event.
}
void VerificationService::shutdown()
{
    cancel();
    clear();
}
} // namespace EchoMusic
