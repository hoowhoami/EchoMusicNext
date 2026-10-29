#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

void PluginRuntimeTest::dependencyGraphAndVersionFailuresNeverExecuteCode()
{
    Host host;
    const auto entry = "throw new Error('dependency preflight must not execute this module');";
    const auto missing =
        pluginPackage(host.temporary.path(), "test.missing", entry,
                      {{"dependencies", QJsonObject{{"test.absent", ">=1.0.0 <2.0.0"}}}});
    const auto first = pluginPackage(host.temporary.path(), "test.cycle-a", entry,
                                     {{"dependencies", QJsonObject{{"test.cycle-b", "1.0.0"}}}});
    const auto second = pluginPackage(host.temporary.path(), "test.cycle-b", entry,
                                      {{"dependencies", QJsonObject{{"test.cycle-a", "1.0.0"}}}});
    const auto wrong = pluginPackage(host.temporary.path(), "test.wrong", entry,
                                     {{"dependencies", QJsonObject{{"test.missing", ">=2.0.0"}}}});
    const auto conflictA =
        pluginPackage(host.temporary.path(), "test.conflict-a", entry,
                      {{"conflicts", QJsonObject{{"test.conflict-b", ">=1.0.0"}}}});
    const auto conflictB = pluginPackage(host.temporary.path(), "test.conflict-b", entry);
    QSignalSpy errors(&host.runtime, &PluginRuntime::pluginFailed);
    for (const auto &path : {missing, first, second, wrong, conflictA, conflictB})
        host.runtime.importDirectory(path);
    QTRY_VERIFY(!host.runtime.loading());
    for (const auto &id : {"test.missing", "test.cycle-a", "test.cycle-b", "test.wrong",
                           "test.conflict-a", "test.conflict-b"}) {
        const auto installation = host.runtime.installationForId(id);
        QCOMPARE(host.runtime.state(installation), PluginRuntime::Blocked);
        QVERIFY(!host.runtime.context(installation));
        QVERIFY(!host.runtime.record(installation).value("error").toString().isEmpty());
    }
    QCOMPARE(errors.size(), 0);
    const auto missingId = host.runtime.installationForId("test.missing");
    QVERIFY(host.runtime.setEnabled(missingId, false));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.state(missingId), PluginRuntime::Disabled);
    QVERIFY(!host.runtime.record(missingId).value("dependencyBlocked").toBool());
    QVERIFY(host.runtime.setEnabled(missingId, true));
    QTRY_COMPARE(host.runtime.state(missingId), PluginRuntime::Blocked);
    QVERIFY(PluginPackage::versionMatches(">=1.0.0 <2.0.0", "1.2.0"));
    QVERIFY(!PluginPackage::versionMatches("^1.0.0", "1.2.0"));
}

