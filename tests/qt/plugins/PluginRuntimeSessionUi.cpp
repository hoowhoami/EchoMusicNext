#include "PluginRuntimeTest.h"
#include "PluginRuntimeEcosystemFixture.h"
#include "EchoPluginDialog.h"
#include "ApiInterceptors.h"
namespace
{
EchoPluginContext *sessionPlugin(Host &host)
{
    auto path = pluginPackage(host.temporary.path(), "session-ui", "export function activate(c){}",
                              {{"capabilities", QJsonArray{"kugouAuth"}}});
    write(path + "/Panel.qml", R"(import QtQuick
import EchoMusic.PluginSdk
import EchoMusic.Controls
import EchoMusic.Theme
Item {
    id: panel
    required property EchoPluginContext plugin
    required property EchoPluginDialog dialog
    objectName: "sessionUiPanel"
    implicitHeight: content.implicitHeight
    property alias draft: draftInput.text
    Column {
        id: content
        width: parent.width
        spacing: 12
        Text { text: "插件会话面板"; color: Theme.floatingText; font: Theme.font(14, Theme.regularWeight) }
        EchoTextField { id: draftInput; width: parent.width; text: "preserved" }
        EchoButton { text: "关闭"; onClicked: panel.dialog.close() }
    }
})");
    write(path + "/Broken.qml", "invalid qml");
    host.runtime.importDirectory(path);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId("session-ui")) ==
                       PluginRuntime::Active;
            },
            5000))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId("session-ui"));
}
} // namespace
void PluginRuntimeTest::authSnapshotAuthorizationCurrentSessionAndCancellation()
{
    Host host;
    auto *scope = sessionPlugin(host);
    QVERIFY(scope);
    QVERIFY(host.api.setAccount(9007199254740993ULL, "fake-token", "fake-t1"));
    QSignalSpy prompts(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested);
    auto *denied = scope->kugou()->authSnapshot();
    QCOMPARE(prompts.size(), 1);
    QVERIFY(!denied->finished());
    host.runtime.permissions()->decide(prompts.last()[0].toString(), false);
    QCOMPARE(denied->errorCode(), "PermissionDenied");
    auto *first = scope->kugou()->authSnapshot();
    host.runtime.permissions()->decide(prompts.last()[0].toString(), true);
    QCOMPARE(first->state(), EchoPluginOperation::Succeeded);
    auto snapshot = first->result().toMap();
    const auto authorization = snapshot.value("Authorization").toString();
    QVERIFY(authorization.contains("token=fake-token"));
    QVERIFY(authorization.contains("userid=9007199254740993"));
    QVERIFY(authorization.contains("t1=fake-t1"));
    QCOMPARE(authorization.split(';').size(), 9);
    QCOMPARE(authorization, host.api.pluginAuthorizationSnapshot());
    // An authorized read is queued, and a session change cancels its delivery.
    auto *stale = scope->kugou()->authSnapshot();
    QVERIFY(host.api.setAccount(42, "new-token", "new-t1"));
    QCOMPARE(stale->state(), EchoPluginOperation::Canceled);
    auto *current = scope->kugou()->authSnapshot();
    QTRY_VERIFY(current->finished());
    QVERIFY(
        current->result().toMap().value("Authorization").toString().contains("token=new-token"));
    QVERIFY(current->result().toMap().value("revision") != snapshot.value("revision"));
    auto records = host.runtime.permissions()->records();
    QVERIFY(!records.isEmpty());
    auto *revoked = scope->kugou()->authSnapshot();
    QVERIFY(host.runtime.permissions()->revoke(records.first().toMap().value("id").toString()));
    QTRY_VERIFY(revoked->finished());
    QCOMPARE(revoked->errorCode(), "PermissionDenied");
    auto *pending = scope->kugou()->authSnapshot();
    QVERIFY(!pending->finished());
    QVERIFY(host.api.setAccount(0, {}, {}));
    QCOMPARE(pending->state(), EchoPluginOperation::Canceled);
    auto *loggedOut = scope->kugou()->authSnapshot();
    host.runtime.permissions()->decide(prompts.last()[0].toString(), true);
    QVERIFY(!loggedOut->result().toMap().value("Authorization").toString().contains("token="));
    // An ordinary API capability does not grant credential access.
    auto *ordinary = ecosystem(host, "", "ordinary");
    QVERIFY(ordinary);
    QCOMPARE(ordinary->kugou()->authSnapshot()->errorCode(), "CapabilityDenied");
    // The middleware projection and explicit snapshot share the exact formatter.
    const QJsonObject identity{{"userid", qint64(42)}, {"token", "test"}, {"mid", "device"}};
    QCOMPARE(ApiInterceptChain::publicRequest({{"identity", identity}})
                 .value("headers")
                 .toObject()
                 .value("Authorization")
                 .toString(),
             ApiInterceptChain::logicalAuthorization(identity));
}
void PluginRuntimeTest::pluginClipboardWritesAndScopeCancellation()
{
    Host host;
    auto *scope = sessionPlugin(host);
    QVERIFY(scope);
    auto *clipboard = QGuiApplication::clipboard();
    auto previous = std::make_unique<QMimeData>();
    if (const auto *mime = clipboard->mimeData())
        for (const auto &format : mime->formats())
            previous->setData(format, mime->data(format));
    const auto restore = qScopeGuard([&] { clipboard->setMimeData(previous.release()); });
    auto *writeOp = scope->clipboard()->write(QString::fromUtf8("会话分享\nhttps://example.test"));
    QTRY_VERIFY(writeOp->finished());
    QCOMPARE(writeOp->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(clipboard->text(), QString::fromUtf8("会话分享\nhttps://example.test"));
    QCOMPARE(scope->clipboard()->write(QString(1024 * 1024 + 1, 'x'))->errorCode(),
             "LimitExceeded");
    auto *cancelled = scope->clipboard()->write("must not appear");
    scope->beginRetire();
    QCOMPARE(cancelled->state(), EchoPluginOperation::Canceled);
    QCoreApplication::processEvents();
    QVERIFY(clipboard->text() != "must not appear");
}
void PluginRuntimeTest::pluginMainWindowDialogsLifecycle()
{
    Host host;
    auto window = ecosystemWindow(host, R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow {
    id: testWindow
    width: 800; height: 600; visible: true
    property int backgroundClicks: 0
    MouseArea { anchors.fill: parent; onClicked: testWindow.backgroundClicks++ }
    function cover(value: bool): void { EchoPopupCoordinator.setInteractionSuspended(testWindow, value); }
})");
    auto *mainWindow = qobject_cast<QQuickWindow *>(window.get());
    QVERIFY(mainWindow);
    host.mainWindow = mainWindow;
    auto *scope = sessionPlugin(host);
    QVERIFY(scope);
    QCOMPARE(scope->ui()->showDialog({{"component", "../Panel.qml"}})->errorCode(),
             "InvalidArgument");
    QCOMPARE(scope->ui()->showDialog({{"component", "Panel.qml"}, {"modal", "yes"}})->errorCode(),
             "InvalidArgument");
    QList<EchoPluginOperation *> queued;
    for (int i = 0; i < 8; ++i)
        queued.append(scope->ui()->showDialog({{"component", "Panel.qml"}}));
    QCOMPARE(scope->ui()->showDialog({{"component", "Panel.qml"}})->errorCode(), "LimitExceeded");
    for (auto *op : queued)
        op->cancel();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto route = host.router.current();
    QCOMPARE(scope->findChildren<EchoPluginDialog *>().size(), 0);
    auto *open = scope->ui()->showDialog({{"component", "Panel.qml"}, {"width", 400}});
    QTRY_VERIFY(open->finished());
    QVERIFY2(open->state() == EchoPluginOperation::Succeeded, qPrintable(open->errorMessage()));
    auto keepOpen = host.engine.newQObject(open);
    auto *handle = open->result().value<EchoPluginDialog *>();
    QPointer<EchoPluginDialog> handleGuard(handle);
    QVERIFY(handle && handle->active());
    auto *popup = handle->findChild<QObject *>("pluginDialog");
    QVERIFY(popup);
    QCOMPARE(popup->property("modal").toBool(), true);
    QCOMPARE(popup->property("width").toInt(), 400);
    QCOMPARE(host.router.current(), route);
    auto *panel = handle->findChild<QQuickItem *>("sessionUiPanel");
    QVERIFY(panel);
    const auto captureDir = qEnvironmentVariable("ECHO_PLUGIN_UI_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        QDir().mkpath(captureDir);
        for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
            host.theme.setMode(mode);
            for (int zoom = 0; zoom < 2; ++zoom) {
                if (zoom)
                    for (int step = 0; step < 5; ++step)
                        host.shellSettings.zoomIn();
                QTest::qWait(150);
                const auto image = mainWindow->grabWindow();
                QVERIFY(!image.isNull());
                QVERIFY(
                    image.save(captureDir + QString("/dialog-%1-%2.png")
                                                .arg(mode == ThemeStore::Dark ? "dark" : "light")
                                                .arg(zoom)));
                host.shellSettings.resetZoom();
            }
        }
    }
    const auto policy = popup->property("closePolicy");
    QVERIFY(QMetaObject::invokeMethod(window.get(), "cover", Q_ARG(bool, true)));
    QTRY_VERIFY(!popup->property("enabled").toBool());
    QCOMPARE(popup->property("closePolicy").toInt(), 0);
    QVERIFY(handle->active());
    QCOMPARE(panel->property("draft").toString(), "preserved");
    QVERIFY(QMetaObject::invokeMethod(window.get(), "cover", Q_ARG(bool, false)));
    QTRY_VERIFY(popup->property("enabled").toBool());
    QCOMPARE(popup->property("closePolicy"), policy);
    QPointer<QObject> guard(popup);
    QPointer<QObject> panelGuard(panel);
    mainWindow->showMinimized();
    QTest::qWait(50);
    QVERIFY(handle->active());
    QCOMPARE(panel->property("draft").toString(), "preserved");
    mainWindow->showNormal();
    QTest::mouseClick(mainWindow, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QTRY_VERIFY(!handle->active());
    QCOMPARE(mainWindow->property("backgroundClicks").toInt(), 0);
    QTRY_VERIFY(guard.isNull() && panelGuard.isNull());
    host.engine.collectGarbage();
    QVERIFY(handleGuard); // A retained completed operation still owns a valid result.
    QVERIFY(!handleGuard->active());
    auto *bad = scope->ui()->showDialog({{"component", "Broken.qml"}});
    QTRY_VERIFY(bad->finished());
    QCOMPARE(bad->errorCode(), "ComponentError");
    auto *again = scope->ui()->showDialog(
        {{"component", "Panel.qml"}, {"modal", false}, {"closeOnMask", false}});
    QTRY_VERIFY(again->finished());
    QCOMPARE(again->state(), EchoPluginOperation::Succeeded);
    auto *second = again->result().value<EchoPluginDialog *>();
    QVERIFY(second && second->active());
    auto *secondPopup = second->findChild<QObject *>("pluginDialog");
    QVERIFY(secondPopup);
    QVERIFY(!secondPopup->property("modal").toBool());
    QTest::mouseClick(mainWindow, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QTest::qWait(50);
    QVERIFY(second->active());
    QVERIFY(mainWindow->property("backgroundClicks").toInt() > 0);
    QVERIFY(host.api.setAccount(123, "other"));
    QTRY_VERIFY(!second->active());
    auto *cancelled = scope->ui()->showDialog({{"component", "Panel.qml"}});
    cancelled->cancel();
    QCoreApplication::processEvents();
    QCOMPARE(cancelled->state(), EchoPluginOperation::Canceled);
    auto *escape = scope->ui()->showDialog({{"component", "Panel.qml"}});
    QTRY_VERIFY(escape->finished());
    auto *escapeHandle = escape->result().value<EchoPluginDialog *>();
    QVERIFY(escapeHandle);
    mainWindow->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(mainWindow));
    QTest::keyClick(mainWindow, Qt::Key_Escape);
    QTRY_VERIFY(!escapeHandle->active());
    auto *last = scope->ui()->showDialog({{"component", "Panel.qml"}});
    QTRY_VERIFY(last->finished());
    auto *lastHandle = last->result().value<EchoPluginDialog *>();
    QVERIFY(lastHandle);
    mainWindow->hide();
    QTRY_VERIFY(!lastHandle->active());
}
