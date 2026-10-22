pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme

// Shared title, action and close-button placement for business drawers.
Control {
    id: header
    property string heading: ""
    property string description: ""
    property string closeLabel: qsTr("关闭")
    property bool closeDisabled: false
    property font headingFont: Theme.font(16, Theme.semiboldWeight)
    property Component titleContent: defaultTitle
    property Component descriptionContent: description.length ? defaultDescription : null
    property Component leadingContent
    property Component actions
    readonly property alias closeButton: closeAction
    signal closeRequested
    function focusClose(): void {
        closeAction.forceActiveFocus(Qt.OtherFocusReason);
    }
    leftPadding: 24
    rightPadding: 24
    topPadding: 16
    bottomPadding: 14
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    contentItem: RowLayout {
        objectName: "drawerHeadingRow"
        spacing: 12
        Loader {
            Layout.fillHeight: false
            visible: header.leadingContent !== null
            sourceComponent: header.leadingContent
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.minimumWidth: 0
            spacing: 2
            Loader {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 32
                sourceComponent: header.titleContent
            }
            Loader {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: header.descriptionContent !== null
                sourceComponent: header.descriptionContent
            }
        }
        RowLayout {
            objectName: "drawerHeaderActions"
            Layout.fillWidth: false
            Layout.fillHeight: false
            spacing: 8
            Loader {
                Layout.fillHeight: false
                visible: header.actions !== null
                sourceComponent: header.actions
            }
            EchoIconButton {
                id: closeAction
                objectName: "drawerCloseButton"
                implicitWidth: 32
                implicitHeight: 32
                padding: 9
                glyph: "x"
                text: header.closeLabel
                tooltipText: ""
                colors: Theme.floating
                enabled: !header.closeDisabled
                onClicked: header.closeRequested()
            }
        }
    }
    Component {
        id: defaultTitle
        EchoTextLine {
            objectName: "drawerHeadingTitle"
            text: header.heading
            font: header.headingFont
            color: Theme.floatingText
            lineHeight: 32
            wrap: true
        }
    }
    Component {
        id: defaultDescription
        EchoTextLine {
            text: header.description
            font: Theme.font(12, Theme.regularWeight)
            color: Theme.floatingSecondaryText
            lineHeight: 18
            wrap: true
        }
    }
}
