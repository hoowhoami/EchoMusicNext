#include "TaskbarButtonController.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

// The two taskbar button preferences are the storage contract the settings page
// writes and the platform layer reads back. The keys are pinned literally: a
// rename would silently orphan what users already have on disk, and a wrong
// default would switch a taskbar feature on without the user asking.
using EchoMusic::TaskbarButtonController;

class TaskbarButtonControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAreOff();
    void togglesPersistUnderTheirWindowKeys();
    void repeatedSetDoesNotNotifyAgain();
    void storedValuesAreReadBackOnConstruction();
};

void TaskbarButtonControllerTest::defaultsAreOff()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("window.ini"), QSettings::IniFormat);
    TaskbarButtonController controller(settings);

    QVERIFY(!controller.progressEnabled());
    QVERIFY(!controller.coverPreviewEnabled());
}

void TaskbarButtonControllerTest::togglesPersistUnderTheirWindowKeys()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("window.ini"), QSettings::IniFormat);
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
    QSettings settings(directory.filePath("window.ini"), QSettings::IniFormat);
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
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("window/taskbarProgress"), true);
        settings.setValue(QStringLiteral("window/taskbarCoverPreview"), true);
        settings.sync();
    }
    QSettings settings(path, QSettings::IniFormat);
    TaskbarButtonController controller(settings);

    QVERIFY(controller.progressEnabled());
    QVERIFY(controller.coverPreviewEnabled());
}

QTEST_MAIN(TaskbarButtonControllerTest)
#include "TaskbarButtonControllerTest.moc"
