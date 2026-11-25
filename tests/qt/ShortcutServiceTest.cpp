#include "ShortcutService.h"
#include "ShortcutActions.h"
#include "ShellController.h"
#include "ShellSettings.h"
#include "WindowController.h"
#include "ThemeStore.h"
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QGuiApplication>
#include <QMetaEnum>
#include <QQmlComponent>
#include <QQmlEngine>
using namespace EchoMusic;
class ShortcutServiceTest : public QObject
{
    Q_OBJECT
private slots:
    void spaceUsesOnlyActiveAttachedWindowsAndOnePress()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("space.ini"));
        ShortcutService service(settings, false);
        QQuickWindow main, mini, other;
        service.attach(&main);
        service.attachMini(&mini);
        QSignalSpy actions(&service, &ShortcutService::triggered);
        const auto activate = [](QQuickWindow &window) {
            window.show();
            window.raise();
            window.requestActivate();
        };
        activate(main);
        QTRY_VERIFY(main.isActive());
        // Space is an additional gesture, even after rebinding/clearing playback.
        service.clearBinding(ShortcutService::TogglePlayback, false);
        service.setSuppressDefault(false);
        QTest::keyPress(&main, Qt::Key_Space);
        QCOMPARE(actions.size(), 1);
        QCOMPARE(qvariant_cast<ShortcutService::Command>(actions.first().first()),
                 ShortcutService::TogglePlayback);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true);
        QCoreApplication::sendEvent(&main, &repeat);
        QCOMPARE(actions.size(), 1);
        QTest::keyRelease(&main, Qt::Key_Space);
        QTest::keyClick(&main, Qt::Key_Space);
        QCOMPARE(actions.size(), 2);
        for (const auto modifier :
             {Qt::ShiftModifier, Qt::AltModifier, Qt::ControlModifier, Qt::MetaModifier})
            QTest::keyClick(&main, Qt::Key_Space, modifier);
        QCOMPARE(actions.size(), 2);
        activate(mini);
        QTRY_VERIFY(mini.isActive());
        QTest::keyClick(&main, Qt::Key_Space);
        QCOMPARE(actions.size(), 2);
        QTest::keyClick(&mini, Qt::Key_Space);
        QCOMPARE(actions.size(), 3);
        service.beginRecording(ShortcutService::NextTrack, false);
        QTest::keyClick(&mini, Qt::Key_Space);
        QCOMPARE(actions.size(), 3);
        service.stopRecording();
        activate(other);
        QTRY_VERIFY(other.isActive());
        QTest::keyClick(&other, Qt::Key_Space);
        QTest::keyClick(&mini, Qt::Key_Space);
        QCOMPARE(actions.size(), 3);
        activate(mini);
        QTRY_VERIFY(mini.isActive());
        service.attachMini(nullptr);
        QTest::keyClick(&mini, Qt::Key_Space);
        QCOMPARE(actions.size(), 3);
        activate(main);
        QTRY_VERIFY(main.isActive());
        QTest::keyPress(&main, Qt::Key_Space);
        QCOMPARE(actions.size(), 4);
        // Focus can leave while the key is held, so its release is lost.
        activate(other);
        QTRY_VERIFY(other.isActive());
        activate(main);
        QTRY_VERIFY(main.isActive());
        QTest::keyClick(&main, Qt::Key_Space);
        QCOMPARE(actions.size(), 5);
        service.shutdown();
        QTest::keyClick(&main, Qt::Key_Space);
        QCOMPARE(actions.size(), 5);
    }
    void spaceOverridesFocusedControlsAndPreservesEditors()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("space-input.ini"));
        ShortcutService service(settings, false);
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import EchoMusic.Controls
            Item {
                width: 640; height: 480
                property int clicks: 0
                EchoTextField { objectName: "single"; y: 10; width: 160; height: 30 }
                EchoTextArea { objectName: "multi"; y: 50; width: 160; height: 50 }
                ComboBox { objectName: "combo"; y: 110; editable: true; model: ["alpha"] }
                EchoSpinBox { objectName: "number"; y: 160 }
                EchoButton { objectName: "button"; y: 210; text: "Action"; onClicked: parent.clicks++ }
                EchoCheckBox { objectName: "check"; y: 260; text: "Option" }
                EchoSelect { objectName: "select"; y: 310; filterable: true; options: ["alpha", "beta"] }
                Popup { objectName: "page"; width: 400; height: 300; focus: true; modal: true }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickWindow window;
        qobject_cast<QQuickItem *>(root.data())->setParentItem(window.contentItem());
        service.attach(&window);
        window.show();
        window.raise();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QSignalSpy actions(&service, &ShortcutService::triggered);
        for (const auto *name : {"single", "multi", "combo", "number"}) {
            auto *item = root->findChild<QQuickItem *>(QString::fromLatin1(name));
            QVERIFY(item);
            if (QString::fromLatin1(name) == "combo" || QString::fromLatin1(name) == "number")
                item = qvariant_cast<QQuickItem *>(item->property("contentItem"));
            QVERIFY(item);
            item->forceActiveFocus();
            QTRY_VERIFY(item->hasActiveFocus());
            auto *editor = window.activeFocusItem();
            QVERIFY(editor);
            QTest::keyClick(&window, Qt::Key_Space);
            QVERIFY(actions.isEmpty());
            if (QString::fromLatin1(name) != "number")
                QVERIFY2(editor->property("text").toString().contains(' '), name);
        }
        auto *single = root->findChild<QQuickItem *>("single");
        auto *selectInput = root->findChild<QQuickItem *>("selectSearchInput");
        QVERIFY(selectInput);
        selectInput->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(selectInput->property("text").toString(), QString(" "));
        QVERIFY(actions.isEmpty());
        // Typing may open the select's own popup; leave that editing context.
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>("select"), "closePopup"));
        single->forceActiveFocus();
        QInputMethodEvent composing("candidate", {});
        QCoreApplication::sendEvent(single, &composing);
        QTest::keyClick(&window, Qt::Key_Space);
        QVERIFY(actions.isEmpty());
        auto *button = root->findChild<QQuickItem *>("button");
        button->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(root->property("clicks").toInt(), 0);
        QCOMPARE(actions.size(), 1);
        actions.clear();
        auto *check = root->findChild<QQuickItem *>("check");
        service.setSuppressDefault(false);
        check->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Space);
        QVERIFY(!check->property("checked").toBool());
        QCOMPARE(actions.size(), 1);
        actions.clear();
        service.setSuppressDefault(true);
        auto *page = root->findChild<QObject *>("page");
        QVERIFY(QMetaObject::invokeMethod(page, "open"));
        QTRY_VERIFY(page->property("opened").toBool());
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(actions.size(), 1);
        QVERIFY(QMetaObject::invokeMethod(page, "close"));
        QTRY_VERIFY(!page->property("visible").toBool());
        qobject_cast<QQuickItem *>(root.data())->forceActiveFocus();
        QCOMPARE(window.activeFocusItem(), qobject_cast<QQuickItem *>(root.data()));
        service.beginRecording(ShortcutService::TogglePlayback, false);
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(actions.size(), 1);
        QCOMPARE(service.recordingRow(), int(ShortcutService::TogglePlayback));
        service.stopRecording();
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(actions.size(), 2);
        QTest::keyPress(&window, Qt::Key_Space);
        QCOMPARE(actions.size(), 3);
        button->forceActiveFocus();
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true);
        QCoreApplication::sendEvent(&window, &repeat);
        QTest::keyRelease(&window, Qt::Key_Space);
        QCOMPARE(actions.size(), 3);
        QCOMPARE(root->property("clicks").toInt(), 0);
        QVERIFY(!button->property("down").toBool());
    }
    void defaultGuardBlocksNavigationButPreservesEditorsAndExplicitShortcuts()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("default-guard.ini"));
        ShortcutService service(settings, false);
        QVERIFY(service.suppressDefault());
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import EchoMusic.Controls
            Item {
                width: 640; height: 480
                Item {
                    objectName: "sink"
                    property int presses: 0
                    property int releases: 0
                    // QtTest sends separate modifier events for Shift chords.
                    // Count delivered action keys, while leaving modifiers alone.
                    function actionKey(key: int): bool {
                        return ![Qt.Key_Shift, Qt.Key_Control, Qt.Key_Alt, Qt.Key_Meta].includes(key);
                    }
                    Keys.onPressed: event => { if (actionKey(event.key)) presses++; event.accepted = true; }
                    Keys.onReleased: event => { if (actionKey(event.key)) releases++; event.accepted = true; }
                }
                EchoSlider { objectName: "slider"; y: 50; from: 0; to: 100; value: 50; stepSize: 10 }
                EchoTextField {
                    objectName: "editor"; y: 100; text: "alpha beta"
                    property int lastKey: 0
                    Keys.onPressed: event => { lastKey = event.key; event.accepted = false; }
                }
            }
        )",
                          QUrl());
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickWindow window, mini;
        qobject_cast<QQuickItem *>(root.data())->setParentItem(window.contentItem());
        service.attach(&window);
        window.show();
        window.raise();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        auto *sink = root->findChild<QQuickItem *>("sink");
        auto *slider = root->findChild<QQuickItem *>("slider");
        auto *editor = root->findChild<QQuickItem *>("editor");
        QVERIFY(sink && slider && editor);
        QSignalSpy actions(&service, &ShortcutService::triggered);
        sink->forceActiveFocus();
        const QList<Qt::Key> guarded{Qt::Key_Space, Qt::Key_Left,   Qt::Key_Right,
                                     Qt::Key_Up,    Qt::Key_Down,   Qt::Key_Home,
                                     Qt::Key_End,   Qt::Key_PageUp, Qt::Key_PageDown};
        for (const auto key : guarded) {
            QTest::keyClick(&window, key);
            QTest::keyClick(&window, key, Qt::ShiftModifier);
        }
        QCOMPARE(sink->property("presses").toInt(), 0);
        QCOMPARE(sink->property("releases").toInt(), 0);
        QCOMPARE(actions.size(), 1); // Only bare Space controls playback.
        QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(actions.size(), 2);
        QCOMPARE(qvariant_cast<ShortcutService::Command>(actions.last().first()),
                 ShortcutService::NextTrack);
        int pluginCalls = 0;
        QObject pluginOwner;
        QString pluginError;
        ShortcutService::PluginBinding plugin;
        plugin.key = "default-guard-probe";
        plugin.sequence = QKeySequence("Ctrl+Alt+F12");
        plugin.owner = &pluginOwner;
        plugin.invoke = [&pluginCalls] { ++pluginCalls; };
        QVERIFY2(service.registerPlugin(std::move(plugin), pluginError), qPrintable(pluginError));
        const int beforePlugin = sink->property("presses").toInt();
        QTest::keyClick(&window, Qt::Key_F12, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(pluginCalls, 1);
        QCOMPARE(sink->property("presses").toInt(), beforePlugin);
        const int beforeOtherKeys = sink->property("presses").toInt();
        for (const auto key : {Qt::Key_Tab, Qt::Key_Return, Qt::Key_Escape, Qt::Key_A})
            QTest::keyClick(&window, key);
        QCOMPARE(sink->property("presses").toInt(), beforeOtherKeys + 4);
        const int beforeModifierKeys = sink->property("presses").toInt();
        QTest::keyClick(&window, Qt::Key_Home, Qt::AltModifier);
        QVERIFY(sink->property("presses").toInt() > beforeModifierKeys);
        slider->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Right);
        QTest::keyClick(&window, Qt::Key_Left);
        QCOMPARE(slider->property("value").toDouble(), 50.);
        editor->forceActiveFocus();
        editor->setProperty("cursorPosition", 5);
        QTest::keyClick(&window, Qt::Key_Left);
        QCOMPARE(editor->property("cursorPosition").toInt(), 4);
        QTest::keyClick(&window, Qt::Key_Home);
        // Cocoa Home scrolls rather than moving the caret. Check delivery,
        // leaving the platform's native editing behavior intact.
        QCOMPARE(editor->property("lastKey").toInt(), int(Qt::Key_Home));
        editor->setProperty("cursorPosition", 0);
        QTest::keyClick(&window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(editor->property("selectedText").toString(), QString("a"));
        QCOMPARE(actions.size(), 2);
        // A held, suppressed key cannot begin editing after a focus change.
        sink->forceActiveFocus();
        QTest::keyPress(&window, Qt::Key_Right);
        editor->forceActiveFocus();
        editor->setProperty("cursorPosition", 3);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, {}, true);
        QCoreApplication::sendEvent(&window, &repeat);
        QTest::keyRelease(&window, Qt::Key_Right);
        QCOMPARE(editor->property("cursorPosition").toInt(), 3);
        service.setSuppressDefault(false);
        slider->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Right);
        QCOMPARE(slider->property("value").toDouble(), 60.);
        QTest::keyClick(&window, Qt::Key_Left);
        QCOMPARE(slider->property("value").toDouble(), 50.);
        sink->forceActiveFocus();
        const int beforeDisabled = sink->property("presses").toInt();
        for (const auto key : guarded)
            if (key != Qt::Key_Space)
                QTest::keyClick(&window, key);
        QCOMPARE(sink->property("presses").toInt(), beforeDisabled + 8);
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(actions.size(), 3);
        QCOMPARE(sink->property("presses").toInt(), beforeDisabled + 8);
        service.setSuppressDefault(true);
        service.attachMini(&mini);
        sink->setParentItem(mini.contentItem());
        mini.show();
        mini.raise();
        mini.requestActivate();
        QTRY_VERIFY(mini.isActive());
        sink->forceActiveFocus();
        const int beforeMini = sink->property("presses").toInt();
        QTest::keyClick(&mini, Qt::Key_Down);
        QCOMPARE(sink->property("presses").toInt(), beforeMini);
        service.setSuppressDefault(false);
        QTest::keyClick(&mini, Qt::Key_Down);
        QCOMPARE(sink->property("presses").toInt(), beforeMini + 1);
        sink->setParentItem(qobject_cast<QQuickItem *>(root.data()));
    }
    void miniUsesSharedBindingsAndItsOwnEditorGate()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("mini.ini"));
        ShortcutService service(settings, false);
        QQuickWindow main, mini, other;
        service.attach(&main);
        service.attachMini(&mini);
        main.show();
        mini.show();
        mini.requestActivate();
        QTRY_VERIFY(mini.isActive());
        QSignalSpy actions(&service, &ShortcutService::triggered);
        for (const auto key : {Qt::Key_Up, Qt::Key_Right, Qt::Key_I, Qt::Key_M}) {
            QTest::keyClick(&mini, key, Qt::ControlModifier);
            QCOMPARE(actions.size(), 1);
            actions.clear();
        }
        QQuickItem editor(mini.contentItem());
        editor.setFlag(QQuickItem::ItemAcceptsInputMethod);
        editor.forceActiveFocus();
        QTest::keyClick(&mini, Qt::Key_Right, Qt::ControlModifier);
        QVERIFY(actions.isEmpty());
        mini.contentItem()->forceActiveFocus();
        service.beginRecording(ShortcutService::NextTrack, false);
        QTest::keyClick(&mini, Qt::Key_Right, Qt::ControlModifier);
        QVERIFY(actions.isEmpty());
        QCOMPARE(service.recordingRow(), int(ShortcutService::NextTrack));
        service.stopRecording();
        other.show();
        other.requestActivate();
        QTRY_VERIFY(other.isActive());
        QTest::keyClick(&other, Qt::Key_Right, Qt::ControlModifier);
        QVERIFY(actions.isEmpty());
        service.attachMini(nullptr);
        mini.requestActivate();
        QTRY_VERIFY(mini.isActive());
        QTest::keyClick(&mini, Qt::Key_Right, Qt::ControlModifier);
        QVERIFY(actions.isEmpty());
    }
    void macArrowEventsMatchBindings()
    {
#if defined(Q_OS_MACOS)
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("arrows.ini"));
        ShortcutService service(settings, false);
        QQuickWindow window;
        service.attach(&window);
        window.show();
        // A preceding tool-window test can leave the process in the background.
        // Cocoa's requestActivate alone does not foreground the application.
        if (QGuiApplication::platformName() == "cocoa")
            window.raise();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QSignalSpy actions(&service, &ShortcutService::triggered);
        struct Binding
        {
            Qt::Key key;
            Qt::KeyboardModifiers modifiers;
            ShortcutService::Command command;
        };
        const QList<Binding> bindings{
            {Qt::Key_Left, Qt::ControlModifier, ShortcutService::PreviousTrack},
            {Qt::Key_Right, Qt::ControlModifier, ShortcutService::NextTrack},
            {Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier, ShortcutService::SeekBackward},
            {Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier, ShortcutService::SeekForward},
            {Qt::Key_Up, Qt::ControlModifier, ShortcutService::VolumeUp},
            { Qt::Key_Down,
              Qt::ControlModifier,
              ShortcutService::VolumeDown }};
        for (const auto &binding : bindings) {
            actions.clear();
            QTest::keyClick(&window, binding.key, binding.modifiers | Qt::KeypadModifier);
            QCOMPARE(actions.size(), 1);
            QCOMPARE(qvariant_cast<ShortcutService::Command>(actions.first().first()),
                     binding.command);
            actions.clear();
            QTest::keyClick(&window, binding.key, binding.modifiers);
            QCOMPARE(actions.size(), 1);
            QTest::keyClick(&window, binding.key,
                            binding.modifiers | Qt::ShiftModifier | Qt::KeypadModifier);
            QCOMPARE(actions.size(), 1);
        }
        auto editor = new QQuickItem(window.contentItem());
        editor->setFlag(QQuickItem::ItemAcceptsInputMethod);
        editor->forceActiveFocus();
        actions.clear();
        QTest::keyClick(&window, Qt::Key_Right, Qt::ControlModifier | Qt::KeypadModifier);
        QVERIFY(actions.isEmpty());
        service.shutdown();
