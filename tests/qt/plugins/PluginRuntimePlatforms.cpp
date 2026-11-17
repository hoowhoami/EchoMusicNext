#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "PluginDependencyGraph.h"

namespace
{
QString foreignPlatform()
{
    return PluginPackage::currentPlatform() == "windows" ? "linux" : "windows";
}
QJsonObject manifestAt(const QString &directory)
{
    QFile file(directory + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read platform fixture");
    return QJsonDocument::fromJson(file.readAll()).object();
}
QJsonObject onPlatforms(QJsonObject manifest, const QJsonValue &platforms)
{
    auto requirements = manifest["requires"].toObject();
    requirements["platforms"] = platforms;
    auto features = requirements["features"].toArray();
    if (!features.contains("manifest.platforms"))
        features.append("manifest.platforms");
    requirements["features"] = features;
    manifest["requires"] = requirements;
    return manifest;
}
// A small stored ZIP fixture exercises the production .echomod extractor on
// every platform without depending on an external archiver or Node runtime.
QByteArray storedArchive(const QJsonObject &manifest)
{
    QByteArray result, central;
    QDataStream out(&result, QIODevice::WriteOnly), directory(&central, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    directory.setByteOrder(QDataStream::LittleEndian);
    const QList<QPair<QByteArray, QByteArray>> files{
        {"manifest.json", QJsonDocument(manifest).toJson()},
        {"entry.mjs", "throw new Error('foreign code must never execute');"}};
    for (const auto &[name, bytes] : files) {
        quint32 crc = 0xffffffff;
        for (const auto byte : bytes) {
            crc ^= quint8(byte);
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        crc = ~crc;
        const auto offset = quint32(result.size()), size = quint32(bytes.size());
        out << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0) << quint16(0)
            << quint16(0) << crc << size << size << quint16(name.size()) << quint16(0);
        out.writeRawData(name.constData(), name.size());
        out.writeRawData(bytes.constData(), bytes.size());
        directory << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0) << quint16(0)
                  << quint16(0) << quint16(0) << crc << size << size << quint16(name.size())
                  << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << offset;
        directory.writeRawData(name.constData(), name.size());
    }
    const auto offset = quint32(result.size());
    out.writeRawData(central.constData(), central.size());
    out << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(files.size())
        << quint16(files.size()) << quint32(central.size()) << offset << quint16(0);
    return result;
}
} // namespace

void PluginRuntimeTest::platformManifestValidationAndDependencies()
{
    QTemporaryDir temporary;
    const auto manifest = manifestAt(package(temporary.path(), "export function activate(){}"));
    const auto inspect = [](const QJsonObject &value) {
        return PluginPackage::inspectManifest(QJsonDocument(value).toJson());
    };
    QVERIFY(inspect(manifest).error.isEmpty());
    QVERIFY(inspect(manifest).platforms.isEmpty());
    for (const auto &platform : {"windows", "macos", "linux"}) {
        const auto result = inspect(onPlatforms(manifest, QJsonArray{platform}));
        QCOMPARE(result.platforms, QStringList{platform});
        QCOMPARE(result.incompatible, platform != PluginPackage::currentPlatform());
        QCOMPARE(result.error.isEmpty(), !result.incompatible);
        QVERIFY(!result.platformLabel().isEmpty());
    }
    QVERIFY(
        inspect(onPlatforms(manifest, QJsonArray{"windows", "macos", "linux"})).error.isEmpty());
    for (const auto &platforms :
         QList<QJsonValue>{QJsonArray{}, QJsonArray{"Windows"}, QJsonArray{"windows", "windows"},
                           QJsonArray{1}, "windows", QJsonValue::Null}) {
        const auto result = inspect(onPlatforms(manifest, platforms));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.incompatible); // Malformed metadata is invalid, not another OS.
    }
    auto missingFeature = onPlatforms(manifest, QJsonArray{PluginPackage::currentPlatform()});
    auto requirements = missingFeature["requires"].toObject();
    requirements["features"] = QJsonArray{"lifecycle"};
    missingFeature["requires"] = requirements;
    QVERIFY(inspect(missingFeature).error.contains("manifest.platforms"));
    auto misspelled = manifest;
    auto unknown = misspelled["requires"].toObject();
    unknown["platform"] = QJsonArray{"windows"};
    misspelled["requires"] = unknown;
    QVERIFY(inspect(misspelled).error.contains("compatibility field"));
    auto provider = inspect(onPlatforms(manifest, QJsonArray{foreignPlatform()}));
    provider.id = "provider";
    provider.dependencies = {{"another-platform-component", ">=1.0.0"}};
    auto consumer = inspect(manifest);
    consumer.id = "consumer";
    consumer.dependencies = {{"provider", ">=1.0.0"}};
    for (const auto &enabled : {QSet<QString>{"consumer", "provider"}, QSet<QString>{"consumer"}}) {
        const auto plan = PluginDependencyGraph::resolve({consumer, provider}, enabled);
        QVERIFY(plan.errors.value("consumer").contains(provider.error));
    }
    consumer.optionalDependencies = consumer.dependencies;
    consumer.dependencies.clear();
    QVERIFY(PluginDependencyGraph::resolve({consumer, provider}, {"consumer"}).errors.isEmpty());
    QVERIFY(!PluginDependencyGraph::resolve({consumer, provider}, {"consumer", "provider"})
                 .errors.value("consumer")
                 .isEmpty());
}

