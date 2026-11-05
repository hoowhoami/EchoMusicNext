pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Plugins

RoutePage {
    id: page
    objectName: "pluginShareResolvePage"
    required property PluginManager manager
    readonly property string resolveState: manager.shareState
    readonly property bool working: manager.busy || manager.catalogLoading || manager.runtime.loading
    // Match the source media query against the scaled application viewport.
    readonly property bool narrow: (Window.window ? Window.window.width / settings.uiScale : width) <= 560
    readonly property bool resourcesActive: pageActive && context.name === "plugin-share-resolve" && visible && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    function management() {
        manager.search = manager.shareTarget.id || "";
        manager.sourceFilter = manager.shareTarget.sourceId || "";
        router.replace("plugin-management", {
            view: "marketplace"
        });
    }
    function syncRedirect() {
        if (resourcesActive && resolveState === "installed" && !working) {
            if (!redirect.running)
                redirect.start();
        } else {
            redirect.stop();
        }
    }
    onResourcesActiveChanged: syncRedirect()
    onWorkingChanged: syncRedirect()
    Component.onCompleted: syncRedirect()
    // Scope the source's 700ms redirect to this visible route. A new share,
    // leaving the page, hiding the window or destruction cancels the old wait.
    Timer {
        id: redirect
        objectName: "pluginShareInstalledRedirect"
        interval: 700
        onTriggered: {
            if (page.resourcesActive && page.resolveState === "installed" && !page.working)
                page.management();
        }
    }
    Connections {
        target: page.manager
        function onShareOpened() {
            redirect.stop();
        }
        function onChanged() {
            page.syncRedirect();
        }
    }
    Flickable {
        id: viewport
        objectName: "pluginShareViewport"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, panel.y + panel.height + 72)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: EchoPageScrollBar {
            view: viewport
        }
        Item {
            id: panel
            objectName: "pluginSharePanel"
            width: Math.max(0, Math.min(680, viewport.width - 48))
            height: content.implicitHeight + 56
            x: (viewport.width - width) / 2
            y: 40 + Math.max(0, (viewport.height - 112 - height) / 2)
            EchoFloatingSurface {
                anchors.fill: parent
                radius: Theme.controlRadius
                color: Theme.card
                border.color: Theme.border
            }
            ColumnLayout {
                id: content
                x: 28
                y: 28
                width: Math.max(0, parent.width - 56)
                spacing: 0
                EchoTag {
                    text: qsTr("插件分享")
                    tone: EchoTag.Accent
                    implicitHeight: 30
                    contentComponent: Component {
                        Label {
                            textFormat: Text.PlainText
                            text: qsTr("插件分享")
                            font: Theme.font(12, Theme.extraBoldWeight)
                            color: Theme.accentText
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 20
                    columns: page.narrow ? 1 : 2
                    columnSpacing: 16
                    rowSpacing: 16
                    Rectangle {
                        implicitWidth: 64
                        implicitHeight: 64
                        Layout.alignment: Qt.AlignTop
                        readonly property color statusColor: page.resolveState === "failed" ? Theme.colors.warning : page.resolveState === "installed" ? "#0f9d58" : Theme.accentText
                        radius: Theme.controlRadius
                        color: Theme.withAlpha(page.resolveState === "installed" ? "#0f9d58" : page.resolveState === "failed" ? Theme.colors.warning : Theme.accent, 0.12)
                        border.color: Theme.withAlpha(page.resolveState === "installed" ? "#0f9d58" : page.resolveState === "failed" ? Theme.colors.warning : Theme.accent, 0.22)
                        EchoSpinner {
                            anchors.centerIn: parent
                            visible: page.working || page.resolveState === "loading"
                            resourceActive: visible && page.resourcesActive
                            width: 30
                            height: 30
                        }
                        EchoIcon {
                            anchors.centerIn: parent
                            visible: !page.working && page.resolveState !== "loading"
                            width: 30
                            height: 30
                            name: page.resolveState === "failed" ? "alert-triangle" : page.resolveState === "installed" ? "check" : "puzzle"
                            color: parent.statusColor
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        spacing: 10
                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: page.manager.busy ? page.manager.shareRecord.updateAvailable ? qsTr("正在更新插件") : qsTr("正在安装插件") : page.manager.catalogLoading ? qsTr("正在处理插件源") : page.resolveState === "need-source" ? qsTr("需要添加插件源") : page.resolveState === "ready" ? page.manager.shareRecord.updateAvailable ? qsTr("更新分享的插件") : qsTr("安装分享的插件") : page.resolveState === "installed" ? qsTr("插件已安装") : page.resolveState === "failed" ? qsTr("分享的插件打不开") : qsTr("正在打开分享的插件")
                            font: Theme.font(26, Theme.extraBoldWeight)
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }
                        Label {
                            textFormat: Text.PlainText
                            objectName: "pluginShareStatusDescription"
                            Layout.fillWidth: true
                            text: page.manager.busy ? qsTr("安装完成后会自动刷新插件运行时并回到插件管理页。") : page.manager.catalogLoading ? qsTr("完成后会继续查找分享的插件。") : page.resolveState === "failed" ? page.manager.shareError : page.resolveState === "need-source" ? qsTr("分享的插件来自尚未添加或已停用的插件源。确认添加或启用后，将继续查找插件。") : page.resolveState === "installed" ? qsTr("已完成安装，即将回到插件管理页。") : page.resolveState === "ready" ? qsTr("来源：%1 · 将安装 v%2").arg(page.manager.shareRecord.sourceName || page.manager.shareSourceUrl).arg(page.manager.shareRecord.version || "") : qsTr("正在读取插件源和插件清单。")
                            font: Theme.font(14, Theme.regularWeight)
                            color: Theme.secondaryText
                            wrapMode: Text.Wrap
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 23.8
                        }
                    }
                }
                Rectangle {
                    objectName: "pluginSharePlugin"
                    visible: !!page.manager.shareRecord.id
                    Layout.fillWidth: true
                    Layout.topMargin: 20
                    implicitHeight: pluginDetails.implicitHeight + 28
                    radius: Theme.controlRadius
                    color: Theme.mutedSurface
                    border.color: Theme.border
                    GridLayout {
                        id: pluginDetails
                        x: 14
                        y: 14
                        width: Math.max(0, parent.width - 28)
                        columns: page.narrow ? 1 : 2
                        columnSpacing: 16
                        rowSpacing: 16
                        Rectangle {
                            implicitWidth: 56
                            implicitHeight: 56
                            Layout.alignment: Qt.AlignTop
                            radius: Theme.controlRadius
                            color: Theme.withAlpha("#1a73e8", 0.12)
                            PluginIconImage {
                                id: icon
                                objectName: "pluginShareIcon"
                                anchors.fill: parent
                                manager: page.manager
                                iconSource: page.manager.shareRecord.iconUrl || ""
                                resourceActive: page.resourcesActive
                                cornerRadius: Theme.controlRadius
                                placeholderOpacity: 0
                                placeholderColor: "transparent"
                            }
                            Label {
                                textFormat: Text.PlainText
                                objectName: "pluginShareIconFallback"
                                anchors.centerIn: parent
                                visible: icon.status !== Image.Ready
                                text: (Array.from((page.manager.shareRecord.name || "").trim())[0] || "E").toUpperCase()
                                font: Theme.font(21.6, Theme.extraBoldWeight)
                                color: "#1a73e8"
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: false
                            spacing: 10
                            Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: page.manager.shareRecord.name || ""
                                font: Theme.font(15, Theme.extraBoldWeight)
                                color: Theme.text
                                wrapMode: Text.Wrap
                            }
                            Label {
                                textFormat: Text.PlainText
                                objectName: "pluginShareDescription"
                                Layout.fillWidth: true
                                text: page.manager.shareRecord.description || qsTr("暂无描述")
                                font: Theme.font(14, Theme.regularWeight)
                                color: Theme.secondaryText
                                wrapMode: Text.Wrap
                                lineHeightMode: Text.FixedHeight
                                lineHeight: 23.8
                            }
                        }
                    }
                }
                Rectangle {
                    objectName: "pluginShareWarning"
                    visible: !!page.manager.shareRecord.shareWarning
                    Layout.fillWidth: true
                    Layout.topMargin: 14
                    implicitHeight: warning.implicitHeight + 20
                    radius: Theme.controlRadius
                    color: Theme.withAlpha("#f59e0b", 0.08)
                    border.color: Theme.withAlpha("#f59e0b", 0.18)
                    EchoButtonContent {
                        id: warning
                        objectName: "pluginShareWarningContent"
                        x: 12
                        y: 10
                        width: Math.max(0, parent.width - 24)
                        height: implicitHeight
                        horizontalAlignment: Qt.AlignLeft
                        glyph: "alert-triangle"
                        glyphSize: 15
                        glyphAtFirstLine: true
                        spacing: 8
                        text: page.manager.shareRecord.shareWarning || ""
                        font: Theme.font(12, Theme.boldWeight)
                        lineHeight: 18.6
                        wrap: true
                        color: Theme.dark ? "#fbbf24" : "#b45309"
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    implicitHeight: details.implicitHeight + 28
                    radius: Theme.controlRadius
                    color: Theme.mutedSurface
                    GridLayout {
                        id: details
                        x: 14
                        y: 14
                        width: Math.max(0, parent.width - 28)
                        columns: page.narrow ? 1 : 2
                        columnSpacing: 14
                        rowSpacing: 8
                        Label {
                            textFormat: Text.PlainText
                            Layout.preferredWidth: page.narrow ? -1 : 72
                            text: qsTr("插件 ID")
                            font: Theme.font(12, Theme.extraBoldWeight)
                            color: Theme.secondaryText
                        }
                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: page.manager.shareTarget.id || ""
                            font: Theme.font(12, Theme.boldWeight)
                            color: Theme.text
                            wrapMode: Text.WrapAnywhere
                        }
                        Label {
                            textFormat: Text.PlainText
                            Layout.preferredWidth: page.narrow ? -1 : 72
                            text: qsTr("插件源")
                            font: Theme.font(12, Theme.extraBoldWeight)
                            color: Theme.secondaryText
                        }
                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: page.manager.shareSourceUrl
                            font: Theme.font(12, Theme.boldWeight)
                            color: Theme.text
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 22
                    Layout.preferredHeight: implicitHeight
                    spacing: 10
                    EchoButton {
                        visible: page.resolveState === "need-source"
                        text: qsTr("添加或启用插件源")
                        size: EchoControlSize.Standard
                        enabled: !page.working
                        onClicked: page.manager.acceptShareSource()
                    }
                    EchoButton {
                        objectName: "pluginShareInstall"
                        visible: page.resolveState === "ready"
                        text: page.manager.shareRecord.updateAvailable ? qsTr("更新插件") : qsTr("安装插件")
                        size: EchoControlSize.Standard
                        enabled: !page.working && !!page.manager.shareRecord.compatible
                        onClicked: page.manager.installShared()
                    }
                    EchoButton {
                        visible: page.resolveState === "failed"
                        text: qsTr("重新打开")
                        glyph: "refresh"
                        size: EchoControlSize.Standard
                        enabled: !page.working
                        onClicked: page.manager.refreshCatalog(true)
                    }
                    EchoButton {
                        accent: false
                        text: qsTr("插件管理")
                        glyph: "arrow-left"
                        size: EchoControlSize.Standard
                        enabled: !page.working
                        onClicked: page.management()
                    }
                }
            }
        }
    }
}
