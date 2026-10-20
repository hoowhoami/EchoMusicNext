#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

void PluginRuntimeTest::sharedPluginVersionWarnings_data()
{
    QTest::addColumn<QString>("sharedVersion");
    QTest::addColumn<bool>("compatible");
    QTest::newRow("same") << QString("1.0.0") << true;
    QTest::newRow("source-newer") << QString("0.9.0") << true;
    QTest::newRow("source-older") << QString("2.0.0") << true;
    QTest::newRow("incompatible") << QString("1.0.0") << false;
    QTest::newRow("version-warning-first") << QString("2.0.0") << false;
}

void PluginRuntimeTest::sharedPluginVersionWarnings()
{
    QFETCH(QString, sharedVersion);
    QFETCH(bool, compatible);
    Host host;
    PluginManager manager(host.runtime, host.settings);
    const auto source = manager.m_sources.first().toMap();
    manager.m_entries = {QVariantMap{{"id", "test.share"},
                                     {"path", "plugins/share"},
                                     {"sourceId", source.value("id")},
                                     {"version", "1.0.0"},
                                     {"compatible", compatible},
                                     {"error", "缺少所需能力"}}};
    manager.m_shareTarget = {{"id", "test.share"},
                             {"path", "plugins/share"},
                             {"sourceId", source.value("id")},
                             {"version", sharedVersion}};
    manager.resolveShared();
    QCOMPARE(manager.shareState(), "ready");
    QVERIFY(manager.shareError().isEmpty());
    QCOMPARE(manager.shareRecord().value("sharedVersion").toString(), sharedVersion);
    const auto expected =
        sharedVersion != "1.0.0"
            ? QString("分享时版本为 v%1，当前插件源提供 v1.0.0。").arg(sharedVersion)
        : compatible ? QString()
                     : QString("缺少所需能力");
    QCOMPARE(manager.shareRecord().value("shareWarning").toString(), expected);
    if (!compatible)
        QVERIFY(!manager.installShared());
}

void PluginRuntimeTest::sharedPluginPagePresentationAndRedirect()
{
    Host host;
    host.theme.setMotionEnabled(false);
    PluginManager manager(host.runtime, host.settings);
    const auto source = manager.m_sources.first().toMap();
    QImage artwork(64, 64, QImage::Format_RGB32);
    artwork.fill(QColor("#cf4084"));
    const auto iconPath = host.temporary.path() + "/icon.png";
    QVERIFY(artwork.save(iconPath));
    QVariantMap entry{{"id", "test.share"},
                      {"key", "share"},
                      {"path", "plugins/share"},
                      {"sourceId", source.value("id")},
                      {"sourceName", "EchoMusicNext 插件源"},
                      {"sourceUrl", source.value("url")},
                      {"name", "分享插件"},
                      {"description", "展示插件图标与完整描述。这里保留插件源提供的说明，长内容"
                                      "会自然换行，并可随页面滚动。"},
                      {"iconUrl", QUrl::fromLocalFile(iconPath)},
                      {"version", "1.0.0"},
                      {"compatible", true}};
    manager.m_entries = {entry};
    manager.m_shareTarget = {{"id", "test.share"},
                             {"path", "plugins/share"},
                             {"sourceId", source.value("id")},
                             {"sourceUrl", source.value("url")},
                             {"version", "2.0.0"}};
    manager.resolveShared();
    host.router.registerPage(
        {"plugin-share-resolve", "/plugins/share", QUrl("qrc:/share.qml"), {}, false, false});
    host.router.registerPage(
        {"plugin-management", "/plugins", QUrl("qrc:/plugins.qml"), {"view"}, true, false});
    QVERIFY(host.router.navigate("plugin-share-resolve"));
    ShellSettings settings(host.settings);
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &host.engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Plugins
import EchoMusic.Shell
ApplicationWindow {
 id: win
 required property PluginManager manager
 required property RouteController router
 required property ShellSettings settings
 property real uiScale: settings.uiScale
 width: 1000; height: 900; visible: true; color: Theme.main
 Item {
  width: win.width / win.uiScale; height: win.height / win.uiScale
  scale: win.uiScale; transformOrigin: Item.TopLeft
  Loader {
   anchors.fill: parent
   active: win.router.current !== null && win.router.current.name === "plugin-share-resolve"
   sourceComponent: Component {
    PluginShareResolvePage { manager: win.manager; router: win.router; context: win.router.current; settings: win.settings }
   }
  }
 }
})",
                      QUrl("qrc:/plugin-share-page-test.qml"));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"manager", QVariant::fromValue(&manager)},
                                               {"router", QVariant::fromValue(&host.router)},
                                               {"settings", QVariant::fromValue(&settings)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window && Test::prepareNativeInput(window));
    auto *description = visual(window->contentItem(), "pluginShareDescription");
    QVERIFY(description);
    QCOMPARE(description->property("text").toString(), entry.value("description").toString());
    auto *warning = visual(window->contentItem(), "pluginShareWarningContent");
    QVERIFY(warning);
    QCOMPARE(warning->property("text").toString(), "分享时版本为 v2.0.0，当前插件源提供 v1.0.0。");
    auto *icon = visual(window->contentItem(), "pluginShareIcon");
    QVERIFY(icon);
    QTRY_COMPARE(icon->property("status").toInt(), 1); // Image.Ready
    const auto output = qEnvironmentVariable("ECHO_PLUGIN_SHARE_OUTPUT");
    if (!output.isEmpty())
        QDir().mkpath(output);
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (const auto scale : {1.0, 1.25})
            for (const int width : {1000, 480}) {
                host.theme.setMode(mode);
                settings.resetZoom();
                if (scale > 1)
                    for (int step = 0; step < 5; ++step)
                        settings.zoomIn();
                QCOMPARE(settings.uiScale(), scale);
                window->setWidth(width);
                QTest::qWait(100);
                const auto image = window->grabWindow();
                QVERIFY(!image.isNull());
                // Prove the real masked media rendered, not just Image.Ready.
                const auto point = icon->mapToScene({28, 28}) * image.devicePixelRatio();
                // The offscreen software backend omits MultiEffect media;
                // native runs must verify these real pixels for acceptance.
                if (QGuiApplication::platformName() != "offscreen")
                    QCOMPARE(image.pixelColor(qRound(point.x()), qRound(point.y())),
                             QColor("#cf4084"));
                auto *panel = visual(window->contentItem(), "pluginSharePanel");
                QVERIFY(panel && panel->width() > 0);
                QVERIFY(panel->mapToScene({0, 0}).x() >= 0);
                if (!output.isEmpty())
                    QVERIFY(image.save(
                        output +
                        QString("/share-%1-%2-%3.png").arg(int(mode)).arg(scale).arg(width)));
                if (width == 480) {
                    auto *viewport = visual(window->contentItem(), "pluginShareViewport");
                    QVERIFY(viewport);
                    viewport->setProperty("contentY", viewport->property("contentHeight").toReal() -
                                                          viewport->height());
                    QTest::qWait(50);
                    auto *action = visual(window->contentItem(), "pluginShareInstall");
                    QVERIFY(action);
                    const auto bottom = action->mapToScene({0, action->height()});
                    QVERIFY(bottom.y() <= window->height());
                    if (!output.isEmpty())
                        QVERIFY(window->grabWindow().save(
                            output + QString("/share-bottom-%1-%2.png").arg(int(mode)).arg(scale)));
                    viewport->setProperty("contentY", 0);
                }
            }
    entry["iconUrl"] = "";
    entry["description"] = "";
    entry["name"] = " alpha";
    manager.m_entries = {entry};
    manager.resolveShared();
    QCOMPARE(description->property("text").toString(), "暂无描述");
    auto *fallback = visual(window->contentItem(), "pluginShareIconFallback");
    QVERIFY(fallback && fallback->isVisible());
    QCOMPARE(fallback->property("text").toString(), "A");
    entry["name"] = "";
    entry["compatible"] = false;
    entry["error"] = "缺少所需能力";
    manager.m_shareTarget["version"] = "1.0.0";
    manager.m_entries = {entry};
    manager.resolveShared();
    QCOMPARE(fallback->property("text").toString(), "E");
    auto *install = visual(window->contentItem(), "pluginShareInstall");
    QVERIFY(install && !install->isEnabled());
    QCOMPARE(warning->property("text").toString(), "缺少所需能力");
    // A real installed fixture determines the installed branch, including
    // its source-owned version/update decision.
    const auto packagePath =
        pluginPackage(host.temporary.path(), "test.share", "export function activate() {} ");
    QVERIFY(host.runtime.installDirectory(packagePath,
                                          {{"kind", "marketplace"}, {"id", source.value("id")}}));
    QTRY_VERIFY(!host.runtime.installationForId("test.share").isEmpty());
    entry["compatible"] = true;
    entry["error"] = "";
    manager.m_entries = {entry};
    manager.resolveShared();
    QCOMPARE(manager.shareState(), "installed");
    auto *timer = window->findChild<QObject *>("pluginShareInstalledRedirect");
    QVERIFY(timer);
    QTRY_VERIFY(timer->property("running").toBool());
    window->hide();
    QTRY_VERIFY(!timer->property("running").toBool());
    QTest::qWait(750);
    QCOMPARE(host.router.current()->name(), "plugin-share-resolve");
    window->show();
    QTRY_VERIFY(timer->property("running").toBool());
    // A new share cancels an installed redirect even if the old wait is
    // nearly complete. Catalog resolution restarts only for its new result.
    QTest::qWait(400);
    emit manager.shareOpened();
    manager.m_shareTarget["version"] = "9.0.0";
    manager.m_shareTarget["id"] = "test.missing";
    manager.resolveShared();
    QTest::qWait(400);
    QCOMPARE(host.router.current()->name(), "plugin-share-resolve");
    manager.m_shareTarget["id"] = "test.share";
    manager.resolveShared();
    QTRY_VERIFY(timer->property("running").toBool());
    QPointer<QObject> oldPage = visual(window->contentItem(), "pluginShareResolvePage");
    QVERIFY(host.router.navigate("search"));
    QTRY_VERIFY(!oldPage);
    QTest::qWait(750);
    QCOMPARE(host.router.current()->name(), "search");
    QVERIFY(host.router.navigate("plugin-share-resolve"));
    QTest::qWait(300);
    QCOMPARE(host.router.current()->name(), "plugin-share-resolve");
    QTRY_COMPARE(host.router.current()->name(), "plugin-management");
    QCOMPARE(host.router.current()->query().value("view").toString(), "marketplace");
    QCOMPARE(manager.search(), "test.share");
    QCOMPARE(manager.sourceFilter(), source.value("id").toString());
    host.router.back();
    QCOMPARE(host.router.current()->name(), "search"); // replace removes intermediate page
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void PluginRuntimeTest::catalogInstallationSourceIsNotCompatibility_data()
{
    QTest::addColumn<QString>("originKind");
    QTest::addColumn<bool>("matchingSource");
    QTest::newRow("local") << QString("local") << false;
    QTest::newRow("other-marketplace") << QString("marketplace") << false;
    QTest::newRow("same-marketplace") << QString("marketplace") << true;
}

