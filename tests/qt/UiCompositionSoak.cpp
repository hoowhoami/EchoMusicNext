#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "plugins/PluginRuntimeTestFixture.h"
#include "AccountUiFixture.h"
#include "WindowController.h"
#include "EchoUiPartRegistry.h"
#if __has_include("EchoUiContribution.h")
#include "EchoUiContribution.h"
#define ECHO_HAS_UI_COMPOSITION 1
#else
#define ECHO_HAS_UI_COMPOSITION 0
#endif
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QStyleHints>
#include <QTimer>
#include <QEventLoop>
#include "SearchModel.h"
#include "LyricParser.h"
#include "LyricModel.h"
#include "LyricTextController.h"
#include <atomic>
#include <initializer_list>
#include <sys/resource.h>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif

namespace
{
qint64 objectCount(std::initializer_list<QObject *> roots)
{
    QSet<QObject *> seen;
    const auto visit = [&](auto &&self, QObject *object) -> void {
        if (!object || seen.contains(object))
            return;
        seen.insert(object);
        for (auto *child : object->children())
            self(self, child);
    };
    for (auto *root : roots)
        visit(visit, root);
    return seen.size();
}
QJsonObject usage()
{
    rusage value{};
    getrusage(RUSAGE_SELF, &value);
    const auto seconds = [](const timeval &v) { return v.tv_sec + v.tv_usec / 1e6; };
    QJsonObject result{{"cpuSeconds", seconds(value.ru_utime) + seconds(value.ru_stime)}};
#ifdef Q_OS_MACOS
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS) {
        result["rssBytes"] = qint64(info.resident_size);
        result["footprintBytes"] = qint64(info.phys_footprint);
    }
#endif
    return result;
}
bool pausedLyricMotionIdle(QQuickWindow &window, QQmlEngine &engine)
{
    QSet<QObject *> seen;
    QList<QObject *> pending{&window, &engine};
    while (!pending.isEmpty()) {
        auto *object = pending.takeLast();
        if (!object || seen.contains(object))
            continue;
        seen.insert(object);
        if (object->inherits("EchoMusic::LyricSpring") && object->property("running").toBool()) {
            QQuickItem *owner = nullptr;
            bool pooled = false;
            for (auto *parent = object->parent(); parent; parent = parent->parent()) {
                pooled = pooled || parent->property("pooled").toBool();
                if (!owner)
                    owner = qobject_cast<QQuickItem *>(parent);
            }
            if (!pooled && owner && owner->window() == &window && owner->isVisible())
                return false;
        }
        if (auto *item = qobject_cast<QQuickItem *>(object))
            for (auto *child : item->childItems())
                pending.append(child);
        pending.append(object->children());
    }
    return true;
}
void makeAudio(const QString &path, int seconds)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qFatal("Cannot create isolated audio workload");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    const int bytes = seconds * 48000 * 2;
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(bytes);
    QByteArray second;
    QDataStream samples(&second, QIODevice::WriteOnly);
    samples.setByteOrder(QDataStream::LittleEndian);
    for (int i = 0; i < 48000; ++i)
        samples << qint16(std::sin(i * 2.0 * 3.141592653589793 * 440 / 48000) * 12000);
    for (int i = 0; i < seconds; ++i)
        file.write(second);
}
} // namespace

