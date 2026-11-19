#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "PluginCardRowModel.h"
#include <QImage>

void PluginRuntimeTest::pluginCatalogRecyclingRetainsScrollGeometry_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<double>("scale");
    QTest::addColumn<bool>("dark");
    QTest::newRow("light-three-columns") << 1000 << 1. << false;
    QTest::newRow("dark-two-columns-scaled") << 1000 << 1.25 << true;
    QTest::newRow("light-one-column") << 620 << 1. << false;
    QTest::newRow("dark-one-column-scaled") << 760 << 1.25 << true;
}

void PluginRuntimeTest::pluginCatalogRecyclingRetainsScrollGeometry()
{
    QFETCH(int, width);
    QFETCH(double, scale);
    QFETCH(bool, dark);
    int requests = 0;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->setProperty("request",
                                    socket->property("request").toByteArray() + socket->readAll());
                if (socket->property("answered").toBool() ||
                    !socket->property("request").toByteArray().contains("\r\n\r\n"))
                    return;
                socket->setProperty("answered", true);
                ++requests;
                socket->write("HTTP/1.1 429 Too Many Requests\r\nContent-Length: 7\r\n"
                              "Connection: close\r\n\r\nlimited");
                socket->disconnectFromHost();
            });
        }
    });
    Host host;
    host.theme.setMotionEnabled(false);
    host.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    PluginManager manager(host.runtime, host.settings);
    manager.m_loaded = manager.m_catalogComplete = true;
    const auto sourceId = manager.m_sources.first().toMap().value("id");
    for (int i = 0; i < 40; ++i) {
        const auto id = "scroll-" + QString::number(i);
        manager.m_entries.append(QVariantMap{
            {"key", id},
            {"id", id},
            {"name", "插件 " + id},
            {"version", "1.0.0"},
            {"author", "作者"},
            {"compatible", true},
            {"sourceId", sourceId},
            {"description", "图标失败后继续滚动，异步创建和复用仍须保持滚动范围稳定。"},
            {"tags", i % 3 ? QStringList{"播放页", "歌词", "皮肤", "Folia"} : QStringList{}},
            {"iconUrl",
             QUrl(QString("http://127.0.0.1:%1/%2.svg").arg(server.serverPort()).arg(i))}});
    }
    manager.rebuild();
    host.router.registerPage({"plugin-management",
                              "/plugins",
                              QUrl("qrc:/plugins.qml"),
                              {"view"},
                              true,
                              false,
                              false,
                              {}});
    QVERIFY(host.router.navigate("plugin-management", {{"view", "marketplace"}}));
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Plugins
import EchoMusic.Shell
import EchoMusic.Theme
Window {
 id: win
 required property PluginManager manager
 required property RouteController router
 required property ShellSettings settings
 property real uiScale: 1
 width: 1000; height: 620; visible: true; color: Theme.main
 Item {
  width: win.width / win.uiScale; height: win.height / win.uiScale
  scale: win.uiScale; transformOrigin: Item.TopLeft
  Loader {
   anchors.fill: parent; asynchronous: true
   sourceComponent: PluginManagementPage {
    manager: win.manager; router: win.router; context: win.router.current; settings: win.settings
   }
  }
 }
})",
                      QUrl("qrc:/plugin-scroll-failure-test.qml"));
    std::unique_ptr<QObject> root(component.createWithInitialProperties(
        {{"manager", QVariant::fromValue(&manager)},
         {"router", QVariant::fromValue(&host.router)},
         {"settings", QVariant::fromValue(&host.shellSettings)},
         {"width", width},
         {"uiScale", scale}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window && Test::prepareNativeInput(window));
    QQuickItem *list = nullptr;
    QTRY_VERIFY((list = visual(window->contentItem(), "pluginCardList")));
    QTRY_VERIFY(visual(list, "pluginCard-scroll-0"));
    QTRY_VERIFY(list->height() > 0 && list->property("contentHeight").toDouble() > list->height());
    QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
    QTRY_VERIFY(visual(list, "pluginCard-scroll-39"));
    auto *viewport = list->parentItem();
    while (viewport && !viewport->property("scrollView").isValid())
        viewport = viewport->parentItem();
    QVERIFY(viewport);
    auto *contentItem = list->property("contentItem").value<QQuickItem *>();
    QVERIFY(contentItem);
    const auto captures = qEnvironmentVariable("ECHO_PLUGIN_SCROLL_CAPTURES");
    if (!captures.isEmpty())
        QVERIFY(QDir().mkpath(captures));
    Test::FrameSpy frames(window);
    int pooledSamples = 0, peakCards = 0;
    ulong stamp = 10000;
    const auto wheel = [&](QPoint delta, Qt::ScrollPhase phase) {
        const auto pos = list->mapToScene({list->width() * .2, list->height() * .5});
        QWheelEvent event(pos, window->mapToGlobal(pos.toPoint()), delta, delta * 2, Qt::NoButton,
                          Qt::NoModifier, phase, false);
        event.setTimestamp(stamp += 16);
        QCoreApplication::sendEvent(window, &event);
    };
    for (int step = 0; step < 60; ++step) {
        // Explicit jumps exercise row reuse; wheel input uses the production
        // scroll controller, including its originY-aware bounds and saved offset.
        if (step < 20) {
            const auto maximum =
                qMax(0., list->property("contentHeight").toDouble() - list->height());
            QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo",
                                              Q_ARG(double, maximum *((step * 7) % 17) / 16.)));
        } else {
            if (step == 20)
                wheel({}, Qt::ScrollBegin);
            wheel({0, step < 40 ? -250 : 250}, Qt::ScrollUpdate);
        }
        const auto key =
            manager.m_entries[step % manager.m_entries.size()].toMap().value("key").toString();
        manager.m_stats[key] =
            QVariantMap{{"installCount", step}, {"updateCount", 0}, {"score", 0}};
        manager.rebuild();
        window->update();
        QVERIFY(frames.wait());
        QList<QPair<qreal, qreal>> spans;
        int cards = 0;
        for (auto *row : contentItem->childItems()) {
            if (row->property("pooled").toBool()) {
                ++pooledSamples;
                // Unloading offscreen cards must not collapse the extent which
                // ListView will use when it reuses this row.
                QVERIFY2(row->height() > 0, "A pooled row collapsed to zero height");
                for (auto *projection : row->findChildren<PluginCardRowModel *>())
                    QCOMPARE(projection->rowCount(), 0);
                continue;
            }
            for (auto *projection : row->findChildren<PluginCardRowModel *>())
                cards += projection->rowCount();
            const auto top = row->mapToItem(list, QPointF{}).y();
            const auto begin = qMax(0., top), end = qMin(list->height(), top + row->height());
            if (begin < end)
                spans.append({begin, end});
        }
        peakCards = qMax(peakCards, cards);
        QVERIFY2(cards < 20, "The viewport instantiated most of the 40-card catalog");
        std::sort(spans.begin(), spans.end());
        qreal covered = 0, end = 0;
        for (const auto &span : spans) {
            covered += qMax(0., span.second - qMax(end, span.first));
            end = qMax(end, span.second);
        }
        if (covered < list->height() * .25 && !captures.isEmpty())
            window->grabWindow().save(captures + "/blank-" + QTest::currentDataTag() + ".png");
        QVERIFY2(covered >= list->height() * .25, "The presented plugin viewport is mostly blank");
        if (step % 23 == 0)
            host.engine.collectGarbage();
    }
    wheel({}, Qt::ScrollEnd);
    QVERIFY(pooledSamples > 0);
    QTRY_VERIFY(requests > 3);
    if (!captures.isEmpty())
        QVERIFY(
            window->grabWindow().save(captures + "/scrolled-" + QTest::currentDataTag() + ".png"));
    QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo", Q_ARG(double, 0.)));
    QTRY_VERIFY(visual(list, "pluginCard-scroll-0"));
    qInfo() << "Recycled rows checked:" << pooledSamples << "peak live cards:" << peakCards;
    root.reset();
    manager.shutdown();
}

