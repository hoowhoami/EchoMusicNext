#include "PluginFileWorker_p.h"
#include <QTcpSocket>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUuid>
#include <QUrl>
namespace EchoMusic
{
namespace
{
class MediaClient final : public QObject
{
public:
    MediaClient(PluginFileWorker &owner, QTcpSocket *channel)
        : QObject(&owner), worker(owner), socket(channel)
    {
        worker.mediaClients.insert(this);
        socket->setParent(this);
        socket->setReadBufferSize(8193);
        deadline.setSingleShot(true);
        deadline.setInterval(30000);
        connect(&deadline, &QTimer::timeout, this, [this] { abort(); });
        connect(socket, &QTcpSocket::readyRead, this, [this] { request(); });
        connect(socket, &QTcpSocket::bytesWritten, this, [this] {
            deadline.start();
            pump();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this] {
            if (worker.media.contains(token))
                worker.media[token].clients.remove(this);
            file.close();
            deleteLater();
        });
        deadline.start();
        if (socket->bytesAvailable())
            request();
    }
    ~MediaClient() override
    {
        worker.mediaClients.remove(this);
    }
    void abort()
    {
        file.close();
        deadline.stop();
        socket->abort();
        deleteLater();
    }

private:
    void reject(int code, const QByteArray &reason, const QByteArray &extra = {})
    {
        socket->write("HTTP/1.1 " + QByteArray::number(code) + ' ' + reason +
                      "\r\nContent-Length: 0\r\nConnection: close\r\n" + extra + "\r\n");
        socket->disconnectFromHost();
    }
    void request()
    {
        if (started) {
            abort();
            return;
        }
        input += socket->readAll();
        if (input.size() > 8192) {
            reject(431, "Request Header Fields Too Large");
            return;
        }
        if (!input.contains("\r\n\r\n"))
            return;
        started = true;
        const auto lines = input.split('\n');
        const auto first = lines.first().trimmed().split(' ');
        if (first.size() != 3 || (first[0] != "GET" && first[0] != "HEAD")) {
            reject(405, "Method Not Allowed");
            return;
        }
        const auto path =
            QUrl::fromEncoded("http://localhost" + first[1]).path().split('/', Qt::SkipEmptyParts);
        if (path.size() != 2 || !worker.media.contains(path.first())) {
            reject(410, "Gone");
            return;
        }
        token = path.first();
        const auto lease = worker.media.value(token);
        try {
            const auto resolved = worker.resolve(lease.owner, lease.ref, false);
            file.setFileName(resolved.path);
            if (!QFileInfo(resolved.path).isFile() || !file.open(QIODevice::ReadOnly)) {
                reject(404, "Not Found");
                return;
            }
        } catch (const PluginFileFailure &) {
            reject(410, "Gone");
            return;
        }
        const auto size = file.size();
        qint64 begin = 0, end = size ? size - 1 : 0;
        bool partial = false;
        QByteArray range;
        for (const auto &raw : lines) {
            const auto line = raw.trimmed();
            if (line.left(6).toLower() == "range:")
                range = line.mid(6).trimmed();
        }
        if (!range.isEmpty()) {
            const auto match =
                QRegularExpression("^bytes=([0-9]*)-([0-9]*)$").match(QString::fromLatin1(range));
            bool ok1 = false, ok2 = false;
            if (match.hasMatch()) {
                if (match.captured(1).isEmpty()) {
                    const auto suffix = match.captured(2).toLongLong(&ok2);
                    begin = qMax<qint64>(0, size - suffix);
                    ok1 = suffix > 0;
                } else {
                    begin = match.captured(1).toLongLong(&ok1);
                    if (match.captured(2).isEmpty())
                        ok2 = true;
                    else
                        end = match.captured(2).toLongLong(&ok2);
                }
            }
            if (!match.hasMatch() || !ok1 || !ok2 || begin < 0 || begin >= size || end < begin) {
                reject(416, "Range Not Satisfiable",
                       "Content-Range: bytes */" + QByteArray::number(size) + "\r\n");
                return;
            }
            end = qMin(end, size - 1);
            partial = true;
        }
        remaining = size ? end - begin + 1 : 0;
        if (!file.seek(begin)) {
            reject(500, "Read Error");
            return;
        }
        QByteArray headers = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        headers += "Content-Length: " + QByteArray::number(remaining) +
                   "\r\nAccept-Ranges: bytes\r\nCache-Control: no-store\r\nConnection: close\r\n";
        headers += "Content-Type: " +
                   QMimeDatabase()
                       .mimeTypeForFile(file.fileName(), QMimeDatabase::MatchExtension)
                       .name()
                       .toUtf8() +
                   "\r\n";
        if (partial)
            headers += "Content-Range: bytes " + QByteArray::number(begin) + '-' +
                       QByteArray::number(end) + '/' + QByteArray::number(size) + "\r\n";
        socket->write(headers + "\r\n");
        worker.media[token].clients.insert(this);
        if (first[0] == "HEAD")
            remaining = 0;
        pump();
    }
    void pump()
    {
        if (!started || token.isEmpty() || socket->state() != QAbstractSocket::ConnectedState)
            return;
        if (!worker.media.contains(token) ||
            !worker.authorized(worker.media[token].owner, worker.media[token].ref)) {
            abort();
            return;
        }
        while (remaining > 0 && socket->bytesToWrite() < 256 * 1024) {
            const auto bytes = file.read(qMin<qint64>(128 * 1024, remaining));
            if (bytes.isEmpty()) {
                abort();
                return;
            }
            remaining -= bytes.size();
            if (socket->write(bytes) != bytes.size()) {
                abort();
                return;
            }
        }
        if (!remaining) {
            file.close();
            socket->disconnectFromHost();
        }
    }
    PluginFileWorker &worker;
    QTcpSocket *socket;
    QFile file;
    QTimer deadline;
    QByteArray input;
    QString token;
    qint64 remaining = 0;
    bool started = false;
};
} // namespace
QVariant PluginFileWorker::openMedia(const PluginFileOwner &owner, const QVariantMap &input)
{
    const auto ref = input.value("file").toMap();
    const auto resolved = resolve(owner, ref, false);
    if (!QFileInfo(resolved.path).isFile())
        throw PluginFileFailure{"NotFile", "Media must reference a file"};
    if (media.size() >= 128)
        throw PluginFileFailure{"QuotaExceeded", "Too many active media leases"};
    if (!server) {
        server = new QTcpServer(this);
        server->setMaxPendingConnections(32);
        connect(server, &QTcpServer::newConnection, this, [this] {
            while (server && server->hasPendingConnections()) {
                auto *socket = server->nextPendingConnection();
                if (mediaClients.size() >= 32) {
                    socket->abort();
                    socket->deleteLater();
                } else
                    new MediaClient(*this, socket);
            }
        });
        if (!server->listen(QHostAddress::LocalHost)) {
            delete std::exchange(server, nullptr);
            throw PluginFileFailure{"Unavailable", "Media listener could not be opened"};
        }
    }
    const auto id = QUuid::createUuid().toString(QUuid::Id128);

    QUrl url;
    url.setScheme("http");
    url.setHost("127.0.0.1");
    url.setPort(server->serverPort());
    url.setPath('/' + id + '/' + QFileInfo(resolved.path).fileName());
    media.insert(id, {owner, ref, url, {}});
    return QVariantMap{{"id", id}, {"url", url}};
}
void PluginFileWorker::releaseMedia(const QString &id)
{
    if (!media.contains(id))
        return;
    const auto entry = media.take(id);
    for (auto *client : entry.clients)
        static_cast<MediaClient *>(client)->abort();
    QMetaObject::invokeMethod(&service, [target = &service, id, url = entry.url] {
        emit target->leaseClosed(id);
        emit target->mediaRevoked(url);
    });
    if (media.isEmpty() && server) {
        server->close();
        server->deleteLater();
        server = nullptr;
        const auto clients = mediaClients;
        for (auto *client : clients)
            static_cast<MediaClient *>(client)->abort();
    }
}
void PluginFileWorker::retireMedia(
    const std::function<bool(const PluginFileOwner &, const QVariantMap &)> &predicate)
{
    const auto keys = media.keys();
    for (const auto &id : keys)
        if (predicate(media[id].owner, media[id].ref))
            releaseMedia(id);
}
} // namespace EchoMusic
