#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"

void PluginRuntimeTest::settingsContributionRetiresWithScope()
{
    Host host;
    const auto source =
        package(host.temporary.path(),
                "export function activate(c) { c.ui.registerSettings('Settings.qml'); }",
                {"lifecycle", "ui.settings"});
    write(source + "/Settings.qml",
          "import QtQuick\nItem { required property var plugin; implicitHeight: 100 }");
    host.runtime.importDirectory(source);
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    auto *ui = host.runtime.context(host.installation())->ui();
    QVERIFY(!ui->settingsComponent().isEmpty());
    QPointer<EchoPluginUi> retired = ui;
    QVERIFY(host.runtime.setEnabled(host.installation(), false));
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Disabled);
    QTRY_VERIFY(retired.isNull());
}

void PluginRuntimeTest::statisticsUseSampledWireAndRejectInvalidNumbers()
{
    QFile sample(
        QFINDTESTDATA("../../docs/internal/verification/plugin-source-samples/stats.body"));
    QVERIFY(sample.open(QIODevice::ReadOnly));
    const auto bytes = sample.readAll();
    QString error;
    const QString key = "github:hoowhoami/echomusicnextplugins:org.echomusicnext.volume-presets";
    auto stats = PluginMarketplaceStats::parse(bytes, {key}, error);
    QVERIFY(error.isEmpty());
    QCOMPARE(stats.size(), 1);
    QCOMPARE(stats.value(key).toMap().value("installCount").toInt(), 0);
    QCOMPARE(PluginMarketplaceStats::parse(bytes, {}, error).size(), 0);
    auto invalid = bytes;
    invalid.replace("\"installCount\":0", "\"installCount\":\"0\"");
    stats = PluginMarketplaceStats::parse(invalid, {key}, error);
    QVERIFY(!error.isEmpty());
    QVERIFY(stats.isEmpty());
}

void PluginRuntimeTest::spectrumCapabilityAndOwnerLifetime()
{
    Host host;
    const auto source = package(host.temporary.path(), "export function activate(ctx) {}",
                                {"lifecycle", "audio.spectrum"});
    host.runtime.importDirectory(source);
    QTRY_COMPARE(host.runtime.state(host.installation()), PluginRuntime::Active);
    QVERIFY(!host.runtime.context(host.installation())->audio()->spectrum()->subscribe());
    QCOMPARE(host.playback.spectrum()->subscriberCount(), 0);
    auto owner = std::make_unique<QObject>();
    const auto id =
        host.playback.spectrum()->subscribe(owner.get(), {{"fps", 999}, {"fftSize", 300}});
    QVERIFY(id);
    QCOMPARE(PlayerSpectrum::normalize({{"fps", 999}, {"fftSize", 300}}).value("fps").toInt(), 60);
    QCOMPARE(PlayerSpectrum::normalize({{"fps", 999}, {"fftSize", 300}}).value("fftSize").toInt(),
             2048);
    QCOMPARE(host.playback.spectrum()->subscriberCount(), 1);
    owner.reset();
    QCOMPARE(host.playback.spectrum()->subscriberCount(), 0);
}