void PluginRuntimeTest::metadataCommitFailureRetiresNewCodeAndRetainsRecoverySnapshot()
{
    Host host;
    const auto source = package(host.temporary.path(), "export function activate(c) {}");
    QSignalSpy finished(&host.runtime, &PluginRuntime::managementFinished);
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(finished.count(), 1);
    QFile manifestFile(source + "/manifest.json");
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest["version"] = "2.0.0";
    write(source + "/manifest.json", QJsonDocument(manifest).toJson());
    write(source + "/entry.mjs",
          "var done; export function activate(c) { return new Promise(function(resolve) { done "
          "= resolve; }); } export function complete() { done(); }");
    QVERIFY(host.runtime.installDirectory(source));
    const auto installation = host.installation();
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Activating);
    const auto file = PluginPaths(host.runtime.directory()).state("catalog.json");
    QVERIFY(QFile::rename(file, file + ".saved"));
    QVERIFY(QDir().mkpath(file));
    QString installedEntry;
    QDir versions(PluginPaths(host.runtime.directory()).images() + "/test.runtime");
    for (const auto &version : versions.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(versions.filePath(version + "/manifest.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        if (QJsonDocument::fromJson(file.readAll()).object().value("version") == "2.0.0")
            installedEntry = versions.filePath(version + "/entry.mjs");
    }
    QVERIFY(!installedEntry.isEmpty());
    auto module = host.engine.importModule(installedEntry);
    QVERIFY2(!module.isError(), qPrintable(module.toString()));
    QVERIFY(!module.property("complete").call().isError());
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(!finished.last()[1].toBool());
    QCOMPARE(host.runtime.record(installation).value("version").toString(), QString("1.0.0"));
    QVERIFY(host.runtime.state(installation) != PluginRuntime::Active);
    QVERIFY(!QDir(PluginPaths(host.runtime.directory()).transactions("test.runtime"))
                 .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
                 .isEmpty());
    QVERIFY(QDir(PluginPaths(host.runtime.directory()).transactions("test.runtime")).exists());
    QVERIFY(QDir(file).removeRecursively());
    QVERIFY(QFile::rename(file + ".saved", file));
    host.settings.sync();
}

void PluginRuntimeTest::backupBarrierDrainsScopesWithoutChangingEnabledPreference()
{
    Host host;
    const auto source = package(host.temporary.path(), R"JS(
import { awaitOperation } from 'echo:plugin-sdk';
export function activate(ctx) {
  return awaitOperation(ctx.io.writeValue('backup-value',{value:42}));
}
)JS",
                                {"storage"});
    host.runtime.importDirectory(source);
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    QTRY_VERIFY(!host.runtime.loading());
    const auto installation = host.installation();
    bool ready = false;
    QVERIFY(host.runtime.beginMaintenance([&](bool result) { ready = result; }));
    QTRY_VERIFY(ready);
    QVERIFY(!host.runtime.context(installation));
    QCOMPARE(host.runtime.enabledCount(), 1);
    QVERIFY(!host.runtime.setEnabled(installation, false));
    QVERIFY(!host.runtime.reload(installation));
    const auto entries = host.runtime.backupEntries();
    QCOMPARE(entries.size(), 1);
    QVERIFY(!entries.first().toMap().value("origin").toMap().contains("directory"));
    const auto captured = host.temporary.path() + "/data-snapshot";
    const auto result = StorageSnapshot::capture(
        PluginPaths(host.runtime.directory()).data("test.runtime"), captured);
    QVERIFY2(result.isEmpty(), qPrintable(result));
    QVERIFY(QFile::exists(captured + "/values.sqlite"));
    const auto connection = QStringLiteral("backup-barrier-snapshot");
    auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(captured + "/values.sqlite");
        database.setConnectOptions("QSQLITE_OPEN_READONLY");
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec("SELECT json FROM plugin_values WHERE key='backup-value'"));
        QVERIFY(query.next());
        QCOMPARE(QJsonDocument::fromJson(query.value(0).toByteArray())
                     .object()
                     .value("value")
                     .toObject()
                     .value("value")
                     .toInt(),
                 42);
    }
    host.runtime.endMaintenance();
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QCOMPARE(host.runtime.enabledCount(), 1);
}

