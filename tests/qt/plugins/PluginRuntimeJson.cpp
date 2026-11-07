#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginUiParts.h"
#include "EchoUiPartRegistration.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginDialog.h"
#include <QUuid>

namespace
{
const QByteArray arrayCheck = R"JS(
function check(value, expected) {
    if (Array.isArray(expected)) {
        return Array.isArray(value) && Object.getPrototypeOf(value) === Array.prototype &&
            value.length === expected.length &&
            value.map(function(v, i) { return check(v, expected[i]); }).every(Boolean);
    }
    if (expected !== null && typeof expected === 'object') {
        return value !== null && typeof value === 'object' &&
            Object.keys(value).length === Object.keys(expected).length &&
            Object.keys(expected).every(function(key) {
                return Object.prototype.hasOwnProperty.call(value, key) &&
                    check(value[key], expected[key]);
            });
    }
    return value === expected;
}
)JS";

void seedValue(const QString &root, const QString &id, const QByteArray &json)
{
    const auto path = PluginPaths(root).data(id);
    if (!QDir().mkpath(path))
        qFatal("Cannot create fixture data directory");
    const auto connection = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path + "/values.sqlite");
        if (!db.open())
            qFatal("Cannot open fixture values database");
        QSqlQuery query(db);
        if (!query.exec("CREATE TABLE IF NOT EXISTS plugin_values "
                        "(key TEXT PRIMARY KEY NOT NULL, json BLOB NOT NULL)") ||
            !query.exec("PRAGMA user_version=1"))
            qFatal("Cannot initialize fixture values database");
        query.prepare("INSERT OR REPLACE INTO plugin_values(key,json) VALUES('payload',?)");
        query.addBindValue(json);
        if (!query.exec())
            qFatal("Cannot seed fixture value");
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}
} // namespace

void PluginRuntimeTest::operationJsonArraysAcrossIoCommandsAndQml_data()
{
    QTest::addColumn<QByteArray>("expression");
    QTest::newRow("nested") << QByteArray("({folders:[{id:'a',children:[]}],songs:[['x',1]]})");
    QTest::newRow("top-level") << QByteArray("[{id:'a',children:[1,2]},null,false,0,'']");
    QTest::newRow("empty") << QByteArray("[]");
    QTest::newRow("null") << QByteArray("null");
    QTest::newRow("primitive") << QByteArray("false");
    QTest::newRow("large") << QByteArray(
        "Array.from({length:500},function(_,i){return [i,{tags:[String(i)]}];})");
    QTest::newRow("special-keys") << QByteArray(
        "JSON.parse('{\"__proto__\":{\"folders\":[]},\"constructor\":[1]}')");
}

void PluginRuntimeTest::operationJsonArraysAcrossIoCommandsAndQml()
{
    QFETCH(QByteArray, expression);
    Host host;
    const auto entry = "import {awaitOperation} from 'echo:plugin-sdk';\n" + arrayCheck +
                       "export function activate(c) { const value = " + expression + R"JS(;
        c.registerCommand('echo', function(args) {
            if (!check(args.payload,value)) throw new Error('Command argument array shape changed');
            return args.payload;
        });
        c.registerCommand('read', function() { return awaitOperation(c.io.readValue('payload')); });
        return awaitOperation(c.io.writeValue('payload',value)).then(function() {
            return awaitOperation(c.io.readValue('payload'));
        }).then(function(read) {
            if (!check(read,value)) throw new Error('Storage array shape changed');
        });
    })JS";
    const auto source = pluginPackage(host.temporary.path(), "test.json", entry);
    host.runtime.importDirectory(source);
    QTRY_VERIFY(!host.runtime.loading());
    const auto installation = host.runtime.installationForId("test.json");
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QVERIFY(scope);
    const auto expected = host.engine.evaluate("JSON.stringify(" + expression + ")").toString();

    QQmlComponent component(&host.engine);
    component.setData(R"QML(import QtQml
import EchoMusic.PluginSdk
QtObject {
    required property EchoPluginContext plugin
    required property string expectedJson
    required property string command
    property bool done: false
    property bool valid: false
    property string failure: ""
)QML" + arrayCheck + R"QML(
    Component.onCompleted: {
        const op = plugin.executeCommand(command, {payload:JSON.parse(expectedJson)})
        function finish() {
            if (op.state < 2) return
            op.completed.disconnect(finish)
            failure = op.errorCode + ":" + op.errorMessage
            valid = op.state === 2 && check(op.result, JSON.parse(expectedJson))
            done = true
        }
        op.completed.connect(finish)
        finish()
    }
})QML",
                      QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    for (const auto &command : {QString("echo"), QString("read")}) {
        std::unique_ptr<QObject> page(
            component.createWithInitialProperties({{"plugin", QVariant::fromValue(scope)},
                                                   {"expectedJson", expected},
                                                   {"command", command}}));
        QVERIFY2(page, qPrintable(component.errorString()));
        QTRY_VERIFY(page->property("done").toBool());
        QVERIFY2(page->property("valid").toBool(),
                 qPrintable(page->property("failure").toString()));
    }
}

