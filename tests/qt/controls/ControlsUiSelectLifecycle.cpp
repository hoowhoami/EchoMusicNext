#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"
#include <QQmlIncubator>
#include <QQmlIncubationController>
#include <QMetaProperty>

namespace
{
class SelectCreation final : public QQmlIncubator
{
public:
    SelectCreation(QQuickItem *parent, std::atomic<bool> &proceed, int cycle)
        : QQmlIncubator(Asynchronous), m_parent(parent), m_proceed(proceed), m_cycle(cycle)
    {
    }
    QPointer<QQuickItem> item;
    bool observed = false;
    bool publishedEarly = true;
    bool readyEarly = true;
    bool commandsAccepted = true;

protected:
    void setInitialState(QObject *object) override
    {
        item = qobject_cast<QQuickItem *>(object);
        if (!item) {
            m_proceed.store(false);
            return;
        }
        item->setParent(m_parent);
        item->setParentItem(m_parent);
        item->setPosition({30, 30});
        item->setSize({180, 36});
        observed = true;
        readyEarly = item->property("_componentReady").toBool();
        publishedEarly = item->property("popup").value<QObject *>() != nullptr;
        if (m_cycle == 1 || m_cycle == 2)
            commandsAccepted &= QMetaObject::invokeMethod(item, "openPopup");
        if (m_cycle == 2)
            commandsAccepted &= QMetaObject::invokeMethod(item, "closePopup");
        m_proceed.store(false);
    }

private:
    QQuickItem *m_parent;
    std::atomic<bool> &m_proceed;
    int m_cycle;
};
} // namespace

void ControlsUiTest::selectPublishesPopupAfterConstruction()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    session.theme.setMotionEnabled(false);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent host(&session.engine);
    host.setData("import QtQuick\nWindow { width:400; height:240; visible:true }", QUrl());
    session.root.reset(host.create());
    QVERIFY2(session.root, qPrintable(host.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    QQmlComponent recipe(&session.engine, QUrl("qrc:/qt/qml/EchoMusic/Controls/EchoSelect.qml"));
    QVERIFY2(recipe.isReady(), qPrintable(recipe.errorString()));
    QQmlIncubationController controlled;
    auto *normal = session.engine.incubationController();
    session.engine.setIncubationController(&controlled);
    const auto restore = qScopeGuard([&] { session.engine.setIncubationController(normal); });
    for (int cycle = 0; cycle < 4; ++cycle) {
        std::atomic<bool> proceed{true};
        SelectCreation creation(session.window->contentItem(), proceed, cycle);
        recipe.create(creation);
        controlled.incubateWhile(&proceed, 5000);
        QVERIFY(creation.observed && creation.item);
        QVERIFY(!creation.publishedEarly && !creation.readyEarly);
        QVERIFY(creation.commandsAccepted);
        if (cycle == 0) {
            creation.clear();
            QTRY_VERIFY(creation.item.isNull());
        } else {
            QTRY_VERIFY(([&] {
                controlled.incubateFor(1);
                return creation.status() == QQmlIncubator::Ready;
            })());
            auto *select = creation.item.data();
            QVERIFY(select->property("_componentReady").toBool());
            const auto popupProperty =
                select->metaObject()->property(select->metaObject()->indexOfProperty("popup"));
            QVERIFY(!popupProperty.isWritable());
            QVERIFY(popupProperty.metaType().flags().testFlag(QMetaType::PointerToQObject));
            if (cycle != 1) {
                QVERIFY(!select->property("popup").value<QObject *>());
                QVERIFY(Test::settleNativeItem(select));
                QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                                  select->mapToScene(select->boundingRect().center()).toPoint());
            }
            QPointer<QObject> popup;
            QTRY_VERIFY((popup = select->property("popup").value<QObject *>()) &&
                        popup->property("opened").toBool());
            QVERIFY(select->property("optionList").value<QObject *>());
            if (cycle != 3) {
                QVERIFY(QMetaObject::invokeMethod(select, "closePopup"));
                QTRY_VERIFY(popup.isNull());
            } else {
                // Retiring the owner while a searched popup is still open
                // must not leave a public reference to its closing content.
                select->setProperty("searchTerm", "retiring");
            }
            creation.clear(); // Ready objects remain caller-owned.
            delete select;
            QVERIFY(creation.item.isNull());
            QTRY_VERIFY(popup.isNull());
        }
        QCOMPARE(controlled.incubatingObjectCount(), 0);
    }
    QStringList errors;
    for (const auto &entry : warnings)
        for (const auto &error : entry.value(0).value<QList<QQmlError>>())
            errors << error.toString();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
}
