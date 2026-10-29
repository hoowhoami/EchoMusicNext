#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "PluginSourceEntry.h"

void PluginRuntimeTest::crossRepositoryCatalogInstallAndFailureIsolation()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    QFile manifestFile(QFINDTESTDATA("../fixtures/plugins/marketplace/manifest.json"));
    QFile archiveFile(QFINDTESTDATA("../fixtures/plugins/marketplace/archive.echomod"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QVERIFY(archiveFile.open(QIODevice::ReadOnly));
    const auto manifest = manifestFile.readAll(), archive = archiveFile.readAll();
    const auto checksum =
        QString::fromLatin1(QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex());
    QTcpServer indexServer, authorServer;
    QVERIFY(indexServer.listen(QHostAddress::LocalHost));
    QVERIFY(authorServer.listen(QHostAddress::LocalHost));
    QStringList requests;
    bool failAuthor = false;
    const auto wire = [&](QTcpServer &server, bool index) {
        connect(&server, &QTcpServer::newConnection, &server, [&, index, serverPointer = &server] {
            auto *socket = serverPointer->nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, index] {
                auto request = socket->property("bytes").toByteArray() + socket->readAll();
                socket->setProperty("bytes", request);
                if (!request.contains("\r\n\r\n"))
                    return;
                const auto url = QString::fromUtf8(
                    QByteArray::fromPercentEncoding(request.split(' ').value(1).mid(1)));
                requests.append(url);
                bool ok = true;
                QByteArray body;
                if (index)
                    body = QJsonDocument(
                               QJsonObject{
                                   {"schemaVersion", 2},
                                   {"plugins",
                                    QJsonArray{
                                        QJsonObject{
                                            {"id", "org.test.catalog"},
                                            {"repo", "https://github.com/author/independent.git/"},
                                            {"ref", "release/v1"},
                                            {"path", ""},
                                            {"artifact", "https://github.com/author/independent/"
                                                         "releases/download/v1/plugin.echomod"},
                                            {"sha256", checksum}},
                                        QJsonObject{{"id", "missing-plugin"},
                                                    {"repo", "https://github.com/other/missing"},
                                                    {"path", "plugins/missing"},
                                                    {"artifact", "artifacts/missing.echomod"},
                                                    {"sha256", checksum}}}}})
                               .toJson();
                else {
                    ok = !url.contains("/other/missing/") && !failAuthor;
                    body = url.endsWith(".echomod") ? archive : manifest;
                }
                socket->write(QByteArray(ok ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Missing\r\n") +
                              "Connection: close\r\nContent-Length: " +
                              QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            });
        });
    };
    wire(indexServer, true);
    wire(authorServer, false);
    manager.m_resolveUrl = [&](const QUrl &url) {
        const bool index = url.path().endsWith("echo-plugins.json");
        return QUrl("http://127.0.0.1:" +
                    QString::number(index ? indexServer.serverPort() : authorServer.serverPort()) +
                    '/' + QString::fromLatin1(QUrl::toPercentEncoding(url.toString())));
    };
    manager.refreshCatalog();
    QTRY_VERIFY(manager.loaded());
    QCOMPARE(manager.catalogCount(), 1);
    QVERIFY(!manager.sourceError().isEmpty());
    const auto row = manager.catalog()->data(manager.catalog()->index(0), Qt::UserRole + 1).toMap();
    QCOMPARE(row.value("repository").toString(), "author/independent");
    QVERIFY(requests.contains(
        "https://raw.githubusercontent.com/author/independent/release%2Fv1/manifest.json"));
    QVERIFY(manager.install(row.value("key").toString()));
    QTRY_VERIFY(!manager.busy());
    const auto installation = host.runtime.installationForId("org.test.catalog");
    QVERIFY(!installation.isEmpty());
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QCOMPARE(
        host.runtime.record(installation).value("origin").toMap().value("repository").toString(),
        "author/independent");
    QVERIFY(QFileInfo(host.runtime.directory() + "/org.test.catalog/manifest.json").isFile());
    QVERIFY(
        QFileInfo(PluginPaths(host.runtime.directory()).data("org.test.catalog") + "/values.sqlite")
            .isFile());
    failAuthor = true;
    manager.refreshCatalog(true);
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(manager.catalogCount(), 1);
    QVERIFY(manager.catalog()
                ->data(manager.catalog()->index(0), Qt::UserRole + 1)
                .toMap()
                .value("stale")
                .toBool());
    QString error;
    QVERIFY(PluginSourceEntry::parse({{"id", "con.txt"}}, "author/repo", error).isEmpty());
}

void PluginRuntimeTest::flatDirectoriesDataManagementAndReinstallIsolation()
{
    Host host;
    const auto source =
        package(host.temporary.path(),
                "import {awaitOperation} from 'echo:plugin-sdk'; export function activate(c){ "
                "c.registerCommand('read',()=>awaitOperation(c.io.readValue('value'))); return "
                "awaitOperation(c.io.writeValue('value',42)); }",
                {"lifecycle", "commands", "storage"});
    const QVariantMap firstOrigin{
        {"kind", "marketplace"}, {"id", "github:official/catalog"}, {"repository", "author/first"}};
    QVERIFY(host.runtime.installDirectory(source, firstOrigin));
    QTRY_VERIFY(!host.runtime.loading());
    const auto token = host.installation();
    QVERIFY(QFileInfo(host.runtime.directory() + "/test.runtime/manifest.json").isFile());
    QVERIFY(QFileInfo(PluginPaths(host.runtime.directory()).data("test.runtime") + "/values.sqlite")
                .isFile());
    QVERIFY(!QDir(host.runtime.directory() + "/packages").exists());
    DirectoryCapture capture;
    QDesktopServices::setUrlHandler("file", &capture, "open");
    QVERIFY(host.runtime.openPluginDirectory(token));
    QCOMPARE(capture.opened.toLocalFile(), host.runtime.directory() + "/test.runtime");
    QVERIFY(host.runtime.uninstall(token, false));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.dataEntries().size(), 1);
    QVERIFY(host.runtime.dataEntries().first().toMap().value("retained").toBool());
    write(source + "/entry.mjs",
          "import {awaitOperation} from 'echo:plugin-sdk'; export function activate(c){ "
          "c.registerCommand('read',()=>awaitOperation(c.io.readValue('value'))); } ");
    auto secondOrigin = firstOrigin;
    secondOrigin["repository"] = "author/second";
    QVERIFY(host.runtime.installDirectory(source, secondOrigin));
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(host.installation() != token);
    auto *read = host.runtime.executeCommand(host.installation(), "read");
    QVERIFY(read);
    QTRY_VERIFY(read->finished());
    QVERIFY(!read->result().isValid());
    QCOMPARE(host.runtime.dataEntries().size(), 2);
    QVERIFY(host.runtime.uninstall(host.installation(), false));
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(host.runtime.installDirectory(source, firstOrigin));
    QTRY_VERIFY(!host.runtime.loading());
    auto *restored = host.runtime.executeCommand(host.installation(), "read");
    QVERIFY(restored);
    QTRY_VERIFY(restored->finished());
    QCOMPARE(restored->result().toInt(), 42);
    QVERIFY(host.runtime.clearPluginData(host.installation()));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    const auto retained = host.runtime.dataEntries().last().toMap();
    QVERIFY(host.runtime.clearPluginData(retained.value("key").toString()));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.dataEntries().size(), 1);
    // An ordinary plugin called "runtime" must never own the shared image cache.
    const auto guardSource =
        pluginPackage(host.temporary.path(), "cache.guard", "export function activate(c){}");
    QVERIFY(host.runtime.installDirectory(guardSource));
    QTRY_VERIFY(!host.runtime.loading());
    const auto guardImage =
        host.runtime.context(host.runtime.installationForId("cache.guard"))->package().root;
    const auto runtimeSource =
        pluginPackage(host.temporary.path(), "runtime", "export function activate(c){}");
    QVERIFY(host.runtime.installDirectory(runtimeSource));
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(QDir().mkpath(PluginPaths(host.runtime.directory()).cache("runtime")));
    QVERIFY(host.runtime.uninstall(host.runtime.installationForId("runtime"), true));
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(QFileInfo(guardImage + "/entry.mjs").isFile());
    QCOMPARE(host.runtime.state(host.runtime.installationForId("cache.guard")),
             PluginRuntime::Active);
}

