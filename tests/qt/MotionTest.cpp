#include "ThemeStore.h"
#include "SongCatalogModel.h"
#include <QDir>
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
QObject *motionObjectImpl(QObject *object, const QString &name, QSet<QObject *> &seen)
{
    if (seen.contains(object))
        return nullptr;
    seen.insert(object);
    if (object->objectName() == name)
        return object;
    for (auto *child : object->children())
        if (auto *found = motionObjectImpl(child, name, seen))
            return found;
    if (auto *item = qobject_cast<QQuickItem *>(object))
        for (auto *child : item->childItems())
            if (auto *found = motionObjectImpl(child, name, seen))
                return found;
    return nullptr;
}
QObject *motionObject(QObject *object, const QString &name)
{
    QSet<QObject *> seen;
    return motionObjectImpl(object, name, seen);
}
} // namespace
class MotionTest : public QObject
{
    Q_OBJECT
private slots:
    void popupEntryPreservesPlacementAndSettlesWhenHidden()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
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
import EchoMusic.Controls
Window {
 visible: true; width: 500; height: 400
 Rectangle { id: anchor; x: 160; y: 100; width: 40; height: 40; color: "red" }
 EchoAnchoredPopup {
  objectName: "popup"; anchorItem: anchor; preferredWidth: 220; uiScale: 1.25
  contentItem: Item { implicitHeight: 60; Text { text: "弹层 · Popup" } }
 }
})",
                          QUrl("qrc:/motion-popup.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        auto *popup = motionObject(root.data(), "popup");
        QVERIFY(window && popup);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(popup->property("anchorRect").toRectF().y(), 100.);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        auto *motion = motionObject(popup, "popupMotion");
        QVERIFY(motion);
        auto *slide = qvariant_cast<QObject *>(motion->property("slide"));
        QVERIFY(slide);
        const auto y = popup->property("y").toDouble();
        QTRY_VERIFY(slide->property("progress").toDouble() > 0);
        QVERIFY(slide->property("progress").toDouble() < 1);
        QCOMPARE(popup->property("y").toDouble(), y);
        window->hide();
        QTRY_COMPARE(slide->property("progress").toDouble(), 1.);
        QTRY_COMPARE(popup->property("opacity").toDouble(), 1.);
        theme.setMotionEnabled(false);
        QMetaObject::invokeMethod(popup, "close");
        window->show();
        QMetaObject::invokeMethod(popup, "open");
        QTRY_VERIFY(popup->property("opened").toBool());
        QCOMPARE(slide->property("progress").toDouble(), 1.);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void sharedSongInsertionKeepsScrollAndIgnoresOffscreenChanges()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
        ThemeStore theme(prefs);
        SongCatalogModel songs;
        if (qEnvironmentVariableIsSet("ECHO_MOTION_DARK"))
            theme.setMode(ThemeStore::Dark);
        else
            theme.setMode(ThemeStore::Light);
        constexpr int songCount = 10000;
        const auto song = [](int id) {
            return QJsonObject{{"id", QString::number(id)},
                               {"title", QString("Song %1").arg(id)},
                               {"hash", QString::number(id)},
                               {"artists", QJsonArray{}}};
        };
        QJsonArray entries;
        for (int i = 1; i <= songCount; ++i)
            entries.append(song(i));
        songs.replaceSongs(entries);
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
import EchoMusic.Music
import EchoMusic.Theme
Window {
 visible: true; width: 600; height: 360; color: Theme.main
 required property SongCatalogModel collection
 property real testScale: 1
 SongList {
  id: songs; objectName: "songs"; width: parent.width / testScale; height: parent.height / testScale
  scale: testScale; transformOrigin: Item.TopLeft; model: collection
  topMargin: 276
 }
})",
                          QUrl("qrc:/motion-songs.qml"));
        const auto requestedScale = qEnvironmentVariable("ECHO_MOTION_SCALE").toDouble();
        QScopedPointer<QObject> root(component.createWithInitialProperties(
            {{"collection", QVariant::fromValue(&songs)},
             {"testScale", requestedScale > 0 ? requestedScale : 1.}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        auto *view = root->findChild<QQuickItem *>("songs");
        QVERIFY(window && view);
        QTRY_VERIFY(window->isVisible());
        QTRY_VERIFY(view->height() > 0);
        QMetaObject::invokeMethod(view, "forceLayout");
        QTRY_VERIFY(view->property("count").toInt() == songCount);
        auto *content = qvariant_cast<QQuickItem *>(view->property("contentItem"));
        QVERIFY(content);
        QVERIFY(content->childItems().size() < 40);
        auto *motion = motionObject(view, "listInsertMotion");
        QVERIFY(motion);
        auto *slide = qvariant_cast<QObject *>(motion->property("slide"));
        QVERIFY(slide);
        QTRY_VERIFY(slide->property("allowed").toBool());
        const auto scroll =
            view->property("contentY").toDouble() - view->property("originY").toDouble();
        const auto save = [&](const QString &name) {
            const auto path = qEnvironmentVariable("ECHO_MOTION_ARTIFACT_DIR");
            if (!path.isEmpty()) {
                QDir().mkpath(path);
                const auto frame = window->grabWindow();
                QVERIFY(!frame.isNull());
                QVERIFY(frame.save(path + "/" + name + ".png"));
            }
        };
        save("songs-before");
        entries.prepend(song(songCount + 1));
        songs.replaceSongs(entries, true);
        QTRY_VERIFY(motion->property("running").toBool());
        QTest::qWait(35);
        QVERIFY(content->opacity() >= .92 && content->opacity() < 1.);
        QVERIFY(content->mapToItem(view, QPointF()).y() < content->y());
        QCOMPARE(view->property("contentY").toDouble() - view->property("originY").toDouble(),
                 scroll);
        save("songs-inserting");
        QTRY_VERIFY(!motion->property("running").toBool());
        QCOMPARE(content->opacity(), 1.);
        save("songs-after");
        motion->setProperty("occlusionHeight", view->height());
        entries.prepend(song(songCount + 4));
        songs.replaceSongs(entries, true);
        QVERIFY(!motion->property("running").toBool()); // Covered by a sticky header.
        motion->setProperty("occlusionHeight", 0.);
        songs.appendSongs(QJsonArray{song(songCount + 2)});
        QVERIFY(!motion->property("running").toBool());
        // A stable-id promotion shares the same presentation, without a reset.
        entries.prepend(entries.takeAt(20));
        songs.replaceSongs(entries, true);
        QTRY_VERIFY(motion->property("running").toBool());
        QCOMPARE(view->property("contentY").toDouble() - view->property("originY").toDouble(),
                 scroll);
        theme.setMotionEnabled(false);
        QTRY_VERIFY(!motion->property("running").toBool());
        QCOMPARE(content->opacity(), 1.);
        theme.setMotionEnabled(true);
        entries.prepend(song(songCount + 3));
        songs.replaceSongs(entries, true);
        QTRY_VERIFY(motion->property("running").toBool());
        window->hide();
        QTRY_VERIFY(!motion->property("running").toBool());
        QCOMPARE(content->opacity(), 1.);
        // Real SongRow removal retains only a finite visual delegate.
        window->show();
        const auto count = view->property("count").toInt();
        entries.removeAt(0);
        songs.replaceSongs(entries, true);
        QTRY_COMPARE(view->property("count").toInt(), count - 1);
        QTest::qWait(300);
        // Once the reader has scrolled, a prepend keeps the native visible
        // row anchor; preserving the expanded header must not force a reset.
        view->setProperty("contentY", view->property("originY").toDouble() + 1200);
        QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
        QQuickItem *anchor = nullptr;
        for (auto *row : content->childItems()) {
            const auto y = row->mapToItem(view, QPointF()).y();
            if (row->objectName() == "songDelegate" && y >= 60 && y < 180) {
                anchor = row;
                break;
            }
        }
        QVERIFY(anchor);
        const auto anchorId = anchor->property("resourceId").toString();
        const auto anchorY = anchor->mapToItem(view, QPointF()).y();
        entries.prepend(song(songCount + 5));
        songs.replaceSongs(entries, true);
        QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
        QVERIFY(!motion->property("running").toBool());
        QCOMPARE(anchor->property("resourceId").toString(), anchorId);
        QCOMPARE(anchor->mapToItem(view, QPointF()).y(), anchorY);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void rollingStatisticsKeepNativeMetricsAndReleaseHiddenDigits()
    {
        QTemporaryDir dir;
        SettingsStore prefs(dir.filePath("prefs.ini"));
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
import EchoMusic.Controls
import EchoMusic.Theme
Window {
 visible: true; width: 400; height: 180; color: Theme.main
 EchoRollingNumber { id: number; objectName: "number"; text: "10,095 小时"; x: 20; y: 20 }
 Text { objectName: "reference"; text: number.text; font: number.font; color: number.color; x: 20; y: 80 }
})",
                          QUrl("qrc:/motion-number.qml"));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        auto *number = root->findChild<QQuickItem *>("number");
        auto *reference = root->findChild<QQuickItem *>("reference");
        QVERIFY(window && number && reference);
        QTRY_COMPARE(number->implicitWidth(), reference->implicitWidth());
        QCOMPARE(number->implicitHeight(), reference->implicitHeight());
        QCOMPARE(number->baselineOffset(), reference->baselineOffset());
        const auto captures = qEnvironmentVariable("ECHO_MOTION_ARTIFACT_DIR");
        auto *roll = motionObject(number, "digitRoll");
        QVERIFY(roll);
        auto *nine = motionObject(number, "rollingDigit-4");
        auto *nineValue = nine ? motionObject(nine, "rollingDigitValue") : nullptr;
        QVERIFY(nineValue);
        QSet<int> intermediates;
        int last = -1;
        bool countedFrame = false;
        QElapsedTimer initialClock;
        initialClock.start();
        while (roll->property("running").toBool() && initialClock.elapsed() < 2000) {
            const auto value = nineValue->property("text").toInt();
            QVERIFY(value >= last);
            last = value;
            if (value > 0 && value < 9)
                intermediates.insert(value);
            if (!captures.isEmpty() && !countedFrame && value >= 2 && value <= 7) {
                QVERIFY(QDir().mkpath(captures));
                QVERIFY(window->grabWindow().save(captures + "/numbers-counting.png"));
                countedFrame = true;
            }
            QTest::qWait(16);
        }
        QVERIFY(!roll->property("running").toBool());
        // Inspect the text actually drawn between the endpoints. A 0/9
        // cross-fade, even with a 900ms duration, cannot satisfy this.
        QVERIFY(intermediates.size() >= 3);
        QCOMPARE(nineValue->property("text").toString(), QString("9"));
        // Follow a changing digit on the real frame clock. The old 240ms
        // panel timing would have already finished at this checkpoint.
        number->setProperty("text", "20,096 小时");
        QTRY_VERIFY(roll->property("running").toBool());
        QTest::qWait(300);
        QVERIFY(roll->property("running").toBool());
        auto *digit = roll->property("target").value<QObject *>();
        QVERIFY(digit);
        const auto progress = digit->property("progress").toReal();
        QVERIFY(progress > .7 && progress < .9);
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            QVERIFY(window->grabWindow().save(captures + "/numbers-rolling.png"));
        }
        QTRY_VERIFY(!roll->property("running").toBool());
        QCOMPARE(digit->property("progress").toReal(), 1.);

        number->setProperty("text", "99 小时");
        QTRY_VERIFY(!roll->property("running").toBool());
        number->setProperty("text", "100 小时");
        QTRY_VERIFY(roll->property("running").toBool());
        auto *units = motionObject(number, "rollingDigit-2");
        QVERIFY(units);
        QCOMPARE(units->property("position").toReal(), 9.);
        QTest::qWait(100);
        const auto carriedPosition = units->property("position").toReal();
        QVERIFY(carriedPosition > 9 && carriedPosition < 10);
        number->setProperty("text", "98 小时");
        auto *reversedUnits = motionObject(number, "rollingDigit-1");
        QVERIFY(reversedUnits);
        QVERIFY(qAbs(reversedUnits->property("position").toReal() - carriedPosition) < .001);
        QTest::qWait(100);
        QVERIFY(reversedUnits->property("position").toReal() < carriedPosition);
        QTRY_VERIFY(!roll->property("running").toBool());
        QCOMPARE(motionObject(reversedUnits, "rollingDigitValue")->property("text").toString(),
                 QString("8"));

        number->setProperty("text", "5 天 2 小时");
        QTest::qWait(100);
        auto *days = motionObject(number, "rollingDigit-0");
        auto *hours = motionObject(number, "rollingDigit-4");
        QVERIFY(days && hours);
        QVERIFY(days->property("position").toReal() > 5 && days->property("position").toReal() < 8);
        QVERIFY(hours->property("position").toReal() > 0 &&
                hours->property("position").toReal() < 2);

        // Updating while rolling still yields to reduced motion immediately.
        number->setProperty("text", "10,096 小时");
        QTRY_VERIFY(roll->property("running").toBool());
        theme.setMotionEnabled(false);
        QTRY_VERIFY(!motionObject(number, "digitRoll"));
        QCOMPARE(number->property("text").toString(), QString("10,096 小时"));
        if (!captures.isEmpty())
            QVERIFY(window->grabWindow().save(captures + "/numbers-after.png"));
        theme.setMotionEnabled(true);
        QTRY_VERIFY(motionObject(number, "digitRoll"));
        window->hide();
        QTRY_VERIFY(!motionObject(number, "digitRoll"));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
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