void PluginRuntimeTest::pluginCardStateChangesPreserveDelegateAndScroll()
{
    Host host;
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &host.engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    PluginManager manager(host.runtime, host.settings);
    manager.m_loaded = true;
    manager.m_catalogComplete = true;
    const auto sourceId = manager.m_sources.first().toMap().value("id");
    QImage artwork(44, 44, QImage::Format_ARGB32_Premultiplied);
    artwork.fill(QColor("#087f6b"));
    const auto artworkPath = host.temporary.path() + "/card.png";
    QVERIFY(artwork.save(artworkPath));
    for (int i = 0; i < 40; ++i) {
        const auto id = "card-" + QString::number(i);
        manager.m_entries.append(QVariantMap{{"key", id},
                                             {"id", id},
                                             {"name", "插件 " + id},
                                             {"version", "1.0.0"},
                                             {"author", "作者"},
                                             {"compatible", true},
                                             {"sourceId", sourceId},
                                             {"description", "真实卡片的状态更新不应重建界面。"},
                                             {"iconUrl", QUrl::fromLocalFile(artworkPath)}});
    }
    manager.rebuild();
    PluginCardRowModel projection;
    projection.setSource(manager.catalog());
    projection.setLimit(2);
    QAbstractItemModelTester modelTest(&projection,
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(projection.rowCount(), 2);
    host.router.registerPage({"plugin-management",
                              "/main/settings/plugins",
                              QUrl("qrc:/qt/qml/EchoMusic/Plugins/PluginManagementPage.qml"),
                              {"view", "highlightPluginId"},
                              true,
                              false,
                              false,
                              {}});
    QVERIFY(host.router.navigate("plugin-management", {{"view", "marketplace"}}));
    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import EchoMusic.Plugins
import EchoMusic.Shell
import EchoMusic.Theme
Window { id:win; required property PluginManager manager; required property RouteController router
 required property ShellSettings settings; width:760; height:820; visible:true; color:Theme.main
 PluginManagementPage { anchors.fill:parent; manager:win.manager; router:win.router; context:win.router.current; settings:win.settings }
})",
                      QUrl());
    std::unique_ptr<QObject> root(component.createWithInitialProperties(
        {{"manager", QVariant::fromValue(&manager)},
         {"router", QVariant::fromValue(&host.router)},
         {"settings", QVariant::fromValue(&host.shellSettings)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    QTRY_VERIFY(visual(window->contentItem(), "pluginCard-card-0"));
    QPointer<QQuickItem> card = visual(window->contentItem(), "pluginCard-card-0");
    QPointer<QQuickItem> neighbor = visual(window->contentItem(), "pluginCard-card-1");
    QPointer<QQuickItem> icon = visual(card, "pluginCardIcon");
    QTRY_COMPARE(icon->property("status").toInt(), 1);
    QSignalSpy imageState(icon, SIGNAL(statusChanged()));
    QVERIFY(imageState.isValid());
    const qreal height = card->height();
    auto entry = manager.m_entries[0].toMap();
    entry["compatible"] = false;
    entry["error"] = "此插件仅支持 Windows，当前平台为 macOS";
    manager.m_entries[0] = entry;
    manager.rebuild();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY2(card, "A record-only update destroyed the card delegate");
    QCOMPARE(visual(window->contentItem(), "pluginCard-card-0"), card.data());
    QVERIFY(neighbor && icon);
    QCOMPARE(visual(card, "pluginCardIcon"), icon.data());
    QCOMPARE(imageState.count(), 0);
    // Quick delegates have visual parents which need not follow the Window's
    // QObject ownership tree. Inspect each visual owner, including pooled rows.
    QSet<PluginCardRowModel *> sceneModels;
    std::function<void(QQuickItem *)> collectModels = [&](QQuickItem *item) {
        for (auto *model : item->findChildren<PluginCardRowModel *>())
            sceneModels.insert(model);
        for (auto *child : item->childItems())
            collectModels(child);
    };
    collectModels(window->contentItem());
    QVERIFY(!sceneModels.isEmpty() && sceneModels.size() < 20);
    QPointer<PluginCardRowModel> ownedProjection = *sceneModels.begin();
    QVERIFY(ownedProjection);
    QTRY_COMPARE(card->height(), height);
    auto *list = visual(window->contentItem(), "pluginCardList");
    QVERIFY(list);
    QVERIFY(list->setProperty("contentY", 900.));
    QTest::qWait(50);
    QPointer<QQuickItem> reading;
    int readingIndex = -1;
    for (int i = 0; i < 40; ++i) {
        auto *candidate = visual(list, "pluginCard-card-" + QString::number(i));
        if (!candidate)
            continue;
        const auto position = candidate->mapToItem(list, QPointF{});
        if (position.y() > 0 && position.y() < list->height() - 80) {
            reading = candidate;
            readingIndex = i;
            break;
        }
    }
    QVERIFY(reading);
    const qreal position = list->property("contentY").toReal(), readingHeight = reading->height();
    const auto scene = reading->mapToScene({});
    for (int revision = 0; revision < 20; ++revision) {
        auto changed = manager.m_entries[readingIndex].toMap();
        changed["compatible"] = revision % 2 == 0;
        changed["error"] = revision % 2 ? "缺少所需能力，请使用提供该能力的主程序后再启用。" : "";
        changed["stats"] =
            QVariantMap{{"installCount", revision}, {"updateCount", revision}, {"score", revision}};
        manager.m_entries[readingIndex] = changed;
        manager.rebuild();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(5);
        QVERIFY(reading);
        QCOMPARE(reading->height(), readingHeight);
        QCOMPARE(reading->mapToScene({}), scene);
        QCOMPARE(list->property("contentY").toReal(), position);
    }
    // Installed cards offer a useful management action, including a local
    // installation which cannot be replaced by this marketplace source.
    QVERIFY(list->setProperty("contentY", 0.));
    QVERIFY(host.runtime.installDirectory(
        pluginPackage(host.temporary.path(), "card-0", "export function activate(){}")));
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("card-0")),
                 PluginRuntime::Active);
    QTRY_VERIFY(visual(window->contentItem(), "pluginCard-card-0"));
    auto *manage =
        visual(visual(window->contentItem(), "pluginCard-card-0"), "pluginMarketplaceInstall");
    QVERIFY(manage && manage->isEnabled());
    QCOMPARE(manage->property("text").toString(), "管理");
    QVERIFY(QMetaObject::invokeMethod(manage, "clicked"));
    QTRY_COMPARE(host.router.current()->query().value("view").toString(), "installed");
    QTRY_VERIFY(visual(window->contentItem(), "pluginCard-card-0"));
    auto *page = visual(window->contentItem(), "pluginManagementPage");
    QVERIFY(page);
    QTRY_COMPARE(page->property("highlightedPluginId").toString(), "card-0");
    auto *installed = visual(window->contentItem(), "pluginCard-card-0");
    QVERIFY(installed->property("highlighted").toBool());
    QVERIFY(visual(installed, "pluginCardLocateRing")->isVisible());
    const auto installedHeight = installed->height();
    QTRY_VERIFY_WITH_TIMEOUT(!installed->property("highlighted").toBool(), 5000);
    QCOMPARE(installed->height(), installedHeight);
    QVERIFY(!visual(installed, "pluginCardLocateRing")->isVisible());

    // Share navigation uses the same route target and feedback, even when
    // the target starts outside the virtualized viewport.
    host.router.replaceQuery({{"view", "marketplace"}, {"highlightPluginId", "card-39"}});
    QTRY_COMPARE(page->property("highlightedPluginId").toString(), "card-39");
    QTRY_VERIFY(visual(list, "pluginCard-card-39"));
    auto *last = visual(list, "pluginCard-card-39");
    QVERIFY(last->property("highlighted").toBool());
    QTRY_VERIFY(last->mapToItem(list, QPointF{}).y() < list->height());
    QVERIFY(last->mapToItem(list, QPointF{0, last->height()}).y() > 0);
    const auto locatedPosition = list->property("contentY");
    manager.rebuild();
    QTest::qWait(50);
    QCOMPARE(list->property("contentY"), locatedPosition);
    // Feedback is keyed by identity rather than a reused row or delegate.
    host.router.replaceQuery({{"view", "marketplace"}, {"highlightPluginId", "card-0"}});
    QTRY_COMPARE(page->property("highlightedPluginId").toString(), "card-0");
    QTRY_VERIFY(visual(list, "pluginCard-card-0"));
    QVERIFY(visual(list, "pluginCard-card-0")->property("highlighted").toBool());
    if (auto *old = visual(list, "pluginCard-card-39"))
        QVERIFY(!old->property("highlighted").toBool());
    auto *timer = window->findChild<QObject *>("pluginLocateHighlightTimer");
    QVERIFY(timer && timer->property("running").toBool());
    window->hide();
    QTRY_VERIFY(page->property("highlightedPluginId").toString().isEmpty());
    QVERIFY(!timer->property("running").toBool());
    window->show();
    QTest::qWait(50);
    QVERIFY(page->property("highlightedPluginId").toString().isEmpty());
    // A catalog arriving after route creation must still consume the target
    // exactly once, without a retry timer or repeated scroll on refresh.
    host.router.replaceQuery({{"view", "marketplace"}, {"highlightPluginId", "arriving"}});
    QTRY_COMPARE(page->property("revealPluginId").toString(), "arriving");
    manager.m_entries.append(QVariantMap{{"id", "arriving"},
                                         {"key", "arriving"},
                                         {"name", "稍后到达"},
                                         {"sourceId", sourceId},
                                         {"compatible", true}});
    manager.rebuild();
    QTRY_COMPARE(page->property("highlightedPluginId").toString(), "arriving");
    QTRY_VERIFY(visual(list, "pluginCard-arriving"));
    QVERIFY(visual(list, "pluginCard-arriving")->property("highlighted").toBool());
    host.router.replaceQuery({{"view", "installed"}});
    QTRY_VERIFY(page->property("highlightedPluginId").toString().isEmpty());
    QVERIFY(!timer->property("running").toBool());
    root.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!reading);
    QVERIFY(!ownedProjection);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    manager.shutdown();
}

