#include "TaskbarProgressState.h"
#include <QtTest>

// The taskbar progress bar is pure state: playback reports a track, a position
// and a duration, and the resolver decides which of the four Windows progress
// modes the button shows. The throttle is separate from the mapping and is
// pinned here too, because a wrong threshold either starves the bar or floods
// the shell with identical updates.
using EchoMusic::resolveTaskbarProgress;
using EchoMusic::TaskbarProgressGate;
using EchoMusic::TaskbarProgressMode;

class TaskbarProgressStateTest : public QObject
{
    Q_OBJECT
private slots:
    void noTrackRemovesTheBar();
    void unknownDurationIsIndeterminate();
    void playingShowsTheFraction();
    void positionBeyondDurationClamps();
    void pausedKeepsAVisibleSlice();
    void gatePassesFirstValueImmediately();
    void gateThrottlesIdenticalValues();
    void gateAppliesModeChangesImmediately();
    void gateAppliesVisibleRatioStepsImmediately();
    void gateResetForgetsTheLastValue();
};

void TaskbarProgressStateTest::noTrackRemovesTheBar()
{
    const auto value = resolveTaskbarProgress(false, true, 30'000, 120'000);
    QCOMPARE(value.mode, TaskbarProgressMode::None);
    QCOMPARE(value.ratio, 0.0);
}

void TaskbarProgressStateTest::unknownDurationIsIndeterminate()
{
    // A live stream reports no duration; an all-zero normal bar would look
    // stalled, so the mode carries the activity instead.
    const auto value = resolveTaskbarProgress(true, true, 0, 0);
    QCOMPARE(value.mode, TaskbarProgressMode::Indeterminate);
    QCOMPARE(value.ratio, 1.0);
}

void TaskbarProgressStateTest::playingShowsTheFraction()
{
    const auto value = resolveTaskbarProgress(true, true, 30'000, 120'000);
    QCOMPARE(value.mode, TaskbarProgressMode::Normal);
    QCOMPARE(value.ratio, 0.25);
}

void TaskbarProgressStateTest::positionBeyondDurationClamps()
{
    // The engine can report a position past the metadata duration during a
    // rebuffer; the fraction must stay inside the bar.
    const auto value = resolveTaskbarProgress(true, true, 130'000, 120'000);
    QCOMPARE(value.mode, TaskbarProgressMode::Normal);
    QCOMPARE(value.ratio, 1.0);
}

void TaskbarProgressStateTest::pausedKeepsAVisibleSlice()
{
    const auto atStart = resolveTaskbarProgress(true, false, 0, 120'000);
    QCOMPARE(atStart.mode, TaskbarProgressMode::Paused);
    QCOMPARE(atStart.ratio, 0.01);
    // Past the minimum the real fraction is kept, so the pause marker does not
    // jump back to the left edge.
    const auto midway = resolveTaskbarProgress(true, false, 60'000, 120'000);
    QCOMPARE(midway.mode, TaskbarProgressMode::Paused);
    QCOMPARE(midway.ratio, 0.5);
}

void TaskbarProgressStateTest::gatePassesFirstValueImmediately()
{
    TaskbarProgressGate gate;
    const auto value = resolveTaskbarProgress(true, true, 10'000, 120'000);
    QVERIFY(gate.accept(value, 1'000));
    QCOMPARE(gate.last().mode, value.mode);
    QCOMPARE(gate.last().ratio, value.ratio);
}

void TaskbarProgressStateTest::gateThrottlesIdenticalValues()
{
    TaskbarProgressGate gate;
    const auto value = resolveTaskbarProgress(true, true, 10'000, 120'000);
    QVERIFY(gate.accept(value, 0));
    // Same fraction inside the throttle window: the bar already shows it.
    QVERIFY(!gate.accept(value, 100));
    // Past the window the value is re-sent, so a bar Explorer dropped while
    // hidden recovers.
    QVERIFY(gate.accept(value, 200));
}

void TaskbarProgressStateTest::gateAppliesModeChangesImmediately()
{
    TaskbarProgressGate gate;
    QVERIFY(gate.accept(resolveTaskbarProgress(true, true, 10'000, 120'000), 0));
    // Pause must repaint the bar at once, inside the throttle window.
    QVERIFY(gate.accept(resolveTaskbarProgress(true, false, 10'000, 120'000), 10));
}

void TaskbarProgressStateTest::gateAppliesVisibleRatioStepsImmediately()
{
    TaskbarProgressGate gate;
    QVERIFY(gate.accept(resolveTaskbarProgress(true, true, 10'000, 120'000), 0));
    // A full percent is visible on the button; only sub-permille noise waits.
    QVERIFY(gate.accept(resolveTaskbarProgress(true, true, 12'000, 120'000), 10));
    QVERIFY(!gate.accept(resolveTaskbarProgress(true, true, 12'050, 120'000), 20));
}

void TaskbarProgressStateTest::gateResetForgetsTheLastValue()
{
    TaskbarProgressGate gate;
    const auto value = resolveTaskbarProgress(true, true, 10'000, 120'000);
    QVERIFY(gate.accept(value, 0));
    gate.reset();
    // Nothing is applied to a torn-down button, so the next value is the first
    // one again.
    QVERIFY(gate.accept(value, 10));
}

QTEST_MAIN(TaskbarProgressStateTest)
#include "TaskbarProgressStateTest.moc"
