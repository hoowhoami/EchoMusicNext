#include "PluginRuntimeTest.h"
#include "PluginRuntimeTestFixture.h"
#include "EchoPluginUiParts.h"
#include "EchoUiPartRef.h"
#include "EchoUiModelContribution.h"
#include "EchoUiExtensionModel.h"
#include "EchoUiModelAdapter.h"
#include "EchoUiScope.h"
#include "EchoUiPartRegistry.h"
#include "SettingsKeys.h"
#include <QItemSelectionModel>
#include <QStandardItemModel>

namespace
{
EchoPluginContext *modelPlugin(Host &host)
{
    const auto source =
        pluginPackage(host.temporary.path(), "ui-models", "export function activate(c) {}");
    write(source + "/Cell.qml", R"(
import QtQuick
import EchoMusic.PluginSdk
Rectangle {
 required property EchoPluginContext plugin
 required property EchoUiPartRef part
 required property var row
 required property var entry
 objectName: "liveCell-" + (row.values.resourceId || entry.key)
 implicitWidth: 140; implicitHeight: 42
 property string draft: "retained"
 Text { objectName: "liveCellLabel"; text: parent.row.values.display || parent.entry.label || "" }
 TextInput { objectName: "liveCellEditor"; y: 18; width: 120; height: 22; text: parent.draft }
})");
    write(source + "/Visual.qml", R"(import QtQuick
import EchoMusic.PluginSdk
Item { required property EchoPluginContext plugin; required property EchoUiPartRef part; implicitWidth:120; implicitHeight:32 }
)");
    write(source + "/Invalid.qml", R"(import QtQuick
import EchoMusic.PluginSdk
Item { required property EchoPluginContext plugin; required property EchoUiPartRef part; required property string unavailable }
)");
    host.runtime.importDirectory(source);
    if (!QTest::qWaitFor(
            [&] {
                return host.runtime.state(host.runtime.installationForId("ui-models")) ==
                       PluginRuntime::Active;
            },
            5000))
        return nullptr;
    return host.runtime.context(host.runtime.installationForId("ui-models"));
}
} // namespace

