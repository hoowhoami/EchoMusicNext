#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include <QQmlExpression>
#include <QApplication>
#include <QFileDialog>
#include <algorithm>

namespace
{
QQuickItem *browser(QQuickItem *root)
{
    if (root->property("plugin").isValid() && root->property("pageActive").toBool() &&
        root->property("errorMessage").isValid())
        return root;
    for (auto *child : root->childItems())
        if (auto *found = browser(child))
            return found;
    return nullptr;
}
QQuickItem *songRow(QQuickItem *root, int index)
{
    if (root->objectName() == "songDelegate" && root->property("index").toInt() == index)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = songRow(child, index))
            return found;
    return nullptr;
}
} // namespace

void PluginRuntimeTest::reportedPackagesPrivatePlayRouting_data()
{
    QTest::addColumn<bool>("reverse");
    QTest::newRow("local-first") << false;
    QTest::newRow("webdav-first") << true;
}

void PluginRuntimeTest::reportedPackagesPrivatePlayRouting()
{
    QFETCH(bool, reverse);
    const auto directory = qEnvironmentVariable("ECHO_PLUGIN_H3_PACKAGES_DIR");
    if (directory.isEmpty())
        QSKIP("Set ECHO_PLUGIN_H3_PACKAGES_DIR to the supplied .echomod package directory");
    Host host;
    QStringList ids{"local-music", "webdav-music"};
    if (reverse)
        std::reverse(ids.begin(), ids.end());
    for (const auto &id : ids) {
        const auto path = directory + '/' + id + "-1.0.0.echomod";
        QFile archive(path);
        QVERIFY2(archive.open(QIODevice::ReadOnly), qPrintable(path));
        qInfo().noquote()
            << id << "SHA-256"
            << QCryptographicHash::hash(archive.readAll(), QCryptographicHash::Sha256).toHex();
        QVERIFY(host.runtime.installDirectory(path));
        QTRY_VERIFY_WITH_TIMEOUT(!host.runtime.loading(), 10000);
        const auto installation = host.runtime.installationForId(id);
        QVERIFY2(!installation.isEmpty(), qPrintable(id));
        QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    }
    // Empty, isolated profiles produce distinct command responses without any
    // media playback, credentials, file grants, or WebDAV network requests.
    for (const auto &id : ids) {
        const auto installation = host.runtime.installationForId(id);
        auto *play = host.runtime.executeCommand(installation, "play", {});
        QVERIFY(play);
        QTRY_VERIFY(play->finished());
        QCOMPARE(play->state(), EchoPluginOperation::Succeeded);
        const auto error = play->result().toMap().value("error").toString();
        qInfo().noquote()
            << id << "native play"
            << QJsonDocument::fromVariant(play->result()).toJson(QJsonDocument::Compact);
        if (id == "local-music")
            QVERIFY(error == "not-ready" || error == "empty");
        else
            QCOMPARE(error, "no-library");
    }
    // Use the real host route recipe and the packages' unmodified BrowserPage
    // components, so the context injection and QML command helper are covered.
    QVERIFY(host.router.navigate("local-music:page:local-browser"));
    auto window = ecosystemWindow(host, R"QML(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {
    id: win
    required property RouteController router
    required property ShellSettings settings
    width: 1000; height: 700; visible: true
    RouteHost { anchors.fill: parent; router: win.router; settings: win.settings }
})QML",
                                  {{"router", QVariant::fromValue(&host.router)},
                                   {"settings", QVariant::fromValue(&host.shellSettings)}});
    auto *mainWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(mainWindow);
    host.mainWindow = mainWindow;
    for (const auto &id :
         QStringList{"local-music", "webdav-music", "local-music", "webdav-music"}) {
        const auto pageId = id == "local-music" ? "local-browser" : "webdav-browser";
        QVERIFY(host.router.navigate(id + ":page:" + pageId));
        QTRY_VERIFY(browser(mainWindow->contentItem()));
        auto *page = browser(mainWindow->contentItem());
        auto *scope = page->property("plugin").value<EchoPluginContext *>();
        QVERIFY(scope);
        QCOMPARE(scope->id(), id);
        QQmlExpression request(qmlContext(page), page,
                               "run('play', {}, function(result) { errorMessage = '__H3__' + "
                               "JSON.stringify(result); })");
        request.evaluate();
        QVERIFY2(!request.hasError(), qPrintable(request.error().toString()));
        QTRY_VERIFY(page->property("errorMessage").toString().startsWith("__H3__"));
        const auto response = page->property("errorMessage").toString().mid(6).toUtf8();
        const auto result = QJsonDocument::fromJson(response).object();
        qInfo().noquote() << id << "BrowserPage injected" << scope->id() << "QML play" << response;
        if (id == "local-music")
            QVERIFY(result.value("error") == "not-ready" || result.value("error") == "empty");
        else
            QCOMPARE(result.value("error").toString(), "no-library");
    }
}

