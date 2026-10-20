#include "NetworkTransport.h"
#include "ProxyResolver.h"
#include "NetworkMediaConnection.h"
#include "ProxyFailureCache.h"
#include "HttpProxyRequest.h"
#include "NetworkSocketBuffer.h"
#include <QFutureWatcher>
#include <QCoreApplication>
#include <QHostInfo>
#include <QNetworkProxy>
#include <QSet>
#include <QSslSocket>
#include <QTcpServer>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrentRun>
#include <utility>
#include <QUuid>
#include <QSemaphore>
#include <QScopeGuard>
#include <QLoggingCategory>
#include <QNetworkInterface>
#include <QFileInfo>

namespace EchoMusic
{
namespace
{
Q_LOGGING_CATEGORY(transportLog, "echo.network.transport")
constexpr qint64 bufferLimit = 128 * 1024;
constexpr qsizetype headerLimit = 64 * 1024;
QByteArray authority(const QUrl &url)
{
    auto host = url.host().contains(':') ? url.host().toLatin1() : QUrl::toAce(url.host());
    if (host.contains(':'))
        host = '[' + host + ']';
    return host + ':' + QByteArray::number(url.port(443));
}
class TransportConnection final : public QObject
{
public:
    TransportConnection(QTcpSocket *client, NetworkConfiguration configuration, QString password,
                        QThreadPool &resolvers, ProxyFailureCache &failures,
                        std::shared_ptr<QSemaphore> resolverSlots, QObject *parent)
        : QObject(parent), m_client(client), m_config(std::move(configuration)),
          m_password(std::move(password)), m_resolvers(resolvers), m_failures(failures),
          m_resolverSlots(std::move(resolverSlots))
    {
        client->setParent(this);
        client->setReadBufferSize(bufferLimit);
        connect(client, &QTcpSocket::readyRead, this, [this] { clientReady(); });
        connect(client, &QTcpSocket::bytesWritten, this, [this] { relayUpstream(); });
        connect(client, &QTcpSocket::disconnected, this, [this] {
            m_cancelled->store(true, std::memory_order_release);
            m_failed = true;
            m_deadline.stop();
            if (m_upstream)
                m_upstream->abort();
            deleteLater();
        });
        m_deadline.setSingleShot(true);
        m_deadline.setInterval(10000);
        connect(&m_deadline, &QTimer::timeout, this, [this] {
            if (m_attemptActive) {
                m_failures.failed(m_endpoint);
                tryNext();
            } else
                fail(504);
        });
        m_deadline.start();
    }
    ~TransportConnection() override
    {
        m_cancelled->store(true, std::memory_order_release);
        if (m_upstream)
            m_upstream->abort();
    }
    void close()
    {
        m_cancelled->store(true, std::memory_order_release);
        m_failed = true;
        m_deadline.stop();
        m_client->abort();
        deleteLater();
    }

private:
    void fail(int status = 502)
    {
        if (m_failed)
            return;
        m_deadline.stop();
        m_failed = true;
        if (m_upstream)
            m_upstream->abort();
        if (!m_relay)
            m_client->write(
                "HTTP/1.1 " + QByteArray::number(status) +
                " Proxy connection failed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        m_client->disconnectFromHost();
        if (m_client->state() == QAbstractSocket::UnconnectedState)
            deleteLater();
    }
    void clientReady()
    {
        if (m_relay) {
            relayClient();
            return;
        }
        if (m_resolving)
            return;
        m_header += m_client->readAll();
        const auto end = m_header.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_header.size() > headerLimit)
                fail(431);
            return;
        }
        if (end > headerLimit) {
            fail(431);
            return;
        }
        m_initialBody = m_header.mid(end + 4);
        m_header.truncate(end + 4);
        const auto firstLine = m_header.left(m_header.indexOf("\r\n")).split(' ');
        if (firstLine.size() != 3 || !firstLine[2].startsWith("HTTP/1.")) {
            fail(400);
            return;
        }
        m_connect = firstLine[0] == "CONNECT";
        m_target = QUrl::fromEncoded(m_connect ? "https://" + firstLine[1] + '/' : firstLine[1],
                                     QUrl::StrictMode);
        if (!m_target.isValid() || m_target.host().isEmpty() ||
            (!m_connect && m_target.scheme() != "http") || m_target.port(80) < 1 ||
            m_target.port(80) > 65535) {
            fail(400);
            return;
        }
        m_target.setUserInfo({});
        m_target.setFragment({});
        m_resolving = true;
        m_deadline.start(30000);
        if (m_config.mode == NetworkConfiguration::Mode::Direct ||
            m_config.mode == NetworkConfiguration::Mode::FixedServers) {
            // Pure URL/rule selection has no I/O. Retiring system/PAC work must
            // never queue a new DIRECT/manual connection behind stale lookups.
            m_candidates =
                m_failures.prioritize(ProxyResolver::resolve(m_config, m_target).endpoints);
            tryNext();
            return;
        }
        if (!m_resolverSlots->tryAcquire()) {
            fail(503);
            return;
        }
        auto *watcher = new QFutureWatcher<ProxyResolution>(this);
        connect(watcher, &QFutureWatcher<ProxyResolution>::finished, this, [this, watcher] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (m_failed)
                return;
            m_candidates = m_failures.prioritize(result.endpoints);
            tryNext();
        });
        watcher->setFuture(QtConcurrent::run(&m_resolvers, [config = m_config, url = m_target,
                                                            cancelled = m_cancelled,
                                                            resolverBudget = m_resolverSlots] {
            const auto release = qScopeGuard([&] { resolverBudget->release(); });
            return ProxyResolver::resolve(config, url, cancelled);
        }));
    }
    void tryNext()
    {
        if (m_failed)
            return;
        if (m_upstream) {
            m_upstream->disconnect(this);
            m_upstream->abort();
            m_upstream->deleteLater();
            m_upstream = nullptr;
        }
        if (m_candidate >= m_candidates.size()) {
            fail();
            return;
        }
        m_endpoint = m_candidates[m_candidate++];
        m_attemptActive = true;
        m_deadline.start(30000);
        m_handshake.clear();
        m_stage = 0;
        const bool direct = m_endpoint.kind == NetworkProxyEndpoint::Kind::Direct;
        auto *socket = new QSslSocket(this);
        m_upstream = socket;
        socket->setProxy(QNetworkProxy::NoProxy);
        socket->setReadBufferSize(bufferLimit);
        connect(socket, &QTcpSocket::readyRead, this, [this] { upstreamReady(); });
        connect(socket, &QTcpSocket::bytesWritten, this, [this] { relayClient(); });
        connect(socket, &QSslSocket::encryptedBytesWritten, this, [this] { relayClient(); });
        connect(socket, &QTcpSocket::disconnected, this, [this] {
            if (m_relay) {
                m_upstreamClosed = true;
                relayUpstream();
            }
        });
        connect(socket, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError error) {
                    if (m_relay && error == QAbstractSocket::RemoteHostClosedError)
                        return;
                    qCDebug(transportLog)
                        << "Upstream socket failed" << int(error) << m_upstream->errorString();
                    if (m_relay || m_sentRequest ||
                        error == QAbstractSocket::ProxyAuthenticationRequiredError ||
                        error == QAbstractSocket::SocketAccessError ||
                        error == QAbstractSocket::ProxyProtocolError)
                        fail();
                    else {
                        m_failures.failed(m_endpoint);
                        tryNext();
                    }
                });
        const auto connected = [this] { connectedUpstream(); };
        if (m_endpoint.kind == NetworkProxyEndpoint::Kind::Https) {
            connect(socket, &QSslSocket::sslErrors, this, [](const QList<QSslError> &errors) {
                for (const auto &error : errors)
                    qCWarning(transportLog) << "Proxy TLS validation failed" << error.errorString();
            });
            connect(socket, &QSslSocket::encrypted, this, connected);
            socket->connectToHostEncrypted(m_endpoint.address.host(),
                                           quint16(m_endpoint.address.port(443)));
        } else {
            connect(socket, &QTcpSocket::connected, this, connected);
            socket->connectToHost(
                direct ? m_target.host() : m_endpoint.address.host(),
                quint16(
                    direct ? m_target.port(m_connect ? 443 : 80)
                           : m_endpoint.address.port(
                                 m_endpoint.kind == NetworkProxyEndpoint::Kind::Http ? 80 : 1080)));
        }
    }
    void connectedUpstream()
    {
        using Kind = NetworkProxyEndpoint::Kind;
        if (m_endpoint.kind == Kind::Direct) {
            beginRelay();
        } else if (m_endpoint.kind == Kind::Http || m_endpoint.kind == Kind::Https) {
            // Only explicit configured credentials enter the auth adapter.
            // It adopts this route-selected socket; origin TLS stays opaque.
            if (!m_config.username.isEmpty() && !m_header.toLower().contains("\nupgrade:")) {
                m_attemptActive = false;
                m_deadline.stop();
                m_upstream->disconnect(this);
                auto *upstream = std::exchange(m_upstream, nullptr);
                new HttpProxyRequest(m_client, upstream, m_endpoint, m_target, m_header,
                                     std::exchange(m_initialBody, {}), m_config.username,
                                     m_password, this);
                return;
            }
            if (!m_connect) {
                beginRelay();
                return;
            }
            m_upstream->write("CONNECT " + authority(m_target) + " HTTP/1.1\r\nHost: " +
                              authority(m_target) + "\r\n" + proxyAuthorization() + "\r\n");
            m_stage = 10;
        } else if (m_endpoint.kind == Kind::Socks5) {
            m_upstream->write(m_config.username.isEmpty() && m_password.isEmpty()
                                  ? QByteArray::fromHex("050100")
                                  : QByteArray::fromHex("05020002"));
            m_stage = 20;
        } else {
            // SOCKS4 resolves at the client, unlike SOCKS5's remote hostname.
            QHostInfo::lookupHost(m_target.host(), this, [this](const QHostInfo &info) {
                if (m_failed || m_stage != 0)
                    return;
                for (const auto &ip : info.addresses()) {
                    if (ip.protocol() != QAbstractSocket::IPv4Protocol)
                        continue;
                    QByteArray request = QByteArray::fromHex("0401");
                    appendPort(request);
                    const auto number = ip.toIPv4Address();
                    for (int shift = 24; shift >= 0; shift -= 8)
                        request.append(char(number >> shift));
                    request += m_config.username.toUtf8() + '\0';
                    m_upstream->write(request);
                    m_stage = 30;
                    return;
                }
                tryNext();
            });
        }
    }
    QByteArray proxyAuthorization() const
    {
        if (m_config.username.isEmpty() && m_password.isEmpty())
            return {};
        return "Proxy-Authorization: Basic " +
               (m_config.username.toUtf8() + ':' + m_password.toUtf8()).toBase64() + "\r\n";
    }
    void appendPort(QByteArray &bytes) const
    {
        const auto port = m_target.port(m_connect ? 443 : 80);
        bytes.append(char(port >> 8));
        bytes.append(char(port));
    }
    void socks5Connect()
    {
        auto host = QUrl::toAce(m_target.host());
        QHostAddress address(m_target.host());
        QByteArray request = QByteArray::fromHex("050100");
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            request.append(char(1));
            const auto number = address.toIPv4Address();
            for (int shift = 24; shift >= 0; shift -= 8)
                request.append(char(number >> shift));
        } else if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            request.append(char(4));
            const auto number = address.toIPv6Address();
            request.append(reinterpret_cast<const char *>(number.c), 16);
        } else {
            if (host.size() > 255) {
                fail();
                return;
            }
            request.append(char(3));
            request.append(char(host.size()));
            request += host;
        }
        appendPort(request);
        m_stage = 22;
        m_upstream->write(request);
    }
    void upstreamReady()
    {
        if (m_failed)
            return;
        if (m_relay) {
            relayUpstream();
            return;
        }
        m_handshake += m_upstream->readAll();
        if (m_handshake.size() > headerLimit) {
            fail();
            return;
        }
        if (m_stage == 10) {
            const auto end = m_handshake.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const auto status = m_handshake.left(m_handshake.indexOf("\r\n")).split(' ');
            if (status.size() < 2 || status[1].toInt() != 200) {
                fail();
                return;
            }
            m_handshake.remove(0, end + 4);
            beginRelay();
        } else if (m_stage == 20 && m_handshake.size() >= 2) {
            const auto method = quint8(m_handshake[1]);
            if (quint8(m_handshake[0]) != 5 || (method != 0 && method != 2)) {
                fail();
                return;
            }
            m_handshake.remove(0, 2);
            if (method == 0) {
                socks5Connect();
            } else {
                auto user = m_config.username.toUtf8(), password = m_password.toUtf8();
                if (user.size() > 255 || password.size() > 255) {
                    fail();
                    return;
                }
                QByteArray auth(1, char(1));
                auth += char(user.size());
                auth += user;
                auth += char(password.size());
                auth += password;
                m_stage = 21;
                m_upstream->write(auth);
            }
        } else if (m_stage == 21 && m_handshake.size() >= 2) {
            if (quint8(m_handshake[0]) != 1 || quint8(m_handshake[1]) != 0) {
                fail();
                return;
            }
            m_handshake.remove(0, 2);
            socks5Connect();
        } else if (m_stage == 22 && m_handshake.size() >= 5) {
            const auto type = quint8(m_handshake[3]);
            const int length = type == 1   ? 10
                               : type == 4 ? 22
                               : type == 3 ? 7 + quint8(m_handshake[4])
                                           : 0;
            if (!length || quint8(m_handshake[0]) != 5 || quint8(m_handshake[1]) != 0) {
                fail();
                return;
            }
            if (m_handshake.size() < length)
                return;
            m_handshake.remove(0, length);
            beginRelay();
        } else if (m_stage == 30 && m_handshake.size() >= 8) {
            if (quint8(m_handshake[1]) != 90) {
                fail();
                return;
            }
            m_handshake.remove(0, 8);
            beginRelay();
        }
    }
    void beginRelay()
    {
        m_attemptActive = false;
        m_relay = true;
        m_deadline.stop();
        if (m_connect) {
            m_client->write("HTTP/1.1 200 Connection Established\r\n\r\n");
        } else {
            const bool httpProxy = m_endpoint.kind == NetworkProxyEndpoint::Kind::Http ||
                                   m_endpoint.kind == NetworkProxyEndpoint::Kind::Https;
            const auto lines = m_header.split('\n');
            const auto first = lines.first().trimmed().split(' ');
            auto path = m_target.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority |
                                           QUrl::RemoveFragment);
            if (path.isEmpty() || path.startsWith('?'))
                path.prepend('/');
            QByteArray header = first[0] + ' ' + (httpProxy ? m_target.toEncoded() : path) + ' ' +
                                first[2] + "\r\n";
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const auto line = lines[i].trimmed();
                const auto name = line.left(line.indexOf(':')).toLower();
                if (line.isEmpty() || name == "proxy-authorization" || name == "proxy-connection" ||
                    name == "connection")
                    continue;
                header += line + "\r\n";
            }
            header += "Connection: close\r\n";
            if (httpProxy)
                header += proxyAuthorization();
            m_sentRequest = true;
            m_upstream->write(header + "\r\n");
        }
        if (!m_initialBody.isEmpty())
            m_upstream->write(std::exchange(m_initialBody, {}));
        if (!m_handshake.isEmpty())
            m_client->write(std::exchange(m_handshake, {}));
        relayClient();
        relayUpstream();
    }
    void relayClient()
    {
        const auto pending = pendingSocketWrite(m_upstream);
        if (m_relay && !m_failed && m_client->isOpen() && m_upstream->isOpen() &&
            m_client->bytesAvailable() > 0 && pending < bufferLimit)
            m_upstream->write(m_client->read(bufferLimit - pending));
    }
    void relayUpstream()
    {
        // QSslSocket can retain decrypted bytes after the peer's EOF. Drain
        // those bytes before closing the client, without reading an empty socket.
        if (m_relay && !m_failed && m_client->isOpen() && m_upstream->bytesAvailable() > 0 &&
            m_client->bytesToWrite() < bufferLimit)
            m_client->write(m_upstream->read(bufferLimit - m_client->bytesToWrite()));
        if (m_upstreamClosed && !m_upstream->bytesAvailable())
            m_client->disconnectFromHost();
    }
    QTcpSocket *m_client;
    QSslSocket *m_upstream = nullptr;
    NetworkConfiguration m_config;
    QString m_password;
    QThreadPool &m_resolvers;
    ProxyFailureCache &m_failures;
    std::shared_ptr<QSemaphore> m_resolverSlots;
    ProxyResolveCancellation m_cancelled = std::make_shared<std::atomic_bool>(false);
    QTimer m_deadline;
    QByteArray m_header, m_initialBody, m_handshake;
    QUrl m_target;
    QList<NetworkProxyEndpoint> m_candidates;
    NetworkProxyEndpoint m_endpoint;
    qsizetype m_candidate = 0;
    int m_stage = 0;
    bool m_connect = false, m_resolving = false, m_relay = false, m_failed = false,
         m_sentRequest = false, m_upstreamClosed = false, m_attemptActive = false;
};
} // namespace
class NetworkTransportWorker final : public QObject
{
    Q_DECLARE_TR_FUNCTIONS(EchoMusic::NetworkTransportWorker)
public:
    explicit NetworkTransportWorker(std::function<void(QString, int)> rejected)
        : m_mediaNetwork(this), m_rejected(std::move(rejected))
    {
        m_resolvers.setMaxThreadCount(4);
        m_server.setProxy(QNetworkProxy::NoProxy);
        m_mediaServer.setProxy(QNetworkProxy::NoProxy);
        m_castServer.setProxy(QNetworkProxy::NoProxy);
        connect(&m_castServer, &QTcpServer::newConnection, this, [this] {
            while (m_castServer.hasPendingConnections()) {
                auto *socket = m_castServer.nextPendingConnection();
                if (m_castConnections.size() >= 8 || m_mediaConnections.size() >= 128) {
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                auto *connection = new NetworkMediaConnection(
                    socket, m_mediaNetwork,
                    [this](const QString &token) { return m_castSources.value(token); },
                    qRound(m_config.playerTimeoutSeconds * 1000), this,
                    [this, socket](int status) {
                        const auto connection =
                            static_cast<NetworkMediaConnection *>(socket->parent());
                        m_rejected(connection->token(), status);
                    },
                    true);
                m_mediaConnections.insert(connection);
                m_castConnections.insert(connection);
                connect(connection, &QObject::destroyed, this, [this, connection] {
                    m_mediaConnections.remove(connection);
                    m_castConnections.remove(connection);
                });
            }
        });
        connect(&m_mediaServer, &QTcpServer::newConnection, this, [this] {
            while (m_mediaServer.hasPendingConnections()) {
                auto *socket = m_mediaServer.nextPendingConnection();
                if (m_mediaConnections.size() >= 128) {
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                auto *connection = new NetworkMediaConnection(
                    socket, m_mediaNetwork,
                    [this](const QString &token) { return m_mediaSources.value(token); },
                    qRound(m_config.playerTimeoutSeconds * 1000), this);
                m_mediaConnections.insert(connection);
                connect(connection, &QObject::destroyed, this,
                        [this, connection] { m_mediaConnections.remove(connection); });
            }
        });
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                auto *socket = m_server.nextPendingConnection();
                if (m_connections.size() + m_waiting.size() >= 128) {
                    socket->abort();
                    socket->deleteLater();
                    continue;
                }
                if (!m_enabled) {
                    m_waiting.insert(socket);
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                    connect(socket, &QObject::destroyed, this,
                            [this, socket] { m_waiting.remove(socket); });
                } else
                    accept(socket);
            }
        });
    }
    quint16 start()
    {
        if (!m_server.listen(QHostAddress::LocalHost, 0) ||
            !m_mediaServer.listen(QHostAddress::LocalHost, 0))
            return 0;
        m_mediaNetwork.setProxy(
            QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", m_server.serverPort()));
        return m_server.serverPort();
    }
    ~NetworkTransportWorker() override
    {
        m_server.close();
        m_mediaServer.close();
        m_castServer.close();
        qDeleteAll(std::exchange(m_connections, {}));
        qDeleteAll(std::exchange(m_mediaConnections, {}));
        qDeleteAll(std::exchange(m_waiting, {}));
        m_resolvers.waitForDone();
    }
    quint16 mediaPort() const
    {
        return m_mediaServer.serverPort();
    }
    void registerMedia(const QString &token, const QUrl &source)
    {
        m_mediaSources.insert(token, source);
    }
    QPair<QUrl, QString> registerCastMedia(const QString &token, const QUrl &source,
                                           const QHostAddress &receiver)
    {
        if (source.isLocalFile() && !QFileInfo(source.toLocalFile()).isFile())
            return {{}, tr("投放音源文件不存在")};
        if (m_castSources.size() >= 2)
            return {{}, tr("投放媒体资源数量已达上限")};
        QHostAddress address =
            receiver.isLoopback() ? QHostAddress(QHostAddress::LocalHost) : QHostAddress();
        for (bool virtualInterfaces : {false, true}) {
            for (const auto &iface : QNetworkInterface::allInterfaces()) {
                if (!iface.flags().testFlag(QNetworkInterface::IsUp) ||
                    iface.flags().testFlag(QNetworkInterface::IsLoopBack) ||
                    (iface.type() == QNetworkInterface::Virtual) != virtualInterfaces)
                    continue;
                for (const auto &entry : iface.addressEntries()) {
                    if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                        continue;
                    if (!receiver.isNull() &&
                        receiver.isInSubnet(entry.ip(), entry.prefixLength())) {
                        address = entry.ip();
                        break;
                    }
                    if (address.isNull() &&
                        (entry.ip().isInSubnet(QHostAddress("10.0.0.0"), 8) ||
                         entry.ip().isInSubnet(QHostAddress("172.16.0.0"), 12) ||
                         entry.ip().isInSubnet(QHostAddress("192.168.0.0"), 16)))
                        address = entry.ip();
                }
            }
            if (!address.isNull())
                break;
        }
        if (address.isNull())
            return {{}, tr("没有可供投放设备访问的局域网地址")};
        if (m_castServer.isListening() && m_castServer.serverAddress() != address)
            return {{}, tr("投放网络接口正在使用，请先停止投放")};
        if (!m_castServer.isListening() && !m_castServer.listen(address, 0))
            return {{}, tr("无法启动投放媒体中转")};
        m_castSources.insert(token, source);
        QUrl resource;
        resource.setScheme("http");
        resource.setHost(address.toString());
        resource.setPort(m_castServer.serverPort());
        resource.setPath('/' + token);
        return {resource, {}};
    }
    void releaseMedia(const QString &token)
    {
        m_mediaSources.remove(token);
        m_castSources.remove(token);
        if (m_castSources.isEmpty())
            m_castServer.close();
        const auto connections = m_mediaConnections;
        for (auto *connection : connections)
            if (connection->token() == token)
                connection->close();
    }
    void apply(const NetworkConfiguration &configuration, const QString &password)
    {
        if (m_enabled && m_config == configuration && m_password == password)
            return;
        const bool changedRoute =
            m_config.mode != configuration.mode || m_config.pacScript != configuration.pacScript ||
            m_config.proxyRules != configuration.proxyRules ||
            m_config.username != configuration.username ||
            m_config.bypassRules != configuration.bypassRules || m_password != password;
        m_config = configuration;
        m_password = password;
        if (changedRoute)
            recover();
        m_enabled = true;
        const auto waiting = std::exchange(m_waiting, {});
        for (auto *socket : waiting) {
            if (socket->state() == QAbstractSocket::ConnectedState)
                accept(socket);
            else
                socket->deleteLater();
        }
    }
    void recover()
    {
        ProxyResolver::invalidate();
        m_failures.clear();
        QHostInfo::clearCache();
        const auto connections = m_connections;
        for (auto *connection : connections)
            connection->close();
    }
    void stop()
    {
        m_server.close();
        m_mediaServer.close();
        m_castServer.close();
        recover();
        const auto mediaConnections = m_mediaConnections;
        for (auto *connection : mediaConnections)
            connection->close();
        for (auto *socket : std::exchange(m_waiting, {})) {
            socket->abort();
            socket->deleteLater();
        }
    }

private:
    void accept(QTcpSocket *socket)
    {
        auto *connection = new TransportConnection(socket, m_config, m_password, m_resolvers,
                                                   m_failures, m_resolverSlots, this);
        m_connections.insert(connection);
        connect(connection, &QObject::destroyed, this,
                [this, connection] { m_connections.remove(connection); });
        // A client can send its complete header before the secure-store restore
        // releases the startup gate; readyRead is not re-emitted for old bytes.
        if (socket->bytesAvailable())
            QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection);
    }
    QTcpServer m_server;
    QTcpServer m_mediaServer;
    QTcpServer m_castServer;
    QNetworkAccessManager m_mediaNetwork;
    QHash<QString, QUrl> m_mediaSources;
    QHash<QString, QUrl> m_castSources;
    QSet<NetworkMediaConnection *> m_castConnections;
    std::function<void(QString, int)> m_rejected;
    QSet<NetworkMediaConnection *> m_mediaConnections;
    QThreadPool m_resolvers;
    ProxyFailureCache m_failures;
    std::shared_ptr<QSemaphore> m_resolverSlots = std::make_shared<QSemaphore>(128);
    QSet<TransportConnection *> m_connections;
    QSet<QTcpSocket *> m_waiting;
    NetworkConfiguration m_config;
    QString m_password;
    bool m_enabled = false;
};
NetworkTransport::NetworkTransport(QObject *parent) : QObject(parent), m_worker(nullptr)
{
    auto *initializer = new QObject;
    initializer->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, initializer, &QObject::deleteLater);
    m_thread.setObjectName("echo-network-transport");
    m_thread.start();
    QMetaObject::invokeMethod(
        initializer,
        [this] {
            m_worker = new NetworkTransportWorker([this](const QString &token, int status) {
                QMetaObject::invokeMethod(
                    this, [this, token, status] { emit mediaRequestRejected(token, status); },
                    Qt::QueuedConnection);
            });
            m_port = m_worker->start();
            m_mediaPort = m_worker->mediaPort();
        },
        Qt::BlockingQueuedConnection);
}
NetworkTransport::~NetworkTransport()
{
    QMetaObject::invokeMethod(m_worker, [this] {
        m_worker->stop();
        m_worker->deleteLater();
        m_thread.quit();
    });
    m_thread.wait();
}
bool NetworkTransport::isReady() const
{
    return m_port != 0;
}
QUrl NetworkTransport::endpoint() const
{
    QUrl url;
    url.setScheme("http");
    url.setHost("127.0.0.1");
    url.setPort(m_port);
    return url;
}
QUrl NetworkTransport::registerMedia(const QUrl &source)
{
    if (!isReady() || !source.isValid() || !QStringList{"http", "https"}.contains(source.scheme()))
        return {};
    const auto token = QUuid::createUuid().toString(QUuid::Id128);
    QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, token, source] { worker->registerMedia(token, source); });
    QUrl resource;
    resource.setScheme("http");
    resource.setHost("127.0.0.1");
    resource.setPort(m_mediaPort);
    resource.setPath('/' + token);
    return resource;
}
void NetworkTransport::registerCastMedia(const QUrl &source, const QHostAddress &receiver,
                                         std::function<void(QUrl, QString)> complete)
{
    if (!isReady() || !source.isValid() || !source.userInfo().isEmpty() ||
        (!source.isLocalFile() && !QStringList{"http", "https"}.contains(source.scheme()))) {
        complete({}, tr("投放音源地址无效"));
        return;
    }
    const auto token = QUuid::createUuid().toString(QUuid::Id128);
    QMetaObject::invokeMethod(
        m_worker, [this, source, receiver, token, complete = std::move(complete)] {
            const auto result = m_worker->registerCastMedia(token, source, receiver);
            QMetaObject::invokeMethod(
                this, [result, complete] { complete(result.first, result.second); },
                Qt::QueuedConnection);
        });
}
void NetworkTransport::releaseMedia(const QUrl &resource)
{
    const auto token = resource.path().mid(1);
    QMetaObject::invokeMethod(m_worker,
                              [worker = m_worker, token] { worker->releaseMedia(token); });
}
void NetworkTransport::apply(const NetworkConfiguration &configuration, const QString &password)
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, configuration, password] {
        worker->apply(configuration, password);
    });
}
void NetworkTransport::recoverAfterWake()
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->recover(); });
}
void NetworkTransport::afterPendingChanges(std::function<void()> complete)
{
    // This owner outlives its worker join. Queued GUI completion is removed if
    // the owner is destroyed; the GUI never waits for DNS, PAC or socket work.
    QMetaObject::invokeMethod(m_worker, [this, complete = std::move(complete)] {
        QMetaObject::invokeMethod(this, complete, Qt::QueuedConnection);
    });
}
} // namespace EchoMusic
