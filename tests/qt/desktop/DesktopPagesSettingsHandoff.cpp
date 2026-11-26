#include "DesktopPagesTest.h"
#include "DesktopPagesTestFixture.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiLayout.h"
#include "EchoPluginPermissions.h"
#include "PluginPaths.h"
#include <QQmlIncubationController>

namespace
{
class SectionSources final : public QObject
{
    Q_OBJECT
public:
    explicit SectionSources(QObject *owner) : m_owner(owner)
    {
        connect(owner, SIGNAL(itemChanged()), this, SLOT(observe()));
    }
    QStringList sources;
private slots:
    void observe()
    {
        auto *popup = m_owner->property("item").value<QObject *>();
        if (!popup)
            return;
        // The owner publishes the instance before Popup.open attaches its
        // content to the window overlay; observe that detached tree as well.
        m_loader =
            visual(popup->property("contentItem").value<QQuickItem *>(), "settingsSectionLoader");
        if (m_loader) {
            connect(m_loader, SIGNAL(sourceChanged()), this, SLOT(record()));
            // A cached component can defer sourceChanged until incubation is
            // Ready. Observe Loading too, including a subsequently cancelled load.
            connect(m_loader, SIGNAL(statusChanged()), this, SLOT(record()));
            record();
        }
    }
    void record()
    {
        const auto source = m_loader ? m_loader->property("source").toUrl().toString() : QString();
        if (!source.isEmpty() && !sources.contains(source))
            sources << source;
    }

private:
    QObject *m_owner;
    QPointer<QObject> m_loader;
};

class ConstructionInterrupt final : public QObject
{
public:
    std::atomic<bool> proceed{true};
    int interruptions = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded) {
            ++interruptions;
            proceed.store(false);
        }
        return false;
    }
};
} // namespace

void DesktopPagesTest::settingsSectionIncubationCanBeCancelled()
{
    Session session;
    NetworkPreferences network(session.preferences, session.api);
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("playback.sqlite"), &network);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback, &network));
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *normalController = session.engine.incubationController();
    QQmlIncubationController controlled;
    const auto restoreController =
        qScopeGuard([&] { session.engine.setIncubationController(normalController); });
    session.engine.setIncubationController(&controlled);
    for (bool motion : {false, true}) {
        session.theme.setMotionEnabled(motion);
        session.shell.setMotionEnabled(motion);
        for (int cycle = 0; cycle < 40; ++cycle) {
            session.router.navigate("settings", {{"section", "player"}});
            QPointer<QObject> dialog = session.root->findChild<QObject *>("settingsDialog");
            QVERIFY(dialog);
            auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
            QVERIFY(catalog);
            auto *loader = visual(dialog->property("contentItem").value<QQuickItem *>(),
                                  "settingsSectionLoader");
            QVERIFY(loader);
            QVERIFY(loader->property("asynchronous").toBool());
            // Exercise cancellation both before and during finalization, then
            // complete the replacement section while the close transition runs.
            for (const auto &section :
                 {QString("network"), QString("player"), QString("network")}) {
                catalog->select(section);
                if (cycle % 5) {
                    controlled.incubateFor(cycle % 5);
                    session.engine.collectGarbage();
                    QCoreApplication::processEvents();
                }
            }
            network.apply(cycle % 2 ? "direct" : "system", {}, {}, {}, {}, false, {}, 0, 0);
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            controlled.incubateFor(10);
            QTRY_VERIFY(dialog.isNull());
            QTRY_VERIFY_WITH_TIMEOUT(
                (controlled.incubateFor(20), controlled.incubatingObjectCount() == 0), 15000);
            session.engine.collectGarbage();
            QTRY_VERIFY(!network.busy());
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::settingsEngineNetworkSwitchAndClose_data()
{
    QTest::addColumn<bool>("controlled");
    QTest::addColumn<bool>("motion");
    QTest::addColumn<bool>("takeover");
    for (bool controlled : {false, true})
        for (bool motion : {false, true})
            for (bool takeover : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2%3").arg(controlled ? "partial" : "frames",
                                                                motion ? "animated" : "reduced",
                                                                takeover ? "" : "-native")))
                    << controlled << motion << takeover;
}