void PluginRuntimeTest::reportedPackagesRestartPlayback_data()
{
    QTest::addColumn<bool>("reverse");
    QTest::newRow("local-first") << false;
    QTest::newRow("webdav-first") << true;
}

void PluginRuntimeTest::reportedPackagesRestartPlayback()
{
    QFETCH(bool, reverse);
    if (qEnvironmentVariableIsEmpty("ECHO_PLUGIN_H3_PACKAGES_DIR"))
        QSKIP("Set ECHO_PLUGIN_H3_PACKAGES_DIR to the supplied package directory");
    QTemporaryDir profile;
    const auto cleanup = qScopeGuard(
        [&] { QDir(PluginPaths(profile.path() + "/plugins").cache()).removeRecursively(); });
    for (const auto &phase : {QString("prepare"), QString("0"), QString("1"), QString("2")}) {
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("ECHO_H3_RESTART_PROFILE", profile.path());
        environment.insert("ECHO_H3_RESTART_PHASE", phase);
        environment.insert("ECHO_H3_REVERSE", reverse ? "1" : "0");
        // Keep automated folder selection independent of the native platform
        // picker. Every restarted playback phase retains the requested backend.
        if (phase == "prepare")
            environment.insert("QT_QPA_PLATFORM", "offscreen");
        child.setProcessEnvironment(environment);
        child.setProcessChannelMode(QProcess::MergedChannels);
        QByteArray output;
        connect(&child, &QProcess::readyRead, &child, [&] {
            const auto chunk = child.readAll();
            output.append(chunk);
            qInfo().noquote() << chunk;
        });
        child.start(QCoreApplication::applicationFilePath(), {"reportedPackagesRestartFixture"});
        QVERIFY(child.waitForStarted());
        QVERIFY2(QTest::qWaitFor([&] { return child.state() == QProcess::NotRunning; }, 60000),
                 output.constData());
        output.append(child.readAll());
        qInfo().noquote() << "RESTART" << (reverse ? "webdav-first" : "local-first") << phase
                          << "exit=" << child.exitCode();
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, output.constData());
    }
}

