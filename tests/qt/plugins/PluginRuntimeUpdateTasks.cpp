#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

namespace
{
const QVariantMap source{{"id", "github:test/catalog"},
                         {"repository", "test/catalog"},
                         {"enabled", true},
                         {"url", "https://github.com/test/catalog"},
                         {"name", "Test catalog"}};
const QVariantMap origin{
    {"kind", "marketplace"}, {"id", "github:test/catalog"}, {"repository", "test/author"}};
QVariantMap offer(const QString &id, const QString &version)
{
    return {{"key", "github:test/catalog:" + id},
            {"sourceId", "github:test/catalog"},
            {"repository", "test/author"},
            {"id", id},
            {"name", id},
            {"version", version},
            {"compatible", true}};
}
QString token(TaskCenterModel *tasks)
{
    return tasks->data(tasks->index(0), TaskCenterModel::TokenRole).toString();
}
int status(TaskCenterModel *tasks)
{
    return tasks->data(tasks->index(0), TaskCenterModel::StatusRole).toInt();
}
TaskDetailsModel *details(TaskCenterModel *tasks)
{
    return tasks->data(tasks->index(0), TaskCenterModel::DetailsRole).value<TaskDetailsModel *>();
}
// Only private HTTP is used. Catalog/publication tests cover their wire formats;
// these cases exercise real installed packages, task commands and update commits.
void serve(QTcpServer &server, std::function<QPair<int, QByteArray>(QByteArray)> response)
{
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, response] {
        auto *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, response] {
            auto request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n") || socket->property("answered").toBool())
                return;
            socket->setProperty("answered", true);
            const auto [code, body] = response(request);
            socket->write("HTTP/1.1 " + QByteArray::number(code) + " Result\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
}
} // namespace

void PluginRuntimeTest::marketplaceUpdateTasksFilterDismissAndReplace()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    manager.m_sources = {source};
    for (const auto &id : {QString("test.update"), QString("test.incompatible")}) {
        QVERIFY(host.runtime.installDirectory(
            pluginPackage(host.temporary.path(), id, "export function activate(c){}"), origin));
        QTRY_VERIFY(!host.runtime.loading());
    }
    auto current = offer("test.update", "2.0.0");
    auto incompatible = offer("test.incompatible", "2.0.0");
    incompatible["compatible"] = false;
    incompatible["error"] = "Host is too old";
    auto otherPublisher = current;
    otherPublisher["repository"] = "another/author";
    otherPublisher["sourceId"] = "another/source";
    manager.m_entries = {current, incompatible, otherPublisher, offer("not.installed", "2.0.0")};
    manager.attachTasks(*host.shell.tasks());
    auto *tasks = host.shell.tasks();
    QCOMPARE(tasks->count(), 1);
    QCOMPARE(status(tasks), int(TaskCenterModel::Pending));
    QCOMPARE(details(tasks)->rowCount(), 2);
    QCOMPARE(details(tasks)->data(details(tasks)->index(1), TaskDetailsModel::ErrorRole).toString(),
             QString("Host is too old"));
    QVERIFY(details(tasks)
                ->data(details(tasks)->index(1), TaskDetailsModel::ActionsRole)
                .toList()
                .isEmpty());
    const auto previous = token(tasks);
    QVERIFY(tasks->invokeAction("plugin-updates", previous, "later"));
    QCOMPARE(tasks->count(), 0);
    manager.setSearch("unrelated filter");
    QCOMPARE(tasks->count(), 0);
    current["version"] = "3.0.0";
    manager.m_entries[0] = current;
    manager.rebuild();
    QCOMPARE(tasks->count(), 1);
    QCOMPARE(details(tasks)->rowCount(), 1);
    QVERIFY(!tasks->invokeAction("plugin-updates", previous, "update-all"));
    auto disabled = source;
    disabled["enabled"] = false;
    manager.m_sources = {disabled};
    manager.rebuild();
    QCOMPARE(tasks->count(), 0);
    manager.m_sources = {source};
    manager.rebuild();
    QCOMPARE(tasks->count(), 1);
    manager.shutdown();
    QCOMPARE(tasks->count(), 0);
}

