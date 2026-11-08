#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoUiPartRegistry.h"
#include "EchoUiPartRegistration.h"
#include "EchoUiScope.h"
#include "EchoUiContribution.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginWindow.h"
#include "EchoPluginViewContext.h"
#include "SettingsKeys.h"
#include <QStandardItemModel>
#include <QTimer>

namespace
{
const QByteArray boundaryView = R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName: "boundaryExtension"
 implicitWidth: 48; implicitHeight: 28; color: "#d02040"
})";
QString boundaryPlugin(Host &host, const QString &id, const QByteArray &code,
                       const QJsonObject &manifest = {}, const QByteArray &view = boundaryView)
{
    const auto path = pluginPackage(host.temporary.path(), id,
                                    "export function activate(c) {" + code + "}", manifest);
    write(path + "/View.qml", view);
    host.runtime.importDirectory(path);
    const bool ready = QTest::qWaitFor(
        [&] {
            return host.runtime.state(host.runtime.installationForId(id)) == PluginRuntime::Active;
        },
        7000);
    if (!ready)
        qWarning() << "Boundary plugin did not activate:" << id;
    return host.runtime.installationForId(id);
}
std::unique_ptr<QObject> boundaryWindow(Host &host, const QByteArray &qml,
                                        const QVariantMap &properties = {})
{
    QQmlComponent component(&host.engine);
    component.setData(qml, QUrl("qrc:/boundary-window.qml"));
    auto root = std::unique_ptr<QObject>(component.createWithInitialProperties(properties));
    if (!root)
        qWarning().noquote() << component.errorString();
    return root;
}
} // namespace

void PluginRuntimeTest::compositionDecorationDoesNotLayout_data()
{
    QTest::addColumn<QString>("container");
    QTest::addColumn<QString>("position");
    QTest::addColumn<bool>("legacy");
    for (const auto &type :
         {"Row", "Column", "Grid", "Flow", "RowLayout", "ColumnLayout", "GridLayout"})
        for (const auto &position : {"background", "foreground"})
            for (bool legacy : {false, true})
                QTest::newRow(qPrintable(
                    QString("%1-%2-%3").arg(type, position, legacy ? "mount" : "contribute")))
                    << QString(type) << QString(position) << legacy;
}
void PluginRuntimeTest::compositionDecorationDoesNotLayout()
{
    QFETCH(QString, container);
    QFETCH(QString, position);
    QFETCH(bool, legacy);
    Host host;
    auto root = boundaryWindow(host, QString(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
ApplicationWindow {
 width: 600; height: 360; visible: true
 %1 { objectName: "decorationContainer"; x: 20; y: 20; width: 400; height: 200
  Rectangle { objectName: "first"; width: 70; height: 30; color: "blue" }
  Rectangle { objectName: "second"; width: 50; height: 24; color: "green" }
 }
})")
                                         .arg(container)
                                         .toUtf8());
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *target = visual(window->contentItem(), "decorationContainer");
    auto *first = visual(target, "first"), *second = visual(target, "second");
    QVERIFY(target && first && second);
    QTest::qWait(30);
    const QRectF a(first->position(), first->size()), b(second->position(), second->size());
    const QSizeF natural(target->implicitWidth(), target->implicitHeight());
    const auto body = legacy ? QString("const p=c.ui.parts.query({key:'decorationContainer'}); "
                                       "c.ui.parts.mount('layer',p,'%1','View.qml');")
                                   .arg(position)
                             : QString("c.ui.parts.contribute('layer',{key:'decorationContainer'},{"
                                       "operation:'insert',position:'%1',component:'View.qml'});")
                                   .arg(position);
    const auto installation = boundaryPlugin(host, "boundary-decoration", body.toUtf8());
    QCOMPARE(host.runtime.state(installation), PluginRuntime::Active);
    QTRY_VERIFY(visual(target, "boundaryExtension"));
    QTest::qWait(40);
    QCOMPARE(QRectF(first->position(), first->size()), a);
    QCOMPARE(QRectF(second->position(), second->size()), b);
    QCOMPARE(QSizeF(target->implicitWidth(), target->implicitHeight()), natural);
    const auto frame = window->grabWindow();
    QVERIFY(!frame.isNull());
    const auto point = target->mapToScene({360, 170}) * window->devicePixelRatio();
    const auto color = frame.pixelColor(qRound(point.x()), qRound(point.y()));
    QVERIFY2(color.red() > 150 && color.green() < 80, qPrintable(color.name()));
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(!visual(target, "boundaryExtension"));
    QCOMPARE(QRectF(first->position(), first->size()), a);
    QCOMPARE(QRectF(second->position(), second->size()), b);
}