#else
        QSKIP("macOS arrow-key modifier semantics");
#endif
    }
    void macArrowRecordingAndStoredBindingsUseSameIdentity()
    {
#if defined(Q_OS_MACOS)
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("recording.ini"));
        // A binding previously recorded from a native arrow event can carry Num.
        settings.setValue("shortcuts/local/nextTrack", "Ctrl+Num+Right");
        ShortcutService service(settings, false);
        service.beginRecording(ShortcutService::PreviousTrack, false);
        QSignalSpy notices(&service, &ShortcutService::notice);
        QVERIFY(!service.recordKey(Qt::Key_Right, Qt::ControlModifier));
        QCOMPARE(notices.size(), 1);
        QVERIFY(!service.recordKey(Qt::Key_Right, Qt::ControlModifier | Qt::KeypadModifier));
        QCOMPARE(notices.size(), 2);
        service.stopRecording();
        service.beginRecording(ShortcutService::PreviousTrack, false);
        QVERIFY(service.recordKey(Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier |
                                                    Qt::ShiftModifier | Qt::KeypadModifier));
        QCOMPARE(settings.value("shortcuts/local/previousTrack").toString(),
                 QString("Ctrl+Alt+Shift+Left"));
        service.setGlobalEnabled(true);
        service.beginRecording(ShortcutService::NextTrack, true);
        QVERIFY(service.recordKey(Qt::Key_Right,
                                  Qt::ControlModifier | Qt::ShiftModifier | Qt::KeypadModifier));
        QCOMPARE(settings.value("shortcuts/global/nextTrack").toString(),
                 QString("Ctrl+Shift+Right"));
        // Numeric keypad keys retain their distinct identity.
        service.beginRecording(ShortcutService::PreviousTrack, false);
        QVERIFY(service.recordKey(Qt::Key_1, Qt::ControlModifier | Qt::KeypadModifier));
        QCOMPARE(
            QKeySequence(settings.value("shortcuts/local/previousTrack").toString()),
            QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::KeypadModifier, Qt::Key_1)));
