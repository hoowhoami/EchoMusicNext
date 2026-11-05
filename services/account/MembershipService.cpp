#include "MembershipService.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <utility>

namespace EchoMusic
{
MembershipService::MembershipService(ApiClient &api, QObject *parent) : QObject(parent), m_api(api)
{
    connect(&api, &ApiClient::succeeded, this, &MembershipService::succeed);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &payload) { fail(id, payload); });
    connect(&api, &ApiClient::sessionChanged, this, &MembershipService::clear);
}
MembershipService::~MembershipService()
{
    const auto request = std::exchange(m_request, 0);
    if (request)
        m_api.cancel(request);
}
void MembershipService::clear()
{
    const auto request = std::exchange(m_request, 0);
    if (request)
        m_api.cancel(request);
    m_domain = {};
    m_error.clear();
    m_age.invalidate();
    setFacts({});
    emit changed();
}
void MembershipService::ensure(bool force)
{
    if (m_request)
        return;
    if (!m_api.currentAccountUserId()) {
        m_domain = {};
        m_error.clear();
        setFacts({true, false, false});
        emit changed();
        emit settled();
        return;
    }
    if (!force && m_facts.known && m_age.isValid() && m_age.elapsed() < 30000) {
        emit settled();
        return;
    }
    m_revision = m_api.sessionRevision();
    m_request = m_api.userVipDetail();
    if (!m_request) {
        m_domain = {};
        m_error = tr("会员权益尚未确认");
        setFacts({});
        emit changed();
        emit settled();
        return;
    }
    m_error.clear();
    emit changed();
}
void MembershipService::succeed(quint64 id, const QByteArray &bytes)
{
    if (!id || id != m_request || m_revision != m_api.sessionRevision())
        return;
    const auto value = QJsonDocument::fromJson(bytes).object();
    m_request = 0;
    m_domain = value;
    MembershipFacts facts;
    bool allKnown =
        value.value("user_type_code").isDouble() && value.value("vip_type_code").isDouble();
    // Apply membership display policy to the typed Rust user_vip_detail result.
    // Response parsing remains in the API module.
    facts.superVip = (value.value("user_type_code").toInteger() & 16) != 0;
    facts.conceptVip = facts.superVip || value.value("vip_type_code").toInt() == 6;
    for (const auto &entry : value.value("products").toArray()) {
        const auto product = entry.toObject();
        if (product.value("product").toString() == "svip") {
            allKnown &= product.value("active").isBool();
            facts.conceptVip |= product.value("active").toBool();
        }
    }
    facts.known = allKnown || facts.superVip || facts.conceptVip;
    m_error.clear();
    m_age.start();
    setFacts(facts);
    emit changed();
    emit settled();
}
void MembershipService::fail(quint64 id, const QByteArray &payload)
{
    if (!id || id != m_request || m_revision != m_api.sessionRevision())
        return;
    m_request = 0;
    m_domain = {};
    m_age.invalidate();
    m_error = ApiClient::errorMessage(payload, tr("会员信息获取失败，请稍后重试"));
    setFacts({});
    emit changed();
    emit settled();
}
void MembershipService::setFacts(MembershipFacts facts)
{
    if (m_facts == facts)
        return;
    m_facts = facts;
    emit factsChanged();
}
} // namespace EchoMusic