void PluginRuntimeTest::compositionAddressesAndScopedPreferences()
{
    Host host;
    auto root = boundaryWindow(host, R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow {
 objectName: "addressWindow"; width: 600; height: 320; visible: true
 Item { x: 0; width: 260; height: 150; EchoUiScope.key: "route:left"
  EchoSoftButton { objectName: "sameAction"; x: 10; text: "Left" }
  EchoSoftButton { y: 50; text: "Unnamed A" }
  EchoSoftButton { y: 100; text: "Unnamed B" }
  EchoSoftButton { objectName:"duplicateAction"; x:100; y:50; text:"Duplicate A" }
  EchoSoftButton { objectName:"duplicateAction"; x:100; y:100; text:"Duplicate B" }
 }
 Item { x: 300; width: 260; height: 150; EchoUiScope.key: "route:right"
  EchoSoftButton { objectName: "sameAction"; x: 10; text: "Right" }
 }
})");
    QVERIFY(root);
    const auto installation = boundaryPlugin(host, "boundary-address", "");
    auto *parts = host.runtime.context(installation)->ui()->parts();
    const auto refs = parts->queryAll({{"key", "sameAction"}, {"role", "control.button"}});
    QCOMPARE(refs.size(), 2);
    auto *left = refs[0].value<EchoUiPartRef *>(), *right = refs[1].value<EchoUiPartRef *>();
    const auto keepLeft = host.engine.newQObject(left), keepRight = host.engine.newQObject(right);
    QVERIFY(left && right && left->persistent() && right->persistent());
    QVERIFY(left->address() != right->address());
    QVERIFY(left->scope() != right->scope());
    QCOMPARE(parts->queryAll({{"address", left->address()}}).size(), 1);
    QVERIFY(parts->queryUnique({{"address", left->address()}}));
    QVERIFY(!parts->queryUnique({{"key", "sameAction"}}));
    QVERIFY(parts->errorMessage().contains("2 parts"));
    QCOMPARE(parts->queryAll({{"key", "sameAction"}, {"scope", right->scope()}}).size(), 1);
    const auto duplicated = parts->queryAll({{"key", "duplicateAction"}});
    QCOMPARE(duplicated.size(), 2);
    QVERIFY(!duplicated[0].value<EchoUiPartRef *>()->persistent());
    QVERIFY(!duplicated[1].value<EchoUiPartRef *>()->persistent());
    QVERIFY(duplicated[0].value<EchoUiPartRef *>()->address() !=
            duplicated[1].value<EchoUiPartRef *>()->address());
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    registry->setPreference(left->address(), {"@host"});
    QCOMPARE(registry->preference(right->address()), QStringList());
    const auto unnamed = parts->queryAll({{"role", "control.button"}, {"key", ""}});
    QCOMPARE(unnamed.size(), 2);
    auto *a = unnamed[0].value<EchoUiPartRef *>(), *b = unnamed[1].value<EchoUiPartRef *>();
    QVERIFY(a && b && !a->persistent() && !b->persistent() && a->address() != b->address());
    registry->setPreference(a->address(), {"@host"});
    QVERIFY(registry->preference(b->address()).isEmpty());
    QVERIFY(!registry->preferences().value("order").toMap().contains(a->address()));
    const auto address = left->address();
    QVERIFY(host.account.adoptLoginAuthorization(R"({"user_id":1001,"token":"fixture-token"})"));
    QTRY_VERIFY(!left->valid());
    QTRY_VERIFY(registry->preference(address).isEmpty());
    host.account.signOut();
    QTRY_COMPARE(registry->preference(address), QStringList({"@host"}));
    QVERIFY(host.runtime.setEnabled(installation, false));
}