void DesktopPagesTest::settingsEngineNetworkSwitchAndClose()
{
    QFETCH(bool, controlled);
    QFETCH(bool, motion);
    QFETCH(bool, takeover);
    Session session;
    session.theme.setMotionEnabled(motion);
    session.shell.setMotionEnabled(motion);
    session.preferences.setValue("network/proxyPolicy",
                                 QVariantMap{{"mode", "fixed_servers"},
                                             {"proxyRules", "http://127.0.0.1:1"},
                                             {"username", "test"}});
    NetworkPreferences network(session.preferences, session.api);
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("playback.sqlite"), &network);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback, &network));
    PluginRuntime runtime(session.engine, session.preferences,
                          {playback,
                           session.theme,
                           session.commands,
                           session.router,
                           session.accounts.account,
                           session.api,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr,
                           {},
                           &session.shell,
                           nullptr,
                           [&] { return session.window; },
                           &network},
                          session.directory.filePath("plugins"));
    QSignalSpy runtimeClosed(&runtime, &PluginRuntime::closed);
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
        runtime.shutdown();
        if (runtimeClosed.isEmpty())
            runtimeClosed.wait(5000);
        if (!runtimeClosed.isEmpty())
            QDir(PluginPaths(runtime.directory()).cache()).removeRecursively();
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    const auto source = session.directory.filePath("takeover-source");
    QVERIFY(QDir().mkpath(source));
    const auto write = [&](const QString &name, const QByteArray &data) {
        QFile file(source + '/' + name);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    QVERIFY(write("manifest.json", R"({
      "format":"echomod","schemaVersion":1,"id":"test.settings-takeover",
      "name":"Settings lifecycle fixture","version":"1.0.0","kind":"qml-js",
      "entry":"entry.mjs","contexts":["main"],"capabilities":[],
      "requires":{"hostVersion":">=0.1.0 <1.0.0","sdkVersion":">=1.0.0 <2.0.0",
      "features":["lifecycle","ui.parts","ui.composition"]}})"));
    QVERIFY(write("entry.mjs", R"(export function activate(ctx) {
      const selector={key:"settingsGithubProxyUrl",owner:"@host"};
      if(!ctx.ui.parts.contribute("network-hint",selector,{operation:"insert",position:"before",
          component:"Takeover.qml",layout:{preferredWidth:240,preferredHeight:36},priority:10}))
        throw new Error(ctx.ui.parts.errorMessage);
      if(!ctx.ui.parts.contribute("network-input",selector,{operation:"hide",priority:10}))
        throw new Error(ctx.ui.parts.errorMessage);
    })"));
    QVERIFY(write("Takeover.qml", R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.PluginSdk
Rectangle {
  objectName: "settingsTakeoverHint"
  required property EchoPluginContext plugin
  required property EchoUiPartRef part
  implicitWidth:240; implicitHeight:36
  Label { anchors.fill:parent; text:"Settings takeover" }
})"));
    runtime.start();
    QTRY_VERIFY(!runtime.loading());
    const auto actualSource = qEnvironmentVariable("ECHO_SETTINGS_TAKEOVER_SOURCE");
    const auto pluginId =
        actualSource.isEmpty() ? QString("test.settings-takeover") : QString("github-accelerate");
    const auto hintName = actualSource.isEmpty() ? QString("settingsTakeoverHint")
                                                 : QString("githubAccelerateTakeover");
    connect(runtime.permissions(), &PluginPermissionStore::confirmationRequested, &runtime,
            [&](const QString &request, const QVariantMap &) {
                runtime.permissions()->decide(request, true);
            });
    if (takeover) {
        runtime.importDirectory(actualSource.isEmpty() ? source : actualSource);
        QTRY_COMPARE(runtime.state(runtime.installationForId(pluginId)), PluginRuntime::Active);
    }
    auto *normalController = session.engine.incubationController();
    QQmlIncubationController partial;
    const auto restoreController =
        qScopeGuard([&] { session.engine.setIncubationController(normalController); });
    if (controlled)
        session.engine.setIncubationController(&partial);
    else
        session.engine.setIncubationController(session.window->incubationController());
    ConstructionInterrupt interrupt;
    int interruptedTargets = 0;
    int incompleteTargets = 0;
    connect(EchoUiPartRegistry::forEngine(session.engine), &EchoUiPartRegistry::added, &interrupt,
            [&](EchoUiPartRegistration *part) {
                if (part->key() == "settingsGithubProxyUrl" &&
                    !EchoUiLayout::constructionComplete(part->target()))
                    ++incompleteTargets;
                if (controlled && part->key() == "settingsGithubProxyUrl") {
                    ++interruptedTargets;
                    interrupt.proceed.store(false);
                }
            });
    if (controlled)
        qApp->installEventFilter(&interrupt);
    const auto removeInterrupt = qScopeGuard([&] { qApp->removeEventFilter(&interrupt); });
    const int delays[] = {0, 1, 4, 16, 33, 0, 2, 8};
    for (int cycle = 0; cycle < 30; ++cycle) {
        session.router.navigate("settings", {{"section", cycle % 2 ? "network" : "player"}});
        QPointer<QObject> dialog = session.root->findChild<QObject *>("settingsDialog");
        QVERIFY(dialog);
        QPointer<QQuickItem> takeoverHint;
        auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
        auto *loader =
            visual(dialog->property("contentItem").value<QQuickItem *>(), "settingsSectionLoader");
        QVERIFY(catalog);
        QVERIFY(loader);
        auto *categories =
            visual(dialog->property("contentItem").value<QQuickItem *>(), "settingsCategories");
        QVERIFY(categories);
        QVERIFY(QMetaObject::invokeMethod(categories, "forceLayout"));
        for (int step = 0; step < 8; ++step) {
            const auto key = (cycle + step) % 2 ? QString("network") : QString("player");
            if (controlled) {
                catalog->select(key);
            } else {
                QQuickItem *entry = nullptr;
                const auto find = [&](auto &&self, QQuickItem *item) -> void {
                    if (item->property("sectionKey").toString() == key)
                        entry = item;
                    for (auto *child : item->childItems())
                        if (!entry)
                            self(self, child);
                };
                find(find, categories);
                QVERIFY(entry);
                const auto point = entry->mapToScene(entry->boundingRect().center()).toPoint();
                QTest::mouseMove(session.window, point);
                QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier, point);
                QCOMPARE(catalog->current(), key);
            }
            const int delay = delays[(cycle + step) % 8];
            if (controlled) {
                interrupt.proceed.store(true);
                partial.incubateWhile(&interrupt.proceed, delay);
                QCoreApplication::processEvents();
            } else if (delay) {
                QTest::qWait(delay);
            }
            if (step % 3 == 0)
                session.engine.collectGarbage();
        }
        // Alternate fully displayed sections and cancellation during creation.
        // Closing the actual toolbar action also covers focus loss callbacks.
        if (cycle % 3 == 0) {
            if (controlled) {
                for (int turn = 0; turn < 3000 && loader->property("status").toInt() != 1; ++turn) {
                    interrupt.proceed.store(true);
                    partial.incubateWhile(&interrupt.proceed, 1);
                    session.engine.collectGarbage();
                    QCoreApplication::processEvents();
                }
                QCOMPARE(loader->property("status").toInt(), 1);
            } else
                QVERIFY(session.settingsSectionReady());
            if (catalog->current() == "network") {
                auto *input = visual(dialog->property("contentItem").value<QQuickItem *>(),
                                     "settingsGithubProxyUrl");
                QVERIFY(input);
                if (takeover) {
                    QTRY_VERIFY((takeoverHint =
                                     visual(dialog->property("contentItem").value<QQuickItem *>(),
                                            hintName)));
                    QVERIFY(!input->isVisible());
                } else {
                    QVERIFY(input->isVisible());
                    QVERIFY(
                        !visual(dialog->property("contentItem").value<QQuickItem *>(), hintName));
                }
            }
            auto *field = visual(dialog->property("contentItem").value<QQuickItem *>(),
                                 cycle % 2 ? "settingsEngineCacheSecs" : "settingsApiTimeout");
            if (field)
                field->forceActiveFocus();
        }
        auto *close =
            visual(dialog->property("contentItem").value<QQuickItem *>(), "settingsClose");
        QVERIFY(close);
        QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                          close->mapToScene(close->boundingRect().center()).toPoint());
        if (controlled)
            partial.incubateFor(cycle % 5);
        QTRY_VERIFY(dialog.isNull());
        QTRY_VERIFY(takeoverHint.isNull());
        if (controlled)
            QTRY_VERIFY_WITH_TIMEOUT((partial.incubateFor(5), partial.incubatingObjectCount() == 0),
                                     5000);
        session.engine.collectGarbage();
    }
    QStringList errors;
    for (const auto &entry : warnings)
        for (const auto &error : entry.value(0).value<QList<QQmlError>>())
            errors << error.toString();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    if (controlled)
        QVERIFY(interruptedTargets > 0);
    QCOMPARE(incompleteTargets, 0);
}

