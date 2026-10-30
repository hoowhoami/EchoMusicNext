#include "ControlsUiTest.h"
#include "ControlsUiTestFixture.h"
#include <QApplication>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QFile>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

void ControlsUiTest::scaledPopupTextKeepsNativePixelResolution()
{
    ControlsSession session;
    session.theme.attachToEngine(session.engine);
    session.theme.setMotionEnabled(false);
    session.theme.setFloatingFrosted(false);
    session.theme.setMode(ThemeStore::Light);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.Theme
ApplicationWindow {
 id: host; width: 1100; height: 800; visible: true; color: "white"
 property real zoom: 1
 property alias panel: owner.item
 function showPanel() { owner.open() }
 EchoPopupLoader { id: owner; hostItem: host.contentItem
  sourceComponent: EchoDrawer { uiScale: host.zoom; playerBottomInset: 12
   contentItem: Item {
    Text { x: 24; y: 48; text: "播放队列 弹出层文字 清晰度\nEchoMusicNext 0123456789"; font: Theme.font(13,600); color: "#111111" }
    Item {
     id: pattern; objectName: "resolutionPattern"; x: 24; y: 120
     property real pixel: 1 / (host.zoom * Screen.devicePixelRatio)
     width: 128 * pixel; height: 16 * pixel
     Row { Repeater { model: 128
      Rectangle { required property int index; width: pattern.pixel; height: pattern.height; color: index % 2 ? "black" : "white" }
     } }
    }
   }
  }
 }
})",
                      QUrl());
    session.root.reset(component.create());
    QVERIFY2(session.root, qPrintable(component.errorString()));
    session.window = qobject_cast<QQuickWindow *>(session.root.data());
    QVERIFY(session.window && Test::prepareNativeInput(session.window));
    if (session.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Text pixels require native RHI");
    for (qreal scale : {1., 1.25, 1.5, 2.}) {
        session.root->setProperty("zoom", scale);
        QVERIFY(QMetaObject::invokeMethod(session.root.data(), "showPanel"));
        QPointer<QObject> panel = session.root->property("panel").value<QObject *>();
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        auto *content = panel->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QTest::qWait(100);
        QQuickItem *capture = nullptr;
        QTRY_VERIFY((capture = findVisual(session.window->contentItem(), "popupContentCapture")));
        const auto texture = capture->property("textureSize").toSize();
        const auto dpr = session.window->devicePixelRatio();
        const auto span = content->mapRectToScene(content->boundingRect());
        QCOMPARE(
            texture,
            QSize(qCeil(span.right() * dpr - 0.000001) - qFloor(span.left() * dpr + 0.000001),
                  qCeil(span.bottom() * dpr - 0.000001) - qFloor(span.top() * dpr + 0.000001)));
        const auto actualTexture = renderedTextureSize(session.window, capture);
        QCOMPARE(actualTexture, texture);
        qInfo() << "popup dpr" << dpr << "actual content texture" << actualTexture;
        auto *effect = findVisual(session.window->contentItem(), "popupContentMaskEffect");
        QVERIFY(effect);
        qInfo() << "effect size" << effect->size() << "hasProxySource"
                << effect->property("hasProxySource");
        auto *clip = panel->findChild<QObject *>("popupContentClip");
        qInfo() << "capture metrics" << span << clip->property("dpr")
                << capture->property("sourceRect") << effect->mapRectToScene(effect->boundingRect())
                << session.window->size() << session.window->grabWindow().size();
        const auto point = content->mapToScene(QPointF(22, 46)) * dpr;
        const QRect crop(qRound(point.x()), qRound(point.y()), qRound(340 * scale * dpr),
                         qRound(45 * scale * dpr));
        auto *pattern = findVisual(content, "resolutionPattern");
        QVERIFY(pattern);
        const auto patternPoint = pattern->mapToScene({2 * pattern->property("pixel").toReal(),
                                                       2 * pattern->property("pixel").toReal()}) *
                                  dpr;
        const QRect patternCrop(qRound(patternPoint.x()), qRound(patternPoint.y()), 124, 12);
        const auto maskedFrame = session.window->grabWindow();
        const auto masked = maskedFrame.copy(crop).convertToFormat(QImage::Format_RGB32);
        const auto maskedPattern = maskedFrame.copy(patternCrop);
        QVERIFY(capture->setProperty("sourceItem", QVariant::fromValue<QQuickItem *>(nullptr)));
        effect->setVisible(false);
        QTest::qWait(100);
        const auto directFrame = session.window->grabWindow();
        const auto direct = directFrame.copy(crop).convertToFormat(QImage::Format_RGB32);
        const auto directPattern = directFrame.copy(patternCrop);
        int patternDifference = 0, inversePatternDifference = 0, patternContrast = 255;
        for (int y = 0; y < maskedPattern.height(); ++y)
            for (int x = 0; x < maskedPattern.width(); ++x) {
                const auto value = qGray(maskedPattern.pixel(x, y));
                const auto reference = qGray(directPattern.pixel(x, y));
                patternDifference = qMax(patternDifference, qAbs(value - reference));
                inversePatternDifference =
                    qMax(inversePatternDifference, qAbs(value - (255 - reference)));
                if (x > 0)
                    patternContrast =
                        qMin(patternContrast, qAbs(value - qGray(maskedPattern.pixel(x - 1, y))));
            }
        // Rectangle rasterization can choose the adjacent pixel at an
        // exact edge tie. Either stripe phase must retain full contrast
        // with no repeated pixels; interpolation or low-res copies cannot.
        patternDifference = qMin(patternDifference, inversePatternDifference);
        // An RGBA offscreen target uses grayscale text AA; the direct
        // target can use colored LCD AA. Compare neutral text samples,
        // and independently reject resampling with one-device-pixel stripes.
        double difference = 0, neutralDifference = 0;
        int neutralSamples = 0;
        for (int y = 0; y < masked.height(); ++y)
            for (int x = 0; x < masked.width(); ++x) {
                const auto pixel = direct.pixel(x, y);
                const auto delta = qAbs(qGray(masked.pixel(x, y)) - qGray(pixel));
                difference += delta;
                if (qMax(qRed(pixel), qMax(qGreen(pixel), qBlue(pixel))) -
                        qMin(qRed(pixel), qMin(qGreen(pixel), qBlue(pixel))) <=
                    2) {
                    neutralDifference += delta;
                    ++neutralSamples;
                }
            }
        difference /= masked.width() * masked.height();
        QVERIFY(neutralSamples > 0);
        neutralDifference /= neutralSamples;
        qInfo() << "popup scale" << scale << "texture" << texture << "mean pixel difference"
                << difference << "neutral text difference" << neutralDifference
                << "stripe phase-independent difference" << patternDifference
                << "stripe minimum contrast" << patternContrast;
        const auto captures = qEnvironmentVariable("ECHO_TEXT_CLARITY_CAPTURES");
        if (!captures.isEmpty()) {
            QDir().mkpath(captures);
            QVERIFY(masked.save(captures + QString("/masked-%1.png").arg(scale)));
            QVERIFY(direct.save(captures + QString("/direct-%1.png").arg(scale)));
            QVERIFY(maskedPattern.save(captures + QString("/stripes-masked-%1.png").arg(scale)));
            QVERIFY(directPattern.save(captures + QString("/stripes-direct-%1.png").arg(scale)));
        }
        QVERIFY2(neutralDifference < 2., qPrintable(QString::number(neutralDifference)));
        QVERIFY2(patternDifference <= 2, qPrintable(QString::number(patternDifference)));
        QVERIFY2(patternContrast >= 253, qPrintable(QString::number(patternContrast)));
        QVERIFY(QMetaObject::invokeMethod(panel, "close"));
        QTRY_VERIFY(panel.isNull());
    }
}

