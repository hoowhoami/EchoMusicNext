#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

void PluginRuntimeTest::marketplaceInstallsActualPackageAndRejectsTampering()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QFile manifestFile(QFINDTESTDATA("../fixtures/plugins/marketplace/manifest.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    auto manifest = manifestFile.readAll();
    QFile archiveFile(QFINDTESTDATA("../fixtures/plugins/marketplace/archive.echomod"));
    QVERIFY(archiveFile.open(QIODevice::ReadOnly));
    auto archive = archiveFile.readAll();
    QJsonObject reportedEvent;
    bool holdCatalog = true;
    bool failCatalog = false;
    QList<QPointer<QTcpSocket>> heldRequests;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            const auto request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n"))
                return;
            const auto headerEnd = request.indexOf("\r\n\r\n") + 4;
            const auto length = QRegularExpression("(?im)^content-length: *([0-9]+)")
                                    .match(QString::fromLatin1(request.left(headerEnd)))
                                    .captured(1)
                                    .toInt();
            if (request.size() < headerEnd + length)
                return;
            if (holdCatalog) {
                if (!heldRequests.contains(socket))
                    heldRequests.append(socket);
                return;
            }
            const auto requestBody =
                QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
            if (requestBody.contains("event"))
                reportedEvent = requestBody;
            const auto body =
                request.startsWith("GET /echo-plugins.json ")
                    ? QJsonDocument(
                          QJsonObject{{"schemaVersion", 2},
                                      {"plugins",
                                       QJsonArray{QJsonObject{
                                           {"id", "org.test.catalog"},
                                           {"path", "plugins/fixture"},
                                           {"artifact", "artifacts/fixture.echomod"},
                                           {"sha256", QString::fromLatin1(
                                                          QCryptographicHash::hash(
                                                              archive, QCryptographicHash::Sha256)
                                                              .toHex())}}}}})
                          .toJson()
                : request.startsWith("GET /archive.echomod ") ? archive
                : request.startsWith("POST ") ? QByteArray(R"({"ok":true,"plugins":[]})")
                                              : manifest;
            const QByteArray status = failCatalog && request.startsWith("GET /echo-plugins.json ")
                                          ? "HTTP/1.1 503 Unavailable"
                                          : "HTTP/1.1 200 OK";
            socket->write(status + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    });
    manager.m_resolveUrl = [&](const QUrl &url) {
        return QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) +
                    (url.path().endsWith(".echomod")            ? "/archive.echomod"
                     : url.path().endsWith("echo-plugins.json") ? "/echo-plugins.json"
                     : url.path().contains("/v1/")              ? "/stats"
                                                                : "/manifest.json"));
    };
    host.router.registerPage({"plugin-management",
                              "/main/settings/plugins",
                              QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginManagementPage.qml"),
                              {"view"},
                              true,
                              false,
                              false,
                              {}});
    QVERIFY(host.router.navigate("plugin-management", {{"view", "marketplace"}}));
    ShellSettings uiSettings(host.settings);
    QQmlComponent ui(&host.engine);
    ui.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Plugins
