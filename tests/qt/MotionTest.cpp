#include "ThemeStore.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include "SettingsStore.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace EchoMusic;
namespace
{
QObject *motionObject(QObject *object, const QString &name)
{
    if (object->objectName() == name)
        return object;
    for (auto *child : object->children())
        if (auto *found = motionObject(child, name))
            return found;
    return nullptr;
}
} // namespace
class MotionTest : public QObject
{
    Q_OBJECT
private slots:
    void contentEntryHonorsMotionVisibilityAndStagger()
    {
        QTemporaryDir dir;
        EchoMusic::SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QStringList errors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const auto &warnings) {
            for (const auto &warning : warnings)
                errors.append(warning.toString());
        });
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Window
import EchoMusic.Controls
Window {
 id: window
 visible: true; width: 400; height: 300
 Rectangle {
  id: target
  objectName: "motionTarget"
  anchors.centerIn: parent
  width: 100; height: 100
  color: "red"
 }
 EchoEnter {
  id: enter
  objectName: "motionEnter"
  target: target
  active: false
  duration: 300
  order: 2
 }
})",
                          QUrl("qrc:/motion-entry.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        auto *target = window->findChild<QQuickItem *>("motionTarget");
        auto *enter = motionObject(window, "motionEnter");
        QVERIFY(target && enter);
        QTRY_COMPARE(target->property("opacity").toDouble(), 0.);
        enter->setProperty("active", true);
        QTRY_VERIFY(enter->property("running").toBool());
        QVERIFY(target->property("opacity").toDouble() < 1.);
        QTRY_COMPARE_WITH_TIMEOUT(target->property("opacity").toDouble(), 1., 2000);
        QTRY_VERIFY(!enter->property("running").toBool());
        // Toggling reduced motion while entering lands on the final frame.
        enter->setProperty("active", false);
        enter->setProperty("active", true);
        QTRY_VERIFY(enter->property("running").toBool());
        theme.setMotionEnabled(false);
        QTRY_COMPARE(target->property("opacity").toDouble(), 1.);
        QVERIFY(!enter->property("running").toBool());
        theme.setMotionEnabled(true);
        // Deactivation hides immediately; no leave animation keeps a stale frame.
        enter->setProperty("active", false);
        QCOMPARE(target->property("opacity").toDouble(), 0.);
        // Reduced motion lands on the final frame without a timer.
        theme.setMotionEnabled(false);
        enter->setProperty("active", true);
        QTRY_COMPARE(target->property("opacity").toDouble(), 1.);
        QVERIFY(!enter->property("running").toBool());
        // A hidden window must never leave content transparent.
        enter->setProperty("active", false);
        theme.setMotionEnabled(true);
        window->hide();
        enter->setProperty("active", true);
        QTRY_COMPARE(target->property("opacity").toDouble(), 1.);
        QVERIFY(!enter->property("running").toBool());
        window->show();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
};
QTEST_MAIN(MotionTest)
#include "MotionTest.moc"
