#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginUiStyles.h"
#include "EchoUiPresentation.h"
#include "PresentationScope.h"
#include "EchoUiExtensionModel.h"
#include <QStandardItemModel>
#include "SettingsCatalog.h"
#include "WindowController.h"
#include "EchoUiInspectorProbe.h"
#include "EchoUiValue.h"
#include <ctime>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif

namespace
{
std::unique_ptr<QObject> contractWindow(Host &host)
{
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.PluginSdk
ApplicationWindow {
 id: rootWindow
 property bool routeTabsToPassword: false
 readonly property Item nativeTabTarget: routeTabsToPassword ? nativePassword : nativeEditor
 width: 640; height: 440; visible: true
 property bool showControlLabel: true
 property int activations: 0
 property real hostPadding: 11
 property string hostTitle: "Native action"
 EchoSwitch { objectName: "contractSwitch"; x: 30; y: 30 }
 Button { id: contractButton; focusPolicy: Qt.StrongFocus; KeyNavigation.tab: rootWindow.nativeTabTarget; objectName: "contractButton"; x: 30; y: 90; text: hostTitle; padding: hostPadding; onClicked: activations++
  contentItem: Label { objectName: "contractNativeLabel"; text: contractButton.text; visible: showControlLabel }
 }
 Slider { objectName: "contractSlider"; x: 30; y: 150; from: 0; to: 10; value: 2 }
 TextInput { id: nativeEditor; objectName: "contractEditor"; x: 30; y: 210; width: 200; height: 30; text: "Hello" }
 TextInput { id: nativePassword; objectName: "contractPassword"; x: 30; y: 260; width: 200; height: 30; text: "secret"; echoMode: TextInput.Password }
 Rectangle { objectName: "contractClip"; x: 300; y: 100; width: 180; height: 160; clip: true
  Item { objectName: "contractAncestor"; width: 150; height: 120
   transform: Translate { id: transform; objectName: "contractTransform" }
   Rectangle { objectName: "contractTarget"; width: 100; height: 80; color: "red" }
   PresentationScope { objectName: "contractPresentation"; width: 100; height: 80 }
  }
 }
})",
                      QUrl("qrc:/contract-window.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    if (!root)
        qWarning().noquote() << component.errorString();
    return root;
}
EchoPluginContext *contractPlugin(Host &host)
{
    const auto source =
        pluginPackage(host.temporary.path(), "ui-contract", "export function activate(c) {}");
    write(source + "/Wrap.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
import QtQuick.Controls.Basic
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName: "contractNativeWrap"
 Component.onCompleted: plugin.ui.setState({built: Number(plugin.ui.state.built || 0) + 1})
 color: "transparent"
 EchoUiHostContent { anchors.fill: parent; part: parent.part }
 Button { objectName: "contractPluginFocus"; anchors.right: parent.right; width: 24; height: 24; text: "+"; activeFocusOnTab: true }
})");
    write(source + "/Replace.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle { required property EchoPluginContext plugin; required property EchoUiPartRef part; objectName: "contractNativeReplace"; color: "red" }
)");
    write(source + "/Row.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 required property var row
 required property var entry
 objectName: "modelCell-" + entry.key
 implicitWidth: 110; implicitHeight: 32; color: "#7a3545"
 Text { text: String(parent.row.values.display || parent.entry.label || ""); color: "white" }
})");
    for (const auto &position : {"Before", "After"})
        write(source + "/" + position + ".qml", QString(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.PluginSdk
EchoSoftButton { required property EchoPluginContext plugin; required property EchoUiPartRef part; objectName: "focus%1"; text: "+"; implicitWidth: 28; implicitHeight: 28; focusPolicy: Qt.StrongFocus }
)")
                                                    .arg(position)
                                                    .toUtf8());
    write(source + "/Huge.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle { required property EchoPluginContext plugin; required property EchoUiPartRef part; implicitWidth: 2000; implicitHeight: 28; color: "red" }
)");
    write(source + "/MissingRequired.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle { required property EchoPluginContext plugin; required property EchoUiPartRef part; required property string missingValue; implicitWidth: 28; implicitHeight: 28; color: "red" }
)");
    host.runtime.importDirectory(source);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId("ui-contract")) ==
                       PluginRuntime::Active;
            },
            5000))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId("ui-contract"));
}
} // namespace
void PluginRuntimeTest::nativeUiStateStylesAndCommands()
{
    Host host;
    auto root = contractWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *parts = scope->ui()->parts();
    const auto get = [&](const QString &key) { return parts->queryUnique({{"key", key}}); };
    auto *toggle = get("contractSwitch");
    QVERIFY(toggle);
    host.engine.globalObject().setProperty("pinnedToggle", host.engine.newQObject(toggle));
    QVERIFY(toggle->actions().contains("activate"));
    QVERIFY(toggle->state().contains("checked"));
    QSignalSpy changed(toggle, &EchoUiPartRef::changed);
    auto *rule = scope->ui()->styles()->registerRule(
        "state", {{"key", "contractSwitch"}, {"state", QVariantMap{{"checked", true}}}},
        {{"opacity", .4}}, 5);
    QVERIFY(rule);
    auto *nativeToggle = visual(window->contentItem(), "contractSwitch");
    QVERIFY(nativeToggle);
    QCOMPARE(nativeToggle->opacity(), 1.);
    QVERIFY(toggle->invoke("activate")->state() == EchoPluginOperation::Succeeded);
    QTRY_VERIFY(toggle->state().value("checked").toBool());
    QTRY_COMPARE(nativeToggle->opacity(), .4);
    QVERIFY(!changed.isEmpty());
    QVERIFY(toggle->invoke("activate")->state() == EchoPluginOperation::Succeeded);
    QTRY_COMPARE(nativeToggle->opacity(), 1.);
    auto *button = get("contractButton");
    QVERIFY(button);
    host.engine.globalObject().setProperty("pinnedButton", host.engine.newQObject(button));
    auto *padding = scope->ui()->styles()->registerRule(
        "padding", {{"id", button->id()}}, {{"padding", 25}, {"text", "Plugin action"}});
    QVERIFY(padding);
    auto *nativeButton = visual(window->contentItem(), "contractButton");
    QTRY_COMPARE(nativeButton->property("padding").toInt(), 25);
    QCOMPARE(nativeButton->property("text").toString(), "Plugin action");
    root->setProperty("hostPadding", 17);
    root->setProperty("hostTitle", "Updated host");
    padding->dispose();
    QTRY_COMPARE(nativeButton->property("padding").toInt(), 17);
    QCOMPARE(nativeButton->property("text").toString(), "Updated host");
    auto *slider = get("contractSlider");
    QVERIFY(slider);
    QVERIFY(slider->invoke("setValue", {{"value", 7}})->state() == EchoPluginOperation::Succeeded);
    QCOMPARE(slider->state().value("value").toDouble(), 7.);
    QVERIFY(slider->invoke("setValue", {{"value", 11}})->state() == EchoPluginOperation::Failed);
    auto *editor = get("contractEditor");
    QVERIFY(editor);
    QVERIFY(!editor->state().contains("text"));
    QVERIFY(editor->invoke("focus")->state() == EchoPluginOperation::Succeeded);
    QVERIFY(editor->invoke("selectText", {{"start", 0}, {"end", 2}})->state() ==
            EchoPluginOperation::Succeeded);
    QCOMPARE(editor->state().value("selectionEnd").toInt(), 2);
    QVERIFY(editor->invoke("insertText", {{"text", "!"}})->state() ==
            EchoPluginOperation::Succeeded);
    QCOMPARE(visual(window->contentItem(), "contractEditor")->property("text").toString(),
             "He!llo");
    auto *password = get("contractPassword");
    QVERIFY(password);
    QVERIFY(!password->actions().contains("selectText"));
    QVERIFY(!password->state().contains("text"));
    auto *ancestor = visual(window->contentItem(), "contractAncestor");
    QVERIFY(ancestor);
    auto *relative = scope->ui()->styles()->registerRule(
        "relative",
        {{"key", "contractAncestor"},
         {"parentOf",
          QVariantMap{{"key", "structuralProbe"}, {"state", QVariantMap{{"checked", true}}}}}},
        {{"opacity", .35}}, 0);
    auto *descendant = scope->ui()->styles()->registerRule(
        "descendant",
        {{"key", "structuralProbe"},
         {"ancestor",
          QVariantMap{{"key", "contractAncestor"}, {"state", QVariantMap{{"enabled", false}}}}}},
        {{"opacity", .6}}, 0);
    QVERIFY(relative && descendant);
    QQmlComponent probe(&host.engine);
    probe.setData("import EchoMusic.Controls\nEchoSwitch { objectName: 'structuralProbe' }",
                  QUrl("qrc:/native-state-dependency.qml"));
    auto *child = qobject_cast<QQuickItem *>(probe.create());
    QVERIFY2(child, qPrintable(probe.errorString()));
    child->setParent(ancestor);
    child->setParentItem(ancestor);
    QTRY_VERIFY(scope->ui()->parts()->queryUnique({{"key", "structuralProbe"}}));
    child->setProperty("checked", true);
    QTRY_COMPARE(ancestor->opacity(), .35);
    child->setProperty("checked", false);
    QTRY_COMPARE(ancestor->opacity(), 1.);
    ancestor->setEnabled(false);
    QTRY_COMPARE(child->opacity(), .6);
    ancestor->setEnabled(true);
    QTRY_COMPARE(child->opacity(), 1.);
    child->setProperty("checked", true);
    QTRY_COMPARE(ancestor->opacity(), .35);
    child->setProperty("checked", false);
    relative->dispose();
    QTRY_COMPARE(ancestor->opacity(), 1.);
    descendant->dispose();
    delete child;
    QVERIFY(host.runtime.setEnabled(host.runtime.installationForId("ui-contract"), false));
    QTRY_VERIFY(!toggle->valid());
    QCOMPARE(nativeToggle->opacity(), 1.);
}
void PluginRuntimeTest::nativeUiPresentationBoundary()
{
    Host host;
    auto root = contractWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *part = scope->ui()->parts()->queryUnique({{"key", "contractTarget"}});
    QVERIFY(part);
    host.engine.globalObject().setProperty("pinnedTarget", host.engine.newQObject(part));
    auto *presentation =
        qobject_cast<PresentationScope *>(visual(window->contentItem(), "contractPresentation"));
    auto *ancestor = visual(window->contentItem(), "contractAncestor");
    auto *transform = root->findChild<QObject *>("contractTransform");
    QVERIFY(presentation && ancestor && transform);
    QSignalSpy changes(part, &EchoUiPartRef::changed);
    QTRY_VERIFY(part->visible() && presentation->presented());
    ancestor->setOpacity(0);
    QTRY_VERIFY(!part->visible() && !presentation->presented());
    QVERIFY(!changes.isEmpty());
    ancestor->setOpacity(1);
    QTRY_VERIFY(part->visible() && presentation->presented());
    transform->setProperty("x", 300);
    QTRY_VERIFY(!part->visible() && !presentation->presented());
    transform->setProperty("x", 0);
    ancestor->setRotation(35);
    QTRY_COMPARE(part->visible(), presentation->presented());
    ancestor->setScale(0);
    QTRY_VERIFY(!part->visible() && !presentation->presented());
    ancestor->setScale(1);
    QTRY_VERIFY(part->visible() && presentation->presented());
}