void PluginRuntimeTest::realActivationSdkAndRetirement()
{
    Host host;
    QAbstractItemModelTester manager(&host.runtime,
                                     QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto source =
        package(host.temporary.path(), R"JS(
import { awaitOperation } from 'echo:plugin-sdk';
export function activate(ctx) {
  const oldPage = ctx.ui.registerPage('tools','Tools.qml');
  ctx.ui.registerPage('tools','Tools.qml');
  oldPage.dispose();
  const old = ctx.ui.registerTitlebarAction('action','Old','settings',()=>ctx.player.setVolume(90));
  ctx.ui.registerTitlebarAction('action','Plugin volume','settings',()=>ctx.player.setVolume(30));
  old.dispose();
  ctx.registerCommand('read',()=>awaitOperation(ctx.io.readValue('answer')));
  ctx.registerCommand('slow',()=>awaitOperation(ctx.io.writeValue('slow',123)));
  ctx.setTimer('volume',60000,true,()=>ctx.player.setVolume(80));
  ctx.onPlayerChanged('observe',()=>{});
  ctx.ui.createTask('task','Plugin task').setProgress(25,'Loading');
  ctx.onDispose(()=>{ throw new Error('cleanup failure must not skip others'); });
  return awaitOperation(ctx.io.writeValue('answer',{value:42}))
    .then(() => awaitOperation(ctx.player.setVolume(60)));
}
export function deactivate(ctx) {
  if (ctx.active) throw new Error('scope still active during deactivate');
}
)JS",
                {"storage", "ui.pages", "ui.titlebar", "tasks", "playback.controls", "commands"});
    write(source + "/Tools.qml", R"QML(import QtQuick
import EchoMusic.PluginSdk 1.0
EchoPluginPage { property string caption: plugin.player.title }
)QML");
    host.runtime.importDirectory(source);
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    auto *context = host.runtime.context(host.installation());
    QVERIFY(context);
    QCOMPARE(host.playback.volume(), 60);
    QVERIFY(host.router.available("test.runtime:page:tools"));
    QCOMPARE(host.shell.tasks()->count(), 1);
    auto *read = host.runtime.executeCommand(host.installation(), "read");
    QVERIFY(read);
    QTRY_COMPARE(read->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(read->result().toMap().value("value").toInt(), 42);
    QVERIFY(host.shell.titlebarActions()->activate("test.runtime:titlebar:action"));
    QTRY_COMPARE(host.playback.volume(), 30);
    QVERIFY(context->ui()->navigate("tools")->state() == EchoPluginOperation::Succeeded);
    QCOMPARE(host.router.current()->name(), "test.runtime:page:tools");
    QQmlComponent component(&host.engine, host.router.current()->component());
    QTRY_VERIFY(component.status() != QQmlComponent::Loading);
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const auto props = host.router.current()->initialProperties();
    auto initial = props;
    initial.insert("context", QVariant::fromValue(host.router.current()));
    QScopedPointer<QObject> page(component.createWithInitialProperties(initial));
    QVERIFY2(page, qPrintable(component.errorString()));
    QVERIFY(!props.contains("router") && !props.contains("settings"));
    auto *oldSession = context->player()->setVolume(99);
    host.api.advanceSession();
    QCOMPARE(oldSession->state(), EchoPluginOperation::Canceled);
    auto *pending = context->player()->setVolume(95);
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(!host.shell.titlebarActions()->activate("test.runtime:titlebar:action"));
    QVERIFY(!host.router.available("test.runtime:page:tools"));
    QVERIFY(!host.router.current() || host.router.current()->name() != "test.runtime:page:tools");
    page.reset();
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    QCOMPARE(host.playback.volume(), 30);
    QVERIFY(!host.runtime.context(host.installation()));
    QCOMPARE(host.shell.tasks()->count(), 0);
    QVERIFY(host.runtime.setEnabled(host.installation(), true));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    QVERIFY(host.runtime.context(host.installation())->activationGeneration() != "1");
}

void PluginRuntimeTest::failureRollsBackAndPendingActivationCancels()
{
    Host host;
    const auto source = package(host.temporary.path(), R"JS(
export function activate(ctx) {
 ctx.ui.registerTitlebarAction('broken','Never published','settings',()=>{});
 ctx.ui.createTask('broken','Never published');
 ctx.onDispose(()=>{ throw 'cleanup'; });
 throw 'activation error';
}
)JS");
    QSignalSpy failures(&host.runtime, &PluginRuntime::pluginFailed);
    host.runtime.importDirectory(source);
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.rowCount(), 0);
    QVERIFY(!failures.isEmpty());
    QVERIFY(failures.last()[1].toString().contains("activation error"));
    QVERIFY(!host.shell.titlebarActions()->activate("test.runtime:titlebar:broken"));
    QCOMPARE(host.shell.tasks()->count(), 0);
    QVERIFY(!host.runtime.safeMode());
    QVERIFY(PluginInstallationStore(host.runtime.directory()).records().isEmpty());
    QVERIFY(!QDir(PluginPaths(host.runtime.directory()).code("test.runtime")).exists());
    QVERIFY(!QDir(PluginPaths(host.runtime.directory()).data("test.runtime")).exists());
}