void PluginRuntimeTest::catalogInstallationSourceIsNotCompatibility()
{
    QFETCH(QString, originKind);
    QFETCH(bool, matchingSource);
    Host host;
    host.theme.setMotionEnabled(false);
    PluginManager manager(host.runtime, host.settings);
    const auto officialSource =
        manager.sources()->data(manager.sources()->index(0), Qt::UserRole + 1).toMap();
    const auto sourceId = officialSource.value("id").toString();
    const QVariantMap origin{{"kind", originKind},
                             {"id", matchingSource ? sourceId : "other-source"},
                             {"name", "Installed source"}};
    const auto source = pluginPackage(
        host.temporary.path(), "test.catalog-origin",
        "import {awaitOperation} from 'echo:plugin-sdk'; export function activate(c) { "
        "return awaitOperation(c.io.writeValue('kept',41)); }");
    QVERIFY(host.runtime.installDirectory(source, origin));
    QTRY_VERIFY(!host.runtime.installationForId("test.catalog-origin").isEmpty());
    const auto installation = host.runtime.installationForId("test.catalog-origin");
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    const auto generation = host.runtime.context(installation)->activationGeneration();
    QVariantMap entry{{"key", "catalog-origin"},
                      {"id", "test.catalog-origin"},
                      {"name", "频谱可视化"},
                      {"version", "2.0.0"},
                      {"compatible", true},
                      {"sourceId", sourceId},
                      {"sourceUrl", officialSource.value("url")},
                      {"sourceName", officialSource.value("name")},
                      {"path", "plugins/catalog-origin"}};
    manager.m_entries = {entry};
    manager.m_loaded = true;
    manager.rebuild();
    auto record = manager.catalog()->data(manager.catalog()->index(0), Qt::UserRole + 1).toMap();
    QVERIFY(record.value("compatible").toBool());
    QVERIFY(record.value("installed").toBool());
    QVERIFY(record.value("error").toString().isEmpty());
    QCOMPARE(record.value("updateAvailable").toBool(), matchingSource);
    QCOMPARE(record.value("installLabel").toString(),
             matchingSource ? QString("更新") : QString("已安装"));
    QCOMPARE(manager.updateCount(), matchingSource ? 1 : 0);
    manager.m_shareTarget = {{"sourceId", sourceId},
                             {"id", entry.value("id")},
                             {"path", entry.value("path")},
                             {"version", "2.0.0"}};
    manager.resolveShared();
    QCOMPARE(manager.shareState(), matchingSource ? QString("ready") : QString("installed"));
    QCOMPARE(manager.shareRecord().value("updateAvailable").toBool(), matchingSource);
    if (!matchingSource) {
        QSignalSpy notices(&manager, &PluginManager::notice);
        QVERIFY(!manager.install("catalog-origin"));
        QVERIFY(!notices.isEmpty());
        QVERIFY(!notices.last()[1].toBool());
        QVERIFY(notices.last()[2].toBool());
        manager.updateAll();
        QVERIFY(!manager.busy());
        QVERIFY(manager.m_updateQueue.isEmpty());
        QCOMPARE(host.runtime.context(installation)->activationGeneration(), generation);
        QCOMPARE(host.runtime.record(installation).value("origin").toMap(), origin);
        auto *kept = host.runtime.context(installation)->io()->readValue("kept");
        const auto guard = host.engine.newQObject(kept);
        QTRY_VERIFY(kept->finished());
        QCOMPARE(kept->result().toInt(), 41);
    }
    if (originKind != "local")
        return;
    host.router.registerPage({"plugin-management",
                              "/main/settings/plugins",
                              QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginManagementPage.qml"),
                              {"view"},
                              true,
                              false,
                              false,
                              {}});
    QVERIFY(host.router.navigate("plugin-management", {{"view", "marketplace"}}));
    ShellSettings settings(host.settings);
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &host.engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Plugins
import EchoMusic.Shell
ApplicationWindow {
 id: win
 required property PluginManager manager
 required property RouteController router
 required property ShellSettings settings
 property real uiScale: 1
 width: 500; height: 900; visible: true; color: Theme.main
 Item {
  width: win.width / win.uiScale; height: win.height / win.uiScale
  scale: win.uiScale; transformOrigin: Item.TopLeft
  PluginManagementPage { anchors.fill: parent; manager: win.manager; router: win.router; context: win.router.current; settings: win.settings }
 }
})",
                      QUrl("qrc:/plugin-origin-test.qml"));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"manager", QVariant::fromValue(&manager)},
                                               {"router", QVariant::fromValue(&host.router)},
                                               {"settings", QVariant::fromValue(&settings)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window && Test::prepareNativeInput(window));
    const auto output = qEnvironmentVariable("ECHO_PLUGIN_ORIGIN_OUTPUT");
    if (!output.isEmpty())
        QDir().mkpath(output);
    const QStringList messages{
        "", "缺少 audio.spectrum 能力",
        "缺少所需的 audio.spectrum "
        "能力，请使用提供该能力的主程序版本后再启用插件。当前界面设置与插件私有数据仍保留。"};
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (const auto scale : {1.0, 1.25})
            for (int scenario = 0; scenario < messages.size(); ++scenario) {
                host.theme.setMode(mode);
                window->setProperty("uiScale", scale);
                entry["compatible"] = scenario == 0;
                entry["error"] = messages[scenario];
                manager.m_entries = {entry};
                manager.rebuild();
                QTRY_VERIFY(visual(window->contentItem(), "pluginCard-test.catalog-origin"));
                auto *card = visual(window->contentItem(), "pluginCard-test.catalog-origin");
                auto *button = visual(card, "pluginMarketplaceInstall");
                QVERIFY(button);
                QCOMPARE(button->property("text").toString(),
                         scenario == 0 ? QString("已安装") : QString("不兼容"));
                auto *buttonLabel = visual(button, "buttonLabel");
                QVERIFY(buttonLabel);
                QTRY_VERIFY(!buttonLabel->property("truncated").toBool());
                auto *warning = visual(card, "pluginCardWarning");
                QVERIFY(warning);
                QCOMPARE(warning->isVisible(), scenario != 0);
                QTest::qWait(100);
                if (scenario != 0) {
                    auto *content = visual(warning, "pluginCardWarningContent");
                    auto *icon = visual(content, "buttonGlyph");
                    auto *label = visual(content, "buttonLabel");
                    QVERIFY(icon && label);
                    QTRY_COMPARE(icon->property("status").toInt(), 1);
                    auto *text =
                        qobject_cast<QQuickItem *>(label->property("textItem").value<QObject *>());
                    QVERIFY(text);
                    QCOMPARE(text->property("text").toString(), messages[scenario]);
                    if (scenario == 2)
                        QVERIFY(text->property("lineCount").toInt() > 1);
                    const auto image = window->grabWindow();
                    const qreal dpr = qreal(image.width()) / window->width();
                    const auto pixel =
                        warning->mapToScene({warning->width() - 4, warning->height() / 2}) * dpr;
                    const auto background = image.pixelColor(qRound(pixel.x()), qRound(pixel.y()));
                    const auto foreground = content->property("color").value<QColor>();
                    const qreal opacity = card->opacity();
                    const auto paint = QColor::fromRgbF(
                        foreground.redF() * opacity + background.redF() * (1 - opacity),
                        foreground.greenF() * opacity + background.greenF() * (1 - opacity),
                        foreground.blueF() * opacity + background.blueF() * (1 - opacity));
                    const auto iconArea = icon->mapRectToScene(icon->boundingRect());
                    const auto lineArea = text->mapRectToScene(
                        {0, 0, text->width(), label->property("lineHeight").toReal()});
                    // Measure coverage relative to this translucent card's background.
                    // A thin software-rasterized SVG can have no nearly opaque pixels
                    // along its bottom stroke, unlike a native 2x capture. Include
                    // antialiasing at >=20% coverage for both the icon and text.
                    const auto inkBounds = [&](const QRectF &area) -> std::optional<QRect> {
                        const QPointF origin = area.topLeft() * dpr;
                        const auto pixels = QRectF(origin, area.size() * dpr)
                                                .toAlignedRect()
                                                .intersected(image.rect());
                        const double red = paint.red() - background.red();
                        const double green = paint.green() - background.green();
                        const double blue = paint.blue() - background.blue();
                        const double norm = red * red + green * green + blue * blue;
                        QRect bounds;
                        for (int y = pixels.top(); y <= pixels.bottom(); ++y)
                            for (int x = pixels.left(); x <= pixels.right(); ++x) {
                                const auto color = image.pixelColor(x, y);
                                const double coverage =
                                    ((color.red() - background.red()) * red +
                                     (color.green() - background.green()) * green +
                                     (color.blue() - background.blue()) * blue) /
                                    norm;
                                if (coverage >= .2)
                                    bounds = bounds.united(QRect(x, y, 1, 1));
                            }
                        return bounds.isEmpty() ? std::nullopt : std::optional<QRect>(bounds);
                    };
                    const auto iconInk = inkBounds(iconArea);
                    const auto textInk = inkBounds(lineArea);
                    QVERIFY(iconInk && textInk);
                    const qreal delta = qAbs((iconInk->top() + iconInk->bottom()) / 2.0 -
                                             (textInk->top() + textInk->bottom()) / 2.0);
                    qInfo() << "Warning ink" << int(mode) << scale << scenario << *iconInk
                            << *textInk << "icon box center"
                            << icon->mapToScene({0, icon->height() / 2}).y() * dpr
                            << "text metric center"
                            << label->mapToScene(
                                        {0, label->property("firstLineInkCenter").toReal()})
                                       .y() *
                                   dpr;
                    if (!output.isEmpty())
                        image.save(output + QString("/warning-probe-%1-%2-%3.png")
                                                .arg(int(mode))
                                                .arg(scale)
                                                .arg(scenario));
                    QVERIFY2(
                        delta <= 1,
                        qPrintable(
                            QString("first-line rendered ink differs %1 physical px").arg(delta)));
                }
                if (!output.isEmpty())
                    QVERIFY(window->grabWindow().save(
                        output +
                        QString("/catalog-%1-%2-%3.png").arg(int(mode)).arg(scale).arg(scenario)));
            }
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void PluginRuntimeTest::rootDirectoryDiscoveryUpdatesAndRemovesOwnedInput()
{
    const QByteArray entry =
        "import {awaitOperation} from 'echo:plugin-sdk'; export function activate(c) { "
        "c.registerCommand('read', function() { return "
        "awaitOperation(c.io.readValue('value')); }); "
        "return awaitOperation(c.io.readValue('value')).then(function(v) { if (!v) return "
        "awaitOperation(c.io.writeValue('value', {version:1})); }); }";
    Host host(
        [&](const QString &root) { package(root, entry, {"lifecycle", "storage", "commands"}); });
    QCOMPARE(host.runtime.rowCount(), 1);
    const auto root = host.runtime.directory();
    const auto source = root + "/source";
    const auto installation = host.installation();
    QCOMPARE(
        host.runtime.record(installation).value("origin").toMap().value("directory").toString(),
        QFileInfo(source).canonicalFilePath());
    QVERIFY(host.runtime.context(installation)
                ->package()
                .root.startsWith(QFileInfo(root + "/packages").canonicalFilePath() + '/'));
    QVERIFY(!QDir(root + "/local").exists());
    const auto data = root + "/data/" + installation;
    QVERIFY(QFileInfo(data + "/values.sqlite").isFile());
    QSignalSpy finished(&host.runtime, &PluginRuntime::managementFinished);
    QFile manifestFile(source + "/manifest.json");
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest["version"] = "2.0.0";
    write(source + "/manifest.json", QJsonDocument(manifest).toJson());
    write(source + "/entry.mjs",
          "import {awaitOperation} from 'echo:plugin-sdk'; export function activate(c) { "
          "return awaitOperation(c.io.writeValue('value', {version:2})).then(function() { "
          "throw new Error('root update fails'); }); }");
    host.runtime.refresh();
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(finished.count(), 1);
    QVERIFY(!finished[0][1].toBool());
    QCOMPARE(host.installation(), installation);
    QCOMPARE(host.runtime.record(installation).value("version").toString(), QString("1.0.0"));
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *read = host.runtime.executeCommand(installation, "read");
    QVERIFY(read);
    QTRY_COMPARE(read->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(read->result().toMap().value("version").toInt(), 1);
    QVERIFY(!QDir(root + "/transactions/" + installation).exists());
    QVERIFY(host.runtime.uninstall(installation, false));
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.rowCount(), 0);
    QVERIFY(!QDir(source).exists());
    QVERIFY(QDir(data).exists());
    host.runtime.refresh();
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.rowCount(), 0);
    // A deliberate reinstall keeps the retained SQL identity; a complete
    // uninstall removes both the owned folder and that same private store.
    package(root, entry, {"lifecycle", "storage", "commands"});
    host.runtime.refresh();
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.installation(), installation);
    QVERIFY(host.runtime.uninstall(installation, true));
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(!QDir(source).exists());
    QVERIFY(!QDir(data).exists());
}

