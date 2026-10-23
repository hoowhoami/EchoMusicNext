#pragma once
#include "EchoPluginWebServer.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTimer>
namespace EchoMusic
{
/// All transport state and HTTP parsing stay on one event-driven worker thread.
class PluginWebServerWorker final : public QObject
{
public:
    explicit PluginWebServerWorker(EchoPluginWebServer &owner) : owner(owner) {}
    void listen(const QString &operation, const QVariantMap &options);
    void close();
    void incoming();
    void parse(QTcpSocket *);
    void respond(const QString &, const QVariant &value, bool failed);
    void upgrade(const QString &, const QVariant &value, bool failed);
    void send(const QString &, const QString &operation, const QString &kind, const QByteArray &);
    void closeSocket(const QString &, int, const QString &);
    void result(const QString &, QVariant value = {}, QString code = {}, QString message = {});
    void notify(const QString &, const QString &, const QVariantMap & = {});
    void status();
    struct Client
    {
        QPointer<QTcpSocket> socket;
        QTimer *timer;
        QByteArray body;
        QVariantMap request;
        qint64 length = -1, chunk = -1;
        bool headers = false, chunked = false, waiting = false, trailers = false;
    };
    void reject(QTcpSocket *, int code);
    void finish(QTcpSocket *, int, QVariantMap headers, QByteArray body);
    void forget(QTcpSocket *);
    EchoPluginWebServer &owner;
    QTcpServer *server = nullptr;
    QWebSocketServer *ws = nullptr;
    QList<QPointer<QTcpSocket>> accepted;
    QHash<QTcpSocket *, Client> clients;
    QHash<QString, QPointer<QTcpSocket>> requests;
    QHash<QString, QPointer<QWebSocket>> sockets;
    QHash<QString, qint64> deliveryBytes;
    QHash<QString, int> deliveryCount;
    QVariantMap options;
    QString origin;
    qint64 started = 0;
};
} // namespace EchoMusic