void DesktopPagesTest::settingsSectionsSurviveGcBetweenCreationSteps()
{
    Session session;
    session.theme.setMotionEnabled(false);
    session.shell.setMotionEnabled(false);
    session.preferences.setValue("network/proxyPolicy",
                                 QVariantMap{{"mode", "fixed_servers"},
                                             {"proxyRules", "http://127.0.0.1:1"},
                                             {"username", "test"}});
    NetworkPreferences network(session.preferences, session.api);
    PlaybackService playback(session.api, session.preferences, nullptr,
                             session.directory.filePath("playback.sqlite"), &network);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback, &network));
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *normal = session.engine.incubationController();
    QQmlIncubationController partial;
    const auto restore = qScopeGuard([&] { session.engine.setIncubationController(normal); });
    session.engine.setIncubationController(&partial);
    ConstructionInterrupt interrupt;
    qApp->installEventFilter(&interrupt);
    const auto remove = qScopeGuard([&] { qApp->removeEventFilter(&interrupt); });
    for (int cycle = 0; cycle < 20; ++cycle) {
        session.router.navigate("settings", {{"section", "player"}});
        QPointer<QObject> dialog = session.root->findChild<QObject *>("settingsDialog");
        QVERIFY(dialog);
        auto *catalog = dialog->property("catalog").value<SettingsCatalog *>();
        QVERIFY(catalog);
        for (const auto &key : {QString("network"), QString("player"), QString("network")}) {
            catalog->select(key);
            for (int turn = 0; turn < 3000 && partial.incubatingObjectCount(); ++turn) {
                interrupt.proceed.store(true);
                partial.incubateWhile(&interrupt.proceed, 1);
                session.engine.collectGarbage();
                QCoreApplication::processEvents();
                if (key != "network" && turn >= cycle % 12)
                    break;
            }
            if (key == "network")
                QCOMPARE(partial.incubatingObjectCount(), 0);
        }
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(dialog.isNull());
        session.engine.collectGarbage();
    }
    QVERIFY(interrupt.interruptions > 100);
    QCOMPARE(warnings.size(), 0);
}