void PluginRuntimeTest::codeAndDataJournalRecovery()
{
    QTemporaryDir temporary;
    const auto root = temporary.path() + "/plugins";
    const PluginPaths paths(root);
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto source = package(temporary.path(), "export function activate(c){}");
    auto first = PluginPackage::importDirectory(source, paths.images(), token);
    QVERIFY(first.error.isEmpty());
    auto initial = PluginInstallTransaction::begin(root, first, {});
    QVERIFY2(initial.second.isEmpty(), qPrintable(initial.second));
    const QVariantList catalog{QVariantMap{{"id", first.id},
                                           {"installation", token},
                                           {"enabled", true},
                                           {"origin", QVariantMap{{"kind", "local"}}},
                                           {"transaction", QFileInfo(initial.first).fileName()}}};
    QVERIFY(PluginInstallationStore(root).save(catalog).isEmpty());
    QVERIFY(PluginInstallTransaction::recover(root).isEmpty());
    write(paths.data(first.id) + "/test.txt", "original");
    write(source + "/entry.mjs", "export function activate(c){ throw Error('new'); }");
    const auto second = PluginPackage::importDirectory(source, paths.images(), token);
    const auto update = PluginInstallTransaction::begin(root, second, first);
    QVERIFY(update.second.isEmpty());
    write(paths.data(first.id) + "/test.txt", "uncommitted");
    QVERIFY(PluginInstallTransaction::recover(root).isEmpty());
    QFile code(paths.code(first.id) + "/entry.mjs"), data(paths.data(first.id) + "/test.txt");
    QVERIFY(code.open(QIODevice::ReadOnly));
    QVERIFY(data.open(QIODevice::ReadOnly));
    QVERIFY(!code.readAll().contains("throw"));
    QCOMPARE(data.readAll(), "original");
    code.close();
    data.close();
    const auto removal = PluginInstallTransaction::remove(root, first.id, token, true);
    QVERIFY(removal.second.isEmpty());
    QVERIFY(!QDir(paths.code(first.id)).exists());
    QVERIFY(PluginInstallTransaction::recover(root).isEmpty());
    QVERIFY(QDir(paths.code(first.id)).exists());
    const auto committedRemoval = PluginInstallTransaction::remove(root, first.id, token, true);
    QVERIFY(committedRemoval.second.isEmpty());
    QVERIFY(PluginInstallationStore(root).detach(catalog.first().toMap(), false).isEmpty());
    QVERIFY(PluginInstallTransaction::recover(root).isEmpty());
    QVERIFY(!QDir(paths.code(first.id)).exists());
    QVERIFY(!QDir(paths.data(first.id)).exists());
}