void PluginRuntimeTest::operationNativeHandlesAndTerminalResults()
{
    Host host;
    const auto source =
        pluginPackage(host.temporary.path(), "test.handles", "export function activate(c){}");
    host.runtime.importDirectory(source);
    QTRY_VERIFY(!host.runtime.loading());
    auto *scope = host.runtime.context(host.runtime.installationForId("test.handles"));
    QVERIFY(scope);
    QObject handle;
    handle.setObjectName("native-handle");
    QQmlEngine::setObjectOwnership(&handle, QQmlEngine::CppOwnership);
    auto *op = scope->operation();
    auto wrapper = host.engine.newQObject(op);
    QCOMPARE(wrapper.property("result").isUndefined(), true);
    op->succeed(QVariantMap{
        {"handle", QVariant::fromValue(&handle)},
        {"lists", QVariantList{QStringList{"a", "b"}, QVariantHash{{"empty", QVariantList{}}}}}});
    const auto result = wrapper.property("result");
    QCOMPARE(result.property("handle").toQObject(), &handle);
    QCOMPARE(op->result().toMap().value("handle").value<QObject *>(), &handle);
    QVERIFY(result.strictlyEquals(wrapper.property("result")));
    QVERIFY(result.property("lists").isArray());
    QVERIFY(result.property("lists").property(0).isArray());
    QVERIFY(result.property("lists").property(1).property("empty").isArray());
    QSignalSpy completed(op, &EchoPluginOperation::completed);
    op->cancel();
    op->fail("Ignored", "Already succeeded");
    op->succeed(QVariantList{});
    QCOMPARE(completed.size(), 0);
    QVERIFY(result.strictlyEquals(wrapper.property("result")));
    auto *direct = scope->operation();
    direct->succeed(QVariant::fromValue(&handle));
    QCOMPARE(host.engine.newQObject(direct).property("result").toQObject(), &handle);
    auto *canceled = scope->operation();
    canceled->cancel();
    canceled->succeed(QVariantList{1});
    QVERIFY(host.engine.newQObject(canceled).property("result").isUndefined());
}

