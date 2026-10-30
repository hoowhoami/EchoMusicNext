#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

void PluginRuntimeTest::publicPluginCatalogCommandsAndRetirement()
{
    Host host;
    host.engine.globalObject().setProperty("catalogEvents", host.engine.newArray());
    const auto consumer =
        pluginPackage(host.temporary.path(), "test.consumer", R"JS(
        export function activate(c) {
            c.plugins.onChanged('directory',function(value){catalogEvents.push(value);});
        }
    )JS",
                      {{"optionalDependencies", QJsonObject{{"test.provider", ">=1.0.0 <2.0.0"}}}});
    const auto provider = pluginPackage(host.temporary.path(), "test.provider", R"JS(
        import {awaitOperation} from 'echo:plugin-sdk';
        export function activate(c) {
            c.registerCommand('private',function(){return {secret:true};});
            c.plugins.registerService('api','1.0.0',{z:function(){return 2;},a:function(){return 1;}});
            c.plugins.registerCommand('set-volume','1.0.0',function(args){
                return awaitOperation(c.player.setVolume(args.percent)).then(function(){
                    return {percent:c.player.volume};
                });
            },{title:'Set volume',description:'Uses the host playback service'});
            c.plugins.registerCommand('wait','1.0.0',function(){return new Promise(function(){});});
            c.plugins.registerCommand('bad','1.0.0',function(){return c.player;});
        }
    )JS");
    host.runtime.importDirectory(consumer);
    QTRY_VERIFY(!host.runtime.loading());
    const auto consumerId = host.runtime.installationForId("test.consumer");
    auto *scope = host.runtime.context(consumerId);
    QVERIFY(scope);
    auto *directory = scope->plugins();
    QTRY_COMPARE(host.engine.evaluate("catalogEvents.length").toInt(), 1);
    auto dependencies = directory->snapshot().value("dependencies").toList();
    QCOMPARE(dependencies.size(), 1);
    QVERIFY(!dependencies[0].toMap().value("available").toBool());
    QVERIFY(!dependencies[0].toMap().value("required").toBool());
    host.runtime.importDirectory(provider);
    QTRY_VERIFY(!host.runtime.loading());
    QTRY_VERIFY(directory->isAvailable("test.provider"));
    QTRY_VERIFY(host.engine.evaluate("catalogEvents.length").toInt() >= 2);
    const auto providerId = host.runtime.installationForId("test.provider");
    auto *owner = host.runtime.context(providerId);
    QVERIFY(owner);
    dependencies = directory->snapshot().value("dependencies").toList();
    const auto entry = dependencies[0].toMap();
    QCOMPARE(entry.value("id").toString(), "test.provider");
    QCOMPARE(entry.value("version").toString(), "1.0.0");
    QCOMPARE(entry.value("services").toList().size(), 1);
    QCOMPARE(entry.value("services").toList()[0].toMap().value("methods").toStringList(),
             (QStringList{"a", "z"}));
    QCOMPARE(entry.value("commands").toList().size(), 3);
    QPointer<EchoPluginOperation> set =
        directory->callCommand("test.provider", "set-volume", ">=1.0.0 <2.0.0", {{"percent", 82}});
    QTRY_COMPARE(set->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(set->result().toMap().value("percent").toInt(), 82);
    QCOMPARE(host.playback.volume(), 82.);
    QCOMPARE(directory->callCommand("test.provider", "private", "1.0.0")->errorCode(),
             "CommandUnavailable");
    QCOMPARE(directory->callCommand("test.provider", "set-volume", ">=2.0.0")->errorCode(),
             "CommandUnavailable");
    QCOMPARE(directory->callCommand("not.declared", "set-volume", "1.0.0")->errorCode(),
             "DependencyUnavailable");
    QCOMPARE(directory
                 ->callCommand("test.provider", "set-volume", "1.0.0",
                               {{"host", QVariant::fromValue(&host.playback)}})
                 ->errorCode(),
             "InvalidArguments");
    QPointer<EchoPluginOperation> bad = directory->callCommand("test.provider", "bad", "1.0.0");
    QTRY_COMPARE(bad->state(), EchoPluginOperation::Failed);
    QCOMPARE(bad->errorCode(), "InvalidResult");
    auto *first = owner->plugins()->registerCommand(
        "dynamic", "1.0.0", host.engine.evaluate("(function(){return {answer:1};})"));
    QVERIFY(first);
    QTRY_COMPARE(directory->snapshot()
                     .value("dependencies")
                     .toList()[0]
                     .toMap()
                     .value("commands")
                     .toList()
                     .size(),
                 4);
    const int changes = host.engine.evaluate("catalogEvents.length").toInt();
    auto *second = owner->plugins()->registerCommand(
        "dynamic", "1.1.0", host.engine.evaluate("(function(){return {answer:2};})"));
    QVERIFY(second && !first->active());
    first->dispose();
    QVERIFY(second->active());
    QVERIFY(!owner->plugins()->registerCommand("dynamic", "invalid",
                                               host.engine.evaluate("(function(){})")));
    QVERIFY(second->active());
    QTRY_VERIFY(host.engine.evaluate("catalogEvents.length").toInt() > changes);
    QPointer<EchoPluginOperation> replaced =
        directory->callCommand("test.provider", "dynamic", ">=1.0.0");
    QTRY_COMPARE(replaced->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(replaced->result().toMap().value("answer").toInt(), 2);
    QVERIFY(owner->plugins()->isAvailable(owner->id()));
    QPointer<EchoPluginOperation> self =
        owner->plugins()->callCommand(owner->id(), "dynamic", ">=1.0.0");
    QTRY_COMPARE(self->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(self->result().toMap().value("answer").toInt(), 2);
    QPointer<EchoPluginOperation> canceled =
        directory->callCommand("test.provider", "wait", "1.0.0");
    QTest::qWait(20);
    canceled->cancel();
    QCOMPARE(canceled->state(), EchoPluginOperation::Canceled);
    const auto allFinished = [&] {
        const auto operations = owner->findChildren<EchoPluginOperation *>();
        return std::all_of(operations.cbegin(), operations.cend(),
                           [](auto *op) { return op->finished(); });
    };
    QTRY_VERIFY(allFinished());
    QPointer<EchoPluginOperation> pending =
        directory->callCommand("test.provider", "wait", "1.0.0");
    QTest::qWait(20);
    QVERIFY(host.runtime.setEnabled(providerId, false));
    QTRY_VERIFY(!host.runtime.loading());
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    // The runtime retires consumers before the provider, including optional
    // dependencies, then reactivates eligible consumers with a fresh scope.
    QTRY_COMPARE(host.runtime.state(consumerId), PluginRuntime::Active);
    scope = host.runtime.context(consumerId);
    QVERIFY(scope);
    directory = scope->plugins();
    QTRY_VERIFY(!directory->snapshot()
                     .value("dependencies")
                     .toList()[0]
                     .toMap()
                     .value("available")
                     .toBool());
    QVERIFY(host.runtime.setEnabled(providerId, true));
    QTRY_VERIFY(!host.runtime.loading());
    scope = host.runtime.context(consumerId);
    QVERIFY(scope);
    directory = scope->plugins();
    QTRY_VERIFY(directory->isAvailable("test.provider"));
    QVERIFY(directory->snapshot().value("dependencies").toList()[0].toMap().value("generation") !=
            entry.value("generation"));
}

void PluginRuntimeTest::publicPluginCatalogScopeAccountAndReplacement()
{
    Host host;
    const auto provider =
        pluginPackage(host.temporary.path(), "test.provider", "export function activate(c){}");
    const auto consumer =
        pluginPackage(host.temporary.path(), "test.consumer", "export function activate(c){}",
                      {{"optionalDependencies", QJsonObject{{"test.provider", ">=1.0.0"}}}});
    host.runtime.importDirectory(provider);
    host.runtime.importDirectory(consumer);
    QTRY_VERIFY(!host.runtime.loading());
    auto *scope = host.runtime.context(host.runtime.installationForId("test.consumer"));
    auto *owner = host.runtime.context(host.runtime.installationForId("test.provider"));
    QVERIFY(scope && owner);
    auto *directory = scope->plugins();
    auto *api = owner->plugins()->registerService(
        "live", "1.0.0", host.engine.evaluate("({read:function(){return 1;}})"));
    QVERIFY(api);
    QSignalSpy notifications(directory, &EchoPluginRegistry::availabilityChanged);
    QTRY_COMPARE(directory->snapshot()
                     .value("dependencies")
                     .toList()[0]
                     .toMap()
                     .value("services")
                     .toList()
                     .size(),
                 1);
    auto presentationOwner = std::make_unique<QObject>();
    QPointer<EchoPluginContext> child =
        scope->createPresentation(presentationOwner.get(), "test.catalog");
    QVERIFY(child);
    host.engine.evaluate("var childNotices=0;var oldNotices=0;var newNotices=0;"
                         "var quietNotices=0;var lastDirectoryEpoch='';");
    auto *oldObserver =
        directory->onChanged("replace", host.engine.evaluate("(function(){oldNotices++;})"));
    QVERIFY(oldObserver);
    auto *newObserver = directory->onChanged(
        "replace", host.engine.evaluate(
                       "(function(value){newNotices++;lastDirectoryEpoch=value.accountEpoch;})"));
    QVERIFY(newObserver);
    QVERIFY(child->plugins()->onChanged("child",
                                        host.engine.evaluate("(function(){childNotices++;})")));
    QTRY_COMPARE(host.engine.evaluate("newNotices").toInt(), 1);
    QCOMPARE(host.engine.evaluate("oldNotices").toInt(), 0);
    QVERIFY(directory->onChanged("quiet", host.engine.evaluate("(function(){quietNotices++;})"),
                                 false));
    QTest::qWait(20);
    QCOMPARE(host.engine.evaluate("quietNotices").toInt(), 0);
    QTRY_COMPARE(host.engine.evaluate("childNotices").toInt(), 1);
    QVERIFY(
        !child->plugins()->registerCommand("bad", "1.0.0", host.engine.evaluate("(function(){})")));
    QVERIFY(!child->plugins()->registerService("bad", "1.0.0",
                                               host.engine.evaluate("({run:function(){}})")));
    auto *wait = owner->plugins()->registerCommand(
        "wait", "1.0.0", host.engine.evaluate("(function(){return new Promise(function(){});})"));
    QVERIFY(wait);
    QPointer<EchoPluginOperation> pending =
        directory->callCommand("test.provider", "wait", "1.0.0");
    QTest::qWait(20);
    auto *replacement = owner->plugins()->registerCommand(
        "wait", "1.0.0", host.engine.evaluate("(function(){return true;})"));
    QVERIFY(replacement);
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(!owner->plugins()->registerCommand(
        "wait", "1.0.0", host.engine.evaluate("(function(){})"), {{"unexpected", true}}));
    QVERIFY(replacement->active());
    host.engine.evaluate("var lateResolve=null;");
    QVERIFY(owner->plugins()->registerCommand(
        "late", "1.0.0",
        host.engine.evaluate(
            "(function(){return new Promise(function(resolve){lateResolve=resolve;});})")));
    pending = directory->callCommand("test.provider", "late", "1.0.0");
    const auto operationRoot = host.engine.newQObject(pending);
    QTRY_VERIFY(host.engine.globalObject().property("lateResolve").isCallable());
    QVERIFY(QMetaObject::invokeMethod(&host.api, "sessionChanged", Q_ARG(quint64, 1)));
    QTRY_COMPARE(pending->state(), EchoPluginOperation::Canceled);
    host.engine.globalObject().property("lateResolve").call({QJSValue("old account result")});
    QTest::qWait(20);
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    Q_UNUSED(operationRoot);
    QVERIFY(!child || !child->active());
    presentationOwner.reset();
    QTRY_VERIFY(child.isNull());
    QTRY_COMPARE(host.engine.evaluate("lastDirectoryEpoch").toString(),
                 directory->snapshot().value("accountEpoch").toString());
    const int childCount = host.engine.evaluate("childNotices").toInt();
    QTest::qWait(30);
    const int before = host.engine.evaluate("newNotices").toInt();
    auto *newService = owner->plugins()->registerService(
        "live", "1.1.0", host.engine.evaluate("({write:function(){return true;}})"));
    QVERIFY(newService && !api->active());
    api->dispose();
    QTRY_VERIFY(host.engine.evaluate("newNotices").toInt() > before);
    QCOMPARE(host.engine.evaluate("childNotices").toInt(), childCount);
    QCOMPARE(directory->snapshot()
                 .value("dependencies")
                 .toList()[0]
                 .toMap()
                 .value("services")
                 .toList()[0]
                 .toMap()
                 .value("version")
                 .toString(),
             "1.1.0");
    newService->dispose();
    QTRY_COMPARE(directory->snapshot()
                     .value("dependencies")
                     .toList()[0]
                     .toMap()
                     .value("services")
                     .toList()
                     .size(),
                 0);
    QVERIFY(!notifications.isEmpty());
    const auto overflow = pluginPackage(host.temporary.path(), "test.overflow", R"JS(
        export function activate(c) {
            for (var i=0;i<129;i++)
                c.plugins.registerCommand('command'+i,'1.0.0',function(){return true;});
        }
    )JS");
    QSignalSpy failedInstall(&host.runtime, &PluginRuntime::managementFinished);
    host.runtime.importDirectory(overflow);
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(failedInstall.count(), 1);
    QVERIFY(!failedInstall.first()[1].toBool());
    QVERIFY(host.runtime.installationForId("test.overflow").isEmpty());
    QVERIFY(!directory->isAvailable("test.overflow"));
}
