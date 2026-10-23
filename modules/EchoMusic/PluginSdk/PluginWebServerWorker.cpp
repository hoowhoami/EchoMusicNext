#include "PluginWebServerWorker_p.h"
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QUuid>
#include <QNetworkProxy>
#include <QNetworkRequest>
namespace EchoMusic
{
namespace
{
constexpr qint64 bodyLimit = 8 * 1024 * 1024;
const QRegularExpression token("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$");
QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QVariantMap query(const QUrl &u)
{
    QVariantMap out;
    for (const auto &i : QUrlQuery(u).queryItems(QUrl::FullyDecoded)) {
        if (!out.contains(i.first))
            out[i.first] = i.second;
        else {
            auto list = out[i.first].metaType().id() == QMetaType::QStringList
                            ? out[i.first].toStringList()
                            : QStringList{out[i.first].toString()};
            list.append(i.second);
            out[i.first] = list;
        }
    }
    return out;
}
} // namespace
void PluginWebServerWorker::result(const QString &id, QVariant value, QString code, QString message)
{
    QMetaObject::invokeMethod(&owner, [target = &owner, id, value, code, message] {
        target->deliver(id, value, code, message);
    });
}
void PluginWebServerWorker::notify(const QString &id, const QString &kind, const QVariantMap &data)
{
    const bool message = kind == "message";
    const qint64 bytes =
        message ? (data.value("binary").toBool() ? data.value("data").toByteArray().size()
                                                 : data.value("data").toString().size() * 2)
                : 0;
    if (message && (deliveryCount.value(id) >= 256 ||
                    deliveryBytes.value(id) + bytes > options.value("maxBufferedBytes").toInt())) {
        closeSocket(id, 1009, "Plugin message queue is full");
        return;
    }
    if (message) {
        deliveryBytes[id] += bytes;
        ++deliveryCount[id];
    }
    const QPointer<PluginWebServerWorker> worker(this);
    QMetaObject::invokeMethod(&owner, [target = &owner, worker, id, kind, data, bytes, message] {
        target->receive(id, kind, data);
        if (message && worker && !target->m_stopping)
            QMetaObject::invokeMethod(worker, [worker, id, bytes] {
                if (!worker)
                    return;
                worker->deliveryBytes[id] -= bytes;
                if (--worker->deliveryCount[id] <= 0) {
                    worker->deliveryCount.remove(id);
                    worker->deliveryBytes.remove(id);
                }
            });
    });
}
void PluginWebServerWorker::status()
{
    QVariantMap s{{"running", server && server->isListening()},
                  {"host", "127.0.0.1"},
                  {"port", server ? int(server->serverPort()) : 0},
                  {"origin", origin},
                  {"url", origin + options.value("path", "/").toString()},
                  {"startedAt", started},
                  {"connections", sockets.size()},
                  {"pendingRequests", requests.size()}};
    QMetaObject::invokeMethod(&owner, [target = &owner, s] {
        target->m_status = s;
        emit target->changed();
    });
}
void PluginWebServerWorker::listen(const QString &operation, const QVariantMap &input)
{
    close();
    options = input;
    server = new QTcpServer(this);
    server->setProxy(QNetworkProxy::NoProxy);
    server->setMaxPendingConnections(80);
    ws = new QWebSocketServer("EchoMusicNext plugin", QWebSocketServer::NonSecureMode, this);
    ws->setHandshakeTimeout(10000);
    ws->setMaxPendingConnections(64);
    connect(server, &QTcpServer::newConnection, this, &PluginWebServerWorker::incoming);
    connect(ws, &QWebSocketServer::newConnection, this, [this] {
        auto *s = ws->nextPendingConnection();
        if (!s)
            return;
        s->setParent(this);
        if (sockets.size() >= options.value("maxConnections").toInt()) {
            s->close(QWebSocketProtocol::CloseCodePolicyViolated, "Connection limit reached");
            s->abort();
            s->deleteLater();
            return;
        }
        QString id;
        for (auto it = requests.begin(); it != requests.end(); ++it) {
            const auto transport = it.value();
            if (transport && transport->peerAddress() == s->peerAddress() &&
                transport->peerPort() == s->peerPort()) {
                id = it.key();
                requests.erase(it);
                break;
            }
        }
        if (id.isEmpty() || s->state() != QAbstractSocket::ConnectedState) {
            s->abort();
            s->deleteLater();
            return;
        }
        sockets[id] = s;
        s->setMaxAllowedIncomingFrameSize(quint64(options.value("maxMessageBytes").toInt()));
        s->setMaxAllowedIncomingMessageSize(quint64(options.value("maxMessageBytes").toInt()));
        s->setReadBufferSize(quint64(options.value("maxMessageBytes").toInt()) + 1024);
        connect(s, &QWebSocket::textMessageReceived, this, [this, id](const QString &text) {
            notify(id, "message", {{"data", text}, {"binary", false}});
        });
        connect(s, &QWebSocket::binaryMessageReceived, this, [this, id](const QByteArray &bytes) {
            notify(id, "message", {{"data", bytes}, {"binary", true}});
        });
        connect(s, &QWebSocket::pong, this, [this, id](quint64 elapsed, const QByteArray &bytes) {
            notify(id, "pong", {{"elapsed", double(elapsed)}, {"bytes", bytes}});
        });
        connect(s, &QWebSocket::errorOccurred, this,
                [this, id, s] { notify(id, "error", {{"message", s->errorString()}}); });
        connect(s, &QWebSocket::disconnected, this, [this, id, s] {
            sockets.remove(id);
            notify(id, "closed", {{"code", int(s->closeCode())}, {"reason", s->closeReason()}});
            s->deleteLater();
            status();
        });
        const auto u = s->requestUrl();
        QVariantMap headers;
        const auto request = s->request();
        for (const auto &name : request.rawHeaderList())
            headers[QString::fromLatin1(name.toLower())] =
                QString::fromLatin1(request.rawHeader(name));
        notify(id, "open",
               {{"connectionId", id},
                {"url", u.toString()},
                {"path", u.path()},
                {"query", query(u)},
                {"headers", headers},
                {"protocol", s->subprotocol()},
                {"remoteAddress", s->peerAddress().toString()}});
        status();
    });
    if (!server->listen(QHostAddress::LocalHost, quint16(options.value("port").toInt()))) {
        const auto error = server->errorString();
        close();
        result(operation, {}, "ListenFailed", error);
        return;
    }
    origin = "http://127.0.0.1:" + QString::number(server->serverPort());
    started = QDateTime::currentMSecsSinceEpoch();
    status();
    result(operation, QVariantMap{{"host", "127.0.0.1"},
                                  {"port", int(server->serverPort())},
                                  {"origin", origin},
                                  {"url", origin + options.value("path", "/").toString()},
                                  {"startedAt", started}});
}
void PluginWebServerWorker::forget(QTcpSocket *s)
{
    auto it = clients.find(s);
    if (it == clients.end())
        return;
    const auto id = it->request.value("requestId").toString();
    clients.erase(it);
    requests.remove(id);
    notify(id, "cancel");
    status();
}
void PluginWebServerWorker::finish(QTcpSocket *s, int code, QVariantMap headers, QByteArray body)
{
    if (!s)
        return;
    qint64 buffered = body.size();
    for (const auto &socket : accepted)
        if (socket)
            buffered += socket->bytesToWrite();
    if (buffered > 32 * 1024 * 1024) {
        code = 503;
        body = "Server response queue is full";
        headers.clear();
    }
    if (body.size() > bodyLimit) {
        code = 500;
        body = "Response too large";
        headers.clear();
    }
    QByteArray output =
        "HTTP/1.1 " + QByteArray::number(code) + " " + (code < 400 ? "OK" : "Error") + "\r\n";
    for (auto i = headers.begin(); i != headers.end(); ++i) {
        if (!token.match(i.key()).hasMatch() ||
            QStringList{"connection", "content-length", "transfer-encoding"}.contains(
                i.key().toLower()))
            continue;
        const auto values = (i.value().metaType().id() == QMetaType::QStringList ||
                             i.value().metaType().id() == QMetaType::QVariantList)
                                ? i.value().toStringList()
                                : QStringList{i.value().toString()};
        for (const auto &value : values)
            if (!value.contains('\r') && !value.contains('\n'))
                output += i.key().toLatin1() + ": " + value.toLatin1() + "\r\n";
    }
    output +=
        "Connection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    const bool head = clients.value(s).request.value("method") == "HEAD";
    forget(s);
    s->write(output);
    if (!head)
        s->write(body);
    s->disconnectFromHost();
}
void PluginWebServerWorker::reject(QTcpSocket *s, int code)
{
    finish(s, code, {}, "Request rejected");
}
void PluginWebServerWorker::incoming()
{
    while (server && server->hasPendingConnections()) {
        auto *s = server->nextPendingConnection();
        if (!s)
            continue;
        s->setParent(this);
        s->setReadBufferSize(bodyLimit + 65536);
        accepted.removeIf([](const auto &socket) { return !socket; });
        if (accepted.size() >= 80 || clients.size() >= 16) {
            reject(s, 503);
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            continue;
        }
        accepted.append(s);
        auto *timer = new QTimer(s);
        timer->setSingleShot(true);
        timer->start(30000);
        clients.insert(s, {s, timer, {}, {}});
        connect(timer, &QTimer::timeout, this, [this, s] {
            if (clients.contains(s))
                reject(s, 504);
            else
                s->abort();
        });
        connect(s, &QTcpSocket::readyRead, this, [this, s] { parse(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            forget(s);
            s->deleteLater();
        });
        parse(s);
    }
}
void PluginWebServerWorker::parse(QTcpSocket *s)
{
    auto it = clients.find(s);
    if (it == clients.end() || it->waiting)
        return;
    auto &c = it.value();
    if (!c.headers) {
        const auto peek = s->peek(65537);
        const auto end = peek.indexOf("\r\n\r\n");
        if (end < 0) {
            if (peek.size() > 65536)
                reject(s, 431);
            return;
        }
        auto lines = peek.left(end).split('\n');
        const auto first = lines.takeFirst().trimmed().split(' ');
        if (first.size() != 3 || !token.match(QString::fromLatin1(first[0])).hasMatch() ||
            first[2] != "HTTP/1.1" || !first[1].startsWith('/') || first[1].size() > 8192) {
            reject(s, 400);
            return;
        }
        QVariantMap headers;
        for (auto line : lines) {
            if (line.endsWith('\r'))
                line.chop(1);
            int colon = line.indexOf(':');
            if (colon <= 0 || !token.match(QString::fromLatin1(line.left(colon))).hasMatch()) {
                reject(s, 400);
                return;
            }
            auto name = QString::fromLatin1(line.left(colon).toLower());
            auto value = QString::fromLatin1(line.mid(colon + 1).trimmed());
            if (headers.contains(name)) {
                if (QStringList{"content-length", "host", "transfer-encoding", "origin",
                                "sec-websocket-key", "sec-websocket-version"}
                        .contains(name)) {
                    reject(s, 400);
                    return;
                }
                if (QStringList{"connection", "upgrade", "sec-websocket-protocol"}.contains(name))
                    headers[name] = headers[name].toString() + ", " + value;
                else if (name == "cookie")
                    headers[name] = headers[name].toString() + "; " + value;
                else {
                    auto values = headers[name].metaType().id() == QMetaType::QStringList
                                      ? headers[name].toStringList()
                                      : QStringList{headers[name].toString()};
                    values.append(value);
                    headers[name] = values;
                }
            } else
                headers[name] = value;
        }
        const auto host = headers.value("host").toString();
        const auto port = QString::number(server->serverPort());
        if (host != "127.0.0.1:" + port && host != "localhost:" + port) {
            reject(s, 403);
            return;
        }
        const auto requestOrigin = headers.value("origin").toString();
        if (!options.value("allowCrossOrigin").toBool() && !requestOrigin.isEmpty() &&
            requestOrigin != origin && requestOrigin != "http://localhost:" + port) {
            reject(s, 403);
            return;
        }
        const auto url = QUrl::fromEncoded(first[1]);
        if (!url.isValid() || url.hasFragment()) {
            reject(s, 400);
            return;
        }
        int pendingHttp = 0, pendingUpgrades = 0;
        for (auto transport : requests) {
            if (!transport)
                continue;
            const auto client = clients.constFind(transport.data());
            if (client != clients.cend() &&
                client->request.value("headers").toMap().value("upgrade").toString().compare(
                    "websocket", Qt::CaseInsensitive) != 0)
                ++pendingHttp;
            else
                ++pendingUpgrades;
        }
        const auto id = uuid();
        c.request = {{"requestId", id},
                     {"method", QString::fromLatin1(first[0])},
                     {"url", origin + QString::fromLatin1(first[1])},
                     {"path", url.path()},
                     {"query", query(url)},
                     {"headers", headers},
                     {"remoteAddress", s->peerAddress().toString()}};
        if (headers.value("upgrade").toString().compare("websocket", Qt::CaseInsensitive) == 0) {
            if (url.path() != options.value("path", "/").toString() ||
                sockets.size() + pendingUpgrades >= options.value("maxConnections").toInt()) {
                reject(s, 503);
                return;
            }
            c.waiting = true;
            requests[id] = s;
            c.timer->start(10000);
            auto req = c.request;
            req["connectionId"] = id;
            const auto protocols =
                headers.value("sec-websocket-protocol").toString().split(',', Qt::SkipEmptyParts);
            QStringList clean;
            for (auto p : protocols)
                clean.append(p.trimmed());
            req["protocols"] = clean;
            QMetaObject::invokeMethod(
                &owner, [target = &owner, id, req] { target->dispatch("upgrade", id, req); });
            status();
            return;
        }
        if (pendingHttp >= 16) {
            reject(s, 503);
            return;
        }
        if (headers.contains("transfer-encoding")) {
            if (headers.value("transfer-encoding").toString().toLower() != "chunked" ||
                headers.contains("content-length")) {
                reject(s, 400);
                return;
            }
            c.chunked = true;
        } else {
            bool ok = true;
            c.length = headers.contains("content-length")
                           ? headers["content-length"].toString().toLongLong(&ok)
                           : 0;
            if (!ok || c.length < 0 || c.length > bodyLimit) {
                reject(s, 413);
                return;
            }
        }
        s->read(end + 4);
        c.headers = true;
        if (headers.value("expect").toString().compare("100-continue", Qt::CaseInsensitive) == 0)
            s->write("HTTP/1.1 100 Continue\r\n\r\n");
    }
    if (c.chunked) {
        while (true) {
            if (c.trailers) {
                const auto data = s->peek(65537);
                if (data.startsWith("\r\n")) {
                    s->read(2);
                    break;
                }
                const auto end = data.indexOf("\r\n\r\n");
                if (end < 0) {
                    if (data.size() > 65536)
                        reject(s, 431);
                    return;
                }
                s->read(end + 4);
                break;
            }
            if (c.chunk < 0) {
                const auto line = s->peek(1025);
                auto end = line.indexOf("\r\n");
                if (end < 0) {
                    if (line.size() > 1024)
                        reject(s, 400);
                    return;
                }
                bool ok;
                c.chunk = line.left(end).split(';').first().toLongLong(&ok, 16);
                if (!ok || c.chunk < 0 || c.chunk > bodyLimit - c.body.size()) {
                    reject(s, 413);
                    return;
                }
                s->read(end + 2);
                if (!c.chunk) {
                    c.trailers = true;
                    continue;
                }
            }
            if (s->bytesAvailable() < c.chunk + 2)
                return;
            auto bytes = s->read(c.chunk);
            if (s->read(2) != "\r\n") {
                reject(s, 400);
                return;
            }
            c.body += bytes;
            c.chunk = -1;
        }
    } else {
        if (s->bytesAvailable() < c.length)
            return;
        c.body = s->read(c.length);
    }
    c.waiting = true;
    c.timer->start(30000);
    c.request["body"] = c.body;
    const auto id = c.request.value("requestId").toString();
    requests[id] = s;
    auto req = c.request;
    QMetaObject::invokeMethod(&owner,
                              [target = &owner, id, req] { target->dispatch("request", id, req); });
    status();
}
void PluginWebServerWorker::respond(const QString &id, const QVariant &value, bool failed)
{
    auto s = requests.value(id);
    if (!s)
        return;
    if (failed) {
        finish(s, 500, {}, "Plugin handler failed");
        return;
    }
    auto response = value.toMap();
    int code = response.value("status", 200).toInt();
    if (code < 200 || code > 599) {
        finish(s, 500, {}, "Invalid response status");
        return;
    }
    auto body =
        response.contains("body")
            ? response.value("body")
            : (response.contains("status") || response.contains("headers") ? QVariant() : value);
    auto headers = response.value("headers").toMap();
    QByteArray bytes;
    if (!body.isValid() || body.isNull()) {
    } else if (body.metaType().id() == QMetaType::QByteArray)
        bytes = body.toByteArray();
    else if (body.metaType().id() == QMetaType::QString)
        bytes = body.toString().toUtf8();
    else {
        bytes = QJsonDocument::fromVariant(QVariantList{body}).toJson(QJsonDocument::Compact);
        bytes = bytes.mid(1, bytes.size() - 2);
        headers["Content-Type"] = "application/json; charset=utf-8";
    }
    if (code == 204 || code == 304)
        bytes.clear();
    finish(s, code, headers, bytes);
}
void PluginWebServerWorker::upgrade(const QString &id, const QVariant &value, bool failed)
{
    auto s = requests.value(id);
    if (!s)
        return;
    auto decision = value.toMap();
    if (failed || (value.metaType().id() == QMetaType::Bool && !value.toBool()) ||
        (decision.contains("accept") && !decision.value("accept").toBool())) {
        reject(s, 403);
        return;
    }
    const auto protocol = decision.value("protocol").toString();
    const auto headers = clients.value(s).request.value("headers").toMap();
    QStringList offered;
    for (auto p : headers.value("sec-websocket-protocol").toString().split(','))
        offered.append(p.trimmed());
    if (!protocol.isEmpty() && !offered.contains(protocol)) {
        reject(s, 400);
        return;
    }
    if (sockets.size() >= options.value("maxConnections").toInt()) {
        reject(s, 503);
        return;
    }
    clients.value(s).timer->stop();
    disconnect(s, nullptr, this, nullptr);
    clients.remove(s);
    // Keep the pre-upgrade identity until Qt accepts the same TCP endpoint.
    // Asynchronous authorization can therefore correlate the delivered socket.
    connect(s, &QTcpSocket::disconnected, this, [this, id] {
        requests.remove(id);
        status();
    });
    ws->setSupportedSubprotocols(protocol.isEmpty() ? QStringList{} : QStringList{protocol});
    ws->handleConnection(s);
    status();
}
void PluginWebServerWorker::send(const QString &id, const QString &op, const QString &kind,
                                 const QByteArray &bytes)
{
    auto s = sockets.value(id);
    if (!s || s->state() != QAbstractSocket::ConnectedState) {
        result(op, {}, "ConnectionClosed", "WebSocket is closed");
        return;
    }
    if (bytes.size() > options.value("maxMessageBytes").toInt() ||
        s->bytesToWrite() + bytes.size() > options.value("maxBufferedBytes").toInt()) {
        result(op, {}, "Backpressure", "WebSocket outbound buffer is full");
        return;
    }
    if (kind == "ping")
        s->ping(bytes);
    else if (kind == "text")
        s->sendTextMessage(QString::fromUtf8(bytes));
    else
        s->sendBinaryMessage(bytes);
    result(op);
}
void PluginWebServerWorker::closeSocket(const QString &id, int code, const QString &reason)
{
    if (auto s = sockets.value(id)) {
        s->close(QWebSocketProtocol::CloseCode(code), reason);
        QTimer::singleShot(1000, s, [s] { s->abort(); });
    }
}
void PluginWebServerWorker::close()
{
    if (server) {
        server->close();
        delete server;
        server = nullptr;
    }
    const auto live = clients.keys();
    for (auto *s : live) {
        forget(s);
        disconnect(s, nullptr, this, nullptr);
        s->abort();
        s->deleteLater();
    }
    clients.clear();
    requests.clear();
    const auto connected = sockets;
    for (auto s : connected)
        if (s) {
            disconnect(s, nullptr, this, nullptr);
            s->close(QWebSocketProtocol::CloseCodeGoingAway, "Plugin service stopped");
            s->abort();
            s->deleteLater();
        }
    sockets.clear();
    if (ws) {
        delete ws;
        ws = nullptr;
    }
    const auto transports = std::exchange(accepted, {});
    for (auto socket : transports)
        if (socket) {
            socket->abort();
            socket->deleteLater();
        }
    origin.clear();
    started = 0;
    status();
}
} // namespace EchoMusic