void PluginRuntimeTest::rootDiscoverySkipsStoresAndContinuesPastInvalidInput()
{
    Host host;
    const auto root = host.runtime.directory();
    const auto source = package(root, "export function activate(c) {}");
    QFile manifestFile(source + "/manifest.json");
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    write(root + "/aaa-invalid/manifest.json", "{}");
    for (const auto &reserved : QStringList{"packages", "data", "transactions"}) {
        manifest["id"] = "test." + reserved;
        write(root + '/' + reserved + "/manifest.json", QJsonDocument(manifest).toJson());
        write(root + '/' + reserved + "/entry.mjs",
              "export function activate(c) { throw 'internal store executed'; }");
        QVERIFY(!host.runtime.installDirectory(root + '/' + reserved));
    }
    manifest["id"] = "test.second";
    write(root + "/second/manifest.json", QJsonDocument(manifest).toJson());
    write(root + "/second/entry.mjs", "export function activate(c) {}");
    QSignalSpy finished(&host.runtime, &PluginRuntime::managementFinished);
    host.runtime.refresh();
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.rowCount(), 2);
    QVERIFY(!host.runtime.installationForId("test.runtime").isEmpty());
    QVERIFY(!host.runtime.installationForId("test.second").isEmpty());
    QCOMPARE(finished.count(), 3);
    QVERIFY(!finished[0][1].toBool());
    QVERIFY(finished[1][1].toBool());
    QVERIFY(finished[2][1].toBool());
    QVERIFY(!QDir(root + "/local").exists());
}