void PluginRuntimeTest::marketplaceUpdateTasksRunBatchAndRetry()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    manager.m_sources = {source};
    QFile manifestFile(QFINDTESTDATA("../fixtures/plugins/marketplace/manifest.json"));
    QFile archiveFile(QFINDTESTDATA("../fixtures/plugins/marketplace/archive.echomod"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QVERIFY(archiveFile.open(QIODevice::ReadOnly));
    const auto descriptor = PluginPackage::inspectManifest(manifestFile.readAll());
    const auto archive = archiveFile.readAll();
    QVERIFY(descriptor.error.isEmpty());
    const auto initial = pluginPackage(host.temporary.path(), descriptor.id,
                                       "export function activate(c){}", {{"version", "0.9.0"}});
    QVERIFY(host.runtime.installDirectory(initial, origin));
    QTRY_VERIFY(!host.runtime.loading());
    const auto installation = host.runtime.installationForId(descriptor.id);
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(host.runtime.installDirectory(
        pluginPackage(host.temporary.path(), "test.failed", "export function activate(c){}"),
        origin));
    QTRY_VERIFY(!host.runtime.loading());
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int successfulDownloads = 0, failedDownloads = 0;
    serve(server, [&](const QByteArray &request) -> QPair<int, QByteArray> {
        if (request.startsWith("GET /package ")) {
            ++successfulDownloads;
            return {200, archive};
        }
        if (request.startsWith("GET /failed ")) {
            ++failedDownloads;
            return {503, "offline fixture"};
        }
        return {200, "{}"};
    });
    const auto base = QString("http://127.0.0.1:%1").arg(server.serverPort());
    manager.m_resolveUrl = [base](const QUrl &url) {
        return QUrl(base +
                    (url.path() == "/package" || url.path() == "/failed" ? url.path() : "/stats"));
    };
    auto update = offer(descriptor.id, descriptor.version);
    update["artifactUrl"] = base + "/package";
    update["manifest"] = descriptor.manifest;
    update["sha256"] =
        QString::fromLatin1(QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex());
    auto failed = offer("test.failed", "2.0.0");
    failed["artifactUrl"] = base + "/failed";
    manager.m_entries = {update, failed};
    manager.attachTasks(*host.shell.tasks());
    auto *tasks = host.shell.tasks();
    const auto pendingToken = token(tasks);
    QVERIFY(tasks->invokeAction("plugin-updates", pendingToken, "update-all"));
    QCOMPARE(status(tasks), int(TaskCenterModel::Running));
    QCOMPARE(token(tasks), pendingToken); // Pending starts the same run.
    QVERIFY(!manager.install(update.value("key").toString()));
    QTRY_VERIFY(!manager.busy() && !manager.updatingAll() && !host.runtime.loading());
    QCOMPARE(successfulDownloads, 1);
    QCOMPARE(failedDownloads, 1);
    QCOMPARE(status(tasks), int(TaskCenterModel::Error));
    QCOMPARE(host.runtime.record(installation).value("version").toString(), descriptor.version);
    QVERIFY(!host.runtime.record(installation).value("enabled").toBool());
    QCOMPARE(
        details(tasks)->data(details(tasks)->index(0), TaskDetailsModel::StatusRole).toString(),
        QString("已更新"));
    QVERIFY(!details(tasks)
                 ->data(details(tasks)->index(1), TaskDetailsModel::ErrorRole)
                 .toString()
                 .isEmpty());

    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {
 id: win
 required property TaskCenterModel tasks
 width: 760; height: 720; visible: true
 TaskCenterDialog {service:win.tasks; Component.onCompleted:open()}
})",
                      QUrl("qrc:/plugin-update-task-window.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> scene(
        component.createWithInitialProperties({{"tasks", QVariant::fromValue(tasks)}}));
    QVERIFY2(scene, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(scene.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_VERIFY(visual(window->contentItem(), "taskAction-plugin-updates//retry-failed"));
    QTRY_VERIFY(visual(window->contentItem(), "taskProgress-plugin-updates"));
    QVERIFY(!visual(window->contentItem(), "taskProgress-plugin-updates")->isVisible());
    auto *dialog = scene->findChild<QObject *>("taskCenterDialog");
    QVERIFY(dialog);
    QTRY_COMPARE(dialog->property("opacity").toReal(), 1.);
    const auto capture = qEnvironmentVariable("ECHO_PLUGIN_TASK_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            host.theme.setMode(mode);
            QTest::qWait(350); // Capture settled theme colors, not the transition.
            for (int width : {760, 480}) {
                window->resize(width, 720);
                QTest::qWait(100);
                QVERIFY(window->grabWindow().save(
                    capture + QString("/tasks-%1-%2.png").arg(int(mode)).arg(width)));
            }
        }
    }
    QVERIFY(QMetaObject::invokeMethod(
        visual(window->contentItem(), "taskAction-plugin-updates//retry-failed"), "clicked"));
    QTRY_VERIFY(!manager.busy() && !manager.updatingAll() && !host.runtime.loading());
    QCOMPARE(successfulDownloads, 1); // A failed-only retry cannot reinstall successful items.
    QCOMPARE(failedDownloads, 2);
    QCOMPARE(status(tasks), int(TaskCenterModel::Error));
    QVERIFY(tasks->dismiss("plugin-updates", token(tasks)));
    manager.setSearch("closed result");
    QCOMPARE(tasks->count(), 0);
    manager.m_entries.removeLast();
    manager.rebuild();
    QCOMPARE(status(tasks), int(TaskCenterModel::Completed));
    QTRY_COMPARE_WITH_TIMEOUT(tasks->count(), 0, 6000); // Transient completion expires after 5s.
    manager.setSearch("anything");
    QCOMPARE(tasks->count(), 0);
    QVERIFY(host.runtime.uninstall(installation, false));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(tasks->count(), 0);
    QVERIFY(manager.m_updateJobs.isEmpty());
}

void PluginRuntimeTest::marketplaceStartupCheckDefersRecoversAndStops()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    manager.m_sources = {source};
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    bool fail = true;
    int requests = 0, authorRequests = 0;
    serve(server, [&](const QByteArray &request) -> QPair<int, QByteArray> {
        if (request.startsWith("GET /manifest ")) {
            ++authorRequests;
            return {404, "uninstalled author is not needed for startup"};
        }
        ++requests;
        return fail
                   ? QPair<int, QByteArray>{503, "offline fixture"}
                   : QPair<int, QByteArray>{
                         200,
                         R"({"schemaVersion":2,"plugins":[{"id":"not.installed","repo":"https://github.com/test/unused"}]})"};
    });
    manager.m_resolveUrl = [&](const QUrl &url) {
        return QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()) +
                    (url.path().endsWith("echo-plugins.json") ? "index" : "manifest"));
    };
    manager.startStartupUpdateCheck();
    manager.startStartupUpdateCheck();
    QCOMPARE(manager.m_startupCheck.interval(), 5000);
    manager.m_busy = true;
    manager.m_startupCheck.start(0);
    QTRY_VERIFY(manager.m_startupReady);
    QCOMPARE(requests, 0);
    manager.m_busy = false;
    emit manager.changed();
    QTRY_VERIFY(requests == 1 && !manager.catalogLoading());
    QVERIFY(!manager.m_startupChecked);
    manager.setSearch("no polling");
    QTest::qWait(50);
    QCOMPARE(requests, 1);
    fail = false;
    manager.startupNetworkChanged(); // Same path as the native reachability signal.
    QTRY_VERIFY2(manager.m_startupChecked, qPrintable(manager.sourceError()));
    QCOMPARE(requests, 2);
    manager.startupNetworkChanged();
    QCOMPARE(requests, 2);
    QCOMPARE(authorRequests, 0);
    QVERIFY(!manager.m_catalogComplete);
    QVERIFY(!manager.loaded());
    manager.refreshCatalog(); // The later marketplace visit must load the full catalog.
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(requests, 3);
    QCOMPARE(authorRequests, 1);
    QVERIFY(manager.m_catalogComplete);
    QVERIFY(manager.loaded());
    manager.shutdown();
    manager.startupNetworkChanged();
    QCOMPARE(requests, 3);

    PluginManager closing(host.runtime, host.settings);
    closing.m_sources = {source};
    closing.startStartupUpdateCheck();
    QVERIFY(closing.m_startupCheck.isActive());
    closing.shutdown();
    QVERIFY(!closing.m_startupCheck.isActive());
    closing.startStartupUpdateCheck();
    QVERIFY(!closing.m_startupCheck.isActive());
}
