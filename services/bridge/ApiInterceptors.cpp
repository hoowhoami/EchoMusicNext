#include "ApiInterceptors.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <utility>
namespace EchoMusic
{
namespace
{
bool secret(const QString &key)
{
    static const QSet<QString> names{"identity",      "token",   "t1",      "password",
                                     "authorization", "cookie",  "cookies", "access_token",
                                     "refresh_token", "captcha", "ticket"};
    return names.contains(key.toLower());
}
QJsonValue sanitize(const QJsonValue &value)
{
    if (value.isObject()) {
        QJsonObject result;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!secret(it.key()))
                result[it.key()] = sanitize(it.value());
        return result;
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const auto &item : value.toArray())
            result.append(sanitize(item));
        return result;
    }
    return value;
}
QJsonObject merge(QJsonObject base, const QJsonObject &patch)
{
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (secret(it.key()))
            continue;
        if (it.value().isObject())
            base[it.key()] = merge(base.value(it.key()).toObject(), it.value().toObject());
        else
            base[it.key()] = sanitize(it.value());
    }
    return base;
}
} // namespace
struct ApiInterceptChain::Frame
{
    bool done = false, forwarded = false, resolved = false;
    ApiInterceptReply reply;
    QList<ApiInterceptDone> waiters;
    std::function<void()> retire;
};
ApiInterceptChain::ApiInterceptChain(QList<std::shared_ptr<ApiInterceptor>> entries, QObject *p)
    : QObject(p), m_entries(std::move(entries))
{
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const auto &a, const auto &b) {
        return a->priority != b->priority ? a->priority > b->priority : a->sequence < b->sequence;
    });
}
ApiInterceptChain::~ApiInterceptChain()
{
    cancel();
}
QJsonObject ApiInterceptChain::publicRequest(const QJsonObject &request)
{
    if (request.value("stage") == "transport")
        return request;
    auto query = request.value("query").toObject();
    // Some typed variants use top-level arguments (album_detail.id, follow.id),
    // others use a query object. Present both through the same public query.
    for (auto it = request.begin(); it != request.end(); ++it)
        if (it.key() != "operation" && it.key() != "identity" && it.key() != "query")
            query[it.key()] = it.value();
    const auto identity = request.value("identity").toObject();
    QStringList authorization;
    const auto add = [&](const char *field, const char *name) {
        const auto value = identity.value(field);
        const auto text = value.isString() ? value.toString() : QString::number(value.toInteger());
        if (!value.isUndefined() && !text.isEmpty() && text != "0")
            authorization.append(QString::fromLatin1(name) + '=' + text);
    };
    add("token", "token");
    add("userid", "userid");
    add("t1", "t1");
    add("dfid", "dfid");
    add("mid", "KUGOU_API_MID");
    add("uuid", "uuid");
    add("guid", "KUGOU_API_GUID");
    add("device_name", "KUGOU_API_DEV");
    add("mac", "KUGOU_API_MAC");
    return {{"stage", "api"},
            {"operation", request.value("operation")},
            {"query", sanitize(query)},
            {"headers", QJsonObject{{"Authorization", authorization.join(';')}}}};
}
QJsonObject ApiInterceptChain::applyQueryPatch(QJsonObject request, const QJsonObject &patch)
{
    if (request.value("stage") == "transport") {
        for (const auto &key : {"url", "method", "headers", "bodyBase64"})
            if (patch.contains(key))
                request[key] = patch.value(key);
        return request;
    }
    // The operation, origin, identity and verification policy are host-owned.
    if (patch.contains("query")) {
        const auto queryPatch = patch.value("query").toObject();
        QJsonObject nested;
        for (auto it = queryPatch.begin(); it != queryPatch.end(); ++it) {
            if (secret(it.key()) || it.key() == "operation" || it.key() == "query")
                continue;
            if (request.contains(it.key()) || !request.contains("query")) {
                const auto updated = merge(request, QJsonObject{{it.key(), it.value()}});
                request[it.key()] = updated.value(it.key());
            } else
                nested[it.key()] = it.value();
        }
        if (!nested.isEmpty())
            request["query"] = merge(request.value("query").toObject(), nested);
    }
    return request;
}
void ApiInterceptChain::run(QJsonObject request, ApiInterceptNext transport, ApiInterceptDone done)
{
    m_transport = std::move(transport);
    QPointer<ApiInterceptChain> guard(this);
    invoke(0, std::move(request), [guard, done = std::move(done)](auto reply) {
        if (!guard || !guard->m_active)
            return;
        guard->cancel();
        done(std::move(reply));
    });
}
void ApiInterceptChain::invoke(int index, QJsonObject request, ApiInterceptDone complete)
{
    if (!m_active)
        return;
    while (index < m_entries.size() && !m_entries[index]->active)
        ++index;
    if (index == m_entries.size()) {
        m_transport(std::move(request), std::move(complete));
        return;
    }
    auto frame = std::make_shared<Frame>();
    m_frames.append(frame);
    QPointer<ApiInterceptChain> guard(this);
    const auto done = [guard, frame, complete = std::move(complete)](auto reply) {
        if (!guard || !guard->m_active || std::exchange(frame->done, true))
            return;
        complete(std::move(reply));
    };
    const auto next = [guard, frame, index, request](QJsonObject patch, ApiInterceptDone waiter) {
        if (!guard || !guard->m_active || frame->done)
            return;
        if (frame->resolved) {
            waiter(frame->reply);
            return;
        }
        frame->waiters.append(std::move(waiter));
        if (std::exchange(frame->forwarded, true))
            return;
        guard->invoke(index + 1, applyQueryPatch(request, patch), [guard, frame](auto reply) {
            if (!guard || !guard->m_active)
                return;
            frame->resolved = true;
            frame->reply = reply;
            const auto waiters = std::exchange(frame->waiters, {});
            for (const auto &waiter : waiters) {
                if (!guard || !guard->m_active)
                    break;
                waiter(reply);
            }
        });
    };
    auto retire = m_entries[index]->invoke(publicRequest(request), next, done);
    if (!m_active) {
        if (retire)
            retire();
    } else
        frame->retire = std::move(retire);
}
void ApiInterceptChain::cancel()
{
    if (!std::exchange(m_active, false))
        return;
    for (const auto &frame : std::exchange(m_frames, {})) {
        frame->waiters.clear();
        if (auto retire = std::exchange(frame->retire, {}))
            retire();
    }
    m_transport = {};
    m_entries.clear();
}
} // namespace EchoMusic