void PluginRuntimeTest::builtAuthorPackageCommands()
{
    const auto source = qEnvironmentVariable("ECHO_AUTHOR_PLUGIN_PACKAGE");
    if (source.isEmpty())
        QSKIP("Set ECHO_AUTHOR_PLUGIN_PACKAGE to the built spectrum-visualizer .echomod");
    Host host;
    host.shell.attachPlayback(host.playback);
    host.shell.attachTheme(host.theme);
    host.theme.setMotionEnabled(false);
    const auto originalVolume = host.playback.volume();
    host.runtime.importDirectory(source);
    QTRY_VERIFY(!host.runtime.loading());
    const auto installation = host.runtime.installationForId("spectrum-visualizer");
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QList<QPointer<EchoPluginOperation>> operations;
    QList<QJSValue> operationRoots;
    auto cleanupOperations = qScopeGuard([&] {
        for (const auto &op : operations)
            if (op)
                delete op.data();
    });
    // A native QPointer does not root a JavaScript-owned completed result.
    // Screenshot/render cycles can collect it between QTRY evaluations.
    const auto retain = [&](EchoPluginOperation *op) {
        // Completion deliberately switches ownership on the next tick.
        // A persistent JS wrapper roots the result through that transition.
        operationRoots.append(host.engine.newQObject(op));
        operations.append(op);
        return QPointer<EchoPluginOperation>(op);
    };
    QCOMPARE(host.playback.volume(), originalVolume);
    QTRY_COMPARE(host.shell.pluginSurfaces()->entries().size(), 3);
    QCOMPARE(scope->ui()->state().value("showLyricControls").toBool(), true);
    QCOMPARE(scope->ui()->state().value("showPlayerBar").toBool(), false);
    QPointer<EchoPluginOperation> patch = retain(scope->executeCommand(
        "settings.patch",
        {{"showPlayerBar", true}, {"showMiniPlayer", true}, {"mode", "hybrid"}, {"fps", 30}}));
    QTRY_VERIFY(patch && patch->finished());
    QCOMPARE(patch->state(), EchoPluginOperation::Succeeded);
    QPointer<EchoPluginOperation> stored = retain(scope->io()->readValue("settings"));
    QTRY_COMPARE(stored->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(stored->result().toMap().value("mode").toString(), "hybrid");
    QVERIFY(!scope->ui()->settingsComponent().isEmpty());
    QStringList warnings;
    connect(&host.engine, &QQmlEngine::warnings, &host.engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    FloatingPlayerController floating(host.settings, false);
    floating.toggleMini();
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
import EchoMusic.Playback
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Plugins
import EchoMusic.PluginSdk
ApplicationWindow {
    id: win
    required property ShellController shell
    required property RouteController router
    required property PlaybackService playback
    required property FloatingPlayerController floating
    required property PluginRuntime runtime
    required property EchoPluginContext scope
    required property string installation
    function openSettings() { settingsOwner.open(); }
    function closeSettings() { settingsOwner.close(); }
    EchoPopupLoader {
        id: settingsOwner
        hostItem: win.contentItem
        sourceComponent: Component {
            PluginSettingsDialog { runtime: win.runtime; scope: win.scope; installationId: win.installation; uiScale: win.uiScale }
        }
    }
    property real uiScale: 1
    width: 1100; height: 340; visible: true; color: Theme.main
    Item {
        width: win.width / win.uiScale; height: win.height / win.uiScale
        scale: win.uiScale; transformOrigin: Item.TopLeft
        PlayerBar { width: parent.width; y: 20; controller: win.shell; router: win.router; uiScale: win.uiScale; manageCapacity: false }
        PlayerBar { width: parent.width; y: 230; lyricStyle: true; controller: win.shell; router: win.router; uiScale: win.uiScale; manageCapacity: false }
    }
    MiniPlayerWindow { objectName: "spectrumMini"; service: win.floating; playback: win.playback; pluginSurfaces: win.shell.pluginSurfaces }
})",
                      QUrl("qrc:/spectrum-production-ui.qml"));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{"shell", QVariant::fromValue(&host.shell)},
                                               {"router", QVariant::fromValue(&host.router)},
                                               {"playback", QVariant::fromValue(&host.playback)},
                                               {"floating", QVariant::fromValue(&floating)},
                                               {"runtime", QVariant::fromValue(&host.runtime)},
                                               {"scope", QVariant::fromValue(scope)},
                                               {"installation", installation}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *mini = root->findChild<QQuickWindow *>("spectrumMini");
    QVERIFY(window && mini);
    QTest::qWait(200);
    std::function<void(QQuickItem *)> describe = [&](QQuickItem *item) {
        if (item->objectName() == "spectrumVisualizerLayer" ||
            QString(item->metaObject()->className()).contains("PluginSurfaceHost"))
            qInfo() << item->metaObject()->className() << item->property("target")
                    << item->property("placement") << item->size() << item->implicitHeight()
                    << item->isVisible() << item->property("live") << item->property("allowed");
        for (auto *child : item->childItems())
            describe(child);
    };
    describe(window->contentItem());
    describe(mini->contentItem());
    QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 3);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
    // Quiet, real local PCM passes through the production decoder, mixer,
    // output callback, FFT, FFI and typed subscriber; no injected frames.
    QByteArray pcm;
    QDataStream data(&pcm, QIODevice::WriteOnly);
    data.setByteOrder(QDataStream::LittleEndian);
    constexpr int rate = 48000, seconds = 60;
    for (int i = 0; i < rate * seconds; ++i)
        data << qint16(std::sin(i * 2.0 * 3.141592653589793 * 1000 / rate) * 12000);
    QByteArray wave;
    QDataStream header(&wave, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header.writeRawData("RIFF", 4);
    header << quint32(36 + pcm.size());
    header.writeRawData("WAVEfmt ", 8);
    header << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2)
           << quint16(2) << quint16(16);
    header.writeRawData("data", 4);
    header << quint32(pcm.size());
    wave.append(pcm);
    const auto path = host.temporary.path() + "/1000hz.wav";
    write(path, wave);
    PlaybackTrack track;
    track.id = "spectrum-tone";
    track.title = "1000 Hz";
    track.localPath = path;
    track.durationMs = seconds * 1000;
    host.playback.setVolume(8);
    auto restore = qScopeGuard([&] { host.playback.setVolume(originalVolume); });
    QPointer<EchoPluginSpectrumSubscription> sub =
        scope->audio()->spectrum()->subscribe({{"fps", 30},
                                               {"binCount", 512},
                                               {"fftSize", 8192},
                                               {"scale", "linear"},
                                               {"smoothing", 0.0},
                                               {"includeWaveform", true}});
    QVERIFY(sub && sub->active());
    QSignalSpy frames(sub, &EchoPluginSpectrumSubscription::frameChanged);
    host.playback.playTracks({track}, 0, "Spectrum test", "spectrum-test");
    QTRY_VERIFY2_WITH_TIMEOUT(host.playback.playing() && !host.playback.loading(),
                              qPrintable(host.playback.errorMessage()), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(sub->frame().toMap().value("source") == "player" &&
                                 sub->frame().toMap().value("rms").toDouble() > 0.005 &&
                                 sub->frame().toMap().value("fftSize").toInt() == 8192,
                             10000);
    auto frame = sub->frame().toMap();
    const auto bins = frame.value("bins").toList();
    QCOMPARE(bins.size(), 512);
    QCOMPARE(frame.value("fftSize").toInt(), 8192);
    QVERIFY(frame.value("sampleRate").toInt() >= 16000);
    QVERIFY(frame.value("rms").toDouble() > 0.005);
    QVERIFY(frame.value("peak").toDouble() > frame.value("rms").toDouble());
    QCOMPARE(frame.value("waveform").toList().size(), 8192);
    auto maximum = std::max_element(bins.begin(), bins.end(),
                                    [](auto a, auto b) { return a.toDouble() < b.toDouble(); });
    const auto peakHz = 20 + (std::distance(bins.begin(), maximum) + .5) * (20000 - 20) / 512;
    QVERIFY2(std::abs(peakHz - 1000) < 150, qPrintable(QString::number(peakHz)));
    qInfo() << "Actual output PCM" << frame.value("sampleRate") << "FFT" << frame.value("fftSize")
            << "peak Hz" << peakHz << "RMS" << frame.value("rms");
    host.playback.pause();
    QTRY_VERIFY(!host.playback.spectrum()->status().value("running").toBool());
    QTRY_COMPARE(sub->frame().toMap().value("source").toString(), "silence");
    frames.clear();
    QTest::qWait(120);
    QCOMPARE(frames.count(), 0);
    host.playback.play();
    QTRY_VERIFY(host.playback.spectrum()->status().value("running").toBool());
    QTRY_COMPARE(sub->frame().toMap().value("source").toString(), "player");
    sub->setPaused(true);
    frames.clear();
    QTest::qWait(120);
    QCOMPARE(frames.count(), 0);
    sub->setPaused(false);
    QTRY_VERIFY(frames.count() > 0);
    sub->dispose();
    QTRY_VERIFY(sub.isNull());
    // Opt-in Release measurement of the real output/SDK/production surfaces.
    // Process CPU includes every thread; setup, screenshots and SQL writes
    // are excluded from each steady-state interval.
    const int benchmarkMs = qEnvironmentVariableIntValue("ECHO_SPECTRUM_BENCHMARK_MS");
    if (benchmarkMs > 0) {
        host.theme.setMode(ThemeStore::Dark);
        QSignalSpy delivered(host.playback.spectrum(), &PlayerSpectrum::frameReady);
        QSignalSpy rendered(window, &QQuickWindow::frameSwapped);
        QSignalSpy miniRendered(mini, &QQuickWindow::frameSwapped);
        const auto measure = [&](const QString &label) {
            if (window->isVisible())
                QVERIFY(Test::prepareNativeInput(window));
            QTest::qWait(500);
            delivered.clear();
            rendered.clear();
            miniRendered.clear();
            QElapsedTimer elapsed;
            elapsed.start();
            const auto cpu = std::clock();
            QTest::qWait(qBound(1000, benchmarkMs, 20000));
            const auto wall = elapsed.elapsed();
            const auto cpuMs = 1000. * (std::clock() - cpu) / CLOCKS_PER_SEC;
            qInfo().noquote() << QString("SPECTRUM_CPU %1 wall_ms=%2 cpu_ms=%3 percent=%4 "
                                         "frames=%5 rendered=%6 mini_rendered=%7")
                                     .arg(label)
                                     .arg(wall)
                                     .arg(cpuMs, 0, 'f', 1)
                                     .arg(cpuMs * 100 / wall, 0, 'f', 2)
                                     .arg(delivered.count())
                                     .arg(rendered.count())
                                     .arg(miniRendered.count());
            if (label.contains("fps") && !label.startsWith("disabled")) {
                QVERIFY(host.playback.playing() && !host.playback.loading());
                QCOMPARE(host.playback.spectrum()->subscriberCount(), 3);
                const auto minimum =
                    host.runtime.context(installation)->ui()->state().value("fps").toInt() * wall /
                    1000 / 3;
                // Baseline saturation is itself a measured regression; keep
                // recording it, but never silently count a stalled interval
                // as a successful CPU reduction.
                if (delivered.count() < minimum * 3 || rendered.count() < minimum ||
                    miniRendered.count() < minimum)
                    qInfo().noquote() << "SPECTRUM_STALLED" << label;
            }
        };
        for (const auto fps : {15, 30}) {
            for (const auto &mode : {"disabled", "bars", "wave", "hybrid", "mist", "centered"}) {
                patch = retain(scope->executeCommand("settings.patch",
                                                     {{"enabled", QString(mode) != "disabled"},
                                                      {"mode", mode},
                                                      {"fps", fps},
                                                      {"showBackdrop", true}}));
                QTRY_VERIFY(patch && patch->finished());
                QCOMPARE(patch->state(), EchoPluginOperation::Succeeded);
                measure(QString("%1-%2fps").arg(mode).arg(fps));
            }
        }
        host.playback.pause();
        QTRY_VERIFY(!host.playback.spectrum()->status().value("running").toBool());
        measure("paused");
        window->hide();
        mini->hide();
        QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 0);
        measure("hidden");
        return;
    }
    const auto artifacts = qEnvironmentVariable("ECHO_SPECTRUM_ARTIFACTS");
    if (!artifacts.isEmpty())
        QDir().mkpath(artifacts);
    for (const auto &mode : {"bars", "wave", "hybrid", "mist", "centered"}) {
        patch = retain(
            scope->executeCommand("settings.patch", {{"mode", mode}, {"showBackdrop", true}}));
        QTRY_VERIFY(patch && patch->finished());
        QCOMPARE(patch->state(), EchoPluginOperation::Succeeded);
        QTest::qWait(1);
        host.engine.collectGarbage();
        QVERIFY(patch);
        QCOMPARE(patch->state(), EchoPluginOperation::Succeeded);
        for (const auto &theme : {"dark", "light"}) {
            host.theme.setMode(QString(theme) == "dark" ? ThemeStore::Dark : ThemeStore::Light);
            for (double scale : {1.0, 1.25}) {
                window->setProperty("uiScale", scale);
                QTest::qWait(180);
                auto *layer = visual(window->contentItem(), "spectrumVisualizerLayer");
                auto *miniLayer = visual(mini->contentItem(), "spectrumVisualizerLayer");
                QVERIFY(layer && miniLayer);
                QVERIFY(layer->property("frame").toMap().value("source") == "player");
                QVERIFY(!window->grabWindow().isNull());
                int curveCount = 0;
                std::function<void(QQuickItem *)> verifyVectors = [&](QQuickItem *item) {
                    QVERIFY(!QString(item->metaObject()->className()).contains("Canvas"));
                    if (item->property("preferredRendererType").isValid()) {
                        QCOMPARE(item->property("preferredRendererType").toInt(), 4);
                        if (window->rendererInterface()->graphicsApi() ==
                            QSGRendererInterface::Metal)
                            QCOMPARE(item->property("rendererType").toInt(), 4);
                        ++curveCount;
                    }
                    for (auto *child : item->childItems())
                        verifyVectors(child);
                };
                verifyVectors(layer);
                verifyVectors(miniLayer);
                QVERIFY(curveCount >= 2);
                if (!artifacts.isEmpty()) {
                    const auto name = QString("/%1-%2-%3.png").arg(mode, theme).arg(scale);
                    QVERIFY(window->grabWindow().save(artifacts + name));
                    QVERIFY(mini->grabWindow().save(artifacts + "/mini" + name.mid(1)));
                }
            }
        }
    }
    host.playback.pause();
    QTRY_VERIFY(!host.playback.spectrum()->status().value("running").toBool());
    auto *centerLayer = visual(window->contentItem(), "spectrumVisualizerLayer");
    QVERIFY(centerLayer);
    auto *centerClock = centerLayer->findChild<QObject *>("spectrumRenderClock");
    QVERIFY(centerClock);
    QTRY_VERIFY_WITH_TIMEOUT(!centerClock->property("running").toBool(), 3000);
    host.playback.play();
    QTRY_VERIFY(centerClock->property("running").toBool());
    // Exercise the same demand-created settings dialog as the management page.
    window->resize(1100, 900);
    for (const auto mode : {ThemeStore::Dark, ThemeStore::Light}) {
        host.theme.setMode(mode);
        for (const auto scale : {1.0, 1.25}) {
            window->setProperty("uiScale", scale);
            QVERIFY(QMetaObject::invokeMethod(root.get(), "openSettings"));
            QTRY_VERIFY(visual(window->contentItem(), "spectrumVisualizerSettings"));
            QPointer<QQuickItem> body = visual(window->contentItem(), "spectrumVisualizerSettings");
            QTest::qWait(120);
            QVERIFY(body->width() > 400);
            if (mode == ThemeStore::Dark && scale == 1.0) {
                QVERIFY(Test::prepareNativeInput(window));
                auto *toggle = visual(body, "spectrumEnabledSwitch");
                QVERIFY(toggle);
                auto click = [&] {
                    const auto point =
                        toggle->mapToScene({toggle->width() / 2, toggle->height() / 2}).toPoint();
                    Test::moveNativePointer(window, point);
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
                };
                click();
                QTRY_VERIFY(!scope->ui()->state().value("enabled").toBool());
                QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 0);
                click();
                QTRY_VERIFY(scope->ui()->state().value("enabled").toBool());
                QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 3);
            }

            QPointer<QQuickItem> viewport = visual(window->contentItem(), "pluginSettingsViewport");
            QVERIFY(viewport);
            QVERIFY(viewport->property("contentHeight").toReal() > viewport->height());
            QVERIFY(qAbs(viewport->width() - body->width() - 24) < .01);
            QList<QQuickItem *> rows;
            std::function<void(QQuickItem *)> collectRows = [&](QQuickItem *item) {
                if (item->objectName() == "spectrumSettingsRow")
                    rows.append(item);
                for (auto *child : item->childItems())
                    collectRows(child);
            };
            collectRows(body);
            QVERIFY(rows.size() >= 5);
            QVERIFY(!visual(rows[0], "spectrumSettingsDivider")->isVisible());
            for (int i = 1; i < 5; ++i) {
                auto *divider = visual(rows[i], "spectrumSettingsDivider");
                QVERIFY(divider && divider->isVisible());
                QCOMPARE(divider->y(),
                         0.0); // Source separators are between rows, never below the group.
            }
            if (!artifacts.isEmpty())
                QVERIFY(window->grabWindow().save(
                    artifacts + QString("/settings-%1-%2.png").arg(int(mode)).arg(scale)));
            QVERIFY(Test::prepareNativeInput(window));
            ulong wheelTimestamp = 10000;
            const auto wheel = [&](QPoint delta, Qt::ScrollPhase phase) {
                const auto point =
                    viewport->mapToScene({viewport->width() * .2, viewport->height() * .5});
                QWheelEvent event(point, window->mapToGlobal(point.toPoint()), delta,
                                  delta.isNull() ? QPoint{} : delta * 2, Qt::NoButton,
                                  Qt::NoModifier, phase, false, Qt::MouseEventSynthesizedBySystem);
                event.setTimestamp(wheelTimestamp += 16);
                QCoreApplication::sendEvent(window, &event);
            };
            wheel({}, Qt::ScrollBegin);
            for (int i = 0; i < 80 && !viewport->property("atYEnd").toBool(); ++i) {
                wheel({0, -100}, Qt::ScrollUpdate);
                QTest::qWait(16);
            }
            wheel({}, Qt::ScrollEnd);
            QTRY_VERIFY(viewport->property("contentY").toReal() > 0);
            QTRY_VERIFY(viewport->property("atYEnd").toBool());
            auto *reset = visual(body, "spectrumResetSettings");
            QVERIFY(reset);
            const auto resetTop = reset->mapToItem(viewport, {0, 0}).y();
            QVERIFY(resetTop >= 0 && resetTop + reset->height() <= viewport->height() + 1);
            auto *scrollbar = visual(window->contentItem(), "popupScrollBar");
            QVERIFY(scrollbar && scrollbar->isVisible());
            qInfo() << "Settings wheel reached bottom at scale" << scale << "contentY"
                    << viewport->property("contentY");
            if (!artifacts.isEmpty())
                QVERIFY(window->grabWindow().save(
                    artifacts + QString("/settings-bottom-%1-%2.png").arg(int(mode)).arg(scale)));
            QVERIFY(QMetaObject::invokeMethod(root.get(), "closeSettings"));
            QTRY_VERIFY(body.isNull());
            QTRY_VERIFY(viewport.isNull());
        }
    }
    if (QGuiApplication::platformName() == "cocoa") {
        window->showMinimized();
        QTRY_COMPARE(window->visibility(), QWindow::Minimized);
        QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 1);
        window->showNormal();
        QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 3);
    }
    window->hide();
    mini->hide();
    QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 0);
    QTRY_VERIFY(!host.playback.spectrum()->status().value("running").toBool());
    QVERIFY(!visual(window->contentItem(), "spectrumVisualizerLayer"));
    for (int i = 0; i < 3; ++i) {
        window->show();
        mini->show();
        QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 3);
        window->hide();
        mini->hide();
        QTRY_COMPARE(host.playback.spectrum()->subscriberCount(), 0);
    }
    patch = retain(scope->executeCommand("settings.reset"));
    QTRY_VERIFY(patch && patch->finished());
    QCOMPARE(patch->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(scope->ui()->state().value("mode").toString(), "bars");
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    QPointer<EchoPluginContext> retired = scope;
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QTRY_VERIFY(retired.isNull());
    QCOMPARE(host.shell.pluginSurfaces()->entries().size(), 0);
    QCOMPARE(host.playback.spectrum()->subscriberCount(), 0);
    QVERIFY(host.runtime.setEnabled(installation, true));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QCOMPARE(host.runtime.context(installation)->ui()->state().value("mode").toString(), "bars");
    QCOMPARE(host.runtime.context(installation)->ui()->state().value("showPlayerBar").toBool(),
             false);
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
    QCOMPARE(host.playback.spectrum()->subscriberCount(), 0);
}