void PluginRuntimeTest::compositionVirtualDelegateGeneration_data()
{
    QTest::addColumn<QString>("type");
    for (const auto &type : {"ListView", "GridView", "TableView"})
        QTest::newRow(type) << QString(type);
}
void PluginRuntimeTest::compositionVirtualDelegateGeneration()
{
    QFETCH(QString, type);
    Host host;
    QStandardItemModel model;
    for (int row = 0; row < 300; ++row)
        model.appendRow(new QStandardItem(QString("resource-%1").arg(row)));
    const bool table = type == "TableView";
    const auto extras = type == "GridView"
                            ? "cellWidth:100; cellHeight:36; cacheBuffer:0; currentIndex:-1"
                        : table ? ""
                                : "cacheBuffer:0; currentIndex:-1";
    const auto indices = table ? "required property int row; required property int column"
                               : "required property int index";
    const auto jump =
        table ? "view.positionViewAtCell(Qt.point(0,n),TableView.AlignTop); view.forceLayout();"
              : "view.positionViewAtIndex(n,ListView.Beginning); view.forceLayout();";
    auto root = boundaryWindow(host,
                               QString(R"(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
 required property var dataModel
 objectName: "reuseWindow"; width: 420; height: 220; visible: true
 property int clicks: 0
 function jump(n) { %4 }
 %1 { id: view; objectName:"realVirtualView"; width: 300; height: 108; reuseItems:true; model:dataModel; %2
  delegate: Item { %3; required property string display; width:100; height:36; implicitWidth:100; implicitHeight:36
   Button { objectName:"reusedAction"; width:90; height:30; text:parent.display; onClicked:clicks++ }
  }
 }
})")
                                   .arg(type, extras, indices, jump)
                                   .toUtf8(),
                               {{"dataModel", QVariant::fromValue<QObject *>(&model)}});
    QVERIFY(root);
    QTRY_VERIFY(visual(qobject_cast<QQuickWindow *>(root.get())->contentItem(), "reusedAction"));
    const auto installation = boundaryPlugin(host, "boundary-reuse", R"(
 let old=null;
 c.registerCommand('remember', function(args){old=c.ui.parts.query({id:args.id});});
 c.registerCommand('late', function(){c.ui.setState(Object.assign({},c.ui.state,{lateValid:old.valid}));old.invoke('activate');});
 c.ui.parts.contribute('row-view',{key:'reusedAction'},{operation:'insert',position:'foreground',component:'View.qml'});
)",
                                             {}, R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 objectName:"boundaryExtension"; implicitWidth:90; implicitHeight:30; color:"transparent"
 Component.onCompleted: plugin.setTimer("row-tick",25,true,function(){const key="tick-"+part.id;const values={};values[key]=(plugin.ui.state[key]||0)+1;plugin.ui.setState(Object.assign({},plugin.ui.state,values));})
})");
    auto *scope = host.runtime.context(installation);
    auto *parts = scope->ui()->parts();
    QTRY_VERIFY(!parts->queryAll({{"key", "reusedAction"}}).isEmpty());
    auto *old = parts->query({{"key", "reusedAction"}});
    QVERIFY(old && old->valid());
    auto oldRoot = host.engine.newQObject(old);
    QPointer<QQuickItem> original = old->registration()->target();
    QTRY_VERIFY(visual(original, "boundaryExtension"));
    QPointer<QQuickItem> oldView = visual(original, "boundaryExtension");
    const auto tickKey = "tick-" + old->id();
    QTRY_VERIFY(scope->ui()->state().value(tickKey).toInt() > 0);
    QList<QPair<QPointer<QQuickItem>, QString>> identities;
    for (const auto &value : parts->queryAll({{"key", "reusedAction"}})) {
        auto *ref = value.value<EchoUiPartRef *>();
        auto *item = ref->registration()->target();
        identities.append({item, item->property("text").toString()});
    }
    auto *late = host.runtime.executeCommand(installation, "remember", {{"id", old->id()}});
    QVERIFY(late);
    QTRY_COMPARE(late->state(), EchoPluginOperation::Succeeded);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "jump", Q_ARG(QVariant, 60)));
    QTRY_VERIFY(!old->valid());
    QTRY_VERIFY(oldView.isNull());
    const auto retiredTicks = scope->ui()->state().value(tickKey).toInt();
    QTest::qWait(70);
    QCOMPARE(scope->ui()->state().value(tickKey).toInt(), retiredTicks);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "jump", Q_ARG(QVariant, 120)));
    QTRY_VERIFY(!parts->queryAll({{"key", "reusedAction"}}).isEmpty());
    // Real reuse, rather than destroying the whole view as a substitute.
    QVERIFY(original);
    QTRY_VERIFY(std::any_of(identities.cbegin(), identities.cend(), [](const auto &entry) {
        return entry.first && entry.first->property("text").toString() != entry.second;
    }));
    auto *rebound = parts->queryAll({{"key", "reusedAction"}}).first().value<EchoUiPartRef *>();
    QVERIFY(rebound && rebound->valid() && rebound->id() != old->id());
    const auto keepRebound = host.engine.newQObject(rebound);
    auto *delayed = host.runtime.executeCommand(installation, "late", {});
    QVERIFY(delayed);
    QTRY_COMPARE(delayed->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(scope->ui()->state().contains("lateValid"));
    QVERIFY(!scope->ui()->state().value("lateValid").toBool());
    QCOMPARE(root->property("clicks").toInt(), 0);
    model.sort(0, Qt::DescendingOrder);
    QVERIFY(!rebound->valid());
    QTRY_VERIFY(parts->query({{"key", "reusedAction"}}));
    auto *changed = parts->query({{"key", "reusedAction"}});
    const auto keepChanged = host.engine.newQObject(changed);
    auto *delegate = changed->registration()->target()->parentItem();
    const auto row = delegate->property(table ? "row" : "index").toInt();
    QVERIFY(model.setData(model.index(row, 0), "new-resource", Qt::DisplayRole));
    QVERIFY(!changed->valid());
    QStandardItemModel replacement;
    for (int index = 0; index < 40; ++index)
        replacement.appendRow(new QStandardItem(QString("replacement-%1").arg(index)));
    QTRY_VERIFY(parts->query({{"key", "reusedAction"}}));
    auto *beforeReplacement = parts->query({{"key", "reusedAction"}});
    const auto keepReplacement = host.engine.newQObject(beforeReplacement);
    QVERIFY(root->setProperty("dataModel", QVariant::fromValue<QObject *>(&replacement)));
    QTRY_VERIFY(!beforeReplacement->valid());
    QTRY_VERIFY(parts->query({{"key", "reusedAction"}}));
    auto *beforeReset = parts->query({{"key", "reusedAction"}});
    const auto keepReset = host.engine.newQObject(beforeReset);
    if (table) {
        QVERIFY(replacement.removeColumn(0));
        QVERIFY(!beforeReset->valid());
    } else {
        replacement.clear();
        QVERIFY(!beforeReset->valid());
    }
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_COMPARE(EchoUiPartRegistry::forEngine(host.engine)->structureWatchCount(), 0);
}