void PluginRuntimeTest::nativeUiControlContentFactories()
{
    Host host;
    auto root = contractWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *parts = scope->ui()->parts();
    auto *part = parts->queryUnique({{"key", "contractButton"}});
    QVERIFY(part && part->composable());
    host.engine.globalObject().setProperty("nativeBodyPart", host.engine.newQObject(part));
    auto *button = visual(window->contentItem(), "contractButton");
    const QPointer<QQuickItem> original = button->property("contentItem").value<QQuickItem *>();
    QVERIFY(original && original->parentItem() == button);
    auto *wrap = parts->contribute("body", {{"id", part->id()}},
                                   {{"operation", "wrap"}, {"component", "Wrap.qml"}});
    QVERIFY(wrap);
    QTRY_VERIFY(visual(window->contentItem(), "contractNativeWrap"));
    QTRY_VERIFY(original && original->parentItem() != button);
    QCOMPARE(original->parent(), button);
    QVERIFY(original->isVisible());
    const auto point = button->mapToScene(button->boundingRect().center()).toPoint();
    Test::moveNativePointer(window, point);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(root->property("activations").toInt(), 1);
    root->setProperty("hostTitle", "live host title");
    QTRY_COMPARE(original->property("text").toString(), "live host title");
    QVERIFY(wrap->update({{"operation", "replace"}, {"component", "Replace.qml"}}));
    QTRY_VERIFY(visual(window->contentItem(), "contractNativeReplace"));
    QTRY_COMPARE(original->parentItem(), button);
    QTRY_VERIFY(!original->isVisible());
    root->setProperty("showControlLabel", false);
    wrap->dispose();
    QTRY_VERIFY(!visual(window->contentItem(), "contractNativeReplace"));
    QVERIFY(!original->isVisible());
    root->setProperty("showControlLabel", true);
    QTRY_VERIFY(original->isVisible());
    QCOMPARE(original->parentItem(), button);
    QCOMPARE(button->property("padding").toInt(), 11);
    auto *focusWrap = parts->contribute("focus-body", {{"id", part->id()}},
                                        {{"operation", "wrap"}, {"component", "Wrap.qml"}});
    QVERIFY(focusWrap);
    QTRY_VERIFY(visual(window->contentItem(), "contractPluginFocus"));
    button->forceActiveFocus(Qt::TabFocusReason);
    auto *pluginButton = visual(window->contentItem(), "contractPluginFocus");
    pluginButton->forceActiveFocus(Qt::TabFocusReason);
    QTRY_COMPARE(window->activeFocusItem(), pluginButton);
    focusWrap->dispose();
    QTRY_COMPARE(window->activeFocusItem(), button);
    QVERIFY(part->valid());
}