void ControlsUiTest::floatingPanelEdgesAndMenuHeight()
{
    ControlsSession session;
    QVERIFY(session.create());
    session.window->resize(1100, 800);
    session.theme.setMotionEnabled(false);
    qInfo() << "Panel graphics API:" << session.window->rendererInterface()->graphicsApi();
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            import EchoMusic.Theme
            Item {
                id: host
                property real panelScale: 1
                property alias dialog: dialogOwner.item
                property alias drawer: drawerOwner.item
                property alias menu: menuOwner.item
                function showDialog(): void { dialogOwner.open(); }
                function showDrawer(): void { drawerOwner.open(); }
                function showMenu(): void { menuOwner.popup(host, 100, 100); }
                EchoBackdropRoot {
                    anchors.fill: parent
                    Rectangle { anchors.fill: parent; color: Theme.main }
                }
                EchoPopupLoader {
                    id: dialogOwner
                    hostItem: host
                    sourceComponent: EchoDialog {
                        uiScale: host.panelScale
                        heading: "对话框 Dialog"
                        body: Component { Item { implicitHeight: 180 } }
                    }
                }
                EchoPopupLoader {
                    id: drawerOwner
                    hostItem: host
                    sourceComponent: EchoDrawer {
                        uiScale: host.panelScale
                        contentItem: EchoDrawerHeader { heading: "抽屉 Drawer" }
                    }
                }
                EchoPopupLoader {
                    id: menuOwner
                    hostItem: host
                    sourceComponent: EchoMenu {
                        EchoMenuEntry { text: "立即播放" }
                        EchoMenuEntry { text: "下一首播放" }
                        EchoMenuEntry { text: "排队候播" }
                        EchoMenuSeparator {}
                        EchoMenuEntry { text: "添加到" }
                        EchoMenuSeparator { visible: false }
                        EchoMenuEntry { visible: false; text: "隐藏操作" }
                        EchoMenuSeparator {}
                        EchoMenuEntry { text: "分享" }
                        EchoMenuEntry { text: "复制信息" }
                    }
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    host->setSize(session.window->size());
    const auto captures = qEnvironmentVariable("ECHO_PANEL_CAPTURES");
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const qreal scale : {1., 1.25}) {
            host->setProperty("panelScale", scale);
            for (const bool frosted : {false, true}) {
                session.theme.setFloatingFrosted(frosted);
                for (const auto *name : {"dialog", "drawer"}) {
                    QVERIFY(!host->property(name).value<QObject *>());
                    QVERIFY(QMetaObject::invokeMethod(
                        host, QByteArray(name) == "dialog" ? "showDialog" : "showDrawer"));
                    QPointer<QObject> popup = host->property(name).value<QObject *>();
                    QVERIFY(popup);
                    QTRY_VERIFY(popup->property("opened").toBool());
                    QTest::qWait(60);
                    qInfo() << name << scale << frosted
                            << popup->property("background")
                                   .value<QQuickItem *>()
                                   ->property("hasBackdrop");
                    if (!captures.isEmpty()) {
                        QVERIFY(QDir().mkpath(captures));
                        const auto path = QString("/%1-%2-%3-%4.png")
                                              .arg(name)
                                              .arg(int(mode))
                                              .arg(scale)
                                              .arg(frosted);
                        QVERIFY(session.window->grabWindow().save(captures + path));
                    }
                    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
                    QTRY_VERIFY(popup.isNull());
                }
            }
        }
    }
    QVERIFY(QMetaObject::invokeMethod(host, "showMenu"));
    QPointer<QObject> menu = host->property("menu").value<QObject *>();
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *list = menu->property("contentItem").value<QQuickItem *>();
    QVERIFY(list);
    QTest::qWait(100);
    qInfo() << "Menu content / viewport:" << list->property("contentHeight") << list->height();
    QVERIFY2(list->property("contentHeight").toReal() <= list->height() + .5,
             "A menu which fits the window must not have a tiny scroll range");
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    QTRY_VERIFY(menu.isNull());
    session.window->resize(1100, 300);
    QVERIFY(QMetaObject::invokeMethod(host, "showMenu"));
    menu = host->property("menu").value<QObject *>();
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    list = menu->property("contentItem").value<QQuickItem *>();
    QVERIFY(list);
    QTest::qWait(100);
    QVERIFY(list->property("contentHeight").toReal() > list->height());
    QVERIFY(list->property("interactive").toBool());
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    QTRY_VERIFY(menu.isNull());
}

