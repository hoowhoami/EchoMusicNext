#include "TaskbarLyricPreferences.h"
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QtTest>

// The taskbar lyric position is stored as a string (an older build may have
// written "auto", and a hand-edited settings file can hold anything) and the
// numeric preferences are the storage-side guard for values the picker range
// would not produce. Both are contracts the QML side and existing settings
// files rely on, so they are pinned here - including the literal storage keys,
// because renaming one silently orphans what users already have on disk.
using EchoMusic::TaskbarLyricPreferences;
using Position = EchoMusic::TaskbarLyricPreferences::Position;

namespace
{
// Literal on purpose: this is the on-disk shape, not a name to keep in sync.
QString taskbarKey(const char *field)
{
    return QStringLiteral("taskbar/") + QLatin1String(field);
}
} // namespace

class TaskbarLyricPreferencesTest : public QObject
{
    Q_OBJECT
private slots:
    void defaultsMatchTheShippedStrip();
    void positionOrderIsThePickerOrder();
    void positionRoundTripsThroughItsStorageKey();
    void unknownPositionFallsBackToAutoRight();
    void numericValuesAreClamped();
    void valuesPersistUnderTheTaskbarKeys();
};

void TaskbarLyricPreferencesTest::defaultsMatchTheShippedStrip()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("taskbar.ini"));
    TaskbarLyricPreferences preferences(settings);

    QVERIFY(preferences.position() == Position::AutoRight);
    QCOMPARE(preferences.maxWidth(), 400);
    QCOMPARE(preferences.fontSize(), 14);
    QCOMPARE(preferences.showTranslation(), true);
    QCOMPARE(preferences.wordByWord(), true);
    QCOMPARE(preferences.dragOffsetX(), 0);
}

void TaskbarLyricPreferencesTest::positionOrderIsThePickerOrder()
{
    // The settings picker binds its combo index straight to the preference, so
    // the declaration order is the displayed order: reordering the enum
    // silently relabels every mode.
    QCOMPARE(int(Position::AutoLeft), 0);
    QCOMPARE(int(Position::AutoRight), 1);
    QCOMPARE(int(Position::FixedLeft), 2);
    QCOMPARE(int(Position::FixedRight), 3);
    QCOMPARE(int(Position::Drag), 4);
}

void TaskbarLyricPreferencesTest::positionRoundTripsThroughItsStorageKey()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("taskbar.ini"));

    const QList<QPair<Position, QString>> accepted{
        {Position::AutoLeft, QStringLiteral("left")},
        {Position::AutoRight, QStringLiteral("right")},
        {Position::FixedLeft, QStringLiteral("fixedLeft")},
        {Position::FixedRight, QStringLiteral("fixedRight")},
        {Position::Drag, QStringLiteral("drag")},
    };
    for (const auto &entry : accepted) {
        TaskbarLyricPreferences preferences(settings);
        preferences.setPosition(entry.first);
        QCOMPARE(preferences.position(), entry.first);
        QCOMPARE(TaskbarLyricPreferences::positionKey(entry.first), entry.second);
        QCOMPARE(settings.value(taskbarKey("position")).toString(), entry.second);
    }
}

void TaskbarLyricPreferencesTest::unknownPositionFallsBackToAutoRight()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("taskbar.ini"));

    // "auto" is what older builds wrote; anything unparseable lands on the
    // shipped default as well.
    for (const auto &legacy : {QStringLiteral("auto"), QStringLiteral("nonsense"), QString()}) {
        settings.setValue(taskbarKey("position"), legacy);
        TaskbarLyricPreferences preferences(settings);
        QVERIFY(preferences.position() == Position::AutoRight);
    }
}

void TaskbarLyricPreferencesTest::numericValuesAreClamped()
{
    QTemporaryDir directory;
    EchoMusic::SettingsStore settings(directory.filePath("taskbar.ini"));
    TaskbarLyricPreferences preferences(settings);

    preferences.setMaxWidth(159);
    QCOMPARE(preferences.maxWidth(), 160);
    preferences.setMaxWidth(801);
    QCOMPARE(preferences.maxWidth(), 800);
    preferences.setMaxWidth(512);
    QCOMPARE(preferences.maxWidth(), 512);

    preferences.setFontSize(10);
    QCOMPARE(preferences.fontSize(), 11);
    preferences.setFontSize(23);
    QCOMPARE(preferences.fontSize(), 22);
    preferences.setFontSize(18);
    QCOMPARE(preferences.fontSize(), 18);

    // A negative drop point would place the strip outside the taskbar; the
    // geometry clamps it again, but the stored value must not stay negative.
    preferences.setDragOffsetX(-40);
    QCOMPARE(preferences.dragOffsetX(), 0);
    preferences.setDragOffsetX(300);
    QCOMPARE(preferences.dragOffsetX(), 300);
}

void TaskbarLyricPreferencesTest::valuesPersistUnderTheTaskbarKeys()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("taskbar.ini");
    {
        EchoMusic::SettingsStore settings(path);
        TaskbarLyricPreferences preferences(settings);
        preferences.setPosition(Position::FixedLeft);
        preferences.setMaxWidth(300);
        preferences.setFontSize(20);
        preferences.setShowTranslation(false);
        preferences.setWordByWord(false);
        preferences.setDragOffsetX(240);
        settings.sync();
    }

    EchoMusic::SettingsStore stored(path);
    QCOMPARE(stored.value(taskbarKey("position")).toString(), QStringLiteral("fixedLeft"));
    QCOMPARE(stored.value(taskbarKey("maxWidth")).toInt(), 300);
    QCOMPARE(stored.value(taskbarKey("fontSize")).toInt(), 20);
    QCOMPARE(stored.value(taskbarKey("showTranslation")).toBool(), false);
    QCOMPARE(stored.value(taskbarKey("wordByWord")).toBool(), false);
    QCOMPARE(stored.value(taskbarKey("dragOffsetX")).toInt(), 240);

    // A fresh instance reads the same state back: that is what survives a
    // restart, including the manual drag mode's drop point.
    EchoMusic::SettingsStore reopened(path);
    TaskbarLyricPreferences preferences(reopened);
    QVERIFY(preferences.position() == Position::FixedLeft);
    QCOMPARE(preferences.maxWidth(), 300);
    QCOMPARE(preferences.fontSize(), 20);
    QCOMPARE(preferences.showTranslation(), false);
    QCOMPARE(preferences.wordByWord(), false);
    QCOMPARE(preferences.dragOffsetX(), 240);
}

QTEST_GUILESS_MAIN(TaskbarLyricPreferencesTest)
#include "TaskbarLyricPreferencesTest.moc"