void PluginRuntimeTest::modelProjectionNativeDataAndLifecycles_data()
{
    QTest::addColumn<QString>("type");
    for (const auto &type : {"ListView", "GridView", "TableView"})
        QTest::newRow(type) << QString(type);
}
void PluginRuntimeTest::modelProjectionNativeDataAndLifecycles()
{
    QFETCH(QString, type);
    Host host;
    QSignalSpy warnings(&host.engine, &QQmlEngine::warnings);
    QStandardItemModel source;
    for (int row = 0; row < 200; ++row)
        source.appendRow({new QStandardItem(QString("native-%1").arg(row)),
                          new QStandardItem(QString("other-%1").arg(row))});
    const bool table = type == "TableView";
    const QString sizing = type == "GridView" ? "cellWidth: 120; cellHeight: 32"
                           : table            ? R"(
rowHeightProvider: row => { if (row >= nativeModel.rowCount()) throw new Error("native row provider received extension index"); return 32; }
columnWidthProvider: column => { if (column >= nativeModel.columnCount()) throw new Error("native column provider received extension index"); return 120; }
contentHeight: nativeModel.rowCount() * 32
contentWidth: nativeModel.columnCount() * 120
flickableDirection: Flickable.VerticalFlick
)"
                                              : "";
    const QString coordinate = table ? "required property int row; required property int column"
                                     : "required property int index";
    const QString text = QString(R"(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
 width: 620; height: 380; visible: true
 required property var nativeModel
 %1 {
  objectName: "modelProjectionView"; anchors.fill: parent; clip: true; reuseItems: true
  model: nativeModel
  %2
  delegate: Rectangle { required property string display; %3; implicitWidth: 120; implicitHeight: 32; width: 120; height: 32; color: "blue"; Text { text: parent.display } }
 }
})")
                             .arg(type, sizing, coordinate);
    QQmlComponent component(&host.engine);
    component.setData(text.toUtf8(), QUrl("qrc:/model-projection-window.qml"));
    auto root = std::unique_ptr<QObject>(
        component.createWithInitialProperties({{"nativeModel", QVariant::fromValue(&source)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *view = visual(window->contentItem(), "modelProjectionView");
    QVERIFY(view);
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    QVariantList entries{
        QVariantMap{{"key", "one"}, {"label", "First extension"}, {"component", "Row.qml"}},
        QVariantMap{{"key", "two"}, {"label", "Second extension"}, {"component", "Row.qml"}}};
    const QVariantMap definition{{"kind", table ? "columns" : "rows"}, {"entries", entries}};
    auto *handle = scope->ui()->parts()->contributeModel("model", {{"key", "modelProjectionView"}},
                                                         definition);
    QVERIFY(handle);
    QTRY_VERIFY(!handle->matches().isEmpty());
    QTRY_VERIFY2(
        handle->matches().first().toMap().value("state").toString() == "active",
        qPrintable(QJsonDocument::fromVariant(handle->matches()).toJson(QJsonDocument::Compact)));
    auto *projection =
        qobject_cast<EchoUiExtensionModel *>(view->property("model").value<QObject *>());
    QVERIFY(projection);
    QAbstractItemModelTester tester(projection,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(projection->rowCount(), table ? 200 : 202);
    QCOMPARE(projection->columnCount(), table ? 4 : 2);
    QCOMPARE(source.rowCount(), 200);
    QCOMPARE(source.columnCount(), 2);
    QCOMPARE(projection->data(projection->index(7, 0), Qt::DisplayRole).toString(), "native-7");
    QCOMPARE(projection->mapToSource(projection->index(7, 1)), source.index(7, 1));
    QPersistentModelIndex persistent(projection->index(7, 0));
    source.sort(0, Qt::DescendingOrder);
    QCOMPARE(persistent.data(Qt::DisplayRole).toString(), "native-7");
    source.insertRow(0, {new QStandardItem("first"), new QStandardItem("second")});
    QCOMPARE(projection->rowCount(), table ? 201 : 203);
    source.setData(source.index(0, 0), "new-native");
    QCOMPARE(projection->data(projection->index(0, 0)).toString(), "new-native");
    QVERIFY(QMetaObject::invokeMethod(view, "forceLayout"));
    qInfo() << "modelViewportBeforeEnd" << type << view->property("originY")
            << view->property("contentY") << view->property("contentHeight");
    if (!table)
        QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtEnd"));
    else
        view->setProperty(
            "contentY", view->property("originY").toDouble() +
                            qMax(0., view->property("contentHeight").toDouble() - view->height()));
    if (table)
        view->setProperty("contentX",
                          qMax(0., view->property("contentWidth").toDouble() - view->width()));
    QTRY_VERIFY(visual(window->contentItem(), "modelCell-one") ||
                visual(window->contentItem(), "modelCell-two"));
    const int soakMs = qEnvironmentVariableIntValue("ECHO_UI_MODEL_SOAK_MS");
    if (soakMs > 0) {
        // Keep a large unrelated semantic tree live while two actual plugins
        // exercise model reuse, style matching and scope retirement at 20 Hz.
        for (int i = 0; i < 4000; ++i) {
            auto *inert = new QQuickItem(window->contentItem());
            inert->setObjectName("model-load-inert-" + QString::number(i));
            inert->setPosition({-10, -10});
            inert->setSize({1, 1});
        }
        const auto second =
            pluginPackage(host.temporary.path(), "model-load-second",
                          "export function activate(c) { "
                          "c.ui.styles.registerRule('view',{key:'modelProjectionView'},{opacity:1})"
                          "; c.ui.parts.observe('view',{key:'modelProjectionView'},()=>{}); }");
        host.runtime.importDirectory(second);
        QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("model-load-second")),
                     PluginRuntime::Active);
        QTest::qWait(250);
        const auto footprint = []() -> qint64 {
#ifdef Q_OS_MACOS
            task_vm_info_data_t info{};
            mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
            if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info),
                          &count) == KERN_SUCCESS)
                return qint64(info.phys_footprint);
#endif
            return 0;
        };
        const auto initialFootprint = footprint();
        const auto initialCpu = std::clock();
        QElapsedTimer elapsed;
        elapsed.start();
        int cycles = 0, maxCells = 0;
        const auto cells = [](auto &&self, QQuickItem *item) -> int {
            int count = qobject_cast<EchoUiModelCell *>(item) ? 1 : 0;
            for (auto *child : item->childItems())
                count += self(self, child);
            return count;
        };
        while (elapsed.elapsed() < soakMs) {
            QElapsedTimer tick;
            tick.start();
            view->setProperty(
                "contentY",
                cycles % 2 ? 0.
                           : qMax(0., view->property("contentHeight").toDouble() - view->height()));
            if (cycles % 8 == 0)
                source.setData(source.index(0, 0), QString("live-%1").arg(cycles));
            QTest::qWait(qMax(1, 50 - int(tick.elapsed())));
            maxCells = qMax(maxCells, cells(cells, view));
            ++cycles;
        }
        const double cpuMs = double(std::clock() - initialCpu) * 1000. / CLOCKS_PER_SEC;
        qInfo().noquote()
            << "native_model_soak"
            << QJsonDocument(
                   QJsonObject{{"type", type},
                               {"elapsedMs", elapsed.elapsed()},
                               {"cpuMs", cpuMs},
                               {"cpuPercent", cpuMs / elapsed.elapsed() * 100.},
                               {"cycles", cycles},
                               {"cpuMsPerCycle", cpuMs / cycles},
                               {"maxCells", maxCells},
                               {"initialFootprint", initialFootprint},
                               {"finalFootprint", footprint()},
                               {"parts", EchoUiPartRegistry::forEngine(host.engine)->partCount()},
                               {"structureWatches",
                                EchoUiPartRegistry::forEngine(host.engine)->structureWatchCount()}})
                   .toJson(QJsonDocument::Compact);
        QCOMPARE(source.rowCount(), 201);
        QVERIFY(maxCells < 128);
    }
    QVariantMap bad = definition;
    bad["entries"] = QVariantList{QVariantMap{{"key", "bad"}, {"component", "missing.qml"}}};
    QVERIFY(!handle->update(bad));
    QCOMPARE(projection->rowCount(), table ? 201 : 203);
    QStandardItemModel replacement;
    replacement.appendRow({new QStandardItem("replacement"), new QStandardItem("native")});
    root->setProperty("nativeModel", QVariant::fromValue(&replacement));
    QTRY_COMPARE(projection->sourceModel(), &replacement);
    QCOMPARE(projection->data(projection->index(0, 0)).toString(), "replacement");
    QVERIFY(host.account.adoptLoginAuthorization(R"({"user_id":1901,"token":"fixture"})"));
    QTRY_VERIFY(!handle->active());
    QTRY_VERIFY(view->property("model").value<QObject *>() == &replacement);
    QTRY_VERIFY(!visual(window->contentItem(), "modelCell-one"));
    root->setProperty("nativeModel", QVariant::fromValue(&source));
    QCOMPARE(view->property("model").value<QObject *>(), &source);
    if (table) {
        QCOMPARE(view->property("contentWidth").toDouble(), source.columnCount() * 120.);
        QCOMPARE(view->property("contentHeight").toDouble(), source.rowCount() * 32.);
    }
    QVERIFY2(warnings.isEmpty(),
             "Native model projection must not emit QML layout/provider warnings");
}

