pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Theme

RoutePage {
    id: page
    required property ShareResolveService service
    objectName: "shareResolvePage"
    readonly property bool narrow: width <= 640
    readonly property bool resourcesActive: pageActive && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    Flickable {
        id: viewport
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, panel.y + panel.height + (page.narrow ? 56 : 72))
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: EchoPageScrollBar {
            view: viewport
        }
        Rectangle {
            id: panel
            objectName: "shareResolvePanel"
            width: Math.max(0, Math.min(680, viewport.width - (page.narrow ? 32 : 48)))
            height: contents.implicitHeight + padding * 2
            readonly property real padding: page.narrow ? 22 : 28
            x: (viewport.width - width) / 2
            y: page.narrow ? 24 : 40 + Math.max(0, (viewport.height - 112 - height) / 2)
            radius: Theme.cardRadius
            color: Theme.card
            EchoDashedFrame {
                anchors.fill: parent
                solid: true
                strokeColor: Theme.border
                radius: Theme.cardRadius
            }
            EchoSurfaceShadow {
                anchors.fill: parent
                surface: panel
                z: -1
                radius: panel.radius
            }
            ColumnLayout {
                id: contents
                x: panel.padding
                y: panel.padding
                width: Math.max(0, panel.width - panel.padding * 2)
                spacing: 0
                Rectangle {
                    implicitWidth: kicker.implicitWidth + 20
                    implicitHeight: 30
                    radius: Theme.controlRadius
                    color: Theme.withAlpha(Theme.accent, 0.10)
                    EchoButtonContent {
                        id: kicker
                        anchors.centerIn: parent
                        text: qsTr("分享链接")
                        glyph: "share"
                        glyphSize: 15
                        spacing: 8
                        color: Theme.accentText
                        font: Theme.font(12, 700)
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 20
                    columns: page.narrow ? 1 : 2
                    rowSpacing: 20
                    columnSpacing: 20
                    Item {
                        Layout.preferredWidth: page.service.loading ? 44 : 64
                        Layout.preferredHeight: page.service.loading ? 32 : 64
                        Layout.alignment: Qt.AlignTop
                        EchoSpinner {
                            x: 10
                            y: 4
                            width: 24
                            height: 24
                            visible: page.service.loading && page.service.spinner
                            resourceActive: visible && page.resourcesActive
                            color: Theme.accent
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: !page.service.loading
                            radius: Theme.cardRadius
                            color: Theme.withAlpha(Theme.colors.warning, 0.12)
                            EchoDashedFrame {
                                anchors.fill: parent
                                solid: true
                                strokeColor: Theme.withAlpha(Theme.colors.warning, 0.24)
                                radius: Theme.cardRadius
                            }
                            EchoIcon {
                                anchors.centerIn: parent
                                width: 30
                                height: 30
                                name: "alert-triangle"
                                color: Theme.colors.warning
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label {
                            objectName: "shareResolveHeading"
                            Layout.fillWidth: true
                            text: page.service.heading
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font: Theme.font(page.narrow ? 22 : 26, 800)
                            lineHeightMode: Text.FixedHeight
                            lineHeight: font.pixelSize * 1.2
                            color: Theme.text
                        }
                        Label {
                            objectName: "shareResolveExplanation"
                            Layout.fillWidth: true
                            text: page.service.explanation
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font: Theme.font(14, 400)
                            lineHeightMode: Text.FixedHeight
                            lineHeight: font.pixelSize * 1.7
                            color: Theme.secondaryText
                        }
                    }
                }
                GridLayout {
                    visible: !page.service.loading
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    columns: page.narrow ? 1 : 2
                    columnSpacing: 10
                    rowSpacing: 10
                    Repeater {
                        model: page.service.targetId ? [
                            {
                                label: qsTr("类型"),
                                value: page.service.resourceLabel
                            },
                            {
                                label: "ID",
                                value: page.service.targetId
                            }
                        ] : [
                            {
                                label: qsTr("类型"),
                                value: page.service.resourceLabel
                            }
                        ]
                        Rectangle {
                            id: fact
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: factText.implicitHeight + 24
                            radius: Theme.itemRadius
                            color: Theme.colors.mutedSurface
                            ColumnLayout {
                                id: factText
                                x: 12
                                y: 12
                                width: Math.max(0, fact.width - 24)
                                spacing: 4
                                Label {
                                    text: fact.modelData.label
                                    font: Theme.font(11, 700)
                                    color: Theme.secondaryText
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: fact.modelData.value
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WrapAnywhere
                                    font: Theme.font(13, 700)
                                    color: Theme.text
                                }
                            }
                        }
                    }
                }
                Flow {
                    visible: !page.service.loading
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    spacing: 10
                    EchoButton {
                        objectName: "shareRetry"
                        width: page.narrow ? parent.width : implicitWidth
                        text: qsTr("重新打开")
                        glyph: "refresh"
                        enabled: !page.service.loading
                        onClicked: page.service.retry()
                    }
                    EchoButton {
                        objectName: "shareHome"
                        width: page.narrow ? parent.width : implicitWidth
                        accent: false
                        text: qsTr("回到首页")
                        glyph: "home"
                        onClicked: page.service.goHome()
                    }
                    EchoButton {
                        objectName: "shareBack"
                        width: page.narrow ? parent.width : implicitWidth
                        accent: false
                        text: qsTr("返回上页")
                        glyph: "arrow-left"
                        onClicked: page.service.goBack()
                    }
                }
            }
        }
    }
}
