#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(EchoMusic_ThemesPlugin)
#include "EchoPluginViewContext.h"
#include "plugins/PluginRuntimeTestFixture.h"

class PluginViewCommandsTest : public QObject
{
    Q_OBJECT
private slots:
    void closeSettingsUsesActivePresentation();
    void closeSettingsCompletesBeforeItsOwnPresentationRetires();
};
void PluginViewCommandsTest::closeSettingsUsesActivePresentation()
{
    Host host;
    host.runtime.importDirectory(
        package(host.temporary.path(), "export function activate(ctx) {}\n"));
    QString installation;
    QTRY_VERIFY(!(installation = host.runtime.installationForId("test.runtime")).isEmpty());
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QObject owner;
    owner.setProperty("active", true);
    auto *presentation = scope->createPresentation(&owner, "skin.settings");
    QVERIFY(presentation);
    EchoPluginViewContext page(*presentation, "skin.settings", &owner);
    QSignalSpy requests(host.theme.extensions(), &AppearanceCatalog::requestRaised);
    QPointer<EchoPluginOperation> operation = page.closeSkinSettings();
    QJSValue handle = host.engine.newQObject(operation);
    QTRY_COMPARE(operation->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(requests.count(), 1);
    QCOMPARE(requests.at(0).at(0).toString(), QString("closeSkinSettings"));
    owner.setProperty("active", false);
    auto *rejected = page.closeSkinSettings();
    const auto rejectedHandle = host.engine.newQObject(rejected);
    QCOMPARE(rejected->state(), EchoPluginOperation::Failed);
    QCOMPARE(rejected->errorCode(), QString("ScopeClosed"));
    QCOMPARE(requests.count(), 1);
}
void PluginViewCommandsTest::closeSettingsCompletesBeforeItsOwnPresentationRetires()
{
    Host host;
    host.runtime.importDirectory(
        package(host.temporary.path(), "export function activate(ctx) {}\n"));
    QString installation;
    QTRY_VERIFY(!(installation = host.runtime.installationForId("test.runtime")).isEmpty());
    QTRY_COMPARE(host.runtime.state(installation), PluginRuntime::Active);
    auto *scope = host.runtime.context(installation);
    QObject owner;
    owner.setProperty("active", true);
    auto *presentation = scope->createPresentation(&owner, "skin.settings");
    QVERIFY(presentation);
    EchoPluginViewContext page(*presentation, "skin.settings", &owner);
    // A zero-duration drawer closes and revokes its content in requestRaised.
    connect(host.theme.extensions(), &AppearanceCatalog::requestRaised, &owner,
            [&owner](const QString &action) {
                if (action == "closeSkinSettings")
                    owner.setProperty("active", false);
            });
    auto *operation = page.closeSkinSettings();
    const auto handle = host.engine.newQObject(operation);
    int deliveredState = -1;
    connect(operation, &EchoPluginOperation::completed, &owner,
            [operation, &deliveredState] { deliveredState = operation->state(); });
    QTRY_VERIFY(deliveredState >= 0);
    QCOMPARE(deliveredState, int(EchoPluginOperation::Succeeded));
    QVERIFY(!page.active());
}

QTEST_MAIN(PluginViewCommandsTest)
#include "PluginViewCommandsTest.moc"