void PluginRuntimeTest::nativeUiMenuModelActions()
{
    Host host;
    QSignalSpy warnings(&host.engine, &QQmlEngine::warnings);
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow {
 id: root; width: 620; height: 380; visible: true
 property int nativeTriggers: 0
 property var menu: EchoMenu {
  id: nativeMenu; objectName: "contractMenu"
  Action { objectName: "originalMenuAction"; text: "Native"; onTriggered: root.nativeTriggers++ }
  Component.onCompleted: contentItem.objectName = "contractMenuEntries"
 }
 property Item lastMenuItem: nativeMenu.itemAt(nativeMenu.count - 1)
 function openMenu() { nativeMenu.popup(80,80); }
})",
                      QUrl("qrc:/contract-menu-window.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *menu = root->property("menu").value<QObject *>();
    QVERIFY(menu);
    QPointer<QObject> original = root->findChild<QObject *>("originalMenuAction");
    QVERIFY(original);
    host.engine.globalObject().setProperty("menuTriggers", 0);
    QVERIFY(scope->registerCommand(
        "menu-selected",
        host.engine.evaluate("(function(a) { menuTriggers++; menuChecked = a.checked; })")));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "openMenu"));
    QTRY_VERIFY(menu->property("visible").toBool());
    auto *handle = scope->ui()->parts()->contributeModel(
        "menu", {{"key", "contractMenuEntries"}},
        {{"kind", "menu"},
         {"entries", QVariantList{QVariantMap{{"key", "separator"}, {"separator", true}},
                                  QVariantMap{{"key", "toggle"},
                                              {"label", "Plugin toggle"},
                                              {"command", "menu-selected"},
                                              {"checkable", true}}}}});
    QVERIFY(handle);
    QTRY_VERIFY2(!handle->matches().isEmpty() &&
                     handle->matches().first().toMap().value("state") == "active",
                 qPrintable(QJsonDocument::fromVariant(handle->matches()).toJson()));
    QTRY_COMPARE(menu->property("count").toInt(), 3);
    auto *item = root->property("lastMenuItem").value<QQuickItem *>();
    QVERIFY(item && item->window());
    QVERIFY(Test::settleNativeItem(item));
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(host.engine.globalObject().property("menuTriggers").toInt(), 1);
    QVERIFY(host.engine.globalObject().property("menuChecked").toBool());
    QCOMPARE(root->property("nativeTriggers").toInt(), 0);
    handle->dispose();
    QTRY_COMPARE(menu->property("count").toInt(), 1);
    QVERIFY(original);
    QVERIFY2(warnings.isEmpty(), "Native menu projection must not emit QML warnings");
}

