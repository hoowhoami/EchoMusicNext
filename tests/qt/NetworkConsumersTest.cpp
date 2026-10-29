#include "NetworkBootstrap.h"
#include "NetworkPreferences.h"
#include "TestProxyStore.h"
#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QNetworkProxy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;
// Exercise the actual QML image consumer, not just the proxy
// factory's return value. The private HTTP server is a transport fixture only;
// it does not pretend to be a sampled music-service response or captcha result.
class NetworkConsumersTest final : public QObject
{
    Q_OBJECT
private slots:
    void qmlImagesShareAppliedPolicy_data()
    {
        QTest::addColumn<bool>("authenticated");
        QTest::newRow("anonymous-proxy") << false;
        QTest::newRow("explicit-basic-proxy") << true;
    }
    void qmlImagesShareAppliedPolicy()
    {
        QFETCH(bool, authenticated);
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("network.ini"));
        ApiClient api(DeviceIdentity::temporary());
        TestProxyStore store;
        NetworkPreferences preferences(settings, api, nullptr, &store);
        QTcpServer first, second;
        first.setProxy(QNetworkProxy::NoProxy);
        second.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(first.listen(QHostAddress::LocalHost));
        QVERIFY(second.listen(QHostAddress::LocalHost));
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        QStringList requests;
        const auto serve = [&](QTcpServer &server, const QByteArray &label) {
            connect(&server, &QTcpServer::newConnection, &server, [&, label, listener = &server] {
                while (listener->hasPendingConnections()) {
                    auto *peer = listener->nextPendingConnection();
                    connect(peer, &QTcpSocket::readyRead, peer, [&, peer, label] {
                        auto request = peer->property("header").toByteArray() + peer->readAll();
                        peer->setProperty("header", request);
                        if (!request.contains("\r\n\r\n") || peer->property("answered").toBool())
                            return;
                        if (authenticated && !request.toLower().contains("proxy-authorization:")) {
                            peer->write(
                                "HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: "
                                "Basic realm=\"consumer\"\r\nContent-Length: "
                                "0\r\nProxy-Connection: keep-alive\r\n\r\n");
                            peer->setProperty("header", QByteArray());
                            return;
                        }
                        if (authenticated)
                            QVERIFY(request.toLower().contains(
                                "proxy-authorization: basic " +
                                QByteArray("proxy-user:proxy-secret").toBase64().toLower()));
                        peer->setProperty("answered", true);
                        const auto line = request.left(request.indexOf("\r\n"));
                        requests.append(QString::fromLatin1(label + ':' + line));
                        const bool picture = line.contains("/pixel.png");
                        const auto body = picture ? png
                                                  : QByteArray("<!doctype html><title>") + label +
                                                        "</title><body>" + label + "</body>";
                        peer->write("HTTP/1.1 200 OK\r\nContent-Type: " +
                                    (picture ? QByteArray("image/png")
                                             : QByteArray("text/html; charset=utf-8")) +
                                    "\r\nCache-Control: no-store\r\nContent-Length: " +
                                    QByteArray::number(body.size()) +
                                    "\r\nConnection: close\r\n\r\n" + body);
                        peer->disconnectFromHost();
                    });
                    connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
                }
            });
        };
        serve(first, "first-route");
        serve(second, "second-route");
        QSignalSpy applied(&preferences, &NetworkPreferences::applied);
        const auto selectProxy = [&](const QTcpServer &server) {
            preferences.apply(
                "fixed_servers", {}, QString("http://127.0.0.1:%1").arg(server.serverPort()),
                authenticated ? QString("proxy-user") : QString(),
                authenticated ? QString("proxy-secret") : QString(), false, "<local>", 0, 60);
        };
        selectProxy(first);
        QTRY_COMPARE(applied.size(), 1);
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        preferences.installApplicationTransport();
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.loadUrl(QUrl::fromLocalFile(QFINDTESTDATA("NetworkConsumersHarness.qml")));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QTRY_COMPARE(window->property("imageStatus").toInt(), 1);
        QVERIFY(
            requests.contains("first-route:GET http://image-source.invalid/pixel.png HTTP/1.1"));
        selectProxy(second);
        QTRY_COMPARE(applied.size(), 2);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "reloadConsumers"));
        QTRY_COMPARE(window->property("imageStatus").toInt(), 1);
        QVERIFY(requests.contains(
            "second-route:GET http://image-source.invalid/pixel.png?second=1 HTTP/1.1"));
        // Destroy the real image/view before its network policy and worker.
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};
int main(int argc, char **argv)
{
    initializeNetworkEnvironment();
    QGuiApplication app(argc, argv);
    NetworkConsumersTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "NetworkConsumersTest.moc"