void PluginRuntimeTest::reportedPackagesRestartFixture()
{
    const auto profile = qEnvironmentVariable("ECHO_H3_RESTART_PROFILE");
    if (profile.isEmpty())
        QSKIP("Subprocess fixture for reportedPackagesRestartPlayback");
    const auto phase = qEnvironmentVariable("ECHO_H3_RESTART_PHASE");
    Host host;
    EchoMusic::SettingsStore preferences(profile + "/settings.ini");
    PluginRuntime runtime(host.engine, preferences,
                          {host.playback,
                           host.theme,
                           host.shell,
                           host.router,
                           host.account,
                           host.api,
                           &host.floating,
                           nullptr,
                           &host.sidebar,
                           &host.shortcuts,
                           &host.verification,
                           nullptr,
                           nullptr,
                           {},
                           &host.shellSettings,
                           nullptr,
                           [&host] { return host.mainWindow.data(); }},
                          profile + "/plugins");
    QSignalSpy closed(&runtime, &PluginRuntime::closed);
    const auto drain = qScopeGuard([&] {
        host.playback.stop();
        runtime.shutdown();
        if (!QTest::qWaitFor([&] { return !closed.isEmpty(); }, 10000))
            qFatal("Restart fixture failed to drain");
    });
    runtime.start();
    QTRY_VERIFY(!runtime.loading());
    if (phase == "prepare") {
        QStringList ids{"local-music", "webdav-music"};
        if (qEnvironmentVariable("ECHO_H3_REVERSE") == "1")
            std::reverse(ids.begin(), ids.end());
        for (const auto &id : ids) {
            QVERIFY(runtime.installDirectory(qEnvironmentVariable("ECHO_PLUGIN_H3_PACKAGES_DIR") +
                                             '/' + id + "-1.0.0.echomod"));
            QTRY_VERIFY_WITH_TIMEOUT(!runtime.loading(), 10000);
            QCOMPARE(runtime.state(runtime.installationForId(id)), PluginRuntime::Active);
        }
    }
    for (const auto &id : {QString("local-music"), QString("webdav-music")})
        QTRY_COMPARE(runtime.state(runtime.installationForId(id)), PluginRuntime::Active);
    const auto installation = runtime.installationForId("local-music");
    auto *scope = runtime.context(installation);
    QVERIFY(scope);
    if (phase == "prepare") {
        const auto music = profile + "/music";
        QByteArray wave;
        QDataStream data(&wave, QIODevice::WriteOnly);
        data.setByteOrder(QDataStream::LittleEndian);
        data.writeRawData("RIFF", 4);
        data << quint32(36 + 320000);
        data.writeRawData("WAVEfmt ", 8);
        data << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000)
             << quint16(2) << quint16(16);
        data.writeRawData("data", 4);
        data << quint32(320000);
        wave.append(QByteArray(320000, 0));
        for (int i = 0; i < 3; ++i)
            write(music + QString("/Track-%1.wav").arg(i), wave);
        QTimer selector;
        connect(&selector, &QTimer::timeout, &selector, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == "pluginNativeFileDialog") {
                    auto *picker = qobject_cast<QFileDialog *>(widget);
                    picker->setDirectory(music);
                    picker->selectFile(music);
                    QMetaObject::invokeMethod(picker, "accept");
                }
        });
        selector.start(10);
        auto *add = runtime.executeCommand(installation, "folders.add");
        QTRY_VERIFY_WITH_TIMEOUT(add->finished(), 10000);
        selector.stop();
        QCOMPARE(add->state(), EchoPluginOperation::Succeeded);
        QVERIFY(add->result().toMap().value("ok").toBool());
        auto *scan = runtime.executeCommand(installation, "library.scan");
        QTRY_VERIFY(scan->finished());
        QCOMPARE(scan->state(), EchoPluginOperation::Succeeded);
        QTRY_VERIFY_WITH_TIMEOUT(scope->ui()->state().value("scan").toMap().value("done").toBool(),
                                 20000);
        QCOMPARE(scope->ui()->state().value("scan").toMap().value("count").toInt(), 3);
        return;
    }
    QVERIFY(host.router.navigate("local-music:page:local-browser"));
    auto window = ecosystemWindow(host, R"QML(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {
    id: win
    required property RouteController router
    required property ShellSettings settings
    width: 1100; height: 800; visible: true
    RouteHost { anchors.fill: parent; router: win.router; settings: win.settings }
})QML",
                                  {{"router", QVariant::fromValue(&host.router)},
                                   {"settings", QVariant::fromValue(&host.shellSettings)}});
    auto *mainWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(mainWindow);
    host.mainWindow = mainWindow;
    QVERIFY(QTest::qWaitForWindowExposed(mainWindow));
    QTRY_VERIFY(browser(mainWindow->contentItem()));
    auto *page = browser(mainWindow->contentItem());
    QCOMPARE(page->property("plugin").value<EchoPluginContext *>()->id(), "local-music");
    QTRY_COMPARE_WITH_TIMEOUT(page->property("songTotal").toInt(), 3, 10000);
    qInfo() << "RESTORED_PAGE" << page->size() << page->property("libraryReady")
            << page->property("pageRows");
    const int index = phase.toInt();
    QTRY_VERIFY(songRow(page, index));
    auto *row = songRow(page, index);
    const auto selectedId = row->property("resourceId").toString();
    qInfo().noquote() << "RESTART_CLICK" << index << selectedId << row->property("title");
    QTest::mouseMove(mainWindow, row->mapToScene({row->width() / 2, row->height() / 2}).toPoint());
    auto *button = visual(row, "songIndexPlay");
    QVERIFY(button);
    QTRY_VERIFY(button->isVisible() && button->isEnabled());
    QTest::mouseClick(mainWindow, Qt::LeftButton, Qt::NoModifier,
                      button->mapToScene({button->width() / 2, button->height() / 2}).toPoint());
    QTRY_COMPARE_WITH_TIMEOUT(host.playback.queueCatalog()->activeKey(),
                              "plugin/local-music/library", 10000);
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.currentTrack().id.endsWith(selectedId), 10000);
    qInfo().noquote() << "RESTART_TRACK" << host.playback.currentTrack().id
                      << host.playback.currentTrack().catalogMetadata.value("plugin_source");
    QVERIFY(host.playback.currentTrack()
                .catalogMetadata.value("plugin_source")
                .toString()
                .startsWith("local-music/local:"));
    QVERIFY2(QTest::qWaitFor(
                 [&] { return host.playback.playing() || !host.playback.errorMessage().isEmpty(); },
                 15000),
             qPrintable(host.playback.errorMessage()));
    QVERIFY2(host.playback.playing(), qPrintable(host.playback.errorMessage()));
}