void PluginRuntimeTest::nativeUiFocusOrderAndCoordinateMapping()
{
    Host host;
    auto root = contractWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *parts = scope->ui()->parts();
    auto *part = parts->queryUnique({{"key", "contractButton"}});
    QVERIFY(part);
    host.engine.globalObject().setProperty("focusPart", host.engine.newQObject(part));
    auto *source = visual(window->contentItem(), "contractButton");
    auto *before = parts->contribute(
        "before", {{"id", part->id()}},
        {{"operation", "insert"}, {"position", "before"}, {"component", "Before.qml"}});
    auto *after = parts->contribute(
        "after", {{"id", part->id()}},
        {{"operation", "insert"}, {"position", "after"}, {"component", "After.qml"}});
    QVERIFY(before && after);
    QTRY_VERIFY2(
        visual(window->contentItem(), "focusBefore") && visual(window->contentItem(), "focusAfter"),
        qPrintable(QJsonDocument::fromVariant(QVariantList{before->matches(), after->matches()})
                       .toJson(QJsonDocument::Compact)));
    auto *first = visual(window->contentItem(), "focusBefore"),
         *last = visual(window->contentItem(), "focusAfter");
    QVERIFY(Test::settleNativeItem(last));
    source->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), last);
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
    QTRY_COMPARE(window->activeFocusItem(), source);
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
    QTRY_COMPARE(window->activeFocusItem(), first);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), source);
    const auto point = part->mapPoint({{"x", 3}, {"y", 4}});
    QVERIFY(point.value("valid").toBool());
    const auto expected = source->mapToScene({3, 4});
    QCOMPARE(QPointF(point.value("x").toDouble(), point.value("y").toDouble()), expected);
    const auto local = part->mapPoint({{"x", expected.x()}, {"y", expected.y()}}, "local");
    QCOMPARE(QPointF(local.value("x").toDouble(), local.value("y").toDouble()), QPointF(3, 4));
    root->setProperty("routeTabsToPassword", true);
    before->dispose();
    after->dispose();
    QTRY_VERIFY(!visual(window->contentItem(), "focusBefore") &&
                !visual(window->contentItem(), "focusAfter"));
    source->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), visual(window->contentItem(), "contractPassword"));
    root->setProperty("routeTabsToPassword", false);
    source->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), visual(window->contentItem(), "contractEditor"));
}

