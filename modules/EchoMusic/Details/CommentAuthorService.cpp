#include "CacheCost.h"
#include "CommentAuthorService.h"
#include <QJsonDocument>
namespace EchoMusic
{
CommentAuthorService::CommentAuthorService(ApiClient &a, bool network, QObject *p)
    : QObject(p), m_api(a), m_network(network)
{
    connect(&a, &ApiClient::succeeded, this,
            [this](quint64 id, const QByteArray &b) { finish(id, b, true); });
    connect(&a, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &b) { finish(id, b, false); });
    connect(&a, &ApiClient::sessionChanged, this, &CommentAuthorService::reset);
    connect(&a, &ApiClient::closed, this, &CommentAuthorService::shutdown);
}
CommentAuthorService::~CommentAuthorService()
{
    shutdown();
}
void CommentAuthorService::reset()
{
    auto ids = m_pending.keys();
    m_pending.clear();
    for (auto id : ids)
        m_api.cancel(id);
    m_cache.clear();
    m_inflight.clear();
    emit changed();
}
void CommentAuthorService::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    reset();
    disconnect(&m_api, nullptr, this, nullptr);
}
QJsonArray CommentAuthorService::products(const QString &id) const
{
    return m_cache.peek(id).value_or(QJsonArray{});
}
bool CommentAuthorService::readyFor(const QJsonArray &comments) const
{
    if (!m_network)
        return true;
    for (const auto &value : comments) {
        const auto id = value.toObject().value("user_id").toString();
        if (!id.toULongLong())
            continue;
        if (!m_cache.peek(id))
            return false;
    }
    return true;
}
void CommentAuthorService::ensure(const QJsonArray &comments)
{
    if (m_closed || !m_network)
        return;
    QStringList missing;
    for (const auto &v : comments) {
        const auto id = v.toObject().value("user_id").toString();
        if (id.toULongLong() > 0 && !m_inflight.contains(id) && !m_cache.peek(id)) {
            m_inflight.insert(id);
            missing.append(id);
        }
    }
    for (qsizetype offset = 0; offset < missing.size(); offset += 20) {
        const auto chunk = missing.mid(offset, 20);
        QList<quint64> ids;
        for (const auto &id : chunk)
            ids.append(id.toULongLong());
        auto request = m_api.commentAuthorProducts(ids);
        if (request)
            m_pending.insert(request, chunk);
        else
            for (const auto &id : chunk) {
                m_inflight.remove(id);
                m_cache.insert(id, {}, 30000);
            }
    }
}
void CommentAuthorService::finish(quint64 request, const QByteArray &bytes, bool success)
{
    if (!m_pending.contains(request) || m_closed)
        return;
    auto ids = m_pending.take(request);
    QHash<QString, QJsonArray> products;
    if (success)
        for (const auto &v : QJsonDocument::fromJson(bytes).array()) {
            auto obj = v.toObject();
            products.insert(obj.value("user_id").toString(), obj.value("products").toArray());
        }
    const auto ttl = success ? 30 * 60 * 1000 : 30 * 1000;
    for (const auto &id : ids) {
        m_inflight.remove(id);
        const auto value = products.value(id);
        m_cache.insert(id, value, ttl, jsonValueCost(value) * 2);
    }
    emit changed();
}
} // namespace EchoMusic