void PluginRuntimeTest::activationStorageReadsPersistedValuesAndErrors()
{
    Host host([](const QString &root) {
        seedValue(root, "test.startup", R"({"value":{"folders":[{"id":"persisted"}]}})");
        const auto source =
            pluginPackage(QFileInfo(root).absolutePath() + "/fixture", "test.startup", R"JS(
import {awaitOperation} from 'echo:plugin-sdk';
export function activate(c) {
    return awaitOperation(c.io.readValue('payload')).then(function(v) {
        if (!v || !Array.isArray(v.folders) || v.folders[0].id !== 'persisted')
            throw new Error('Activation did not receive persisted folders');
        c.registerCommand('readError', function() {
            return awaitOperation(c.io.readValue('payload')).then(
                function() { return {unexpected:true}; },
                function(error) { return {code:error.code}; });
        });
        c.registerCommand('probe', function() {
            return awaitOperation(c.io.readValue('missing')).then(function(missing) {
                return {missing:missing === undefined, folders:v.folders};
            });
        });
    });
})JS");
        if (!QDir().rename(source, root + "/test.startup"))
            qFatal("Cannot install startup fixture");
    });
    const auto installation = host.runtime.installationForId("test.startup");
    for (int activation = 0; activation < 5; ++activation) {
        QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
        auto *probe = host.runtime.executeCommand(installation, "probe");
        QVERIFY(probe);
        QTRY_VERIFY(probe->finished());
        QCOMPARE(probe->state(), EchoPluginOperation::Succeeded);
        QCOMPARE(probe->result().toMap().value("missing").toBool(), true);
        QCOMPARE(probe->result()
                     .toMap()
                     .value("folders")
                     .toList()
                     .first()
                     .toMap()
                     .value("id")
                     .toString(),
                 "persisted");
        if (activation < 4) {
            QVERIFY(host.runtime.setEnabled(installation, false));
            QTRY_VERIFY(!host.runtime.context(installation));
            QVERIFY(host.runtime.setEnabled(installation, true));
        }
    }
    // Corrupt storage is a failed operation, distinct from an absent key.
    seedValue(host.runtime.directory(), "test.startup", "invalid-json");
    auto *scope = host.runtime.context(installation);
    auto *read = scope->io()->readValue("payload");
    QTRY_VERIFY(read->finished());
    QCOMPARE(read->state(), EchoPluginOperation::Failed);
    QCOMPARE(read->errorCode(), "StorageError");
    QVERIFY(!read->errorMessage().isEmpty());
    auto *caught = host.runtime.executeCommand(installation, "readError");
    QTRY_VERIFY(caught->finished());
    QCOMPARE(caught->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(caught->result().toMap().value("code").toString(), "StorageError");

    // Submit the first ledger read immediately after construction, before the
    // GUI has received initialization's loading=false notification.
    QTemporaryDir ledger;
    const auto selected = ledger.path() + "/selected";
    QVERIFY(QDir().mkpath(selected));
    const QJsonObject grant{{"installationId", "ledger-installation"},
                            {"pluginId", "test.ledger"},
                            {"pluginName", "Ledger fixture"},
                            {"id", "persisted-grant"},
                            {"path", selected},
                            {"kind", "directory"},
                            {"access", "read"},
                            {"revision", "1"},
                            {"revoked", false}};
    write(PluginPaths(ledger.path()).state("grants.json"),
          QJsonDocument(QJsonArray{grant}).toJson());
    PluginFileService files(ledger.path());
    QVERIFY(files.loading());
    QSignalSpy done(&files, &PluginFileService::completed);
    const PluginFileOwner who{"ledger-installation", "test.ledger", "Ledger fixture", "first",
                              std::make_shared<std::atomic_bool>(false)};
    const auto request = files.request(who, "listGrants", {});
    QVERIFY(request);
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(done.first()[0].toULongLong(), request);
    QVERIFY(done.first()[1].toBool());
    const auto grants = done.first()[2].toList();
    QCOMPARE(grants.size(), 1);
    QCOMPARE(grants.first().toMap().value("id").toString(), "persisted-grant");
    files.shutdown();
    QTRY_VERIFY(files.isClosed());
}

void PluginRuntimeTest::privateCommandNamesStayWithinQmlPluginScopes()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    for (const auto &id : {QByteArray("test.first"), QByteArray("test.second")}) {
        const auto source = pluginPackage(
            host.temporary.path(), QString::fromUtf8(id),
            "export function activate(c) { c.ui.registerSettings('Panel.qml'); "
            "c.registerCommand('settings.get',function(){return {owner:'" +
                id + "',folders:[]};}); " +
                (id == "test.first" ? "c.registerCommand('only-first',function(){return true;});"
                                    : "") +
                " }");
        write(source + "/Panel.qml", "import QtQuick; import EchoMusic.PluginSdk; Item {required "
                                     "property EchoPluginContext plugin}");
        host.runtime.importDirectory(source);
        QTRY_VERIFY(!host.runtime.loading());
    }
    QQmlComponent component(&host.engine);
    component.setData(R"QML(import QtQml
import EchoMusic.PluginSdk
QtObject {
    required property EchoPluginContext plugin
    property string owner: ""
    Component.onCompleted: {
        const op = plugin.executeCommand('settings.get')
        function finish() {
            if (op.state < 2) return
            op.completed.disconnect(finish)
            owner = op.state === 2 ? op.result.owner : op.errorCode
        }
        op.completed.connect(finish)
        finish()
    }
})QML",
                      QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    for (const auto &id : {QString("test.first"), QString("test.second")}) {
        auto *scope = host.runtime.context(host.runtime.installationForId(id));
        QVERIFY(scope);
        QCOMPARE(manager.requestSettings(host.runtime.installationForId(id)), scope);
        if (id == "test.second")
            QCOMPARE(scope->executeCommand("only-first")->errorCode(), "Unavailable");
        QObject owner;
        auto *presentation = scope->createPresentation(&owner, "test.routing");
        QVERIFY(presentation);
        for (auto *context : {scope, presentation}) {
            std::unique_ptr<QObject> page(
                component.createWithInitialProperties({{"plugin", QVariant::fromValue(context)}}));
            QVERIFY(page);
            QTRY_COMPARE(page->property("owner").toString(), id);
            QCOMPARE(context->executeCommand("unregistered")->errorCode(), "Unavailable");
        }
    }
}

