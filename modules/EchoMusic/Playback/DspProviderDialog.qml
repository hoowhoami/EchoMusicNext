pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

EchoDialog {
    id: manager
    required property PlaybackAudioControls audio
    objectName: "dspProviderDialog"
    heading: qsTr("音效引擎")
    preferredWidth: 520
    flushBody: true
    function capabilities(details: var): list<string> {
        const manifest = details.manifest || {};
        const result = (manifest.resources || []).slice(0, 64).map(resource => {
            const kind = resource.kind.trim().toLowerCase();
            const name = kind === "vpf" ? qsTr("VPF 参数音效") : kind === "impulse-response" ? qsTr("脉冲响应") : resource.kind;
            return name + ((resource.extensions || []).length ? " " + resource.extensions.join(" / ") : "");
        });
        if ((manifest.presets || []).length)
            result.push(qsTr("%1 个预设").arg(manifest.presets.length));
        if ((manifest.controls || []).length)
            result.push(qsTr("%1 项可调参数").arg(manifest.controls.length));
        if (details.maxChannels > 0)
            result.push(qsTr("最高 %1 声道").arg(details.maxChannels));
        result.push(details.latencyFrames > 0 ? qsTr("%1 帧延迟").arg(details.latencyFrames) : qsTr("零额外延迟"));
        return result;
    }
    onAboutToHide: filePickerOwner.close()
    body: ColumnLayout {
        spacing: 10
        EchoBoundedListView {
            id: engines
            maximumViewportHeight: manager.bodyHeightLimit
            objectName: "dspProviderList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: manager.audio.providers.count > 0
            clip: true
            reuseItems: true
            cacheBuffer: 200
            spacing: 10
            topMargin: 2
            bottomMargin: 2
            model: manager.audio.providers
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: manager
                view: engines
                resourceActive: manager.visible
            }
            delegate: Item {
                id: row
                required property var details
                width: engines.width
                height: card.implicitHeight + 28
                Rectangle {
                    objectName: "dspProviderCard"
                    anchors.fill: parent
                    // EchoDialog already supplies the left content inset.
                    // The flush viewport only needs the missing right inset.
                    anchors.leftMargin: 2
                    anchors.rightMargin: 28
                    radius: Theme.cardRadius
                    color: row.details.id === manager.audio.providerId ? Theme.withAlpha(Theme.floating.accent, 0.07) : Theme.floating.mutedSurface
                    border.width: 1
                    border.color: row.details.id === manager.audio.providerId ? Theme.floating.accent : Theme.withAlpha(Theme.floating.text, 0.11)
                }
                ColumnLayout {
                    id: card
                    x: 16
                    y: 14
                    width: parent.width - 58
                    spacing: 0
                    RowLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillHeight: false
                            Layout.fillWidth: true
                            spacing: 3
                            EchoTextTagRow {
                                Layout.fillWidth: true
                                text: row.details.name
                                font: Theme.font(14, Theme.boldWeight)
                                lineHeight: 21
                                color: Theme.floating.text
                                tags: EchoTag {
                                    text: qsTr("使用中")
                                    visible: row.details.id === manager.audio.providerId
                                    tone: EchoTag.Accent
                                    colors: Theme.floating
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: row.details.id + " · v" + row.details.version
                                font: Theme.font(10, 550)
                                lineHeightMode: Text.FixedHeight
                                lineHeight: 15
                                color: Theme.floating.secondaryText
                                elide: Text.ElideRight
                            }
                        }
                        EchoIconButton {
                            id: deleteAction
                            glyph: "trash"
                            text: qsTr("删除音效引擎")
                            implicitWidth: 28
                            implicitHeight: 28
                            padding: 6.5
                            colors: Theme.floating
                            foreground: hovered ? Theme.floating.dangerText : Theme.floating.secondaryText
                            background: Rectangle {
                                radius: Theme.controlRadius
                                color: deleteAction.hovered ? Theme.withAlpha(Theme.floating.dangerText, .10) : "transparent"
                                border.width: deleteAction.visualFocus ? 2 : 0
                                border.color: Theme.floating.accent
                            }
                            enabled: !manager.audio.providers.busy && !manager.audio.effectPending
                            onClicked: manager.audio.removeProvider(row.details.id)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        visible: text.length > 0
                        text: row.details.description
                        font: Theme.font(11, Theme.regularWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 16.5
                        color: Theme.floating.secondaryText
                        wrapMode: Text.Wrap
                    }
                    Flow {

                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        spacing: 6
                        Repeater {
                            // Metadata has a bounded number of resource declarations,
                            // independent of the virtualized provider catalog.
                            model: manager.capabilities(row.details)
                            delegate: EchoTag {
                                required property string modelData
                                text: modelData
                                tone: EchoTag.Muted
                                colors: Theme.floating
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 13
                        implicitHeight: 1
                        color: Theme.withAlpha(Theme.floating.text, 0.08)
                    }
                    RowLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        Layout.topMargin: 11
                        spacing: 12
                        Label {
                            id: providerFileName
                            Layout.fillWidth: true
                            text: row.details.fileName
                            font: Theme.font(10, 550)
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 15
                            color: Theme.floating.secondaryText
                            elide: Text.ElideMiddle
                            EchoToolTip {
                                parent: providerFileName
                                text: row.details.path
                                requestedVisible: pathHover.hovered
                            }
                            HoverHandler {
                                id: pathHover
                            }
                        }
                        EchoButton {
                            size: EchoControlSize.Compact
                            accent: false
                            colors: Theme.floating
                            text: row.details.id === manager.audio.providerId ? qsTr("停用") : qsTr("启用")
                            enabled: !manager.audio.effectPending && !manager.audio.providers.busy
                            onClicked: row.details.id === manager.audio.providerId ? manager.audio.deactivateProvider() : manager.audio.activateProvider(row.details.id)
                        }
                    }
                }
            }
        }
        Label {
            id: emptyState
            objectName: "dspProviderEmptyState"
            Layout.fillWidth: true
            Layout.rightMargin: 24
            visible: manager.audio.providers.count === 0
            text: qsTr("暂无已安装的音效引擎")
            padding: 24
            horizontalAlignment: Text.AlignHCenter
            font: Theme.font(12, Theme.regularWeight)
            color: Theme.floating.secondaryText
            background: Shape {
                ShapePath {
                    strokeWidth: 1
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [4, 4]
                    strokeColor: Theme.withAlpha(Theme.floating.text, 0.16)
                    fillColor: "transparent"
                    PathRectangle {
                        x: 0.5
                        y: 0.5
                        width: Math.max(0, emptyState.width - 1)
                        height: Math.max(0, emptyState.height - 1)
                        radius: Theme.controlRadius
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.rightMargin: 24
            visible: text.length > 0
            text: manager.audio.providers.errorMessage || manager.audio.errorMessage
            color: Theme.floating.dangerText
            font: Theme.font(12, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
    }
    footerContent: Item {
        implicitHeight: importAction.implicitHeight
        EchoButton {
            id: importAction
            objectName: "importDspProviderAction"
            anchors.right: parent.right
            size: EchoControlSize.Standard
            colors: Theme.floating
            text: manager.audio.providers.busy ? qsTr("正在导入…") : qsTr("导入音效引擎")
            enabled: !manager.audio.providers.busy && !manager.audio.effectPending
            onClicked: filePickerOwner.open()
        }
    }
    EchoFileDialogLoader {
        id: filePickerOwner
        hostItem: manager.contentItem
        sourceComponent: Component {
            EchoFileDialog {
                id: filePicker
                title: qsTr("导入音效引擎")
                parentWindow: manager.parent ? manager.parent.Window.window : null
                nameFilters: Qt.platform.os === "windows" ? [qsTr("音效引擎 (*.dll)")] : Qt.platform.os === "osx" ? [qsTr("音效引擎 (*.dylib)")] : [qsTr("音效引擎 (*.so)")]
                onAccepted: manager.audio.providers.importFile(selectedFile)
                onVisibleChanged: if (visible)
                    EchoPopupCoordinator.popupOpening(null, manager.parent)
            }
        }
    }
}