void DesktopPagesTest::lyricSkinGlobalSettingsHandoff_data()
{
    QTest::addColumn<QString>("skin");
    QTest::addColumn<bool>("motion");
    for (const auto &skin : {QString("cover"), QString("lyric"), QString("portrait")})
        for (bool motion : {false, true})
            QTest::newRow(qPrintable(skin + (motion ? "-animated" : "-reduced"))) << skin << motion;
}

void DesktopPagesTest::lyricSkinGlobalSettingsHandoff()
{
    QFETCH(QString, skin);
    QFETCH(bool, motion);
    Session session;
    session.preferences.setValue("playback/volume", 0);
    PlaybackService playback(session.api, session.preferences);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&session] {
        session.root.reset();
        session.window = nullptr;
    });
    session.theme.setMotionEnabled(motion);
    session.shell.setMotionEnabled(motion);
    QVERIFY(session.theme.extensions()->chooseSkin(skin));
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *settingsOwner = session.root->findChild<QObject *>("settingsDialogOwner");
    QVERIFY(settingsOwner);
    SectionSources observed(settingsOwner);

    for (int cycle = 0; cycle < 4; ++cycle) {
        observed.sources.clear();
        auto page = session.openOwnedPopup("playbackPageOwner");
        QVERIFY(page);
        QVERIFY(QMetaObject::invokeMethod(page, "openSkins"));
        auto *drawerOwner = page->findChild<QObject *>("lyricSkinDrawerOwner");
        QVERIFY(drawerOwner);
        QPointer<QObject> drawer;
        QTRY_VERIFY((drawer = drawerOwner->property("item").value<QObject *>()) &&
                    drawer->property("opened").toBool());
        drawer->setProperty("showSettings", cycle >= 2);
        QSignalSpy drawerClosed(drawer, SIGNAL(closed()));
        QSignalSpy requested(drawer, SIGNAL(globalSettingsRequested()));
        const auto findAction = [&](auto &&self, QQuickItem *item) -> QQuickItem * {
            if (!item)
                return nullptr;
            if (item->property("text").toString() == QStringLiteral("全局设置") &&
                item->property("glyph").toString() == "settings")
                return item;
            for (auto *child : item->childItems())
                if (auto *found = self(self, child))
                    return found;
            return nullptr;
        };
        auto *action =
            findAction(findAction, drawer->property("contentItem").value<QQuickItem *>());
        QVERIFY(action);

        // Keep Qt's asynchronous Loader, but control its budget at this boundary.
        // A user can dismiss the new popup before its section finishes binding.
        QQmlIncubationController held;
        auto *normalController = session.engine.incubationController();
        const bool controlled = cycle == 1 || cycle == 2;
        if (controlled)
            session.engine.setIncubationController(&held);
        const auto restoreController =
            qScopeGuard([&] { session.engine.setIncubationController(normalController); });
        QVERIFY(session.clickItem(action));
        QCOMPARE(requested.size(), 1);
        QPointer<QObject> settings = settingsOwner->property("item").value<QObject *>();
        QVERIFY(settings && settings->property("visible").toBool());
        auto *catalog = settings->property("catalog").value<SettingsCatalog *>();
        QVERIFY(catalog);
        QCOMPARE(catalog->current(), QString("lyric"));
        auto *loader = visual(session.window->contentItem(), "settingsSectionLoader");
        QVERIFY(loader);
        qInfo() << "Skin/settings handoff" << skin << "motion" << motion << "cycle" << cycle
                << "drawerClosed" << drawerClosed.size() << "loaderStatus"
                << loader->property("status") << "source" << loader->property("source");
        QVERIFY(loader->property("asynchronous").toBool());
        QVERIFY(loader->property("source").toUrl().path().endsWith("/LyricSettingsSection.qml"));
        QCOMPARE(observed.sources, QStringList{loader->property("source").toUrl().toString()});
        if (controlled) {
            QCOMPARE(loader->property("status").toInt(), 2); // Loader.Loading
            QVERIFY(held.incubatingObjectCount() > 0);
            if (cycle == 2) {
                held.incubateFor(1);
                qInfo() << "After partial incubation" << skin << "motion" << motion << "remaining"
                        << held.incubatingObjectCount() << "status" << loader->property("status");
            }
            QVERIFY(QMetaObject::invokeMethod(settings, "close"));
            QTRY_VERIFY(settings.isNull());
            QCOMPARE(held.incubatingObjectCount(), 0);
        } else {
            QTRY_VERIFY(settings->property("opened").toBool());
            QVERIFY(session.settingsSectionReady());
            QPointer<QObject> section = loader->property("item").value<QObject *>();
            QVERIFY(section);
            QCOMPARE(section->property("playback").value<PlaybackService *>(), &playback);
            QVERIFY(QMetaObject::invokeMethod(settings, "close"));
            QTRY_VERIFY(settings.isNull() && section.isNull());
        }
        QTRY_VERIFY(drawer.isNull());
        QVERIFY(!settingsOwner->property("item").value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(page, "close"));
        QTRY_VERIFY(page.isNull());
        session.engine.collectGarbage();
    }
    QStringList errors;
    for (const auto &entry : warnings)
        for (const auto &error : entry.value(0).value<QList<QQmlError>>())
            errors << error.toString();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
}