void PluginRuntimeTest::nativeUiActionsCommitBusinessIntent()
{
    Host host;
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Playback
ApplicationWindow {
 id: root
 required property PlaybackService playback
 width: 700; height: 500; visible: true
 property real businessValue: 2
 property int businessIndex: 0
 property int commits: 0
 property int picks: 0
 property string selected: "a"
 EchoSlider { objectName: "intentSlider"; x: 20; y: 20; from: 0; to: 10; value: root.businessValue; onMoved: { root.businessValue = value; root.commits++; } }
 ComboBox { objectName: "intentCombo"; x: 20; y: 90; model: ["A", "B"]; currentIndex: root.businessIndex; onActivated: index => { root.businessIndex = index; root.picks++; } }
 EchoSelect { objectName: "intentSelect"; x: 220; y: 90; options: [{label: "A", value: "a"},{label: "B", value: "b"},{label: "Disabled", value: "c", disabled: true}]; value: root.selected; onValueEdited: next => root.selected = next }
 EchoTabBar { objectName: "intentTabs"; x: 20; y: 160; width: 400; labels: ["A", "B"]; currentIndex: root.businessIndex; onPicked: index => { root.businessIndex = index; root.picks++; } }
 Item { id: volumeAnchor; x: 600; y: 150; width: 36; height: 36 }
 PlaybackOptionsPopup { id: volume; objectName: "intentVolumePopup"; playback: root.playback; parent: root.contentItem; anchorItem: volumeAnchor; kind: "volume" }
 function showVolume(): void { volume.open(); }
})",
                      QUrl("qrc:/ui-intents.qml"));
    auto root = std::unique_ptr<QObject>(
        component.createWithInitialProperties({{"playback", QVariant::fromValue(&host.playback)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = modelPlugin(host);
    QVERIFY(scope);
    const auto part = [&](const QString &key) {
        return scope->ui()->parts()->queryUnique({{"key", key}});
    };
    auto *slider = part("intentSlider");
    QVERIFY(slider);
    QCOMPARE(slider->invoke("setValue", {{"value", 7}})->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(root->property("businessValue").toDouble(), 7.);
    QCOMPARE(root->property("commits").toInt(), 1);
    root->setProperty("businessValue", 4.);
    QTRY_COMPARE(slider->state().value("value").toDouble(), 4.);
    QCOMPARE(slider->invoke("setValue", {{"value", 11}})->state(), EchoPluginOperation::Failed);
    auto *combo = part("intentCombo");
    QVERIFY(combo);
    QCOMPARE(combo->invoke("selectIndex", {{"index", 1}})->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(root->property("businessIndex").toInt(), 1);
    QCOMPARE(root->property("picks").toInt(), 1);
    auto *select = part("intentSelect");
    QVERIFY(select);
    QCOMPARE(select->invoke("selectIndex", {{"index", 1}})->state(),
             EchoPluginOperation::Succeeded);
    QCOMPARE(root->property("selected").toString(), "b");
    QCOMPARE(select->invoke("selectIndex", {{"index", 2}})->state(), EchoPluginOperation::Failed);
    auto *tabs = part("intentTabs");
    QVERIFY(tabs);
    QCOMPARE(tabs->invoke("selectIndex", {{"index", 0}})->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(root->property("businessIndex").toInt(), 0);
    QCOMPARE(root->property("picks").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "showVolume"));
    QTRY_VERIFY(part("playbackVolumeSlider"));
    auto *volume = part("playbackVolumeSlider");
    QTRY_VERIFY(volume->visible());
    QCOMPARE(volume->invoke("setValue", {{"value", 37}})->state(), EchoPluginOperation::Succeeded);
    QCOMPARE(host.playback.volume(), 37.);
    QCOMPARE(host.settings.value(SettingsKeys::Playback::Volume).toDouble(), 37.);
    auto *nativeSlider = visual(window->contentItem(), "intentSlider");
    nativeSlider->setEnabled(false);
    QCOMPARE(slider->invoke("setValue", {{"value", 5}})->state(), EchoPluginOperation::Failed);
}

void PluginRuntimeTest::nativeUiModelSelectionAndLiveCells()
{
    Host host;
    QStandardItemModel source;
    source.setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "resourceId"}});
    for (int row = 0; row < 3; ++row) {
        auto *item = new QStandardItem(QString("row-%1").arg(row));
        item->setData(QString("r%1").arg(row), Qt::UserRole + 1);
        source.appendRow(item);
    }
    QItemSelectionModel selection(&source);
    selection.select(source.index(1, 0), QItemSelectionModel::Select);
    selection.setCurrentIndex(source.index(1, 0), QItemSelectionModel::NoUpdate);
    QQmlComponent component(&host.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow {
 required property var nativeModel
 required property var nativeSelection
 width: 700; height: 400; visible: true
 TableView { objectName: "liveTable"; anchors.fill: parent; model: nativeModel; selectionModel: nativeSelection; clip: true; reuseItems: true
  delegate: Rectangle { required property string display; implicitWidth: 120; implicitHeight: 42; Text { text: parent.display } }
 }
})",
                      QUrl("qrc:/ui-selection.qml"));
    auto root = std::unique_ptr<QObject>(component.createWithInitialProperties(
        {{"nativeModel", QVariant::fromValue(&source)},
         {"nativeSelection", QVariant::fromValue(&selection)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = modelPlugin(host);
    QVERIFY(scope);
    auto *handle = scope->ui()->parts()->contributeModel(
        "columns", {{"key", "liveTable"}},
        {{"kind", "columns"},
         {"entries", QVariantList{QVariantMap{{"key", "note"}, {"component", "Cell.qml"}}}}});
    QVERIFY(handle);
    QTRY_VERIFY(visual(window->contentItem(), "liveCell-r1"));
    auto *view = visual(window->contentItem(), "liveTable");
    auto *projection =
        qobject_cast<EchoUiExtensionModel *>(view->property("model").value<QObject *>());
    QVERIFY(projection);
    auto *bridge = view->property("selectionModel").value<QItemSelectionModel *>();
    QVERIFY(bridge && bridge != &selection);
    QCOMPARE(selection.model(), &source);
    QCOMPARE(selection.selectedIndexes(), QModelIndexList{source.index(1, 0)});
    QCOMPARE(bridge->currentIndex(), projection->mapFromSource(source.index(1, 0)));
    bridge->select(projection->mapFromSource(source.index(2, 0)),
                   QItemSelectionModel::ClearAndSelect);
    bridge->setCurrentIndex(projection->mapFromSource(source.index(2, 0)),
                            QItemSelectionModel::NoUpdate);
    QCOMPARE(selection.selectedIndexes(), QModelIndexList{source.index(2, 0)});
    QPointer<QQuickItem> cell = visual(window->contentItem(), "liveCell-r1");
    cell->setProperty("draft", "keep-me");
    auto *editor = visual(cell, "liveCellEditor");
    editor->forceActiveFocus();
    source.setData(source.index(1, 0), "refreshed");
    QTRY_VERIFY(cell && visual(cell, "liveCellLabel")->property("text") == "refreshed");
    QCOMPARE(visual(window->contentItem(), "liveCell-r1"), cell.data());
    QCOMPARE(cell->property("draft").toString(), "keep-me");
    QCOMPARE(window->activeFocusItem(), editor);
    QVERIFY(handle->update(
        {{"kind", "columns"},
         {"entries", QVariantList{QVariantMap{
                         {"key", "note"}, {"label", "updated"}, {"component", "Cell.qml"}}}}}));
    QTRY_COMPARE(projection->headerData(source.columnCount(), Qt::Horizontal).toString(),
                 "updated");
    QTRY_COMPARE(selection.selectedIndexes(), QModelIndexList{source.index(2, 0)});
    QCOMPARE(selection.model(), &source);
    handle->dispose();
    QTRY_COMPARE(view->property("model").value<QObject *>(), &source);
    QCOMPARE(view->property("selectionModel").value<QItemSelectionModel *>(), &selection);
    QCOMPARE(selection.currentIndex(), source.index(2, 0));
    QCOMPARE(selection.selectedIndexes(), QModelIndexList{source.index(2, 0)});
}

void PluginRuntimeTest::nativeUiModelRelativeOrderAndRejectedRevision()
{
    Host host;
    QStandardItemModel source;
    source.setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "resourceId"}});
    for (int i = 0; i < 5; ++i) {
        auto *item = new QStandardItem(QString::number(i));
        item->setData("r" + QString::number(i), Qt::UserRole + 1);
        source.appendRow(item);
    }
    EchoUiExtensionModel projection;
    projection.setSourceModel(&source);
    QAbstractItemModelTester tester(&projection,
                                    QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto entry = [](QString key, QVariantMap values) {
        values["key"] = key;
        return EchoUiExtensionModel::Entry{key, "p", "c", key, values, true};
    };
    QString error;
    QVERIFY(projection.setEntries(
        {entry("front", {{"position", "before"}, {"anchor", "native:r1"}}),
         entry("hidden", {{"operation", "hide"}, {"target", "native:r2"}}),
         entry("moved", {{"operation", "move"}, {"target", "native:r4"}, {"position", "prepend"}})},
        {}, &error));
    QCOMPARE(projection.rowCount(), 5);
    QCOMPARE(projection.mapToSource(projection.index(0, 0)), source.index(4, 0));
    QCOMPARE(projection.mapToSource(projection.index(1, 0)), source.index(0, 0));
    QVERIFY(!projection.mapToSource(projection.index(2, 0)).isValid());
    QCOMPARE(projection.mapFromSource(source.index(1, 0)).row(), 3);
    QVERIFY(!projection.mapFromSource(source.index(2, 0)).isValid());
    source.insertRow(0, new QStandardItem("inserted"));
    QCOMPARE(projection.mapFromSource(source.index(2, 0)).row(), 4);
    QVERIFY(!projection.setEntries(
        {entry("bad", {{"position", "before"}, {"anchor", "native:absent"}})}, {}, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(projection.rowCount(), 6);

    QVERIFY(projection.setEntries(
        {entry("replacement",
               {{"operation", "replace"}, {"target", "native:r1"}, {"label", "Replacement"}})},
        {}, &error));
    const auto replaced = projection.mapFromSource(source.index(2, 0));
    QVERIFY(replaced.isValid());
    QCOMPARE(projection.mapToSource(replaced), source.index(2, 0));
    QCOMPARE(replaced.data().toString(), "Replacement");
    QCOMPARE(replaced.data(Qt::UserRole + 1).toString(), "r1");
    QVERIFY(
        projection.setEntries({entry("low-hide", {{"operation", "hide"}, {"target", "native:r2"}}),
                               entry("high-hide", {{"operation", "hide"}, {"target", "native:#3"}}),
                               entry("adjacent", {{"position", "after"}, {"anchor", "native:r2"}})},
                              {}, &error));
    QCOMPARE(projection.rowCount(), 6);
    QVERIFY(!projection.mapFromSource(source.index(3, 0)).isValid());

    {
        QStandardItemModel sortable;
        sortable.appendRow({new QStandardItem("2"), new QStandardItem("1")});
        sortable.appendRow({new QStandardItem("1"), new QStandardItem("2")});
        EchoUiExtensionModel sorted;
        sorted.setSourceModel(&sortable);
        QVERIFY(sorted.setEntries({}, {entry("first", {{"position", "prepend"}})}, &error));
        sorted.sort(2, Qt::DescendingOrder);
        QCOMPARE(sortable.index(0, 0).data().toString(), "1");
        QVERIFY(!sorted.mapToSource(sorted.index(0, 0)).isValid());
    }

    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
ApplicationWindow { required property var source; width: 700; height: 400; visible: true
 TableView { objectName: "revisionTable"; anchors.fill: parent; model: source; columnWidthProvider: function(column) { return 120; }; contentWidth: source.columnCount()*120; delegate: Rectangle { implicitWidth: 130; implicitHeight: 42 } }
})",
                      QUrl("qrc:/revision.qml"));
    auto root = std::unique_ptr<QObject>(
        component.createWithInitialProperties({{"source", QVariant::fromValue(&source)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    // A native item's base destructor emits window changes. No presentation
    // callback may publish state after the derived C++ lifetime has ended.
    for (int i = 0; i < 32; ++i) {
        auto *retiring = new EchoUiModelCell(window->contentItem());
        retiring->setActive(true);
        QSignalSpy lateState(retiring, &EchoUiContentHost::activeChanged);
        delete retiring;
        QCOMPARE(lateState.size(), 0);
    }
    auto *scope = modelPlugin(host);
    QVERIFY(scope);
    const QVariantMap definition{
        {"kind", "columns"},
        {"entries", QVariantList{QVariantMap{
                        {"key", "note"}, {"component", "Cell.qml"}, {"position", "prepend"}}}}};
    auto *handle =
        scope->ui()->parts()->contributeModel("revision", {{"key", "revisionTable"}}, definition);
    QVERIFY(handle);
    QTRY_VERIFY(visual(window->contentItem(), "liveCell-r1"));
    QPointer<QQuickItem> cell = visual(window->contentItem(), "liveCell-r1");
    auto *editor = visual(cell, "liveCellEditor");
    editor->forceActiveFocus();
    const auto revision = handle->revision();
    auto bad = definition;
    auto entries = bad["entries"].toList();
    auto changed = entries[0].toMap();
    changed["component"] = "Invalid.qml";
    entries[0] = changed;
    bad["entries"] = entries;
    QVERIFY(handle->update(bad));
    QTRY_VERIFY(!handle->pending() && !handle->errorMessage().isEmpty());
    QCOMPARE(handle->revision(), revision);
    QCOMPARE(handle->definition(), definition);
    QCOMPARE(visual(window->contentItem(), "liveCell-r1"), cell.data());
    QVERIFY(editor->hasActiveFocus());
    auto replacement = definition;
    auto replacementEntries = replacement["entries"].toList();
    auto replacementEntry = replacementEntries[0].toMap();
    replacementEntry.remove("position");
    replacementEntry["operation"] = "replace";
    replacementEntry["target"] = "native:#0";
    replacementEntry["width"] = 123;
    replacementEntries[0] = replacementEntry;
    replacement["entries"] = replacementEntries;
    QVERIFY(handle->update(replacement));
    QTRY_VERIFY2(!handle->pending() && handle->errorMessage().isEmpty(),
                 qPrintable(handle->errorMessage()));
    auto *table = visual(window->contentItem(), "revisionTable");
    QTRY_COMPARE(table->property("contentWidth").toDouble(), 123.);
    auto *projected =
        qobject_cast<EchoUiExtensionModel *>(table->property("model").value<QObject *>());
    QVERIFY(projected);
    QCOMPARE(projected->mapToSource(projected->index(2, 0)), source.index(2, 0));
    handle->dispose();
    QTRY_VERIFY(!cell);
}

void PluginRuntimeTest::nativeUiCompositeColumnsVirtualizationAndRecovery()
{
    Host host;
    QStandardItemModel source;
    QHash<int, QByteArray> roles;
    const QList<QByteArray> names{"title",        "artistText", "albumText",     "coverUrl",
                                  "durationText", "extraText",  "resourceId",    "artistLinks",
                                  "albumId",      "original",   "privilegeTags", "qualityLabel"};
    for (int i = 0; i < names.size(); ++i)
        roles[Qt::UserRole + 1 + i] = names[i];
    source.setItemRoleNames(roles);
    for (int row = 0; row < 2000; ++row) {
        auto *item = new QStandardItem;
        for (int i = 0; i < names.size(); ++i) {
            QVariant value = QString();
            if (names[i] == "resourceId")
                value = "song-" + QString::number(row);
            else if (names[i] == "title")
                value = "Song " + QString::number(row);
            else if (names[i] == "artistLinks" || names[i] == "privilegeTags")
                value = QVariantList();
            else if (names[i] == "original")
                value = false;
            item->setData(value, Qt::UserRole + 1 + i);
        }
        source.appendRow(item);
    }
    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Music
ApplicationWindow { id: root; required property var source; width: 900; height: 420; visible: true
 SongListHeader { objectName: "columnsHeader"; width: parent.width; geometry: songs.geometry }
 SongList { id: songs; objectName: "productionColumns"; y:44; width:parent.width; height:parent.height-44; model:root.source }
})",
                      QUrl("qrc:/composite-columns.qml"));
    auto root = std::unique_ptr<QObject>(
        component.createWithInitialProperties({{"source", QVariant::fromValue(&source)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto secondRoot = std::unique_ptr<QObject>(
        component.createWithInitialProperties({{"source", QVariant::fromValue(&source)}}));
    QVERIFY2(secondRoot, qPrintable(component.errorString()));
    auto *secondWindow = qobject_cast<QQuickWindow *>(secondRoot.get());
    QVERIFY(Test::prepareNativeInput(secondWindow));
    auto *scope = modelPlugin(host);
    QVERIFY(scope);
    host.engine.globalObject().setProperty("sortInvocations", 0);
    QVERIFY(scope->registerCommand("sort-notes",
                                   host.engine.evaluate("(function(a) { sortInvocations++; })")));
    const QVariantMap definition{{"kind", "columns"},
                                 {"entries", QVariantList{QVariantMap{{"key", "note"},
                                                                      {"label", "Note"},
                                                                      {"component", "Cell.qml"},
                                                                      {"width", 123},
                                                                      {"position", "before"},
                                                                      {"anchor", "native:album"},
                                                                      {"command", "sort-notes"}}}}};
    auto *parts = scope->ui()->parts();
    QVERIFY2(parts->preflight({{"key", "productionColumns"}}, definition)["valid"].toBool(),
             qPrintable(QJsonDocument::fromVariant(
                            parts->preflight({{"key", "productionColumns"}}, definition))
                            .toJson()));
    auto *handle = parts->contributeModel("composite", {{"key", "productionColumns"}}, definition);
    QVERIFY(handle);
    QTRY_VERIFY2(visual(window->contentItem(), "liveCell-song-0"),
                 qPrintable(handle->errorMessage()));
    QTRY_VERIFY(visual(secondWindow->contentItem(), "liveCell-song-0"));
    QTRY_COMPARE(handle->matches().size(), 2);
    QTRY_VERIFY(visual(window->contentItem(), "pluginColumnHeader-note"));
    auto *header = visual(window->contentItem(), "pluginColumnHeader-note");
    auto *cell = visual(window->contentItem(), "liveCell-song-0");
    QTRY_COMPARE(cell->width(), 123.);
    QTRY_COMPARE(header->width(), 123.);
    QVERIFY(QMetaObject::invokeMethod(header, "click"));
    QTRY_COMPARE(host.engine.globalObject().property("sortInvocations").toInt(), 1);
    auto *view = visual(window->contentItem(), "productionColumns");
    QCOMPARE(view->property("model").value<QObject *>(), &source);
    const auto cells = [&] {
        return window->contentItem()->findChildren<EchoUiModelCell *>().size();
    };
    QVERIFY(cells() < 100);
    QPointer<QQuickItem> first = cell;
    QMetaObject::invokeMethod(view, "positionViewAtIndex", Q_ARG(int, 1900), Q_ARG(int, 0));
    QTRY_VERIFY(visual(window->contentItem(), "liveCell-song-1900"));
    QVERIFY(cells() < 100);
    window->hide();
    QTRY_VERIFY(!visual(window->contentItem(), "liveCell-song-1900"));
    QVERIFY(visual(secondWindow->contentItem(), "liveCell-song-0"));
    window->show();
    QTRY_VERIFY(visual(window->contentItem(), "liveCell-song-1900"));
    handle->dispose();
    QTRY_VERIFY(!visual(secondWindow->contentItem(), "pluginColumnHeader-note"));
    QTRY_VERIFY(!visual(window->contentItem(), "pluginColumnHeader-note"));
    QTRY_COMPARE(cells(), 0);
    QCOMPARE(view->property("model").value<QObject *>(), &source);
}

void PluginRuntimeTest::nativeUiModelContextProvidersAndStaleCommands()
{
    Host host;
    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow { id: root; width:600; height:400; visible:true
 property string resource: "first"
 property var menu: EchoMenu { id: contextual; modelContext: ({resourceId:root.resource}); Action { text:"Native" }
  Component.onCompleted: contentItem.objectName="contextualEntries"
 }
 function showMenu(): void { contextual.popup(30,30); }
 function closeMenu(): void { contextual.close(); }
})",
                      QUrl("qrc:/contextual-menu.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = modelPlugin(host);
    QVERIFY(scope);
    host.engine.globalObject().setProperty("contextCalls", 0);
    QVERIFY(scope->registerCommand(
        "context-command",
        host.engine.evaluate(
            "(function(a) { contextCalls++; contextResource=a.context.values.resourceId; })")));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "showMenu"));
    auto provider = host.engine.evaluate("(function(context) { providerContext=context; return new "
                                         "Promise(resolve => { resolveContext=resolve; }); })");
    auto *handle = scope->ui()->parts()->contributeModel(
        "context", {{"key", "contextualEntries"}}, {{"kind", "menu"}, {"entries", QVariantList()}},
        provider);
    QVERIFY(handle);
    host.engine.globalObject().setProperty("contextModelHandle", host.engine.newQObject(handle));
    QTRY_VERIFY(host.engine.globalObject().property("resolveContext").isCallable());
    auto stale = host.engine.globalObject().property("resolveContext");
    auto *menu = root->property("menu").value<QObject *>();
    QVERIFY(QMetaObject::invokeMethod(root.get(), "closeMenu"));
    QTRY_VERIFY(!menu->property("visible").toBool());
    stale.call({host.engine.toScriptValue(QVariantList{
        QVariantMap{{"key", "old"}, {"label", "Old"}, {"command", "context-command"}}})});
    QTest::qWait(30);
    QCOMPARE(menu->property("count").toInt(), 1);
    root->setProperty("resource", "second");
    QVERIFY(QMetaObject::invokeMethod(root.get(), "showMenu"));
    QTRY_COMPARE(host.engine.globalObject()
                     .property("providerContext")
                     .property("values")
                     .property("resourceId")
                     .toString(),
                 "second");
    auto resolve = host.engine.globalObject().property("resolveContext");
    resolve.call({host.engine.toScriptValue(QVariantList{
        QVariantMap{{"key", "new"}, {"label", "Second"}, {"command", "context-command"}}})});
    QTRY_VERIFY2(
        menu->property("count").toInt() == 2,
        qPrintable(QJsonDocument::fromVariant(QVariantMap{{"error", handle->errorMessage()},
                                                          {"matches", handle->matches()}})
                       .toJson()));
    auto *view = visual(window->contentItem(), "contextualEntries");
    auto *adapter = EchoUiModelAdapter::forView(view, host.engine, false);
    QVERIFY(adapter);
    const auto entry = adapter->entries().last().toMap();
    QVERIFY(adapter->invokeEntry(entry["token"].toString(), entry["key"].toString(), false));
    QTRY_COMPARE(host.engine.globalObject().property("contextCalls").toInt(), 1);
    QCOMPARE(host.engine.globalObject().property("contextResource").toString(), "second");
    QVERIFY(adapter->invokeEntry(entry["token"].toString(), entry["key"].toString(), false));
    root->setProperty("resource", "third"); // cancel before the queued command callback runs
    QTest::qWait(30);
    QCOMPARE(host.engine.globalObject().property("contextCalls").toInt(), 1);
    QTRY_COMPARE(host.engine.globalObject()
                     .property("providerContext")
                     .property("values")
                     .property("resourceId")
                     .toString(),
                 "third");
    host.engine.globalObject()
        .property("resolveContext")
        .call({host.engine.toScriptValue(QVariantList{QVariantMap{
            {"key", "tools"},
            {"label", "Tools"},
            {"position", "before"},
            {"anchor", "native:#0"},
            {"group", "tools"},
            {"children",
             QVariantList{QVariantMap{
                 {"key", "run"}, {"label", "Run"}, {"command", "context-command"}}}}}})});
    QTRY_COMPARE(menu->property("count").toInt(), 2);
    auto menuObject = host.engine.newQObject(menu);
    auto *submenuItem = qobject_cast<QQuickItem *>(
        menuObject.property("itemAt").callWithInstance(menuObject, {0}).toQObject());
    QVERIFY(submenuItem);
    QVERIFY(Test::settleNativeItem(submenuItem));
    QTest::mouseClick(submenuItem->window(), Qt::LeftButton, Qt::NoModifier,
                      submenuItem->mapToScene(submenuItem->boundingRect().center()).toPoint());
    QPointer<QObject> submenu = submenuItem->property("subMenu").value<QObject *>();
    QPointer<QQuickItem> submenuEntry = submenuItem;
    QVERIFY(submenu);
    QTRY_VERIFY(submenu->property("visible").toBool());
    auto submenuObject = host.engine.newQObject(submenu);
    auto *leaf = qobject_cast<QQuickItem *>(
        submenuObject.property("itemAt").callWithInstance(submenuObject, {0}).toQObject());
    QVERIFY(leaf);
    QVERIFY(Test::settleNativeItem(leaf));
    QTest::mouseClick(leaf->window(), Qt::LeftButton, Qt::NoModifier,
                      leaf->mapToScene(leaf->boundingRect().center()).toPoint());
    QTRY_COMPARE(host.engine.globalObject().property("contextCalls").toInt(), 2);
    QCOMPARE(host.engine.globalObject().property("contextResource").toString(), "third");
    // The native submenu graph is owned by this live projection, not retained
    // by the provider or command. A normal action dismisses and destroys it.
    QTRY_VERIFY(submenu.isNull());
    QTRY_VERIFY(submenuEntry.isNull());
    root->setProperty("resource", "fourth");
    QVERIFY(QMetaObject::invokeMethod(root.get(), "showMenu"));
    QTRY_COMPARE(host.engine.globalObject()
                     .property("providerContext")
                     .property("values")
                     .property("resourceId")
                     .toString(),
                 "fourth");
    auto pendingResolver = host.engine.globalObject().property("resolveContext");
    QVERIFY(host.account.adoptLoginAuthorization(R"({"user_id":2345,"token":"fixture"})"));
    QTRY_VERIFY(!handle->active());
    pendingResolver.call({host.engine.toScriptValue(QVariantList{
        QVariantMap{{"key", "retired"}, {"label", "Retired"}, {"command", "context-command"}}})});
    QTRY_COMPARE(menu->property("count").toInt(), 1);
    QCOMPARE(host.engine.globalObject().property("contextCalls").toInt(), 2);
}

void PluginRuntimeTest::nativeUiMixedModelBatchAndPreflight()
{
    Host host;
    QQmlComponent component(&host.engine);
    component.setData(R"(import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
ApplicationWindow { width:700; height:400; visible:true
 EchoTabBar { objectName:"batchTabs"; width:400; labels:["A","B"]; currentIndex:0 }
 Item { objectName:"batchBody"; x:20; y:60; width:500; height:240 }
})",
                      QUrl("qrc:/mixed-model-batch.qml"));
    auto root = std::unique_ptr<QObject>(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(Test::prepareNativeInput(window));
    auto *scope = modelPlugin(host);
    QVERIFY(scope);
    auto *parts = scope->ui()->parts();
    const QVariantMap model{
        {"kind", "tabs"},
        {"entries", QVariantList{QVariantMap{{"key", "tab"},
                                             {"label", "Inserted"},
                                             {"component", "Visual.qml"},
                                             {"contentTarget", QVariantMap{{"key", "batchBody"}}},
                                             {"position", "before"},
                                             {"anchor", "native:#1"}}}}};
    const QVariantList entries{
        QVariantMap{
            {"id", "tabs"}, {"selector", QVariantMap{{"key", "batchTabs"}}}, {"model", model}},
        QVariantMap{{"id", "body"},
                    {"selector", QVariantMap{{"key", "batchBody"}}},
                    {"definition", QVariantMap{{"operation", "insert"},
                                               {"position", "foreground"},
                                               {"component", "Visual.qml"}}}}};
    QVERIFY2(parts->preflightBatch(entries)["valid"].toBool(),
             qPrintable(QJsonDocument::fromVariant(parts->preflightBatch(entries)).toJson()));
    auto *batch = parts->applyBatch("mixed", entries);
    QVERIFY(batch);
    QTRY_COMPARE(batch->state(), "active");
    auto *tabs = visual(window->contentItem(), "batchTabs");
    QCOMPARE(tabs->property("shownLabels").toStringList(), QStringList({"A", "Inserted", "B"}));
    auto updated = entries;
    auto updatedEntry = updated[0].toMap();
    auto updatedModel = model;
    auto updatedCells = updatedModel["entries"].toList();
    auto updatedCell = updatedCells[0].toMap();
    updatedCell["label"] = "Updated";
    updatedCells[0] = updatedCell;
    updatedModel["entries"] = updatedCells;
    updatedEntry["model"] = updatedModel;
    updated[0] = updatedEntry;
    QVERIFY(batch->update(updated));
    QTRY_VERIFY2(!batch->pending() && batch->errorMessage().isEmpty(),
                 qPrintable(batch->errorMessage()));
    QCOMPARE(tabs->property("shownLabels").toStringList(), QStringList({"A", "Updated", "B"}));
    const auto revision = batch->revision();
    auto invalid = entries;
    auto changed = invalid[0].toMap();
    auto bad = model;
    auto cells = bad["entries"].toList();
    auto value = cells[0].toMap();
    value["component"] = "Invalid.qml";
    cells[0] = value;
    bad["entries"] = cells;
    changed["model"] = bad;
    invalid[0] = changed;
    batch->update(invalid);
    QTRY_VERIFY(!batch->pending() && !batch->errorMessage().isEmpty());
    QCOMPARE(batch->revision(), revision);
    QCOMPARE(tabs->property("shownLabels").toStringList(), QStringList({"A", "Updated", "B"}));
    batch->dispose();
    QTRY_COMPARE(tabs->property("shownLabels").toStringList(), QStringList({"A", "B"}));
}
