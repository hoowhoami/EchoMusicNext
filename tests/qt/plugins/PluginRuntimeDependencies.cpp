#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

void PluginRuntimeTest::dependentServicesActivationCancellationAndProviderReload()
{
    Host host;
    const auto provider = pluginPackage(host.temporary.path(), "test.zz-provider", R"JS(
            import {awaitOperation} from 'echo:plugin-sdk';
            export function activate(c) {
                c.ui.registerSettings('Settings.qml');
                c.plugins.registerService('volume','1.0.0',{
                    set:function(args) { return awaitOperation(c.player.setVolume(args.percent)).then(function(){return {percent:c.player.volume};}); },
                    wait:function() { return new Promise(function(){}); }
                });
                return awaitOperation(c.io.writeValue('initialized',true));
            }
        )JS");
    write(provider + "/Settings.qml", R"QML(
            import QtQuick
            import EchoMusic.PluginSdk 1.0
            Item { required property EchoPluginContext plugin }
        )QML");
    const auto consumer =
        pluginPackage(host.temporary.path(), "test.aa-consumer", R"JS(
            import * as sdk from 'echo:plugin-sdk';
            export function activate(c) {
                if (typeof sdk.modService !== 'undefined' || typeof c.mods !== 'undefined') throw new Error('Obsolete Mod API must not be exported');
                if (!c.plugins.isAvailable('test.zz-provider')) throw new Error('Provider must be committed first');
                var service=sdk.pluginService(c,'test.zz-provider','volume','>=1.0.0 <2.0.0');
                c.registerCommand('set',function(args){return service.call('set',args);});
                c.registerCommand('wait',function(){return service.call('wait',{});});
            }
        )JS",
                      {{"dependencies", QJsonObject{{"test.zz-provider", ">=1.0.0 <2.0.0"}}}});
    const auto anotherConsumer = pluginPackage(
        host.temporary.path(), "test.bb-consumer",
        "export function activate(c) { c.registerCommand('providerVersion', function() { "
        "return c.plugins.version('test.zz-provider'); }); }",
        {{"dependencies", QJsonObject{{"test.zz-provider", ">=1.0.0 <2.0.0"}}}});
    // Reverse discovery order must not become activation order.
    host.runtime.importDirectory(consumer);
    host.runtime.importDirectory(provider);
    QTRY_VERIFY(!host.runtime.loading());
    const auto providerId = host.runtime.installationForId("test.zz-provider");
    const auto consumerId = host.runtime.installationForId("test.aa-consumer");
    QCOMPARE(host.runtime.state(providerId), PluginRuntime::Active);
    QCOMPARE(host.runtime.state(consumerId), PluginRuntime::Active);
    auto *providerScope = host.runtime.context(providerId);
    QVERIFY(!providerScope->ui()->settingsComponent().isEmpty());
    QQmlComponent settings(&host.engine, providerScope->ui()->settingsComponent());
    QScopedPointer<QObject> settingsView(
        settings.createWithInitialProperties({{"plugin", QVariant::fromValue(providerScope)}}));
    QVERIFY2(settingsView, qPrintable(settings.errorString()));
    settingsView.reset();
    // Closing the provider's UI does not retire its activation or service.
    QCOMPARE(host.runtime.context(providerId), providerScope);
    const auto providerGeneration = providerScope->activationGeneration();
    host.runtime.importDirectory(anotherConsumer);
    QTRY_VERIFY(!host.runtime.loading());
    const auto anotherConsumerId = host.runtime.installationForId("test.bb-consumer");
    QTRY_COMPARE(host.runtime.state(anotherConsumerId), PluginRuntime::Active);
    QCOMPARE(host.runtime.context(providerId), providerScope);
    QCOMPARE(providerScope->activationGeneration(), providerGeneration);
    QPointer<EchoPluginOperation> version =
        host.runtime.executeCommand(anotherConsumerId, "providerVersion");
    QVERIFY(version);
    QTRY_COMPARE(version->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(version->result().toString(), "1.0.0");
    QPointer<EchoPluginOperation> set =
        host.runtime.executeCommand(consumerId, "set", {{"percent", 73}});
    QVERIFY(set);
    QTRY_COMPARE(set->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(set->result().toMap().value("percent").toInt(), 73);
    QCOMPARE(host.playback.volume(), 73.);
    auto *scope = host.runtime.context(consumerId);
    QVERIFY(scope->metaObject()->indexOfProperty("plugins") >= 0);
    QVERIFY(scope->metaObject()->indexOfProperty("mods") < 0);
    QVERIFY(PluginPackage::supportedFeatures().contains("plugins.dependencies"));
    QVERIFY(PluginPackage::supportedFeatures().contains("plugins.services"));
    QVERIFY(!PluginPackage::supportedFeatures().contains("mods.dependencies"));
    QCOMPARE(scope->plugins()->call("undeclared.plugin", "volume", "1.0.0", "set", {})->errorCode(),
             "DependencyUnavailable");
    QCOMPARE(
        scope->plugins()->call("test.zz-provider", "volume", ">=2.0.0", "set", {})->errorCode(),
        "ServiceUnavailable");
    QPointer<EchoPluginOperation> waiting = host.runtime.executeCommand(consumerId, "wait");
    QTest::qWait(20);
    const auto generation = scope->activationGeneration();
    QVERIFY(host.runtime.setEnabled(providerId, false));
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(waiting && waiting->state() == EchoPluginOperation::Canceled);
    QVERIFY(!host.runtime.context(consumerId));
    QVERIFY(host.runtime.record(consumerId).value("enabled").toBool());
    QCOMPARE(host.runtime.state(consumerId), PluginRuntime::Blocked);
    QCOMPARE(host.runtime.state(anotherConsumerId), PluginRuntime::Blocked);
    QVERIFY(host.runtime.setEnabled(providerId, true));
    QTRY_VERIFY(host.runtime.context(consumerId));
    QTRY_COMPARE(host.runtime.state(consumerId), PluginRuntime::Active);
    QTRY_COMPARE(host.runtime.state(anotherConsumerId), PluginRuntime::Active);
    QVERIFY(host.runtime.context(consumerId)->activationGeneration() != generation);
    // Updating the provider to an incompatible version must keep live code.
    pluginPackage(host.temporary.path(), "test.zz-provider", "export function activate(c){}",
                  {{"version", "2.0.0"}});
    QVERIFY(!host.runtime.installDirectory(provider));
    QCOMPARE(host.runtime.state(providerId), PluginRuntime::Active);
    // A compatible but failing update rolls back the provider's code/data
    // and reactivates its enabled consumers against the restored service.
    pluginPackage(host.temporary.path(), "test.zz-provider",
                  "export function activate(c){throw new Error('provider update failed');}",
                  {{"version", "1.0.1"}});
    QSignalSpy updated(&host.runtime, &PluginRuntime::managementFinished);
    QVERIFY(host.runtime.installDirectory(provider));
    QTRY_COMPARE(updated.size(), 1);
    QVERIFY(!updated.first()[1].toBool());
    QTRY_COMPARE(host.runtime.state(providerId), PluginRuntime::Active);
    QTRY_COMPARE(host.runtime.state(consumerId), PluginRuntime::Active);
    QCOMPARE(host.runtime.record(providerId).value("version").toString(), "1.0.0");
    QVERIFY(host.runtime.uninstall(providerId, false));
    QTRY_VERIFY(host.runtime.installationForId("test.zz-provider").isEmpty());
    QTRY_COMPARE(host.runtime.state(consumerId), PluginRuntime::Blocked);
}

void PluginRuntimeTest::optionalDependencyAndServiceLeasePreserveOwnership()
{
    Host host;
    const auto provider = pluginPackage(host.temporary.path(), "test.library", R"JS(
            export function activate(c) {
                var lease = c.plugins.registerService('api','1.0.0',{
                    object:function(){return c.player;},
                    wait:function(){return new Promise(function(){});}
                });
                c.registerCommand('close',function(){lease.dispose();return true;});
            }
        )JS");
    const auto consumer =
        pluginPackage(host.temporary.path(), "test.optional", "export function activate(c){}",
                      {{"optionalDependencies", QJsonObject{{"test.library", ">=1.0.0 <2.0.0"}}}});
    host.runtime.importDirectory(consumer);
    QTRY_VERIFY(!host.runtime.loading());
    const auto consumerId = host.runtime.installationForId("test.optional");
    QCOMPARE(host.runtime.state(consumerId), PluginRuntime::Active);
    QVERIFY(!host.runtime.context(consumerId)->plugins()->isAvailable("test.library"));
    host.runtime.importDirectory(provider);
    QTRY_VERIFY(!host.runtime.loading());
    const auto providerId = host.runtime.installationForId("test.library");
    auto *plugins = host.runtime.context(consumerId)->plugins();
    QVERIFY(plugins->isAvailable("test.library"));
    QPointer<EchoPluginOperation> object = plugins->call("test.library", "api", "1.0.0", "object");
    QTRY_COMPARE(object->state(), EchoPluginOperation::Failed);
    QCOMPARE(object->errorCode(), "InvalidResult");
    QCOMPARE(plugins
                 ->call("test.library", "api", "1.0.0", "wait",
                        {{"object", QVariant::fromValue(&host.playback)}})
                 ->errorCode(),
             "InvalidArguments");
    QPointer<EchoPluginOperation> pending = plugins->call("test.library", "api", "1.0.0", "wait");
    QTest::qWait(20);
    auto *close = host.runtime.executeCommand(providerId, "close");
    QTRY_COMPARE(close->state(), EchoPluginOperation::Succeeded);
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(host.runtime.setEnabled(providerId, false));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.state(consumerId), PluginRuntime::Active);
    QVERIFY(!host.runtime.context(consumerId)->plugins()->isAvailable("test.library"));
}