void PluginRuntimeTest::compositionAnchoredSiblingRestoresBindings_data()
{
    QTest::addColumn<QString>("axis");
    QTest::newRow("horizontal") << QString("horizontal");
    QTest::newRow("vertical") << QString("vertical");
}
void PluginRuntimeTest::compositionAnchoredSiblingRestoresBindings()
{
    QFETCH(QString, axis);
    Host host;
    auto root = boundaryWindow(host, R"(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
 width:600; height:360; visible:true; property int clicks:0
 Item { id: group; objectName:"anchoredGroup"; anchors.fill:parent
  Button { objectName:"anchoredAction"; anchors.right:parent.right; anchors.bottom:parent.bottom; anchors.margins:24; width:80; height:32; text:"Host"; onClicked:clicks++ }
 }
})");
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *button = visual(window->contentItem(), "anchoredAction");
    auto *parent = button->parentItem();
    QTest::qWait(30);
    const auto original = button->position();
    const auto installation = boundaryPlugin(host, "boundary-anchor", "");
    auto *parts = host.runtime.context(installation)->ui()->parts();
    const QVariantMap definition{{"operation", "insert"},
                                 {"position", "after"},
                                 {"axis", axis},
                                 {"gap", 4},
                                 {"component", "View.qml"},
                                 {"layout", QVariantMap{{"minimumWidth", 48},
                                                        {"preferredWidth", 48},
                                                        {"minimumHeight", 28},
                                                        {"preferredHeight", 28}}}};
    auto *contribution = parts->contribute("anchor", {{"key", "anchoredAction"}}, definition);
    QVERIFY(contribution);
    const auto keepContribution = host.engine.newQObject(contribution);
    QTRY_VERIFY(visual(window->contentItem(), "boundaryExtension"));
    QPointer<QQuickItem> extension = visual(window->contentItem(), "boundaryExtension");
    QTRY_VERIFY(!button->mapRectToScene(button->boundingRect())
                     .intersects(extension->mapRectToScene(extension->boundingRect())));
    QCOMPARE(button->parentItem(), parent);
    QCOMPARE(button->position(), original);
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    auto *ref =
        host.runtime.context(installation)->ui()->parts()->query({{"key", "anchoredAction"}});
    QVERIFY(ref && ref->insertionPositions().contains("after"));
    const auto keepRef = host.engine.newQObject(ref);
    const auto address = ref->address();
    const auto point = button->mapToScene(button->boundingRect().center()).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(root->property("clicks").toInt(), 1);
    window->resize(700, 440);
    QTRY_COMPARE(button->x(), 700. - 24 - 80);
    QTRY_COMPARE(button->y(), 440. - 24 - 32);
    QCOMPARE(ref->address(), address);
    auto decoration = boundaryView;
    decoration.replace("boundaryExtension", "boundaryDecoration");
    const auto decorationInstallation =
        boundaryPlugin(host, "boundary-anchor-decoration",
                       "c.ui.parts.contribute('decor',{key:'anchoredAction'},{operation:'insert',"
                       "position:'foreground',component:'View.qml'});",
                       {}, decoration);
    auto *decor = host.runtime.context(decorationInstallation)->findChild<EchoUiContribution *>();
    QVERIFY(decor);
    QTRY_VERIFY(visual(button, "boundaryDecoration"));
    window->resize(axis == "vertical" ? 700 : 110, axis == "vertical" ? 50 : 440);
    QTRY_COMPARE(contribution->matches().first().toMap().value("state").toString(),
                 QString("constrained"));
    QTRY_COMPARE(button->mapToScene(QPointF()), button->position());
    QCOMPARE(decor->matches().first().toMap().value("state").toString(), QString("active"));
    QTRY_VERIFY(visual(button, "boundaryDecoration"));
    window->resize(700, 440);
    QTRY_COMPARE(contribution->matches().first().toMap().value("state").toString(),
                 QString("active"));
    QTRY_VERIFY((extension = visual(window->contentItem(), "boundaryExtension")) &&
                extension->isVisible());
    auto rejected = definition;
    rejected["layout"] = QVariantMap{{"minimumWidth", 2000}, {"minimumHeight", 2000}};
    QVERIFY(contribution->update(rejected));
    QTRY_VERIFY(contribution->errorMessage().contains("restored"));
    QCOMPARE(contribution->definition(), definition);
    QTRY_COMPARE(contribution->matches().first().toMap().value("state").toString(),
                 QString("active"));
    // The other axis is bounded too; a minimum cannot grow beyond the parent.
    auto crossRejected = definition;
    auto crossLayout = definition.value("layout").toMap();
    crossLayout[axis == "vertical" ? "minimumWidth" : "minimumHeight"] = 2000;
    crossRejected["layout"] = crossLayout;
    QObject revisionObserver;
    bool sawCrossRevision = false;
    connect(contribution, &EchoUiContribution::changed, &revisionObserver, [&] {
        if (contribution->definition() == crossRejected)
            sawCrossRevision = true;
    });
    QVERIFY(contribution->update(crossRejected));
    QTRY_VERIFY(sawCrossRevision);
    QTRY_VERIFY(contribution->errorMessage().contains("restored"));
    QTRY_COMPARE(contribution->definition(), definition);
    QTRY_COMPARE(contribution->matches().first().toMap().value("state").toString(),
                 QString("active"));
    QVERIFY(host.runtime.setEnabled(decorationInstallation, false));
    QVERIFY(host.runtime.setEnabled(installation, false));
    QTRY_VERIFY(!visual(window->contentItem(), "boundaryExtension"));
    QCOMPARE(button->parentItem(), parent);
    QTRY_COMPARE(button->mapToScene(QPointF()), button->position());
    window->resize(640, 400);
    QTRY_COMPARE(button->x(), 640. - 24 - 80);
    QTRY_COMPARE(button->y(), 400. - 24 - 32);
    QTRY_COMPARE(registry->structureWatchCount(), 0);
}