/// Opt-in macOS Release soak, with isolated services and production QML.
class UiCompositionSoak : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QQuickWindow::setDefaultAlphaBuffer(true);
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        configureUiTypography();
        // Host starts in System mode. Pin the initial application scheme as
        // well as the later screenshot modes: otherwise day/night changes
        // alter whether Qt reuses an existing text node on the first capture.
        const auto scheme = qEnvironmentVariable("ECHO_UI_SOAK_INITIAL_SCHEME", "dark");
        QVERIFY(scheme == "dark" || scheme == "light");
        QGuiApplication::styleHints()->setColorScheme(scheme == "dark" ? Qt::ColorScheme::Dark
                                                                       : Qt::ColorScheme::Light);
        qInfo() << "native_soak_initial_scheme" << QGuiApplication::styleHints()->colorScheme();
    }
    void run()
    {
        const int duration = qEnvironmentVariableIntValue("ECHO_UI_SOAK_SECONDS");
        if (duration < 20)
            QSKIP("Set ECHO_UI_SOAK_SECONDS >= 20 and ECHO_UI_SOAK_OUTPUT");
        const auto mode = qEnvironmentVariable("ECHO_UI_SOAK_MODE", "none");
        QVERIFY(mode == "none" || mode == "disabled" || mode == "retired");
        const auto output = qEnvironmentVariable("ECHO_UI_SOAK_OUTPUT");
        QVERIFY(!output.isEmpty());
        QVERIFY(QDir().mkpath(output));
        Host host;
        QSignalSpy qmlWarnings(&host.engine, &QQmlEngine::warnings);
        Test::AccountUiFixture account(host.api);
        WindowController windowService(host.settings, false);
        host.shell.attachPlayback(host.playback);
        host.playback.setVolume(0);
        auto *registry = EchoUiPartRegistry::forEngine(host.engine);
#if ECHO_HAS_UI_COMPOSITION
        registry->setEnabled(mode != "disabled");
#else
        QVERIFY(mode == "none");
#endif
        SearchModel search(host.api);
        const auto blank = host.temporary.path() + "/Page.qml";
        write(blank, "import EchoMusic.Shell\nRoutePage {}\n");
        QVERIFY(host.router.registerPage(
            {"soak", "/soak", QUrl::fromLocalFile(blank), {}, false, false, false, {}}));
        QVERIFY(host.router.navigate("soak"));
        QQmlComponent component(&host.engine, QUrl("qrc:/qt/qml/EchoMusic/App/Main.qml"));
        auto root = std::unique_ptr<QObject>(component.createWithInitialProperties(
            {{"shellController", QVariant::fromValue(&host.shell)},
             {"router", QVariant::fromValue(&host.router)},
             {"shellSettings", QVariant::fromValue(&host.shellSettings)},
             {"playbackService", QVariant::fromValue(&host.playback)},
             {"searchService", QVariant::fromValue(&search)},
             {"windowService", QVariant::fromValue(&windowService)},
             {"accountController", QVariant::fromValue(&account.controller)},
             {"libraryController", QVariant::fromValue(&account.library)}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        windowService.attach(window);
        window->resize(1100, 800);
        QVERIFY(Test::prepareNativeInput(window));
        QVERIFY(window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software);
        qInfo() << "native_soak_backend" << window->rendererInterface()->graphicsApi() << "dpr"
                << window->devicePixelRatio();
        Test::moveNativePointer(window, {window->width() - 50, window->height() / 2});
        host.mainWindow = window;
        auto *floating = windowService.floating();
        QObject *pageOwner = nullptr;
        QTRY_VERIFY(pageOwner = root->findChild<QObject *>("playbackPageOwner"));
        QTRY_VERIFY_WITH_TIMEOUT(host.playback.engineReady(), 15000);
        QTest::qWait(1000);
#if ECHO_HAS_UI_COMPOSITION
        if (mode == "retired" || mode == "disabled") {
            const auto source = pluginPackage(
                host.temporary.path(), "soak-lifecycle",
                "export function activate(c) { c.ui.parts.contribute('wrap', "
                "{role:'shell.playerbar'}, {operation:'wrap',component:'Wrap.qml'}); }");
            write(source + "/Wrap.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Item {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName: "soakWrapper"
 EchoUiHostContent { anchors.fill: parent; part: parent.part }
})");
            host.runtime.importDirectory(source);
            QTRY_VERIFY(!host.runtime.installationForId("soak-lifecycle").isEmpty());
            const auto installation = host.runtime.installationForId("soak-lifecycle");
            QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
            if (mode == "disabled")
                QVERIFY(!visual(window->contentItem(), "soakWrapper"));
            for (int cycle = 0; mode == "retired" && cycle < 100; ++cycle) {
                QTRY_VERIFY(visual(window->contentItem(), "soakWrapper"));
                QVERIFY(host.runtime.setEnabled(installation, false));
                QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Disabled);
                QTRY_VERIFY(!visual(window->contentItem(), "soakWrapper"));
                if (cycle < 99) {
                    QVERIFY(host.runtime.setEnabled(installation, true));
                    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
                }
            }
        }
#endif
        const auto capture = [&](const QString &scene, QQuickWindow *target) {
            // A stationary cursor can fall onto a newly created lyric row.
            // Compare explicit idle and hover states, not incidental enter delivery.
            Test::moveNativePointer(target, {-20, -20});
            for (auto theme : {ThemeStore::Dark, ThemeStore::Light})
                for (double scale : {1., 1.25}) {
                    host.theme.setMode(theme);
                    host.shellSettings.resetZoom();
                    if (scale > 1)
                        for (int step = 0; step < 5; ++step)
                            host.shellSettings.zoomIn();
                    QTest::qWait(750);
                    const auto name = QString("%1-%2-%3.png").arg(scene).arg(int(theme)).arg(scale);
                    if (scene == "playback") {
                        // Fixed sleep is insufficient for the existing delayed
                        // scroll springs after a short seek. Wait for their real
                        // completion; never snap production motion for a capture.
                        QElapsedTimer settling;
                        settling.start();
                        if (!QTest::qWaitFor(
                                [&] { return pausedLyricMotionIdle(*target, host.engine); }, 5000))
                            return false;
                        qInfo() << "paused_soak_motion_settle_ms" << name << settling.elapsed();
                    }
                    if (!target->grabWindow().save(output + '/' + name))
                        return false;
                    if (scene == "playback") {
                        auto *list = visual(target->contentItem(), "playbackLyricList");
                        auto *current =
                            list ? list->property("currentItem").value<QQuickItem *>() : nullptr;
                        auto *row =
                            current ? visual(current, QString("lyricTextRow%1")
                                                          .arg(current->property("index").toInt()))
                                    : nullptr;
                        if (!row)
                            return false;
                        QPointer<QObject> hover;
                        for (auto *child : row->findChildren<QObject *>())
                            if (child->inherits("QQuickHoverHandler")) {
                                hover = child;
                                break;
                            }
                        if (!hover)
                            return false;
                        Test::moveNativePointer(
                            target, row->mapToScene(row->boundingRect().center()).toPoint());
                        if (!QTest::qWaitFor(
                                [&] { return hover && hover->property("hovered").toBool(); }, 1000))
                            return false;
                        QTest::qWait(300);
                        const auto hoverName =
                            QString("playback-hover-%1-%2.png").arg(int(theme)).arg(scale);
                        if (!target->grabWindow().save(output + '/' + hoverName))
                            return false;
                        Test::moveNativePointer(target, {-20, -20});
                    }
                }
            host.theme.setMode(ThemeStore::Dark);
            host.shellSettings.resetZoom();
            return true;
        };
        QVERIFY(capture("shell", window));
        // Keep media/list contents identical to the archived 900-second baseline
        // when a shorter measurement is requested; only the sampling time changes.
        const int mediaDuration = std::max(duration, 900) + 60;
        makeAudio(host.temporary.path() + "/workload.wav", mediaDuration);
        PlaybackTrack track;
        track.id = "soak-local";
        track.title = "Local audio workload";
        track.localPath = host.temporary.path() + "/workload.wav";
        track.durationMs = mediaDuration * 1000;
        QFile samples(output + "/samples.jsonl");
        QVERIFY(samples.open(QIODevice::WriteOnly));
        const auto initial = usage();
#ifdef Q_OS_MACOS
        QVERIFY(initial.contains("rssBytes") && initial.contains("footprintBytes"));
#endif
        const auto objects = [&] {
            return objectCount({root.get(), &host.engine, &host.runtime, &windowService});
        };
        const auto initialObjects = objects();
        QElapsedTimer clock;
        clock.start();
        const auto frames = std::make_shared<std::atomic<qint64>>(0);
        QList<QMetaObject::Connection> frameConnections;
        frameConnections.append(connect(
            window, &QQuickWindow::frameSwapped, this, [frames] { ++*frames; },
            Qt::DirectConnection));
        auto disconnectFrames = qScopeGuard([&] {
            for (auto connection : frameConnections)
                disconnect(connection);
        });
        int phase = -1;
        QPointer<QQuickWindow> measuredWindow = window;
        const bool strict = qEnvironmentVariableIntValue("ECHO_UI_SOAK_STRICT_CONDITIONS");
        const bool exerciseRecovery =
            qEnvironmentVariableIntValue("ECHO_UI_SOAK_VALIDATE_FOCUS_RECOVERY");
        qint64 validMs = 0;
        int invalidIntervals = 0;
        quint64 conditionEpoch = 0;
        const auto conditions = [&]() -> QVariantMap {
            auto *target = measuredWindow.data();
            QVariantMap state = target ? Test::nativeWindowInputPolicy(target) : QVariantMap();
            state["qtActive"] = target && target->isActive();
            state["qtExposed"] = target && target->isExposed();
            state["qtVisible"] =
                target && target->isVisible() && target->visibility() != QWindow::Minimized;
            state["applicationActive"] =
                QGuiApplication::applicationState() == Qt::ApplicationActive;
            state["valid"] =
                state.value("qtActive").toBool() && state.value("qtExposed").toBool() &&
                state.value("qtVisible").toBool() && state.value("applicationActive").toBool() &&
                state.value("appActive").toBool() && state.value("appForeground").toBool() &&
                state.value("nativeVisible").toBool() && !state.value("nativeOccluded").toBool() &&
                state.value("nativeKey").toBool();
            return state;
        };
        QObject conditionOwner;
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, &conditionOwner,
                [&](Qt::ApplicationState) { ++conditionEpoch; });
        connect(window, &QWindow::activeChanged, &conditionOwner, [&] { ++conditionEpoch; });
        QTimer conditionTimer;
        conditionTimer.setInterval(250);
        connect(&conditionTimer, &QTimer::timeout, &conditionOwner, [&] {
            if (!conditions().value("valid").toBool())
                ++conditionEpoch;
        });
        if (strict)
            conditionTimer.start();
        qint64 lastObjects = initialObjects;
        while ((strict ? validMs : clock.elapsed()) < qint64(duration) * 1000) {
            QVERIFY2(clock.elapsed() < qint64(duration + 150) * 1000,
                     "Unable to obtain the requested valid foreground sampling duration");
            const int next =
                std::min(3, int((strict ? validMs : clock.elapsed()) * 4 / (duration * 1000)));
            if (phase != next) {
                phase = next;
                if (phase == 1) {
                    host.playback.playTracks({track}, 0, "Soak", "soak-local");
                    QTRY_VERIFY_WITH_TIMEOUT(host.playback.playing() && !host.playback.loading(),
                                             15000);
                } else if (phase == 2) {
                    if (qEnvironmentVariableIsSet("ECHO_UI_SOAK_SOURCE_POSITION")) {
                        const int sourcePosition =
                            qEnvironmentVariableIntValue("ECHO_UI_SOAK_SOURCE_POSITION");
                        host.playback.seek(sourcePosition);
                        QTRY_VERIFY(qAbs(host.playback.positionMs() - sourcePosition) < 100);
                    }
                    QString krc;
                    const auto text = QStringLiteral("歌词绘制与布局测试 Sample line");
                    for (int second = 0; second < mediaDuration; second += 4) {
                        krc += QString("[%1,4000]").arg(second * 1000);
                        for (int index = 0; index < text.size(); ++index)
                            krc += QString("<%1,%2,0>")
                                       .arg(index * 4000 / text.size())
                                       .arg(4000 / text.size()) +
                                   text[index];
                        krc += '\n';
                    }
                    const auto parsed = LyricParser::parse(LyricParser::Source::Document,
                                                           {{"format", "krc"}, {"content", krc}});
                    QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
                    QVERIFY(host.playback.lyricText()->replace(parsed.lyrics, track.id));
                    QVERIFY(QMetaObject::invokeMethod(pageOwner, "open",
                                                      Q_ARG(QVariant, QVariant(QVariantMap{}))));
                    host.playback.pause();
                    QTRY_VERIFY(!host.playback.playing());
                    host.playback.seek(42000);
                    QTRY_VERIFY(qAbs(host.playback.positionMs() - 42000) < 100);
                    QVERIFY(capture("playback", window));
                    host.playback.play();
                    QTRY_VERIFY(host.playback.playing());
                } else if (phase == 3) {
                    QPointer<QObject> closingPage = pageOwner->property("item").value<QObject *>();
                    QVERIFY(closingPage);
                    QVERIFY(QMetaObject::invokeMethod(pageOwner, "close"));
                    // Finish the real popup exit before hiding its native host;
                    // otherwise the next phase cancels nested viewport incubation.
                    QTRY_VERIFY(closingPage.isNull());
                    floating->showMini();
                    window->hide();
                    QQuickWindow *mini = nullptr;
                    QTRY_VERIFY([&] {
                        for (auto *candidate : QGuiApplication::allWindows())
                            if (candidate->objectName() == "miniPlayerWindow") {
                                mini = qobject_cast<QQuickWindow *>(candidate);
                                return mini && mini->isVisible();
                            }
                        return false;
                    }());
                    frameConnections.append(connect(
                        mini, &QQuickWindow::frameSwapped, this, [frames] { ++*frames; },
                        Qt::DirectConnection));
                    measuredWindow = mini;
                    connect(mini, &QWindow::activeChanged, &conditionOwner,
                            [&] { ++conditionEpoch; });
                    host.playback.pause();
                    QTRY_VERIFY(!host.playback.playing());
                    host.playback.seek(42000);
                    QTRY_VERIFY(qAbs(host.playback.positionMs() - 42000) < 100);
                    QVERIFY(capture("mini", mini));
                    host.playback.play();
                    QTRY_VERIFY(host.playback.playing());
                }
                if (strict) {
                    QVERIFY(Test::prepareNativeInput(measuredWindow));
                    Test::moveNativePointer(measuredWindow, {-20, -20});
                    QTest::qWait(5000); // Identical phase warm-up, outside measured CPU intervals.
                    qInfo() << "foreground_soak_phase" << phase << conditions();
                    if (exerciseRecovery && phase == 1) {
                        // Preflight only: lose and regain focus within one interval.
                        // The epoch must reject it even if both endpoints look valid.
                        QTimer::singleShot(200, &conditionOwner, [window] { window->hide(); });
                        QTimer::singleShot(600, &conditionOwner, [window] {
                            window->show();
                            window->raise();
                            window->requestActivate();
                        });
                    }
                }
            }
            if (strict && !conditions().value("valid").toBool()) {
                auto recovery = usage();
                recovery["phase"] = "condition-recovery";
                recovery["elapsedMs"] = clock.elapsed();
                recovery["validElapsedMs"] = validMs;
                recovery["conditions"] = QJsonObject::fromVariantMap(conditions());
                samples.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact) + '\n');
                samples.flush();
                QVERIFY(Test::prepareNativeInput(measuredWindow));
                Test::moveNativePointer(measuredWindow, {-20, -20});
                QTest::qWait(1500);
                continue;
            }
            const auto beforeConditions = conditions();
            const auto epoch = conditionEpoch;
            const auto beforeFrames = frames->load();
            const auto beforeCpu = usage().value("cpuSeconds").toDouble();
            QElapsedTimer interval;
            interval.start();
            // Use the application's blocking native event loop. QTest::qWait
            // pumps events with polling sleeps, which alters render pacing.
            QEventLoop wait;
            QTimer sampleTimer;
            sampleTimer.setTimerType(Qt::PreciseTimer);
            sampleTimer.setSingleShot(true);
            connect(&sampleTimer, &QTimer::timeout, &wait, &QEventLoop::quit);
            sampleTimer.start(1000);
            wait.exec();
            const auto intervalMs = interval.elapsed();
            const auto afterCpu = usage().value("cpuSeconds").toDouble();
            const auto intervalFrames = frames->load() - beforeFrames;
            const auto afterConditions = conditions();
            const bool valid =
                !strict || (beforeConditions.value("valid").toBool() &&
                            afterConditions.value("valid").toBool() && conditionEpoch == epoch);
            if (valid)
                validMs += intervalMs;
            else
                ++invalidIntervals;
            if (phase >= 2)
                QVERIFY(host.playback.lyrics()->rowCount() > 0);
            QCOMPARE(host.playback.playing(), phase > 0);
            if (clock.elapsed() / 1000 % 10 == 0)
                lastObjects = objects();
            auto sample = usage();
            sample["elapsedMs"] = clock.elapsed();
            sample["validElapsedMs"] = validMs;
            sample["valid"] = valid;
            sample["intervalMs"] = intervalMs;
            sample["intervalCpuSeconds"] = afterCpu - beforeCpu;
            sample["intervalFrames"] = intervalFrames;
            sample["conditionEpoch"] = qint64(conditionEpoch);
            sample["conditions"] = QJsonObject::fromVariantMap(afterConditions);
            sample["phase"] = phase;
            sample["objects"] = lastObjects;
            sample["parts"] = registry->partCount();
