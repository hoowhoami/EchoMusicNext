#include "EchoPluginWebSocketClient.h"
#include "EchoPluginContext.h"
#include "AccountService.h"
#include "ApiClient.h"
#include <QNetworkRequest>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>

namespace EchoMusic
{
namespace
{
constexpr qint64 messageLimit = 1024 * 1024;
constexpr qint64 bufferLimit = 4 * messageLimit;
struct DeliveryBudget
{
    std::atomic<qint64> bytes{0};
    std::atomic<int> events{0};
};
bool closeCodeValid(int code)
{
    return (code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) ||
           (code >= 3000 && code <= 4999);
}
} // namespace
class PluginWebSocketClientWorker final : public QObject
{
public:
    struct Connection
    {
        QWebSocket *socket;
        QTimer *timer;
        QString opening;
    };
    explicit PluginWebSocketClientWorker(EchoPluginWebSocketClient &owner) : owner(owner) {}
    void result(const QString &op, QVariant value = {}, QString code = {}, QString message = {})
    {
        if (!op.isEmpty())
            QMetaObject::invokeMethod(&owner, [target = &owner, op, value, code, message] {
                target->result(op, value, code, message);
            });
    }
    void finish(const QString &id, QString code = {}, QString message = {})
    {
        if (!connections.contains(id))
            return;
        const auto c = connections.take(id);
        result(c.opening, {}, code.isEmpty() ? "ConnectionClosed" : code,
               message.isEmpty() ? "WebSocket closed during handshake" : message);
        const auto closeCode = int(c.socket->closeCode());
        const auto reason = c.socket->closeReason();
        c.socket->disconnect(this);
        c.socket->abort();
        c.socket->deleteLater();
        QMetaObject::invokeMethod(&owner, [target = &owner, id, closeCode, reason, message] {
            if (!message.isEmpty())
                target->receive(id, "error", message);
            target->closed(id, closeCode, reason);
        });
    }
    void deliver(const QString &id, const QString &kind, const QVariant &data, qint64 size,
                 bool binary = false)
    {
        // Bound the worker -> GUI event backlog even if a plugin stalls the JS engine.
        if (budget->events.load() >= 128 || budget->bytes.load() + size > 8 * messageLimit) {
            finish(id, "Backpressure", "WebSocket receive queue is full");
            return;
        }
        budget->events.fetch_add(1);
        budget->bytes.fetch_add(size);
        QMetaObject::invokeMethod(&owner,
                                  [target = &owner, id, kind, data, binary, size, b = budget] {
                                      b->events.fetch_sub(1);
                                      b->bytes.fetch_sub(size);
                                      target->receive(id, kind, data, binary);
                                  });
    }
    void open(const QString &id, const QString &op, const QNetworkRequest &request,
              const QStringList &protocols, int timeout)
    {
        if (connections.size() >= 16) {
            result(op, {}, "QuotaExceeded", "At most 16 outbound WebSocket connections per scope");
            return;
        }
        auto *socket = new QWebSocket(QString{}, QWebSocketProtocol::VersionLatest, this);
        // Query by URL before Qt creates its TCP/SSL socket: the latter's generic
        // TCP query loses the ws/wss protocol tag used by the application router.
        const auto proxies = QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery(request.url()));
        if (proxies.isEmpty()) {
            socket->deleteLater();
            result(op, {}, "NetworkError", "No WebSocket transport route is available");
            return;
        }
        socket->setProxy(proxies.first());
        // Never inherit an HTTP caller's optional TLS verification override.
        auto ssl = QSslConfiguration::defaultConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket->setSslConfiguration(ssl);
        // Qt's frame decoder may wait for a complete frame before consuming it.
        // A smaller socket buffer would stall otherwise valid large messages.
        socket->setReadBufferSize(messageLimit + 1024);
        socket->setMaxAllowedIncomingFrameSize(messageLimit);
        socket->setMaxAllowedIncomingMessageSize(messageLimit);
        socket->setOutgoingFrameSize(64 * 1024);
        auto *timer = new QTimer(socket);
        timer->setSingleShot(true);
        connections.insert(id, {socket, timer, op});
        QObject::connect(timer, &QTimer::timeout, this,
                         [this, id] { finish(id, "Timeout", "WebSocket operation timed out"); });
        QObject::connect(socket, &QWebSocket::connected, this, [this, id, socket] {
            if (!connections.contains(id))
                return;
            auto &c = connections[id];
            c.timer->stop();
            result(std::exchange(c.opening, {}),
                   QVariantMap{{"id", id},
                               {"url", socket->requestUrl().toString()},
                               {"protocol", socket->subprotocol()}});
        });
        QObject::connect(
            socket, &QWebSocket::textMessageReceived, this,
            [this, id](const QString &text) { deliver(id, "message", text, text.size() * 2); });
        QObject::connect(socket, &QWebSocket::binaryMessageReceived, this,
                         [this, id](const QByteArray &bytes) {
                             deliver(id, "message", bytes, bytes.size(), true);
                         });
        QObject::connect(
            socket, &QWebSocket::pong, this, [this, id](quint64 elapsed, const QByteArray &bytes) {
                deliver(id, "pong", QVariantList{double(elapsed), bytes}, bytes.size());
            });
        QObject::connect(socket, &QWebSocket::sslErrors, this,
                         [this, id](const QList<QSslError> &errors) {
                             QStringList reasons;
                             for (qsizetype i = 0; i < qMin(errors.size(), qsizetype(8)); ++i)
                                 reasons.append(errors[i].errorString());
                             finish(id, "TlsError", reasons.join("; "));
                         });
        QObject::connect(socket, &QWebSocket::errorOccurred, this, [this, id, socket](auto error) {
            finish(id,
                   error == QAbstractSocket::SslHandshakeFailedError ? "TlsError" : "NetworkError",
                   socket->errorString());
        });
        QObject::connect(socket, &QWebSocket::disconnected, this, [this, id] { finish(id); });
        QWebSocketHandshakeOptions handshake;
        handshake.setSubprotocols(protocols);
        timer->start(timeout);
        socket->open(request, handshake);
    }
    void send(const QString &id, const QString &op, const QString &kind, const QByteArray &bytes)
    {
        const auto c = connections.value(id);
        if (!c.socket || c.socket->state() != QAbstractSocket::ConnectedState) {
            result(op, {}, "ConnectionClosed", "WebSocket is not open");
            return;
        }
        if (c.socket->bytesToWrite() + bytes.size() > bufferLimit) {
            result(op, {}, "Backpressure", "WebSocket write buffer is full");
            return;
        }
        qint64 accepted = bytes.size();
        if (kind == "text")
            accepted = c.socket->sendTextMessage(QString::fromUtf8(bytes));
        else if (kind == "binary")
            accepted = c.socket->sendBinaryMessage(bytes);
        else
            c.socket->ping(bytes);
        if (accepted < 0)
            result(op, {}, "NetworkError", "WebSocket send failed");
        else
            result(op, accepted);
    }
    void close(const QString &id, int code, const QString &reason)
    {
        if (!connections.contains(id))
            return;
        auto c = connections.value(id);
        c.timer->start(5000);
        c.socket->close(QWebSocketProtocol::CloseCode(code), reason);
    }
    void closeAll()
    {
        for (const auto &id : connections.keys())
            finish(id);
    }
    EchoPluginWebSocketClient &owner;
    QHash<QString, Connection> connections;
    std::shared_ptr<DeliveryBudget> budget = std::make_shared<DeliveryBudget>();
};

