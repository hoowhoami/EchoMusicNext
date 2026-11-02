pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Plugins

EchoDialog {
    id: dialog
    objectName: "pluginNetworkErrorDialog"
    property var failure: ({})
    signal networkSettingsRequested
    heading: qsTr("加载失败详情")
    preferredWidth: 720
    maximumHeight: 760
    windowHeightMargin: 80
    flushBody: true
    readonly property var metadata: {
        const rows = [qsTr("错误类型"), failure.categoryLabel || qsTr("加载失败"), qsTr("插件源"), failure.sourceName || "—", qsTr("发生时间"), failure.failedAt ? new Date(failure.failedAt).toLocaleString(Qt.locale(), "yyyy/M/d HH:mm:ss") : "—", qsTr("请求地址"), failure.url || "—"];
        if (failure.originUrl)
            rows.push(qsTr("原始地址"), failure.originUrl);
        if (failure.httpStatus)
            rows.push(qsTr("响应状态"), "HTTP " + failure.httpStatus);
        return rows;
    }
    function describe(value: var): string {
        const lines = [];
        if (value.pluginId)
            lines.push(value.pluginId);
        if (value.categoryLabel)
            lines.push(value.categoryLabel);
        if (value.url)
            lines.push(value.url);
        if (value.httpStatus)
            lines.push("HTTP " + value.httpStatus);
        if (value.message)
            lines.push(value.message);
        return lines.join("\n");
    }
    readonly property string rawDetails: {
        const sections = [failure.message || failure.title || ""];
        if (failure.failedCount > 1)
            sections.push(qsTr("本次共有 %1 项失败，最多列出前 16 项详情。").arg(failure.failedCount));
        for (const item of failure.attempts || [])
            sections.push(qsTr("连接尝试") + "\n" + describe(item));
        for (const item of failure.failures || [])
            sections.push(describe(item));
        return sections.join("\n\n");
    }
    body: Flickable {
        id: viewport
        implicitHeight: details.implicitHeight
        contentWidth: width
        contentHeight: details.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: EchoPopupScrollBar {
            surface: dialog
            view: viewport
            resourceActive: dialog.visible
        }
        ColumnLayout {
            id: details
            width: viewport.width - 24
            spacing: 16
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: information.implicitHeight + 36
                radius: Theme.controlRadius
                color: Theme.floating.mutedSurface
                border.color: Theme.floating.subtleBorder
                GridLayout {
                    id: information
                    x: 18
                    y: 18
                    width: parent.width - 36
                    columns: 2
                    columnSpacing: 18
                    rowSpacing: 10
                    Repeater {
                        model: dialog.metadata
                        delegate: Label {
                            required property int index
                            required property string modelData
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: index % 2 ? -1 : 78
                            Layout.fillWidth: index % 2 === 1
                            Layout.minimumWidth: 0
                            text: modelData
                            textFormat: Text.PlainText
                            font: Theme.font(14, Theme.boldWeight)
                            color: index % 2 ? Theme.floatingText : Theme.floatingSecondaryText
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: reason.implicitHeight + 36
                radius: Theme.controlRadius
                color: Theme.floating.mutedSurface
                border.color: Theme.floating.subtleBorder
                ColumnLayout {
                    id: reason
                    x: 18
                    y: 18
                    width: parent.width - 36
                    spacing: 10
                    Label {
                        text: qsTr("可能的原因")
                        font: Theme.font(14, Theme.boldWeight)
                        color: Theme.floatingText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: dialog.failure.hint || dialog.failure.title || ""
                        textFormat: Text.PlainText
                        font: Theme.font(14, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        wrapMode: Text.Wrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 22
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: original.implicitHeight + 36
                radius: Theme.controlRadius
                color: Theme.floating.mutedSurface
                border.color: Theme.floating.subtleBorder
                ColumnLayout {
                    id: original
                    x: 18
                    y: 18
                    width: parent.width - 36
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("原始错误信息")
                            font: Theme.font(14, Theme.boldWeight)
                            color: Theme.floatingText
                        }
                        PluginActionButton {
                            objectName: "pluginNetworkErrorCopy"
                            text: qsTr("复制")
                            glyph: "copy"
                            colors: Theme.floating
                            onClicked: {
                                raw.selectAll();
                                raw.copy();
                                raw.deselect();
                            }
                        }
                    }
                    TextEdit {
                        id: raw
                        objectName: "pluginNetworkRawError"
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        text: dialog.rawDetails
                        textFormat: TextEdit.PlainText
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        wrapMode: TextEdit.WrapAnywhere
                    }
                }
            }
        }
    }
    footerContent: RowLayout {
        PluginActionButton {
            objectName: "pluginNetworkOpenSettings"
            text: qsTr("打开网络设置")
            colors: Theme.floating
            onClicked: {
                dialog.close();
                dialog.networkSettingsRequested();
            }
        }
        Item {
            Layout.fillWidth: true
        }
        PluginActionButton {
            objectName: "pluginNetworkErrorClose"
            text: qsTr("关闭")
            colors: Theme.floating
            onClicked: dialog.close()
        }
    }
}