void PluginRuntimeTest::refreshReloadsAllInstalledPluginsAndPreservesDisabledState()
{
    Host host;
    auto source = package(host.temporary.path() + "/first", "export function activate(c) {}");
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_VERIFY(!host.runtime.loading());
    const auto first = host.runtime.installationForId("test.runtime");
    source = package(host.temporary.path() + "/second", "export function activate(c) {}");
    QFile manifestFile(source + "/manifest.json");
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest["id"] = "test.second";
    write(source + "/manifest.json", QJsonDocument(manifest).toJson());
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_VERIFY(!host.runtime.loading());
    const auto second = host.runtime.installationForId("test.second");
    const auto firstGeneration = host.runtime.context(first)->activationGeneration().toULongLong();
    const auto secondGeneration =
        host.runtime.context(second)->activationGeneration().toULongLong();
    host.runtime.refresh();
    QTRY_VERIFY(!host.runtime.loading());
    QVERIFY(host.runtime.context(first)->activationGeneration().toULongLong() > firstGeneration);
    QVERIFY(host.runtime.context(second)->activationGeneration().toULongLong() > secondGeneration);
    QVERIFY(host.runtime.setEnabled(first, false));
    QTRY_VERIFY(!host.runtime.loading());
    host.runtime.refresh();
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.state(first), PluginRuntime::Disabled);
    QVERIFY(!host.runtime.context(first));
    QVERIFY(!host.runtime.record(first).value("enabled").toBool());
    QCOMPARE(host.runtime.state(second), PluginRuntime::Active);
    QVERIFY(QDir(source).exists()); // External development inputs remain user-owned.
}

void PluginRuntimeTest::directoryActionOpensTheDisplayedRoot()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    DirectoryCapture capture;
    QDesktopServices::setUrlHandler("file", &capture, "open");
    QVERIFY(manager.openDirectory());
    QCOMPARE(capture.opened, QUrl::fromLocalFile(host.runtime.directory()));
    QVERIFY(QDir(host.runtime.directory()).exists());
    QVERIFY(!QDir(host.runtime.directory() + "/local").exists());
}