void PluginRuntimeTest::privateCommandRoutingThroughSettingsAndDialogs()
{
    Host host;
    for (const auto &id : {QByteArray("test.first"), QByteArray("test.second")}) {
        const auto source =
            pluginPackage(host.temporary.path(), QString::fromUtf8(id),
                          "export function activate(c) { c.ui.registerSettings('Settings.qml'); "
                          "c.registerCommand('settings.get',function(){return {owner:'" +
                              id + "'};}); }");
        write(source + "/Settings.qml", R"QML(import QtQuick
import EchoMusic.PluginSdk
Item {
    id: panel
    required property EchoPluginContext plugin
    property EchoPluginDialog dialog: null
    objectName: "commandProbe-" + plugin.id
    implicitHeight: 80
    property string responder: ""
    property int replies: 0
    function query() {
        const op = panel.plugin.executeCommand('settings.get')
        function finish() {
            if (op.state < 2) return
            op.completed.disconnect(finish)
            panel.responder = op.state === 2 ? op.result.owner : op.errorCode
            panel.replies++
        }
        op.completed.connect(finish)
        finish()
    }
    Component.onCompleted: query()
})QML");
        host.runtime.importDirectory(source);
        QTRY_VERIFY(!host.runtime.loading());
    }
    auto window = ecosystemWindow(host, R"QML(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.PluginSdk
import EchoMusic.Plugins
import EchoMusic.Controls
ApplicationWindow {
    id: win
    required property PluginRuntime runtime
    property EchoPluginContext scope: null
    property string installation: ""
    width: 800; height: 600; visible: true
    function openSettings() { settingsOwner.open() }
    function closeSettings() { settingsOwner.close() }
    EchoPopupLoader {
        id: settingsOwner
        hostItem: win.contentItem
        sourceComponent: Component {
            PluginSettingsDialog {
                runtime: win.runtime
                scope: win.scope
                installationId: win.installation
                onClosed: win.scope = null
            }
        }
    }
})QML",
                                  {{"runtime", QVariant::fromValue(&host.runtime)}});
    auto *mainWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(mainWindow);
    host.mainWindow = mainWindow;
    const auto first = host.runtime.installationForId("test.first");
    const auto second = host.runtime.installationForId("test.second");
    auto *scopeA = host.runtime.context(first);
    auto *scopeB = host.runtime.context(second);
    QVERIFY(scopeA && scopeB);
    window->setProperty("scope", QVariant::fromValue(scopeA));
    window->setProperty("installation", first);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "openSettings"));
    QTRY_VERIFY(visual(mainWindow->contentItem(), "commandProbe-test.first"));
    auto *panelA = visual(mainWindow->contentItem(), "commandProbe-test.first");
    QTRY_COMPARE(panelA->property("responder").toString(), "test.first");
    auto *settingsPart = scopeA->ui()->parts()->queryUnique(
        {{"key", "commandProbe-test.first"}, {"owner", "test.first"}, {"surface", "settings"}});
    QVERIFY(settingsPart && settingsPart->registration()->target() == panelA);
    const auto keepSettingsPart = host.engine.newQObject(settingsPart);
    // EchoPopupLoader reuses its current instance if it is opened again before
    // retirement. The body must follow the newly selected plugin as well.
    window->setProperty("scope", QVariant::fromValue(scopeB));
    window->setProperty("installation", second);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "openSettings"));
    QTRY_VERIFY_WITH_TIMEOUT(visual(mainWindow->contentItem(), "commandProbe-test.second"), 2000);
    auto *panelB = visual(mainWindow->contentItem(), "commandProbe-test.second");
    QVERIFY(!settingsPart->valid());
    auto *nextSettingsPart = scopeB->ui()->parts()->queryUnique(
        {{"key", "commandProbe-test.second"}, {"owner", "test.second"}, {"surface", "settings"}});
    QVERIFY(nextSettingsPart && nextSettingsPart->registration()->target() == panelB);
    QTRY_COMPARE(panelB->property("responder").toString(), "test.second");
    auto *settings = window->findChild<QObject *>("pluginSettingsDialog");
    QVERIFY(settings);
    QTRY_VERIFY(settings->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(window.get(), "closeSettings"));
    window->setProperty("scope", QVariant::fromValue(scopeA));
    window->setProperty("installation", first);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "openSettings"));
    QTRY_VERIFY(visual(mainWindow->contentItem(), "commandProbe-test.first"));
    panelA = visual(mainWindow->contentItem(), "commandProbe-test.first");
    QTRY_COMPARE(panelA->property("responder").toString(), "test.first");
    QVERIFY(!settingsPart->valid());
    auto *reopenedPart = scopeA->ui()->parts()->queryUnique(
        {{"key", "commandProbe-test.first"}, {"owner", "test.first"}, {"surface", "settings"}});
    QVERIFY(reopenedPart && reopenedPart->registration()->target() == panelA);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "closeSettings"));
    QTRY_VERIFY(!visual(mainWindow->contentItem(), "commandProbe-test.first"));

    QList<QJSValue> retained;
    QList<QPointer<EchoPluginDialog>> dialogs;
    for (auto *scope : {scopeA, scopeB}) {
        auto *op = scope->ui()->showDialog({{"component", "Settings.qml"}});
        retained.append(host.engine.newQObject(op));
        QTRY_VERIFY(op->finished());
        QCOMPARE(op->state(), EchoPluginOperation::Succeeded);
        auto *dialog = op->result().value<EchoPluginDialog *>();
        QVERIFY(dialog);
        dialogs.append(dialog);
        auto *panel = dialog->findChild<QQuickItem *>("commandProbe-" + scope->id());
        QVERIFY(panel);
        QTRY_COMPARE(panel->property("responder").toString(), scope->id());
    }
    for (qsizetype i = 0; i < dialogs.size(); ++i) {
        const auto id = i == 0 ? QString("test.first") : QString("test.second");
        auto *panel = dialogs[i]->findChild<QQuickItem *>("commandProbe-" + id);
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "query"));
        QTRY_COMPARE(panel->property("replies").toInt(), 2);
        QCOMPARE(panel->property("responder").toString(), id);
        dialogs[i]->close();
    }
}
