#include "EchoPluginWebServer.h"
#include "PluginWebServerWorker_p.h"
#include "EchoPluginContext.h"
#include "AccountService.h"
#include "ApiClient.h"
#include <QTimer>
#include <cmath>
namespace EchoMusic
{
namespace
{
bool integer(const QVariantMap &o, const QString &key, int def, int low, int high)
{
    bool ok;
    double v = o.value(key, def).toDouble(&ok);
    return ok && std::isfinite(v) && v >= low && v <= high && v == std::floor(v) &&
           o.value(key).metaType().id() != QMetaType::Bool;
}
} // namespace
EchoPluginWebServer::EchoPluginWebServer(EchoPluginContext &s)
    : QObject(&s), m_scope(s), m_worker(new PluginWebServerWorker(*this))
{
    m_thread.setObjectName("PluginWebServer-" + s.installationId());
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, this, [this] {
        m_drained = true;
        emit drained();
    });
    if (s.host().permissions)
        connect(s.host().permissions, &PluginPermissionStore::revoked, this,
                [this](const QString &id, const QString &cap) {
                    if (id == m_scope.installationId() && cap == "webServer")
                        stop();
                });
    connect(&s.host().account, &AccountService::sessionAdopted, this, &EchoPluginWebServer::stop);
    connect(&s.host().api, &ApiClient::sessionChanged, this, &EchoPluginWebServer::stop);
}
EchoPluginWebServer::~EchoPluginWebServer()
{
    shutdown();
    m_thread.wait();
}
void EchoPluginWebServer::start()
{
    if (!m_started) {
        m_started = true;
        m_thread.start();
    }
}
EchoPluginOperation *EchoPluginWebServer::listen(const QVariantMap &input)
{
    if (m_stopping)
        return m_scope.rejected("ScopeClosed", "Server scope has ended");
    if (m_listening)
        return m_scope.rejected("Busy", "Server already running or starting");
    for (auto i = input.begin(); i != input.end(); ++i)
        if (!QStringList{"port", "host", "path", "maxConnections", "maxMessageBytes",
                         "maxBufferedBytes", "allowCrossOrigin"}
                 .contains(i.key()))
            return m_scope.rejected("InvalidArgument", "Unknown server option");
    if (!integer(input, "port", 0, 0, 65535) || !integer(input, "maxConnections", 16, 1, 64) ||
        !integer(input, "maxMessageBytes", 1024 * 1024, 1, 8 * 1024 * 1024) ||
        !integer(input, "maxBufferedBytes", 4 * input.value("maxMessageBytes", 1024 * 1024).toInt(),
                 1, 32 * 1024 * 1024) ||
        !QStringList{"127.0.0.1", "localhost"}.contains(
            input.value("host", "127.0.0.1").toString()))
        return m_scope.rejected("InvalidArgument", "Invalid server limits or loopback address");
    if (input.contains("allowCrossOrigin") &&
        input.value("allowCrossOrigin").metaType().id() != QMetaType::Bool)
        return m_scope.rejected("InvalidArgument", "allowCrossOrigin must be boolean");
    const auto path = input.value("path", "/").toString();
    if (!path.startsWith('/') || path.size() > 2048 || path.contains('?') || path.contains('#'))
        return m_scope.rejected("InvalidArgument", "Invalid WebSocket path");
    QVariantMap options{{"port", 0},
                        {"maxConnections", 16},
                        {"maxMessageBytes", 1024 * 1024},
                        {"path", "/"},
                        {"allowCrossOrigin", false}};
    for (auto i = input.begin(); i != input.end(); ++i)
        options[i.key()] = i.value();
    options["maxBufferedBytes"] = input.value(
        "maxBufferedBytes", qMin(4 * options.value("maxMessageBytes").toInt(), 32 * 1024 * 1024));
    if (options.value("maxBufferedBytes").toInt() < options.value("maxMessageBytes").toInt())
        return m_scope.rejected("InvalidArgument", "Buffer must hold at least one message");
    m_listening = true;
    auto *op = m_scope.permissions()->authorize(
        "webServer",
        (options.value("port").toInt() ? tr("监听本机端口 %1").arg(options.value("port").toInt())
                                       : tr("在本机自动分配端口")),
        [this, options](auto *operation) {
            if (m_stopping) {
                operation->cancel();
                return;
            }
            start();
            m_pending[operation->id()] = operation;
            QMetaObject::invokeMethod(m_worker, [w = m_worker, id = operation->id(), options] {
                w->listen(id, options);
            });
        });
    m_opening = op;
    connect(op, &EchoPluginOperation::completed, this, [this, op] {
        if (m_opening == op)
            m_opening = nullptr;
        m_pending.remove(op->id());
        if (op->state() == EchoPluginOperation::Canceled)
            stop();
        else if (op->state() == EchoPluginOperation::Failed)
            m_listening = false;
    });
    if (op->finished() && op->state() != EchoPluginOperation::Succeeded)
        m_listening = false;
    return op;
}
EchoPluginRegistration *EchoPluginWebServer::handler(const QString &kind, const QString &id,
                                                     const QJSValue &callback)
{
    if (!callback.isCallable())
        return nullptr;
    auto *lease = m_scope.registration("server." + kind, id);
    if (!lease)
        return nullptr;
    lease->stage(
        [this, kind, callback, lease] {
            if (auto previous = m_leases.value(kind); previous && previous != lease)
                previous->dispose();
            m_leases[kind] = lease;
            m_callbacks[kind] = callback;
            return true;
        },
        [this, kind, lease] {
            if (m_leases.value(kind) == lease) {
                m_leases.remove(kind);
                m_callbacks.remove(kind);
                if (kind == "connection") {
                    for (const auto &id : m_sockets.keys()) {
                        closeSocket(id, 1001, "Handler disposed");
                        receive(id, "closed", {{"code", 1001}, {"reason", "Handler disposed"}});
                    }
                }
                const auto handlers = m_handlers;
                for (auto op : handlers)
                    if (op && op->property("_echoServerKind").toString() == kind)
                        op->cancel();
            }
        });
    if (m_scope.committed())
        lease->commit();
    return lease;
}
EchoPluginRegistration *EchoPluginWebServer::onRequest(const QString &id, const QJSValue &callback)
{
    return handler("request", id, callback);
}
EchoPluginRegistration *EchoPluginWebServer::onConnection(const QString &id,
                                                          const QJSValue &callback)
{
    return handler("connection", id, callback);
}
EchoPluginRegistration *EchoPluginWebServer::onUpgrade(const QString &id, const QJSValue &callback)
{
    return handler("upgrade", id, callback);
}
void EchoPluginWebServer::dispatch(const QString &kind, const QString &id, const QVariantMap &input)
{
    if (m_stopping || !m_scope.active() || !m_scope.committed() ||
        !m_scope.permissions()->granted("webServer"))
        return;
    auto request = input;
    request["pluginId"] = m_scope.id();
    const auto callback = m_callbacks.value(kind);
    auto lease = m_leases.value(kind);
    if (!callback.isCallable() || !lease || !lease->active()) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, kind, id] {
            if (kind == "upgrade")
                w->upgrade(id, true, false);
            else
                w->respond(id, QVariantMap{{"status", 404}, {"body", "No request handler"}}, false);
        });
        return;
    }
    auto *op = m_scope.invokeRegistered(callback, lease, request);
    op->setProperty("_echoServerKind", kind);
    m_handlers[id] = op;
    connect(op, &EchoPluginOperation::completed, this, [this, kind, id, op] {
        m_handlers.remove(id);
        if (!m_started || m_stopping)
            return;
        const bool failed = op->state() != EchoPluginOperation::Succeeded;
        const auto value = op->result();
        QMetaObject::invokeMethod(m_worker, [w = m_worker, kind, id, value, failed] {
            if (kind == "upgrade")
                w->upgrade(id, value, failed);
            else
                w->respond(id, value, failed);
        });
    });
    auto *timer = new QTimer(op);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, op, [op] { op->fail("Timeout", "Server handler timed out"); });
    timer->start(kind == "upgrade" ? 10000 : 30000);
}
void EchoPluginWebServer::deliver(const QString &id, QVariant value, const QString &code,
                                  const QString &message)
{
    auto op = m_pending.take(id);
    if (!op || op->finished())
        return;
    if (code.isEmpty())
        op->succeed(value);
    else
        op->fail(code, message);
}
void EchoPluginWebServer::receive(const QString &id, const QString &kind, const QVariantMap &data)
{
    if (kind == "cancel") {
        if (auto op = m_handlers.take(id))
            op->cancel();
        return;
    }
    if (kind == "open") {
        if (m_stopping || !m_scope.active() || !m_scope.permissions()->granted("webServer")) {
            closeSocket(id, 1001, "Scope closed");
            return;
        }
        auto request = data;
        request["pluginId"] = m_scope.id();
        auto *socket = new EchoPluginWebSocket(*this, id, request);
        m_sockets[id] = socket;
        m_scope.engine().newQObject(socket);
        const auto cb = m_callbacks.value("connection");
        auto lease = m_leases.value("connection");
        if (cb.isCallable() && lease && lease->active()) {
            const auto result = m_scope.call(cb, {m_scope.engine().newQObject(socket)});
            if (result.isCallable())
                socket->m_cleanup = result;
            else if (result.isError())
                socket->close(1011, "Connection handler failed");
        }
        return;
    }
    auto socket = m_sockets.value(id);
    if (!socket)
        return;
    if (kind == "closed") {
        m_sockets.remove(id);
        socket->m_closed = true;
        if (m_scope.active()) {
            emit socket->closed(data.value("code").toInt(), data.value("reason").toString());
        }
        if (socket->m_cleanup.isCallable())
            m_scope.call(socket->m_cleanup);
        socket->m_cleanup = QJSValue();
        emit socket->changed();
        socket->setParent(nullptr);
        QQmlEngine::setObjectOwnership(socket, QQmlEngine::JavaScriptOwnership);
        return;
    }
    if (!m_scope.active())
        return;
    if (kind == "message")
        emit socket->message(data.value("data"), data.value("binary").toBool());
    else if (kind == "error")
        emit socket->error(data.value("message").toString());
    else if (kind == "pong")
        emit socket->pong(data.value("elapsed").toDouble(), data.value("bytes").toByteArray());
}
EchoPluginOperation *EchoPluginWebServer::send(const QString &id, const QString &kind,
                                               const QByteArray &bytes)
{
    if (m_stopping || !m_scope.permissions()->granted("webServer"))
        return m_scope.rejected("PermissionDenied", "Server permission is unavailable");
    if (!m_sockets.contains(id) || !m_sockets.value(id) || m_sockets.value(id)->readyState() != 1)
        return m_scope.rejected("ConnectionClosed", "WebSocket is closed");
    if (bytes.size() > 8 * 1024 * 1024 || (kind == "ping" && bytes.size() > 125))
        return m_scope.rejected("InvalidArgument", "WebSocket payload too large");
    if (m_pending.size() >= 64 || m_queuedBytes + bytes.size() > 32 * 1024 * 1024)
        return m_scope.rejected("Backpressure", "WebSocket dispatch queue is full");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    m_payloads[op->id()] = bytes.size();
    m_queuedBytes += bytes.size();
    op->start([this, id] { closeSocket(id, 1001, "Operation canceled"); });
    m_pending[op->id()] = op;
    connect(op, &EchoPluginOperation::completed, this, [this, op] {
        m_pending.remove(op->id());
        m_queuedBytes -= m_payloads.take(op->id());
    });
    QMetaObject::invokeMethod(m_worker, [w = m_worker, id, oid = op->id(), kind, bytes] {
        w->send(id, oid, kind, bytes);
    });
    return op;
}
void EchoPluginWebServer::closeSocket(const QString &id, int code, const QString &reason)
{
    if (!m_started || m_stopping)
        return;
    const bool valid = code == 1000 || code == 1001 || code == 1002 || code == 1003 ||
                       code == 1007 || code == 1008 || code == 1009 || code == 1011 ||
                       code == 1012 || code == 1013 || (code >= 3000 && code <= 4999);
    if (!valid || reason.toUtf8().size() > 123)
        return;
    QMetaObject::invokeMethod(
        m_worker, [w = m_worker, id, code, reason] { w->closeSocket(id, code, reason); });
}
void EchoPluginWebServer::stop()
{
    ++m_revision;
    m_listening = false;
    auto opening = m_opening;
    m_opening = nullptr;
    if (opening)
        opening->cancel();
    const auto handlers = m_handlers;
    for (auto op : handlers)
        if (op)
            op->cancel();
    m_handlers.clear();
    const auto pending = m_pending;
    m_pending.clear();
    for (auto op : pending)
        if (op)
            op->cancel();
    const auto ids = m_sockets.keys();
    for (const auto &id : ids)
        receive(id, "closed", {{"code", 1001}, {"reason", "Plugin service stopped"}});
    m_status = {{"running", false}, {"connections", 0}, {"pendingRequests", 0}};
    emit changed();
    if (m_started && !m_stopping)
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->close(); });
}
EchoPluginOperation *EchoPluginWebServer::close()
{
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    stop();
    op->start();
    if (!m_started) {
        op->succeed();
        return op;
    }
    m_pending[op->id()] = op;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, id = op->id()] {
        w->close();
        w->result(id);
    });
    return op;
}
void EchoPluginWebServer::shutdown()
{
    if (m_stopping)
        return;
    stop();
    m_stopping = true;
    if (m_started)
        QMetaObject::invokeMethod(m_worker, [w = m_worker] {
            w->close();
            w->thread()->quit();
        });
    else {
        delete m_worker;
        m_worker = nullptr;
        m_drained = true;
        emit drained();
    }
}
EchoPluginWebSocket::EchoPluginWebSocket(EchoPluginWebServer &o, QString id, QVariantMap r)
    : QObject(&o), m_owner(&o), m_id(std::move(id)), m_request(std::move(r))
{
}
EchoPluginOperation *EchoPluginWebSocket::sendText(const QString &text)
{
    return m_owner ? m_owner->send(m_id, "text", text.toUtf8()) : nullptr;
}
EchoPluginOperation *EchoPluginWebSocket::sendBytes(const QByteArray &bytes)
{
    return m_owner ? m_owner->send(m_id, "bytes", bytes) : nullptr;
}
EchoPluginOperation *EchoPluginWebSocket::ping(const QByteArray &bytes)
{
    return m_owner ? m_owner->send(m_id, "ping", bytes) : nullptr;
}
void EchoPluginWebSocket::close(int code, const QString &reason)
{
    if (m_owner)
        m_owner->closeSocket(m_id, code, reason);
}
} // namespace EchoMusic
