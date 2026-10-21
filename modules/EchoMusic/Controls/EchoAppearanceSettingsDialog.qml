pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

EchoDialog {
    id: dialog
    objectName: "appearanceSettingsDialog"
    required property AppearanceEntry entry
    onEntryChanged: if (!entry && visible)
        requestDismiss()
    heading: entry ? qsTr("%1 设置").arg(entry.title) : qsTr("外观设置")
    preferredWidth: 640
    maximumHeight: 760
    windowHeightMargin: 120
    flushBody: true
    body: Component {
        Flickable {
            id: viewport
            implicitHeight: contentHeight
            contentWidth: width
            contentHeight: content.implicitHeight + 24
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: dialog
                view: viewport
                resourceActive: dialog.visible
            }
            ColumnLayout {
                id: content
                x: 24
                width: Math.max(0, viewport.width - 48)
                spacing: 16
                AppearanceHost {
                    id: form
                    objectName: "appearanceSettingsHost"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(1, implicitHeight)
                    entry: dialog.entry
                    placement: "settings"
                    resourceActive: dialog.visible
                }
                Label {
                    Layout.fillWidth: true
                    visible: !form.available || dialog.entry && dialog.entry.errorMessage.length > 0
                    text: dialog.entry && dialog.entry.errorMessage.length ? dialog.entry.errorMessage : qsTr("此主题未提供专属设置")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(13, Theme.regularWeight)
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: dialog.entry ? dialog.entry.configuration.errorMessage : ""
                    visible: text.length > 0
                    color: Theme.floating.dangerText
                    font: Theme.font(12, Theme.regularWeight)
                    wrapMode: Text.Wrap
                }
            }
        }
    }
    footerContent: Component {
        EchoSoftButton {
            text: qsTr("恢复默认")
            enabled: dialog.entry !== null
            onClicked: dialog.entry.configuration.reset()
        }
    }
}