void ControlsUiTest::menusDrawersAndFileDialogsReleaseAfterClose()
{
    ControlsSession session;
    QVERIFY(session.create());
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import QtQuick.Controls.Basic
            import QtQuick.Dialogs as NativeDialogs
            import EchoMusic.Controls
            Item {
                id: host
                width: 300; height: 220
                property bool systemFileDialog: false
                property alias menuOwner: menuOwner
                property alias drawerOwner: drawerOwner
                property alias fileOwner: fileOwner
                function openMenu(): void { menuOwner.popup(host, 0, 60); }
                EchoPopupLoader {
                    id: menuOwner
                    hostItem: host
                    sourceComponent: EchoMenu {
                        popupType: Popup.Window
                        Action { text: "菜单动作" }
                    }
                }
                EchoPopupLoader {
                    id: drawerOwner
                    hostItem: host
                    sourceComponent: EchoDrawer {
                        preferredWidth: 260
                        contentItem: Text { text: "抽屉正文" }
                    }
                }
                EchoFileDialogLoader {
                    id: fileOwner
                    hostItem: host
                    sourceComponent: EchoFileDialog {
                        options: host.systemFileDialog ? 0 : EchoFileDialog.DontUseNativeDialog
                    }
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(host);
    host->setParent(session.root.data());
    host->setParentItem(session.window->contentItem());
    for (const auto *name : {"menuOwner", "drawerOwner", "fileOwner"}) {
        auto *owner = root->property(name).value<QObject *>();
        QVERIFY(owner);
        QVERIFY(!owner->property("item").value<QObject *>());
        for (int cycle = 0; cycle < 3; ++cycle) {
            if (QByteArray(name) == "menuOwner")
                QVERIFY(QMetaObject::invokeMethod(root.data(), "openMenu"));
            else if (QByteArray(name) == "fileOwner")
                QVERIFY(QMetaObject::invokeMethod(owner, "open"));
            else
                QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
            QPointer<QObject> popup = owner->property("item").value<QObject *>();
            QVERIFY(popup);
            QTRY_VERIFY(popup->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(popup, "close"));
            QTRY_VERIFY(popup.isNull());
            QVERIFY(!owner->property("item").value<QObject *>());
        }
    }
    if (QGuiApplication::platformName() == "cocoa") {
        root->setProperty("systemFileDialog", true);
        auto *owner = root->property("fileOwner").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(owner, "open"));
        QPointer<QObject> native = owner->property("item").value<QObject *>();
        QVERIFY(native);
        QTRY_VERIFY(native->property("visible").toBool());
        QCOMPARE(native->property("parentWindow").value<QWindow *>(), session.window);
        QFileDialog *systemDialog = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible())
                systemDialog = dialog;
        QVERIFY(systemDialog);
        qInfo() << "System picker flags" << systemDialog->options() << systemDialog->windowFlags()
                << "native helper" << systemDialog->testAttribute(Qt::WA_DontShowOnScreen);
        QVERIFY(systemDialog->testAttribute(Qt::WA_DontShowOnScreen));
        QVERIFY(QMetaObject::invokeMethod(native, "close"));
        QTRY_VERIFY(native.isNull());
        QVERIFY(!owner->property("item").value<QObject *>());
    }
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::verificationCoverPreservesPopupInstancesAndDrafts()
{
    ControlsSession session;
    QVERIFY(session.create());
    session.theme.setMotionEnabled(false);
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs as NativeDialogs
import EchoMusic.Controls
Item {
 id: host; width: 700; height: 600
 property var originalWindow: host.Window.window
 property alias dialogOwner: dialogOwner
 property alias drawerOwner: drawerOwner
 property alias fileOwner: fileOwner
 property alias folderOwner: folderOwner
 property alias verificationOwner: verificationOwner
 property int businessClosed: 0
 function cover(value: bool): void {
  EchoPopupCoordinator.setInteractionSuspended(originalWindow, value);
  host.enabled = !value;
 }
 Component.onDestruction: EchoPopupCoordinator.setInteractionSuspended(originalWindow, false)
 EchoPopupLoader { id: dialogOwner; hostItem: host
  sourceComponent: EchoDialog {
   property bool allowed: true
   enabled: allowed
   heading: "保留草稿"
   onClosed: host.businessClosed++
   body: Column {
    EchoTextField { objectName: "retainedDraft"; text: "未保存的内容"; width: 230 }
    EchoSelect { objectName: "retainedSelect"; width: 230; options: [{ label: "选项", value: 1 }] }
    EchoDatePicker { objectName: "retainedDate"; width: 230 }
   }
  }
 }
 EchoPopupLoader { id: drawerOwner; hostItem: host
  sourceComponent: EchoDrawer { onClosed: host.businessClosed++; contentItem: Text { text: "抽屉" } }
 }
 EchoFileDialogLoader { id: fileOwner; hostItem: host
  sourceComponent: EchoFileDialog { options: EchoFileDialog.DontUseNativeDialog }
 }
 EchoFolderDialogLoader { id: folderOwner; hostItem: host
  sourceComponent: EchoFileDialog { fileMode: EchoFileDialog.OpenFolder; options: EchoFileDialog.DontUseNativeDialog }
 }
 EchoPopupLoader { id: verificationOwner; hostItem: host.Window.window ? host.Window.window.contentItem : null
  sourceComponent: EchoDialog { verificationSurface: true; heading: "安全验证"; body: Item { implicitHeight: 80 } }
 }
})",
                      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    host->setParent(session.root.data());
    auto owner = [&](const char *name) { return root->property(name).value<QObject *>(); };
    auto show = [&](QObject *value) {
        return QMetaObject::invokeMethod(value, "open", Q_ARG(QVariant, QVariantMap{}));
    };
    QVERIFY(show(owner("dialogOwner")));
    QPointer<QObject> dialog = owner("dialogOwner")->property("item").value<QObject *>();
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto *content = dialog->property("contentItem").value<QQuickItem *>();
    auto *draft = findVisual(content, "retainedDraft");
    auto *select = findVisual(content, "retainedSelect");
    auto *date = findVisual(content, "retainedDate");
    QVERIFY(draft && select && date);
    QVERIFY(QMetaObject::invokeMethod(select, "openPopup"));
    QPointer<QObject> selectPopup = select->property("popup").value<QObject *>();
    QVERIFY(selectPopup);
    auto *calendarButton = findVisual(date, "dateCalendar");
    QVERIFY(calendarButton);
    QVERIFY(QMetaObject::invokeMethod(calendarButton, "clicked"));
    QPointer<QObject> calendarPopup = date->findChild<QObject *>("datePickerCalendar");
    QVERIFY(calendarPopup);
    // A second independent business surface must survive the same cover.
    QVERIFY(show(owner("drawerOwner")));
    QPointer<QObject> drawer = owner("drawerOwner")->property("item").value<QObject *>();
    QVERIFY(drawer);
    const auto policy = dialog->property("closePolicy");
    for (int cycle = 0; cycle < 2; ++cycle) {
        QVERIFY(QMetaObject::invokeMethod(root.data(), "cover", Q_ARG(bool, true)));
        QVERIFY(show(owner("verificationOwner")));
        QPointer<QObject> verification =
            owner("verificationOwner")->property("item").value<QObject *>();
        QVERIFY(verification);
        QTRY_VERIFY(verification->property("opened").toBool());
        QTRY_VERIFY(!dialog->property("enabled").toBool());
        QVERIFY(drawer && selectPopup && calendarPopup);
        QVERIFY(dialog->property("visible").toBool());
        QVERIFY(drawer->property("visible").toBool());
        QVERIFY(selectPopup->property("visible").toBool());
        QVERIFY(calendarPopup->property("visible").toBool());
        QCOMPARE(dialog->property("closePolicy").toInt(), 0);
        QVERIFY(verification->property("enabled").toBool());
        QCOMPARE(root->property("businessClosed").toInt(), 0);
        QCOMPARE(draft->property("text").toString(), QString("未保存的内容"));
        QVERIFY(QMetaObject::invokeMethod(root.data(), "cover", Q_ARG(bool, false)));
        QVERIFY(QMetaObject::invokeMethod(owner("verificationOwner"), "close"));
        QTRY_VERIFY(verification.isNull());
        QCOMPARE(owner("dialogOwner")->property("item").value<QObject *>(), dialog.data());
        QCOMPARE(owner("drawerOwner")->property("item").value<QObject *>(), drawer.data());
        QVERIFY(dialog->property("enabled").toBool());
        QCOMPARE(dialog->property("closePolicy"), policy);
        QCOMPARE(draft->property("text").toString(), QString("未保存的内容"));
        QCOMPARE(root->property("businessClosed").toInt(), 0);
    }
    // Restore the caller's enabled binding rather than forcing enabled=true.
    QVERIFY(QMetaObject::invokeMethod(root.data(), "cover", Q_ARG(bool, true)));
    dialog->setProperty("allowed", false);
    QVERIFY(QMetaObject::invokeMethod(root.data(), "cover", Q_ARG(bool, false)));
    QVERIFY(!dialog->property("enabled").toBool());
    dialog->setProperty("allowed", true);
    QTRY_VERIFY(dialog->property("enabled").toBool());
    for (const char *name : {"fileOwner", "folderOwner"}) {
        QVERIFY(QMetaObject::invokeMethod(owner(name), "open"));
        QPointer<QObject> native = owner(name)->property("item").value<QObject *>();
        QVERIFY(native);
        QTRY_VERIFY(native->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(), "cover", Q_ARG(bool, true)));
        QVERIFY(native && native->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.data(), "cover", Q_ARG(bool, false)));
        QCOMPARE(owner(name)->property("item").value<QObject *>(), native.data());
        QVERIFY(QMetaObject::invokeMethod(owner(name), "close"));
        QTRY_VERIFY(native.isNull());
    }
    // Suspension is not permission to keep a detached/hidden host alive.
    QVERIFY(QMetaObject::invokeMethod(root.data(), "cover", Q_ARG(bool, true)));
    host->setVisible(false);
    QTRY_VERIFY(dialog.isNull());
    QTRY_VERIFY(drawer.isNull());
    QTRY_VERIFY(selectPopup.isNull());
    QTRY_VERIFY(calendarPopup.isNull());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::businessPopupsAreCreatedOnDemandAndDestroyedAfterExit()
{
    ControlsSession session;
    QVERIFY(session.create());
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                id: host
                property alias owner: owner
                property alias picker: picker
                property alias calendar: calendar
                EchoSelect { id: picker; width: 180; options: [{label:"One",value:1},{label:"Two",value:2}] }
                EchoDatePicker { id: calendar; y: 45; width: 180 }
                EchoPopupLoader {
                    id: owner
                    hostItem: host
                    sourceComponent: EchoDialog {
                        heading: "按需窗口"
                        body: EchoSelect { objectName: "nestedSelect"; options: [{label:"nested",value:1}] }
                        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 180 } }
                    }
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    host->setParent(session.root.data());
    auto *owner = root->property("owner").value<QObject *>();
    auto *picker = root->property("picker").value<QObject *>();
    QVERIFY(owner && picker);
    const auto nativePopups = [&] {
        QSet<QObject *> visited;
        QList<QObject *> found;
        const auto visit = [&](auto &&self, QObject *object) -> void {
            if (!object || visited.contains(object))
                return;
            visited.insert(object);
            if (object->inherits("QQuickPopup"))
                found.append(object);
            for (auto *child : object->children())
                self(self, child);
            if (auto *item = qobject_cast<QQuickItem *>(object))
                for (auto *child : item->childItems())
                    self(self, child);
        };
        visit(visit, host);
        return found;
    };
    QCOMPARE(nativePopups().size(), 0);
    QVERIFY(!owner->property("item").value<QObject *>());
    for (int cycle = 0; cycle < 10; ++cycle) {
        QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
        QTRY_VERIFY(owner->property("opened").toBool());
        QPointer<QObject> popup = owner->property("item").value<QObject *>();
        QVERIFY(popup);
        QCOMPARE(nativePopups().size(), 1);
        auto *content = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        auto *nested = findVisual(content, "nestedSelect");
        QVERIFY(nested);
        QVERIFY(QMetaObject::invokeMethod(nested, "openPopup"));
        QTRY_COMPARE(nativePopups().size(), 2);
        QPointer<QObject> nestedPopup = nested->property("popup").value<QObject *>();
        QVERIFY(nestedPopup);
        QVERIFY(QMetaObject::invokeMethod(owner, "close"));
        QVERIFY(popup); // Native exit transition must retain the surface.
        if (cycle == 0) {
            // Reopen before exit ends: the queued disposal must not win.
            QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
            QTRY_VERIFY(owner->property("opened").toBool());
            QCOMPARE(owner->property("item").value<QObject *>(), popup.data());
            QVERIFY(QMetaObject::invokeMethod(owner, "close"));
        }
        QTRY_VERIFY(popup.isNull());
        QTRY_VERIFY(nestedPopup.isNull());
        QTRY_COMPARE(nativePopups().size(), 0);
        QVERIFY(!owner->property("item").value<QObject *>());
    }
    QVERIFY(QMetaObject::invokeMethod(picker, "openPopup"));
    QTRY_COMPARE(nativePopups().size(), 1);
    QPointer<QObject> popup = picker->property("popup").value<QObject *>();
    QVERIFY(popup);
    host->setVisible(false);
    QTRY_VERIFY(popup.isNull());
    QCOMPARE(nativePopups().size(), 0);
    host->setVisible(true);
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
    QTRY_VERIFY(owner->property("opened").toBool());
    popup = owner->property("item").value<QObject *>();
    session.window->hide();
    QTRY_VERIFY(popup.isNull());
    session.window->show();
    QVERIFY(Test::prepareNativeInput(session.window));
    QVERIFY(QMetaObject::invokeMethod(owner, "open", Q_ARG(QVariant, QVariantMap{})));
    QTRY_VERIFY(owner->property("opened").toBool());
    popup = owner->property("item").value<QObject *>();
    root.reset();
    QTRY_VERIFY(popup.isNull());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::tooltipPopupsExistOnlyDuringTheirTrigger()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("BackdropHarness.qml")));
    session.item("frostProbe")->setVisible(false);
    const auto nativeTips = [&] {
        QSet<QObject *> visited;
        QList<QObject *> found;
        const auto visit = [&](auto &&self, QObject *object) -> void {
            if (!object || visited.contains(object))
                return;
            visited.insert(object);
            if (object->inherits("QQuickToolTip"))
                found.append(object);
            for (auto *child : object->children())
                self(self, child);
            if (auto *item = qobject_cast<QQuickItem *>(object))
                for (auto *child : item->childItems())
                    self(self, child);
        };
        visit(visit, session.root.data());
        return found;
    };
    const auto phase = [&](const char *name) {
        QTest::qWait(100);
        qInfo() << "Tooltip phase" << name << "pid" << QCoreApplication::applicationPid()
                << "native-tooltips" << nativeTips().size();
        QTest::qWait(qBound(20, qEnvironmentVariableIntValue("ECHO_TOOLTIP_PROFILE_MS"), 30000));
    };
    phase("empty");
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                Repeater {
                    model: 500
                    Item {
                        id: anchor
                        width: 32
                        height: 32
                        y: 100
                        property alias hint: hint
                        EchoToolTip {
                            id: hint
                            parent: anchor
                            text: "有必要的说明"
                            delay: 0
                        }
                    }
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> bulk(component.create());
    QVERIFY2(bulk, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(bulk.data());
    QVERIFY(host);
    host->setParent(session.root.data());
    host->setParentItem(session.window->contentItem());
    phase("configured-500");
    int coldRecipes = 0, coldTimers = 0;
    for (auto *anchor : host->childItems()) {
        auto *recipe = anchor->property("hint").value<QObject *>();
        if (!recipe)
            continue;
        ++coldRecipes;
        QVERIFY(!recipe->property("session").value<QObject *>());
        for (auto *object : recipe->findChildren<QObject *>())
            coldTimers += object->inherits("QQmlTimer");
    }
    QCOMPARE(coldRecipes, 500);
    QCOMPARE(coldTimers, 0);
    qInfo() << "500 cold tooltip recipes have no hover timers or native popups";
    const auto idleCount = nativeTips().size();
    QObject *hint = nullptr;
    for (auto *anchor : host->childItems()) {
        hint = anchor->property("hint").value<QObject *>();
        if (hint)
            break;
    }
    QVERIFY(hint);
    hint->setProperty("requestedVisible", true);
    QTRY_VERIFY(hint->property("visible").toBool());
    phase("triggered-one");
    const auto triggeredCount = nativeTips().size();
    QList<QPointer<QObject>> retired;
    for (auto *tip : nativeTips())
        if (tip->property("visible").toBool())
            retired.append(tip);
    hint->setProperty("requestedVisible", false);
    phase("left");
    const auto leftCount = nativeTips().size();
    for (int i = 0; i < 20; ++i) {
        hint->setProperty("requestedVisible", true);
        QTRY_VERIFY(hint->property("visible").toBool());
        for (auto *tip : nativeTips())
            if (tip->property("visible").toBool())
                retired.append(tip);
        hint->setProperty("requestedVisible", false);
        QTest::qWait(10);
    }
    phase("left-after-20");
    const auto finalCount = nativeTips().size();
    hint->setProperty("delay", 500);
    for (int i = 0; i < 5; ++i) {
        hint->setProperty("requestedVisible", true);
        QTest::qWait(20);
        QVERIFY(nativeTips().isEmpty());
        hint->setProperty("requestedVisible", false);
    }
    QTest::qWait(550);
    QVERIFY(nativeTips().isEmpty());
    bulk.reset();
    session.engine.collectGarbage();
    phase("destroyed");
    QCOMPARE(idleCount, 0);
    QCOMPARE(triggeredCount, 1);
    QCOMPARE(leftCount, 0);
    QCOMPARE(finalCount, 0);
    for (const auto &tip : retired)
        QVERIFY(tip.isNull());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::tooltipPressPreservesClickAndRelease()
{
    ControlsSession session;
    QVERIFY(session.create());
    session.window->setPosition(session.window->screen()->availableGeometry().topLeft() +
                                QPoint(16, 16));
    QVERIFY(Test::prepareNativeInput(session.window));
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    auto *button = session.item("iconButton");
    QVERIFY(button);
    auto *hint = button->findChild<QObject *>("iconButtonToolTip");
    QVERIFY(hint);
    QSignalSpy clicked(button, SIGNAL(clicked()));
    const auto center = button->mapToScene(button->boundingRect().center()).toPoint();
    const auto hover = [&] {
        Test::moveNativePointer(session.window, {10, 270});
        QTRY_VERIFY(!hint->property("requestedVisible").toBool());
        QTest::qWait(80);
        Test::moveNativePointer(session.window, center);
        QTRY_VERIFY(hint->property("requestedVisible").toBool());
    };
    for (const bool delayed : {false, true}) {
        hint->setProperty("delay", delayed ? 300 : 0);
        hover();
        QPointer<QObject> nativeHint = hint->property("popup").value<QObject *>();
        if (!delayed) {
            QTRY_VERIFY(hint->property("visible").toBool());
            nativeHint = hint->property("popup").value<QObject *>();
            QVERIFY(nativeHint);
        }
        const auto before = clicked.count();
        QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(nativeHint.isNull());
        QVERIFY(button->property("pressed").toBool());
        QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(!button->property("pressed").toBool());
        QTRY_VERIFY(!button->property("down").toBool());
        QCOMPARE(clicked.count(), before + 1);
        QTRY_VERIFY(nativeHint.isNull());
        QTest::qWait(350);
        QVERIFY(!hint->property("popup").value<QObject *>());
        QVERIFY(!session.window->mouseGrabberItem());
    }
    hover();
    QTRY_VERIFY(hint->property("visible").toBool());
    Test::moveNativePointer(session.window, {10, 270});
    QTRY_VERIFY(!hint->property("session").value<QObject *>());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::nativeMenuCancelsVisibleAndDelayedHintsUntilNewHover()
{
    ControlsSession session;
    QVERIFY(session.create());
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QQmlComponent component(&session.engine);
    component.setData(R"(
            import QtQuick
            import EchoMusic.Controls
            Item {
                id: anchor
                width: 32
                height: 32
                property alias hint: hint
                function nativeMenu() { EchoPopupCoordinator.nativeMenuOpening(); }
                EchoToolTip {
                    id: hint
                    parent: anchor
                    text: "说明操作范围"
                    delay: 150
                }
            }
        )",
                      QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *anchor = qobject_cast<QQuickItem *>(object.data());
    anchor->setParentItem(session.window->contentItem());
    auto *hint = object->property("hint").value<QObject *>();
    QVERIFY(hint);
    hint->setProperty("requestedVisible", true);
    QVERIFY(!hint->property("popup").value<QObject *>());
    QVERIFY(QMetaObject::invokeMethod(object.data(), "nativeMenu"));
    QTest::qWait(180);
    QVERIFY(!hint->property("popup").value<QObject *>());
    hint->setProperty("requestedVisible", false);
    hint->setProperty("delay", 0);
    hint->setProperty("requestedVisible", true);
    QTRY_VERIFY(hint->property("visible").toBool());
    QPointer<QObject> popup = hint->property("popup").value<QObject *>();
    QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(object.data(), "nativeMenu"));
    QTRY_VERIFY(popup.isNull());
    QVERIFY(!hint->property("visible").toBool());
    QTest::qWait(180);
    QVERIFY(!hint->property("popup").value<QObject *>());
    hint->setProperty("requestedVisible", false);
    hint->setProperty("requestedVisible", true);
    QTRY_VERIFY(hint->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::dialogHeaderBodyAndFooterShareInsets()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("DialogLayoutHarness.qml")));
    auto *panel = session.root->property("panel").value<QObject *>();
    QVERIFY(panel);
    const auto capture = qEnvironmentVariable("ECHO_DIALOG_LAYOUT_CAPTURE_DIR");
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1.0, 1.25}) {
            session.root->setProperty("surfaceScale", scale);
            for (const bool withActions : {false, true}) {
                session.root->setProperty("withActions", withActions);
                session.root->setProperty(
                    "heading", withActions
                                   ? QStringLiteral("很长的弹窗标题 Long dialog heading 与数量标签")
                                   : QStringLiteral("切换账号"));
                for (const bool flush : {false, true}) {
                    session.root->setProperty("flushBody", flush);
                    session.root->setProperty("topInset", flush ? 26 : 24);
                    QVERIFY(QMetaObject::invokeMethod(panel, "open"));
                    QTRY_VERIFY(panel->property("opened").toBool());
                    auto *close = session.item("dialogCloseButton");
                    auto *title = session.item("dialogHeadingTitle");
                    auto *actions = session.item("dialogHeaderActions");
                    auto *body = session.item("dialogBodyContentProbe");
                    auto *footer = session.item("dialogFooter");
                    QVERIFY(close && title && actions && body && footer);
                    const auto bounds = [](QQuickItem *item) {
                        return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                    };
                    const auto right = panel->property("x").toReal() +
                                       (panel->property("width").toReal() - 24) * scale;
                    QTRY_VERIFY(qAbs(bounds(close).right() - right) < 0.5);
                    QCOMPARE(close->size(), QSizeF(32, 32));
                    QVERIFY(qAbs(bounds(close).top() -
                                 (panel->property("y").toReal() +
                                  panel->property("topPadding").toReal() * scale)) < 0.5);
                    QTRY_VERIFY2(
                        qAbs(bounds(body).right() - right) < 0.5,
                        qPrintable(QString("body right=%1 expected=%2 mode=%3 scale=%4 flush=%5")
                                       .arg(bounds(body).right())
                                       .arg(right)
                                       .arg(int(mode))
                                       .arg(scale)
                                       .arg(flush)));
                    QTRY_VERIFY(qAbs(bounds(footer).right() - right) < 0.5);
                    QTRY_VERIFY(qAbs(bounds(title).left() - bounds(body).left()) < 0.5);
                    if (withActions) {
                        QVERIFY(qAbs(bounds(actions).top() - bounds(close).top()) < 0.5);
                        QTRY_VERIFY(bounds(title).right() + 12 * scale <=
                                    bounds(actions).left() + 0.5);
                        QVERIFY(bounds(actions).right() + 8 * scale <= bounds(close).left() + 0.5);
                    } else {
                        QVERIFY(bounds(title).right() + 8 * scale <= bounds(close).left() + 0.5);
                    }
                    if (!capture.isEmpty() && !flush) {
                        QVERIFY(QDir().mkpath(capture));
                        session.window->update();
                        QTest::qWait(60);
                        QVERIFY(session.window->grabWindow().save(
                            capture + QString("/dialog-%1-%2-%3.png")
                                          .arg(mode == ThemeStore::Light ? "light" : "dark")
                                          .arg(scale)
                                          .arg(withActions)));
                    }
                    QTest::mouseClick(session.window, Qt::LeftButton, Qt::NoModifier,
                                      bounds(close).center().toPoint());
                    QTRY_VERIFY(!panel->property("visible").toBool());
                }
            }
        }
    }
    session.root->setProperty("closable", false);
    QVERIFY(QMetaObject::invokeMethod(panel, "open"));
    QTRY_VERIFY(panel->property("opened").toBool());
    QVERIFY(!session.item("dialogCloseButton")->isVisible());
    QVERIFY(QMetaObject::invokeMethod(panel, "close"));
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::drawerFixedActionsSurviveLongTitlesAndVisibilityChanges()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("DialogLayoutHarness.qml")));
    session.window->resize(980, 900);
    session.root->setProperty("showDrawer", true);
    auto *header = session.item("allocationDrawerHeader");
    auto *actions = session.item("drawerHeaderActions");
    auto *title = session.item("drawerHeadingTitle");
    auto *close = session.item("drawerCloseButton");
    QVERIFY(header && actions && title && close);
    session.theme.setMotionEnabled(false);
    const auto capture = qEnvironmentVariable("ECHO_LAYOUT_ALLOCATION_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const auto scale : {1.0, 1.25}) {
            session.root->setProperty("surfaceScale", scale);
            for (const int width : {360, 440, 720}) {
                session.root->setProperty("drawerWidth", width);
                session.root->setProperty(
                    "heading", QStringLiteral("很长的抽屉标题 Long header 中文与 Latin 字体回退"));
                for (const bool withActions : {false, true}) {
                    session.root->setProperty("withActions", withActions);
                    for (const bool loading : {false, true}) {
                        session.root->setProperty("drawerLoading", loading);
                        session.root->setProperty("secondaryAction", loading);
                        QTRY_COMPARE(actions->width(), actions->implicitWidth());
                        QTRY_VERIFY(qAbs(close->mapToItem(header, {32, 0}).x() - (width - 24)) <
                                    .5);
                        QTRY_VERIFY(title->mapToItem(header, {title->width(), 0}).x() + 12 <=
                                    actions->mapToItem(header, QPointF()).x() + .5);
                        QTRY_VERIFY(
                            qAbs(actions->mapToItem(header, {0, actions->height() / 2}).y() -
                                 (header->property("topPadding").toReal() +
                                  header->property("contentItem").value<QQuickItem *>()->height() /
                                      2)) < .5);
                        if (!withActions) {
                            QTRY_COMPARE(actions->width(), 32.);
                        } else {
                            auto *refresh = session.item("allocationDrawerRefresh");
                            QVERIFY(refresh);
                            QCOMPARE(refresh->height(), 32.);
                            auto *secondary = session.item("allocationDrawerSecondary");
                            QVERIFY(secondary);
                            QCOMPARE(secondary->isVisible(), loading);
                            auto *spinner = findVisual(refresh, "buttonSpinner");
                            QCOMPARE(spinner != nullptr, loading);
                            if (loading) {
                                QVERIFY(spinner->isVisible());
                                QTRY_VERIFY(qAbs(spinner->mapToItem(refresh, {8, 8}).y() - 16) <
                                            .5);
                            }
                        }
                    }
                }
                if (!capture.isEmpty()) {
                    session.window->update();
                    QTest::qWait(60);
                    QVERIFY(session.window->grabWindow().save(
                        capture +
                        QString("/drawer-%1-%2-%3.png").arg(int(mode)).arg(scale).arg(width)));
                }
            }
        }
    }
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::hintsAvoidTriggersAndWindowEdgesAcrossZoom()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    QQmlComponent component(&session.engine);
    component.setData(R"(
import QtQuick
import EchoMusic.Controls
Item {
    id: trigger
    width: 36
    height: 32
    transformOrigin: Item.TopLeft
    property bool requestHint: true
    property string explanation: "不明确的图标：说明操作范围及额外信息 Long explanation"
    Rectangle {
        anchors.fill: parent
        color: "#888888"
    }
    EchoToolTip {
        objectName: "placementHint"
        parent: trigger
        delay: 0
        requestedVisible: trigger.requestHint
        text: trigger.explanation
    }
})",
                      QUrl("tooltip-placement.qml"));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *trigger = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(trigger);
    trigger->setParentItem(session.window->contentItem());
    auto *tip = root->findChild<QObject *>("placementHint");
    QVERIFY(tip);
    const auto capture = qEnvironmentVariable("ECHO_TOOLTIP_CAPTURE_DIR");
    if (!capture.isEmpty())
        QVERIFY(QDir().mkpath(capture));
    for (const auto mode : {ThemeStore::Light, ThemeStore::Dark}) {
        session.theme.setMode(mode);
        for (const double zoom : {1., 1.25}) {
            session.window->setProperty("uiScale", zoom);
            trigger->setScale(zoom);
            for (const auto position :
                 {QPointF(0, 0), QPointF(720 - 36 * zoom, 0), QPointF(0, 290 - 32 * zoom),
                  QPointF(720 - 36 * zoom, 290 - 32 * zoom), QPointF(350, 130)}) {
                trigger->setPosition(position);
                QTRY_VERIFY(tip->property("visible").toBool());
                QTRY_COMPARE(tip->property("popup").value<QObject *>()->property("scale").toReal(),
                             zoom);
                auto *content = tip->property("popup")
                                    .value<QObject *>()
                                    ->property("contentItem")
                                    .value<QQuickItem *>();
                QVERIFY(content && content->parentItem());
                const auto positioned = [&] {
                    const auto painted = content->parentItem()->mapRectToScene(
                        content->parentItem()->boundingRect());
                    const auto anchor = trigger->mapRectToScene(trigger->boundingRect());
                    const QRectF usable(8 * zoom - .6, 8 * zoom - .6, 720 - 16 * zoom + 1.2,
                                        290 - 16 * zoom + 1.2);
                    return usable.contains(painted) && !painted.intersects(anchor);
                };
                QTRY_VERIFY(positioned());
                QSignalSpy frames(session.window, &QQuickWindow::frameSwapped);
                session.window->update();
                QTRY_VERIFY(!frames.isEmpty());
                if (!capture.isEmpty())
                    QVERIFY(
                        session.window->grabWindow().save(capture + QString("/hint-%1-%2-%3-%4.png")
                                                                        .arg(int(mode))
                                                                        .arg(zoom)
                                                                        .arg(position.x())
                                                                        .arg(position.y())));
            }
            // A hovered icon grows visually; the shared hint keeps the
            // window's text scale while tracking that animated trigger.
            trigger->setScale(zoom * 1.1);
            QTRY_COMPARE(tip->property("popup").value<QObject *>()->property("scale").toReal(),
                         zoom);
            trigger->setScale(zoom);
            // Dense explanations remain bounded and cannot cover the trigger.
            trigger->setProperty("explanation", QStringLiteral("长说明 Long ").repeated(100));
            QTRY_VERIFY(tip->property("visible").toBool());
            auto *content = tip->property("popup")
                                .value<QObject *>()
                                ->property("contentItem")
                                .value<QQuickItem *>();
            QTRY_VERIFY(content->property("truncated").toBool());
            const auto painted =
                content->parentItem()->mapRectToScene(content->parentItem()->boundingRect());
            QVERIFY(!painted.intersects(trigger->mapRectToScene(trigger->boundingRect())));
            trigger->setProperty("explanation", QStringLiteral("必要说明 Long explanation"));
        }
    }
    session.window->resize(220, 180);
    trigger->setPosition({175, 0});
    trigger->setProperty("explanation",
                         QStringLiteral("窄窗必要说明 Long explanation ").repeated(4));
    const auto fitsNarrowWindow = [&] {
        auto *content = tip->property("popup")
                            .value<QObject *>()
                            ->property("contentItem")
                            .value<QQuickItem *>();
        const auto rect =
            content->parentItem()->mapRectToScene(content->parentItem()->boundingRect());
        return QRectF(9.4, 9.4, 201.2, 161.2).contains(rect) &&
               !rect.intersects(trigger->mapRectToScene(trigger->boundingRect()));
    };
    QTRY_VERIFY(fitsNarrowWindow());
    if (!capture.isEmpty())
        QVERIFY(session.window->grabWindow().save(capture + "/hint-narrow.png"));
    session.window->resize(720, 290);
    trigger->setPosition({350, 130});
    QScopedPointer<QObject> secondRoot(component.create());
    QVERIFY(secondRoot);
    auto *secondTrigger = qobject_cast<QQuickItem *>(secondRoot.data());
    secondTrigger->setPosition({350, 30});
    secondTrigger->setParentItem(session.window->contentItem());
    auto *secondTip = secondRoot->findChild<QObject *>("placementHint");
    QVERIFY(secondTip);
    QTRY_VERIFY(secondTip->property("visible").toBool());
    QTRY_VERIFY(!tip->property("visible").toBool());
    secondTrigger->setProperty("requestHint", false);
    QTRY_VERIFY(!secondTip->property("visible").toBool());
    QTest::qWait(550);
    QVERIFY(!tip->property("visible").toBool());
    trigger->setProperty("requestHint", false);
    trigger->setProperty("requestHint", true);
    QTRY_VERIFY(tip->property("visible").toBool());
    trigger->setProperty("requestHint", false);
    QTRY_VERIFY(!tip->property("visible").toBool());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::popupsCancelVisibleAndPendingHints()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create());
    // A pinned player from another app can cover Cocoa's default centered
    // test window and cancel native hover after QTest injects its move.
    // Keep the real pointer and trigger in an exposed part of the work area.
    session.window->setPosition(session.window->screen()->availableGeometry().topLeft() +
                                QPoint(16, 16));
    QVERIFY(Test::prepareNativeInput(session.window));
    auto *button = session.item("iconButton");
    auto *popup = session.root->findChild<QObject *>("tooltipTestPopup");
    QVERIFY(button && popup);
    auto *defaultTip = button->findChild<QObject *>("iconButtonToolTip");
    QVERIFY(defaultTip);
    QCOMPARE(defaultTip->property("text").toString(), QString("Favorite"));
    QVERIFY(!defaultTip->property("session").value<QObject *>());
    for (auto *child : defaultTip->findChildren<QObject *>())
        QVERIFY(!child->inherits("QQmlTimer"));
    QVERIFY(!defaultTip->property("popup").value<QObject *>());
    button->setProperty("tooltipText", QStringLiteral("Favorite explanation"));
    auto *tip = button->findChild<QObject *>("iconButtonToolTip");
    QVERIFY(tip);
    auto hover = [&] {
        QVERIFY(Test::prepareNativeInput(session.window));
        Test::moveNativePointer(session.window, {10, 270});
        QTRY_VERIFY(!tip->property("requestedVisible").toBool());
        // Drain Cocoa's popup grab/enter-leave events before the next real
        // hover. QTest's injected move alone does not settle that queue.
        QTest::qWait(80);
        Test::moveNativePointer(
            session.window,
            button->mapToScene({button->width() / 2, button->height() / 2}).toPoint());
        QTRY_VERIFY(tip->property("requestedVisible").toBool());
    };
    hover();
    QVERIFY(!tip->property("popup").value<QObject *>());
    QTRY_VERIFY2(
        tip->property("visible").toBool(),
        qPrintable(QString("requested=%1 dismissed=%2 eligible=%3 has-popup=%4 hover=%5 active=%6")
                       .arg(tip->property("requestedVisible").toBool())
                       .arg(tip->property("dismissedUntilHoverEnds").toBool())
                       .arg(tip->property("eligible").toBool())
                       .arg(tip->property("popup").value<QObject *>() != nullptr)
                       .arg(button->property("hovered").toBool())
                       .arg(session.window->isActive())));
    QPointer<QObject> visibleNativeHint = tip->property("popup").value<QObject *>();
    QVERIFY(visibleNativeHint);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    QTRY_VERIFY(!tip->property("visible").toBool());
    QTRY_VERIFY(visibleNativeHint.isNull());
    QTest::qWait(600);
    QVERIFY(!tip->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY(!tip->property("visible").toBool());
    hover();
    // The new hover has requested a hint but its delay has not elapsed.
    QVERIFY(!tip->property("visible").toBool());
    QVERIFY(!tip->property("popup").value<QObject *>());
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    QTest::qWait(600);
    QVERIFY(!tip->property("visible").toBool());
    QVERIFY(!tip->property("popup").value<QObject *>());
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    hover();
    QTRY_VERIFY2(tip->property("visible").toBool(),
                 qPrintable(QString("requested=%1 dismissed=%2 eligible=%3 hover=%4 "
                                    "cursor=%5,%6 target=%7,%8 active=%9")
                                .arg(tip->property("requestedVisible").toBool())
                                .arg(tip->property("dismissedUntilHoverEnds").toBool())
                                .arg(tip->property("eligible").toBool())
                                .arg(button->property("hovered").toBool())
                                .arg(QCursor::pos().x())
                                .arg(QCursor::pos().y())
                                .arg(button->mapToGlobal({20, 18}).x())
                                .arg(button->mapToGlobal({20, 18}).y())
                                .arg(session.window->isActive())));
    QPointer<QObject> ownedHint = tip;
    button->setProperty("tooltipText", QString());
    QTRY_VERIFY(ownedHint.isNull());
    QCOMPARE(warnings.size(), 0);
}

