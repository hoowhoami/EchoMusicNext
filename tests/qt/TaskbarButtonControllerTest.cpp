#include "TaskbarButtonController.h"
#include "TaskbarLyricController.h"
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QQuickWindow>
#include <QtTest>

// The two taskbar button preferences are the storage contract the settings page
// writes and the platform layer reads back. The keys are pinned literally: a
// rename would silently orphan what users already have on disk, and a wrong
// default would switch a taskbar feature on without the user asking.
using EchoMusic::TaskbarButtonController;

namespace
{
class ObservedTaskbarWindow final : public QQuickWindow
{
public:
    int visibilitySubscribers() const
    {
        return receivers(SIGNAL(visibleChanged(bool)));
    }
    int screenSubscribers() const
    {
        return receivers(SIGNAL(screenChanged(QScreen *)));
    }
    int frameSubscribers() const
    {
        return receivers(SIGNAL(frameSwapped()));
    }
};
} // namespace

class TaskbarButtonControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAreOff();
    void togglesPersistUnderTheirWindowKeys();
    void repeatedSetDoesNotNotifyAgain();
    void storedValuesAreReadBackOnConstruction();
    void detachAndReplaceUnsubscribeRetiredWindows();
    void taskbarLyricsUnsubscribeRetiredWindows();
};

void TaskbarButtonControllerTest::defaultsAreOff()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("window.ini"));
    TaskbarButtonController controller(settings);

    QVERIFY(!controller.progressEnabled());
    QVERIFY(!controller.coverPreviewEnabled());
}

void TaskbarButtonControllerTest::togglesPersistUnderTheirWindowKeys()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("window.ini"));
    TaskbarButtonController controller(settings);

    controller.setProgressEnabled(true);
    controller.setCoverPreviewEnabled(true);
    QCOMPARE(settings.value(QStringLiteral("window/taskbarProgress")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("window/taskbarCoverPreview")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("window/taskbarProgress"), false).metaType(),
             QMetaType::fromType<bool>());

    controller.setProgressEnabled(false);
    QCOMPARE(settings.value(QStringLiteral("window/taskbarProgress")).toBool(), false);
}

void TaskbarButtonControllerTest::repeatedSetDoesNotNotifyAgain()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("window.ini"));
    TaskbarButtonController controller(settings);
    QSignalSpy changes(&controller, &TaskbarButtonController::changed);

    controller.setProgressEnabled(true);
    controller.setCoverPreviewEnabled(true);
    QCOMPARE(changes.count(), 2);

    // The settings page writes the same value when it re-evaluates a binding;
    // that must not look like a change.
    controller.setProgressEnabled(true);
    controller.setCoverPreviewEnabled(true);
    QCOMPARE(changes.count(), 2);
}

void TaskbarButtonControllerTest::storedValuesAreReadBackOnConstruction()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("window.ini");
    {
        EchoMusic::SettingsStore settings(path);
        settings.setValue(QStringLiteral("window/taskbarProgress"), true);
        settings.setValue(QStringLiteral("window/taskbarCoverPreview"), true);
        settings.sync();
    }
    EchoMusic::SettingsStore settings(path);
    TaskbarButtonController controller(settings);

    QVERIFY(controller.progressEnabled());
    QVERIFY(controller.coverPreviewEnabled());
}

void TaskbarButtonControllerTest::detachAndReplaceUnsubscribeRetiredWindows()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("window.ini"));
    TaskbarButtonController controller(settings);
    ObservedTaskbarWindow first;
    ObservedTaskbarWindow second;
    const auto firstVisibility = first.visibilitySubscribers();
    const auto firstScreen = first.screenSubscribers();
    const auto secondVisibility = second.visibilitySubscribers();
    const auto secondScreen = second.screenSubscribers();
    for (int iteration = 0; iteration < 10; ++iteration) {
        controller.attach(&first);
        QCOMPARE(first.visibilitySubscribers(), firstVisibility + 1);
        QCOMPARE(first.screenSubscribers(), firstScreen + 1);
        controller.attach(&first);
        QCOMPARE(first.visibilitySubscribers(), firstVisibility + 1);
        controller.attach(&second);
        QCOMPARE(first.visibilitySubscribers(), firstVisibility);
        QCOMPARE(first.screenSubscribers(), firstScreen);
        QCOMPARE(second.visibilitySubscribers(), secondVisibility + 1);
        QCOMPARE(second.screenSubscribers(), secondScreen + 1);
        controller.attach(nullptr);
        QCOMPARE(second.visibilitySubscribers(), secondVisibility);
        QCOMPARE(second.screenSubscribers(), secondScreen);
    }
}

void TaskbarButtonControllerTest::taskbarLyricsUnsubscribeRetiredWindows()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("window.ini"));
    EchoMusic::TaskbarLyricController controller(settings);
    ObservedTaskbarWindow first;
    ObservedTaskbarWindow second;
    const auto firstFrames = first.frameSubscribers();
    const auto secondFrames = second.frameSubscribers();
    for (int iteration = 0; iteration < 10; ++iteration) {
        controller.attach(&first);
        QCOMPARE(first.frameSubscribers(), firstFrames + 1);
        controller.attach(&second);
        QCOMPARE(first.frameSubscribers(), firstFrames);
        QCOMPARE(second.frameSubscribers(), secondFrames + 1);
        controller.detach();
        QCOMPARE(second.frameSubscribers(), secondFrames);
    }
}

QTEST_MAIN(TaskbarButtonControllerTest)
#include "TaskbarButtonControllerTest.moc"
