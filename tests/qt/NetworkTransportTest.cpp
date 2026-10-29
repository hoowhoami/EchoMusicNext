#include "NetworkBootstrap.h"
#include <QCoreApplication>
#include "NetworkTransport.h"
#include "TestProxyStore.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include "ProxyResolver.h"
#include "ProxyRouting.h"
#include "NetworkPreferences.h"
#include "SystemNetworkMonitor.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QSslServer>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtTest>
#ifdef Q_OS_MACOS
#include <SystemConfiguration/SystemConfiguration.h>
#endif

using namespace EchoMusic;
class NetworkTransportTest : public QObject
{
    Q_OBJECT
private slots:
    void pacDnsSharesEvaluationDeadlineAndHonorsCancellation()
    {
        ProxyPacDns dns;
        QCOMPARE(dns.dnsResolve("192.0.2.1"), QString("192.0.2.1"));
        QCOMPARE(dns.dnsResolveEx("2001:db8::1"), QString("2001:db8::1"));
        QVERIFY(dns.dnsResolve("::1").isEmpty());
        auto cancelled = std::make_shared<std::atomic_bool>(true);
        ProxyPacDns stopped(cancelled);
        QVERIFY(stopped.dnsResolve("192.0.2.1").isEmpty());
        ProxyPacDns expired({}, QDeadlineTimer(0));
        QVERIFY(expired.dnsResolve("localhost").isEmpty());
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = ProxyResolver::evaluatePac(
            "function FindProxyForURL(url,host){while(true) dnsResolve('localhost');}",
            QUrl("https://public.example"));
        QVERIFY(!result.error.isEmpty());
        QVERIFY2(elapsed.elapsed() < 3500, "PAC DNS calls must share the script deadline");
    }
    void systemNotificationsInvalidateOnlyAffectedRoutes()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("network.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        auto *monitor = preferences.findChild<SystemNetworkMonitor *>();
        QVERIFY(monitor);
        QTcpServer origin;
        origin.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(origin.listen(QHostAddress::LocalHost));
        QTcpSocket client;
        client.setProxy(QNetworkProxy::NoProxy);
        client.connectToHost("127.0.0.1", quint16(preferences.transportEndpoint().port()));
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        client.write("CONNECT 127.0.0.1:" + QByteArray::number(origin.serverPort()) +
                     " HTTP/1.1\r\nHost: localhost\r\n\r\n");
        QTRY_VERIFY2(origin.hasPendingConnections(),
                     qPrintable(QString("client=%1, policy=%2, response=%3")
                                    .arg(int(client.state()))
                                    .arg(preferences.errorMessage(),
                                         QString::fromLatin1(client.peek(1024)))));
        auto *upstream = origin.nextPendingConnection();
        QTRY_VERIFY(client.bytesAvailable());
        QVERIFY(client.readAll().startsWith("HTTP/1.1 200"));
        emit monitor->changed(true);
        upstream->write("unaffected");
        QTRY_VERIFY(client.bytesAvailable());
        QCOMPARE(client.readAll(), QByteArray("unaffected"));
        emit monitor->changed(false);
        QTRY_COMPARE(client.state(), QAbstractSocket::UnconnectedState);
        upstream->deleteLater();

        QSignalSpy applied(&preferences, &NetworkPreferences::applied);
        preferences.apply("system", {}, {}, {}, {}, false, "<local>", 0, 60);
        QTRY_COMPARE(applied.size(), 1);
        client.connectToHost("127.0.0.1", quint16(preferences.transportEndpoint().port()));
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        client.write("CONNECT 127.0.0.1:" + QByteArray::number(origin.serverPort()) +
                     " HTTP/1.1\r\nHost: localhost\r\n\r\n");
        QTRY_VERIFY(origin.hasPendingConnections());
        upstream = origin.nextPendingConnection();
        QTRY_VERIFY(client.bytesAvailable());
        client.readAll();
        emit monitor->changed(true);
        QTRY_COMPARE(client.state(), QAbstractSocket::UnconnectedState);
        upstream->deleteLater();
    }
#ifdef Q_OS_MACOS
    void macProxyNotificationIsNativeAndCoalesced()
    {
        SystemNetworkMonitor monitor;
        QSignalSpy changed(&monitor, &SystemNetworkMonitor::changed);
        auto *store = SCDynamicStoreCreate(nullptr, CFSTR("EchoMusicNext notification test"),
                                           nullptr, nullptr);
        QVERIFY(store);
        const auto release = qScopeGuard([&] { CFRelease(store); });
        // Notify only: no proxy value is written and system routing is unchanged.
        if (!SCDynamicStoreNotifyValue(store, CFSTR("State:/Network/Global/Proxies")))
            QSKIP("SystemConfiguration notification permission unavailable");
        SCDynamicStoreNotifyValue(store, CFSTR("State:/Network/Global/Proxies"));
        QElapsedTimer elapsed;
        elapsed.start();
        while (changed.isEmpty() && elapsed.elapsed() < 2000) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, .01, true);
            QCoreApplication::processEvents();
        }
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().first().toBool(), true);
        QTest::qWait(250);
        QCOMPARE(changed.size(), 1);
    }