import EchoMusic.Shell
ApplicationWindow {
 id: uiWin
 required property PluginManager manager
 required property RouteController router
 required property ShellSettings settings
 property real uiScale: 1
 width:900; height:650; visible:true; color:Theme.main
 Item {
  width: uiWin.width / uiWin.uiScale; height: uiWin.height / uiWin.uiScale
  scale: uiWin.uiScale; transformOrigin: Item.TopLeft
  PluginManagementPage { anchors.fill:parent; manager:uiWin.manager; router:uiWin.router; context:uiWin.router.current; settings:uiWin.settings }
 }
})",
               QUrl("qrc:/marketplace-ui-test.qml"));
    std::unique_ptr<QObject> uiRoot(
        ui.createWithInitialProperties({{"manager", QVariant::fromValue(&manager)},
                                        {"router", QVariant::fromValue(&host.router)},
                                        {"settings", QVariant::fromValue(&uiSettings)}}));
    QVERIFY2(uiRoot, qPrintable(ui.errorString()));
    auto *uiWindow = qobject_cast<QQuickWindow *>(uiRoot.get());
    QVERIFY(uiWindow && QTest::qWaitForWindowExposed(uiWindow));
    const auto uiOutput = qEnvironmentVariable("ECHO_PLUGIN_UI_OUTPUT");
    QStringList uiWarnings;
    connect(&host.engine, &QQmlEngine::warnings, uiRoot.get(), [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            uiWarnings.append(error.toString());
    });
    QTRY_VERIFY(visual(uiWindow->contentItem(), "pluginMarketplaceSkeleton"));

    if (!uiOutput.isEmpty()) {
        QDir().mkpath(uiOutput);
        QVERIFY(uiWindow->grabWindow().save(uiOutput + "/marketplace-loading.png"));
    }
    holdCatalog = false;
    for (auto &socket : heldRequests)
        if (socket)
            QVERIFY(QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection));
    manager.refreshCatalog();
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(manager.catalogCount(), 1);
    QTRY_VERIFY(visual(uiWindow->contentItem(), "pluginStat-heartbeat"));
    const auto catalogFitsBelowHeading = [&] {
        auto *heading = visual(uiWindow->contentItem(), "pluginCatalogHeading");
        auto *card = visual(uiWindow->contentItem(), "pluginCard-org.test.catalog");
        if (!heading || !card)
            return false;
        const auto gap =
            card->mapToScene({0, 0}).y() - heading->mapToScene({0, heading->height()}).y();
        return qAbs(gap - 14 * uiWindow->property("uiScale").toReal()) < 1.;
    };
    QTRY_VERIFY(!visual(uiWindow->contentItem(), "pluginMarketplaceSkeleton"));
    QTRY_VERIFY(catalogFitsBelowHeading());
    if (!uiOutput.isEmpty()) {
        QDir().mkpath(uiOutput);
        uiWindow->resize(1000, 800);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            host.theme.setMode(mode);
            for (const auto scale : {1.0, 1.25}) {
                uiWindow->setProperty("uiScale", scale);
                QTest::mouseMove(uiWindow, {uiWindow->width() - 4, uiWindow->height() - 4});
                QTest::qWait(100);
                const auto name = QString("/marketplace-%1-%2").arg(int(mode)).arg(scale);
                QVERIFY(uiWindow->grabWindow().save(uiOutput + name + ".png"));
                auto *surface = visual(uiWindow->contentItem(), "pluginCard-org.test.catalog");
                QVERIFY(surface);
                QTest::mouseMove(uiWindow,
                                 surface->mapToScene({surface->width() - 8, 8}).toPoint());
                QTest::qWait(220);
                QVERIFY(uiWindow->grabWindow().save(uiOutput + name + "-hover.png"));
            }
        }
        uiWindow->setProperty("uiScale", 1.0);
        host.theme.setMode(ThemeStore::Light);
        host.theme.selectSolid(QColor("#fff0ed"));
        QTest::mouseMove(uiWindow, {uiWindow->width() - 4, uiWindow->height() - 4});
        QTest::qWait(100);
        QVERIFY(uiWindow->grabWindow().save(uiOutput + "/marketplace-tinted.png"));
        host.theme.selectEcho();
        uiWindow->resize(900, 650);
        QTest::mouseMove(uiWindow, {uiWindow->width() - 4, uiWindow->height() - 4});
    }
    {
        // A fresh manager restores catalog.json but still owes its first
        // network refresh. Exercise that state without faking loaded/loading.
        PluginManager cached(host.runtime, host.settings);
        cached.m_resolveUrl = manager.m_resolveUrl;
        QCOMPARE(cached.catalogCount(), 1);
        QVERIFY(!cached.loaded());
        const auto restoreManager =
            qScopeGuard([&] { uiRoot->setProperty("manager", QVariant::fromValue(&manager)); });
        QVERIFY(uiRoot->setProperty("manager", QVariant::fromValue(&cached)));
        QTRY_VERIFY(catalogFitsBelowHeading());
        heldRequests.clear();
        holdCatalog = true;
        cached.refreshCatalog();
        QVERIFY(cached.catalogLoading());
        QTRY_VERIFY(!heldRequests.isEmpty());
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
            for (const auto scale : {1., 1.25}) {
                host.theme.setMode(mode);
                uiWindow->setProperty("uiScale", scale);
                QTRY_VERIFY(!visual(uiWindow->contentItem(), "pluginMarketplaceSkeleton"));
                QTRY_VERIFY(catalogFitsBelowHeading());
                if (!uiOutput.isEmpty())
                    QVERIFY(uiWindow->grabWindow().save(
                        uiOutput + QString("/marketplace-cached-refresh-%1-%2.png")
                                       .arg(int(mode))
                                       .arg(scale)));
                // Filtering does not turn an existing catalog into a cold load.
                cached.setSearch("no-matching-cached-plugin");
                QTRY_COMPARE(cached.catalog()->rowCount(), 0);
                QVERIFY(!visual(uiWindow->contentItem(), "pluginMarketplaceSkeleton"));
                cached.setSearch("");
                QTRY_VERIFY(catalogFitsBelowHeading());
            }
        failCatalog = true;
        holdCatalog = false;
        for (auto &socket : heldRequests)
            if (socket)
                QVERIFY(QMetaObject::invokeMethod(socket, "readyRead", Qt::QueuedConnection));
        QTRY_VERIFY(!cached.catalogLoading());
        QCOMPARE(cached.catalogCount(), 1);
        QVERIFY(!cached.sourceError().isEmpty());
        QTRY_VERIFY(!visual(uiWindow->contentItem(), "pluginMarketplaceSkeleton"));
        QTRY_VERIFY(catalogFitsBelowHeading());
        if (!uiOutput.isEmpty())
            QVERIFY(uiWindow->grabWindow().save(uiOutput + "/marketplace-cached-failure.png"));
        cached.shutdown();
        failCatalog = false;
    }
    uiWindow->setProperty("uiScale", 1.);
    host.theme.setMode(ThemeStore::Light);
    QTRY_VERIFY(catalogFitsBelowHeading());
    auto *sourceAction = visual(uiWindow->contentItem(), "pluginSourceManage");
    QVERIFY(sourceAction);
    QVERIFY(QMetaObject::invokeMethod(sourceAction, "clicked"));
    auto *sourceDialog = uiRoot->findChild<QObject *>("pluginSourceDialog");
    QVERIFY(sourceDialog);
    QTRY_VERIFY(sourceDialog->property("visible").toBool());
    QTRY_COMPARE(sourceDialog->property("opacity").toReal(), 1.0);
    if (!uiOutput.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(uiWindow->grabWindow().save(uiOutput + "/source-dialog.png"));
    }
    QVERIFY2(uiWarnings.isEmpty(), qPrintable(uiWarnings.join('\n')));
    uiRoot.reset();
    const auto key = manager.catalog()
                         ->data(manager.catalog()->index(0), Qt::UserRole + 1)
                         .toMap()
                         .value("key")
                         .toString();
    QSignalSpy notice(&manager, &PluginManager::notice);
    manager.share(key);
    const QUrl shareUrl(QGuiApplication::clipboard()->text());
    QVERIFY(shareUrl.scheme() == "echomusicnext");
    SharingCommands publisher(host.account);
    manager.attachSharing(publisher);
    manager.share(key);
    const auto sharedTarget = parseShareTarget(QGuiApplication::clipboard()->text());
    QVERIFY(sharedTarget && sharedTarget->type == ShareResource::Plugin);
    QCOMPARE(sharedTarget->id, QString("org.test.catalog"));
    QVERIFY(QGuiApplication::clipboard()->text().contains(
        "https://hoowhoami.github.io/EchoMusicNext/share/"));
    QVERIFY(
        manager.patchSource("github:hoowhoami/echomusicnextplugins", "Private local label", false));
    auto versionDriftShare = *sharedTarget;
    versionDriftShare.query["version"] = "2.0.0";
    QVERIFY(manager.openShare(versionDriftShare));
    QCOMPARE(manager.shareState(), "need-source");
    QVERIFY(manager.acceptShareSource());
    QTRY_COMPARE(manager.shareState(), "ready");
    QCOMPARE(manager.shareRecord().value("shareWarning").toString(),
             "分享时版本为 v2.0.0，当前插件源提供 v1.0.0。");
    ShellSettings shellSettings(host.settings);
    QQmlComponent sharePage(&host.engine,
                            QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginShareResolvePage.qml"));
    std::unique_ptr<QObject> shareRoot(sharePage.createWithInitialProperties(
        {{"manager", QVariant::fromValue(&manager)},
         {"router", QVariant::fromValue(&host.router)},
         {"context", QVariant::fromValue(host.router.current())},
         {"settings", QVariant::fromValue(&shellSettings)}}));
    QVERIFY2(shareRoot, qPrintable(sharePage.errorString()));
    QVERIFY(manager.installShared());
    QTRY_VERIFY(!manager.busy());
    QTRY_VERIFY(!reportedEvent.isEmpty());
    QVERIFY(!QJsonDocument(reportedEvent).toJson().contains("Private local label"));
    QCOMPARE(reportedEvent.value("plugin").toObject().value("sourceName").toString(),
             QString("hoowhoami/EchoMusicNextPlugins"));
    QTRY_COMPARE(manager.shareState(), "installed");
    QVERIFY(!manager.openShare(
        QUrl("echomusicnext://plugin?source=file:///tmp&id=x&version=1.0.0&path=x")));
    const auto installation = host.runtime.installationForId("org.test.catalog");
    QVERIFY(!installation.isEmpty());
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *command = host.runtime.executeCommand(installation, "value");
    QTRY_COMPARE(command->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(command->result().toInt(), 7);
    QVERIFY(
        QFileInfo(PluginPaths(host.runtime.directory()).data("org.test.catalog") + "/values.sqlite")
            .isFile());
    const auto originalArchive = archive;
    const auto generation = host.runtime.context(installation)->activationGeneration();
    // The index has already bound the original bytes. Even an otherwise
    // extractable container must fail before replacing any active code.
    archive.append("tampered");
    QVERIFY(manager.install(key));
    QTRY_VERIFY(!manager.busy());
    QVERIFY(notice.last()[1].toBool());
    QVERIFY(notice.last()[0].toString().contains("SHA-256"));
    QCOMPARE(host.runtime.context(installation)->activationGeneration(), generation);
    archive = originalArchive;
    manifest.replace("1.0.0\"", "1.1.0\"");
    manager.refreshCatalog(true);
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(manager.updateCount(), 1);
    QVERIFY(manager.install(key));
    QTRY_VERIFY(!manager.busy());
    QVERIFY(notice.last()[1].toBool());
    QCOMPARE(host.runtime.record(installation).value("version").toString(), "1.0.0");
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    manager.shutdown();
}
