#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginSqlite.h"
namespace
{
struct Result
{
    EchoPluginOperation::State state;
    QVariant value;
    QString error;
};
Result settle(Host &host, EchoPluginOperation *op)
{
    if (!op)
        return {EchoPluginOperation::Failed, {}, "Null operation"};
    const auto root = host.engine.newQObject(op);
    if (!QTest::qWaitFor([op] { return op->finished(); }, 7000))
        return {EchoPluginOperation::Failed, {}, "SQL timeout"};
    return {op->state(), op->result(), op->errorMessage()};
}
EchoPluginContext *sqlScope(Host &host, const QString &id = "sqlite")
{
    const auto source = pluginPackage(host.temporary.path(), id, "export function activate(c){}",
                                      {{"capabilities", QJsonArray{"sqlite"}}});
    host.runtime.importDirectory(source);
    if (!QTest::qWaitFor([&] {
            return host.runtime.state(host.runtime.installationForId(id)) == PluginRuntime::Active;
        }))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId(id));
}
EchoPluginDatabase *openDb(Host &host, EchoPluginContext &scope, const QVariantMap &options = {})
{
    const auto result = settle(host, scope.sqlite()->open(options));
    if (result.state != EchoPluginOperation::Succeeded) {
        qWarning() << result.error;
        return nullptr;
    }
    return result.value.value<EchoPluginDatabase *>();
}
} // namespace
void PluginRuntimeTest::sqliteBindingsMigrationsAndTransactions()
{
    Host host;
    auto *scope = sqlScope(host);
    QVERIFY(scope);
    const QVariantList migrations{QVariantMap{
        {"version", 1},
        {"sql", "CREATE TABLE songs(id INTEGER PRIMARY KEY, title TEXT UNIQUE, bytes BLOB, n "
                "INTEGER, missing TEXT); CREATE TABLE audit(title TEXT); CREATE TRIGGER log AFTER "
                "INSERT ON songs BEGIN INSERT INTO audit VALUES(CASE WHEN new.title='a;b' THEN "
                "'semicolon;ok' ELSE new.title END); END;"}}};
    auto *db = openDb(host, *scope, {{"migrations", migrations}});
    QVERIFY(db);
    QCOMPARE(db->version(), 1);
    host.engine.globalObject().setProperty("sqlDb", host.engine.newQObject(db));
    // Exercise the real QJSEngine argument/result path (nested QVariant/BLOB and
    // QObject result handles), rather than only native calls.
    auto operation = js(host, "sqlDb.run('INSERT INTO songs(title,bytes,n,missing) "
                              "VALUES(?,?,?,?)',['a;b',{type:'base64',data:'AP8Q'},{type:'int64',"
                              "data:'9223372036854775807'},null])");
    auto result = settle(host, qobject_cast<EchoPluginOperation *>(operation.toQObject()));
    QCOMPARE(result.state, EchoPluginOperation::Succeeded);
    QCOMPARE(result.value.toMap().value("changes").toInt(), 1);
    result = settle(host, db->get("SELECT title,bytes,n,missing FROM songs"));
    QCOMPARE(result.state, EchoPluginOperation::Succeeded);
    const auto row = result.value.toMap();
    QCOMPARE(row.value("title").toString(), "a;b");
    QCOMPARE(row.value("bytes").toMap().value("data").toString(), "00ff10");
    QCOMPARE(row.value("n").toMap().value("data").toString(), "9223372036854775807");
    QVERIFY(row.value("missing").isNull());
    const auto emptyBlob = settle(host, db->get("SELECT x'' AS bytes, ? AS empty",
                                                {QVariantMap{{"type", "hex"}, {"data", ""}}}));
    QCOMPARE(emptyBlob.state, EchoPluginOperation::Succeeded);
    QCOMPARE(emptyBlob.value.toMap().value("bytes").toMap().value("type").toString(), "hex");
    QCOMPARE(emptyBlob.value.toMap().value("empty").toMap().value("type").toString(), "hex");
    QCOMPARE(
        settle(host, db->get("SELECT title FROM audit")).value.toMap().value("title").toString(),
        "semicolon;ok");
    result =
        settle(host, db->transaction({QVariantMap{{"sql", "INSERT INTO songs(title) VALUES(?)"},
                                                  {"params", QVariantList{"good"}}},
                                      QVariantMap{{"sql", "UPDATE songs SET title=? WHERE title=?"},
                                                  {"params", QVariantList{"renamed", "good"}}}}));
    QCOMPARE(result.state, EchoPluginOperation::Succeeded);
    QCOMPARE(result.value.toList().size(), 2);
    result = settle(host, db->all("SELECT title FROM songs ORDER BY id", {}, {{"limit", 1}}));
    QCOMPARE(result.state, EchoPluginOperation::Succeeded);
    QCOMPARE(result.value.toMap().value("rowCount").toInt(), 1);
    QVERIFY(result.value.toMap().value("truncated").toBool());
    result = settle(host, db->get("SELECT title FROM songs WHERE 0"));
    QCOMPARE(result.state, EchoPluginOperation::Succeeded);
    QVERIFY(result.value.isNull());
    result = settle(
        host,
        db->transaction({QVariantMap{{"sql", "INSERT INTO songs(title) VALUES('rolled back')"}},
                         QVariantMap{{"sql", "INSERT INTO songs(title) VALUES('a;b')"}}}));
    QCOMPARE(result.state, EchoPluginOperation::Failed);
    QCOMPARE(
        settle(host, db->get("SELECT count(*) AS n FROM songs")).value.toMap().value("n").toInt(),
        2);
    QCOMPARE(settle(host, db->close()).state, EchoPluginOperation::Succeeded);
    QVERIFY(!db->active());
    auto *reopened = openDb(host, *scope, {{"migrations", migrations}});
    QVERIFY(reopened);
    QCOMPARE(reopened->version(), 1);
    QCOMPARE(settle(host, reopened->get("SELECT count(*) AS n FROM songs"))
                 .value.toMap()
                 .value("n")
                 .toInt(),
             2);
}
void PluginRuntimeTest::sqliteValidationReadOnlyAndRollback()
{
    Host host;
    auto *scope = sqlScope(host);
    QVERIFY(scope);
    auto *db = openDb(host, *scope);
    QVERIFY(db);
    QCOMPARE(settle(host, db->exec("CREATE TABLE t(v INTEGER)")).state,
             EchoPluginOperation::Succeeded);
    for (const auto &sql :
         {"ATTACH DATABASE '/tmp/escape' AS x", "VACUUM INTO '/tmp/escape'",
          "SELECT \"load_extension\"('x')", "PRAGMA [writable_schema]=ON", "PRAGMA user_version(4)",
          "PRAGMA foreign_keys(0)", "PRAGMA main.user_version=8",
          "BEGIN; INSERT INTO t VALUES(1); COMMIT", "SELECT 1; ATTACH 'x' AS y"}) {
        const auto rejected = settle(host, db->exec(sql));
        QCOMPARE(rejected.state, EchoPluginOperation::Failed);
    }
    QCOMPARE(settle(host, db->run("INSERT INTO t VALUES(?)",
                                  {QVariantMap{{"type", "hex"}, {"data", "xyz"}}}))
                 .state,
             EchoPluginOperation::Failed);
    QCOMPARE(settle(host, db->get("SELECT ?", {QVariantMap{{"type", "int64"},
                                                           {"data", "9223372036854775808"}}}))
                 .state,
             EchoPluginOperation::Failed);
    QCOMPARE(settle(host, db->get("SELECT 1 AS x, 2 AS x")).state, EchoPluginOperation::Failed);
    QCOMPARE(settle(host, scope->sqlite()->deleteDatabase("main")).state,
             EchoPluginOperation::Failed);
    QCOMPARE(settle(host, scope->sqlite()->open({{"name", "../values"}})).state,
             EchoPluginOperation::Failed);
    QCOMPARE(settle(host, db->close()).state, EchoPluginOperation::Succeeded);
    const QVariantList badMigrations{
        QVariantMap{{"version", 1}, {"sql", "CREATE TABLE first(v)"}},
        QVariantMap{{"version", 2}, {"sql", "INSERT INTO missing VALUES(1)"}}};
    QCOMPARE(settle(host, scope->sqlite()->open({{"migrations", badMigrations}})).state,
             EchoPluginOperation::Failed);
    db = openDb(host, *scope, {{"readOnly", true}});
    QVERIFY(db);
    QCOMPARE(db->version(), 0);
    QVERIFY(
        settle(host, db->get("SELECT name FROM sqlite_master WHERE name='first'")).value.isNull());
    QCOMPARE(settle(host, db->run("INSERT INTO t VALUES(1)")).state, EchoPluginOperation::Failed);
    QCOMPARE(settle(host, db->all("INSERT INTO t VALUES(1) RETURNING v")).state,
             EchoPluginOperation::Failed);
    QCOMPARE(settle(host, db->close()).state, EchoPluginOperation::Succeeded);
    const QVariantList arrayMigration{QVariantMap{
        {"version", 1},
        {"sql", QVariantList{"CREATE TABLE array_sql(v)", "INSERT INTO array_sql VALUES(7)"}}}};
    auto *arrayDb = openDb(host, *scope, {{"migrations", arrayMigration}});
    QVERIFY(arrayDb);
    QCOMPARE(arrayDb->version(), 1);
    QCOMPARE(settle(host, arrayDb->get("SELECT v FROM array_sql")).value.toMap().value("v").toInt(),
             7);
    const auto arrayRoot = host.engine.newQObject(arrayDb);
    QCOMPARE(settle(host, arrayDb->close()).state, EchoPluginOperation::Succeeded);
    const auto repeatedClose = settle(host, arrayDb->close());
    QCOMPARE(repeatedClose.state, EchoPluginOperation::Succeeded);
    QVERIFY(!repeatedClose.value.toBool());
    auto list = settle(host, scope->sqlite()->listDatabases());
    QCOMPARE(list.value.toList().size(), 1);
    const auto deleted = settle(host, scope->sqlite()->deleteDatabase("main"));
    QCOMPARE(deleted.state, EchoPluginOperation::Succeeded);
    QVERIFY(deleted.value.toBool());
    QCOMPARE(settle(host, scope->sqlite()->listDatabases()).value.toList().size(), 0);
}
void PluginRuntimeTest::sqliteIsolationCancellationAndRetirement()
{
    Host host;
    auto *one = sqlScope(host, "sql-one");
    auto *two = sqlScope(host, "sql-two");
    QVERIFY(one && two);
    auto *db = openDb(host, *one);
    auto *other = openDb(host, *two);
    QVERIFY(db && other);
    QCOMPARE(settle(host, db->exec("CREATE TABLE t(v); INSERT INTO t VALUES('one')")).state,
             EchoPluginOperation::Succeeded);
    QCOMPARE(settle(host, other->exec("CREATE TABLE t(v); INSERT INTO t VALUES('two')")).state,
             EchoPluginOperation::Succeeded);
    QCOMPARE(settle(host, db->get("SELECT v FROM t")).value.toMap().value("v").toString(), "one");
    QCOMPARE(settle(host, other->get("SELECT v FROM t")).value.toMap().value("v").toString(),
             "two");
    const auto before = one->sqlite()->open({{"name", "canceled"}});
    const auto keep = host.engine.newQObject(before);
    before->cancel();
    QCOMPARE(settle(host, before).state, EchoPluginOperation::Canceled);
    // The next successful open proves canceled-open connections were released.
    auto *again = openDb(host, *one, {{"name", "canceled"}});
    QVERIFY(again);
    auto *pending = db->get("SELECT v FROM t");
    const auto root = host.engine.newQObject(pending);
    QVERIFY(host.api.setAccount(42, "fixture-token", "fixture-refresh"));
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    QVERIFY(db->active()); // installation data is shared, unlike result delivery
    auto childOwner = std::make_unique<QObject>();
    auto *child = one->createPresentation(childOwner.get(), "page");
    QVERIFY(child);
    auto *childDb = openDb(host, *child);
    QVERIFY(childDb);
    QCOMPARE(settle(host, one->sqlite()->deleteDatabase("main")).state,
             EchoPluginOperation::Failed);
    QCOMPARE(settle(host, db->close()).state, EchoPluginOperation::Succeeded);
    QCOMPARE(settle(host, one->sqlite()->deleteDatabase("main")).state,
             EchoPluginOperation::Failed); // child still owns a connection
    QPointer<EchoPluginContext> childGuard(child);
    childOwner.reset();
    QTRY_VERIFY(!childGuard);
    QCOMPARE(settle(host, one->sqlite()->deleteDatabase("main")).state,
             EchoPluginOperation::Succeeded);
    const auto install = one->installationId();
    QVERIFY(host.runtime.setEnabled(install, false));
    QTRY_COMPARE(host.runtime.state(install), PluginRuntime::Disabled);
    QCOMPARE(settle(host, other->get("SELECT v FROM t")).value.toMap().value("v").toString(),
             "two");
    QVERIFY(host.runtime.setEnabled(install, true));
    QTRY_COMPARE(host.runtime.state(install), PluginRuntime::Active);
    auto *restarted = host.runtime.context(install);
    QVERIFY(openDb(host, *restarted, {{"name", "canceled"}}));
    auto *noCapability = ecosystem(host, "", "sql-denied");
    QVERIFY(noCapability);
    QCOMPARE(settle(host, noCapability->sqlite()->open()).state, EchoPluginOperation::Failed);
}
void PluginRuntimeTest::sqliteUpdateFailureRestoresManagedDatabase()
{
    Host host;
    const auto source = pluginPackage(host.temporary.path(), "sql-update",
                                      R"(import {awaitOperation as wait} from 'echo:plugin-sdk';
export function activate(c) { return wait(c.sqlite.open({migrations:[{version:1,sql:"CREATE TABLE IF NOT EXISTS t(v); INSERT INTO t VALUES(1);"}]})).then(function(db){ c.registerCommand('read',function(){return wait(db.get('SELECT sum(v) AS n FROM t'));}); }); })",
                                      {{"capabilities", QJsonArray{"sqlite"}}});
    QSignalSpy finished(&host.runtime, &PluginRuntime::managementFinished);
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished[0][1].toBool());
    const auto install = host.runtime.installationForId("sql-update");
    QFile manifest(source + "/manifest.json");
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    auto data = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    data["version"] = "2.0.0";
    write(source + "/manifest.json", QJsonDocument(data).toJson());
    write(
        source + "/entry.mjs",
        R"(import {awaitOperation as wait} from 'echo:plugin-sdk'; export function activate(c) {return wait(c.sqlite.open({migrations:[{version:2,sql:'INSERT INTO t VALUES(200); CREATE TABLE failed(v);'}]})).then(function(){throw new Error('failed database update');});})");
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(!finished[1][1].toBool());
    QCOMPARE(host.runtime.state(install), PluginRuntime::Active);
    const auto result = settle(host, host.runtime.executeCommand(install, "read"));
    QCOMPARE(result.state, EchoPluginOperation::Succeeded);
    QCOMPARE(result.value.toMap().value("n").toInt(), 1);
    auto *scope = host.runtime.context(install);
    QVERIFY(scope);
    auto *readonlyDb =
        openDb(host, *scope->createPresentation(&host.engine, "test"),
               {{"readOnly", true}}); // separate scope opens the same installation data
    QVERIFY(readonlyDb);
    QCOMPARE(readonlyDb->version(), 1);
    QVERIFY(settle(host, readonlyDb->get("SELECT name FROM sqlite_master WHERE name='failed'"))
                .value.isNull());
}