#endif
    void localSqlitePasswordSurvivesRestartAndCanBeCleared()
    {
        QTemporaryDir dir;
        const auto ini = dir.filePath("network.ini");
        const auto databasePath = dir.filePath("network.sqlite");
        {
            EchoMusic::SettingsStore settings(ini);
            ApiClient api(DeviceIdentity::temporary());
            SqliteCredentialStore store(databasePath);
            NetworkPreferences preferences(settings, api, nullptr, &store);
            QSignalSpy applied(&preferences, &NetworkPreferences::applied);
            preferences.apply("fixed_servers", {}, "http://127.0.0.1:7890", "proxy-user",
                              "sqlite-only-password", false, "<local>", 1.25, 62.5);
            QTRY_COMPARE(applied.size(), 1);
            QVERIFY(preferences.hasProxyPassword());
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "proxy-password-inspection");
            db.setDatabaseName(databasePath);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT value FROM credentials"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toByteArray(), QByteArray("sqlite-only-password"));
            QVERIFY(!query.next());
        }
        QSqlDatabase::removeDatabase("proxy-password-inspection");
        EchoMusic::SettingsStore settings(ini);
        ApiClient api(DeviceIdentity::temporary());
        SqliteCredentialStore store(databasePath);
        NetworkPreferences restored(settings, api, nullptr, &store);
        QTRY_VERIFY(!restored.busy());
        QVERIFY(restored.hasProxyPassword());
        QCOMPARE(restored.proxyUsername(), QString("proxy-user"));
        QCOMPARE(restored.proxyMode(), QString("fixed_servers"));
        QSignalSpy applied(&restored, &NetworkPreferences::applied);
        restored.apply("fixed_servers", {}, "http://127.0.0.1:7890", "proxy-user", {}, true,
                       "<local>", 1.25, 62.5);
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(!restored.hasProxyPassword());
        bool closed = false;
        restored.eraseCredentialForReset([&](const QString &error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            closed = true;
        });
        QTRY_VERIFY(closed);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "proxy-password-inspection");
            db.setDatabaseName(databasePath);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT COUNT(*) FROM credentials"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 0);
        }
        QSqlDatabase::removeDatabase("proxy-password-inspection");
        QVERIFY(QFile::remove(databasePath));
    }
    void localSqliteWriteFailureCanRetryWithoutChangingAppliedPolicy()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("blocked.sqlite");
        QVERIFY(QDir().mkpath(path));
        EchoMusic::SettingsStore settings(dir.filePath("network.ini"));
        ApiClient api(DeviceIdentity::temporary());
        SqliteCredentialStore store(path);
        NetworkPreferences preferences(settings, api, nullptr, &store);
        QSignalSpy applied(&preferences, &NetworkPreferences::applied);
        preferences.apply("fixed_servers", {}, "http://127.0.0.1:7890", "user", "test-password",
                          false, "<local>", 0, 60);
        QTRY_VERIFY(!preferences.busy());
        QCOMPARE(applied.size(), 0);
        QVERIFY(!preferences.errorMessage().isEmpty());
        QVERIFY(!preferences.hasProxyPassword());
        QCOMPARE(preferences.proxyMode(), QString("direct"));
        QVERIFY(QDir(path).removeRecursively());
        preferences.apply("fixed_servers", {}, "http://127.0.0.1:7890", "user", "test-password",
                          false, "<local>", 0, 60);
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(preferences.hasProxyPassword());
        QCOMPARE(preferences.proxyMode(), QString("fixed_servers"));
    }
    void unclassifiedProxySettingsCannotCreateCredentials()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore settings(dir.filePath("network.ini"));
        settings.setValue(
            "network/unclassifiedPolicy",
            QVariantMap{{"mode", "fixed_servers"}, {"secretReference", "unowned-reference"}});
        QCOMPARE(settings.status(), SettingsStore::FormatError);
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        QVERIFY(!preferences.busy());
        QVERIFY(!preferences.hasProxyPassword());
        QCOMPARE(preferences.proxyMode(), QString("direct"));
        QVERIFY(
            !QFile::exists(QFileInfo(settings.deviceFileName()).dir().filePath("network.sqlite")));
    }
    void passwordCommitRetainClearAndStorageRollback()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("network.ini"));
        ApiClient api(DeviceIdentity::temporary());
        TestProxyStore store;
        NetworkPreferences preferences(settings, api, nullptr, &store);
        QSignalSpy applied(&preferences, &NetworkPreferences::applied);
        const auto apply = [&](const QString &password, bool clear) {
            preferences.apply("fixed_servers", {}, "http://127.0.0.1:7890", "proxy-user", password,
                              clear, "<local>", 1.25, 62.5);
        };
        store.failWrite = true;
        apply("test-only-proxy-password", false);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(preferences.proxyMode(), QString("direct"));
        QVERIFY(!preferences.hasProxyPassword());
        store.failWrite = false;
        apply("test-only-proxy-password", false);
        QTRY_COMPARE(applied.size(), 1);
        QVERIFY(preferences.hasProxyPassword());
        QCOMPARE(store.entries.size(), 1);
        const auto reference = store.entries.keys().first();
        apply({}, false);
        QTRY_COMPARE(applied.size(), 2);
        QCOMPARE(store.entries.size(), 1);
        QCOMPARE(store.entries.keys().first(), reference);
        QFile persisted(settings.fileName());
        QVERIFY(persisted.open(QIODevice::ReadOnly));
        QVERIFY(!persisted.readAll().contains("test-only-proxy-password"));
        {
            EchoMusic::SettingsStore fresh(settings.fileName());
            NetworkPreferences restored(fresh, api, nullptr, &store);
            QVERIFY(restored.hasProxyPassword());
            QVERIFY(!restored.busy());
            QCOMPARE(restored.proxyMode(), QString("fixed_servers"));
        }
        // A real settings write failure after SQLite write must not retire the
        // previously committed password. The temporary replacement is discarded.
        const auto fileName = settings.fileName();
        QVERIFY(QFile::remove(fileName));
        QVERIFY(QDir().mkdir(fileName));
        apply("replacement-that-must-rollback", false);
        QTRY_COMPARE(applied.size(), 2);
        QCOMPARE(store.entries.size(), 1);
        QCOMPARE(store.entries.keys().first(), reference);
        QVERIFY(QDir(fileName).removeRecursively());
        apply({}, true);
        QTRY_COMPARE(applied.size(), 3);
        QVERIFY(!preferences.hasProxyPassword());
        QVERIFY(store.entries.isEmpty());
        preferences.setGithubProxyUrl("https://mirror.example///");
        QCOMPARE(preferences.acceleratedGithubUrl(
                     QUrl("https://raw.githubusercontent.com/owner/repo/HEAD/index.json")),
                 QUrl("https://mirror.example/https://raw.githubusercontent.com/owner/repo/HEAD/"
                      "index.json"));
        QCOMPARE(preferences.acceleratedGithubUrl(QUrl("https://api.example/file")),
                 QUrl("https://api.example/file"));
    }
    void qtApplicationFactoryRoutesHttpAndDefaultProxyButKeepsLanSockets()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("network.ini"));
        ApiClient api(DeviceIdentity::temporary());
        NetworkPreferences preferences(settings, api);
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        QByteArray received;
        connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
            auto *peer = proxy.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, peer, [&, peer] {
                received += peer->readAll();
                if (!received.contains("\r\n\r\n"))
                    return;
                peer->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                peer->disconnectFromHost();
            });
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        });
        QSignalSpy applied(&preferences, &NetworkPreferences::applied);
        preferences.apply("fixed_servers", {}, QString("127.0.0.1:%1").arg(proxy.serverPort()), {},
                          {}, false, "<local>", 0, 60);
        QTRY_COMPARE(applied.size(), 1);
        const auto previous = QNetworkProxy::applicationProxy();
        const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
        preferences.installApplicationTransport();
        const auto chromiumProxy = QNetworkProxy::applicationProxy();
        QCOMPARE(chromiumProxy.type(), QNetworkProxy::HttpProxy);
        QCOMPARE(chromiumProxy.port(), preferences.transportEndpoint().port());
        const auto lan =
            QNetworkProxyFactory::proxyForQuery(QNetworkProxyQuery("192.168.1.2", 8000, "dlna"));
        QCOMPARE(lan.first().type(), QNetworkProxy::NoProxy);
        QTcpServer lanServer;
        QVERIFY(lanServer.listen(QHostAddress::LocalHost));
        QUdpSocket discovery;
        QVERIFY(discovery.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QNetworkAccessManager network;
        auto *reply = network.get(QNetworkRequest(QUrl("http://never-resolve.invalid/factory")));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), QByteArray("ok"));
        QVERIFY(received.startsWith("GET http://never-resolve.invalid/factory HTTP/1.1"));
        reply->deleteLater();
    }
    void manualRulesAndBypass()
    {
        NetworkConfiguration config;
        config.mode = NetworkConfiguration::Mode::FixedServers;
        config.proxyRules = "http=one:81;https=https://two:443,direct://;socks=three:1080";
        QCOMPARE(ProxyRouting::manual(config, QUrl("https://public.example/a")).first().kind,
                 NetworkProxyEndpoint::Kind::Https);
        QCOMPARE(ProxyRouting::manual(config, QUrl("ws://public.example/")).first().kind,
                 NetworkProxyEndpoint::Kind::Socks4);
        QVERIFY(ProxyRouting::bypasses(QUrl("http://127.1.2.3"), "<local>"));
        QVERIFY(!ProxyRouting::bypasses(QUrl("http://127.1.2.3"), "<-loopback>"));
        QVERIFY(ProxyRouting::bypasses(QUrl("http://127.1.2.3"), "<-loopback>,127.0.0.0/8"));
        QVERIFY(!ProxyRouting::bypasses(QUrl("http://example.com"), "10.0.0.0/8"));
        QVERIFY(ProxyRouting::bypasses(QUrl("http://10.1.2.3"), "10.0.0.0/8"));
        QVERIFY(!ProxyRouting::bypasses(QUrl("http://example."), "<local>"));
        QVERIFY(ProxyRouting::bypasses(QUrl("https://api.example.com:8443"),
                                       "https://*.example.com:8443"));
        QVERIFY(!ProxyRouting::bypasses(QUrl("http://api.example.com:8443"),
                                        "https://*.example.com:8443"));
    }
    void cancelledPacIoDrainsAndDoesNotBlockNewDirectRequests()
    {
        QTcpServer pac, origin;
        QVERIFY(pac.listen(QHostAddress::LocalHost));
        QVERIFY(origin.listen(QHostAddress::LocalHost));
        int pendingPac = 0;
        connect(&pac, &QTcpServer::newConnection, &pac, [&] {
            auto *peer = pac.nextPendingConnection();
            ++pendingPac;
            connect(peer, &QTcpSocket::readyRead, peer, [peer] { peer->readAll(); });
            connect(peer, &QTcpSocket::disconnected, peer, [&, peer] {
                --pendingPac;
                peer->deleteLater();
            });
        });
        connect(&origin, &QTcpServer::newConnection, &origin, [&] {
            auto *peer = origin.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, peer, [peer] {
                peer->readAll();
                peer->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                peer->disconnectFromHost();
            });
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        });
        NetworkTransport transport;
        NetworkConfiguration config;
        config.mode = NetworkConfiguration::Mode::PacScript;
        config.pacScript = QString("http://127.0.0.1:%1/proxy.pac").arg(pac.serverPort());
        transport.apply(config, {});
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1",
                                       quint16(transport.endpoint().port())));
        QList<QNetworkReply *> waiting;
        for (int index = 0; index < 20; ++index)
            waiting.append(network.get(
                QNetworkRequest(QUrl(QString("http://pending-%1.invalid/media").arg(index)))));
        QTRY_COMPARE_WITH_TIMEOUT(pendingPac, 4, 3000);
        for (auto *reply : waiting) {
            reply->abort();
            reply->deleteLater();
        }
        transport.apply(NetworkConfiguration{}, {});
        QElapsedTimer elapsed;
        elapsed.start();
        auto *direct = network.get(
            QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/media").arg(origin.serverPort()))));
        QSignalSpy finished(direct, &QNetworkReply::finished);
        QVERIFY(finished.wait(3000));
        QCOMPARE(direct->error(), QNetworkReply::NoError);
        QCOMPARE(direct->readAll(), QByteArray("ok"));
        QVERIFY(elapsed.elapsed() < 2000);
        QTRY_COMPARE_WITH_TIMEOUT(pendingPac, 0, 3000);
        direct->deleteLater();
    }
    void orderedProxyFallbackBadCacheAndWakeReset()
    {
        QTcpServer reservation, healthy, recovered;
        QVERIFY(healthy.listen(QHostAddress::LocalHost));
        QVERIFY(reservation.listen(QHostAddress::LocalHost));
        const auto firstPort = reservation.serverPort();
        reservation.close();
        int healthyRequests = 0, recoveredRequests = 0;
        const auto serve = [](QTcpServer &server, const QByteArray &label, int &requests) {
            connect(&server, &QTcpServer::newConnection, &server, [&server, label, &requests] {
                auto *peer = server.nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(peer, &QTcpSocket::readyRead, peer, [peer, bytes, label, &requests] {
                    *bytes += peer->readAll();
                    if (!bytes->contains("\r\n\r\n"))
                        return;
                    ++requests;
                    peer->write(
                        "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(label.size()) +
                        "\r\nConnection: close\r\n\r\n" + label);
                    peer->disconnectFromHost();
                });
                connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
            });
        };
        serve(healthy, "healthy", healthyRequests);
        serve(recovered, "recovered", recoveredRequests);
        NetworkTransport transport;
        NetworkConfiguration config;
        config.mode = NetworkConfiguration::Mode::FixedServers;
        config.proxyRules =
            QString("127.0.0.1:%1,127.0.0.1:%2").arg(firstPort).arg(healthy.serverPort());
        transport.apply(config, {});
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1",
                                       quint16(transport.endpoint().port())));
        const auto fetch = [&]() -> QByteArray {
            auto *reply =
                network.get(QNetworkRequest(QUrl("http://never-resolve.invalid/fallback")));
            QSignalSpy finished(reply, &QNetworkReply::finished);
            if (!finished.wait(5000)) {
                reply->abort();
                reply->deleteLater();
                return {};
            }
            const auto bytes = reply->readAll();
            reply->deleteLater();
            return bytes;
        };
        QCOMPARE(fetch(), QByteArray("healthy"));
        QCOMPARE(healthyRequests, 1);
        QVERIFY(recovered.listen(QHostAddress::LocalHost, firstPort));
        QCOMPARE(fetch(), QByteArray("healthy"));
        QCOMPARE(healthyRequests, 2);
        QCOMPARE(recoveredRequests, 0);
        transport.recoverAfterWake();
        QCOMPARE(fetch(), QByteArray("recovered"));
        QCOMPARE(recoveredRequests, 1);
        QCOMPARE(healthyRequests, 2);
    }
    void pacFileResolutionAndSourceFailureFallback()
    {
        QTemporaryDir directory;
        const auto fileName = directory.filePath("proxy.pac");
        QFile script(fileName);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("function FindProxyForURL(url,host){ return shExpMatch(host,'*.example') ? "
                     "'PROXY proxy.example:8080; DIRECT' : 'DIRECT'; }");
        script.close();
        NetworkConfiguration config;
        config.mode = NetworkConfiguration::Mode::PacScript;
        config.pacScript = QUrl::fromLocalFile(fileName).toString();
        const auto resolved =
            ProxyResolver::resolve(config, QUrl("https://target.example/private"));
        QCOMPARE(resolved.endpoints.size(), 2);
        QCOMPARE(resolved.endpoints.first().address.host(), QString("proxy.example"));
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
        script.write("throw Error('invalid PAC');");
        script.close();
        ProxyResolver::invalidate();
        const auto failed = ProxyResolver::resolve(config, QUrl("https://target.example/private"));
        QCOMPARE(failed.endpoints.size(), 1);
        QCOMPARE(failed.endpoints.first().kind, NetworkProxyEndpoint::Kind::Direct);
        NetworkConfiguration manual;
        manual.mode = NetworkConfiguration::Mode::FixedServers;
        manual.proxyRules = "https=one.example:81;https=two.example:82";
        const auto duplicate = ProxyRouting::manual(manual, QUrl("https://target.example"));
        QCOMPARE(duplicate.size(), 2);
        QCOMPARE(duplicate.first().address.host(), QString("one.example"));
        manual.proxyRules = "one.example:81;http=two.example:82";
        QCOMPARE(ProxyRouting::manual(manual, QUrl("http://target.example")).first().address.host(),
                 QString("one.example"));
        manual.proxyRules = "http=one.example:81;two.example:82";
        QCOMPARE(ProxyRouting::manual(manual, QUrl("https://target.example")).first().kind,
                 NetworkProxyEndpoint::Kind::Direct);
        manual.proxyRules = "HTTPS=HTTPS://one.example:81";
        QCOMPARE(ProxyRouting::manual(manual, QUrl("https://target.example")).first().kind,
                 NetworkProxyEndpoint::Kind::Direct);
    }
    void pacHelpersAndPrivacy()
    {
        const auto result = ProxyResolver::evaluatePac(
            "function FindProxyForURL(url,host) {"
            "if(url !== 'https://public.example/' || host !== 'public.example') throw "
            "Error('privacy');"
            "if(!dnsDomainIs(host,'.example') || !shExpMatch(host,'public.*')) throw "
            "Error('helpers');"
            "return 'HTTPS proxy.example:443; SOCKS5 socks.example:1080; DIRECT';}",
            QUrl("https://user:secret@public.example/private?token=hidden#fragment"));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.endpoints.size(), 3);
        QCOMPARE(result.endpoints[0].kind, NetworkProxyEndpoint::Kind::Https);
        QCOMPARE(result.endpoints[2].kind, NetworkProxyEndpoint::Kind::Direct);
        QVERIFY(!ProxyResolver::evaluatePac("while(true){}", QUrl("https://public.example"))
                     .error.isEmpty());
    }
    void proxyTunnelHandshakes_data()
    {
        QTest::addColumn<QString>("scheme");
        QTest::addColumn<bool>("trusted");
        QTest::newRow("http-connect") << QString("http") << true;
        QTest::newRow("https-proxy-verified-tls") << QString("https") << true;
        QTest::newRow("https-proxy-rejects-untrusted") << QString("https") << false;
        QTest::newRow("socks4") << QString("socks4") << true;
        QTest::newRow("socks5-auth-remote-dns") << QString("socks5") << true;
    }
    void proxyTunnelHandshakes()
    {
        QFETCH(QString, scheme);
        QFETCH(bool, trusted);
        const auto originalSsl = QSslConfiguration::defaultConfiguration();
        const auto restoreSsl =
            qScopeGuard([&] { QSslConfiguration::setDefaultConfiguration(originalSsl); });
        std::unique_ptr<QTcpServer> proxy;
        if (scheme == "https") {
            QFile certificateFile(QFINDTESTDATA("../fixtures/network/proxy-test-cert.pem"));
            QFile authorityFile(QFINDTESTDATA("../fixtures/network/proxy-test-ca.pem"));
            QFile keyFile(QFINDTESTDATA("../fixtures/network/proxy-test-key.pem"));
            QVERIFY(certificateFile.open(QIODevice::ReadOnly));
            QVERIFY(authorityFile.open(QIODevice::ReadOnly));
            QVERIFY(keyFile.open(QIODevice::ReadOnly));
            QSslCertificate certificate(certificateFile.readAll());
            QSslKey key(keyFile.readAll(), QSsl::Rsa);
            QVERIFY(!certificate.isNull());
            QVERIFY(!key.isNull());
            if (trusted) {
                auto configuration = originalSsl;
                configuration.addCaCertificate(QSslCertificate(authorityFile.readAll()));
                QSslConfiguration::setDefaultConfiguration(configuration);
            }
            auto serverConfiguration = originalSsl;
            serverConfiguration.setLocalCertificate(certificate);
            serverConfiguration.setPrivateKey(key);
            serverConfiguration.setPeerVerifyMode(QSslSocket::VerifyNone);
            auto server = std::make_unique<QSslServer>();
            server->setSslConfiguration(serverConfiguration);
            proxy = std::move(server);
        } else
            proxy = std::make_unique<QTcpServer>();
        QVERIFY(proxy->listen(QHostAddress::LocalHost));
        QByteArray authentication, destination;
        const QByteArray greeting("opaque-server-greeting");
        connect(proxy.get(), &QTcpServer::pendingConnectionAvailable, proxy.get(), [&] {
            auto *peer = proxy->nextPendingConnection();
            QVERIFY(peer);
            if (scheme == "https")
                QVERIFY(qobject_cast<QSslSocket *>(peer)->isEncrypted());
            auto buffer = std::make_shared<QByteArray>();
            auto stage = std::make_shared<int>(0);
            const auto readRequest = [&, peer, buffer, stage] {
                const auto data = peer->readAll();
                if (*stage == 4) {
                    peer->write(data);
                    return;
                }
                *buffer += data;
                if ((scheme == "http" || scheme == "https")) {
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    if (!buffer->toLower().contains("proxy-authorization:")) {
                        peer->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                                    "Proxy-Authenticate: Basic realm=\"echo-test\"\r\n"
                                    "Content-Length: 0\r\nProxy-Connection: keep-alive\r\n\r\n");
                        buffer->clear();
                        return;
                    }
                    destination = *buffer;
                    peer->write("HTTP/1.1 200 Connection Established\r\n\r\n" + greeting);
                    *stage = 4;
                } else if (scheme == "socks4") {
                    if (buffer->size() < 9 || buffer->indexOf('\0', 8) < 0)
                        return;
                    destination = buffer->left(8);
                    authentication = buffer->mid(8, buffer->indexOf('\0', 8) - 8);
                    peer->write(QByteArray::fromHex("005a000000000000") + greeting);
                    *stage = 4;
                } else if (*stage == 0) {
                    if (buffer->size() < 4)
                        return;
                    peer->write(QByteArray::fromHex("0502"));
                    buffer->clear();
                    *stage = 1;
                } else if (*stage == 1) {
                    if (buffer->size() < 3)
                        return;
                    const auto userLength = quint8((*buffer)[1]);
                    if (buffer->size() < 3 + userLength)
                        return;
                    const auto passwordLength = quint8((*buffer)[2 + userLength]);
                    if (buffer->size() < 3 + userLength + passwordLength)
                        return;
                    authentication = buffer->mid(2, userLength) + ':' +
                                     buffer->mid(3 + userLength, passwordLength);
                    peer->write(QByteArray::fromHex("0100"));
                    buffer->clear();
                    *stage = 2;
                } else {
                    if (buffer->size() < 5)
                        return;
                    const auto length = quint8((*buffer)[4]);
                    if (buffer->size() < length + 7)
                        return;
                    destination = buffer->mid(5, length);
                    peer->write(QByteArray::fromHex("05000001000000000000") + greeting);
                    *stage = 4;
                }
            };
            connect(peer, &QTcpSocket::readyRead, peer, readRequest);
            // TLS may buffer CONNECT before QSslServer publishes the accepted
            // peer. Installing a handler does not replay its earlier readyRead.
            if (peer->bytesAvailable() > 0)
                readRequest();
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        });
        NetworkTransport transport;
        NetworkConfiguration config;
        config.mode = NetworkConfiguration::Mode::FixedServers;
        config.proxyRules = scheme + "://localhost:" + QString::number(proxy->serverPort());
        config.bypassRules = "<-loopback>";
        config.username = "route-user";
        transport.apply(config, "route-secret");
        QTcpSocket client;
        client.setProxy(QNetworkProxy::NoProxy);
        QByteArray received;
        connect(&client, &QTcpSocket::readyRead, &client, [&] { received += client.readAll(); });
        client.connectToHost(QHostAddress::LocalHost, quint16(transport.endpoint().port()));
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        const auto target = scheme == "socks4" ? QByteArray("127.0.0.2:443")
                                               : QByteArray("never-resolve.invalid:443");
        client.write("CONNECT " + target + " HTTP/1.1\r\nHost: " + target + "\r\n\r\n");
        if (!trusted) {
            QTRY_VERIFY(received.startsWith("HTTP/1.1 502"));
            QVERIFY(destination.isEmpty());
            QVERIFY(authentication.isEmpty());
            return;
        }
        QTRY_VERIFY2_WITH_TIMEOUT(received.contains(greeting), received.constData(), 5000);
        QVERIFY(received.startsWith("HTTP/1.1 200"));
        if ((scheme == "http" || scheme == "https")) {
            QVERIFY(destination.startsWith("CONNECT never-resolve.invalid:443 HTTP/1.1"));
            QVERIFY(destination.contains("Proxy-Authorization: Basic " +
                                         QByteArray("route-user:route-secret").toBase64()));
        } else if (scheme == "socks4") {
            QCOMPARE(destination, QByteArray::fromHex("040101bb7f000002"));
            QCOMPARE(authentication, QByteArray("route-user"));
        } else {
            QCOMPARE(destination, QByteArray("never-resolve.invalid"));
            QCOMPARE(authentication, QByteArray("route-user:route-secret"));
        }
        received.clear();
        const QByteArray payload(512 * 1024, 'p');
        client.write(payload);
        QTRY_COMPARE_WITH_TIMEOUT(received.size(), payload.size(), 5000);
        QCOMPARE(received, payload);
        transport.apply(NetworkConfiguration{}, {});
        QTRY_COMPARE(client.state(), QAbstractSocket::UnconnectedState);
    }
    void managedMediaPreservesStreamingFailure_data()
    {
        QTest::addColumn<QByteArray>("response");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("success");
        QTest::newRow("complete-chunks")
            << QByteArray("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: "
                          "close\r\n\r\n2\r\nok\r\n0\r\n\r\n")
            << 200 << true;
        QTest::newRow("truncated-chunks")
            << QByteArray("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: "
                          "close\r\n\r\n5\r\nok")
            << 200 << false;
        QTest::newRow("unusable-redirect")
            << QByteArray("HTTP/1.1 302 Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            << 502 << false;
    }
    void managedMediaPreservesStreamingFailure()
    {
        QFETCH(QByteArray, response);
        QFETCH(int, status);
        QFETCH(bool, success);
        QTcpServer origin;
        origin.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(origin.listen(QHostAddress::LocalHost));
        connect(&origin, &QTcpServer::newConnection, &origin, [&] {
            auto *peer = origin.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, peer, [&, peer] {
                const auto header = peer->property("header").toByteArray() + peer->readAll();
                peer->setProperty("header", header);
                if (!header.contains("\r\n\r\n") || peer->property("answered").toBool())
                    return;
                peer->setProperty("answered", true);
                peer->write(response);
                peer->disconnectFromHost();
            });
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        });
        NetworkTransport transport;
        transport.apply(NetworkConfiguration{}, {});
        const auto resource = transport.registerMedia(
            QUrl(QString("http://127.0.0.1:%1/video").arg(origin.serverPort())));
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        auto *reply = network.get(QNetworkRequest(resource));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), status);
        QCOMPARE(reply->error() == QNetworkReply::NoError, success);
        if (success)
            QCOMPARE(reply->readAll(), QByteArray("ok"));
        reply->deleteLater();
        transport.releaseMedia(resource);
    }
    void managedMediaKeepsCookieAndContentEncodingIsolation()
    {
        QTcpServer proxy;
        proxy.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        QList<QByteArray> requests;
        // Deliberately ignore Accept-Encoding as a misconfigured origin might:
        // the adapter must still preserve the exact encoded byte stream.
        const auto encoded = QByteArray::fromHex("1f8b0800000000000213cbcf060047dddc7902000000");
        connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
            auto *peer = proxy.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, peer, [&, peer] {
                const auto header = peer->property("header").toByteArray() + peer->readAll();
                peer->setProperty("header", header);
                if (!header.contains("\r\n\r\n") || peer->property("answered").toBool())
                    return;
                peer->setProperty("answered", true);
                requests.append(header);
                peer->write("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " +
                            QByteArray::number(encoded.size()) +
                            "\r\nSet-Cookie: prior-account=must-not-leak; Path=/\r\nConnection: "
                            "close\r\n\r\n" +
                            encoded);
                peer->disconnectFromHost();
            });
            connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        });
        NetworkTransport transport;
        NetworkConfiguration config;
        config.mode = NetworkConfiguration::Mode::FixedServers;
        config.proxyRules = QString("http://127.0.0.1:%1").arg(proxy.serverPort());
        transport.apply(config, {});
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        for (int i = 0; i < 2; ++i) {
            const auto resource =
                transport.registerMedia(QUrl(QString("http://video-source.invalid/%1").arg(i)));
            QNetworkRequest request(resource);
            request.setRawHeader("Accept-Encoding", "identity");
            request.setRawHeader("Cookie", "untrusted-local-client=must-not-forward");
            request.setRawHeader("Authorization", "Bearer must-not-forward");
            auto *reply = network.get(request);
            QSignalSpy finished(reply, &QNetworkReply::finished);
            QVERIFY(finished.wait(5000));
            QCOMPARE(reply->error(), QNetworkReply::NoError);
            QCOMPARE(reply->readAll(), encoded);
            QCOMPARE(reply->rawHeader("Content-Encoding"), QByteArray("gzip"));
            QCOMPARE(reply->rawHeader("Content-Length").toInt(), encoded.size());
            QVERIFY(!reply->hasRawHeader("Set-Cookie"));
            reply->deleteLater();
            transport.releaseMedia(resource);
        }
        QCOMPARE(requests.size(), 2);
        for (const auto &request : requests) {
            const auto header = request.toLower();
            QVERIFY(header.contains("accept-encoding: identity"));
            QVERIFY(!header.contains("cookie:"));
            QVERIFY(!header.contains("authorization:"));
        }
    }
    void directAndManualHttpRoutes()
    {
        QTcpServer origin, proxy;
        QVERIFY(origin.listen(QHostAddress::LocalHost));
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        QList<QByteArray> originRequests, proxyRequests;
        const auto serve = [](QTcpServer &server, QList<QByteArray> &requests, bool auth = false) {
            connect(&server, &QTcpServer::newConnection, &server, [&server, &requests, auth] {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests, auth] {
                    const auto bytes = socket->readAll();
                    if (!bytes.contains("\r\n\r\n"))
                        return;
                    if (auth && !bytes.toLower().contains("proxy-authorization:")) {
                        socket->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                                      "Proxy-Authenticate: Basic realm=\"test\"\r\n"
                                      "Content-Length: 0\r\nProxy-Connection: keep-alive\r\n\r\n");
                        return;
                    }
                    requests.append(bytes);
                    socket->write(
                        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            });
        };
        serve(origin, originRequests);
        serve(proxy, proxyRequests, true);
        NetworkTransport transport;
        QVERIFY(transport.isReady());
        transport.apply(NetworkConfiguration{}, {});
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1",
                                       quint16(transport.endpoint().port())));
        auto *direct = network.get(QNetworkRequest(
            QUrl(QString("http://127.0.0.1:%1/music?q=1").arg(origin.serverPort()))));
        QSignalSpy done(direct, &QNetworkReply::finished);
        QVERIFY(done.wait(5000));
        QCOMPARE(direct->error(), QNetworkReply::NoError);
        QCOMPARE(direct->readAll(), QByteArray("ok"));
        QCOMPARE(originRequests.size(), 1);
        QVERIFY(originRequests[0].startsWith("GET /music?q=1 HTTP/1.1"));
        QVERIFY(!originRequests[0].contains("Proxy-Authorization"));
        direct->deleteLater();
        NetworkConfiguration config;
        config.mode = NetworkConfiguration::Mode::FixedServers;
        config.proxyRules = QString("http://127.0.0.1:%1").arg(proxy.serverPort());
        config.username = "route-user";
        transport.apply(config, "route-secret");
        auto *proxied = network.get(QNetworkRequest(QUrl("http://never-resolve.invalid/media")));
        QSignalSpy finished(proxied, &QNetworkReply::finished);
        QVERIFY(finished.wait(5000));
        QCOMPARE(proxied->error(), QNetworkReply::NoError);
        QCOMPARE(proxyRequests.size(), 1);
        QVERIFY(proxyRequests[0].startsWith("GET http://never-resolve.invalid/media HTTP/1.1"));
        QVERIFY(proxyRequests[0].contains("Proxy-Authorization: Basic " +
                                          QByteArray("route-user:route-secret").toBase64()));
        QCOMPARE(originRequests.size(), 1);
        proxied->deleteLater();
        const auto resource = transport.registerMedia(
            QUrl("http://never-resolve.invalid/video?private=not-in-public-url"));
        QVERIFY(!resource.toString().contains("private"));
        QNetworkRequest videoRequest(resource);
        videoRequest.setRawHeader("Range", "bytes=0-1");
        auto *video = network.get(videoRequest);
        QSignalSpy videoFinished(video, &QNetworkReply::finished);
        QVERIFY(videoFinished.wait(5000));
        QCOMPARE(video->error(), QNetworkReply::NoError);
        QCOMPARE(video->readAll(), QByteArray("ok"));
        QCOMPARE(proxyRequests.size(), 2);
        QVERIFY(proxyRequests.last().toLower().contains("range: bytes=0-1"));
        video->deleteLater();
        transport.releaseMedia(resource);
        auto *retired = network.get(QNetworkRequest(resource));
        QSignalSpy retiredFinished(retired, &QNetworkReply::finished);
        QVERIFY(retiredFinished.wait(5000));
        QCOMPARE(retired->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
        QCOMPARE(proxyRequests.size(), 2);
        retired->deleteLater();
    }
};
int main(int argc, char **argv)
{
    initializeNetworkEnvironment();
    QCoreApplication application(argc, argv);
    NetworkTransportTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "NetworkTransportTest.moc"
