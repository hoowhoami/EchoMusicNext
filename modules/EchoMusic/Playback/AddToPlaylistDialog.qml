pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

EchoDialog {
    id: dialog
    objectName: "addToPlaylistDialog"
    required property PlaybackLibraryActions actions
    heading: qsTr("添加到")
    preferredWidth: 420
    readonly property real targetSpacing: 10
    // Resolve the intrinsic extent outside the ListView/layout construction.
    readonly property real targetViewportHeight: Math.max(80, dialog.actions.targetCount * 40 + Math.max(0, dialog.actions.targetCount - 1) * dialog.targetSpacing + dialog.actions.sectionCount * (Math.ceil(sectionMetrics.height) + 8))
    flushBody: true
    headerActions: Component {
        RowLayout {
            spacing: 8
            EchoTextTagRow {
                objectName: "addToMultipleLabel"
                lineHeight: 20
                text: qsTr("多选")
                font: Theme.font(12, Theme.semiboldWeight)
                color: Theme.floatingSecondaryText
            }
            EchoSwitch {
                objectName: "addToMultipleSwitch"
                compact: true
                checked: dialog.actions.multiple
                enabled: !dialog.actions.busy && !dialog.actions.hasFailures
                Accessible.name: qsTr("多选模式")
                onToggled: dialog.actions.multiple = checked
            }
        }
    }
    FontMetrics {
        id: sectionMetrics
        font: Theme.font(11, Theme.semiboldWeight)
    }
    body: Component {
        ColumnLayout {
            spacing: 10
            ListView {
                id: targets
                objectName: "playlistAddTargets"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Virtual ListView.contentHeight is a changing estimate. Its viewport
                // must not shrink itself after model reset/reuse on the next open.
                implicitHeight: dialog.targetViewportHeight
                Layout.minimumHeight: 80
                clip: true
                reuseItems: true
                EchoViewPoolGuard {
                    targetView: targets
                }
                cacheBuffer: 240
                spacing: dialog.targetSpacing
                model: dialog.actions
                section.property: "targetSection"
                section.delegate: Label {
                    required property string section
                    width: targets.width - 24
                    topPadding: 4
                    bottomPadding: 4
                    height: Math.ceil(sectionMetrics.height) + 8
                    font: sectionMetrics.font
                    color: Theme.floatingSecondaryText
                    text: section
                }
                delegate: Button {
                    id: target
                    required property int index
                    required property string targetTitle
                    required property string targetSection
                    required property int songCount
                    required property bool selected
                    required property string resultText
                    required property bool failed
                    width: targets.width - 24
                    height: 40
                    padding: 0
                    leftPadding: 12
                    rightPadding: 12
                    hoverEnabled: true
                    enabled: !dialog.actions.busy && (!dialog.actions.multiple || !dialog.actions.hasFailures)
                    Accessible.name: targetTitle
                    Accessible.role: dialog.actions.multiple ? Accessible.CheckBox : Accessible.Button
                    Accessible.checked: selected
                    onClicked: dialog.actions.choose(index)
                    background: Rectangle {
                        id: targetBackground
                        radius: Theme.itemRadius
                        color: Theme.withAlpha(Theme.floating.tone, .07)
                        border.width: target.targetSection === qsTr("播放队列") ? 0 : 1
                        border.color: target.selected || target.hovered ? Theme.accent : Theme.floating.controlBorder
                        Shape {
                            anchors.fill: parent
                            visible: target.targetSection === qsTr("播放队列")
                            ShapePath {
                                strokeColor: targetBackground.border.color
                                strokeWidth: 1
                                strokeStyle: ShapePath.DashLine
                                dashPattern: [3, 2]
                                fillColor: "transparent"
                                PathSvg {
                                    readonly property real r: Theme.itemRadius
                                    readonly property real w: target.width - .5
                                    readonly property real h: target.height - .5
                                    path: "M " + r + " .5 H " + (w - r) + " Q " + w + " .5 " + w + " " + r + " V " + (h - r) + " Q " + w + " " + h + " " + (w - r) + " " + h + " H " + r + " Q .5 " + h + " .5 " + (h - r) + " V " + r + " Q .5 .5 " + r + " .5 Z"
                                }
                            }
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 6
                        EchoCheckBox {
                            visible: dialog.actions.multiple
                            colors: Theme.floating
                            checked: target.selected
                            enabled: target.enabled
                            focusPolicy: Qt.NoFocus
                            onClicked: dialog.actions.choose(target.index)
                        }
                        EchoIcon {
                            visible: target.targetSection === qsTr("播放队列")
                            name: "list"
                            color: Theme.floatingText
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                        }
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            text: target.targetTitle
                            fillTextWidth: true
                            font: Theme.font(13, Theme.semiboldWeight)
                            color: Theme.floatingText
                        }
                        EchoTextTagRow {
                            text: target.resultText.length ? target.resultText : qsTr("%1 首").arg(target.songCount)
                            font: Theme.font(11, Theme.regularWeight)
                            color: target.failed ? Theme.floating.dangerText : Theme.floatingSecondaryText
                        }
                    }
                }
                onAtYEndChanged: if (atYEnd)
                    dialog.actions.fetchMoreTargets()
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: targets
                }
            }
            Label {
                id: errorLabel
                Layout.fillWidth: true
                Layout.rightMargin: 24
                visible: text.length > 0
                text: dialog.actions.errorMessage
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floating.dangerText
                wrapMode: Text.Wrap
            }
        }
    }
    footerContent: actions.multiple ? selectionFooter : null
    Component {
        id: selectionFooter
        RowLayout {
            spacing: 8
            EchoCheckBox {
                colors: Theme.floating
                checkState: dialog.actions.selectedCount === 0 ? Qt.Unchecked : dialog.actions.selectedCount === dialog.actions.targetCount ? Qt.Checked : Qt.PartiallyChecked
                enabled: !dialog.actions.busy && !dialog.actions.hasFailures
                Accessible.name: qsTr("全选")
                onClicked: dialog.actions.selectAll(checkState !== Qt.Unchecked)
            }
            Label {
                Layout.fillWidth: true
                text: dialog.actions.selectedCount + "/" + dialog.actions.targetCount
                color: Theme.floatingSecondaryText
                font: Theme.font(11, Theme.regularWeight)
            }
            EchoButton {
                size: EchoControlSize.Standard
                accent: false
                text: qsTr("取消")
                enabled: !dialog.actions.busy
                onClicked: dialog.close()
            }
            EchoButton {
                size: EchoControlSize.Standard
                text: dialog.actions.hasFailures ? qsTr("重试失败项") : qsTr("添加到 %1 个目标").arg(dialog.actions.selectedCount)
                enabled: !dialog.actions.busy && dialog.actions.selectedCount > 0
                onClicked: dialog.actions.submitSelection()
            }
        }
    }
}
