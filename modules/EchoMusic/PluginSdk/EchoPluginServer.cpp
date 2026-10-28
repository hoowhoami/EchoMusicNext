#include "EchoPluginServer.h"
#include "EchoPluginContext.h"
#include "EchoPluginPromise.h"
#include "ApiClient.h"
#include "AccountService.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QStringConverter>
#include <QTimer>
#include <cmath>
#include <utility>
namespace EchoMusic
{
namespace
{
QJSValue invokeMiddleware(EchoPluginContext &scope, const QJSValue &callback,
                          const QJSValueList &args)
{
    // Privileged middleware sees signed URLs/authorization. Never persist an
    // arbitrary thrown exception that may stringify these credentials.
    auto values = scope.engine().newArray(uint(args.size()));
    for (int i = 0; i < args.size(); ++i)
        values.setProperty(uint(i), args[i]);
    const auto safe = scope.engine().evaluate(
        "(function(fn,args){try{return {ok:true,value:fn.apply(undefined,args)}}catch(e){return "
        "{ok:false}}})");
    const auto result = scope.call(safe, {callback, values});
    if (!result.property("ok").toBool()) {
        return scope.engine().newErrorObject(QJSValue::GenericError, "Host interceptor failed");
    }
    return result.property("value");
}
} // namespace
EchoPluginInterceptRequest::EchoPluginInterceptRequest(EchoPluginContext &s, QJsonObject r,
                                                       ApiInterceptNext next, ApiInterceptDone done,
                                                       int timeout)
    : QObject(&s), m_scope(s), m_request(std::move(r)), m_next(std::move(next)),
      m_done(std::move(done))
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    m_deadline.setInterval(timeout);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (m_active) {
            emit m_scope.callbackFailed("Host request interceptor timed out");
            fallback();
        }
    });
    m_deadline.start();
}
QVariantMap EchoPluginInterceptRequest::response(const ApiInterceptReply &reply, bool wire)
{
    const auto document = QJsonDocument::fromJson(reply.payload);
    if (wire)
        return document.object().toVariantMap();
    return {{"status", reply.status}, {"body", document.toVariant()}};
}
EchoPluginOperation *EchoPluginInterceptRequest::next(const QVariantMap &patch)
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    if (!m_active) {
        op->fail("RequestClosed", "Host request has ended");
        return op;
    }
    const auto fields = stage() == "transport"
                            ? QStringList{"url", "method", "headers", "bodyBase64"}
                            : QStringList{"query"};
    bool valid = true;
    for (auto it = patch.begin(); it != patch.end(); ++it)
        valid = valid && fields.contains(it.key());
    if (!valid ||
        QJsonDocument(QJsonObject::fromVariantMap(patch)).toJson(QJsonDocument::Compact).size() >
            24 * 1024 * 1024) {
        op->fail("InvalidArgument", "Only a bounded query patch is accepted");
        return op;
    }
    QPointer<EchoPluginOperation> guard(op);
    m_waits.append(guard);
    op->start();
    const bool wire = stage() == "transport";
    QPointer<EchoPluginInterceptRequest> self(this);
    m_deadline.stop();
    m_next(QJsonObject::fromVariantMap(patch), [guard, wire, self](const auto &reply) {
        if (self && self->active())
            self->m_deadline.start();
        if (guard && !guard->finished())
            guard->succeed(response(reply, wire));
    });
    return op;
}
void EchoPluginInterceptRequest::settle(const QVariant &value)
{
    if (!m_active)
        return;
    const auto map = value.toMap();
    if (stage() == "transport") {
        if ((map.value("status").toInt() < 100 &&
             !(map.value("status").toInt() == 0 && map.contains("error"))) ||
            map.value("status").toInt() > 599 || !map.contains("bodyBase64") ||
            !map.contains("headers")) {
            fallback();
            return;
        }
        const auto bytes =
            QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact);
        if (bytes.size() > 24 * 1024 * 1024) {
            fallback();
            return;
        }
        auto done = std::exchange(m_done, {});
        invalidate();
        if (done)
            done({0, bytes});
        return;
    }
    const auto status = map.value("status");
    if (!map.contains("body") || !status.isValid() || status.toInt() < 0 || status.toInt() > 5) {
        fallback();
        return;
    }
    const auto body = QJsonValue::fromVariant(map.value("body"));
    if (!body.isArray() && !body.isObject()) {
        fallback();
        return;
    }
    const auto bytes =
        (body.isArray() ? QJsonDocument(body.toArray()) : QJsonDocument(body.toObject()))
            .toJson(QJsonDocument::Compact);
    if (bytes.size() > 16 * 1024 * 1024) {
        fallback();
        return;
    }
    auto done = std::exchange(m_done, {});
    invalidate();
    if (done)
        done({status.toInt(), bytes});
}
void EchoPluginInterceptRequest::fallback()
{
    if (!m_active)
        return;
    // next is memoized by the host. Even an exception AFTER a write forwarded
    // cannot replay it. Existing in-flight response becomes this frame's result.
    auto next = m_next;
    auto done = std::exchange(m_done, {});
    invalidate();
    next({}, std::move(done));
}
void EchoPluginInterceptRequest::invalidate()
{
    if (!std::exchange(m_active, false))
        return;
    m_deadline.stop();
    m_next = {};
    m_done = {};
    for (auto op : std::exchange(m_waits, {}))
        if (op && !op->finished())
            op->cancel();
    emit changed();
    deleteLater();
}
EchoPluginServer::EchoPluginServer(EchoPluginContext &s) : QObject(&s), m_scope(s)
{
    connect(&s.host().api, &ApiClient::sessionChanged, this, [this] { reset(); });
    connect(&s.host().account, &AccountService::sessionAdopted, this, [this] { reset(); });
    if (s.host().permissions)
        connect(s.host().permissions, &PluginPermissionStore::revoked, this,
                [this](const QString &installation, const QString &cap) {
                    if (installation == m_scope.installationId() && cap == "serverIntercept")
                        reset();
                });
}
QString EchoPluginServer::encodeBody(const QVariant &value, const QString &encoding)
{
    QByteArray bytes;
    if (encoding == "utf8" && value.metaType() == QMetaType::fromType<QString>())
        bytes = value.toString().toUtf8();
    else if (encoding == "json") {
        bytes = QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)})
                    .toJson(QJsonDocument::Compact);
        bytes = bytes.mid(1, bytes.size() - 2);
    } else {
        m_scope.engine().throwError(QJSValue::TypeError, "Expected json or utf8 encoding");
        return {};
    }
    if (bytes.size() > 16 * 1024 * 1024) {
        m_scope.engine().throwError(QJSValue::RangeError, "Body exceeds 16 MiB");
        return {};
    }
    return QString::fromLatin1(bytes.toBase64());
}
QVariant EchoPluginServer::decodeBody(const QString &base64, const QString &encoding)
{
    if (base64.size() > 24 * 1024 * 1024) {
        m_scope.engine().throwError(QJSValue::RangeError, "Body exceeds 16 MiB");
        return {};
    }
    const auto decoded =
        QByteArray::fromBase64Encoding(base64.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (base64.size() > 24 * 1024 * 1024 || !decoded || decoded.decoded.size() > 16 * 1024 * 1024) {
        m_scope.engine().throwError(QJSValue::TypeError, "Invalid or oversized Base64 body");
        return {};
    }
    if (encoding == "json") {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson('[' + decoded.decoded + ']', &error);
        if (error.error == QJsonParseError::NoError && document.array().size() == 1)
            return document.array().first().toVariant();
    } else if (encoding == "utf8") {
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder(decoded.decoded);
        if (!decoder.hasError())
            return text;
    }
    m_scope.engine().throwError(QJSValue::TypeError, "Invalid body encoding or content");
    return {};
}
void EchoPluginServer::reset()
{
    for (auto lease : std::exchange(m_leases, {}))
        if (lease)
            lease->dispose();
}
EchoPluginOperation *EchoPluginServer::intercept(const QJSValue &definition)
{
    const bool wire = definition.property("stage").toString() == "transport";
    const auto id = definition.property("id").toString();
    const auto handler = definition.property("handle");
    const auto match = definition.property("match");
    const auto priority = definition.property("priority").isUndefined()
                              ? 0.
                              : definition.property("priority").toNumber();
    const auto timeout = definition.property("timeoutMs").isUndefined()
                             ? 2500.
                             : definition.property("timeoutMs").toNumber();
    if (!handler.isCallable() || !std::isfinite(priority) || priority < -100000 ||
        priority > 100000 || !std::isfinite(timeout) || timeout < 1 || timeout > 30000 ||
        (!match.isUndefined() && !match.isString() && !match.isCallable() && !match.isRegExp()))
        return m_scope.rejected("InvalidArgument", "Invalid host interceptor definition");
    m_leases.removeIf([](const auto &lease) { return !lease || !lease->active(); });
    if (m_leases.size() >= 32)
        return m_scope.rejected("LimitExceeded", "At most 32 interceptors per scope");
    return m_scope.permissions()->authorize(
        "serverIntercept",
        tr("可读取和修改宿主请求、请求头和原始响应，包括其中的账号资料。仅允许您信任的插件；登录及"
           "验证命令和插件自身请求不经过此链。"),
        [this, id, handler, match, priority, timeout, wire](EchoPluginOperation *op) {
            auto *lease = m_scope.registration("serverIntercept", id);
            if (!lease) {
                op->fail("InvalidArgument", "Invalid interceptor id");
                return;
            }
            m_leases.append(lease);
            auto token = std::make_shared<quint64>(0);
            lease->stage(
                [this, lease, token, handler, match, priority, timeout, wire] {
                    *token = m_scope.host().api.addInterceptor(
                        priority,
                        [this, lease, handler, match, timeout](const QJsonObject &request,
                                                               ApiInterceptNext next,
                                                               ApiInterceptDone done) {
                            if (!m_scope.active() || !lease->active()) {
                                next({}, done);
                                return std::function<void()>{};
                            }
                            if (match.isString() && !request.value("operation")
                                                         .toString()
                                                         .startsWith(match.toString())) {
                                next({}, done);
                                return std::function<void()>{};
                            }
                            if (match.isRegExp()) {
                                const auto test = m_scope.engine().evaluate(
                                    "(function(re,op){re.lastIndex=0;return re.test(op)})");
                                const auto matches = invokeMiddleware(
                                    m_scope, test, {match, request.value("operation").toString()});
                                if (matches.isError() || !matches.toBool()) {
                                    if (matches.isError())
                                        emit m_scope.callbackFailed(
                                            "Host request interceptor matcher failed");
                                    next({}, done);
                                    return std::function<void()>{};
                                }
                            }
                            if (match.isCallable()) {
                                const auto matches = invokeMiddleware(
                                    m_scope, match,
                                    {m_scope.engine().toScriptValue(request.toVariantMap())});
                                if (matches.isError() || !matches.toBool()) {
                                    if (matches.isError())
                                        emit m_scope.callbackFailed(
                                            "Host request interceptor matcher failed");
                                    next({}, done);
                                    return std::function<void()>{};
                                }
                            }
                            auto *call = new EchoPluginInterceptRequest(m_scope, request, next,
                                                                        done, int(timeout));
                            QPointer<EchoPluginInterceptRequest> guard(call);
                            connect(lease, &EchoPluginRegistration::activeChanged, call, [guard] {
                                if (guard)
                                    guard->fallback();
                            });
                            auto *result = m_scope.operation();
                            result->start();
                            connect(result, &EchoPluginOperation::completed, call,
                                    [this, guard, result] {
                                        if (!guard || !guard->active())
                                            return;
                                        if (result->state() == EchoPluginOperation::Succeeded)
                                            guard->settle(result->result());
                                        else {
                                            if (result->state() == EchoPluginOperation::Failed)
                                                emit m_scope.callbackFailed(
                                                    "Host request interceptor rejected its result");
                                            guard->fallback();
                                        }
                                    });
                            connect(call, &EchoPluginInterceptRequest::changed, result, [result] {
                                if (!result->finished())
                                    result->cancel();
                            });
                            const auto value = invokeMiddleware(
                                m_scope, handler, {m_scope.engine().newQObject(call)});
                            if (value.isError()) {
                                result->fail("PluginError", value.toString());
                            } else {
                                auto *bridge = new EchoPluginPromise(*result, &m_scope);
                                QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
                                const auto attach = m_scope.engine().evaluate(
                                    "(function(value,b){Promise.resolve(value).then(function(v){"
                                    "try{b.resolve(v)}catch(e){}},"
                                    "function(e){try{b.reject(String(e))}catch(ignored){}})})");
                                m_scope.call(attach, {value, m_scope.engine().newQObject(bridge)});
                            }
                            return std::function<void()>([guard] {
                                if (guard)
                                    guard->invalidate();
                            });
                        },
                        wire);
                    return true;
                },
                [this, token] { m_scope.host().api.removeInterceptor(*token); });
            if (m_scope.committed() && !lease->commit()) {
                op->fail("Unavailable", "Cannot publish host interceptor");
                return;
            }
            op->succeed(QVariant::fromValue(static_cast<QObject *>(lease)));
        });
}
} // namespace EchoMusic