EchoPluginWebSocketClient::EchoPluginWebSocketClient(EchoPluginContext &scope)
    : QObject(&scope), m_scope(scope), m_worker(new PluginWebSocketClientWorker(*this))
{
    m_thread.setObjectName("PluginWebSocket-" + scope.installationId());
    m_worker->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    QObject::connect(&m_thread, &QThread::finished, this, [this] {
        m_drained = true;
        emit drained();
    });
    if (scope.host().permissions)
        QObject::connect(scope.host().permissions, &PluginPermissionStore::revoked, this,
                         [this](const QString &id, const QString &cap) {
                             if (id == m_scope.installationId() && cap == "webSocket")
                                 retire();
                         });
    QObject::connect(&scope.host().account, &AccountService::sessionAdopted, this,
                     &EchoPluginWebSocketClient::retire);
    QObject::connect(&scope.host().api, &ApiClient::sessionChanged, this,
                     &EchoPluginWebSocketClient::retire);
}
EchoPluginWebSocketClient::~EchoPluginWebSocketClient()
{
    shutdown();
    m_thread.wait();
}
EchoPluginOperation *EchoPluginWebSocketClient::connect(const QString &input,
                                                        const QVariantMap &options)
{
    if (m_stopping || !m_scope.active())
        return m_scope.rejected("ScopeClosed", "WebSocket scope has ended");
    const QUrl url(input, QUrl::StrictMode);
    const auto invalid = [this] {
        return m_scope.rejected("InvalidArgument", "Invalid outbound WebSocket options");
    };
    if (!url.isValid() || !QStringList{"ws", "wss"}.contains(url.scheme()) ||
        url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment() ||
        input.size() > 8192)
        return invalid();
    for (auto it = options.begin(); it != options.end(); ++it)
        if (!QStringList{"headers", "protocols", "connectTimeoutMs"}.contains(it.key()))
            return invalid();
    bool ok;
    const auto timeout = options.value("connectTimeoutMs", 10000).toDouble(&ok);
    if (!ok || !std::isfinite(timeout) || timeout < 1 || timeout > 120000 ||
        std::floor(timeout) != timeout ||
        options.value("connectTimeoutMs").metaType().id() == QMetaType::Bool)
        return invalid();
    QNetworkRequest request(url);
    if (options.contains("headers") &&
        options.value("headers").metaType().id() != QMetaType::QVariantMap)
        return invalid();
    const QRegularExpression token("\\A[!#$%&'*+.^_`|~0-9A-Za-z-]+\\z");
    const auto headers = options.value("headers").toMap();
    qsizetype headerBytes = 0;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const auto name = it.key().toLower();
        const auto value = it.value().toString();
        if (!token.match(it.key()).hasMatch() || it.value().metaType().id() != QMetaType::QString ||
            value.contains('\r') || value.contains('\n') || value.contains(QChar::Null) ||
            QStringList{"host", "connection", "upgrade", "proxy-authorization"}.contains(name) ||
            name.startsWith("sec-websocket-"))
            return invalid();
        headerBytes += it.key().size() + value.toUtf8().size();
        request.setRawHeader(it.key().toLatin1(), value.toUtf8());
    }
    if (headerBytes > 32768)
        return invalid();
    QStringList protocols;
    if (options.contains("protocols")) {
        const auto value = options.value("protocols");
        if (value.metaType().id() != QMetaType::QVariantList &&
            value.metaType().id() != QMetaType::QStringList)
            return invalid();
        const auto list = value.toList();
        if (list.size() > 16)
            return invalid();
        for (const auto &p : list) {
            const auto protocol = p.toString();
            if (p.metaType().id() != QMetaType::QString || protocol.size() > 128 ||
                !token.match(protocol).hasMatch() || protocols.contains(protocol))
                return invalid();
            protocols.append(protocol);
        }
    }
    return m_scope.permissions()->authorize(
        "webSocket", tr("连接 WebSocket 服务 %1").arg(url.host()),
        [this, request, protocols, timeout](auto *op) {
            if (m_stopping) {
                op->cancel();
                return;
            }
            if (m_pending.size() >= 64) {
                op->fail("Busy", "Too many pending WebSocket operations");
                return;
            }
            if (!m_started) {
                m_started = true;
                m_thread.start();
            }
            const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_pending.insert(op->id(), op);
            QObject::connect(op, &EchoPluginOperation::completed, this, [this, id, op] {
                m_pending.remove(op->id());
                if (op->state() == EchoPluginOperation::Canceled)
                    abort(id);
            });
            QMetaObject::invokeMethod(
                m_worker, [w = m_worker, id, oid = op->id(), request, protocols, timeout] {
                    w->open(id, oid, request, protocols, int(timeout));
                });
        });
}
void EchoPluginWebSocketClient::result(const QString &id, const QVariant &input,
                                       const QString &code, const QString &message)
{
    auto op = m_pending.take(id);
    if (!op || op->finished())
        return;
    if (!code.isEmpty()) {
        op->fail(code, message);
        return;
    }
    QVariant value = input;
    if (input.metaType().id() == QMetaType::QVariantMap) {
        const auto result = input.toMap();
        auto *socket = new EchoPluginWebSocketConnection(*this, result.value("id").toString(),
                                                         result.value("url").toString(),
                                                         result.value("protocol").toString());
        m_connections.insert(socket->id(), socket);
        m_scope.engine().newQObject(socket);
        value = QVariant::fromValue(socket);
    }
    op->succeed(value);
}
EchoPluginOperation *EchoPluginWebSocketClient::send(const QString &id, const QString &kind,
                                                     const QByteArray &bytes)
{
    if (!m_scope.permissions()->granted("webSocket"))
        return m_scope.rejected("PermissionDenied", "WebSocket permission is unavailable");
    auto socket = m_connections.value(id);
    if (m_stopping || !socket || socket->readyState() != 1)
        return m_scope.rejected("ConnectionClosed", "WebSocket is not open");
    if (bytes.size() > (kind == "ping" ? 125 : messageLimit))
        return m_scope.rejected("InvalidArgument", "WebSocket payload exceeds its byte limit");
    if (m_pending.size() >= 64 || m_queuedBytes + bytes.size() > 8 * messageLimit)
        return m_scope.rejected("Backpressure", "WebSocket dispatch queue is full");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    m_pending.insert(op->id(), op);
    m_queuedBytes += bytes.size();
    QObject::connect(op, &EchoPluginOperation::completed, this,
                     [this, id, op, size = bytes.size()] {
                         m_pending.remove(op->id());
                         m_queuedBytes -= size;
                         if (op->state() == EchoPluginOperation::Canceled)
                             abort(id);
                     });
    QMetaObject::invokeMethod(m_worker, [w = m_worker, id, oid = op->id(), kind, bytes] {
        w->send(id, oid, kind, bytes);
    });
    return op;
}
void EchoPluginWebSocketClient::receive(const QString &id, const QString &kind,
                                        const QVariant &data, bool binary)
{
    const auto socket = m_connections.value(id);
    if (!socket || (socket->readyState() != 1 && (kind != "error" || socket->readyState() != 2)) ||
        !m_scope.permissions()->granted("webSocket"))
        return;
    if (kind == "message")
        emit socket->message(data, binary);
    else if (kind == "error")
        emit socket->error(data.toString());
    else if (kind == "pong") {
        const auto values = data.toList();
        emit socket->pong(values[0].toDouble(), values[1].toByteArray());
    }
}
void EchoPluginWebSocketClient::closed(const QString &id, int code, const QString &reason,
                                       bool notify)
{
    if (auto socket = m_connections.take(id)) {
        socket->m_state = 3;
        socket->m_closeCode = code;
        socket->m_closeReason = reason;
        socket->setParent(nullptr);
        QQmlEngine::setObjectOwnership(socket, QQmlEngine::JavaScriptOwnership);
        if (notify) {
            emit socket->changed();
            if (socket)
                emit socket->closed(code, reason);
        }
    }
}
void EchoPluginWebSocketClient::abort(const QString &id)
{
    closed(id, 1006, {}, false);
    if (m_started && !m_stopping)
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id] { w->finish(id); });
}
void EchoPluginWebSocketClient::close(const QString &id, int code, const QString &reason)
{
    auto socket = m_connections.value(id);
    if (!socket || socket->readyState() != 1)
        return;
    if (!closeCodeValid(code) || reason.toUtf8().size() > 123) {
        emit socket->error("Invalid WebSocket close code or reason (maximum 123 UTF-8 bytes)");
        return;
    }
    socket->m_state = 2;
    emit socket->changed();
    QMetaObject::invokeMethod(m_worker,
                              [w = m_worker, id, code, reason] { w->close(id, code, reason); });
}
void EchoPluginWebSocketClient::retire()
{
    const auto pending = m_pending;
    for (auto op : pending)
        if (op)
            op->cancel();
    for (const auto &id : m_connections.keys())
        closed(id, 1006, {}, false);
    if (m_started && !m_stopping)
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->closeAll(); });
}
void EchoPluginWebSocketClient::shutdown()
{
    if (m_stopping)
        return;
    retire();
    m_stopping = true;
    if (m_started)
        QMetaObject::invokeMethod(m_worker, [w = m_worker] {
            w->closeAll();
            w->thread()->quit();
        });
    else {
        delete m_worker;
        m_worker = nullptr;
        m_drained = true;
        emit drained();
    }
}
EchoPluginWebSocketConnection::EchoPluginWebSocketConnection(EchoPluginWebSocketClient &owner,
                                                             QString id, QString url,
                                                             QString protocol)
    : QObject(&owner), m_owner(&owner), m_id(std::move(id)), m_url(std::move(url)),
      m_protocol(std::move(protocol))
{
}
EchoPluginOperation *EchoPluginWebSocketConnection::sendText(const QString &text)
{
    // Avoid an unbounded UTF-8 temporary before dispatch validation.
    if (!m_owner)
        return nullptr;
    if (text.size() > messageLimit)
        return m_owner->m_scope.rejected("InvalidArgument", "WebSocket text exceeds 1 MiB");
    return m_owner->send(m_id, "text", text.toUtf8());
}
EchoPluginOperation *EchoPluginWebSocketConnection::sendBytes(const QByteArray &bytes)
{
    return m_owner ? m_owner->send(m_id, "binary", bytes) : nullptr;
}
EchoPluginOperation *EchoPluginWebSocketConnection::ping(const QByteArray &bytes)
{
    return m_owner ? m_owner->send(m_id, "ping", bytes) : nullptr;
}
void EchoPluginWebSocketConnection::close(int code, const QString &reason)
{
    if (m_owner)
        m_owner->close(m_id, code, reason);
}
} // namespace EchoMusic