void ControlsUiTest::popupAndPageTracksShareRightInsetAndRetainDragMapping()
{
    ControlsSession session;
    QSignalSpy warnings(&session.engine, &QQmlEngine::warnings);
    QVERIFY(session.create(QStringLiteral("PopupScrollHarness.qml")));
    auto *panel = session.root->property("panel").value<QObject *>();
    QVERIFY(panel);
    QVERIFY(QMetaObject::invokeMethod(panel, "open"));
    QTRY_VERIFY(panel->property("opened").toBool());
    auto *page = session.item("pageTrack");
    QVERIFY(page);
    auto *pageThumb = findVisual(page, "scrollThumb");
    QVERIFY(pageThumb);
    const auto pageInset = [&] {
        return page->parentItem()->width() -
               pageThumb->mapToItem(page->parentItem(), {pageThumb->width(), 0}).x();
    };
    for (int kind = 0; kind < 3; ++kind) {
        QPointer<QQuickItem> previous = kind > 0 ? session.item("popupTrack") : nullptr;
        session.root->setProperty("viewKind", kind);
        if (kind > 0)
            QTRY_VERIFY(previous.isNull());
        QQuickItem *track = nullptr;
        QTRY_VERIFY((track = session.item("popupTrack")));
        auto *thumb = findVisual(track, "scrollThumb");
        QVERIFY(thumb);
        auto *content = panel->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QCOMPARE(track->parentItem(), content->parentItem());
        for (const int padding : {0, 8, 24})
            for (const qreal scale : {0.8, 1.0, 1.5}) {
                session.root->setProperty("contentPadding", padding);
                session.root->setProperty("surfaceScale", scale);
                QTRY_VERIFY(track->height() > 0);
                QTRY_COMPARE(track->x() + track->width(), qreal(260));
                QTRY_VERIFY(qAbs(track->parentItem()->width() -
                                 thumb->mapToItem(track->parentItem(), {thumb->width(), 0}).x() -
                                 pageInset()) < 0.01);
                // The track is outside the clipped/padded Loader, and maps the
                // scrolling region's vertical bounds through resize and zoom.
                QTRY_VERIFY_WITH_TIMEOUT(qAbs(track->y() - padding) < 0.01, 1000);
                QTRY_VERIFY_WITH_TIMEOUT(qAbs(track->height() - (200 - 2 * padding)) < 0.01, 1000);
            }
        auto *view = session.root->property("scrollingView").value<QQuickItem *>();
        QVERIFY(view);
        auto *flick = kind == 2 ? view->property("contentItem").value<QQuickItem *>() : view;
        QVERIFY(flick);
        QTRY_VERIFY(track->property("size").toReal() < 1);
        QTest::qWait(40); // Complete the final layout before native hit testing.
        const auto from = thumb->mapToScene({thumb->width() / 2, thumb->height() / 2}).toPoint();
        const auto to = track->mapToScene({track->width() / 2, track->height() - 10}).toPoint();
        QTest::mousePress(session.window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(session.window, to, 20);
        QTest::mouseRelease(session.window, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_VERIFY(flick->property("contentY").toReal() > 0);
    }
    QPointer<QQuickItem> closingTrack = session.item("popupTrack");
    QVERIFY(closingTrack);
    QVERIFY(QMetaObject::invokeMethod(panel, "close"));
    QTRY_VERIFY(!closingTrack || !closingTrack->isVisible());
    // Exercise the actual shared Select/Menu too: each has its own popup
    // content model and padding, rather than the generic fixture viewport.
    for (const auto *name : {"selectPanel", "contextMenu"}) {
        if (QByteArray(name) == "selectPanel")
            QVERIFY(QMetaObject::invokeMethod(session.root.data(), "openSelect"));
        auto *actual = session.root->property(name).value<QObject *>();
        QVERIFY(actual);
        QVERIFY(QMetaObject::invokeMethod(actual, "open"));
        QTRY_VERIFY(actual->property("opened").toBool());
        QQuickItem *track = nullptr;
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *node) -> QQuickItem * {
            if (node->objectName() == "popupScrollBar" &&
                node->property("surface").value<QObject *>() == actual)
                return node;
            for (auto *child : node->childItems())
                if (auto *found = find(child))
                    return found;
            return nullptr;
        };
        QTRY_VERIFY((track = find(session.window->contentItem())));
        QTRY_VERIFY(track->property("size").toReal() < 1);
        QTRY_COMPARE(track->x() + track->width(), actual->property("width").toReal());
        QPointer<QQuickItem> closing = track;
        QVERIFY(QMetaObject::invokeMethod(actual, "close"));
        QTRY_VERIFY(!closing || !closing->isVisible());
    }
    QCOMPARE(warnings.count(), 0);
}

void ControlsUiTest::filePickerPreservesSelectionAndCancelsWithOwner()
{
    QTest::failOnWarning(QRegularExpression(".*Cannot read property.*"));
    ControlsSession session;
    QVERIFY(session.create());
    QTemporaryDir directory;
    const auto path = directory.filePath("song.flac");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(QDir(directory.path()).mkdir("child"));
    QQmlComponent component(&session.engine);
    component.setData(R"QML(
import QtQuick
import EchoMusic.Controls
Item {
    id: host
    width: 600
    height: 400
    property alias owner: owner
    property url directory
    property int mode: EchoFileDialog.OpenFiles
    property font pickerFont: Qt.font({pointSize: 17, weight: Font.DemiBold})
    property var selected: []
    property int acceptedCount: 0
    readonly property int selectedCount: selected.length
    readonly property string selectedPath: selected.length ? selected[0].toString() : ""
    EchoFileDialogLoader {
        id: owner
        hostItem: host
        sourceComponent: EchoFileDialog {
            title: "File picker regression"
            font: host.pickerFont
            currentFile: host.mode === EchoFileDialog.SaveFile ? "preselected-name" : ""
            fileMode: host.mode
            options: EchoFileDialog.DontUseNativeDialog
            currentFolder: host.directory
            defaultSuffix: "echonext-backup"
            nameFilters: ["All files (*)"]
            onAccepted: {
                host.selected = selectedFiles.slice();
                host.acceptedCount++;
            }
        }
    }
})QML",
                      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *host = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(host);
    host->setParentItem(session.window->contentItem());
    root->setProperty("directory", QUrl::fromLocalFile(directory.path()));
    auto *owner = root->property("owner").value<QObject *>();
    auto widget = []() -> QFileDialog * {
        for (auto *top : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QFileDialog *>(top); dialog && dialog->isVisible())
                return dialog;
        return nullptr;
    };
    for (int mode : {1, 2, 3}) {
        root->setProperty("mode", mode);
        QVERIFY(QMetaObject::invokeMethod(owner, "open"));
        QPointer<QObject> picker = owner->property("item").value<QObject *>();
        QVERIFY(picker);
        QTRY_VERIFY(widget());
        QPointer<QFileDialog> dialog = widget();
        QCOMPARE(dialog->windowHandle()->transientParent(), session.window);
        QCOMPARE(dialog->fileMode(), mode == 1   ? QFileDialog::ExistingFiles
                                     : mode == 2 ? QFileDialog::AnyFile
                                                 : QFileDialog::Directory);
#ifdef Q_OS_WIN
        QVERIFY(!(GetWindowLongPtrW(reinterpret_cast<HWND>(dialog->winId()), GWL_EXSTYLE) &
                  WS_EX_NOREDIRECTIONBITMAP));
#endif
        const auto chosen = mode == 2   ? directory.filePath("backup")
                            : mode == 3 ? directory.path()
                                        : path;
        qInfo() << "Picker acceptance mode" << mode;
        QVERIFY(dialog->testOption(QFileDialog::DontUseNativeDialog));
        auto *files = dialog->findChild<QFileSystemModel *>();
        QVERIFY(files);
        const auto folderIndex = files->index(directory.path());
        QTRY_VERIFY(files->rowCount(folderIndex) > 0);
        // QFileDialog::selectFile intentionally leaves an already focused
        // filename editor alone on Cocoa. Drive the real editor instead.
        QCOMPARE(dialog->font().pointSize(), 17);
        QCOMPARE(dialog->font().weight(), QFont::DemiBold);
        if (mode == 2)
            QCOMPARE(QFileInfo(dialog->selectedFiles().value(0)).completeBaseName(),
                     QString("preselected-name"));
        auto *fileName = dialog->findChild<QLineEdit *>("fileNameEdit");
        QVERIFY(fileName);
        fileName->setText(chosen);
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *acceptButton = nullptr;
        for (auto *button : buttons->buttons())
            if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                acceptButton = qobject_cast<QPushButton *>(button);
        QVERIFY(acceptButton);
        if (QGuiApplication::platformName() != "offscreen")
            QVERIFY(Test::prepareNativeInput(dialog->windowHandle()));
        QTRY_VERIFY(acceptButton->isEnabled());
        QTRY_COMPARE(dialog->selectedFiles().size(), 1);
        QTRY_COMPARE(dialog->selectedFiles().first(),
                     mode == 2 ? chosen + ".echonext-backup" : chosen);
        QSignalSpy acceptClicks(buttons, &QDialogButtonBox::accepted);
        // Fail a regression without hanging inside QFileDialog's invalid-path
        // message box. Valid selections never enter that nested event loop.
        bool unexpectedMessage = false;
        QTimer::singleShot(0, dialog, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *message = qobject_cast<QMessageBox *>(widget);
                    message && message->isVisible()) {
                    unexpectedMessage = true;
                    message->reject();
                }
        });
        QTest::mouseClick(acceptButton, Qt::LeftButton);
        QCOMPARE(acceptClicks.size(), 1);
        QVERIFY2(!unexpectedMessage,
                 qPrintable(QString("Unexpected picker message in mode %1").arg(mode)));
        QTRY_VERIFY(picker.isNull());
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(root->property("selectedCount").toInt(), 1);
        QCOMPARE(QUrl(root->property("selectedPath").toString()).toLocalFile(),
                 mode == 2 ? chosen + ".echonext-backup" : chosen);
    }
    QCOMPARE(root->property("acceptedCount").toInt(), 3);
    QVERIFY(QMetaObject::invokeMethod(owner, "open"));
    QTRY_VERIFY(widget());
    QPointer<QFileDialog> canceled = widget();
    host->setEnabled(false);
    QTRY_VERIFY(!owner->property("item").value<QObject *>());
    QTRY_VERIFY(canceled.isNull());
    QCOMPARE(root->property("acceptedCount").toInt(), 3);
    host->setEnabled(true);
    QVERIFY(QMetaObject::invokeMethod(owner, "open"));
    QTRY_VERIFY(widget());
    QPointer<QFileDialog> destroyed = widget();
    root.reset();
    QTRY_VERIFY(destroyed.isNull());
}