void PluginRuntimeTest::managementUpdateRollsBackPackageAndPrivateData()
{
    Host host;
    const auto source =
        package(host.temporary.path(),
                "import {awaitOperation} from 'echo:plugin-sdk'; export function activate(c) { "
                "c.registerCommand('read', function() { return "
                "awaitOperation(c.io.readValue('value')); }); return "
                "awaitOperation(c.io.readValue('value')).then(function(v) { if (!v) return "
                "awaitOperation(c.io.writeValue('value', {version:1})); }); }",
                {"lifecycle", "storage", "commands"});
    QSignalSpy finished(&host.runtime, &PluginRuntime::managementFinished);
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished[0][1].toBool());
    const auto installation = host.installation();
    const auto firstGeneration = host.runtime.context(installation)->activationGeneration();
    QFile file(source + "/manifest.json");
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    manifest["version"] = "2.0.0";
    write(source + "/manifest.json", QJsonDocument(manifest).toJson());
    write(source + "/entry.mjs",
          "import {awaitOperation} from 'echo:plugin-sdk'; export function activate(c) { "
          "return awaitOperation(c.io.writeValue('value', {version:2})).then(function(){ throw "
          "new Error('new version fails'); }); }");
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(!finished[1][1].toBool());
    QVERIFY(finished[1][2].toString().contains("new version fails"));
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QCOMPARE(host.runtime.record(installation).value("version").toString(), "1.0.0");
    QVERIFY(host.runtime.context(installation)->activationGeneration() != firstGeneration);
    auto *read = host.runtime.executeCommand(installation, "read");
    QTRY_COMPARE(read->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(read->result().toMap().value("version").toInt(), 1);
    QVERIFY(!host.settings.value("plugins/rollback/" + installation).toBool());
    QVERIFY(!QDir(host.temporary.path() + "/plugins/transactions/" + installation).exists());
    QVERIFY(host.runtime.setSafeMode(true));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QVERIFY(host.runtime.record(installation).value("enabled").toBool());
    QVERIFY(host.runtime.setSafeMode(false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QVERIFY(host.runtime.uninstall(installation, false));
    QTRY_COMPARE(finished.count(), 3);
    QCOMPARE(host.runtime.rowCount(), 0);
    QVERIFY(QFileInfo(host.temporary.path() + "/plugins/data/" + installation + "/values.sqlite")
                .isFile());
    // Reinstall retains the private-data identity, and explicit removal clears it.
    manifest["version"] = "1.0.0";
    write(source + "/manifest.json", QJsonDocument(manifest).toJson());
    write(source + "/entry.mjs", "export function activate(c) {}");
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(finished.count(), 4);
    QCOMPARE(host.installation(), installation);
    QVERIFY(host.runtime.uninstall(installation, true));
    QTRY_COMPARE(finished.count(), 5);
    QVERIFY(!QDir(host.temporary.path() + "/plugins/data/" + installation).exists());
    QVERIFY(QDir(source).exists()); // Uninstall never removes an external author workspace.
}

void PluginRuntimeTest::localBatchContinuesAfterInvalidPackage()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    const auto source = package(host.temporary.path(), "export function activate(c) {}");
    QSignalSpy finished(&host.runtime, &PluginRuntime::managementFinished);
    QSignalSpy notice(&manager, &PluginManager::notice);
    QVERIFY(manager.installLocalBatch({QUrl::fromLocalFile(host.temporary.path() + "/invalid.txt"),
                                       QUrl::fromLocalFile(source)}));
    QTRY_VERIFY(!manager.busy());
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    QVERIFY(notice.count() >= 2);
    QVERIFY(notice.last()[1].toBool());
    // A valid manifest with an unsafe link fails during worker capture. The
    // completion identity must still release manager busy after rejection.
    QVERIFY(QFile::link(source + "/entry.mjs", source + "/unsafe.mjs"));
    QVERIFY(manager.installLocal(QUrl::fromLocalFile(source)));
    QTRY_VERIFY(!manager.busy());
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(!finished.last()[1].toBool());
    QCOMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
}

void PluginRuntimeTest::arrivingInstallFilesCompleteDependencyBatch()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    const auto provider = pluginPackage(host.temporary.path(), "test.batch-provider",
                                        "export function activate(c){}");
    const auto consumer =
        pluginPackage(host.temporary.path(), "test.batch-consumer", "export function activate(c){}",
                      {{"dependencies", QJsonObject{{"test.batch-provider", "1.0.0"}}}});
    QSignalSpy finished(&host.runtime, &PluginRuntime::managementFinished);
    QVERIFY(manager.installLocalBatch({QUrl::fromLocalFile(consumer)}));
    QVERIFY(manager.busy());
    QVERIFY(manager.installLocalBatch({QUrl::fromLocalFile(provider)}));
    QTRY_COMPARE(finished.count(), 2);
    QTRY_VERIFY(!manager.busy() && !host.runtime.loading());
    for (const auto &id : {"test.batch-provider", "test.batch-consumer"})
        QCOMPARE(host.runtime.state(host.runtime.installationForId(id)), PluginRuntime::Active);
    QVERIFY(finished.first()[1].toBool());
    QVERIFY(finished.last()[1].toBool());
}

void PluginRuntimeTest::qtFileOpenQueuesBeforeServiceInitialization()
{
    ApplicationUrlHandler handler;
    const QUrl url("echomusicnext://plugin?source=https://github.com/owner/"
                   "repo&id=org.test.plugin&version=1.0.0&path=plugins/test");
    QSignalSpy delivered(&handler, &ApplicationUrlHandler::urlRequested);
    QFileOpenEvent first(url);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &first);
    const auto queuedPackage = QUrl::fromLocalFile(QDir::tempPath() + "/queued.echomod");
    QFileOpenEvent queued(queuedPackage);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &queued);
    QCOMPARE(handler.takePending(1), QList<QUrl>{url});
    QCOMPARE(delivered.count(), 0);
    handler.beginDelivery();
    QCOMPARE(delivered.count(), 1);
    QCOMPARE(delivered[0][0].toUrl(), queuedPackage);
    QFileOpenEvent next(url);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &next);
    QCOMPARE(delivered.count(), 2);
    const auto local = QUrl::fromLocalFile(QDir::tempPath() + "/native-open.echomod");
    QFileOpenEvent plugin(local);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &plugin);
    QCOMPARE(delivered.count(), 3);
    QCOMPARE(delivered[2][0].toUrl(), local);
    QFileOpenEvent zip(QUrl::fromLocalFile(QDir::tempPath() + "/old.zip"));
    QCoreApplication::sendEvent(QCoreApplication::instance(), &zip);
    QCOMPARE(delivered.count(), 3);
}

void PluginRuntimeTest::githubMirrorFallbackAndPostShareUnifiedProxy()
{
    Host host;
    NetworkPreferences preferences(host.settings, host.api);
    PluginManager manager(host.runtime, host.settings);
    manager.setNetworkPreferences(&preferences);
    QTcpServer proxy;
    proxy.setProxy(QNetworkProxy::NoProxy);
    QVERIFY(proxy.listen(QHostAddress::LocalHost));
    QList<QByteArray> requests;
    bool rejectOrigin = false;
    connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
        auto *peer = proxy.nextPendingConnection();
        connect(peer, &QTcpSocket::readyRead, peer, [&, peer] {
            const auto header = peer->property("header").toByteArray() + peer->readAll();
            peer->setProperty("header", header);
            if (!header.contains("\r\n\r\n") || peer->property("answered").toBool())
                return;
            peer->setProperty("answered", true);
            requests.append(header.left(header.indexOf("\r\n")));
            const bool rejected = rejectOrigin || requests.last().contains("/mirror/");
            const QByteArray body(rejected ? "rejected" : "actual download bytes");
            peer->write(
                QByteArray(rejected ? "HTTP/1.1 503 Unavailable\r\n" : "HTTP/1.1 200 OK\r\n") +
                "Content-Length: " + QByteArray::number(body.size()) +
                "\r\nConnection: close\r\n\r\n" + body);
            peer->disconnectFromHost();
        });
        connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
    });
    QSignalSpy applied(&preferences, &NetworkPreferences::applied);
    preferences.apply("fixed_servers", {}, QString("http://127.0.0.1:%1").arg(proxy.serverPort()),
                      {}, {}, false, "<local>", 0, 60);
    QTRY_COMPARE(applied.size(), 1);
    const auto previous = QNetworkProxy::applicationProxy();
    const auto restore = qScopeGuard([&] { QNetworkProxy::setApplicationProxy(previous); });
    preferences.installApplicationTransport();
    preferences.setGithubProxyUrl("https://mirror.invalid/");
    QList<QUrl> resolved;
    // Keep real mirror selection, retry and Qt proxy I/O; only map the
    // external destination to a private transport fixture (not a catalog).
    manager.m_resolveUrl = [&](const QUrl &url) {
        resolved.append(url);
        return QUrl("http://plugin-source.invalid/" +
                    QString(url.host() == "mirror.invalid" ? "mirror/" : "origin/") +
                    "package.zip");
    };
    int completed = 0;
    QByteArray result;
    QString failure;
    const auto finish = [&](QByteArray bytes, QString error) {
        ++completed;
        result = bytes;
        failure = error;
    };
    const QUrl source("https://github.com/owner/repo/releases/download/v1/package.zip");
    manager.get(source, 1024, finish);
    QTRY_COMPARE(completed, 1);
    QVERIFY(failure.isEmpty());
    QCOMPARE(result, QByteArray("actual download bytes"));
    QCOMPARE(resolved.size(), 2);
    QVERIFY(resolved[0].toString().startsWith("https://mirror.invalid/https://github.com/"));
    QCOMPARE(resolved[1], source);
    QCOMPARE(requests.size(), 2);
    QVERIFY(requests[0].startsWith("GET http://plugin-source.invalid/mirror/"));
    QVERIFY(requests[1].startsWith("GET http://plugin-source.invalid/origin/"));
    // Statistics/other writes must never use the mirror or be replayed.
    rejectOrigin = true;
    manager.get(source, 1024, finish, QByteArray("{}"));
    QTRY_COMPARE(completed, 2);
    QVERIFY(failure.contains("503"));
    QCOMPARE(resolved.size(), 3);
    QCOMPARE(resolved[2], source);
    QCOMPARE(requests.size(), 3);
    QVERIFY(requests[2].startsWith("POST http://plugin-source.invalid/origin/"));
    manager.shutdown();
}