void PluginRuntimeTest::pluginCardPresentationStates()
{
    Host host;
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &host.engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    PluginManager manager(host.runtime, host.settings);
    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import EchoMusic.Plugins
import EchoMusic.Theme
Window { id:win; required property PluginManager manager; property var record:({}); property bool market:true
 property real uiScale:1; width:620; height:620; visible:true; color:Theme.main
 Item { width:win.width/win.uiScale; height:win.height/win.uiScale; scale:win.uiScale; transformOrigin:Item.TopLeft
 PluginCard {objectName:'stateCard'; x:20;y:20;width:parent.width-40; manager:win.manager; record:win.record;marketplace:win.market} }
})",
                      QUrl());
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"manager", QVariant::fromValue(&manager)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    auto *card = visual(window->contentItem(), "stateCard");
    struct State
    {
        QString name;
        bool market;
        QVariantMap fields;
        QString label, action;
        bool enabled;
    };
    const QList<State> states{
        {"available", true, {}, "", "安装", true},
        {"platform",
         true,
         {{"compatible", false},
          {"platformIncompatible", true},
          {"error", "此插件仅支持 Windows，当前平台为 macOS"}},
         "不可用",
         "安装",
         false},
        {"version",
         true,
         {{"compatible", false},
          {"error",
           "缺少所需的绘图能力，请安装提供该能力的主程序后再使用。插件配置和已有数据不会受影响。"}},
         "不可用",
         "安装",
         false},
        {"installed", true, {{"installed", true}}, "已安装", "管理", true},
        {"update",
         true,
         {{"installed", true}, {"updateAvailable", true}, {"installedVersion", "0.9.0"}},
         "有更新",
         "更新",
         true},
        {"installing", true, {{"busy", true}}, "安装中", "安装", false},
        {"stale", true, {{"stale", true}, {"refreshError", "network"}}, "来源离线", "安装", true},
        {"active", false, {{"active", true}}, "运行中", "", true},
        {"disabled", false, {{"enabled", false}}, "已停用", "", true},
        {"safe-mode", false, {{"safeMode", true}}, "未加载", "", true},
        {"history",
         false,
         {{"active", true}, {"failure", QVariantMap{{"message", "上次回调失败"}}}},
         "运行中",
         "",
         true},
        {"blocked",
         false,
         {{"dependencyBlocked", true}, {"error", "需要启用依赖插件 audio-library"}},
         "待依赖",
         "",
         true},
        {"failed",
         false,
         {{"error", "插件初始化失败：无法解析频谱配置"},
          {"failure", QVariantMap{{"message", "插件初始化失败：无法解析频谱配置"}}}},
         "运行异常",
         "",
         true},
        {"local-platform",
         false,
         {{"compatible", false},
          {"platformIncompatible", true},
          {"error", "此插件仅支持 Windows，当前平台为 macOS"}},
         "不可用",
         "",
         true}};
    const auto captures = qEnvironmentVariable("ECHO_PLUGIN_CARD_UX_CAPTURES");
    if (!captures.isEmpty())
        QDir().mkpath(captures);
    for (const int fontScale : {100, 125}) {
        host.theme.setGlobalFontSizeScale(fontScale);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            host.theme.setMode(mode);
            for (const auto scale : {1., 1.25}) {
                window->setWidth(scale == 1. ? 620 : 420);
                window->setProperty("uiScale", scale);
                QMap<bool, qreal> heights;
                for (const auto &state : states) {
                    QVariantMap record{{"id", "state-preview"},
                                       {"name", "频谱可视化"},
                                       {"author", "hoowhoami"},
                                       {"version", "1.0.0"},
                                       {"enabled", true},
                                       {"compatible", true},
                                       {"sourceName", state.market ? "官方插件源" : "本地安装"},
                                       {"description", "在播放栏、Mini 和歌词页中显示实时频谱。"},
                                       {"tags", QStringList{"频谱", "可视化"}},
                                       {"platformLabel", "Windows"}};
                    for (auto it = state.fields.begin(); it != state.fields.end(); ++it)
                        record[it.key()] = it.value();
                    QVERIFY(host.runtime.setSafeMode(record.value("safeMode").toBool()));
                    window->setProperty("market", state.market);
                    window->setProperty("record", record);
                    QTest::qWait(25);
                    if (QGuiApplication::platformName() != "offscreen") {
                        // Native resize/layout polish can outlive a fixed wait
                        // under build load. Inspect a committed frame.
                        QSignalSpy frame(window, &QQuickWindow::frameSwapped);
                        window->update();
                        QVERIFY(frame.wait(2000));
                    }
                    auto *status = visual(card, "pluginCardStatus");
                    QCOMPARE(status->property("text").toString(), state.label);
                    QCOMPARE(status->isVisible(), !state.label.isEmpty());
                    auto *button = visual(card, "pluginMarketplaceInstall");
                    QCOMPARE(button->isVisible(), state.market);
                    if (state.market) {
                        QCOMPARE(button->property("text").toString(), state.action);
                        QCOMPARE(button->isEnabled(), state.enabled);
                    }
                    auto *reason = visual(card, "pluginCardWarning");
                    auto *platform = visual(card, "pluginCardPlatforms");
                    QCOMPARE(platform->isVisible(), !reason->isVisible());
                    if (!heights.contains(state.market))
                        heights[state.market] = card->height();
                    QCOMPARE(card->height(), heights[state.market]);
                    auto *actions = visual(card, "pluginCardActions");
                    for (auto *child : actions->findChildren<QQuickItem *>())
                        if (child->isVisible() && child->inherits("QQuickAbstractButton")) {
                            const auto rect = child->mapRectToItem(card, child->boundingRect());
                            if (rect.left() < 0 || rect.right() > card->width()) {
                                qInfo()
                                    << "card action overflow" << state.name << mode << scale
                                    << fontScale << card->width() << child->objectName() << rect;
                                if (!captures.isEmpty())
                                    window->grabWindow().save(captures + "/overflow.png");
                            }
                            QVERIFY2(rect.left() >= 0 && rect.right() <= card->width(),
                                     qPrintable(state.name));
                        }
                    if (!captures.isEmpty())
                        QVERIFY(window->grabWindow().save(captures + "/" + state.name + "-" +
                                                          QString::number(int(mode)) + "-" +
                                                          QString::number(scale) + "-font" +
                                                          QString::number(fontScale) + ".png"));
                    if (state.name == "platform" || state.name == "active") {
                        const auto background = card->property("color");
                        const auto opacity = card->opacity();
                        const auto normalHeight = card->height();
                        card->setProperty("highlighted", true);
                        QTest::qWait(25);
                        QVERIFY(visual(card, "pluginCardLocateRing")->isVisible());
                        QCOMPARE(card->property("color"), background);
                        QCOMPARE(card->opacity(), opacity);
                        QCOMPARE(card->height(), normalHeight);
                        if (!captures.isEmpty())
                            QVERIFY(window->grabWindow().save(
                                captures + "/located-" + state.name + "-" +
                                QString::number(int(mode)) + "-" + QString::number(scale) +
                                "-font" + QString::number(fontScale) + ".png"));
                        card->setProperty("highlighted", false);
                    }
                }
            }
        }
    }
    host.theme.setGlobalFontSizeScale(100);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    manager.shutdown();
}