#if ECHO_HAS_UI_COMPOSITION
            sample["structureWatches"] = registry->structureWatchCount();
#else
            sample["structureWatches"] = 0;
#endif
            sample["frames"] = frames->load();
            sample["playing"] = host.playback.playing();
            sample["positionMs"] = host.playback.positionMs();
            sample["lyricRows"] = host.playback.lyrics()->rowCount();
            sample["mode"] = mode;
            sample["compositionBuild"] = bool(ECHO_HAS_UI_COMPOSITION);
            sample["mainExposed"] = window->isExposed();
            sample["activeExposed"] = measuredWindow && measuredWindow->isExposed();
            sample["applicationState"] = int(QGuiApplication::applicationState());
            samples.write(QJsonDocument(sample).toJson(QJsonDocument::Compact) + '\n');
            samples.flush();
        }
        floating->leaveMini();
        window->show();
        host.playback.pause();
        QTest::qWait(1500);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto final = usage();
        final["elapsedMs"] = clock.elapsed();
        final["validElapsedMs"] = validMs;
        final["strictConditions"] = strict;
        final["invalidIntervals"] = invalidIntervals;
        conditionTimer.stop();
        final["objects"] = objects();
        final["parts"] = registry->partCount();
#if ECHO_HAS_UI_COMPOSITION
        final["structureWatches"] = registry->structureWatchCount();
        QCOMPARE(registry->structureWatchCount(), 0);
#endif
        final["initialObjects"] = initialObjects;
        final["initialUsage"] = initial;
        final["phase"] = "final";
        samples.write(QJsonDocument(final).toJson(QJsonDocument::Compact) + '\n');
        QCOMPARE(qmlWarnings.size(), 0);
        if (exerciseRecovery)
            QVERIFY(invalidIntervals > 0);
    }
};
QTEST_MAIN(UiCompositionSoak)
#include "UiCompositionSoak.moc"