void PluginRuntimeTest::dataManagementProductionDialogAndRetirement()
{
    Host host;
    const auto source =
        package(host.temporary.path(), "export function activate(c){}", {"lifecycle", "storage"});
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_VERIFY(!host.runtime.loading());
    auto *writeValue = host.runtime.context(host.installation())->io()->writeValue("erase", 42);
    QTRY_VERIFY(writeValue->finished());
    const auto installOther = [&](const QString &id) {
        const auto other = package(host.temporary.path() + '/' + id,
                                   "export function activate(c){}", {"lifecycle", "storage"});
        QFile file(other + "/manifest.json");
        if (!file.open(QIODevice::ReadOnly))
            return false;
        auto manifest = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        manifest["id"] = id;
        manifest["name"] = id;
        write(other + "/manifest.json", QJsonDocument(manifest).toJson());
        return host.runtime.installDirectory(other);
    };
    QVERIFY(installOther("test.other"));
    QTRY_VERIFY(!host.runtime.loading());
    const auto otherInstallation = host.runtime.installationForId("test.other");
    QVERIFY(!otherInstallation.isEmpty());
    auto *otherValue = host.runtime.context(otherInstallation)->io()->writeValue("keep", 17);
    QTRY_VERIFY(otherValue->finished());
    QVERIFY(installOther("test.retained"));
    QTRY_VERIFY(!host.runtime.loading());
    auto *retainedValue = host.runtime.context(host.runtime.installationForId("test.retained"))
                              ->io()
                              ->writeValue("keep", 99);
    QTRY_VERIFY(retainedValue->finished());
    QVERIFY(host.runtime.uninstall(host.runtime.installationForId("test.retained"), false));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.dataEntries().size(), 3);
    QQmlComponent component(&host.engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Plugins
ApplicationWindow {
 id: window
 required property PluginRuntime runtime
 width: 900; height: 700; visible: true
 EchoPopupLoader {
  objectName: "dataDialogOwner"
  hostItem: window.contentItem
  sourceComponent: PluginDataDialog { runtime: window.runtime }
 }
})QML",
                      QUrl("qrc:/plugin-data-window.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> scene(
        component.createWithInitialProperties({{"runtime", QVariant::fromValue(&host.runtime)}}));
    QVERIFY2(scene, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(scene.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *owner = scene->findChild<QObject *>("dataDialogOwner");
    QVERIFY(owner);
    QVERIFY(!scene->findChild<QObject *>("pluginDataDialog"));
    QStringList warnings;
    const auto connection = connect(&host.engine, &QQmlEngine::warnings, scene.get(),
                                    [&](const QList<QQmlError> &errors) {
                                        for (const auto &error : errors)
                                            warnings.append(error.toString());
                                    });
    const auto disconnect = qScopeGuard([connection] { QObject::disconnect(connection); });
    const QVariant selection = QVariantMap{{"installationId", host.installation()}};
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, selection)));
    QTRY_VERIFY(scene->findChild<QObject *>("pluginDataDialog"));
    auto *dialog = scene->findChild<QObject *>("pluginDataDialog");
    QPointer<QObject> lifetime(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTRY_COMPARE(dialog->property("opacity").toReal(), 1.);
    auto entries = [&] { return dialog->property("entries").toList(); };
    QTRY_COMPARE(entries().size(), 1);
    QCOMPARE(entries().first().toMap().value("key").toString(), host.installation());
    QVERIFY(!visual(window->contentItem(), "pluginClearData-test.other"));
    QVERIFY(!visual(window->contentItem(), "pluginClearData-test.retained"));
    // Switching scope dismisses a pending destructive action from the old card.
    QTRY_VERIFY(visual(window->contentItem(), "pluginClearData-test.runtime"));
    QVERIFY(QMetaObject::invokeMethod(visual(window->contentItem(), "pluginClearData-test.runtime"),
                                      "clicked"));
    QTRY_VERIFY(visual(window->contentItem(), "pluginConfirmClearData"));
    QVERIFY(dialog->setProperty("installationId", otherInstallation));
    QVERIFY(dialog->property("pending").toMap().isEmpty());
    QTRY_VERIFY(!visual(window->contentItem(), "pluginConfirmClearData"));
    QTRY_COMPARE(entries().size(), 1);
    QCOMPARE(entries().first().toMap().value("id").toString(), QString("test.other"));
    QTRY_VERIFY(visual(window->contentItem(), "pluginClearData-test.other"));
    QVERIFY(dialog->setProperty("installationId", QString()));
    QTRY_COMPARE(entries().size(), 1);
    QVERIFY(entries().first().toMap().value("retained").toBool());
    QCOMPARE(entries().first().toMap().value("id").toString(), QString("test.retained"));
    QTRY_VERIFY(visual(window->contentItem(), "pluginClearData-test.retained"));
    QVERIFY(dialog->setProperty("installationId", host.installation()));
    QTRY_COMPARE(entries().size(), 1);
    const auto directory = qEnvironmentVariable("ECHO_PLUGIN_DISTRIBUTION_CAPTURE_DIR");
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        host.theme.setMode(mode);
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        window->resize(mode == ThemeStore::Light ? 900 : 901, 701);
        window->requestUpdate();
        QTRY_VERIFY(!frames.isEmpty());
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(window->grabWindow().save(
                directory + (mode == ThemeStore::Light ? "/data-light.png" : "/data-dark.png")));
        }
    }
    QTRY_VERIFY(visual(window->contentItem(), "pluginClearData-test.runtime"));
    QVERIFY(QMetaObject::invokeMethod(visual(window->contentItem(), "pluginClearData-test.runtime"),
                                      "clicked"));
    QTRY_VERIFY(visual(window->contentItem(), "pluginConfirmClearData"));
    QVERIFY(QMetaObject::invokeMethod(visual(window->contentItem(), "pluginConfirmClearData"),
                                      "clicked"));
    QTRY_VERIFY(!host.runtime.loading());
    auto *read = host.runtime.context(host.installation())->io()->readValue("erase");
    QTRY_VERIFY(read->finished());
    QVERIFY(!read->result().isValid());
    auto *otherRead = host.runtime.context(otherInstallation)->io()->readValue("keep");
    QTRY_VERIFY(otherRead->finished());
    QCOMPARE(otherRead->result().toInt(), 17);
    QVERIFY(dialog->setProperty("installationId", QString()));
    QTRY_VERIFY(visual(window->contentItem(), "pluginClearData-test.retained"));
    QVERIFY(QMetaObject::invokeMethod(
        visual(window->contentItem(), "pluginClearData-test.retained"), "clicked"));
    QTRY_VERIFY(visual(window->contentItem(), "pluginConfirmClearData"));
    QVERIFY(QMetaObject::invokeMethod(visual(window->contentItem(), "pluginConfirmClearData"),
                                      "clicked"));
    QTRY_VERIFY(!host.runtime.loading());
    QTRY_COMPARE(entries().size(), 0);
    QVERIFY(QMetaObject::invokeMethod(owner, "close"));
    QTRY_VERIFY(lifetime.isNull());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}
