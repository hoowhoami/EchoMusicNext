#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"
#ifdef ECHO_ENABLE_QML_BINDING_PROBE
#include <private/qqmlanybinding_p.h>
#include <private/qqmldata_p.h>
#endif
#include <QFile>
#include <memory>
#include <vector>
#ifdef Q_OS_MACOS
#include <malloc/malloc.h>
#endif

namespace
{
struct Footprint
{
    QSet<QObject *> objects;
#ifdef ECHO_ENABLE_QML_BINDING_PROBE
    QSet<const void *> bindings;
#endif
    int layouts = 0, inkMetrics = 0;
};
Footprint footprint(QObject *root)
{
    Footprint result;
    const auto visit = [&](auto &&self, QObject *object) -> void {
        if (!object || result.objects.contains(object))
            return;
        result.objects.insert(object);
        result.layouts += object->inherits("QQuickLayout") || object->inherits("QQuickFlow");
        result.inkMetrics += object->inherits("EchoMusic::EchoTextInkMetrics");
#ifdef ECHO_ENABLE_QML_BINDING_PROBE
        // Test-only Qt 6.12 inspection. Deduplicate aliases and count both the
        // classic QML list (including value subproperties) and QProperty bindings.
        // Reading/ref-counting bindings does not evaluate or remove them.
        if (auto *data = QQmlData::get(object, false))
            for (auto *binding = data->bindings; binding; binding = binding->nextBinding())
                result.bindings.insert(binding);
        const auto *meta = object->metaObject();
        for (int i = 0; i < meta->propertyCount(); ++i) {
            const auto property = meta->property(i);
            if (!property.isBindable())
                continue;
            const auto binding = property.bindable(object).binding();
            if (!binding.isNull())
                result.bindings.insert(QPropertyBindingPrivate::get(binding));
        }
#endif
        for (auto *child : object->children())
            self(self, child);
        if (auto *item = qobject_cast<QQuickItem *>(object))
            for (auto *child : item->childItems())
                self(self, child);
    };
    visit(visit, root);
    return result;
}
void drainRetiredContent()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
} // namespace