void PluginRuntimeTest::applicationQuitDrainsPluginIoFixture()
{
    const auto mode = qEnvironmentVariable("ECHO_PLUGIN_QUIT_PROBE");
    if (mode.isEmpty())
        QSKIP("Subprocess fixture; exercised by applicationQuitDrainsPluginIo");
    Host host;
    PluginRuntime runtime(
        host.engine, host.settings,
        {host.playback, host.theme, host.shell, host.router, host.account, host.api},
        host.temporary.path() + "/exit-plugins");
    runtime.start({package(host.temporary.path(),
                           "import {awaitOperation} from 'echo:plugin-sdk'; export function "
                           "activate(c) { return awaitOperation(c.io.writeValue('ready',1)); }",
                           {"storage"})});
    QTRY_COMPARE(runtime.rowCount(), 1);
    const auto installation =
        runtime.data(runtime.index(0), PluginRuntime::InstallationRole).toString();
    QTRY_COMPARE(runtime.state(installation), PluginRuntime::Active);
    QPointer<EchoPluginContext> context = runtime.context(installation);
    QVERIFY(context);
    const auto connection = QStringLiteral("quit-drain-values");
    auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(PluginPaths(runtime.directory()).data("test.runtime") +
                                 "/values.sqlite");
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec("BEGIN EXCLUSIVE"));
        for (int i = 0; i < 32; ++i)
            context->io()->writeValue(QString("quit-%1").arg(i), i);
        int requests = 0;
        bool unlocked = false, drained = false;
        ApplicationShutdown shutdown(*qApp, [&] {
            ++requests;
            runtime.shutdown();
        });
        connect(&runtime, &PluginRuntime::closed, &shutdown, [&] {
            drained = true;
            shutdown.complete();
        });
        // A real locked SQLite worker needs GUI event processing to unlock.
        // Blocking the GUI to join instead of draining cannot pass this case.
        QTimer::singleShot(150, &shutdown, [&] { unlocked = query.exec("COMMIT"); });
        QTimer::singleShot(0, &shutdown, [&] {
            if (mode.startsWith("exit"))
                QCoreApplication::exit(7);
            else if (mode == "posted")
                QCoreApplication::postEvent(qApp, new QEvent(QEvent::Quit));
            else
                QCoreApplication::quit();
            if (mode == "repeated") {
                QCoreApplication::quit();
                QTimer::singleShot(1, &shutdown, &QCoreApplication::quit);
            }
            if (mode == "exit-repeated")
                QTimer::singleShot(1, &shutdown, [] { QCoreApplication::exit(9); });
        });
        const int result = QCoreApplication::exec();
        QCOMPARE(result, mode.startsWith("exit") ? 7 : 0);
        QCOMPARE(requests, 1);
        QVERIFY(unlocked && drained);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(context.isNull());
        QVERIFY(query.prepare("SELECT json FROM plugin_values WHERE key=?"));
        for (int i = 0; i < 32; ++i) {
            query.bindValue(0, QString("quit-%1").arg(i));
            QVERIFY(query.exec());
            QVERIFY(query.next());
            QCOMPARE(QJsonDocument::fromJson(query.value(0).toByteArray())
                         .object()
                         .value("value")
                         .toInt(),
                     i);
        }
        qInfo("Plugin quit probe: drained and preserved all 32 accepted SQL writes");
    }
}

void PluginRuntimeTest::applicationQuitDrainsPluginIo_data()
{
    QTest::addColumn<QString>("mode");
    for (const auto &mode : {"quit", "posted", "exit", "repeated", "exit-repeated"})
        QTest::newRow(mode) << QString(mode);
}

void PluginRuntimeTest::applicationQuitDrainsPluginIo()
{
    QFETCH(QString, mode);
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    environment.insert("ECHO_PLUGIN_QUIT_PROBE", mode);
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {"applicationQuitDrainsPluginIoFixture"});
    QVERIFY(child.waitForStarted());
    const bool finished = child.waitForFinished(15000);
    if (!finished) {
        child.kill();
        child.waitForFinished(5000);
    }
    const auto output = child.readAll();
    QVERIFY2(finished, output.constData());
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QVERIFY2(child.exitCode() == 0, output.constData());
    QVERIFY(output.contains("drained and preserved all 32 accepted SQL writes"));
    QVERIFY(!output.contains("Destroyed while thread"));
}

void PluginRuntimeTest::ioDestructionJoinsAcceptedStorageWrites()
{
    Host host;
    host.runtime.importDirectory(
        package(host.temporary.path(), "export function activate(c) {}", {"storage"}));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    const auto directory = host.temporary.path() + "/unwinding-values";
    {
        EchoPluginIo io(*host.runtime.context(host.installation()), directory);
        for (int i = 0; i < 32; ++i)
            io.writeValue(QString("unwind-%1").arg(i), i);
    }
    const auto connection = QStringLiteral("unwound-values");
    auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(connection); });
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(directory + "/values.sqlite");
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec("SELECT COUNT(*) FROM plugin_values"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 32);
    }
}