void PluginRuntimeTest::nativeUiTabModelContentAndSelection_data()
{
    QTest::addColumn<QString>("tabType");
    QTest::newRow("segmented") << QString("EchoTabBar");
    QTest::newRow("detail") << QString("EchoDetailTabs");
}
void PluginRuntimeTest::nativeUiTabModelContentAndSelection()
{
    QFETCH(QString, tabType);
    const auto prefix = tabType == "EchoTabBar" ? QString("tab-") : QString("detailTab-");
    Host host;
    QSignalSpy warnings(&host.engine, &QQmlEngine::warnings);
    QQmlComponent component(&host.engine);
    component.setData(QString(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow {
 width: 620; height: 380; visible: true
 property int nativePicked: -1
 %1 { objectName: "contractTabs"; x: 20; y: 20; width: 580; labels: ["One", "Two"]; currentIndex: 1; onPicked: index => { currentIndex = index; nativePicked = index; } }
 EchoUiPartHost { objectName: "contractTabBody"; partKey: "contractTabBody"; partRole: "page.content"; x: 20; y: 90; width: 580; height: 240
  Rectangle { objectName: "originalTabBody"; anchors.fill: parent; color: "blue" }
 }
})")
                          .arg(tabType)
                          .toUtf8(),
                      QUrl("qrc:/contract-tabs-window.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *tabs = visual(window->contentItem(), "contractTabs");
    QVERIFY(tabs);
    const QVariantMap definition{
        {"kind", "tabs"},
        {"entries",
         QVariantList{QVariantMap{{"key", "extra"},
                                  {"label", "Plugin"},
                                  {"glyph", "puzzle"},
                                  {"component", "Replace.qml"},
                                  {"contentTarget", QVariantMap{{"key", "contractTabBody"}}}}}}};
    auto *handle =
        scope->ui()->parts()->contributeModel("tabs", {{"key", "contractTabs"}}, definition);
    QVERIFY(handle);
    QTRY_VERIFY(visual(tabs, prefix + "2"));
    auto *button = visual(tabs, prefix + "2");
    QVERIFY(Test::settleNativeItem(button));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      button->mapToScene(button->boundingRect().center()).toPoint());
    QTRY_VERIFY(visual(window->contentItem(), "contractNativeReplace"));
    QCOMPARE(tabs->property("currentIndex").toInt(), 1);
    QCOMPARE(root->property("nativePicked").toInt(), -1);
    QCOMPARE(tabs->property("shownIndex").toInt(), 2);
    auto *native = visual(tabs, prefix + "0");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      native->mapToScene(native->boundingRect().center()).toPoint());
    QTRY_VERIFY(!visual(window->contentItem(), "contractNativeReplace"));
    QCOMPARE(root->property("nativePicked").toInt(), 0);
    QCOMPARE(tabs->property("shownIndex").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(tabs, "selectTab", Q_ARG(int, 2)));
    QTRY_VERIFY(visual(window->contentItem(), "contractNativeReplace"));
    QTRY_VERIFY(visual(tabs, prefix + "2"));
    visual(tabs, prefix + "2")->forceActiveFocus(Qt::TabFocusReason);
    handle->dispose();
    QTRY_VERIFY(!visual(tabs, prefix + "2"));
    QCOMPARE(tabs->property("currentIndex").toInt(), 0);
    QTRY_COMPARE(window->activeFocusItem(), visual(tabs, prefix + "0"));
    QVERIFY2(warnings.isEmpty(), "Native tab projection must not emit QML warnings");
}
void PluginRuntimeTest::nativeUiSettingsModelSections()
{
    Host host;
    WindowController windows(host.settings, false);
    QSignalSpy warnings(&host.engine, &QQmlEngine::warnings);
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Settings
import EchoMusic.Shell
ApplicationWindow {
 id: root; width: 980; height: 720; visible: true
 required property ShellSettings preferences
 required property WindowController windows
 property var settingsDialog: SettingsDialog { settings: root.preferences; windowService: root.windows }
 function openSettings() { settingsDialog.open(); }
})",
                      QUrl("qrc:/contract-settings-window.qml"));
    auto root = std::unique_ptr<QObject>(component.createWithInitialProperties(
        {{"preferences", QVariant::fromValue(&host.shellSettings)},
         {"windows", QVariant::fromValue(&windows)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "openSettings"));
    auto *dialog = root->property("settingsDialog").value<QObject *>();
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto *catalog = qobject_cast<SettingsCatalog *>(dialog->property("catalog").value<QObject *>());
    QVERIFY(catalog);
    catalog->select("window");
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *handle = scope->ui()->parts()->contributeModel(
        "settings", {{"role", "model.settings"}},
        {{"kind", "settings"},
         {"entries", QVariantList{QVariantMap{{"key", "spectrum"},
                                              {"label", "Plugin section"},
                                              {"keywords", "spectrum"},
                                              {"component", "Row.qml"}}}}});
    QVERIFY(handle);
    QTRY_COMPARE(catalog->count(), 14);
    QCOMPARE(catalog->current(), "window");
    const auto key =
        catalog->data(catalog->index(catalog->count() - 1), SettingsCatalog::Key).toString();
    catalog->select(key);
    QCOMPARE(catalog->current(), key);
    QVERIFY(catalog->extensionRecipe());
    const auto diagnostic = [&] {
        auto *loader = visual(window->contentItem(), "settingsSectionLoader");
        auto *body = loader ? loader->property("item").value<QObject *>() : nullptr;
        auto *part = scope->ui()->parts()->queryUnique({{"role", "model.settings"}});
        return QString::fromUtf8(
            QJsonDocument::fromVariant(
                QVariantMap{{"current", catalog->current()},
                            {"entry", catalog->extensionEntry()},
                            {"loaderActive", loader ? loader->property("active") : QVariant()},
                            {"loaderStatus", loader ? loader->property("status") : QVariant()},
                            {"loaderHeight", loader ? loader->height() : -1},
                            {"body", body ? QString::fromLatin1(body->metaObject()->className())
                                          : QString()},
                            {"bodyActive", body ? body->property("active") : QVariant()},
                            {"bodyError", body ? body->property("errorMessage") : QVariant()},
                            {"partVisible", part && part->visible()},
                            {"matches", handle->matches()}})
                .toJson());
    };
    QTRY_VERIFY2(visual(window->contentItem(), "modelCell-spectrum"), qPrintable(diagnostic()));
    QVERIFY(catalog->source().isEmpty());
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(catalog->current(), key);
    QVERIFY(visual(window->contentItem(), "modelCell-spectrum"));
    auto *loader = visual(window->contentItem(), "settingsSectionLoader");
    QVERIFY(loader);
    QVERIFY(loader->height() > 0);
    handle->dispose();
    QTRY_COMPARE(catalog->count(), 13);
    QCOMPARE(catalog->current(), "window");
    QTRY_VERIFY(!visual(window->contentItem(), "modelCell-spectrum"));
    QVERIFY(!catalog->source().isEmpty());
    QVERIFY2(warnings.isEmpty(), "Production settings projection must not emit QML warnings");
}
void PluginRuntimeTest::nativeUiInspectorPointerPick()
{
    Host host;
    auto root = contractWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    QQmlComponent component(&host.engine);
    component.setData(R"(import EchoMusic.Controls
EchoUiInspectorProbe {})",
                      QUrl("qrc:/probe.qml"));
    auto object = std::unique_ptr<QObject>(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *probe = qobject_cast<EchoUiInspectorProbe *>(object.get());
    QVERIFY(probe);
    QSignalSpy picks(probe, &EchoUiInspectorProbe::picked);
    probe->setActive(true);
    auto *button = visual(window->contentItem(), "contractButton");
    const auto point = button->mapToScene(button->boundingRect().center()).toPoint();
    QTest::mouseMove(window, point);
    QTRY_VERIFY(!probe->selectedId().isEmpty());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(picks.size(), 1);
    QVERIFY(!probe->active());
    QCOMPARE(root->property("activations").toInt(), 0);
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    auto *picked = registry->find(probe->selectedId());
    QVERIFY(picked);
    auto *item = picked->target();
    while (item && item != button)
        item = item->parentItem();
    QCOMPARE(item, button);
    probe->setActive(true);
    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY(!probe->active());
    object.reset();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(root->property("activations").toInt(), 1);
}

void PluginRuntimeTest::modelProjectionValueForms_data()
{
    QTest::addColumn<QString>("expression");
    QTest::addColumn<int>("rows");
    QTest::newRow("numeric") << QString("40") << 40;
    QTest::newRow("strings") << QString("['One', 'Two', 'Three']") << 3;
    QTest::newRow("objects") << QString("[{label:'One'}, {label:'Two'}]") << 2;
}
void PluginRuntimeTest::modelProjectionValueForms()
{
    QFETCH(QString, expression);
    QFETCH(int, rows);
    Host host;
    QSignalSpy warnings(&host.engine, &QQmlEngine::warnings);
    QQmlComponent component(&host.engine);
    component.setData(QString(R"(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
 width: 620; height: 380; visible: true
 property var sourceValues: %1
 ListView {
  objectName: "valueView"; anchors.fill: parent; clip: true; reuseItems: true
  model: sourceValues
  delegate: Text { required property var modelData; width: 200; height: 32; text: String(modelData.label || modelData) }
 }
})")
                          .arg(expression)
                          .toUtf8(),
                      QUrl("qrc:/contract-value-model.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *view = visual(window->contentItem(), "valueView");
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *handle = scope->ui()->parts()->contributeModel(
        "values", {{"key", "valueView"}},
        {{"kind", "rows"},
         {"entries", QVariantList{QVariantMap{{"key", "one"}, {"component", "Row.qml"}}}}});
    QVERIFY(handle);
    QTRY_VERIFY2(!handle->matches().isEmpty() &&
                     handle->matches().first().toMap().value("state") == "active",
                 qPrintable(QJsonDocument::fromVariant(handle->matches()).toJson()));
    QTRY_COMPARE(view->property("count").toInt(), rows + 1);
    auto *proxy = qobject_cast<EchoUiExtensionModel *>(view->property("model").value<QObject *>());
    QVERIFY(proxy);
    const auto rowRole = proxy->roleNames().key("echoUiRow", -1);
    QVERIFY(rowRole >= 0);
    const auto row = proxy->data(proxy->index(0, 0), rowRole).toMap();
    QVERIFY2(!row.isEmpty(), "Public native model values cannot be silently dropped");
    if (expression.startsWith("[{"))
        QCOMPARE(row.value("modelData").toMap().value("label").toString(), "One");
    if (expression.startsWith("['"))
        QCOMPARE(row.value("modelData").toString(), "One");
    QVERIFY(QMetaObject::invokeMethod(view, "positionViewAtEnd"));
    QTRY_VERIFY(visual(window->contentItem(), "modelCell-one"));
    if (expression.startsWith("[{")) {
        QQmlComponent unsupported(&host.engine);
        unsupported.setData("import QtQuick.Controls.Basic\nButton { objectName: 'valueView'; "
                            "width:32; height:32 }",
                            QUrl("qrc:/unsupported-model-target.qml"));
        auto *other = qobject_cast<QQuickItem *>(unsupported.create());
        QVERIFY(other);
        other->setParent(window->contentItem());
        other->setParentItem(window->contentItem());
        QTRY_VERIFY(!handle->errorMessage().isEmpty());
        // Reject the new target without retiring the committed projection.
        QTRY_COMPARE(view->property("count").toInt(), rows + 1);
        QTRY_COMPARE(handle->matches().size(), 2);
        int active = 0, rejected = 0;
        for (const auto &value : handle->matches()) {
            const auto match = value.toMap();
            QVERIFY(!match.value("error").toString().isEmpty());
            if (match.value("state") == "active")
                ++active;
            if (match.value("state") == "error")
                ++rejected;
        }
        QCOMPARE(active, 1);
        QCOMPARE(rejected, 1);
        delete other;
        QTRY_VERIFY(handle->errorMessage().isEmpty());
        QTRY_COMPARE(view->property("count").toInt(), rows + 1);
    }
    root->setProperty("sourceValues", 4);
    QTRY_COMPARE(view->property("count").toInt(), 5);
    handle->dispose();
    QTRY_COMPARE(view->property("count").toInt(), 4);
    root->setProperty("sourceValues", 6);
    QTRY_COMPARE(view->property("count").toInt(), 6);
    QVERIFY2(warnings.isEmpty(),
             "Value-model projection must retain native delegates without warnings");
}

void PluginRuntimeTest::nativeUiBatchPreparationCommitAndRollback()
{
    Host host;
    auto root = contractWindow(host);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *button = visual(window->contentItem(), "contractButton");
    auto *slider = visual(window->contentItem(), "contractSlider");
    const auto originalPadding = button->property("padding").toDouble();
    const auto style = [](double padding) {
        return QVariantMap{{"id", "style"},
                           {"selector", QVariantMap{{"key", "contractButton"}}},
                           {"style", QVariantMap{{"padding", padding}}}};
    };
    const auto visualEntry = [](const QString &id, const QString &operation,
                                const QString &component) {
        QVariantMap definition{{"operation", operation}, {"component", component}};
        if (operation == "insert")
            definition["position"] = "after";
        return QVariantMap{{"id", id},
                           {"selector", QVariantMap{{"key", "contractButton"}}},
                           {"definition", definition}};
    };
    QVariantList entries{style(21), visualEntry("action", "insert", "After.qml"),
                         visualEntry("body", "wrap", "Wrap.qml"),
                         QVariantMap{{"id", "hide"},
                                     {"selector", QVariantMap{{"key", "contractSlider"}}},
                                     {"definition", QVariantMap{{"operation", "hide"}}}}};
    auto *batch = scope->ui()->parts()->applyBatch("group", entries);
    QVERIFY(batch);
    host.engine.globalObject().setProperty("uiBatch", host.engine.newQObject(batch));
    QVERIFY(batch->pending());
    QCOMPARE(button->property("padding").toDouble(), originalPadding);
    QVERIFY(slider->isVisible());
    QTRY_VERIFY2(!batch->pending(), qPrintable(batch->errorMessage()));
    QVERIFY2(batch->state() == "active", qPrintable(batch->errorMessage()));
    QVERIFY2(batch->errorMessage().isEmpty(), qPrintable(batch->errorMessage()));
    QCOMPARE(button->property("padding").toDouble(), 21.);
    QVERIFY(!slider->isVisible());
    QTRY_VERIFY(visual(window->contentItem(), "contractNativeWrap"));
    QTRY_VERIFY(visual(window->contentItem(), "focusAfter"));
    QPointer<QQuickItem> oldWrapper = visual(window->contentItem(), "contractNativeWrap");
    oldWrapper->setProperty("draft", "preserved");
    QTest::qWait(30);
    QCOMPARE(scope->ui()->state().value("built").toInt(), 1);
    const auto revision = batch->revision();
    QVariantList invalid{style(37), visualEntry("body", "replace", "MissingRequired.qml")};
    QVERIFY(batch->update(invalid));
    QCOMPARE(button->property("padding").toDouble(), 21.);
    QTRY_VERIFY(!batch->pending());
    QCOMPARE(batch->state(), "active");
    QVERIFY(!batch->errorMessage().isEmpty());
    QCOMPARE(batch->revision(), revision);
    QCOMPARE(host.runtime.state(host.runtime.installationForId("ui-contract")),
             PluginRuntime::Active);
    QVERIFY(oldWrapper && oldWrapper == visual(window->contentItem(), "contractNativeWrap"));
    QCOMPARE(oldWrapper->property("draft").toString(), "preserved");
    QCOMPARE(button->property("padding").toDouble(), 21.);
    QVERIFY(!slider->isVisible());
    const QPointer<QQuickItem> oldAction = visual(window->contentItem(), "focusAfter");
    QVERIFY(oldAction);
    oldAction->forceActiveFocus(Qt::TabFocusReason);
    QCOMPARE(window->activeFocusItem(), oldAction.data());
    auto huge = visualEntry("action", "insert", "Huge.qml");
    auto hugeDefinition = huge.value("definition").toMap();
    hugeDefinition["layout"] = QVariantMap{{"minimumWidth", 2000}, {"preferredWidth", 2000}};
    huge["definition"] = hugeDefinition;
    QVariantList constrained{style(37), huge, visualEntry("body", "wrap", "Wrap.qml")};
    QVERIFY(batch->update(constrained));
    QTRY_VERIFY(!batch->pending());
    QVERIFY(!batch->errorMessage().isEmpty());
    QCOMPARE(batch->revision(), revision);
    QCOMPARE(button->property("padding").toDouble(), 21.);
    QVERIFY(oldWrapper && oldWrapper == visual(window->contentItem(), "contractNativeWrap"));
    QCOMPARE(oldWrapper->property("draft").toString(), "preserved");
    QVERIFY(oldAction && oldAction == visual(window->contentItem(), "focusAfter"));
    QCOMPARE(window->activeFocusItem(), oldAction.data());
    QVariantList cancelled{style(36), visualEntry("body", "replace", "Replace.qml")};
    QVERIFY(batch->update(cancelled));
    batch->cancel();
    QVERIFY(!batch->pending());
    QCOMPARE(batch->revision(), revision);
    QVERIFY(oldWrapper && oldWrapper == visual(window->contentItem(), "contractNativeWrap"));
    QVERIFY(batch->update(cancelled));
    QTRY_VERIFY(!batch->pending());
    QVERIFY2(batch->errorMessage().isEmpty(), qPrintable(batch->errorMessage()));
    QCOMPARE(button->property("padding").toDouble(), 36.);
    QTRY_VERIFY(visual(window->contentItem(), "contractNativeReplace"));
    QTRY_VERIFY(!visual(window->contentItem(), "contractNativeWrap"));
    QVERIFY(slider->isVisible());
    root->setProperty("hostPadding", 17.);
    QCOMPARE(button->property("padding").toDouble(), 36.);
    QVERIFY(host.account.adoptLoginAuthorization(R"({"user_id":1991,"token":"fixture"})"));
    QTRY_VERIFY(!batch->active());
    QTRY_VERIFY(!visual(window->contentItem(), "contractNativeReplace"));
    QCOMPARE(button->property("padding").toDouble(), 17.);
    QVERIFY(slider->isVisible());
}

void PluginRuntimeTest::nativeUiBoundedPublicValues()
{
    Host host;
    QObject secret;
    host.engine.globalObject().setProperty("privateObject", host.engine.newQObject(&secret));
    auto value =
        host.engine.evaluate("(() => { const cycle = {}; cycle.self = cycle; return {title:'One', "
                             "nested:{number:7}, unsafe:privateObject, fn:()=>{}, cycle}; })()");
    qsizetype budget = 65536;
    auto result = EchoUiValue::json(QVariant::fromValue(value), budget).toMap();
    QCOMPARE(result.value("title").toString(), "One");
    QCOMPARE(result.value("nested").toMap().value("number").toInt(), 7);
    QVERIFY(!result.contains("unsafe"));
    QVERIFY(!result.contains("fn"));
    QVERIFY(budget >= 0);
    budget = 65536;
    QVERIFY(!EchoUiValue::json(QString(100000, 'x'), budget).isValid());
}
void PluginRuntimeTest::nativeUiAdjacentFocusAndCapacity()
{
    Host host;
    QSignalSpy warnings(&host.engine, &QQmlEngine::warnings);
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
ApplicationWindow {
 width: 900; height: 300; visible: true
 property real nativeMargin: 6
 readonly property real actualMargin: a.Layout.leftMargin
 RowLayout { x: 20; y: 30; width: 800; spacing: 8
  Button { id: a; Layout.leftMargin: nativeMargin; objectName: "groupA"; text: "A"; KeyNavigation.tab: b; activeFocusOnTab: true }
  Button { id: b; objectName: "groupB"; text: "B"; KeyNavigation.backtab: a; KeyNavigation.tab: editor; activeFocusOnTab: true }
  TextInput { id: editor; objectName: "groupEditor"; Layout.fillWidth: true; height: 32; text: "draft"; activeFocusOnTab: true }
 }
 Item { x: 20; y: 160; width: 840; height: 80
  Button { objectName: "anchorA"; x: 200; y: 10; width: 80; height: 32; text: "A" }
  Button { objectName: "anchorB"; x: 400; y: 10; width: 80; height: 32; text: "B" }
 }
})",
                      QUrl("qrc:/native-adjacent-focus.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = contractPlugin(host);
    QVERIFY(scope);
    auto *a = visual(window->contentItem(), "groupA"), *b = visual(window->contentItem(), "groupB"),
         *editor = visual(window->contentItem(), "groupEditor");
    QVERIFY(a && b && editor);
    auto *first = scope->ui()->parts()->contribute(
        "a", {{"key", "groupA"}},
        {{"operation", "insert"}, {"position", "after"}, {"component", "After.qml"}});
    auto *second = scope->ui()->parts()->contribute(
        "b", {{"key", "groupB"}},
        {{"operation", "insert"}, {"position", "before"}, {"component", "Before.qml"}});
    QVERIFY(first && second);
    QTRY_VERIFY(visual(window->contentItem(), "focusAfter"));
    QTRY_VERIFY(visual(window->contentItem(), "focusBefore"));
    auto *after = visual(window->contentItem(), "focusAfter"),
         *before = visual(window->contentItem(), "focusBefore");
    a->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), after);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), before);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), b);
    first->dispose();
    QTRY_VERIFY(!visual(window->contentItem(), "focusAfter"));
    a->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), before);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), b);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), editor);
    second->dispose();
    QTRY_VERIFY(!visual(window->contentItem(), "focusBefore"));
    a->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Tab);
    QTRY_COMPARE(window->activeFocusItem(), b);
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
    QTRY_COMPARE(window->activeFocusItem(), a);
    QCOMPARE(editor->property("text").toString(), "draft");
    auto *marginRef = scope->ui()->parts()->queryUnique({{"key", "groupA"}});
    QVERIFY(marginRef);
    QCOMPARE(marginRef->visuals().value("leftMargin").toDouble(), 6.);
    auto *rule = scope->ui()->styles()->registerRule("margins", {{"key", "groupA"}},
                                                     {{"leftMargin", 23}}, 0);
    QVERIFY(rule);
    QTRY_COMPARE(root->property("actualMargin").toDouble(), 23.);
    root->setProperty("nativeMargin", 9.);
    QCOMPARE(root->property("actualMargin").toDouble(), 23.);
    rule->dispose();
    QTRY_COMPARE(root->property("actualMargin").toDouble(), 9.);
    QCOMPARE(marginRef->visuals().value("leftMargin").toDouble(), 9.);
    auto anchored = [](const QString &position, const QString &component) {
        return QVariantMap{{"operation", "insert"},
                           {"position", position},
                           {"component", component},
                           {"layout", QVariantMap{{"minimumWidth", 200}, {"preferredWidth", 200}}}};
    };
    auto *left = scope->ui()->parts()->contribute("anchor-a", {{"key", "anchorA"}},
                                                  anchored("after", "After.qml"));
    auto *right = scope->ui()->parts()->contribute("anchor-b", {{"key", "anchorB"}},
                                                   anchored("before", "Before.qml"));
    QVERIFY(left && right);
    QTRY_VERIFY(visual(window->contentItem(), "focusAfter"));
    QTRY_VERIFY(visual(window->contentItem(), "focusBefore"));
    auto box = [&](const QString &name) {
        auto *item = visual(window->contentItem(), name);
        return item->mapRectToScene(item->boundingRect());
    };
    QTRY_VERIFY2(!box("focusAfter").intersects(box("focusBefore")),
                 "Adjacent anchor groups must partition a shared free gap");
    QCOMPARE(visual(window->contentItem(), "anchorA")->x(), 200.);
    QCOMPARE(visual(window->contentItem(), "anchorB")->x(), 400.);
    left->dispose();
    right->dispose();
    QTRY_VERIFY(!visual(window->contentItem(), "focusBefore"));
    QCOMPARE(box("anchorA").left(), 220.);
    QCOMPARE(box("anchorB").left(), 420.);

    QVERIFY2(warnings.isEmpty(), "Adjacent native focus leases must restore without warnings");
}