void PluginRuntimeTest::platformInstallationUpdateAndStartup()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    const auto source =
        package(host.temporary.path(),
                "export function activate(c){ c.ui.setState({marker:'original'}); }");
    const auto original =
        onPlatforms(manifestAt(source), QJsonArray{PluginPackage::currentPlatform()});
    write(source + "/manifest.json", QJsonDocument(original).toJson());
    QSignalSpy complete(&host.runtime, &PluginRuntime::managementFinished);
    QSignalSpy notices(&manager, &PluginManager::notice);
    QVERIFY(manager.installLocal(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!manager.busy());
    const auto installation = host.installation();
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *context = host.runtime.context(installation);
    const auto root = context->package().root;
    const PluginPaths paths(host.runtime.directory());
    write(paths.data("test.runtime") + "/kept.txt", "private data");
    auto incompatible = onPlatforms(original, QJsonArray{foreignPlatform()});
    incompatible["version"] = "2.0.0";
    const auto error = PluginPackage::inspectManifest(QJsonDocument(incompatible).toJson()).error;
    write(source + "/manifest.json", QJsonDocument(incompatible).toJson());
    write(source + "/entry.mjs", "throw new Error('foreign code must never execute');");
    QVERIFY(manager.installLocal(QUrl::fromLocalFile(source)));
    QVERIFY(!manager.busy());
    QVERIFY(notices.last().first().toString().contains(error));
    QCOMPARE(host.runtime.context(installation), context);
    QCOMPARE(context->package().root, root);
    QCOMPARE(context->package().version, "1.0.0");
    QCOMPARE(context->ui()->state().value("marker").toString(), "original");
    const auto fresh = pluginPackage(host.temporary.path(), "platform-unavailable",
                                     "throw new Error('foreign code must never execute');",
                                     {{"requires", incompatible["requires"]}});
    QVERIFY(manager.installLocal(QUrl::fromLocalFile(fresh)));
    QVERIFY(!manager.busy());
    QCOMPARE(host.runtime.rowCount(), 1);
    QVERIFY(!QFileInfo::exists(paths.code("platform-unavailable")));
    QVERIFY(!QFileInfo::exists(paths.data("platform-unavailable")));
    const auto archive = host.temporary.path() + "/foreign.echomod";
    write(archive, storedArchive(incompatible));
    QVERIFY(manager.installLocal(QUrl::fromLocalFile(archive)));
    QTRY_VERIFY(!manager.busy());
    QVERIFY(notices.last().first().toString().contains(error));
    // Direct loader calls cannot bypass the manager's preflight either.
    QVERIFY(host.runtime.installDirectory(archive));
    QTRY_COMPARE(complete.count(), 2); // Initial install plus rejected archive.
    QVERIFY(!complete.last()[1].toBool());
    QVERIFY(complete.last()[2].toString().contains(error));
    QCOMPARE(host.runtime.context(installation), context);
    QVERIFY(QFileInfo::exists(paths.data("test.runtime") + "/kept.txt"));
    QVERIFY(host.runtime.record(installation).value("failure").toMap().isEmpty());
    manager.shutdown();

    // Reopen a real installed catalog under an incompatible manifest, as when a
    // profile is restored on another OS. No activation or safe mode is allowed.
    Host restarted([&](const QString &directory) {
        const PluginPaths next(directory);
        write(next.code("test.runtime") + "/manifest.json", QJsonDocument(incompatible).toJson());
        write(next.code("test.runtime") + "/entry.mjs", "throw new Error('must not execute');");
        write(next.data("test.runtime") + "/kept.txt", "private data");
        auto record = PluginInstallationStore(host.runtime.directory()).records().first().toMap();
        auto consumer = original;
        consumer["id"] = "platform-consumer";
        consumer["dependencies"] = QJsonObject{{"test.runtime", ">=1.0.0"}};
        write(next.code("platform-consumer") + "/manifest.json", QJsonDocument(consumer).toJson());
        write(next.code("platform-consumer") + "/entry.mjs",
              "throw new Error('dependent must not execute');");
        auto consumerRecord = record;
        consumerRecord["id"] = "platform-consumer";
        consumerRecord["installation"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto failure = PluginInstallationStore(directory).save({record, consumerRecord});
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    });
    QCOMPARE(restarted.runtime.state(installation), PluginRuntime::Incompatible);
    QVERIFY(!restarted.runtime.context(installation));
    QVERIFY(restarted.runtime.record(installation).value("error").toString().contains(error));
    QVERIFY(!restarted.runtime.setEnabled(installation, true));
    const auto consumer = restarted.runtime.installationForId("platform-consumer");
    QCOMPARE(restarted.runtime.state(consumer), PluginRuntime::Blocked);
    QVERIFY(restarted.runtime.record(consumer).value("error").toString().contains(error));
    QVERIFY(!restarted.runtime.context(consumer));
    QVERIFY(restarted.runtime.record(consumer).value("failure").toMap().isEmpty());
    QVERIFY(QFileInfo::exists(PluginPaths(restarted.runtime.directory()).data("test.runtime") +
                              "/kept.txt"));
    QVERIFY(!restarted.runtime.safeMode());
    QVERIFY(restarted.runtime.record(installation).value("failure").toMap().isEmpty());
    QCOMPARE(PluginInstallationStore(restarted.runtime.directory())
                 .records()
                 .first()
                 .toMap()
                 .value("enabled")
                 .toBool(),
             true);
}

void PluginRuntimeTest::platformCatalogCacheAndProductionCard()
{
    Host host;
    const auto source = package(host.temporary.path(), "export function activate(){}");
    auto manifest = onPlatforms(manifestAt(source), QJsonArray{foreignPlatform()});
    manifest["version"] = "2.0.0";
    manifest["distribution"] =
        QJsonObject{{"artifact", "artifacts/test.echomod"}, {"sha256", QString(64, '0')}};
    QByteArray publication = QJsonDocument(manifest).toJson();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    int downloads = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            const auto request = socket->property("bytes").toByteArray() + socket->readAll();
            socket->setProperty("bytes", request);
            if (!request.contains("\r\n\r\n") || socket->property("answered").toBool())
                return;
            socket->setProperty("answered", true);
            downloads += request.contains(".echomod") ? 1 : 0;
            const auto body =
                request.startsWith("GET /echo-plugins.json ")
                    ? QByteArray(
                          R"({"schemaVersion":2,"plugins":[{"id":"test.runtime","path":"plugins/test"}]})")
                    : publication;
            socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    });
    PluginManager manager(host.runtime, host.settings);
    const auto sourceId = manager.m_sources.first().toMap().value("id");
    QVERIFY(host.runtime.installDirectory(
        source, {{"kind", "marketplace"},
                 {"id", sourceId},
                 {"repository",
                  manager.m_sources.first().toMap().value("repository").toString().toLower()}}));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    manager.m_resolveUrl = [&](const QUrl &url) {
        return QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) +
                    (url.path().endsWith("echo-plugins.json") ? "/echo-plugins.json"
                     : url.path().endsWith(".echomod")        ? "/package.echomod"
                                                              : "/manifest.json"));
    };
    manager.refreshCatalog();
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(manager.catalogCount(), 1);
    auto row = manager.catalog()->data(manager.catalog()->index(0), Qt::UserRole + 1).toMap();
    QVERIFY(!row.value("compatible").toBool());
    QCOMPARE(row.value("platforms").toStringList(), QStringList{foreignPlatform()});
    QVERIFY(row.value("error").toString().contains(row.value("platformLabel").toString()));
    QVERIFY(!manager.install(row.value("key").toString()));
    QCOMPARE(manager.updateCount(), 0);
    QVERIFY(!row.value("updateAvailable").toBool());
    QCOMPARE(downloads, 0);

    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import EchoMusic.Plugins