void PluginRuntimeTest::interruptedProcessFixture()
{
    const auto root = qEnvironmentVariable("ECHO_PLUGIN_PROCESS_ROOT");
    if (root.isEmpty())
        QSKIP("Subprocess fixture; exercised by killedProcessRecovery");
    const auto phase = qEnvironmentVariable("ECHO_PLUGIN_PROCESS_PHASE");
    Host host;
    EchoMusic::SettingsStore preferences(root + "/settings.ini");
    PluginRuntime runtime(
        host.engine, preferences,
        {host.playback, host.theme, host.shell, host.router, host.account, host.api},
        root + "/plugins");
    runtime.start({root + "/source"});
    QTRY_COMPARE(runtime.rowCount(), 1);
    const auto installation =
        runtime.data(runtime.index(0), PluginRuntime::InstallationRole).toString();
    QTRY_COMPARE(runtime.state(installation),
                 phase == "activating" ? PluginRuntime::Activating : PluginRuntime::Active);
    if (phase == "deactivating" || phase == "normal") {
        QSignalSpy closed(&runtime, &PluginRuntime::closed);
        runtime.shutdown();
        if (phase == "normal") {
            QTRY_COMPARE(closed.count(), 1);
            write(root + "/ready", "closed");
            return;
        }
        QTRY_COMPARE(runtime.state(installation), PluginRuntime::Deactivating);
    }
    write(root + "/ready", installation.toUtf8());
    // Parent uses QProcess::kill, so destructors and shutdown cannot clean
    // installation data on the test's behalf.
    QEventLoop loop;
    loop.exec();
}

void PluginRuntimeTest::killedProcessRecovery_data()
{
    QTest::addColumn<QString>("phase");
    QTest::addColumn<bool>("manualSafe");
    QTest::newRow("active-kill-silent") << QString("active") << false;
    QTest::newRow("loading-kill-silent") << QString("activating") << false;
    QTest::newRow("shutdown-kill-silent") << QString("deactivating") << false;
    QTest::newRow("normal-exit-clean") << QString("normal") << false;
    QTest::newRow("explicit-safe-mode-retained") << QString("active") << true;
}

void PluginRuntimeTest::killedProcessRecovery()
{
    QFETCH(QString, phase);
    QFETCH(bool, manualSafe);
    QTemporaryDir directory;
    const auto root = directory.path();
    const auto source =
        package(root,
                phase == "activating"
                    ? "export function activate(c) { return new Promise(()=>{}); }"
                    : "import {awaitOperation} from 'echo:plugin-sdk'; export function "
                      "activate(c) { return awaitOperation(c.io.writeValue('kept',41)); }",
                {"lifecycle", "storage"});
    if (phase == "deactivating") {
        QFile entry(source + "/entry.mjs");
        QVERIFY(entry.open(QIODevice::Append));
        entry.write("\nexport function deactivate(c) { return new Promise(()=>{}); }\n");
    }
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    environment.insert("ECHO_PLUGIN_PROCESS_ROOT", root);
    environment.insert("ECHO_PLUGIN_PROCESS_PHASE", phase);
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {"interruptedProcessFixture"});
    QVERIFY(child.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(root + "/ready") || child.state() == QProcess::NotRunning, 10000);
    QVERIFY2(QFileInfo::exists(root + "/ready"), child.readAll().constData());
    if (phase == "normal") {
        QVERIFY(child.waitForFinished(10000) || child.state() == QProcess::NotRunning);
        QCOMPARE(child.exitCode(), 0);
    } else {
        child.kill();
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);
    }
    EchoMusic::SettingsStore preferences(root + "/settings.ini");
    const auto catalog = PluginInstallationStore(root + "/plugins").records();
    QCOMPARE(catalog.size(), phase == "activating" ? 0 : 1);
    const auto installation =
        catalog.isEmpty() ? QString() : catalog.first().toMap().value("installation").toString();
    if (manualSafe)
        preferences.setValue("plugins/safeMode", true);
    Host host;
    {
        PluginRuntime runtime(
            host.engine, preferences,
            {host.playback, host.theme, host.shell, host.router, host.account, host.api},
            root + "/plugins");
        QCOMPARE(runtime.safeMode(), manualSafe);
        QSignalSpy failures(&runtime, &PluginRuntime::pluginFailed);
        runtime.start();
        if (phase == "activating") {
            QTRY_VERIFY(!runtime.loading());
            QCOMPARE(runtime.rowCount(), 0);
            QVERIFY(!QDir(PluginPaths(root + "/plugins").data("test.runtime")).exists());
        } else
            QTRY_COMPARE(runtime.state(installation),
                         manualSafe ? PluginRuntime::Disabled : PluginRuntime::Active);
        if (!installation.isEmpty())
            QVERIFY(runtime.record(installation).value("enabled").toBool());
        QVERIFY(runtime.record(installation).value("error").toString().isEmpty());
        QVERIFY(runtime.record(installation).value("failure").toMap().isEmpty());
        QCOMPARE(failures.count(), 0);
        if (phase != "activating" && !manualSafe) {
            auto *operation = runtime.context(installation)->io()->readValue("kept");
            const auto jsRoot = host.engine.newQObject(operation);
            QTRY_VERIFY(operation->finished());
            QCOMPARE(operation->result().toInt(), 41);
        }
        QSignalSpy closed(&runtime, &PluginRuntime::closed);
        runtime.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    }
}

