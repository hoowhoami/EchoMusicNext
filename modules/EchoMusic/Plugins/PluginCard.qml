pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Plugins

Rectangle {
    id: card
    required property var record
    required property PluginManager manager
    property bool marketplace: false
    signal uninstallRequested(string installation)
    signal settingsRequested(string installation)
    signal failureRequested(string installation)
    readonly property bool busy: !!record.busy || manager.busy
    readonly property bool warning: !record.compatible || !!record.dependencyBlocked || !!record.stale
    readonly property bool currentFailure: !marketplace && !!record.error && !record.dependencyBlocked
    readonly property bool hasFailure: !marketplace && !!record.failure?.message
    readonly property bool historicalFailure: hasFailure && !currentFailure && !record.dependencyBlocked
    readonly property string titleText: record.name || record.id || qsTr("无效插件")
    readonly property color pluginAccent: {
        const palette = ["#1a73e8", "#0f9d58", "#f29900", "#d93025", "#7b1fa2", "#00897b"];
        let sum = 0;
        for (const character of Array.from(record.id || ""))
            sum += character.charCodeAt(0);
        return palette[sum % palette.length];
    }
    function countLabel(value: real): string {
        if (value >= 100000000)
            return (value / 100000000).toFixed(1).replace(/\.0$/, "") + qsTr("亿");
        if (value >= 10000)
            return (value / 10000).toFixed(1).replace(/\.0$/, "") + qsTr("万");
        return Number(value).toLocaleString(Qt.locale("zh_CN"), "f", value % 1 ? 1 : 0);
    }
    readonly property var displayTags: Array.from(new Set(record.displayTags || record.tags || []))
    implicitHeight: content.implicitHeight + 34
    radius: Theme.cardRadius
    opacity: warning || !marketplace && !record.enabled ? 0.72 : 1
    color: warning ? Theme.withAlpha("#f59e0b", 0.1) : currentFailure ? Theme.withAlpha(Theme.colors.danger, 0.1) : Theme.mutedSurface
    border.color: warning ? Theme.mix(Theme.subtleBorder, "#f59e0b", 0.34) : currentFailure ? Theme.mix(Theme.subtleBorder, Theme.colors.danger, 0.3) : "transparent"
    // Hover lifts the card without adding a border.
    transform: Translate {
        y: hover.hovered && Theme.store.motionEnabled ? -1 : 0
        Behavior on y {
            NumberAnimation {
                duration: Theme.normalDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeStandard
            }
        }
    }
    HoverHandler {
        id: hover
    }
    ColumnLayout {
        id: content
        x: 17
        y: 17
        width: card.width - 34
        height: card.height - 34
        spacing: 12
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: Theme.cardRadius
                color: image.status === Image.Ready ? "transparent" : Theme.main
                border.width: image.status === Image.Ready ? 0 : 1
                border.color: Theme.mix(Theme.subtleBorder, card.pluginAccent, 0.18)
                clip: true
                Shape {
                    id: mediaGradient
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: image.status !== Image.Ready
                    // CSS's 135-degree translucent gradient follows the rounded
                    // inner border; clipping an Item alone only clips a rectangle.
                    ShapePath {
                        strokeColor: "transparent"
                        fillGradient: LinearGradient {
                            x1: 0
                            y1: 0
                            x2: mediaGradient.width
                            y2: mediaGradient.height
                            GradientStop {
                                position: 0
                                color: Theme.withAlpha(card.pluginAccent, 0.18)
                            }
                            GradientStop {
                                position: 1
                                color: Theme.withAlpha(card.pluginAccent, 0.07)
                            }
                        }
                        startX: 9
                        startY: 0
                        PathLine {
                            x: mediaGradient.width - 9
                            y: 0
                        }
                        PathArc {
                            x: mediaGradient.width
                            y: 9
                            radiusX: 9
                            radiusY: 9
                        }
                        PathLine {
                            x: mediaGradient.width
                            y: mediaGradient.height - 9
                        }
                        PathArc {
                            x: mediaGradient.width - 9
                            y: mediaGradient.height
                            radiusX: 9
                            radiusY: 9
                        }
                        PathLine {
                            x: 9
                            y: mediaGradient.height
                        }
                        PathArc {
                            x: 0
                            y: mediaGradient.height - 9
                            radiusX: 9
                            radiusY: 9
                        }
                        PathLine {
                            x: 0
                            y: 9
                        }
                        PathArc {
                            x: 9
                            y: 0
                            radiusX: 9
                            radiusY: 9
                        }
                    }
                }
                EchoCover {
                    id: image
                    anchors.fill: parent
                    source: card.record.iconUrl || ""
                    cornerRadius: Theme.cardRadius
                    placeholderOpacity: 0
                    placeholderColor: "transparent"
                }
                Label {
                    anchors.centerIn: parent
                    visible: image.status !== Image.Ready
                    text: (Array.from(card.titleText.trim())[0] || "E").toUpperCase()
                    font: Theme.font(21.6, Theme.extraBoldWeight)
                    color: card.pluginAccent
                }
            }
            ColumnLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 5.6
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    Layout.minimumHeight: 22
                    spacing: 12
                    EchoTextTagRow {
                        id: title
                        objectName: "pluginCardTitle"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: card.titleText
                        font: Theme.font(14, Theme.boldWeight)
                        color: Theme.text
                        HoverHandler {
                            id: titleHover
                            parent: title
                        }
                        EchoToolTip {
                            parent: title
                            requestedVisible: titleHover.hovered && title.textItem.text !== title.text
                            text: card.titleText
                        }
                    }
                    EchoTag {
                        objectName: "pluginCardStatus"
                        visible: !!card.record.status && (!card.marketplace || card.warning || card.record.installed || card.record.updateAvailable)
                        text: card.record.status || ""
                        size: EchoTag.Small
                        tone: card.warning || card.record.status === qsTr("安全模式") || card.record.updateAvailable ? EchoTag.Custom : card.currentFailure ? EchoTag.Custom : card.record.status === qsTr("运行中") ? EchoTag.Accent : EchoTag.Neutral
                        customTone: card.currentFailure && !card.warning ? Theme.colors.danger : "#f59e0b"
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: card.record.author || ""
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.secondaryText
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.maximumWidth: card.width - 88
                        text: card.marketplace && card.record.installed ? card.record.updateAvailable ? "v" + card.record.installedVersion + " → v" + card.record.version : "v" + card.record.installedVersion : "v" + (card.record.version || "—")
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.secondaryText
                        elide: Text.ElideRight
                    }
                }
            }
        }
        Label {
            id: descriptionLabel
            Layout.fillWidth: true
            Layout.minimumHeight: 42
            Layout.preferredHeight: 42
            text: card.record.description || qsTr("暂无描述")
            font: Theme.font(13, Theme.regularWeight)
            color: Theme.secondaryText
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeightMode: Text.FixedHeight
            lineHeight: 20.8
            HoverHandler {
                id: descriptionHover
            }
            EchoToolTip {
                parent: descriptionLabel
                requestedVisible: descriptionHover.hovered && descriptionLabel.truncated
                text: descriptionLabel.text
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: warningRow.implicitHeight + 16
            visible: card.warning && (!!card.record.error || !!card.record.refreshError)
            objectName: "pluginCardWarning"
            radius: Theme.cardRadius
            color: Theme.withAlpha("#f59e0b", 0.08)
            border.color: Theme.withAlpha("#f59e0b", 0.18)
            EchoButtonContent {
                id: warningRow
                objectName: "pluginCardWarningContent"
                x: 8
                y: 8
                width: parent.width - 16
                height: implicitHeight
                spacing: 8
                horizontalAlignment: Qt.AlignLeft
                glyph: "alert-triangle"
                glyphSize: 14
                wrap: true
                glyphAtFirstLine: true
                text: card.record.stale ? qsTr("来源暂时不可用，显示上次成功获取的版本。") : card.record.error || ""
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.dark ? "#fbbf24" : "#b45309"
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 8
            visible: !card.marketplace && (card.record.windows || []).length > 0
            Repeater {
                model: card.marketplace ? [] : card.record.windows || []
                delegate: EchoSoftButton {
                    required property var modelData
                    objectName: "pluginOpenWindow-" + modelData.id
                    text: modelData.title || modelData.id
                    enabled: card.record.active && !card.busy
                    onClicked: card.manager.runtime.openWindow(card.record.installationId, modelData.id)
                }
            }
        }
        Flow {

            Layout.fillWidth: true
            Layout.minimumHeight: 24
            Layout.preferredHeight: Math.max(24, implicitHeight)
            spacing: 6
            visible: card.displayTags.length > 0
            Repeater {
                model: card.displayTags
                delegate: EchoTag {
                    required property string modelData
                    width: Math.min(implicitWidth, 144)
                    text: modelData
                    size: EchoTag.Small
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: details.implicitHeight
            ColumnLayout {
                id: details
                width: parent.width
                anchors.bottom: parent.bottom
                spacing: 8
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        id: idLabel
                        Layout.preferredHeight: 16.5
                        objectName: "pluginCardId"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: "ID: " + (card.record.id || "")
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.secondaryText
                        elide: Text.ElideRight
                        HoverHandler {
                            id: idHover
                        }
                        EchoToolTip {
                            parent: idLabel
                            requestedVisible: idHover.hovered && idLabel.truncated
                            text: idLabel.text
                        }
                    }
                    Label {
                        id: sourceLabel
                        Layout.preferredHeight: 16.5
                        objectName: "pluginCardSource"
                        Layout.maximumWidth: content.width * 0.55
                        text: qsTr("来源: ") + (card.record.sourceName || qsTr("本地安装"))
                        font: Theme.font(11, Theme.regularWeight)
                        color: Theme.secondaryText
                        elide: Text.ElideRight
                        HoverHandler {
                            id: sourceHover
                        }
                        EchoToolTip {
                            parent: sourceLabel
                            requestedVisible: sourceHover.hovered && sourceLabel.truncated
                            text: sourceLabel.text
                        }
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    visible: card.marketplace
                    spacing: 5
                    Repeater {
                        model: [
                            {
                                icon: "arrow-bar-to-down",
                                label: qsTr("安装和更新总量"),
                                value: card.record.stats ? card.record.stats.installCount + card.record.stats.updateCount : -1
                            },
                            {
                                icon: "refresh",
                                label: qsTr("更新量"),
                                value: card.record.stats ? card.record.stats.updateCount : -1
                            },
                            {
                                icon: "heartbeat",
                                label: qsTr("热度"),
                                value: card.record.stats ? card.record.stats.score : -1
                            }
                        ]
                        delegate: Rectangle {
                            id: stat
                            required property var modelData
                            objectName: "pluginStat-" + modelData.icon
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            implicitHeight: 26
                            radius: Theme.itemRadius
                            color: Theme.withAlpha(Theme.mutedSurface, Theme.mutedSurface.a * 0.82)
                            EchoButtonContent {
                                anchors.fill: parent
                                anchors.margins: 6
                                text: stat.modelData.value < 0 ? "—" : card.countLabel(stat.modelData.value)
                                glyph: stat.modelData.icon
                                glyphSize: 13
                                spacing: 5
                                font: Theme.font(11, Theme.boldWeight)
                                color: Theme.secondaryText
                                iconColor: Theme.mix(Theme.secondaryText, Theme.accentText, 0.72)
                            }
                            HoverHandler {
                                id: statHover
                            }
                            EchoToolTip {
                                parent: stat
                                requestedVisible: statHover.hovered
                                text: stat.modelData.label
                            }
                        }
                    }
                }
            }
        }
        Item {
            objectName: "pluginCardActions"
            Layout.fillWidth: true
            implicitHeight: 47
            Layout.topMargin: 2
            Rectangle {
                objectName: "pluginCardDivider"
                width: parent.width
                height: 1
                color: Theme.subtleBorder
            }
            RowLayout {
                width: parent.width
                y: 11
                spacing: 8
                PluginActionButton {
                    tooltipText: card.marketplace ? "" : card.record.settingsAvailable ? "" : !card.record.compatible ? qsTr("插件与当前主程序版本不兼容，无法加载设置") : !card.record.enabled ? qsTr("启用插件后可读取设置项") : card.manager.runtime.safeMode ? qsTr("安全模式下不会加载插件设置") : card.record.status !== qsTr("运行中") ? qsTr("插件运行后可读取设置项") : qsTr("该插件没有提供设置项")
                    text: card.marketplace ? qsTr("分享") : qsTr("设置")
                    glyph: card.marketplace ? "share" : "settings"
                    foreground: !card.marketplace && !card.record.settingsAvailable ? Theme.withAlpha(Theme.secondaryText, 0.78) : colors.text
                    enabled: !card.busy
                    onClicked: card.marketplace ? card.manager.share(card.record.key) : card.settingsRequested(card.record.installationId)
                }
                PluginActionButton {
                    text: card.marketplace ? qsTr("仓库") : qsTr("移除")
                    glyph: card.marketplace ? "external-link" : ""
                    danger: !card.marketplace
                    enabled: !card.busy
                    onClicked: card.marketplace ? card.manager.openRepository(card.record.key) : card.uninstallRequested(card.record.installationId)
                }
                EchoIconButton {
                    visible: !card.marketplace
                    glyph: "folder-open"
                    text: qsTr("打开插件目录")
                    enabled: !card.busy
                    onClicked: card.manager.runtime.openPluginDirectory(card.record.installationId)
                }
                EchoIconButton {
                    id: failureButton
                    visible: card.hasFailure
                    implicitWidth: 36
                    implicitHeight: 36
                    padding: 11
                    glyph: "alert-triangle"
                    text: card.historicalFailure ? qsTr("查看插件历史异常") : qsTr("查看插件异常详情")
                    tooltipText: text
                    foreground: hovered ? "white" : card.historicalFailure || card.warning ? "#f59e0b" : Theme.colors.danger
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: failureButton.hovered ? card.historicalFailure || card.warning ? "#f59e0b" : Theme.colors.danger : Theme.withAlpha(card.historicalFailure || card.warning ? "#f59e0b" : Theme.colors.danger, 0.1)
                    }
                    onClicked: card.failureRequested(card.record.installationId)
                }
                Item {
                    Layout.fillWidth: true
                }
                PluginActionButton {
                    objectName: "pluginMarketplaceInstall"
                    visible: card.marketplace
                    implicitWidth: Math.max(64, Math.ceil(contentItem.implicitWidth + leftPadding + rightPadding))
                    Layout.minimumWidth: implicitWidth
                    accent: true
                    glyph: card.record.installed && !card.record.updateAvailable ? "circle-check" : "arrow-bar-to-down"
                    loading: !!card.record.busy
                    text: card.record.installLabel || qsTr("安装")
                    enabled: card.record.compatible && !card.busy && (!card.record.installed || card.record.updateAvailable)
                    onClicked: card.manager.install(card.record.key)
                }
                EchoSwitch {
                    visible: !card.marketplace
                    checked: !!card.record.enabled
                    enabled: card.record.compatible && !card.busy && !card.manager.runtime.loading
                    Accessible.name: qsTr("启用 %1").arg(card.titleText)
                    onToggled: card.manager.runtime.setEnabled(card.record.installationId, checked)
                }
            }
        }
    }
}