void PluginRuntimeTest::sourceCatalogNetworkCacheAndOfficialProtection()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    QAbstractItemModelTester catalogTester(manager.catalog(),
                                           QAbstractItemModelTester::FailureReportingMode::QtTest);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    bool fail = false;
    const auto source = package(host.temporary.path(), "export function activate(c) {}");
    QFile file(source + "/manifest.json");
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto manifest = file.readAll();
    file.close();
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            auto request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n"))
                return;
            const QByteArray body =
                fail ? QByteArray("unavailable")
                : request.startsWith("GET /echo-plugins.json ")
                    ? QByteArray(
                          R"({"schemaVersion":2,"plugins":[{"path":"plugins/test","artifact":"artifacts/test.echomod","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}]})")
                    : manifest;
            socket->write(
                QByteArray(fail ? "HTTP/1.1 503 Unavailable\r\n" : "HTTP/1.1 200 OK\r\n") +
                "Content-Length: " + QByteArray::number(body.size()) +
                "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
    });
    manager.m_resolveUrl = [&](const QUrl &url) {
        return QUrl(
            "http://127.0.0.1:" + QString::number(server.serverPort()) +
            (url.path().endsWith("echo-plugins.json") ? "/echo-plugins.json" : "/manifest.json"));
    };
    manager.refreshCatalog();
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(manager.catalog()->rowCount(), 1);
    QVERIFY(manager.sourceError().isEmpty());
    manager.setSearch("nothing");
    QCOMPARE(manager.catalog()->rowCount(), 0);
    manager.setSearch("Runtime");
    QCOMPARE(manager.catalog()->rowCount(), 1);
    QVERIFY(!manager.removeSource("github:hoowhoami/echomusicnextplugins"));
    QVERIFY(!manager.addSource("https://github.com/owner/repo/../bad", "bad"));
    fail = true;
    manager.refreshCatalog(true);
    QTRY_VERIFY(!manager.catalogLoading());
    QCOMPARE(manager.catalog()->rowCount(), 1);
    QVERIFY(manager.sourceError().contains("503"));
    QVERIFY(manager.patchSource("github:hoowhoami/echomusicnextplugins", "Official", false));
    QCOMPARE(manager.catalog()->rowCount(), 0);
    QCOMPARE(manager.sourceOptions().size(), 2);
    QVERIFY(manager.sourceOptions()[1].toMap().value("disabled").toBool());
    QUrl shared("echomusicnext://plugin");
    QUrlQuery query;
    query.addQueryItem("source", "https://github.com/owner/unavailable");
    query.addQueryItem("id", "test.runtime");
    query.addQueryItem("version", "1.0.0");
    query.addQueryItem("path", "plugins/test");
    shared.setQuery(query);
    QVERIFY(manager.openShare(shared));
    QCOMPARE(manager.shareState(), "need-source");
    QVERIFY(manager.acceptShareSource());
    QTRY_COMPARE(manager.shareState(), "failed");
    QVERIFY(manager.shareError().contains("503"));
    QCOMPARE(manager.sources()->rowCount(), 1);
    manager.shutdown();
}