void PluginRuntimeTest::compositionNativeWindowsDiscoveryAndOwnership()
{
    Host host;
    QObject::connect(host.runtime.permissions(), &PluginPermissionStore::confirmationRequested,
                     host.runtime.permissions(), [&host](const QString &id, const QVariantMap &) {
                         host.runtime.permissions()->decide(id, true);
                     });
    const auto path = pluginPackage(host.temporary.path(), "boundary-windows", R"(
export function activate(c) {
 c.ui.parts.contribute('self-check',{owner:c.id,key:'bareWindowRoot'},{operation:'hide'});
})",
                                    {{"capabilities", QJsonArray{"windows"}},
                                     {"windows", QJsonArray{QJsonObject{{"id", "first"},
                                                                        {"component", "Tools.qml"},
                                                                        {"defaultWidth", 320},
                                                                        {"defaultHeight", 180}},
                                                            QJsonObject{{"id", "second"},
                                                                        {"component", "Tools.qml"},
                                                                        {"defaultWidth", 320},
                                                                        {"defaultHeight", 180}}}}});
    write(path + "/Tools.qml", R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.PluginSdk
Rectangle {
 required property EchoPluginContext plugin
 required property EchoPluginWindow window
 objectName: "bareWindowRoot"; color:"#123456"
 RowLayout { objectName:"bareWindowRow"; width:280; height:50
  Button { objectName:"bareWindowAction"; text:"Host" }
 }
})");
    host.runtime.importDirectory(path);
    QTRY_COMPARE(host.runtime.state(host.runtime.installationForId("boundary-windows")),
                 PluginRuntime::Active);
    const auto provider = host.runtime.installationForId("boundary-windows");
    auto *providerScope = host.runtime.context(provider);
    const auto observer = boundaryPlugin(host, "boundary-window-observer", R"(
 c.ui.parts.contribute('new-action',{owner:'boundary-windows',key:'bareWindowAction'},
                      {operation:'insert',position:'after',component:'View.qml'});
 c.ui.styles.registerRule('paint',{owner:'boundary-windows',key:'bareWindowRoot'},{background:'#654321'});
)");
    auto *parts = host.runtime.context(observer)->ui()->parts();
    auto *firstOpen = providerScope->windows()->show("first");
    QTRY_COMPARE(firstOpen->state(), EchoPluginOperation::Succeeded);
    QTRY_COMPARE(parts->queryAll({{"owner", "boundary-windows"}, {"key", "bareWindowRoot"}}).size(),
                 1);
    QVERIFY(parts->queryAll({{"key", "echoWindowPixelSnap"}}).isEmpty());
    auto *first = parts->query({{"owner", "boundary-windows"}, {"key", "bareWindowRoot"}});
    QVERIFY(first && first->visible() && first->surface() == "window.first");
    const auto keepFirst = host.engine.newQObject(first);
    const auto firstAddress = first->address();
    QVERIFY(first->persistent());
    auto *firstRoot = first->registration()->target();
    QTRY_COMPARE(firstRoot->property("color").value<QColor>(), QColor("#654321"));
    QTRY_VERIFY(visual(firstRoot, "boundaryExtension"));
    auto *secondOpen = providerScope->windows()->show("second");
    QTRY_COMPARE(secondOpen->state(), EchoPluginOperation::Succeeded);
    QTRY_COMPARE(parts->queryAll({{"owner", "boundary-windows"}, {"key", "bareWindowRoot"}}).size(),
                 2);
    auto *second = parts->query(
        {{"owner", "boundary-windows"}, {"key", "bareWindowRoot"}, {"surface", "window.second"}});
    QVERIFY(second && second->persistent() && second->address() != firstAddress &&
            second->window() != first->window());
    const auto keepSecond = host.engine.newQObject(second);
    auto *registry = EchoUiPartRegistry::forEngine(host.engine);
    registry->setPreference(firstAddress, {"@host"});
    QVERIFY(registry->preference(second->address()).isEmpty());
    providerScope->windows()->get("first")->close();
    QTRY_VERIFY(!first->valid());
    QVERIFY(second->valid());
    QVERIFY(host.account.adoptLoginAuthorization(R"({"user_id":1001,"token":"fixture-token"})"));
    QTRY_VERIFY(!second->valid());
    QTRY_VERIFY(registry->preference(firstAddress).isEmpty());
    host.account.signOut();
    QTRY_COMPARE(registry->preference(firstAddress), QStringList({"@host"}));
    auto *again = providerScope->windows()->show("first");
    QTRY_COMPARE(again->state(), EchoPluginOperation::Succeeded);
    QTRY_VERIFY(parts->query(
        {{"owner", "boundary-windows"}, {"surface", "window.first"}, {"key", "bareWindowRoot"}}));
    auto *current = parts->query(
        {{"owner", "boundary-windows"}, {"surface", "window.first"}, {"key", "bareWindowRoot"}});
    QCOMPARE(current->address(), firstAddress);
    QPointer<QQuickItem> currentRoot = current->registration()->target();
    QVERIFY(host.runtime.setEnabled(observer, false));
    QTRY_VERIFY(currentRoot && currentRoot->property("color").value<QColor>() == QColor("#123456"));
    QVERIFY(host.runtime.setEnabled(provider, false));
    QTRY_VERIFY(!current->valid());
    QTRY_COMPARE(registry->structureWatchCount(), 0);

    // The production Loader entries must carry ownership even with no shared control.
    host.theme.setMotionEnabled(false);
    const auto entries = boundaryPlugin(host, "boundary-presentations", R"(
 c.ui.registerPage('tools','View.qml');
 c.ui.registerSurface('mini-tools','mini.background','View.qml');
)",
                                        {}, R"(
