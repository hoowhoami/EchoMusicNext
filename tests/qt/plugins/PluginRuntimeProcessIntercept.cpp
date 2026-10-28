#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginProcess.h"
#include "EchoPluginServer.h"
#ifdef Q_OS_UNIX
#include <signal.h>
#include <cerrno>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#endif
namespace
{
EchoPluginContext *extensions(Host &host, bool approve = true)
{
    if (approve)
        QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                         host.runtime.permissions(),
                         [&host](const QString &id, const QVariantMap &) {
                             host.runtime.permissions()->decide(id, true);
                         });
    const auto directory =
        pluginPackage(host.temporary.path(), "native-extensions", "export function activate(c){}",
                      {{"capabilities", QJsonArray{"process", "serverIntercept", "kugouApi"}}});
    const auto name = QFileInfo(QString::fromUtf8(ECHO_PLUGIN_PROCESS_FIXTURE)).fileName();
    if (!QFile::copy(QString::fromUtf8(ECHO_PLUGIN_PROCESS_FIXTURE), directory + '/' + name))
        return nullptr;
    QFile::setPermissions(directory + '/' + name,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    host.runtime.importDirectory(directory);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId("native-extensions")) ==
                       PluginRuntime::Active;
            },
            5000))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId("native-extensions"));
}
QString executable()
{
    return QFileInfo(QString::fromUtf8(ECHO_PLUGIN_PROCESS_FIXTURE)).fileName();
}
QJsonObject marker(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
bool dead(qint64 pid)
{
#ifdef Q_OS_UNIX
    return pid > 0 && ::kill(pid_t(pid), 0) != 0 && errno == ESRCH;
#else
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process)
        return GetLastError() == ERROR_INVALID_PARAMETER;
    DWORD code = STILL_ACTIVE;
    const bool exited = GetExitCodeProcess(process, &code) && code != STILL_ACTIVE;
    CloseHandle(process);
    return exited;
#endif
}
EchoPluginRegistration *install(Host &host, EchoPluginContext *scope, const QString &code)
{
    auto *op = scope->server()->intercept(js(host, code));
    auto root = host.engine.newQObject(op);
    if (!QTest::qWaitFor([op] { return op->finished(); }, 5000))
        return nullptr;
    if (op->state() != EchoPluginOperation::Succeeded) {
        qWarning() << op->errorCode() << op->errorMessage();
        return nullptr;
    }
    return qobject_cast<EchoPluginRegistration *>(op->result().value<QObject *>());
}
class HttpProbe : public QObject
{
public:
    QTcpServer server;
    int count = 0;
    QByteArray last;
    QByteArray body = R"({"status":1,"errcode":0,"data":{"list":[]}})";
    explicit HttpProbe()
    {
        if (!server.listen(QHostAddress::LocalHost))
            qFatal("Cannot listen");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *s = server.nextPendingConnection()) {
                auto bytes = std::make_shared<QByteArray>();
                auto sent = std::make_shared<bool>(false);
                connect(s, &QTcpSocket::readyRead, s, [this, s, bytes, sent] {
                    *bytes += s->readAll();
                    if (*sent || !bytes->contains("\r\n\r\n"))
                        return;
                    *sent = true;
                    ++count;
                    last = *bytes;
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
                             "close\r\nContent-Length: " +
                             QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
};
} // namespace
void PluginRuntimeTest::processAuthorizationOwnershipAndTreeRetirement()
{
    Host host;
    auto *scope = extensions(host);
    QVERIFY(scope);
    const auto path = host.temporary.filePath("process.json");
    host.engine.globalObject().setProperty("processScope", host.engine.newQObject(scope));
    host.engine.globalObject().setProperty(
        "launchOptions",
        host.engine.toScriptValue(QVariantMap{
            {"executable", executable()},
            {"args", QStringList{path, "--ignore-term"}},
            {"env", QVariantMap{{"PLUGIN_TEST_NUMBER", 7}, {"PLUGIN_TEST_BOOL", true}}}}));
    auto *launch = qobject_cast<EchoPluginOperation *>(
        js(host, "processScope.process.launch(launchOptions)").toQObject());
    QVERIFY(launch);
    auto keep = host.engine.newQObject(launch);
    QTRY_COMPARE(launch->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(marker(path).contains("child"));
    const auto pids = marker(path);
    QCOMPARE(scope->process()->running().size(), 1);
    auto owner = std::make_unique<QObject>();
    auto *other = scope->createPresentation(owner.get(), "process-other");
    QVERIFY(other);
    auto *foreign = other->process()->terminate(launch->result().toMap().value("id").toString());
    QCOMPARE(foreign->errorCode(), "NotFound");
    other->beginRetire();
    owner.reset();
    host.engine.globalObject().setProperty("launchedProcess",
                                           host.engine.toScriptValue(launch->result()));
    auto *stop = qobject_cast<EchoPluginOperation *>(
        js(host, "processScope.process.terminate(launchedProcess.id)").toQObject());
    QVERIFY(stop);
    auto stopRoot = host.engine.newQObject(stop);
    QTRY_COMPARE_WITH_TIMEOUT(stop->state(), EchoPluginOperation::Succeeded, 5000);
    QTRY_VERIFY(dead(pids.value("pid").toInteger()));
    QTRY_VERIFY(dead(pids.value("child").toInteger()));
    QVERIFY(scope->process()->isDrained());
    // A normal parent exit must not leave its child running either.
    const auto orphan = host.temporary.filePath("orphan.json");
    auto *normal = scope->process()->launch(
        {{"executable", executable()}, {"args", QStringList{orphan, "--exit-parent"}}});
    auto normalRoot = host.engine.newQObject(normal);
    QTRY_COMPARE(normal->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(marker(orphan).contains("child"));
    const auto child = marker(orphan).value("child").toInteger();
    QTRY_VERIFY(scope->process()->isDrained());
    QTRY_VERIFY(dead(child));
    const auto disabledPath = host.temporary.filePath("disable-active.json");
    auto *active = scope->process()->launch(
        {{"executable", executable()}, {"args", QStringList{disabledPath, "--ignore-term"}}});
    auto activeRoot = host.engine.newQObject(active);
    QTRY_COMPARE(active->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(marker(disabledPath).contains("child"));
    const auto disabledPids = marker(disabledPath);
    const auto installation = scope->installationId();
    host.runtime.setEnabled(installation, false);
    QTRY_COMPARE_WITH_TIMEOUT(host.runtime.state(installation), PluginRuntime::Disabled, 5000);
    QTRY_VERIFY(dead(disabledPids.value("pid").toInteger()));
    QTRY_VERIFY(dead(disabledPids.value("child").toInteger()));
}
void PluginRuntimeTest::processCancellationAccountAndContentGrant()
{
    Host host;
    auto *scope = extensions(host, false);
    QVERIFY(scope);
    QSignalSpy prompts(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested);
    const auto path = host.temporary.filePath("scope.json");
    const QVariantMap options{{"executable", executable()},
                              {"args", QStringList{path, "--ignore-term"}}};
    auto *denied = scope->process()->launch(options);
    auto deniedRoot = host.engine.newQObject(denied);
    QTRY_COMPARE(prompts.size(), 1);
    host.runtime.permissions()->decide(prompts.last()[0].toString(), false);
    QTRY_COMPARE(denied->state(), EchoPluginOperation::Failed);
    QCOMPARE(denied->errorCode(), "PermissionDenied");
    QVERIFY(!QFileInfo::exists(path));
    auto *cancel = scope->process()->launch(options);
    auto cancelRoot = host.engine.newQObject(cancel);
    QTRY_COMPARE(prompts.size(), 2);
    const auto cancelledPrompt = prompts.last()[0].toString();
    cancel->cancel();
    host.runtime.permissions()->decide(cancelledPrompt, true);
    QVERIFY(scope->process()->running().isEmpty());
    auto *allowed = scope->process()->launch(options);
    auto allowedRoot = host.engine.newQObject(allowed);
    QTRY_COMPARE(prompts.size(), 3);
    host.runtime.permissions()->decide(prompts.last()[0].toString(), true);
    QTRY_COMPARE(allowed->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(marker(path).contains("child"));
    const auto pids = marker(path);
    QSignalSpy oldExits(scope->process(), &EchoPluginProcess::exited);
    QVERIFY(host.api.setAccount(77, "test-token", "test-refresh"));
    QTRY_VERIFY_WITH_TIMEOUT(scope->process()->isDrained(), 5000);
    QTRY_VERIFY(dead(pids.value("child").toInteger()));
    QVERIFY(oldExits.isEmpty()); // Old-account helper events cannot reach the new session.
    QVERIFY(scope->process()->running().isEmpty());
    const auto helper = scope->package().root + '/' + executable();
    QFile executableFile(helper);
    const auto length = executableFile.size();
    QVERIFY(executableFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QVERIFY(executableFile.open(QIODevice::Append));
    executableFile.write("changed");
    executableFile.close();
    auto *changed = scope->process()->launch(options);
    auto changedRoot = host.engine.newQObject(changed);
    QTRY_COMPARE(prompts.size(), 4); // A changed executable cannot reuse the old grant.
    changed->cancel();
    QVERIFY(executableFile.open(QIODevice::ReadWrite));
    QVERIFY(executableFile.resize(length));
    executableFile.close();
    QVERIFY(executableFile.setPermissions(QFile::ReadOwner | QFile::ExeOwner));
    auto *again = scope->process()->launch(options);
    auto againRoot = host.engine.newQObject(again);
    QTRY_COMPARE(again->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(prompts.size(), 4); // Same installed bytes, same helper grant.
    QString grant;
    for (const auto &r : host.runtime.permissions()->records())
        if (r.toMap().value("capability") == "process")
            grant = r.toMap().value("id").toString();
    QVERIFY(!grant.isEmpty());
    QVERIFY(host.runtime.permissions()->revoke(grant));
    QTRY_VERIFY_WITH_TIMEOUT(scope->process()->isDrained(), 5000);
    auto *pending = scope->process()->launch(options);
    auto pendingRoot = host.engine.newQObject(pending);
    QTRY_COMPARE(prompts.size(), 5);
    const auto installation = scope->installationId();
    host.runtime.setEnabled(installation, false);
    QTRY_VERIFY(pending->finished());
    QTRY_VERIFY(host.runtime.state(installation) == PluginRuntime::Disabled);
}
void PluginRuntimeTest::hostInterceptorsPriorityForwardOnceAndOrigin()
{
    Host host;
    auto *scope = extensions(host);
    QVERIFY(scope);
    QVERIFY(host.api.setAccount(42, "fixture-token", "fixture-refresh"));
    QVERIFY(host.api.setNetworkTransport(1));
    js(host, "var order=[];var sawAuthorization=false; function wait(op){return new "
             "Promise(function(ok,no){function "
             "f(){if(op.state<2)return;op.completed.disconnect(f);op.state===2?ok(op.result):no("
             "Error(op.errorMessage))}op.completed.connect(f);f()})}");
    auto *low = install(host, scope,
                        "({id:'low',priority:1,match:'search_hot',handle:function(r){order.push('"
                        "low');return {status:0,body:{value:1}}}})");
    QVERIFY(low);
    auto *high = install(host, scope, R"JS(
({id:'high',priority:10,match:'search_hot',handle:function(r){
  order.push('high');
  sawAuthorization=r.headers.Authorization.indexOf('token=fixture-token')>=0;
  return wait(r.next()).then(function(a){
    return wait(r.next()).then(function(b){
      if(a.body.value!==b.body.value)throw Error('not memoized');
      a.body.value++;return a;
    });
  });
}})
)JS");
    QVERIFY(high);
    QSignalSpy success(&host.api, &ApiClient::succeeded);
    const auto id = host.api.searchHot();
    QTRY_COMPARE(success.size(), 1);
    QCOMPARE(success[0][0].toULongLong(), id);
    QCOMPARE(QJsonDocument::fromJson(success[0][1].toByteArray()).object().value("value").toInt(),
             2);
    QCOMPARE(js(host, "order.join(',')").toString(), "high,low");
    QVERIFY(js(host, "sawAuthorization").toBool());
    auto *plugin = scope->kugou()->request("searchHot");
    auto pluginRoot = host.engine.newQObject(plugin);
    QTRY_VERIFY(plugin->finished());
    QCOMPARE(js(host, "order.length").toInt(), 2);
    low->dispose();
    high->dispose();
    QSignalSpy reported(scope, &EchoPluginContext::callbackFailed);
    auto *thrower = install(
        host, scope,
        R"JS(({id:'thrower',priority:5,match:'album_detail',handle:function(r){throw Error(r.headers.Authorization)}}))JS");
    QVERIFY(thrower);
    auto *arguments = install(
        host, scope,
        R"JS(({id:'arguments',priority:10,match:'album_detail',handle:function(r){return wait(r.next({query:{id:456}}))}}))JS");
    auto *target = install(
        host, scope,
        R"JS(({id:'target',match:'album_detail',handle:function(r){return {status:0,body:{albumQueryId:r.query.id}}}}))JS");
    QVERIFY(arguments);
    QVERIFY(target);
    host.api.albumDetail(123);
    QTRY_COMPARE(success.size(), 2);
    QCOMPARE(QJsonDocument::fromJson(success.last()[1].toByteArray())
                 .object()
                 .value("albumQueryId")
                 .toInt(),
             456);
    QVERIFY(!reported.isEmpty());
    for (const auto &event : reported)
        QVERIFY(!event[0].toString().contains("fixture-token"));
    thrower->dispose();
    arguments->dispose();
    target->dispose();
    // Actual HTTP reaches a local peer; malformed wire is repaired BEFORE the
    // Rust DTO converter and next is memoized even after a callback throws.
    HttpProbe peer;
    peer.body = "{bad json";
    QVERIFY(host.api.setNetworkTransport(peer.server.serverPort()));
    auto *wire =
        install(host, scope,
                QString(R"JS(
({id:'wire',stage:'transport',handle:function(r){
  var h=r.transport.headers;h['x-plugin-probe']=['yes'];
  return wait(r.next({url:'http://127.0.0.1:%1/probe',headers:h})).then(function(a){
    return wait(r.next()).then(function(b){
      if(a.bodyBase64!==b.bodyBase64)throw Error('duplicate');
      a.bodyBase64='%2';return a;
    });
  });
}})
)JS")
                    .arg(peer.server.serverPort())
                    .arg(QString::fromLatin1(
                        QByteArray(R"({"status":1,"errcode":0,"data":{"list":[]}})").toBase64())));
    QVERIFY(wire);
    const auto forwarded = host.api.searchHot();
    QTRY_COMPARE(success.size(), 3);
    QCOMPARE(success.last()[0].toULongLong(), forwarded);
    QVERIFY(QJsonDocument::fromJson(success.last()[1].toByteArray()).isArray());
    QCOMPARE(peer.count, 1);
    QVERIFY(peer.last.toLower().contains("x-plugin-probe: yes"));
    wire->dispose();
}
void PluginRuntimeTest::hostInterceptorsCancellationRetirementAndErrors()
{
    Host host;
    auto *scope = extensions(host);
    QVERIFY(scope);
    QVERIFY(host.api.setNetworkTransport(1));
    js(host, "var calls=0;var saved;function wait(op){return new Promise(function(ok,no){function "
             "f(){if(op.state<2)return;op.completed.disconnect(f);op.state===2?ok(op.result):no("
             "Error(op.errorMessage))}op.completed.connect(f);f()})}");
    auto *block = install(host, scope,
                          "({id:'blocked',timeoutMs:30000,handle:function(r){calls++;saved=r;"
                          "return new Promise(function(){})}})");
    QVERIFY(block);
    QSignalSpy failed(&host.api, &ApiClient::failed);
    const auto cancelled = host.api.searchHot();
    QTRY_COMPARE(js(host, "calls").toInt(), 1);
    QVERIFY(host.api.cancelRequest(cancelled));
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(failed[0][0].toULongLong(), cancelled);
    QVERIFY(!js(host, "saved.active").toBool());
    // Disposing a live callback fails open to exactly one real transport.
    host.api.searchHot();
    QTRY_COMPARE(js(host, "calls").toInt(), 2);
    block->dispose();
    QTRY_COMPARE(failed.size(), 2);
    HttpProbe peer;
    peer.body =
        R"({"status":1,"error_code":0,"data":{"userid":99,"pre_total_ver":1,"total_ver":2,"info":{"listid":7,"status":1,"code":1}}})";
    QVERIFY(host.api.setAccount(99, "write-token", "write-refresh"));
    QVERIFY(host.api.setNetworkTransport(peer.server.serverPort()));
    auto *error = install(host, scope,
                          QString(R"JS(
({id:'error-after-next',stage:'transport',handle:function(r){
  return wait(r.next({url:'http://127.0.0.1:%1/once'})).then(function(){throw Error('after forward')});
}})
)JS")
                              .arg(peer.server.serverPort()));
    QVERIFY(error);
    QSignalSpy success(&host.api, &ApiClient::succeeded);
    QVERIFY(host.api.createPlaylist("fixture write", true));
    QTRY_COMPARE(success.size(), 1);
    QCOMPARE(peer.count, 1);
    QVERIFY(peer.last.startsWith("POST "));
    error->dispose();
    auto *timeout = install(
        host, scope,
        "({id:'timeout',timeoutMs:20,handle:function(r){return new Promise(function(){})}})");
    QVERIFY(timeout);
    // A lower mock makes a timed-out middleware's fallback deterministic.
    auto *mock = install(
        host, scope,
        "({id:'mock',priority:-1,handle:function(){return {status:0,body:{fallback:true}}}})");
    QVERIFY(mock);
    host.api.searchHot();
    QTRY_COMPARE(success.size(), 2);
    QVERIFY(QJsonDocument::fromJson(success.last()[1].toByteArray())
                .object()
                .value("fallback")
                .toBool());
    QVERIFY(host.api.setAccount(100, "account-token", "refresh"));
    QVERIFY(!timeout->active());
    QVERIFY(!mock->active());
}
void PluginRuntimeTest::hostInterceptorAuthorizationRevocationAndSessionIsolation()
{
    Host host;
    auto *scope = extensions(host, false);
    QVERIFY(scope);
    QVERIFY(host.api.setNetworkTransport(1));
    QSignalSpy prompts(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested);
    js(host, "var resolveLate;var observed;var invoked=0;");
    const auto definition = js(
        host,
        R"JS(({id:'guarded',handle:function(r){observed=r;invoked++;return new Promise(function(resolve){resolveLate=resolve})}}))JS");
    auto *denied = scope->server()->intercept(definition);
    auto deniedRoot = host.engine.newQObject(denied);
    QCOMPARE(prompts.size(), 1);
    host.runtime.permissions()->decide(prompts.last()[0].toString(), false);
    QCOMPARE(denied->state(), EchoPluginOperation::Failed);
    auto *cancelled = scope->server()->intercept(definition);
    auto cancelledRoot = host.engine.newQObject(cancelled);
    QCOMPARE(prompts.size(), 2);
    cancelled->cancel();
    host.runtime.permissions()->decide(prompts.last()[0].toString(), true);
    QCOMPARE(cancelled->state(), EchoPluginOperation::Canceled);
    auto *allowed = scope->server()->intercept(definition);
    auto allowedRoot = host.engine.newQObject(allowed);
    QCOMPARE(prompts.size(), 3);
    host.runtime.permissions()->decide(prompts.last()[0].toString(), true);
    QCOMPARE(allowed->state(), EchoPluginOperation::Succeeded);
    auto *lease = qobject_cast<EchoPluginRegistration *>(allowed->result().value<QObject *>());
    QVERIFY(lease);
    QSignalSpy failed(&host.api, &ApiClient::failed);
    QSignalSpy success(&host.api, &ApiClient::succeeded);
    host.api.searchHot();
    QTRY_COMPARE(js(host, "invoked").toInt(), 1);
    QString permission;
    for (const auto &record : host.runtime.permissions()->records())
        if (record.toMap().value("capability") == "serverIntercept")
            permission = record.toMap().value("id").toString();
    QVERIFY(!permission.isEmpty());
    QVERIFY(host.runtime.permissions()->revoke(permission));
    QVERIFY(!lease->active());
    QTRY_COMPARE(failed.size(), 1); // Revocation releases the host request once.
    js(host, "resolveLate({status:0,body:{stale:true}})");
    QTest::qWait(20);
    QVERIFY(success.isEmpty());
    auto *again = scope->server()->intercept(definition);
    auto againRoot = host.engine.newQObject(again);
    QCOMPARE(prompts.size(), 4);
    host.runtime.permissions()->decide(prompts.last()[0].toString(), true);
    QCOMPARE(again->state(), EchoPluginOperation::Succeeded);
    host.api.searchHot();
    QTRY_COMPARE(js(host, "invoked").toInt(), 2);
    QVERIFY(host.api.setAccount(42, "session-token", "session-refresh"));
    QVERIFY(!js(host, "observed.active").toBool());
    js(host, "resolveLate({status:0,body:{oldAccount:true}})");
    QTest::qWait(20);
    QVERIFY(success.isEmpty());
    QCOMPARE(failed.size(), 1); // Session changes discard old delivery, not fail open.
    auto *last = install(
        host, scope,
        R"JS(({id:'stop',handle:function(r){observed=r;invoked++;return new Promise(function(){})}}))JS");
    QVERIFY(last);
    host.api.searchHot();
    QTRY_COMPARE(js(host, "invoked").toInt(), 3);
    const auto installation = scope->installationId();
    host.runtime.setEnabled(installation, false);
    QTRY_COMPARE(failed.size(), 2); // Host operation remains valid after plugin disable.
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
}
void PluginRuntimeTest::hostTransportMocksRecoveryAndCancellation()
{
    Host host;
    auto *scope = extensions(host);
    QVERIFY(scope);
    QVERIFY(host.api.setNetworkTransport(1));
    host.engine.globalObject().setProperty("nativeScope", host.engine.newQObject(scope));
    js(host, R"JS(
var rawCalls=0, rawObserved, sawTransportError=false;
function wait(op){return new Promise(function(ok,no){function f(){if(op.state<2)return;op.completed.disconnect(f);op.state===2?ok(op.result):no(Error(op.errorMessage))}op.completed.connect(f);f()})}
var encoded=nativeScope.server.encodeBody({status:1,errcode:0,data:{list:[]}});
)JS");
    QCOMPARE(
        js(host,
           "nativeScope.server.decodeBody(nativeScope.server.encodeBody('歌词','utf8'),'utf8')")
            .toString(),
        QStringLiteral("歌词"));
    auto *mock = install(
        host, scope,
        R"JS(({id:'raw-mock',stage:'transport',handle:function(r){rawCalls++;return {status:200,headers:{},bodyBase64:encoded}}}))JS");
    QVERIFY(mock);
    QSignalSpy success(&host.api, &ApiClient::succeeded);
    QSignalSpy failed(&host.api, &ApiClient::failed);
    host.api.searchHot();
    QTRY_COMPARE(success.size(), 1);
    QVERIFY(QJsonDocument::fromJson(success.last()[1].toByteArray()).isArray());
    QCOMPARE(js(host, "rawCalls").toInt(), 1);
    auto *own = scope->kugou()->request("searchHot");
    auto ownRoot = host.engine.newQObject(own);
    QTRY_COMPARE(own->state(), EchoPluginOperation::Failed);
    QCOMPARE(js(host, "rawCalls").toInt(), 1); // Raw transport also respects plugin origin.
    mock->dispose();
    auto *recovery = install(
        host, scope,
        R"JS(({id:'recover',stage:'transport',handle:function(r){rawCalls++;return wait(r.next()).then(function(reply){sawTransportError=reply.status===0&&!!reply.error;return {status:200,headers:{},bodyBase64:encoded}})}}))JS");
    QVERIFY(recovery);
    host.api.searchHot();
    QTRY_COMPARE(success.size(), 2);
    QVERIFY(js(host, "sawTransportError").toBool());
    recovery->dispose();
    auto *bad = install(
        host, scope,
        R"JS(({id:'invalid-mock',stage:'transport',handle:function(){return {status:200,headers:{},bodyBase64:'!invalid'}}}))JS");
    QVERIFY(bad);
    const auto previousFailures = failed.size();
    host.api.searchHot();
    QTRY_COMPARE(failed.size(), previousFailures + 1);
    bad->dispose();
    auto *blocked = install(
        host, scope,
        R"JS(({id:'blocked-raw',stage:'transport',handle:function(r){rawObserved=r;return new Promise(function(){})}}))JS");
    QVERIFY(blocked);
    const auto id = host.api.searchHot();
    QTRY_VERIFY(js(host, "!!rawObserved").toBool());
    QVERIFY(host.api.cancelRequest(id));
    QTRY_COMPARE(failed.size(), previousFailures + 2);
    QVERIFY(!js(host, "rawObserved.active").toBool());
    blocked->dispose();
    // A schema-invalid unsigned query never dispatches. The host falls back to
    // its original query, then validates the raw mock through the actual DTO.
    auto *patch = install(
        host, scope,
        R"JS(({id:'invalid-query',handle:function(r){return wait(r.next({query:{unknownField:true}}))}}))JS");
    QVERIFY(patch);
    auto *raw = install(
        host, scope,
        R"JS(({id:'query-wire',stage:'transport',match:/^search_hot$/,handle:function(){return {status:200,headers:{},bodyBase64:encoded}}}))JS");
    QVERIFY(raw);
    host.api.searchHot();
    QTRY_COMPARE(success.size(), 3);
}