void DesktopPagesTest::lyricSkinSwitchKeepsCatalogAndPageStable_data()
{
    QTest::addColumn<bool>("motion");
    QTest::addColumn<bool>("dark");
    for (bool motion : {false, true})
        for (bool dark : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(dark ? "dark-125" : "light-100",
                                                          motion ? "animated" : "reduced")))
                << motion << dark;
}

void DesktopPagesTest::lyricSkinSwitchKeepsCatalogAndPageStable()
{
    QFETCH(bool, motion);
    QFETCH(bool, dark);
    Session session;
    session.shell.acceptUserAgreement();
    session.shell.setMotionEnabled(motion);
    session.theme.setMotionEnabled(motion);
    session.theme.setMode(dark ? ThemeStore::Dark : ThemeStore::Light);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    if (dark)
        for (int step = 0; step < 5; ++step)
            session.shell.zoomIn();
    PlaybackService playback(session.api, session.preferences);
    QVERIFY(session.create(&playback));
    const auto cleanup = qScopeGuard([&] {
        session.root.reset();
        session.auxiliary.reset();
        session.window = nullptr;
    });
    QVERIFY(Test::prepareNativeInput(session.window));
    auto page = session.openOwnedPopup("playbackPageOwner");
    QVERIFY(page);
    QVERIFY(QMetaObject::invokeMethod(page, "openSkins"));
    auto *owner = page->findChild<QObject *>("lyricSkinDrawerOwner");
    QVERIFY(owner);
    QPointer<QObject> drawer;
    QTRY_VERIFY((drawer = owner->property("item").value<QObject *>()) &&
                drawer->property("opened").toBool());
    auto *catalog = visual(session.window->contentItem(), "lyricSkinCatalog");
    QVERIFY(catalog);
    QVERIFY(session.settleScene());
    QList<QPointer<QQuickItem>> cards;
    QList<QRectF> rectangles;
    for (const auto *name :
         {"coverLyricSkinCard", "portraitLyricSkinCard", "simpleLyricSkinCard"}) {
        auto *card = visual(catalog, name);
        QVERIFY(card);
        cards.append(card);
        rectangles.append(card->mapRectToScene(card->boundingRect()));
    }
    const auto windowGeometry = session.window->geometry();
    const auto catalogRect = catalog->mapRectToScene(catalog->boundingRect());
    QSignalSpy resets(session.theme.extensions()->skins(), &QAbstractItemModel::modelReset);
    QSignalSpy themeResets(session.theme.extensions()->themes(), &QAbstractItemModel::modelReset);
    QSignalSpy pageClosed(page, SIGNAL(closed()));
    QSignalSpy drawerClosed(drawer, SIGNAL(closed()));
    const auto capture = qEnvironmentVariable("ECHO_SKIN_SWITCH_CAPTURES");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (int index : {2, 1, 0}) {
            auto *card = visual(catalog, index == 0   ? "coverLyricSkinCard"
                                         : index == 1 ? "portraitLyricSkinCard"
                                                      : "simpleLyricSkinCard");
            QVERIFY(card);
            QVERIFY(session.clickItem(card));
            for (int frame = 0; frame < 4; ++frame) {
                session.window->update();
                QTest::qWait(16);
                if (!capture.isEmpty() && cycle == 0) {
                    QVERIFY(session.window->grabWindow().save(capture +
                                                              QString("/%1-%2-%3.png")
                                                                  .arg(QTest::currentDataTag())
                                                                  .arg(index)
                                                                  .arg(frame)));
                }
            }
            qInfo() << "Skin switch scope" << index << "catalog resets" << resets.size()
                    << "page closed" << pageClosed.size() << "drawer closed" << drawerClosed.size();
            QCOMPARE(session.window->geometry(), windowGeometry);
            QVERIFY(page && drawer);
            QCOMPARE(pageClosed.size(), 0);
            QCOMPARE(drawerClosed.size(), 0);
            QCOMPARE(catalog->mapRectToScene(catalog->boundingRect()), catalogRect);
            for (int row = 0; row < cards.size(); ++row) {
                QVERIFY2(cards[row], "Selecting a skin destroyed an unchanged thumbnail card");
                QCOMPARE(cards[row]->mapRectToScene(cards[row]->boundingRect()), rectangles[row]);
                QCOMPARE(cards[row]->property("selected").toBool(), row == index);
            }
            QCOMPARE(resets.size(), 0);
            QCOMPARE(themeResets.size(), 0);
        }
    }
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QTRY_VERIFY(page.isNull() && drawer.isNull());
    for (const auto &card : cards)
        QVERIFY(card.isNull());
    QCOMPARE(warnings.size(), 0);
}

#include "DesktopPagesSettingsHandoff.moc"
