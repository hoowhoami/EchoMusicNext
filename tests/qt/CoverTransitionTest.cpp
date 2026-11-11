#include "ThemeStore.h"
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

using namespace EchoMusic;

class CoverTransitionTest : public QObject
{
    Q_OBJECT
private slots:
    void frostedShellRetainsCoverUntilEntryFinishes()
    {
        QTemporaryDir directory;
        QImage artwork(64, 64, QImage::Format_RGB32);
        artwork.fill(QColor("#f02955"));
        const auto path = directory.filePath("flight-cover.png");
        QVERIFY(artwork.save(path));
        SettingsStore settings(directory.filePath("settings.ini"));
        ThemeStore theme(settings);
        QQmlEngine engine;
        theme.attachToEngine(engine);
        QQuickWindow window;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
    id: host
    width: 56; height: 56
    property bool playbackPresent: false
    property bool playbackOpened: false
    property bool allowRetention: true
    property bool resourcesAllowed: true
    property alias cover: cover
    Item {
        id: shell
        anchors.fill: parent
        visible: !host.playbackPresent
        EchoCover {
            id: cover
            anchors.fill: parent
            transitionSourceActive: host.allowRetention && host.playbackPresent && !host.playbackOpened
            resourceActive: host.resourcesAllowed && (shell.visible || transitionSourceActive)
        }
    }
})",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *host = qobject_cast<QQuickItem *>(object.data());
        auto *cover = object->property("cover").value<QQuickItem *>();
        QVERIFY(host && cover);
        host->setParentItem(window.contentItem());
        cover->setProperty("source", QUrl::fromLocalFile(path));
        QTRY_COMPARE(cover->property("status").toInt(), 1);
        auto *image = cover->findChild<QQuickItem *>("coverImage");
        QVERIFY(image);
        const auto source = image->property("source").toUrl();
        const auto decodeSize = image->property("sourceSize").toSize();
        QVERIFY(!source.isEmpty());
        for (int cycle = 0; cycle < 2; ++cycle) {
            QSignalSpy sourceChanges(image,
                                     image->metaObject()
                                         ->property(image->metaObject()->indexOfProperty("source"))
                                         .notifySignal());
            QVERIFY(sourceChanges.isValid());
            host->setProperty("playbackPresent", true);
            QVERIFY(!cover->isVisible());
            QVERIFY(cover->property("resourceActive").toBool());
            QTest::qWait(80);
            QCOMPARE(image->property("source").toUrl(), source);
            QCOMPARE(cover->property("status").toInt(), 1);
            QCOMPARE(image->property("sourceSize").toSize(), decodeSize);
            QCOMPARE(sourceChanges.size(), 0);
            QCOMPARE(cover->property("transitionArtwork").value<QQuickItem *>(), image);
            host->setProperty("playbackOpened", true);
            QTRY_COMPARE(cover->property("status").toInt(), 0);
            QVERIFY(image->property("source").toUrl().isEmpty());
            host->setProperty("playbackPresent", false);
            host->setProperty("playbackOpened", false);
            QTRY_COMPARE(cover->property("status").toInt(), 1);
        }
        host->setProperty("allowRetention", false);
        host->setProperty("playbackPresent", true);
        QTRY_COMPARE(cover->property("status").toInt(), 0);
        host->setProperty("playbackPresent", false);
        QTRY_COMPARE(cover->property("status").toInt(), 1);
        host->setProperty("allowRetention", true);
        host->setProperty("playbackPresent", true);
        host->setProperty("resourcesAllowed", false);
        QTRY_COMPARE(cover->property("status").toInt(), 0);
    }
};

QTEST_MAIN(CoverTransitionTest)
#include "CoverTransitionTest.moc"
