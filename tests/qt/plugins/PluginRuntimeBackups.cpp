#include "StorageFence.h"
#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "SettingsDataService.h"
#include "SettingsBackupArchive.h"
#include "RelaunchService.h"
#include "WindowController.h"
#include "EchoPluginBackups.h"
#include <QBuffer>

namespace
{
struct DavStore : QTcpServer
{
    QMap<QByteArray, QByteArray> archives;
    QSet<QByteArray> directories{QByteArray("/dav/")};
    QList<QPointer<QTcpSocket>> held;
    bool hold = false, authenticated = true;
    DavStore()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            auto bytes = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                bytes->append(socket->readAll());
                const auto end = bytes->indexOf("\r\n\r\n");
                if (end < 0 || socket->property("handled").toBool())
                    return;
                const auto headers = bytes->left(end);
                const auto length = QRegularExpression("(?im)^content-length: *([0-9]+)")
                                        .match(QString::fromLatin1(headers))
                                        .captured(1)
                                        .toLongLong();
                if (bytes->size() < end + 4 + length)
                    return;
                socket->setProperty("handled", true);
                authenticated &=
                    headers.toLower().contains("authorization: basic zml4dhvyztpzzwnyzxq=");
                if (hold) {
                    held.append(socket);
                    return;
                }
                const auto first = bytes->split('\n').first().split(' ');
                const auto method = first.value(0), path = first.value(1);
                QByteArray status = "200 OK", body;
                if (method == "PROPFIND") {
                    if (!directories.contains(path))
                        status = "404 Not Found";
                    else {
                        status = "207 Multi-Status";
                        body = "<d:multistatus xmlns:d=\"DAV:\">";
                        if (headers.toLower().contains("depth: 1"))
                            for (auto it = archives.begin(); it != archives.end(); ++it)
                                body +=
                                    "<d:response><d:href>" + it.key() +
                                    "</d:href><d:propstat><d:prop><d:getcontentlength>" +
                                    QByteArray::number(it.value().size()) +
                                    "</d:getcontentlength><d:getlastmodified>Wed, 11 Nov 2026 "
                                    "10:00:00 GMT</d:getlastmodified></d:prop><d:status>HTTP/1.1 "
                                    "200 OK</d:status></d:propstat></d:response>";
                        body += "</d:multistatus>";
                    }
                } else if (method == "MKCOL") {
                    directories.insert(path);
                    status = "201 Created";
                } else if (method == "PUT") {
                    archives[path] = bytes->mid(end + 4);
                    status = "201 Created";
                } else if (method == "GET") {
                    body = archives.value(path);
                    if (!archives.contains(path))
                        status = "404 Not Found";
                } else if (method == "DELETE") {
                    archives.remove(path);
                    status = "204 No Content";
                }
                socket->write("HTTP/1.1 " + status +
                              "\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
};
} // namespace
namespace EchoMusic
{
class SettingsBackupTestAccess
{
public:
    static void expire(SettingsDataService &data, const QString &token)
    {
        data.m_inspections[token].expires = 0;
    }
    static int tokens(SettingsDataService &data)
    {
        return data.m_inspections.size();
    }
    static void armShutdownRestore(SettingsDataService &data, const QString &token,
                                   bool settings = true)
    {
        const auto item = data.m_inspections.take(token);
        data.m_staging = item.staging;
        data.m_manifest = item.manifest;
        data.m_restore = true;
        data.m_restoreSettings = settings;
        data.m_restorePlugins = true;
    }
};
} // namespace EchoMusic
namespace
{
struct Fixture
{
    Host host;
    NetworkPreferences network{host.settings, host.api};
    RelaunchService relaunch{host.temporary.path() + "/instance.lock", {}, false};
    SettingsDataService data{host.settings, host.runtime,          relaunch,
                             network,       host.temporary.path(), "0.1.0"};
    Fixture()
    {
        host.runtime.setBackupHost(&data);
        host.engine.globalObject().setProperty("backupProbe", host.engine.newObject());
    }
};
struct Result
{
    EchoPluginOperation::State state;
    QVariant value;
    QString code, error;
};
Result wait(Host &host, EchoPluginOperation *operation)
{
    if (!operation)
        return {EchoPluginOperation::Failed, {}, "Unavailable", "Null operation"};
    const auto root = host.engine.newQObject(operation);
    if (!QTest::qWaitFor([operation] { return operation->finished(); }, 15000))
        return {operation->state(), {}, "Timeout", "Operation did not finish"};
    return {operation->state(), operation->result(), operation->errorCode(),
            operation->errorMessage()};
}
EchoPluginContext *scope(Fixture &fixture, const QString &id = "backup",
                         const QByteArray &body = "export function activate(c){}")
{
    const auto path =
        pluginPackage(fixture.host.temporary.path(), id, body,
                      {{"capabilities", QJsonArray{"backups", "sqlite", "unrestrictedNetwork"}}});
    fixture.host.runtime.importDirectory(path);
    if (!QTest::qWaitFor([&] {
            return fixture.host.runtime.state(fixture.host.runtime.installationForId(id)) ==
                       PluginRuntime::Active &&
                   !fixture.host.runtime.loading();
        }))
        return nullptr;
    return fixture.host.runtime.context(fixture.host.runtime.installationForId(id));
}
EchoPluginDatabase *database(Fixture &fixture, EchoPluginContext &scope)
{
    const auto result = wait(fixture.host, scope.sqlite()->open());
    return result.value.value<EchoPluginDatabase *>();
}
struct Store : QTcpServer
{
    QByteArray archive;
    bool hold = false, fail = false;
    int saves = 0, loads = 0, removed = 0;
    QList<QPointer<QTcpSocket>> held;
    explicit Store(QObject *parent = nullptr) : QTcpServer(parent)
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                auto request = socket->property("bytes").toByteArray() + socket->readAll();
                socket->setProperty("bytes", request);
                const auto end = request.indexOf("\r\n\r\n");
                if (end < 0)
                    return;
                const auto size = QRegularExpression("(?im)^content-length: *([0-9]+)")
                                      .match(QString::fromLatin1(request.left(end)))
                                      .captured(1)
                                      .toInt();
                if (request.size() < end + 4 + size || socket->property("handled").toBool())
                    return;
                socket->setProperty("handled", true);
                if (hold) {
                    held.append(socket);
                    return;
                }
                QByteArray body;
                if (request.startsWith("PUT")) {
                    archive = request.mid(end + 4, size);
                    ++saves;
                    body = "{}";
                } else if (request.startsWith("DELETE")) {
                    archive.clear();
                    ++removed;
                    body = "{}";
                } else if (request.startsWith("GET /bytes")) {
                    body = archive;
                    ++loads;
                } else {
                    QJsonArray rows;
                    if (!archive.isEmpty())
                        for (int i = 0; i < 500; ++i)
                            rows.append(QJsonObject{{"id", QString::number(i)},
                                                    {"name", QString("备份 %1").arg(i)},
                                                    {"size", archive.size()},
                                                    {"description", "可用于恢复的完整归档"}});
                    body = QJsonDocument(rows).toJson(QJsonDocument::Compact);
                }
                socket->write(
                    QByteArray(fail ? "HTTP/1.1 500 Failure\r\n" : "HTTP/1.1 200 OK\r\n") +
                    "Content-Length: " + QByteArray::number(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
};
} // namespace
void PluginRuntimeTest::backupsCreateInspectTokensAndStorageBarrier()
{
    Fixture f;
    auto *c = scope(f);
    QVERIFY(c);
    auto *db = database(f, *c);
    QVERIFY(db);
    QCOMPARE(wait(f.host, db->exec("CREATE TABLE t(v); INSERT INTO t VALUES(1)")).state,
             EchoPluginOperation::Succeeded);
    auto presentationOwner = std::make_unique<QObject>();
    QPointer<EchoPluginContext> presentation =
        c->createPresentation(presentationOwner.get(), "backup-retiring");
    QVERIFY(presentation);
    auto *accepted = presentation->io()->writeValue("beforeSnapshot", true);
    const auto acceptedRoot = f.host.engine.newQObject(accepted);
    presentationOwner.reset();
    const auto generation = c->activationGeneration();
    auto acceptedFileWrite = StorageFence::write(f.host.settings.portableFilesRoot());
    QVERIFY(acceptedFileWrite);
    auto *create = c->backups()->create();
    const auto createRoot = f.host.engine.newQObject(create);
    QCOMPARE(create->state(), EchoPluginOperation::Running);
    QCOMPARE(c->io()->writeValue("whileSnapshot", true)->errorCode(), "Busy");
    QVERIFY(!StorageFence::write(f.host.settings.portableFilesRoot()));
    QCoreApplication::processEvents();
    QCOMPARE(create->state(), EchoPluginOperation::Running);
    acceptedFileWrite.reset();
    auto result = wait(f.host, create);
    QVERIFY2(result.state == EchoPluginOperation::Succeeded, qPrintable(result.error));
    QCOMPARE(f.host.runtime.context(c->installationId()), c);
    QCOMPARE(c->activationGeneration(), generation);
    QVERIFY(db->active());
    QVERIFY(!c->storageFrozen());
    QTRY_VERIFY(!presentation);
    QCOMPARE(wait(f.host, db->get("SELECT v FROM t")).value.toMap().value("v").toInt(), 1);
    const auto bytes = result.value.toMap().value("data").toByteArray();
    QVERIFY(bytes.startsWith("ECHONEXTBACKUP"));
    f.host.engine.globalObject().setProperty("backupContext", f.host.engine.newQObject(c));
    f.host.engine.globalObject().setProperty("backupData", f.host.engine.toScriptValue(bytes));
    const auto jsInspect = js(f.host, "backupContext.backups.inspect(backupData)");
    result = wait(f.host, qobject_cast<EchoPluginOperation *>(jsInspect.toQObject()));
    QVERIFY2(result.state == EchoPluginOperation::Succeeded, qPrintable(result.error));
    const auto token = result.value.toMap().value("token").toString();
    QVERIFY(!token.isEmpty());
    QCOMPARE(result.value.toMap().value("summary").toMap().value("pluginCount").toInt(), 1);
    auto *other = scope(f, "other");
    QVERIFY(other);
    QCOMPARE(wait(f.host, other->backups()->restore(token)).code, "InvalidToken");
    auto *restore = c->backups()->restore(token);
    const auto restoreRoot = f.host.engine.newQObject(restore);
    QVERIFY(!f.data.confirmation().isEmpty());
    auto window = ecosystemWindow(f.host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Settings
ApplicationWindow {id:win;required property SettingsDataService service;width:700;height:650;visible:true;SettingsBackupConfirmation {id:confirm;service:win.service;Component.onCompleted:open()}})",
                                  {{"service", QVariant::fromValue(&f.data)}});
    QVERIFY(window);
    QTRY_VERIFY(visual(qobject_cast<QQuickWindow *>(window.get())->contentItem(),
                       "pluginBackupConfirmRestore"));
    f.data.answerConfirmation(false);
    QCOMPARE(restore->state(), EchoPluginOperation::Canceled);
    QVERIFY(f.data.confirmation().isEmpty());
    window.reset();
    SettingsBackupTestAccess::expire(f.data, token);
    QCOMPARE(wait(f.host, c->backups()->restore(token)).code, "InvalidToken");
    auto corrupt = bytes;
    corrupt[corrupt.size() / 2] ^= 1;
    QCOMPARE(wait(f.host, c->backups()->inspect(corrupt)).state, EchoPluginOperation::Failed);
    auto *canceled = c->backups()->create();
    const auto cancelRoot = f.host.engine.newQObject(canceled);
    canceled->cancel();
    QTRY_VERIFY(!f.data.busy());
    QVERIFY(!c->storageFrozen());
    result = wait(f.host, c->backups()->inspect(bytes));
    QCOMPARE(result.state, EchoPluginOperation::Succeeded);
    QVERIFY(SettingsBackupTestAccess::tokens(f.data) > 0);
    QVERIFY(f.host.runtime.setEnabled(c->installationId(), false));
    QTRY_COMPARE(SettingsBackupTestAccess::tokens(f.data), 0);
}
void PluginRuntimeTest::backupsProvidersUseRealUiAndRetire()
{
    Fixture f;
    Store server;
    QVERIFY(server.isListening());
    const auto base = "http://127.0.0.1:" + QByteArray::number(server.serverPort());
    const auto body =
        QByteArray(
            R"(import {awaitOperation as wait} from 'echo:plugin-sdk'; export function activate(c){
var base=')") +
        base +
        R"('; backupProbe.providerLease=c.backups.registerProvider('cloud',{name:'本机云存储',description:'备份存储提供者',
save:function(r){backupProbe.providerRequest=r;return wait(r.context.net.request({url:base+'/bytes',method:'PUT',body:r.data}));},
list:function(r){backupProbe.providerRequest=r;return wait(r.context.net.request({url:base+'/list',responseType:'json'})).then(function(v){return v.data;});},
load:function(r){backupProbe.providerRequest=r;return wait(r.context.net.request({url:base+'/bytes',responseType:'arrayBuffer'})).then(function(v){return v.data;});},
remove:function(r){return wait(r.context.net.request({url:base+'/bytes',method:'DELETE'}));}});})";
    auto *c = scope(f, "provider", body);
    QVERIFY(c);
    QCOMPARE(f.data.providers().size(), 1);
    const auto key = f.data.providers().first().toMap().value("key").toString();
    f.data.saveToProvider(key, true, true);
    QTRY_VERIFY_WITH_TIMEOUT(!f.data.providerBusy() && !f.data.busy(), 15000);
    QVERIFY2(f.data.providerError().isEmpty(), qPrintable(f.data.providerError()));
    QCOMPARE(server.saves, 1);
    QVERIFY(server.archive.startsWith("ECHONEXTBACKUP"));
    f.data.refreshProvider(key);
    QTRY_VERIFY(!f.data.providerBusy());
    QVERIFY2(f.data.providerError().isEmpty(), qPrintable(f.data.providerError()));
    QCOMPARE(f.data.providerEntries().size(), 500);
    QTRY_VERIFY(!js(f.host, "backupProbe.providerRequest.active").toBool());
    auto root = ecosystemWindow(
        f.host,
        R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Settings
import EchoMusic.Shell
ApplicationWindow {id:win;required property SettingsDataService dataService;required property ShellSettings settings;required property WindowController windows;width:850;height:700;visible:true; ExperimentalSettingsSection {objectName:"backupDataSettings";anchors.fill:parent;settings:win.settings;windowService:win.windows;dataService:win.dataService;restoreProvider:')" +
            key.toUtf8() + R"(';}})",
        {{"dataService", QVariant::fromValue(&f.data)},
         {"settings", QVariant::fromValue(new ShellSettings(f.host.settings, &f.host.engine))},
         {"windows",
          QVariant::fromValue(new WindowController(f.host.settings, false, &f.host.engine))}});
    QVERIFY(root);
    auto *win = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(win);
    auto *restoreButton = visual(win->contentItem(), "settingsRestoreBackup");
    QVERIFY(restoreButton);
    QVERIFY(QMetaObject::invokeMethod(restoreButton, "clicked"));
    QTRY_VERIFY(visual(win->contentItem(), "backupProviderEntries"));
    auto *list = visual(win->contentItem(), "backupProviderEntries");
    QCOMPARE(list->property("count").toInt(), 500);
    auto *items = list->property("contentItem").value<QQuickItem *>();
    QVERIFY(items);
    QVERIFY(items->childItems().size() < 20);
    const auto uiOutput = qEnvironmentVariable("ECHO_BACKUP_UI_OUTPUT");
    if (!uiOutput.isEmpty()) {
        QDir().mkpath(uiOutput);
        auto *dialog = root->findChild<QObject *>("settingsBackupSourceDialog");
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(dialog->property("opacity").toDouble(), 1., 5000);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            f.host.theme.setMode(mode);
            for (const auto scale : {1.0, 1.25}) {
                QVERIFY(dialog->setProperty("uiScale", scale));
                QTest::qWait(350);
                QVERIFY(win->grabWindow().save(
                    uiOutput + QString("/provider-%1-%2.png").arg(mode).arg(scale)));
            }
        }
    }

    auto *section = visual(win->contentItem(), "backupDataSettings");
    QVERIFY(section);
    auto *entry = list->property("currentItem").value<QQuickItem *>();
    QVERIFY(entry);
    QVERIFY(QMetaObject::invokeMethod(entry, "clicked"));
    QTRY_COMPARE(section->property("selectedBackupId").toString(), QString("0"));
    auto *readButton = visual(win->contentItem(), "backupReadSelected");
    QVERIFY(readButton && readButton->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(readButton, "clicked"));
    QTRY_VERIFY(!f.data.busy() && !f.data.providerBusy());
    QCOMPARE(server.loads, 1);
    QVERIFY(f.data.canRestore());
    QTRY_VERIFY(root->findChild<QObject *>("settingsBackupRestoreDialog"));
    QVERIFY(
        root->findChild<QObject *>("settingsBackupRestoreDialog")->property("visible").toBool());
    f.data.releaseInspection();
    QTest::qWait(220);
    QVERIFY(section->setProperty("exportProvider", key));
    auto *createButton = visual(win->contentItem(), "settingsCreateBackup");
    QVERIFY(createButton);
    QVERIFY(QMetaObject::invokeMethod(createButton, "clicked"));
    QTRY_VERIFY(visual(win->contentItem(), "backupCreateToStorage"));
    QVERIFY(
        QMetaObject::invokeMethod(visual(win->contentItem(), "backupCreateToStorage"), "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.data.busy() && !f.data.providerBusy(), 15000);
    QCOMPARE(server.saves, 2);
    QTest::qWait(220);
    f.data.removeFromProvider(key, "0");
    QTRY_VERIFY(!f.data.providerBusy());
    QCOMPARE(server.removed, 1);
    QVERIFY(server.archive.isEmpty());
    server.hold = true;
    f.data.refreshProvider(key);
    QTRY_VERIFY(!server.held.isEmpty());
    const auto request = js(f.host, "backupProbe.providerRequest");
    auto *context = qobject_cast<EchoPluginBackupRequest *>(request.toQObject());
    QVERIFY(context && context->active());
    QVERIFY(f.host.runtime.setEnabled(c->installationId(), false));
    QTRY_VERIFY(f.data.providers().isEmpty());
    QTRY_VERIFY(!context->active());
    QVERIFY(context->canceled());
    QTRY_VERIFY(!f.data.providerBusy());
    QTRY_VERIFY(!server.held.first() ||
                server.held.first()->state() == QAbstractSocket::UnconnectedState);
    QVERIFY(f.data.providerEntries().isEmpty());
    root.reset();
}
void PluginRuntimeTest::backupsRestoreCommitAndRollback()
{
    for (const int scenario : {0, 1, 2}) {
        const bool fail = scenario == 1, settings = scenario != 2;
        Fixture f;
        auto *c = scope(f);
        QVERIFY(c);
        auto *db = database(f, *c);
        QVERIFY(db);
        QCOMPARE(wait(f.host, db->exec("CREATE TABLE t(v);INSERT INTO t VALUES(1)")).state,
                 EchoPluginOperation::Succeeded);
        f.host.settings.setValue("interface/uiScale", 1.25);
        const QByteArray image("managed theme asset");
        const auto fileName =
            QString::fromLatin1(
                QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex()) +
            ".png";
        write(f.host.settings.files("theme-images", SettingsDomain::Portable) + '/' + fileName,
              image);
        auto result =
            wait(f.host, c->backups()->create({{"settings", settings}, {"plugins", true}}));
        QVERIFY2(result.state == EchoPluginOperation::Succeeded, qPrintable(result.error));
        result =
            wait(f.host, c->backups()->inspect(result.value.toMap().value("data").toByteArray()));
        QCOMPARE(result.state, EchoPluginOperation::Succeeded);
        const auto token = result.value.toMap().value("token").toString();
        QCOMPARE(wait(f.host, db->run("UPDATE t SET v=2")).state, EchoPluginOperation::Succeeded);
        f.host.settings.setValue("interface/uiScale", 0.9);
        const auto databasePath = c->dataPath() + "/databases/main.sqlite";
        SettingsBackupTestAccess::armShutdownRestore(f.data, token, settings);
        QSignalSpy closed(&f.host.runtime, &PluginRuntime::closed);
        f.host.runtime.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        if (fail) {
            QVERIFY(QDir(f.host.settings.portableFilesRoot()).removeRecursively());
            write(f.host.settings.portableFilesRoot(), "block file publication");
        }
        f.data.shutdown();
        bool complete = false;
        QString error;
        f.data.finishShutdown([&](QString value) {
            error = value;
            complete = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(complete, 15000);
        QVERIFY2(error.isEmpty() == !fail, qPrintable(error));
        QCOMPARE(f.host.settings.value("interface/uiScale").toDouble(),
                 fail || !settings ? 0.9 : 1.25);
        const auto connection = "backup-verify";
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
            sql.setDatabaseName(databasePath);
            QVERIFY(sql.open());
            QSqlQuery q(sql);
            QVERIFY(q.exec("SELECT v FROM t"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), fail ? 2 : 1);
            sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(!QFile::exists(f.host.temporary.path() + "/settings-restore.json"));
    }
}

void PluginRuntimeTest::backupsPortableFilesFreezeCancellationAndRetirement()
{
    for (const bool retire : {false, true}) {
        Fixture f;
        auto *context = scope(f);
        QVERIFY(context);
        const auto installation = f.host.installation();
        auto writer = StorageFence::write(f.host.settings.portableFilesRoot());
        QVERIFY(writer);
        QSignalSpy created(&f.data, &SettingsDataService::created);
        QPointer<EchoPluginOperation> operation =
            context->backups()->create({{"settings", true}, {"plugins", false}});
        const auto guard = f.host.engine.newQObject(operation);
        QTRY_VERIFY(f.data.busy());
        QTest::qWait(30);
        QVERIFY(operation && !operation->finished());
        if (retire) {
            QVERIFY(f.host.runtime.setEnabled(installation, false));
            QTRY_COMPARE(f.host.runtime.state(installation), PluginRuntime::Disabled);
        } else {
            operation->cancel();
            QTRY_COMPARE(operation->state(), EchoPluginOperation::Canceled);
            QVERIFY(context->active());
        }
        QTRY_VERIFY(!f.data.busy());
        auto reopened = StorageFence::write(f.host.settings.portableFilesRoot());
        QVERIFY(reopened);
        QCOMPARE(created.count(), 0);
        reopened.reset();
        writer.reset();
        if (!retire)
            QCOMPARE(
                wait(f.host, context->backups()->create({{"settings", true}, {"plugins", false}}))
                    .state,
                EchoPluginOperation::Succeeded);
    }
}

void PluginRuntimeTest::builtWebDavProviderAndSettingsLifecycle()
{
    const auto archive = qEnvironmentVariable("ECHO_WEBDAV_PLUGIN_PACKAGE");
    if (archive.isEmpty())
        QSKIP("Set ECHO_WEBDAV_PLUGIN_PACKAGE to the built webdav-backup package");
    Fixture f;
    DavStore server;
    QVERIFY(server.isListening());
    f.host.runtime.importDirectory(archive);
    QTRY_COMPARE(f.host.runtime.state(f.host.runtime.installationForId("webdav-backup")),
                 PluginRuntime::Active);
    auto *c = f.host.runtime.context(f.host.runtime.installationForId("webdav-backup"));
    QVERIFY(c);
    QCOMPARE(f.data.providers().size(), 0);
    const QVariantMap config{{"enabled", true},
                             {"url", QString("http://127.0.0.1:%1/dav/").arg(server.serverPort())},
                             {"username", "fixture"},
                             {"password", "secret"},
                             {"directory", "EchoMusicNext/test"},
                             {"rememberPassword", false}};
    const auto saved = wait(f.host, c->executeCommand("settings.save", {{"config", config}}));
    QCOMPARE(saved.state, EchoPluginOperation::Succeeded);
    QCOMPARE(f.data.providers().size(), 1);
    const auto stored = wait(f.host, c->io()->readValue("settings"));
    QVERIFY(stored.value.toMap().value("password").toString().isEmpty());
    QVERIFY(!c->ui()->state().contains("password"));
    const auto key = f.data.providers().first().toMap().value("key").toString();
    QByteArray payload(9 * 1024 * 1024, Qt::Uninitialized);
    quint32 random = 0x3f1a8c23;
    for (qsizetype i = 0; i < payload.size(); ++i) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        payload[i] = char(random & 255);
    }
    write(f.host.settings.files("webdav-test", SettingsDomain::Portable) + "/payload.bin", payload);
    f.data.saveToProvider(key, true, true);
    QTRY_VERIFY_WITH_TIMEOUT(!f.data.providerBusy() && !f.data.busy(), 30000);
    QVERIFY2(f.data.providerError().isEmpty(), qPrintable(f.data.providerError()));
    QCOMPARE(server.archives.size(), 1);
    QVERIFY(server.archives.first().startsWith("ECHONEXTBACKUP"));
    QVERIFY(server.archives.first().size() > 8 * 1024 * 1024);
    f.data.refreshProvider(key);
    QTRY_VERIFY(!f.data.providerBusy());
    QVERIFY2(f.data.providerError().isEmpty(), qPrintable(f.data.providerError()));
    QCOMPARE(f.data.providerEntries().size(), 1);
    const auto id = f.data.providerEntries().first().toMap().value("id").toString();
    QSignalSpy inspected(&f.data, &SettingsDataService::inspected);
    f.data.loadFromProvider(key, id);
    QTRY_VERIFY_WITH_TIMEOUT(!f.data.providerBusy() && !f.data.busy(), 30000);
    QVERIFY2(f.data.providerError().isEmpty(), qPrintable(f.data.providerError()));
    QTRY_VERIFY(!inspected.isEmpty());
    QVERIFY(f.data.canRestore());
    f.data.releaseInspection();
    f.data.removeFromProvider(key, id);
    QTRY_VERIFY(!f.data.providerBusy());
    QVERIFY(server.archives.isEmpty());
    QVERIFY(server.authenticated);

    QStringList warnings;
    connect(&f.host.engine, &QQmlEngine::warnings, &f.host.engine,
            [&](const QList<QQmlError> &errors) {
                for (const auto &error : errors)
                    warnings.append(error.toString());
            });
    auto root =
        ecosystemWindow(f.host, R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Plugins
import EchoMusic.PluginSdk
import EchoMusic.Theme
ApplicationWindow {
 id: win
 required property PluginRuntime runtime
 required property EchoPluginContext scope
 required property string installation
 property real uiScale: 1
 width: 900; height: 900; visible: true; color: Theme.main
 function openSettings() { owner.open(); }
 function closeSettings() { owner.close(); }
 EchoPopupLoader {
  id: owner; hostItem: win.contentItem
  sourceComponent: Component {
   PluginSettingsDialog { runtime: win.runtime; scope: win.scope; installationId: win.installation; uiScale: win.uiScale }
  }
 }
 Component.onCompleted: openSettings()
})",
                        {{"runtime", QVariant::fromValue(&f.host.runtime)},
                         {"scope", QVariant::fromValue(c)},
                         {"installation", f.host.runtime.installationForId("webdav-backup")}});
    QVERIFY(root);
    auto *win = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(win);
    QTRY_VERIFY(visual(win->contentItem(), "webdavBackupSettings"));
    auto *settings = visual(win->contentItem(), "webdavBackupSettings");
    QTRY_VERIFY(settings->property("loaded").toBool());
    QCOMPARE(visual(win->contentItem(), "webdavUrl")->property("text").toString(),
             config.value("url").toString());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QTRY_VERIFY(qAbs(visual(win->contentItem(), "webdavUsername")->width() -
                     visual(win->contentItem(), "webdavPassword")->width()) < 1.0);
    const auto output = qEnvironmentVariable("ECHO_PLUGIN_UI_OUTPUT");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        for (double scale : {1.0, 1.25}) {
            root->setProperty("uiScale", scale);
            for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
                f.host.theme.setMode(mode);
                QTest::qWait(100);
                if (scale > 1.0) {
                    auto *save = visual(win->contentItem(), "webdavSave");
                    for (auto *parent = save->parentItem(); parent; parent = parent->parentItem()) {
                        if (!parent->inherits("QQuickFlickable"))
                            continue;
                        parent->setProperty("contentY",
                                            qMax(0.0, parent->property("contentHeight").toDouble() -
                                                          parent->height()));
                        const auto point =
                            save->mapToItem(parent, QPointF(save->width() / 2, save->height() / 2));
                        QVERIFY(point.y() >= 0 && point.y() <= parent->height());
                        QTest::qWait(30);
                        break;
                    }
                }
                QVERIFY(win->grabWindow().save(output +
                                               QString("/webdav-%1-%2.png")
                                                   .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                                   .arg(scale)));
            }
        }
    }
    server.hold = true;
    QVERIFY(QMetaObject::invokeMethod(settings, "test"));
    QTRY_VERIFY(!server.held.isEmpty());
    const auto held = server.held.last();
    QVERIFY(QMetaObject::invokeMethod(root.get(), "closeSettings"));
    QTRY_VERIFY(!held);
    QTRY_VERIFY(!visual(win->contentItem(), "webdavBackupSettings"));
    QCOMPARE(f.data.providers().size(), 1);
    QVERIFY(c->active());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    const auto install = f.host.runtime.installationForId("webdav-backup");
    QVERIFY(f.host.runtime.setEnabled(install, false));
    QTRY_COMPARE(f.host.runtime.state(install), PluginRuntime::Disabled);
    QVERIFY(f.data.providers().isEmpty());
}