void PluginRuntimeTest::taskProgressPreservesExpandedDialogGeometry()
{
    Host host;
    host.theme.setMotionEnabled(false);
    auto *tasks = host.shell.tasks();
    QObject owner;
    TaskCenterModel::Registration registration;
    registration.id = "plugin-progress";
    registration.status = TaskCenterModel::Running;
    auto &content = registration.content;
    content.name = "插件更新";
    content.icon = "puzzle";
    content.percent = 0;
    content.statusLabel = "更新中 0/4";
    for (int i = 0; i < 4; ++i)
        content.items.append(
            {QString::number(i), "插件 " + QString::number(i), "1.0.1 → 1.0.2", {}, "更新中…", {}});
    const auto handle = tasks->registerTask(owner, registration);
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
ApplicationWindow {
 id: win
 required property TaskCenterModel tasks
 property real uiScale: 1
 width: 1000; height: 940; visible: true; color: Theme.main
 EchoPopupLoader {
  hostItem: win.contentItem
  sourceComponent: TaskCenterDialog { objectName: "progressDialog"; service: win.tasks; uiScale: win.uiScale }
  Component.onCompleted: open()
 }
})",
                      QUrl("qrc:/task-progress-geometry.qml"));
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        host.theme.setMode(mode);
        for (double scale : {1., 1.25}) {
            std::unique_ptr<QObject> root(component.createWithInitialProperties(
                {{"tasks", QVariant::fromValue(tasks)}, {"uiScale", scale}}));
            QVERIFY2(root, qPrintable(component.errorString()));
            auto *window = qobject_cast<QQuickWindow *>(root.get());
            QVERIFY(window && QTest::qWaitForWindowExposed(window));
            QPointer<QObject> dialog;
            QTRY_VERIFY((dialog = root->findChild<QObject *>("progressDialog")));
            Test::FrameSpy frames(window);
            QTRY_VERIFY(dialog->property("opened").toBool());
            auto *details =
                tasks->index(0).data(TaskCenterModel::DetailsRole).value<TaskDetailsModel *>();
            QVERIFY(details);
            details->setExpanded(true);
            QPointer<QQuickItem> view;
            QTRY_VERIFY((view = visual(window->contentItem(), "taskCenterDetails")));
            QTRY_COMPARE(view->property("count").toInt(), 4);
            QTRY_VERIFY(view && view->property("measuredExtent").toReal() >= 0);
            QTRY_COMPARE(view->height(), view->implicitHeight());
            auto *taskList = visual(window->contentItem(), "taskCenterList");
            QVERIFY(taskList);
            QTRY_VERIFY(taskList->property("measuredExtent").toReal() >= 0);
            window->update();
            QVERIFY(frames.wait());
            QPointer<QQuickItem> row = visual(view, "taskCenterDetailRow");
            QVERIFY(row);
            const qreal height = dialog->property("height").toReal();
            const qreal viewHeight = view->height();
            QSignalSpy heightChanges(dialog, SIGNAL(heightChanged()));
            QSignalSpy detailHeightChanges(view, &QQuickItem::heightChanged);
            QSignalSpy resets(details, &QAbstractItemModel::modelReset);
            for (int step = 0; step < 24; ++step) {
                content.percent = step * 100. / 23;
                content.statusLabel = QString("更新中 %1/4").arg(step % 5);
                content.items[step % 4].statusLabel = step % 2 ? "已更新" : "更新中…";
                QVERIFY(tasks->update(handle, content));
                window->update();
                QVERIFY(frames.wait());
                QVERIFY2(row, "Progress publication destroyed an existing detail delegate");
                QCOMPARE(resets.count(), 0);
                QCOMPARE(dialog->property("height").toReal(), height);
                QCOMPARE(view->height(), viewHeight);
            }
            QCOMPARE(heightChanges.count(), 0);
            QCOMPARE(detailHeightChanges.count(), 0);
            const auto captures = qEnvironmentVariable("ECHO_TASK_PROGRESS_CAPTURES");
            if (!captures.isEmpty()) {
                QVERIFY(QDir().mkpath(captures));
                QVERIFY(window->grabWindow().save(
                    captures + QString("/progress-%1-%2.png").arg(int(mode)).arg(scale)));
            }
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(!dialog);
            root.reset();
        }
    }
}