import EchoMusic.Theme
Window { id:win; required property PluginManager manager; required property var record
 width:660; height:650; visible:true; color:Theme.main
 PluginCard { x:20; y:20; width:win.width-40; manager:win.manager; record:win.record; marketplace:true }
})",
                      QUrl());
    auto uninstalled = row;
    uninstalled["installed"] = false;
    std::unique_ptr<QObject> card(component.createWithInitialProperties(
        {{"manager", QVariant::fromValue(&manager)}, {"record", uninstalled}}));
    QVERIFY2(card, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(card.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *root = window->contentItem();
    auto *support = visual(root, "pluginCardPlatforms");
    QVERIFY(support && !support->isVisible());
    auto *reason = visual(root, "pluginCardWarningContent");
    QVERIFY(reason && reason->isVisible());
    QVERIFY(reason->property("text").toString().contains(row.value("platformLabel").toString()));
    auto *install = visual(root, "pluginMarketplaceInstall");
    QVERIFY(install && !install->isEnabled());
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        host.theme.setMode(mode);
        for (const auto width : {420, 660}) {
            window->setWidth(width);
            QTest::qWait(25);
            QVERIFY(reason->height() > 0 && reason->width() > 0);
            const auto image = window->grabWindow();
            QVERIFY(!image.isNull());
            const auto captures = qEnvironmentVariable("ECHO_PLUGIN_IMPROVEMENT_CAPTURES");
            if (!captures.isEmpty()) {
                QDir().mkpath(captures);
                QVERIFY(image.save(captures + "/platform-" + QString::number(int(mode)) + "-" +
                                   QString::number(width) + ".png"));
            }
        }
    }

    // Both lies in portable cache metadata must be overwritten from the raw
    // manifest. Distribution is outside that cached install manifest.
    auto cached = row;
    cached["compatible"] = true;
    cached["error"] = "";
    auto universal = row;
    auto universalManifest = universal.value("manifest").toMap();
    auto requirements = universalManifest.value("requires").toMap();
    requirements.remove("platforms");
    universalManifest["requires"] = requirements;
    universal["manifest"] = universalManifest;
    universal["compatible"] = false;
    universal["error"] = "stale compatibility";
    const auto cache = PluginPaths(host.runtime.directory()).cache() + "/marketplace/catalog.json";
    write(cache,
          QJsonDocument::fromVariant(
              QVariantMap{{"schemaVersion", 2}, {"plugins", QVariantList{cached, universal}}})
              .toJson());
    PluginManager restored(host.runtime, host.settings);
    QCOMPARE(restored.m_entries.size(), 2);
    QVERIFY(!restored.m_entries[0].toMap().value("compatible").toBool());
    QVERIFY(restored.m_entries[1].toMap().value("compatible").toBool());
    QVERIFY(restored.m_entries[1].toMap().value("platforms").toStringList().isEmpty());
    // The same source/version becomes an available update on a supported OS.
    // This also exercises replacing a previous negative catalog result.
    auto supported = manifest["requires"].toObject();
    supported["platforms"] = QJsonArray{PluginPackage::currentPlatform()};
    manifest["requires"] = supported;
    publication = QJsonDocument(manifest).toJson();
    manager.refreshCatalog(true);
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(manager.updateCount(), 1);
    row = manager.catalog()->data(manager.catalog()->index(0), Qt::UserRole + 1).toMap();
    QVERIFY(row.value("compatible").toBool());
    QVERIFY(row.value("updateAvailable").toBool());
    QVERIFY(card->setProperty("record", row));
    QTRY_VERIFY(install->isEnabled());
    restored.shutdown();
    manager.shutdown();
}
