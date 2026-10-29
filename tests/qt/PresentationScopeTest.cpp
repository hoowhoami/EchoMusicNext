#include "PresentationScope.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>
using namespace EchoMusic;

class PresentationScopeTest : public QObject
{
    Q_OBJECT
private slots:
    void publicContainerObservesClippingAndRetiresLoader()
    {
        qmlRegisterType<PresentationScope>("EchoMusic.PresentationProbe", 1, 0,
                                           "PresentationScope");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import EchoMusic.PresentationProbe 1.0
Item {
    width: 300; height: 200
    property alias content: content
    property alias scope: scope
    property alias instance: loader.item
    property int created: 0
    property int destroyed: 0
    Item {
        id: clipper
        width: 200; height: 100; clip: true
        Item {
            id: content
            width: 200; height: 800
            PresentationScope {
                id: scope
                width: 100; height: 80
                Loader {
                    id: loader
                    active: scope.presented
                    sourceComponent: Item {
                        Component.onCompleted: created += 1
                        Component.onDestruction: destroyed += 1
                    }
                }
            }
        }
    }
})",
                          QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.data());
        auto *scope = object->property("scope").value<PresentationScope *>();
        auto *content = object->property("content").value<QQuickItem *>();
        QVERIFY(item && scope && content);
        QVERIFY(!scope->presented());
        QCOMPARE(object->property("created").toInt(), 0);
        QQuickWindow window;
        window.resize(300, 200);
        item->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(scope->presented());
        QCOMPARE(object->property("created").toInt(), 1);
        content->setY(-81);
        QTRY_VERIFY(!scope->presented());
        QTRY_COMPARE(object->property("destroyed").toInt(), 1);
        content->setY(-79); // one visible logical pixel still needs presentation
        QTRY_VERIFY(scope->presented());
        QCOMPARE(object->property("created").toInt(), 2);
        content->setOpacity(0);
        QTRY_VERIFY(!scope->presented());
        content->setOpacity(1);
        QTRY_VERIFY(scope->presented());
        content->setVisible(false);
        QTRY_VERIFY(!scope->presented());
        content->setVisible(true);
        QTRY_VERIFY(scope->presented());
        window.hide();
        QTRY_VERIFY(!scope->presented());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(scope->presented());
        item->setParentItem(nullptr);
        QTRY_VERIFY(!scope->presented());
        QTRY_COMPARE(object->property("created").toInt(), object->property("destroyed").toInt());
    }
    void reparentTransformsAndWindowEdges()
    {
        QQuickWindow window;
        window.resize(300, 200);
        QQuickItem first(window.contentItem()), second(window.contentItem());
        first.setSize({300, 200});
        second.setSize({300, 200});
        second.setX(400);
        PresentationScope scope(&first);
        scope.setSize({40, 40});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(scope.presented());
        scope.setParentItem(&second);
        QTRY_VERIFY(!scope.presented());
        second.setX(299);
        QTRY_VERIFY(scope.presented());
        second.setScale(0);
        QTRY_VERIFY(!scope.presented());
        second.setScale(1);
        second.setX(320);
        second.setTransformOrigin(QQuickItem::TopLeft);
        second.setRotation(90);
        QTRY_VERIFY(scope.presented());
        scope.setWidth(0);
        QTRY_VERIFY(!scope.presented());
        scope.setWidth(40);
        QTRY_VERIFY(scope.presented());
        window.showMinimized();
        QTRY_VERIFY(!scope.presented());
    }
};
QTEST_MAIN(PresentationScopeTest)
#include "PresentationScopeTest.moc"