void PluginRuntimeTest::promiseCanceledBeforeCommit()
{
    Host host;
    const auto source = package(host.temporary.path(), R"JS(
export function activate(ctx) {
 ctx.ui.registerTitlebarAction('pending','Never committed','settings',()=>{});
 return new Promise(()=>{});
}
)JS");
    host.runtime.importDirectory(source);
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Activating);
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.rowCount(), 0);
    QVERIFY(!host.shell.titlebarActions()->activate("test.runtime:titlebar:pending"));
}

void PluginRuntimeTest::rejectedPromiseAndLifecycleTimeout()
{
    Host host;
    QSignalSpy failures(&host.runtime, &PluginRuntime::pluginFailed);
    const auto source =
        package(host.temporary.path(),
                "export function activate(ctx) { return Promise.reject('rejected'); }\n");
    host.runtime.importDirectory(source);
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.rowCount(), 0);
    QVERIFY(!failures.isEmpty());
    QVERIFY(failures.last()[1].toString().contains("rejected"));
    write(source + "/entry.mjs", "export function activate(ctx) { return new Promise(()=>{}); }\n");
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Activating);
    QTRY_VERIFY_WITH_TIMEOUT(!host.runtime.loading(), 7000);
    QCOMPARE(host.runtime.rowCount(), 0);
    QVERIFY(failures.last()[1].toString().contains("timed out"));
}

void PluginRuntimeTest::persistedInstallAndSafeMode()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.path() + "/settings.ini");
    ApiClient api(DeviceIdentity::temporary());
    AccountService account(api);
    PlaybackService playback(api, settings, nullptr, directory.path() + "/playback.sqlite");
    ThemeStore theme(settings);
    RouteController router;
    ShellController shell(settings, router);
    QQmlEngine engine;
    const auto root = directory.path() + "/plugins";
    const auto source = package(root, "export function activate(ctx) {}\n");
    QString installation;
    {
        PluginRuntime runtime(engine, settings, {playback, theme, shell, router, account, api},
                              root);
        runtime.start({source});
        QTRY_COMPARE(runtime.rowCount(), 1);
        installation = runtime.data(runtime.index(0), PluginRuntime::InstallationRole).toString();
        QTRY_COMPARE(runtime.state(installation), PluginRuntime::Active);
        QSignalSpy closed(&runtime, &PluginRuntime::closed);
        runtime.shutdown();
        QTRY_COMPARE(closed.count(), 1);
    }
    {
        PluginRuntime runtime(engine, settings, {playback, theme, shell, router, account, api},
                              root);
        runtime.start();
        QTRY_VERIFY(!runtime.loading());
        QCOMPARE(runtime.rowCount(), 1);
        QCOMPARE(runtime.record(installation).value("origin").toMap().value("directory").toString(),
                 PluginPaths(root).code("test.runtime"));
        QCOMPARE(runtime.context(installation)->activationGeneration(), QString("1"));
        QSignalSpy closed(&runtime, &PluginRuntime::closed);
        runtime.shutdown();
        QTRY_COMPARE(closed.count(), 1);
    }
    QDir(source).removeRecursively();
    {
        PluginRuntime runtime(engine, settings, {playback, theme, shell, router, account, api},
                              root);
        runtime.start();
        QTRY_COMPARE(runtime.state(installation), PluginRuntime::Active);
        QSignalSpy closed(&runtime, &PluginRuntime::closed);
        runtime.shutdown();
        QTRY_COMPARE(closed.count(), 1);
    }
    settings.setValue("plugins/safeMode", true);
    PluginRuntime safe(engine, settings, {playback, theme, shell, router, account, api}, root);
    QVERIFY(safe.safeMode());
    safe.start();
    QTRY_COMPARE(safe.state(installation), PluginRuntime::Disabled);
    QVERIFY(!safe.context(installation));
    QVERIFY(safe.data(safe.index(0), PluginRuntime::EnabledRole).toBool());
    QSignalSpy closed(&safe, &PluginRuntime::closed);
    safe.shutdown();
    QTRY_COMPARE(closed.count(), 1);
    playback.shutdown();
    api.shutdown();
}