void ControlsUiTest::plainTextRetainsInkWithoutTagLayouts()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme
Window {
    width: 360; height: 160; visible: true; color: Theme.main
    property bool modern: false
    property string sample: "中文 Artist 123 / 😀"
    property bool wrapped: false
    property real lineBox: 19.5
    property real available: 290
    property real zoom: 1
    Item {
        x: 20; y: 20; scale: zoom; transformOrigin: Item.TopLeft
        EchoTextTagRow {
            objectName: "full"; width: available; height: implicitHeight
            visible: !modern; text: sample; wrap: wrapped
            lineHeight: lineBox; font: Theme.font(13, Theme.mediumWeight)
            color: Theme.text; fillTextWidth: true
        }
        EchoTextLine {
            objectName: "plain"; width: available; height: implicitHeight
            visible: modern; text: sample; wrap: wrapped
            lineHeight: lineBox; font: Theme.font(13, Theme.mediumWeight)
            color: Theme.text; fillTextWidth: true
        }
    }
})",
                      QUrl("qrc:/plain-text-equivalence.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *full = session.item("full"), *plain = session.item("plain");
    QVERIFY(full && plain);
    const auto before = footprint(full), after = footprint(plain);
    QVERIFY(before.layouts > 0);
    QCOMPARE(after.layouts, 0);
    QCOMPARE(after.inkMetrics, 1);
    QVERIFY(after.objects.size() < before.objects.size());
#ifdef ECHO_ENABLE_QML_BINDING_PROBE
    QVERIFY(after.bindings.size() < before.bindings.size());
    qInfo() << "plain-text bindings" << before.bindings.size() << "->" << after.bindings.size()
            << "objects" << before.objects.size() << "->" << after.objects.size();
#endif
    const auto capture = qEnvironmentVariable("ECHO_SHARED_CONTROLS_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    int sampleIndex = 0;
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (double scale : {1., 1.25})
            for (const auto &text :
                 {QString("中文 Artist 123 / 😀"), QString("Long fallback 中文换行\n第二行 Artist"),
                  QString("Ág / 123 text that must elide at the right edge")}) {
                session.theme.setMode(mode);
                session.root->setProperty("zoom", scale);
                session.root->setProperty("sample", text);
                session.root->setProperty("available", text.contains('\n') ? 100. : 170.);
                session.root->setProperty("wrapped", text.contains('\n'));
                QTRY_COMPARE(plain->implicitWidth(), full->implicitWidth());
                QTRY_COMPARE(plain->implicitHeight(), full->implicitHeight());
                qreal firstLineCenter = 0;
                QVERIFY(QMetaObject::invokeMethod(plain, "firstLineInkCenter",
                                                  Q_RETURN_ARG(qreal, firstLineCenter)));
                QCOMPARE(firstLineCenter, full->property("firstLineInkCenter").toReal());
                session.root->setProperty("modern", false);
                QTest::qWait(30);
                auto beforePixels = session.window->grabWindow();
                session.root->setProperty("modern", true);
                QTest::qWait(30);
                auto afterPixels = session.window->grabWindow();
                if (!capture.isEmpty()) {
                    QVERIFY(beforePixels.save(capture +
                                              QString("/plain-%1-before.png").arg(sampleIndex)));
                    QVERIFY(afterPixels.save(capture +
                                             QString("/plain-%1-after.png").arg(sampleIndex)));
                }
                QVERIFY2(beforePixels == afterPixels,
                         qPrintable(QString("Plain label pixels differ at sample %1: %2")
                                        .arg(sampleIndex)
                                        .arg(text)));
                ++sampleIndex;
            }
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::sharedLabelMatchesSavedPixels()
{
    const auto directory = qEnvironmentVariable("ECHO_SHARED_CONTROL_BASELINE_DIR");
    if (directory.isEmpty())
        QSKIP(
            "Set ECHO_SHARED_CONTROL_BASELINE_DIR to saved source snapshots for pixel comparison");
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    const auto source = QString(R"(
import QtQuick
import EchoMusic.Controls as Controls
import EchoMusic.Theme
import "%1" as Baseline
Window {
    width: 500; height: 180; visible: true; color: Theme.main
    property bool modern: false
    property string sample: "超级VIP Artist 123"
    property bool wrapped: false
    property int tagSize: 0
    property real available: 80
    property real zoom: 1
    Item {
        x: 30.25; y: 20.25; scale: zoom; transformOrigin: Item.TopLeft
        Baseline.EchoTag {
            objectName: "oldTag"; visible: !modern
            text: sample; wrap: wrapped; size: tagSize
            width: wrapped ? available : implicitWidth; height: implicitHeight
        }
        Controls.EchoTag {
            objectName: "newTag"; visible: modern
            text: sample; wrap: wrapped; size: tagSize
            width: wrapped ? available : implicitWidth; height: implicitHeight
        }
    }
})")
                            .arg(QUrl::fromLocalFile(directory + "/before").toString());
    component.setData(source.toUtf8(), QUrl("qrc:/saved-label-equivalence.qml"));
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    auto *oldTag = session.item("oldTag"), *newTag = session.item("newTag");
    QVERIFY(oldTag && newTag);
    int index = 0;
    for (auto mode : {ThemeStore::Light, ThemeStore::Dark})
        for (double scale : {1., 1.25})
            for (int size : {0, 1})
                for (const auto &text :
                     {QString("超级VIP"), QString("四川 gyp 123"),
                      QString("中文 Artist 😀\n下一行"), QString("Trailing   ")}) {
                    session.theme.setMode(mode);
                    session.root->setProperty("zoom", scale);
                    session.root->setProperty("tagSize", size);
                    session.root->setProperty("sample", text);
                    session.root->setProperty("wrapped", text.contains('\n'));
                    QTRY_COMPARE(newTag->implicitWidth(), oldTag->implicitWidth());
                    QTRY_COMPARE(newTag->implicitHeight(), oldTag->implicitHeight());
                    session.root->setProperty("modern", false);
                    QTest::qWait(30);
                    const auto before = session.window->grabWindow();
                    session.root->setProperty("modern", true);
                    QTest::qWait(30);
                    const auto after = session.window->grabWindow();
                    const auto capture = qEnvironmentVariable("ECHO_SHARED_CONTROLS_CAPTURE_DIR");
                    if (!capture.isEmpty()) {
                        QVERIFY(QDir().mkpath(capture));
                        QVERIFY(before.save(capture + QString("/label-%1-before.png").arg(index)));
                        QVERIFY(after.save(capture + QString("/label-%1-after.png").arg(index)));
                    }
                    QVERIFY2(
                        before == after,
                        qPrintable(
                            QString("Saved label pixels differ at %1: %2").arg(index).arg(text)));
                    ++index;
                }
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::sharedControlsRetireUnusedContent()
{
    ControlsSession session;
    QVERIFY(session.create());
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
    id: root
    width: 600; height: 200
    property bool custom: false
    property bool visuals: false
    property int clicks: 0
    EchoTag {
        id: tag; objectName: "switchingTag"; text: "默认标签"
        contentComponent: root.custom ? customContent : null
    }
    EchoTag { objectName: "neighborTag"; x: 80; width: 80; wrap: true; text: "A" }
    Component {
        id: customContent
        Rectangle { objectName: "customContent"; implicitWidth: 48; implicitHeight: 12 }
    }
    EchoIconButton {
        objectName: "semanticButton"; x: 200; glyph: "heart"; text: "收藏"
        visualContentActive: root.visuals
        onClicked: ++root.clicks
    }
    EchoIconButton { objectName: "neighborButton"; x: 260; glyph: "play"; text: "播放" }
    EchoArtistLinks {
        objectName: "artists"; y: 60; width: 450
        artists: [{id: "1", name: "歌手 Artist"}]
    }
    EchoButtonContent { objectName: "textOnly"; y: 100; text: "文字按钮" }
})",
                      QUrl("qrc:/shared-control-retirement.qml"));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    auto *tag = findVisual(host, "switchingTag"), *button = findVisual(host, "semanticButton");
    auto *artists = findVisual(host, "artists"), *textOnly = findVisual(host, "textOnly");
    QVERIFY(tag && button && artists && textOnly);
    QCOMPARE(footprint(tag).inkMetrics, 1);
    QCOMPARE(footprint(artists).inkMetrics, 1); // no hidden final slash
    QVERIFY(!findVisual(textOnly, "buttonGlyph"));
    QVERIFY(!findVisual(textOnly, "buttonSpinner"));
    QVERIFY(!findVisual(button, "iconImage"));
    auto *semanticButton = button;
    button->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(button->hasActiveFocus());
    QTest::keyClick(session.window, Qt::Key_Space);
    QCOMPARE(object->property("clicks").toInt(), 1);
    for (int i = 0; i < 4; ++i) {
        object->setProperty("custom", true);
        QTRY_VERIFY(findVisual(tag, "customContent"));
        drainRetiredContent();
        QCOMPARE(footprint(tag).inkMetrics, 0);
        object->setProperty("custom", false);
        drainRetiredContent();
        QCOMPARE(footprint(tag).inkMetrics, 1);
        object->setProperty("visuals", true);
        QTRY_VERIFY(findVisual(button, "iconImage"));
        QPointer<QObject> retired = findVisual(button, "iconImage");
        object->setProperty("visuals", false);
        QTRY_VERIFY(retired.isNull());
        QCOMPARE(button, semanticButton);
        QVERIFY(button->hasActiveFocus());
    }
    // Retiring one instance cannot clear bindings in another instance sharing
    // the same engine-owned recipe/context.
    auto *neighborTag = findVisual(host, "neighborTag");
    auto *neighborButton = findVisual(host, "neighborButton");
    QVERIFY(neighborTag && neighborButton);
    const auto previousWidth = neighborTag->implicitWidth();
    neighborTag->setProperty("text", QStringLiteral("Another label 中文"));
    QTRY_VERIFY(neighborTag->implicitWidth() > previousWidth);
    QTRY_VERIFY(neighborTag->implicitHeight() > 18);
    neighborButton->setProperty("glyph", QStringLiteral("settings"));
    auto *neighborImage = findVisual(neighborButton, "iconImage");
    QVERIFY(neighborImage);
    QTRY_VERIFY(neighborImage->property("source").toUrl().toString().contains("settings"));
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::sharedControlsBindingFootprint()
{
#ifndef ECHO_ENABLE_QML_BINDING_PROBE
    QSKIP("Configure with -DECHO_ENABLE_QML_BINDING_PROBE=ON for Qt-private binding diagnostics");
#else
    // Optional retained-instance benchmark. Use the recipe/variant filters in
    // fresh processes for malloc comparisons; no user application state changes.
    const auto directory = qEnvironmentVariable("ECHO_SHARED_CONTROL_BASELINE_DIR");
    if (directory.isEmpty())
        QSKIP("Set ECHO_SHARED_CONTROL_BASELINE_DIR to saved source snapshots for A/B counts");
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    const auto recipeFilter = qEnvironmentVariable("ECHO_SHARED_CONTROL_RECIPE");
    const auto variantFilter = qEnvironmentVariable("ECHO_SHARED_CONTROL_VARIANT");
    for (const auto &name : {QString("EchoArtistLinks.qml"), QString("EchoButtonContent.qml"),
                             QString("EchoLabelSurface.qml"), QString("EchoIconButton.qml")}) {
        if (!recipeFilter.isEmpty() && recipeFilter != name)
            continue;
        for (const auto &variant : {QString("before"), QString("after")}) {
            if (!variantFilter.isEmpty() && variantFilter != variant)
                continue;
            // Label comparison includes the unchanged EchoTag wrapper on
            // both sides, so the typed internal surface has its real module
            // identity. Public icon buttons likewise use the registered type.
            const auto fileName = name == "EchoLabelSurface.qml" ? QString("EchoTag.qml") : name;
            QFile file(directory + '/' + variant + '/' + fileName);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QQmlComponent component(&session.engine);
            auto source = file.readAll();
            source.replace("import QtQuick\n", "import QtQuick\nimport EchoMusic.Controls\n");
            if (name == "EchoLabelSurface.qml" || name == "EchoIconButton.qml") {
                const auto type = name == "EchoLabelSurface.qml" ? "EchoTag" : "EchoIconButton";
                // Match the empty outer component on both sides. Cloning the
                // old Item source directly would omit its composite context.
                const auto import =
                    variant == "after"
                        ? QString("EchoMusic.Controls")
                        : '\"' + QUrl::fromLocalFile(directory + "/before").toString() + '\"';
                component.setData(
                    ("import " + import + " as Recipe\nRecipe." + type + " {}").toUtf8(),
                    QUrl("qrc:/named-control-benchmark.qml"));
            } else {
                component.setData(source, QUrl::fromLocalFile(file.fileName()));
            }
            QVERIFY2(component.isReady(), qPrintable(component.errorString()));
            QVariantMap properties;
            if (name.contains("Artist"))
                properties = {
                    {"artists", QVariantList{QVariantMap{{"id", "1"}, {"name", "歌手 Artist"}}}},
                    {"width", 240}};
            else if (name.contains("IconButton"))
                properties = {{"glyph", "heart"}, {"text", "收藏"}};
            else
                properties = {{"text", "共享内容 Artist 123"}};
            QQmlComponent customContent(&session.engine);
            const auto state = qEnvironmentVariable("ECHO_SHARED_CONTROL_STATE", "default");
            if (name == "EchoLabelSurface.qml" && state == "custom") {
                customContent.setData(
                    "import QtQuick\nRectangle { implicitWidth: 40; implicitHeight: 12 }",
                    QUrl("qrc:/custom-label-benchmark.qml"));
                QVERIFY2(customContent.isReady(), qPrintable(customContent.errorString()));
                properties.insert("contentComponent", QVariant::fromValue(&customContent));
            } else if (name == "EchoIconButton.qml" && state == "hidden") {
                properties.insert("opacity", 0.);
                if (variant == "after")
                    properties.insert("visualContentActive", false);
            }
            {
                QScopedPointer<QObject> warm(component.createWithInitialProperties(properties));
                QVERIFY2(warm, qPrintable(component.errorString()));
            }
            drainRetiredContent();
            auto host = std::make_unique<QQuickItem>();
            Footprint total;
            qint64 held = 0;
#ifdef Q_OS_MACOS
            malloc_statistics_t start{};
            malloc_zone_statistics(malloc_default_zone(), &start);
#endif
            std::vector<std::unique_ptr<QObject>> instances;
            for (int i = 0; i < 500; ++i) {
                auto object =
                    std::unique_ptr<QObject>(component.createWithInitialProperties(properties));
                QVERIFY2(object, qPrintable(component.errorString()));
                object->setParent(host.get());
                if (auto *item = qobject_cast<QQuickItem *>(object.get()))
                    item->setParentItem(host.get());
                instances.push_back(std::move(object));
            }
            drainRetiredContent();
            if (name == "EchoLabelSurface.qml") {
                auto *tag = qobject_cast<QQuickItem *>(instances.front().get());
                QVERIFY(tag && tag->implicitWidth() > 30);
            } else if (name == "EchoIconButton.qml") {
                auto *button = qobject_cast<QQuickItem *>(instances.front().get());
                auto *icon = button ? findVisual(button, "iconImage") : nullptr;
                if (state == "hidden" && variant == "after")
                    QVERIFY(!icon);
                else
                    QVERIFY(icon && icon->property("source").toUrl().toString().contains("heart"));
            }
#ifdef Q_OS_MACOS
            malloc_statistics_t end{};
            malloc_zone_statistics(malloc_default_zone(), &end);
            held = qint64(end.size_in_use) - qint64(start.size_in_use);
#endif
            total = footprint(host.get());
            qInfo().noquote() << "SHARED_CONTROL" << name << variant << "state=" << state
                              << "instances=500"
                              << "objects=" << total.objects.size()
                              << "bindings=" << total.bindings.size() << "layouts=" << total.layouts
                              << "malloc_delta=" << held;
            instances.clear();
            drainRetiredContent();
        }
    }
#endif
}