import QtQuick
import EchoMusic.Shell
import EchoMusic.PluginSdk
Rectangle {
 required property EchoPluginContext plugin
 property RouteContext context: null
 property string placement: ""
 objectName: placement ? "ownedSurface" : "ownedPage"
 implicitHeight: 40; color:"#234567"
})");
    auto *entryScope = host.runtime.context(entries);
    auto *navigation = entryScope->ui()->navigate("tools", {});
    QTRY_COMPARE(navigation->state(), EchoPluginOperation::Succeeded);
    auto entryRoot =
        boundaryWindow(host, R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Shell
ApplicationWindow {
 id: root; width:500; height:320; visible:true
 required property RouteController router
 required property ShellSettings settings
 required property PluginSurfaceModel surfaces
 RouteHost { width:500; height:220; router:root.router; settings:root.settings }
 PluginSurfaceHost { y:240; width:400; height:60; target:"mini.background"; model:root.surfaces }
})",
                       {{"router", QVariant::fromValue(&host.router)},
                        {"settings", QVariant::fromValue(&host.shellSettings)},
                        {"surfaces", QVariant::fromValue(host.shell.pluginSurfaces())}});
    QVERIFY(entryRoot);
    auto *entryParts = entryScope->ui()->parts();
    QTRY_VERIFY(entryParts->queryUnique(
        {{"key", "ownedPage"}, {"owner", "boundary-presentations"}, {"surface", "main"}}));
    auto *pageRef = entryParts->queryUnique(
        {{"key", "ownedPage"}, {"owner", "boundary-presentations"}, {"surface", "main"}});
    const auto keepPage = host.engine.newQObject(pageRef);
    QTRY_VERIFY(entryParts->queryUnique(
        {{"key", "ownedSurface"}, {"owner", "boundary-presentations"}, {"surface", "mini"}}));
    auto *surfaceRef = entryParts->queryUnique(
        {{"key", "ownedSurface"}, {"owner", "boundary-presentations"}, {"surface", "mini"}});
    const auto keepSurface = host.engine.newQObject(surfaceRef);
    QVERIFY(pageRef->persistent() && surfaceRef->persistent());
    const auto blank = host.temporary.path() + "/BoundaryBlank.qml";
    write(blank, "import EchoMusic.Shell\nRoutePage {}\n");
    QVERIFY(host.router.registerPage({"boundary-blank",
                                      "/boundary-blank",
                                      QUrl::fromLocalFile(blank),
                                      {},
                                      false,
                                      false,
                                      false,
                                      {}}));
    QVERIFY(host.router.navigate("boundary-blank"));
    QTRY_VERIFY(!pageRef->valid());
    QVERIFY(surfaceRef->valid());
    QVERIFY(host.runtime.setEnabled(entries, false));
    QTRY_VERIFY(!surfaceRef->valid());
    QTRY_COMPARE(registry->structureWatchCount(), 0);
}

