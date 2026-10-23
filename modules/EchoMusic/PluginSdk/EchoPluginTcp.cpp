#include "EchoPluginTcp.h"
#include "EchoPluginContext.h"
#include "AccountService.h"
#include "ApiClient.h"
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QTimer>
#include <QUuid>
#include <cmath>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <mstcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif
namespace EchoMusic
{
namespace
{
bool integer(const QVariantMap &o, const QString &k, int fallback, int lo, int hi, int &v)
{
    bool ok;
    const double n = o.value(k, fallback).toDouble(&ok);
    if (!ok || !std::isfinite(n) || n < lo || n > hi || std::floor(n) != n ||
        o.value(k).metaType().id() == QMetaType::Bool)
        return false;
    v = int(n);
    return true;
}
bool halfClose(qintptr fd)
{
#ifdef Q_OS_WIN
    return ::shutdown(SOCKET(fd), SD_SEND) == 0;
#else
    return ::shutdown(int(fd), SHUT_WR) == 0;
#endif
}
void keepAliveDelay(qintptr fd, int ms)
{
    if (!ms)
        return;
#ifdef Q_OS_WIN
    tcp_keepalive values{1, ULONG(ms), 1000};
    DWORD bytes = 0;
    WSAIoctl(SOCKET(fd), SIO_KEEPALIVE_VALS, &values, sizeof(values), nullptr, 0, &bytes, nullptr,
             nullptr);
#else
    const int seconds = qMax(1, int((qint64(ms) + 999) / 1000));
#ifdef Q_OS_MACOS
    setsockopt(int(fd), IPPROTO_TCP, TCP_KEEPALIVE, &seconds, sizeof(seconds));
#else
    setsockopt(int(fd), IPPROTO_TCP, TCP_KEEPIDLE, &seconds, sizeof(seconds));
#endif
#endif
}
} // namespace
class PluginTcpWorker final : public QObject
{
public:
    struct Write
    {
        QString op;
        QByteArray bytes;
        bool end = false, submitted = false;
    };
    struct Connection
    {
        QTcpSocket *socket;
        QTimer *timer;
        QString opening, reading;
        QList<Write> writes;
        int timeout;
        bool eof = false, ended = false;
    };
    explicit PluginTcpWorker(EchoPluginTcp &owner) : owner(owner) {}
    void result(const QString &op, QVariant value = {}, QString code = {}, QString message = {})
    {
        if (op.isEmpty())
            return;
        QMetaObject::invokeMethod(&owner, [thisOwner = &owner, op, value, code, message] {
            thisOwner->result(op, value, code, message);
        });
    }
    void close(const QString &id, QString code = "ConnectionClosed",
               QString message = "TCP connection closed")
    {
        if (!connections.contains(id))
            return;
        auto c = connections.take(id);
        result(c.opening, {}, code, message);
        result(c.reading, {}, code, message);
        for (const auto &w : c.writes)
            result(w.op, {}, code, message);
        c.socket->disconnect(this);
        c.socket->abort();
        c.socket->deleteLater();
        QMetaObject::invokeMethod(&owner, [target = &owner, id] { target->closed(id); });
    }
    void open(const QString &id, const QString &op, const QVariantMap &options)
    {
        if (connections.size() >= 16) {
            result(op, {}, "QuotaExceeded", "At most 16 TCP connections per scope");
            return;
        }
        auto *s = new QTcpSocket(this);
        s->setProxy(QNetworkProxy::NoProxy);
        s->setReadBufferSize(128 * 1024);
        auto *timer = new QTimer(s);
        timer->setSingleShot(true);
        connections.insert(id, {s, timer, op, {}, {}, options.value("writeTimeoutMs").toInt()});
        QObject::connect(timer, &QTimer::timeout, this,
                         [this, id] { close(id, "Timeout", "TCP operation timed out"); });
        QObject::connect(s, &QTcpSocket::connected, this, [this, id, options] {
            if (!connections.contains(id))
                return;
            auto &c = connections[id];
            c.timer->stop();
            c.socket->setSocketOption(QAbstractSocket::LowDelayOption,
                                      options.value("noDelay", true));
            c.socket->setSocketOption(QAbstractSocket::KeepAliveOption,
                                      options.value("keepAlive", false));
            if (options.value("keepAlive").toBool())
                keepAliveDelay(c.socket->socketDescriptor(),
                               options.value("keepAliveInitialDelayMs").toInt());
            const auto op = std::exchange(c.opening, {});
            result(op, id);
        });
        QObject::connect(s, &QTcpSocket::readyRead, this, [this, id] { readReady(id); });
        QObject::connect(s, &QTcpSocket::bytesWritten, this, [this, id] { pump(id); });
        QObject::connect(s, &QTcpSocket::disconnected, this, [this, id] {
            if (!connections.contains(id))
                return;
            auto &c = connections[id];
            c.eof = true;
            if (!c.opening.isEmpty() || !c.writes.isEmpty()) {
                close(id);
                return;
            }
            readReady(id);
        });
        QObject::connect(s, &QTcpSocket::errorOccurred, this, [this, id, s](auto error) {
            if (error != QAbstractSocket::RemoteHostClosedError)
                close(id, "NetworkError", s->errorString());
        });
        timer->start(options.value("connectTimeoutMs").toInt());
        s->connectToHost(options.value("host").toString(), quint16(options.value("port").toInt()));
    }
    void readReady(const QString &id)
    {
        if (!connections.contains(id))
            return;
        auto &c = connections[id];
        if (c.reading.isEmpty())
            return;
        if (c.socket->bytesAvailable()) {
            auto op = std::exchange(c.reading, {});
            result(op, c.socket->read(64 * 1024));
        } else if (c.eof) {
            auto op = std::exchange(c.reading, {});
            result(op, QVariant::fromValue(nullptr));
        }
    }
    void pump(const QString &id)
    {
        if (!connections.contains(id))
            return;
        auto &c = connections[id];
        while (!c.writes.isEmpty()) {
            auto &w = c.writes.first();
            if (!w.submitted) {
                w.submitted = true;
                c.timer->start(c.timeout);
                if (!w.end && c.socket->write(w.bytes) != w.bytes.size()) {
                    close(id, "NetworkError", "TCP write failed");
                    return;
                }
            }
            if (c.socket->bytesToWrite())
                return;
            if (w.end && !halfClose(c.socket->socketDescriptor())) {
                close(id, "NetworkError", "TCP half-close failed");
                return;
            }
            auto op = w.op;
            c.writes.removeFirst();
            c.timer->stop();
            result(op);
        }
    }
    void command(const QString &id, const QString &op, const QString &action,
                 const QByteArray &bytes)
    {
        if (!connections.contains(id)) {
            result(op, {}, "ConnectionClosed", "TCP connection is closed");
            return;
        }
        auto &c = connections[id];
        if (action == "read") {
            if (!c.reading.isEmpty()) {
                result(op, {}, "Busy", "Only one pending TCP read is allowed");
                return;
            }
            c.reading = op;
            readReady(id);
            return;
        }
        if (c.ended || c.eof) {
            result(op, {}, "ConnectionClosed", "TCP writing has ended");
            return;
        }
        qint64 queued = bytes.size();
        for (const auto &w : c.writes)
            queued += w.bytes.size();
        if (c.writes.size() >= 16 || queued > 4 * 1024 * 1024) {
            result(op, {}, "Backpressure", "TCP write queue is full");
            return;
        }
        if (action == "end")
            c.ended = true;
        c.writes.append({op, bytes, action == "end", false});
        pump(id);
    }
    void closeAll()
    {
        for (const auto &id : connections.keys())
            close(id);
    }
    EchoPluginTcp &owner;
    QHash<QString, Connection> connections;
};
EchoPluginTcp::EchoPluginTcp(EchoPluginContext &s)
    : QObject(&s), m_scope(s), m_worker(new PluginTcpWorker(*this))
{
    m_thread.setObjectName("PluginTCP-" + s.installationId());
    m_worker->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    QObject::connect(&m_thread, &QThread::finished, this, [this] {
        m_drained = true;
        emit drained();
    });
    if (s.host().permissions)
        QObject::connect(s.host().permissions, &PluginPermissionStore::revoked, this,
                         [this](const QString &id, const QString &cap) {
                             if (id == m_scope.installationId() && cap == "tcp")
                                 closeAll();
                         });
    QObject::connect(&s.host().account, &AccountService::sessionAdopted, this,
                     &EchoPluginTcp::closeAll);
    QObject::connect(&s.host().api, &ApiClient::sessionChanged, this, &EchoPluginTcp::closeAll);
}
EchoPluginTcp::~EchoPluginTcp()
{
    shutdown();
    m_thread.wait();
}
void EchoPluginTcp::start()
{
    if (!m_started) {
        m_started = true;
        m_thread.start();
    }
}
EchoPluginOperation *EchoPluginTcp::connect(const QVariantMap &input)
{
    auto options = input;
    int port, ct, wt, keep;
    const auto host = options.value("host").toString();
    for (auto i = options.begin(); i != options.end(); ++i)
        if (!QStringList{"host", "port", "connectTimeoutMs", "writeTimeoutMs", "noDelay",
                         "keepAlive", "keepAliveInitialDelayMs"}
                 .contains(i.key()))
            return m_scope.rejected("InvalidArgument", "Unknown TCP option");
    if (m_stopping || host.isEmpty() || host.size() > 253 || host.contains(QChar::Null) ||
        !integer(options, "port", 0, 1, 65535, port) ||
        !integer(options, "connectTimeoutMs", 5000, 1, 120000, ct) ||
        !integer(options, "writeTimeoutMs", 10000, 1, 120000, wt) ||
        !integer(options, "keepAliveInitialDelayMs", 30000, 0, 2147483647, keep))
        return m_scope.rejected("InvalidArgument", "Invalid TCP options");
    for (const auto &k : {"noDelay", "keepAlive"})
        if (options.contains(k) && options.value(k).metaType().id() != QMetaType::Bool)
            return m_scope.rejected("InvalidArgument", "TCP flags must be boolean");
    options["connectTimeoutMs"] = ct;
    options["writeTimeoutMs"] = wt;
    options["keepAliveInitialDelayMs"] = keep;
    return m_scope.permissions()->authorize(
        "tcp", tr("连接 %1:%2").arg(host).arg(port), [this, options](auto *op) {
            if (m_stopping) {
                op->cancel();
                return;
            }
            start();
            const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_pending.insert(op->id(), op);
            QObject::connect(op, &EchoPluginOperation::completed, this, [this, id, op] {
                m_pending.remove(op->id());
                if (op->state() == EchoPluginOperation::Canceled)
                    close(id);
            });
            QMetaObject::invokeMethod(m_worker, [w = m_worker, id, oid = op->id(), options] {
                w->open(id, oid, options);
            });
        });
}
void EchoPluginTcp::result(const QString &id, QVariant value, const QString &code,
                           const QString &message)
{
    auto op = m_pending.take(id);
    if (!op || op->finished())
        return;
    if (!code.isEmpty()) {
        op->fail(code, message);
        return;
    }
    if (value.metaType().id() == QMetaType::QString) {
        auto *c = new EchoPluginTcpConnection(*this, value.toString());
        m_connections.insert(c->id(), c);
        m_scope.engine().newQObject(c);
        value = QVariant::fromValue(c);
    }
    op->succeed(value);
}
EchoPluginOperation *EchoPluginTcp::operation(const QString &id, const QString &action,
                                              const QByteArray &data)
{
    if (!m_scope.permissions()->granted("tcp"))
        return m_scope.rejected("PermissionDenied", "TCP permission is unavailable");
    if (!m_connections.contains(id) || !m_connections.value(id) ||
        m_connections.value(id)->closed())
        return m_scope.rejected("ConnectionClosed", "TCP connection is closed");
    if (data.size() > 1024 * 1024)
        return m_scope.rejected("InvalidArgument", "TCP write exceeds 1 MiB");
    if (m_pending.size() >= 64 || m_queuedBytes + data.size() > 8 * 1024 * 1024)
        return m_scope.rejected("Backpressure", "TCP dispatch queue is full");
    auto *op = m_scope.operation();
    if (op->finished())
        return op;
    op->start();
    m_queuedBytes += data.size();
    m_payloads[op->id()] = data.size();
    m_pending.insert(op->id(), op);
    QObject::connect(op, &EchoPluginOperation::completed, this, [this, id, op] {
        m_pending.remove(op->id());
        m_queuedBytes -= m_payloads.take(op->id());
        if (op->state() == EchoPluginOperation::Canceled)
            close(id);
    });
    QMetaObject::invokeMethod(m_worker, [w = m_worker, id, oid = op->id(), action, data] {
        w->command(id, oid, action, data);
    });
    return op;
}
void EchoPluginTcp::close(const QString &id)
{
    if (m_started && !m_stopping)
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id] { w->close(id); });
}
void EchoPluginTcp::closed(const QString &id)
{
    if (auto c = m_connections.take(id)) {
        c->m_closed = true;
        emit c->changed();
        c->setParent(nullptr);
        QQmlEngine::setObjectOwnership(c, QQmlEngine::JavaScriptOwnership);
    }
}
void EchoPluginTcp::closeAll()
{
    const auto ops = m_pending;
    for (auto op : ops)
        if (op)
            op->cancel();
    for (const auto &id : m_connections.keys())
        closed(id);
    if (m_started && !m_stopping)
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->closeAll(); });
}
void EchoPluginTcp::shutdown()
{
    if (m_stopping)
        return;
    closeAll();
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
EchoPluginTcpConnection::EchoPluginTcpConnection(EchoPluginTcp &o, QString id)
    : QObject(&o), m_owner(&o), m_id(std::move(id))
{
}
EchoPluginOperation *EchoPluginTcpConnection::read()
{
    return m_owner ? m_owner->operation(m_id, "read") : nullptr;
}
EchoPluginOperation *EchoPluginTcpConnection::write(const QByteArray &data)
{
    return m_owner ? m_owner->operation(m_id, "write", data) : nullptr;
}
EchoPluginOperation *EchoPluginTcpConnection::end()
{
    return m_owner ? m_owner->operation(m_id, "end") : nullptr;
}
void EchoPluginTcpConnection::close()
{
    if (!m_closed && m_owner)
        m_owner->close(m_id);
}
} // namespace EchoMusic
