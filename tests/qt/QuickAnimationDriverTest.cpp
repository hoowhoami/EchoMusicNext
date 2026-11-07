#include "QuickAnimationDriver.h"
#include "NativeWindow.h"
#include <QAnimationDriver>
#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantAnimation>
#include <QtTest>
#include <memory>
#include <QtCore/private/qabstractanimation_p.h>
#include <QtQuick/private/qsgrenderloop_p.h>

using namespace EchoMusic;

class QuickAnimationDriverTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QQuickWindow> m_window;
    static QAnimationDriver *driver()
    {
        return QSGRenderLoop::instance()->animationDriver();
    }
    static void waitNative(int milliseconds)
    {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    }

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression("QUnifiedTimer::.*"));
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(320, 240);
        QVERIFY(driver());
        if (!qEnvironmentVariableIntValue("ECHO_ANIMATION_GUARD_BASELINE"))
            installQuickAnimationDriverGuard(*m_window);
        m_window->show();
        QVERIFY(Test::prepareNativeInput(m_window.get()));
        QTRY_VERIFY(!driver()->isRunning());
    }
    void cleanup()
    {
        if (driver() && driver()->isRunning() &&
            QUnifiedTimer::instance()->runningAnimationCount() == 0)
            QMetaObject::invokeMethod(QUnifiedTimer::instance(), "stopTimer", Qt::DirectConnection);
        m_window.reset();
    }
    void emptyRestartDoesNotKeepRendering()
    {
        QCOMPARE(QUnifiedTimer::instance()->runningAnimationCount(), 0);
        // Qt 6.12 localRestart starts its driver even with no registered timers.
        // This is the same state captured during the real playback soak.
        QUnifiedTimer::instance()->restart();
        QVERIFY(driver()->isRunning());
        waitNative(50);
        QTRY_VERIFY_WITH_TIMEOUT(!driver()->isRunning(), 1000);
    }
    void pendingAndRunningAnimationKeepsItsClock()
    {
        QVariantAnimation animation;
        animation.setStartValue(0.);
        animation.setEndValue(1.);
        animation.setDuration(200);
        animation.setLoopCount(2);
        QUnifiedTimer::instance()->restart();
        animation.start();
        waitNative(70);
        QCOMPARE(animation.state(), QAbstractAnimation::Running);
        QVERIFY(animation.currentValue().toDouble() > 0);
        QVERIFY(animation.currentValue().toDouble() < 1);
        QTRY_COMPARE(animation.state(), QAbstractAnimation::Stopped);
        QCOMPARE(animation.currentValue().toDouble(), 1.);
        QTRY_VERIFY(!driver()->isRunning());
    }
    void qmlFrameAndPauseTimersRemainFunctional()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
Item {
    id: root
    property bool animate: true
    property int frames: 0
    property int timers: 0
    FrameAnimation { running: root.animate; onTriggered: root.frames++ }
    Timer { interval: 60; running: true; repeat: true; onTriggered: root.timers++ }
})",
                          QUrl("qrc:/AnimationDriverProbe.qml"));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.get());
        QVERIFY(item);
        item->setParentItem(m_window->contentItem());
        QTRY_VERIFY(root->property("frames").toInt() >= 5);
        QTRY_VERIFY(root->property("timers").toInt() >= 3);
        root->setProperty("animate", false);
        const auto frames = root->property("frames").toInt();
        const auto timers = root->property("timers").toInt();
        waitNative(220);
        QCOMPARE(root->property("frames").toInt(), frames);
        QVERIFY(root->property("timers").toInt() >= timers + 2);
    }
    void severalWindowsShareOneGuard()
    {
        QQuickWindow other;
        other.resize(320, 240);
        installQuickAnimationDriverGuard(other);
        installQuickAnimationDriverGuard(*m_window);
        QCOMPARE(driver()
                     ->findChildren<QObject *>("echoQuickAnimationDriverGuard",
                                               Qt::FindDirectChildrenOnly)
                     .size(),
                 1);
        other.show();
        QVERIFY(Test::prepareNativeInput(&other));
        m_window.reset();
        QVariantAnimation animation;
        animation.setStartValue(0.);
        animation.setEndValue(1.);
        animation.setDuration(100);
        animation.start();
        QTRY_COMPARE(animation.state(), QAbstractAnimation::Stopped);
        QCOMPARE(animation.currentValue().toDouble(), 1.);
        QUnifiedTimer::instance()->restart();
        waitNative(50);
        QTRY_VERIFY(!driver()->isRunning());
    }
    void renderThreadAnimatorStillCompletes()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
Rectangle {
    id: root
    width: 60; height: 16; color: "red"
    property alias animating: turn.running
    RotationAnimator on rotation { id: turn; from: 0; to: 90; duration: 240; running: true }
})",
                          QUrl("qrc:/RenderThreadAnimatorProbe.qml"));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.get());
        QVERIFY(item);
        item->setParentItem(m_window->contentItem());
        QTRY_VERIFY(!root->property("animating").toBool());
        QTRY_COMPARE(item->rotation(), 90.);
        QTRY_VERIFY(!driver()->isRunning());
    }
    void rapidStartStopDoesNotLeaveAnIdleDriver()
    {
        for (int i = 0; i < 40; ++i) {
            QVariantAnimation animation;
            animation.setStartValue(0.);
            animation.setEndValue(1.);
            animation.setDuration(1);
            QUnifiedTimer::instance()->restart();
            animation.start();
            if (i % 2)
                animation.stop();
            waitNative(5);
        }
        QTRY_VERIFY(!driver()->isRunning());
    }
};

QTEST_MAIN(QuickAnimationDriverTest)
#include "QuickAnimationDriverTest.moc"