void PluginRuntimeTest::compositionProductionAnchoredInputActions()
{
    Host host;
    auto root = boundaryWindow(host, R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow {
 width:500; height:180; visible:true; property int clears:0
 EchoTextField { objectName:"productionField"; x:24; y:24; width:400; text:"Original input"; clearable:true; onCleared:clears++ }
})");
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *field = visual(window->contentItem(), "productionField");
    auto *clear = visual(field, "clearTextAction");
    QVERIFY(field && clear);
    QVERIFY(Test::settleNativeItem(clear)); // Capture the painted presentation, including phase.
    const auto padding = field->property("rightPadding").toDouble();
    const auto base = clear->mapToScene(clear->boundingRect().center());
    const QByteArray definition = R"(
 c.ui.parts.contribute('field-tools',{key:'clearTextAction'},
   {operation:'insert',position:'before',component:'View.qml',gap:4,priority:PRIORITY,
    layout:{preferredWidth:28,preferredHeight:28}});
)";
    auto lowCode = definition;
    lowCode.replace("PRIORITY", "1");
    auto highCode = definition;
    highCode.replace("PRIORITY", "20");
    const auto low = boundaryPlugin(host, "field-tools-low", lowCode);
    auto *lowParts = host.runtime.context(low)->ui()->parts();
    QTRY_VERIFY(lowParts->query({{"key", "boundaryExtension"}, {"owner", "field-tools-low"}}));
    auto *lowRef = lowParts->query({{"key", "boundaryExtension"}, {"owner", "field-tools-low"}});
    const auto keepLow = host.engine.newQObject(lowRef);
    QTRY_VERIFY(field->property("rightPadding").toDouble() > padding);
    const auto onePadding = field->property("rightPadding").toDouble();
    const auto high = boundaryPlugin(host, "field-tools-high", highCode);
    auto *highParts = host.runtime.context(high)->ui()->parts();
    QTRY_VERIFY(highParts->query({{"key", "boundaryExtension"}, {"owner", "field-tools-high"}}));
    auto *highRef = highParts->query({{"key", "boundaryExtension"}, {"owner", "field-tools-high"}});
    const auto keepHigh = host.engine.newQObject(highRef);
    QTRY_VERIFY(field->property("rightPadding").toDouble() > onePadding);
    QTRY_VERIFY(!lowRef->geometry().intersects(highRef->geometry()));
    QTRY_VERIFY(!highRef->geometry().intersects(clear->mapRectToScene(clear->boundingRect())));
    QTRY_VERIFY(lowRef->geometry().center().x() < highRef->geometry().center().x());
    QTRY_COMPARE(clear->mapToScene(clear->boundingRect().center()), base);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, base.toPoint());
    QTRY_COMPARE(root->property("clears").toInt(), 1);
    QCOMPARE(field->property("text").toString(), QString());
    QVERIFY(host.runtime.setEnabled(high, false));
    QTRY_VERIFY(!highRef->valid());
    QTRY_VERIFY(field->property("rightPadding").toDouble() <= onePadding);
    QVERIFY(lowRef->valid());
    // The original field padding is a live size binding, not a saved constant.
    QVERIFY(field->setProperty("size", 2));
    QVERIFY(host.runtime.setEnabled(low, false));
    QTRY_COMPARE(field->property("rightPadding").toDouble(),
                 field->property("leftPadding").toDouble() + 24.);
    QTRY_COMPARE(EchoUiPartRegistry::forEngine(host.engine)->structureWatchCount(), 0);
}