void PluginRuntimeTest::managementHeaderAndLoadingContentStayAligned()
{
    Host host;
    host.theme.setMotionEnabled(false);
    PluginManager manager(host.runtime, host.settings);
    ShellSettings settings(host.settings);
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &host.engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Plugins
import EchoMusic.Shell
ApplicationWindow {
    id: win
    required property PluginManager manager
    required property RouteController router
    required property ShellSettings settings
    property real uiScale: 1
    width: 1200; height: 650; visible: true; color: Theme.main
    Item {
        width: win.width / win.uiScale; height: win.height / win.uiScale
        scale: win.uiScale; transformOrigin: Item.TopLeft
        PluginManagementPage {
            anchors.fill: parent; manager: win.manager; router: win.router
            context: win.router.current; settings: win.settings
        }
        // Use the production refresh/update action independently of network completion timing.
        PluginActionButton {
            objectName: "loadingTextProbe"
            x: 24; y: parent.height - height - 24; width: 180
            glyph: "refresh"; text: "更新 Update"
        }
    }
})",
                      QUrl("qrc:/plugin-alignment-test.qml"));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"manager", QVariant::fromValue(&manager)},
                                               {"router", QVariant::fromValue(&host.router)},
                                               {"settings", QVariant::fromValue(&settings)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window && Test::prepareNativeInput(window));
    auto *page = visual(window->contentItem(), "pluginManagementPage");
    auto *refresh = visual(page, "pluginRefresh");
    auto *probe = visual(window->contentItem(), "loadingTextProbe");
    QVERIFY(page && refresh && probe);
    const auto output = qEnvironmentVariable("ECHO_PLUGIN_ALIGNMENT_OUTPUT");
    if (!output.isEmpty())
        QVERIFY(QDir().mkpath(output));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (qreal scale : {1.0, 1.25})
            for (int width : {1200, 800, 420}) {
                host.theme.setMode(mode);
                window->setProperty("uiScale", scale);
                window->resize(width, 650);
                auto *emptyIcon = visual(page, "pluginEmptyIcon");
                QVERIFY(emptyIcon);
                QTRY_COMPARE(emptyIcon->size(), QSizeF(48, 48));
                // The trailing action must share the page's 24px inset;
                // a stretching nested Flex leaves a large blank tail.
                QTRY_VERIFY(qAbs(refresh->mapToItem(page, {refresh->width(), 0}).x() -
                                 (page->width() - 24)) < 0.5);
                for (bool loading : {false, true}) {
                    for (auto *button : {refresh, probe}) {
                        button->setProperty("loading", loading);
                        button->setProperty("enabled", !loading);
                        auto *row = visual(button, "buttonContentRow");
                        QVERIFY(row);
                        QTRY_VERIFY(
                            qAbs(row->mapToItem(button, {row->width() / 2, row->height() / 2}).x() -
                                 button->width() / 2) < 0.5);
                        if (loading) {
                            auto *spinner = visual(button, "buttonSpinner");
                            QVERIFY(spinner && spinner->isVisible());
                            QTRY_VERIFY(qAbs(spinner
                                                 ->mapToItem(button, {spinner->width() / 2,
                                                                      spinner->height() / 2})
                                                 .y() -
                                             button->height() / 2) < 0.5);
                        }
                    }
                    if (QGuiApplication::platformName() == "offscreen")
                        continue;
                    auto *spinner = visual(refresh, "buttonSpinner");
                    if (loading)
                        QTRY_COMPARE(spinner->property("status").toInt(), 1);
                    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
                    window->update();
                    QTRY_VERIFY(!frames.isEmpty());
                    const auto image = window->grabWindow();
                    QVERIFY(!image.isNull());
                    if (loading) {
                        const auto dpr = window->devicePixelRatio();
                        const auto topLeft = refresh->mapToScene({0, 0}) * dpr;
                        const qreal unit = scale * dpr;
                        const auto fill = image.pixelColor(qRound(topLeft.x() + 5 * unit),
                                                           qRound(topLeft.y() + 5 * unit));
                        QRect ink;
                        for (int y = qCeil(topLeft.y() + 4 * unit);
                             y < qFloor(topLeft.y() + (refresh->height() - 4) * unit); ++y)
                            for (int x = qCeil(topLeft.x() + 4 * unit);
                                 x < qFloor(topLeft.x() + (refresh->width() - 4) * unit); ++x) {
                                const auto pixel = image.pixelColor(x, y);
                                if (qAbs(qGray(pixel.rgb()) - qGray(fill.rgb())) > 12)
                                    ink = ink.united(QRect(x, y, 1, 1));
                            }
                        QVERIFY(!ink.isEmpty());
                        const QPointF inkCenter(ink.x() + ink.width() / 2.0,
                                                ink.y() + ink.height() / 2.0);
                        const auto center =
                            refresh->mapToScene({refresh->width() / 2, refresh->height() / 2}) *
                            dpr;
                        QVERIFY2(QLineF(inkCenter, center).length() <= 1.5 * unit,
                                 "Rendered loading ring must be centered in the icon action");
                    }
                    if (!output.isEmpty())
                        QVERIFY(image.save(output + QString("/header-%1-%2-%3-%4.png")
                                                        .arg(int(mode))
                                                        .arg(scale)
                                                        .arg(width)
                                                        .arg(loading)));
                }
            }
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void PluginRuntimeTest::managementPageUsesProductionModelsAndDialogs()
{
    Host host;
    PluginManager manager(host.runtime, host.settings);
    ShellSettings settings(host.settings);
    const auto source =
        package(host.temporary.path(),
                "export function activate(c) { c.ui.registerSettings('Settings.qml'); "
                "c.registerCommand('fail', function() { throw new Error('actual callback "
                "failure'); }); }",
                {"lifecycle", "ui.settings", "commands"});
    write(source + "/Settings.qml",
          "import QtQuick\nimport EchoMusic.PluginSdk\nItem { required property "
          "EchoPluginContext plugin; implicitHeight: 80; objectName: 'actualPluginSettings' }");
    QSignalSpy installed(&host.runtime, &PluginRuntime::managementFinished);
    QVERIFY(host.runtime.installDirectory(source));
    QTRY_COMPARE(installed.count(), 1);
    host.router.registerPage({"plugin-management",
                              "/main/settings/plugins",
                              QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginManagementPage.qml"),
                              {"view"},
                              true,
                              false,
                              false,
                              {}});
    QVERIFY(host.router.navigate("plugin-management"));
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &host.engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Theme
import EchoMusic.Plugins
import EchoMusic.Shell
ApplicationWindow {
 id: win
 required property PluginManager manager
 required property RouteController router
 required property ShellSettings settings
 property real uiScale: 1
 width:900; height:650; visible:true; color:Theme.main
 Item {
  width: win.width / win.uiScale; height: win.height / win.uiScale; scale: win.uiScale; transformOrigin: Item.TopLeft
  PluginManagementPage { anchors.fill:parent; manager:win.manager; router:win.router; context:win.router.current; settings:win.settings }
 }
})",
                      QUrl("qrc:/management-test.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"manager", QVariant::fromValue(&manager)},
                                               {"router", QVariant::fromValue(&host.router)},
                                               {"settings", QVariant::fromValue(&settings)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(!visual(window->contentItem(), "pluginRecoveryBanner"));
    QVERIFY(!visual(window->contentItem(), "pluginResumeLoading"));
    const auto exitArtifacts = qEnvironmentVariable("ECHO_PLUGIN_EXIT_ARTIFACTS");
    if (!exitArtifacts.isEmpty()) {
        QDir().mkpath(exitArtifacts);
        window->resize(1000, 800);
        for (const auto mode : {ThemeStore::Dark, ThemeStore::Light}) {
            host.theme.setMode(mode);
            for (const auto scale : {1.0, 1.25}) {
                window->setProperty("uiScale", scale);
                QTest::mouseMove(window, {window->width() - 4, window->height() - 4});
                QTest::qWait(100);
                const auto name = QString("/plugins-%1-%2").arg(int(mode)).arg(scale);
                QVERIFY(window->grabWindow().save(exitArtifacts + name + ".png"));
                auto *surface = visual(window->contentItem(), "pluginCard-test.runtime");
                QVERIFY(surface);
                QTest::mouseMove(window, surface->mapToScene({surface->width() - 8, 8}).toPoint());
                QTest::qWait(220);
                QVERIFY(window->grabWindow().save(exitArtifacts + name + "-hover.png"));
            }
        }
        window->setProperty("uiScale", 1.0);
        host.theme.setMode(ThemeStore::Light);
        host.theme.selectSolid(QColor("#fff0ed"));
        QTest::mouseMove(window, {window->width() - 4, window->height() - 4});
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(exitArtifacts + "/plugins-tinted.png"));
        host.theme.selectEcho();
        window->resize(900, 650);
        QTest::mouseMove(window, {window->width() - 4, window->height() - 4});
    }
    QVERIFY(!root->findChild<QObject *>("pluginPackageFileDialog"));
    auto *installPackage = visual(window->contentItem(), "pluginInstallPackage");
    QVERIFY(installPackage);
    QCOMPARE(installPackage->property("text").toString(), QString("安装插件"));
    QVERIFY(QMetaObject::invokeMethod(installPackage, "clicked"));
    QPointer<QObject> fileDialog = root->findChild<QObject *>("pluginPackageFileDialog");
    QVERIFY(fileDialog);
    QCOMPARE(fileDialog->property("title").toString(), QString("安装 EchoMusicNext 插件"));
    QCOMPARE(fileDialog->property("nameFilters").toStringList(),
             QStringList{QString("EchoMusicNext 插件 (*.echomod)")});
    QTRY_VERIFY(fileDialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(fileDialog, "close"));
    QTRY_VERIFY(!fileDialog);
    // The native picker must remain usable after complete destruction and
    // be released on route retirement as well as explicit close.
    QVERIFY(QMetaObject::invokeMethod(installPackage, "clicked"));
    fileDialog = root->findChild<QObject *>("pluginPackageFileDialog");
    QVERIFY(fileDialog);
    QVERIFY(host.router.navigate("search"));
    QTRY_VERIFY(!fileDialog);
    QVERIFY(host.router.navigate("plugin-management"));
    QTRY_VERIFY(visual(window->contentItem(), "pluginCard-test.runtime"));
    auto *card =
        qobject_cast<QQuickItem *>(visual(window->contentItem(), "pluginCard-test.runtime"));
    QTRY_VERIFY(card->height() > 100);
    auto *idLabel = visual(card, "pluginCardId");
    auto *sourceLabel = visual(card, "pluginCardSource");
    QVERIFY(idLabel && sourceLabel);
    QVERIFY(idLabel->mapToScene({0, 0}).x() < sourceLabel->mapToScene({0, 0}).x());
    QVERIFY(visual(card, "pluginCardDivider"));
    auto *statusTag = visual(card, "pluginCardStatus");
    QVERIFY(statusTag);
    QVERIFY(statusTag->x() > card->width() * 0.5);
    auto *headerIcon = visual(window->contentItem(), "pluginHeaderIcon");
    QVERIFY(headerIcon);
    auto *directoryLabel = visual(window->contentItem(), "pluginDirectory");
    QVERIFY(directoryLabel);
    QCOMPARE(directoryLabel->property("text").toString(), host.runtime.directory());
    QVERIFY(QFile::exists(":/qt/qml/EchoMusic/Controls/icons/puzzle.svg"));
    auto *headerImage = visual(headerIcon, "iconImage");
    QVERIFY(headerImage);
    QVERIFY2(!headerImage->property("source").toUrl().isEmpty(),
             qPrintable(QString("icon visible=%1 color=%2 geometry=%3x%4")
                            .arg(headerIcon->isVisible())
                            .arg(headerIcon->property("color").toString())
                            .arg(headerIcon->width())
                            .arg(headerIcon->height())));
    QTRY_COMPARE(headerIcon->property("status").toInt(), 1);
    QVERIFY(headerIcon->width() > 0);
    QVERIFY(headerIcon->height() > 0);
    QVERIFY(
        QMetaObject::invokeMethod(card, "failureRequested", Q_ARG(QString, host.installation())));
    QPointer<QObject> failureDialog = root->findChild<QObject *>("pluginFailureDialog");
    QVERIFY(failureDialog);
    QTRY_VERIFY(failureDialog->property("visible").toBool());
    auto *failed = host.runtime.executeCommand(host.installation(), "fail");
    QTRY_COMPARE(failed->state(), EchoPluginOperation::Failed);
    QVERIFY(!host.runtime.safeMode());
    QTRY_VERIFY(visual(window->contentItem(), "pluginFailureText"));
    auto *failureText = visual(window->contentItem(), "pluginFailureText");
    QTRY_VERIFY(failureText->property("text").toString().contains("actual callback failure"));
    auto *clearFailure = visual(window->contentItem(), "pluginClearFailure");
    QVERIFY(clearFailure);
    QVERIFY(QMetaObject::invokeMethod(clearFailure, "clicked"));
    QTRY_VERIFY(!failureDialog);
    QVERIFY(host.runtime.record(host.installation()).value("failure").toMap().isEmpty());
    // The bounded row Repeater replaces records after dataChanged. Reacquire
    // its visual delegate after the callback/clear sequence.
    QTRY_VERIFY(visual(window->contentItem(), "pluginCard-test.runtime"));
    card = visual(window->contentItem(), "pluginCard-test.runtime");
    QVERIFY(
        QMetaObject::invokeMethod(card, "settingsRequested", Q_ARG(QString, host.installation())));
    QTRY_VERIFY(visual(window->contentItem(), "actualPluginSettings"));
    QPointer<QObject> dialog = root->findChild<QObject *>("pluginSettingsDialog");
    QVERIFY(dialog);
    QVERIFY(dialog->property("visible").toBool());
    QVERIFY(host.router.navigate("search"));
    QTRY_VERIFY(!dialog);
    QVERIFY(host.router.navigate("plugin-management"));
    auto *safe = visual(window->contentItem(), "pluginSafeMode");
    QVERIFY(safe);
    QVERIFY(safe->setProperty("checked", true));
    QVERIFY(QMetaObject::invokeMethod(safe, "toggled"));
    QTRY_VERIFY(host.runtime.safeMode());
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    // Persisted preference is authoritative; QML consumes its notification.
    QVERIFY(host.settings.value("plugins/safeMode").toBool());
    const auto output = qEnvironmentVariable("ECHO_PLUGIN_UI_OUTPUT");
    if (!output.isEmpty()) {
        QDir().mkpath(output);
        host.theme.setMode(ThemeStore::Light);
        QTest::qWait(150);
        QVERIFY(window->grabWindow().save(output + "/installed-light.png"));
        host.theme.setMode(ThemeStore::Dark);
        QTest::qWait(150);
        QVERIFY(window->grabWindow().save(output + "/installed-dark.png"));
    }
    card = visual(window->contentItem(), "pluginCard-test.runtime");
    QVERIFY(card);
    QVERIFY(
        QMetaObject::invokeMethod(card, "uninstallRequested", Q_ARG(QString, host.installation())));
    QTRY_VERIFY(visual(window->contentItem(), "pluginRemoveDataLabel"));
    QPointer<QObject> uninstallDialog = root->findChild<QObject *>("pluginUninstallDialog");
    QVERIFY(uninstallDialog);
    QTRY_COMPARE(uninstallDialog->property("opacity").toReal(), 1.0);
    if (!output.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(output + "/uninstall-dialog.png"));
    }
    QVERIFY(QMetaObject::invokeMethod(uninstallDialog, "close"));
    QTRY_VERIFY(!uninstallDialog);
    QVERIFY(!root->findChild<QObject *>("pluginLocalInstallOverlay"));
    QMimeData draggedPackage;
    draggedPackage.setUrls({QUrl::fromLocalFile(source)});
    QDragEnterEvent enter(QPoint(300, 300), Qt::CopyAction, &draggedPackage, Qt::LeftButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(window, &enter);
    QVERIFY(enter.isAccepted());
    QPointer<QObject> dragOverlay;
    QTRY_VERIFY((dragOverlay = root->findChild<QObject *>("pluginLocalInstallOverlay")));
    QTRY_VERIFY(dragOverlay->property("visible").toBool());
    QTRY_COMPARE(dragOverlay->property("opacity").toReal(), 1.0);
    QTRY_VERIFY(visual(window->contentItem(), "pluginLocalInstallIcon"));
    auto *dragIcon = visual(window->contentItem(), "pluginLocalInstallIcon");
    QTRY_COMPARE(dragIcon->property("status").toInt(), 1);
    if (!output.isEmpty())
        QVERIFY(window->grabWindow().save(output + "/local-install-overlay.png"));
    QDropEvent dropped(QPointF(300, 300), Qt::CopyAction, &draggedPackage, Qt::LeftButton,
                       Qt::NoModifier);
    QCoreApplication::sendEvent(window, &dropped);
    QVERIFY(dropped.isAccepted());
    QTRY_VERIFY(!manager.busy());
    QCOMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(!dragOverlay);
    window->setWidth(650);
    QTest::qWait(100);
    auto *tabs = visual(window->contentItem(), "pluginViewTabs");
    safe = visual(window->contentItem(), "pluginSafeMode");
    QVERIFY(tabs && safe);
    QVERIFY(safe->mapToScene({0, 0}).y() >= tabs->mapToScene({0, 0}).y() + tabs->height());
    QSignalSpy settingNotice(&manager, &PluginManager::notice);
    QVERIFY(!manager.requestSettings(host.installation()));
    QCOMPARE(settingNotice.count(), 1);
    QVERIFY(settingNotice[0][1].toBool());
    if (!output.isEmpty())
        QVERIFY(window->grabWindow().save(output + "/installed-narrow.png"));
    QVERIFY(host.runtime.setSafeMode(false));
    const auto blockedSource = pluginPackage(
        host.temporary.path(), "test.blocked", "throw new Error('must remain blocked');",
        {{"dependencies", QJsonObject{{"test.required-core", ">=1.0.0 <2.0.0"}}}});
    host.runtime.importDirectory(blockedSource);
    QTRY_VERIFY(!host.runtime.loading());
    QCOMPARE(host.runtime.state(host.runtime.installationForId("test.blocked")),
             PluginRuntime::Blocked);
    window->setWidth(900);
    QTRY_VERIFY(visual(window->contentItem(), "pluginCard-test.blocked"));
    auto *blockedCard = visual(window->contentItem(), "pluginCard-test.blocked");
    QVERIFY(blockedCard->property("warning").toBool());
    QVERIFY(!blockedCard->property("currentFailure").toBool());
    QCOMPARE(visual(blockedCard, "pluginCardStatus")->property("text").toString(),
             QString("依赖未就绪"));
    if (!output.isEmpty()) {
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            host.theme.setMode(mode);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(output + (mode == ThemeStore::Light
                                                            ? "/dependency-light.png"
                                                            : "/dependency-dark.png")));
        }
    }
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    root.reset();
    manager.shutdown();
}