#else
        QSKIP("macOS arrow-key modifier semantics");
#endif
    }
    void presentationUsesSourceModifierOrderWithoutChangingBindings()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("shortcuts.ini"));
        ShortcutService service(settings, false);
        const auto row = service.index(ShortcutService::SeekBackward);
#if defined(Q_OS_MACOS)
        QCOMPARE(row.data(ShortcutService::LocalLabel).toString(), QString("⌘⌥←"));
        QCOMPARE(row.data(ShortcutService::GlobalLabel).toString(), QString("⌘⇧⌥←"));
#else
        QCOMPARE(row.data(ShortcutService::LocalLabel).toString(), QString("Ctrl+Alt+Left"));
        QCOMPARE(row.data(ShortcutService::GlobalLabel).toString(), QString("Ctrl+Shift+Alt+Left"));
#endif
        service.beginRecording(ShortcutService::SeekBackward, false);
        QVERIFY(service.recordKey(Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier));
        QCOMPARE(QKeySequence(settings.value("shortcuts/local/seekBackward").toString(),
                              QKeySequence::PortableText),
                 QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return));
    }
    void typedActionsUsePlaybackPercentAndExistingMuteState()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ApiClient api(DeviceIdentity::temporary());
        PlaybackService playback(api, settings, nullptr, directory.filePath("playback.sqlite"));
        auto *firstView = new QObject;
        QObject secondView;
        playback.cast()->setBrowsingFor(firstView, true);
        playback.cast()->setBrowsingFor(&secondView, true);
        QVERIFY(playback.cast()->browsing());
        delete firstView;
        QVERIFY(playback.cast()->browsing());
        playback.cast()->setBrowsingFor(&secondView, false);
        QVERIFY(!playback.cast()->browsing());
        RouteController router;
        ShellController shell(settings, router);
        ShellSettings preferences(settings);
        WindowController window(settings, false);
        ShortcutService shortcuts(settings, false);
        ShortcutActions actions(shortcuts, playback, preferences, window, shell);
        playback.setVolume(50);
        shortcuts.triggered(ShortcutService::VolumeUp);
        QCOMPARE(playback.volume(), 55.);
        shortcuts.triggered(ShortcutService::VolumeDown);
        QCOMPARE(playback.volume(), 50.);
        shortcuts.triggered(ShortcutService::ToggleMute);
        QCOMPARE(playback.volume(), 0.);
        shortcuts.triggered(ShortcutService::ToggleMute);
        QCOMPARE(playback.volume(), 50.);
        const auto collapsed = preferences.sidebarCollapsed();
        shortcuts.triggered(ShortcutService::ToggleSidebar);
        QCOMPARE(preferences.sidebarCollapsed(), !collapsed);
        QSignalSpy closed(&playback, &PlaybackService::closed);
        playback.shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 15000);
        api.shutdown();
    }
    void nativeRegistrationConflictAndRelease()
    {
#if !defined(Q_OS_MACOS)
        QSKIP("Native macOS registration check");
#else
        if (!qEnvironmentVariableIsSet("ECHO_NATIVE_SHORTCUT_TEST"))
            QSKIP("Run explicitly with the Cocoa platform");
        GlobalShortcuts first, second;
        const QList<GlobalShortcuts::Binding> bindings{
            {77, QKeySequence("Ctrl+Alt+Shift+F19"), "registration lifecycle test"}};
        QSignalSpy failedFirst(&first, &GlobalShortcuts::registrationFailed);
        QSignalSpy failedSecond(&second, &GlobalShortcuts::registrationFailed);
        first.replace(bindings);
        if (!failedFirst.isEmpty())
            QSKIP("The test key is already reserved by this desktop");
        second.replace(bindings);
        QCOMPARE(failedSecond.size(), 1);
        first.replace({});
        failedSecond.clear();
        second.replace(bindings);
        QVERIFY(failedSecond.isEmpty());
        second.replace({});
#endif
    }
    void recordingConflictClearResetAndPersistence()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        {
            ShortcutService service(settings, false);
            QCOMPARE(service.rowCount(), 15);
            QVERIFY(!service.globalEnabled());
            service.beginRecording(1, true);
            QCOMPARE(service.recordingRow(), -1);
            service.beginRecording(1, false);
            QVERIFY(!service.recordKey(Qt::Key_Control, Qt::ControlModifier));
            QVERIFY(!service.recordKey(Qt::Key_A, Qt::NoModifier));
            // Default playback binding is already owned by the first command.
            QSignalSpy notice(&service, &ShortcutService::notice);
            QVERIFY(!service.recordKey(Qt::Key_Space, Qt::ControlModifier));
            QCOMPARE(service.recordingRow(), 1);
            QCOMPARE(notice.size(), 1);
            QVERIFY(service.recordKey(Qt::Key_F6, Qt::NoModifier));
            QCOMPARE(service.recordingRow(), -1);
            QCOMPARE(service.data(service.index(1), ShortcutService::LocalLabel).toString(),
                     QString("F6"));
            service.setGlobalEnabled(true);
            service.beginRecording(2, true);
            // The same sequence may intentionally be used in another scope.
            QVERIFY(service.recordKey(Qt::Key_F6, Qt::NoModifier));
            service.beginRecording(3, false);
            QVERIFY(service.recordKey(Qt::Key_Escape, Qt::NoModifier));
            QVERIFY(
                service.data(service.index(3), ShortcutService::LocalLabel).toString().isEmpty());
        }
        ShortcutService restored(settings, false);
        QCOMPARE(restored.data(restored.index(1), ShortcutService::LocalLabel).toString(),
                 QString("F6"));
        QCOMPARE(restored.data(restored.index(2), ShortcutService::GlobalLabel).toString(),
                 QString("F6"));
        QVERIFY(restored.data(restored.index(1), ShortcutService::Modified).toBool());
        restored.resetBinding(1);
        QVERIFY(!restored.data(restored.index(1), ShortcutService::Modified).toBool());
        restored.resetAll();
        for (int row = 0; row < restored.rowCount(); ++row)
            QVERIFY(!restored.data(restored.index(row), ShortcutService::Modified).toBool());
    }
    void windowFocusEditingAndShutdown()
    {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("settings.ini"));
        ShortcutService service(settings, false);
        QQuickWindow window;
        service.attach(&window);
        window.resize(320, 240);
        window.show();
        window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QSignalSpy actions(&service, &ShortcutService::triggered);
        QTest::keyClick(&window, Qt::Key_Space, Qt::ControlModifier);
        QCOMPARE(actions.size(), 1);
        QCOMPARE(qvariant_cast<ShortcutService::Command>(actions.first().first()),
                 ShortcutService::TogglePlayback);
        auto editor = new QQuickItem(window.contentItem());
        editor->setFlag(QQuickItem::ItemAcceptsInputMethod);
        editor->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Space, Qt::ControlModifier);
        QCOMPARE(actions.size(), 1);
        service.beginRecording(0, false);
        QTest::keyClick(&window, Qt::Key_F9);
        QCOMPARE(service.recordingRow(), -1);
        QCOMPARE(actions.size(), 1);
        editor->setFlag(QQuickItem::ItemAcceptsInputMethod, false);
        QTest::keyClick(&window, Qt::Key_F9);
        QCOMPARE(actions.size(), 2);
        service.shutdown();
        QTest::keyClick(&window, Qt::Key_F9);
        QCOMPARE(actions.size(), 2);
    }
};
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().contains("--native-probe")) {
        QTemporaryDir directory;
        EchoMusic::SettingsStore settings(directory.filePath("probe.ini"));
        ShortcutService service(settings, false);
        QQuickWindow window;
        window.setTitle("EchoMusicNext shortcut probe: ready");
        window.resize(480, 240);
        service.attach(&window);
        int count = 0;
        QObject::connect(
            &service, &ShortcutService::triggered, &window, [&](ShortcutService::Command command) {
                const auto name =
                    QMetaEnum::fromType<ShortcutService::Command>().valueToKey(command);
                window.setTitle(QString("EchoMusicNext shortcut probe: %1 #%2")
                                    .arg(QString::fromLatin1(name))
                                    .arg(++count));
            });
        window.show();
        return app.exec();
    }
    ShortcutServiceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ShortcutServiceTest.moc"
