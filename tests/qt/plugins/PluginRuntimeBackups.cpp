#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "SettingsDataService.h"
#include "SettingsBackupArchive.h"
#include "RelaunchService.h"
#include "WindowController.h"
#include "EchoPluginBackups.h"
#include <QBuffer>
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
    static void armShutdownRestore(SettingsDataService &data, const QString &token)
    {
        const auto item = data.m_inspections.take(token);
        data.m_staging = item.staging;
        data.m_manifest = item.manifest;
        data.m_restore = true;
        data.m_restoreSettings = true;
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
    auto *create = c->backups()->create();
    const auto createRoot = f.host.engine.newQObject(create);
    QCOMPARE(create->state(), EchoPluginOperation::Running);
    QCOMPARE(c->io()->writeValue("whileSnapshot", true)->errorCode(), "Busy");
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
ApplicationWindow {id:win;required property SettingsDataService dataService;required property ShellSettings settings;required property WindowController windows;width:850;height:700;visible:true; DataSettingsSection {anchors.fill:parent;settings:win.settings;windowService:win.windows;dataService:win.dataService;restoreProvider:')" +
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
    for (const bool fail : {false, true}) {
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
        write(f.host.temporary.path() + "/theme-images/" + fileName, image);
        auto result = wait(f.host, c->backups()->create());
        QVERIFY2(result.state == EchoPluginOperation::Succeeded, qPrintable(result.error));
        result =
            wait(f.host, c->backups()->inspect(result.value.toMap().value("data").toByteArray()));
        QCOMPARE(result.state, EchoPluginOperation::Succeeded);
        const auto token = result.value.toMap().value("token").toString();
        QCOMPARE(wait(f.host, db->run("UPDATE t SET v=2")).state, EchoPluginOperation::Succeeded);
        f.host.settings.setValue("interface/uiScale", 0.9);
        const auto databasePath = c->dataPath() + "/databases/main.sqlite";
        SettingsBackupTestAccess::armShutdownRestore(f.data, token);
        QSignalSpy closed(&f.host.runtime, &PluginRuntime::closed);
        f.host.runtime.shutdown();
        QTRY_COMPARE(closed.size(), 1);
        if (fail) {
            QVERIFY(QDir(f.host.temporary.path() + "/theme-images").removeRecursively());
            write(f.host.temporary.path() + "/theme-images", "block file publication");
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
        QCOMPARE(f.host.settings.value("interface/uiScale").toDouble(), fail ? 0.9 : 1.25);
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
