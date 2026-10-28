pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Dialog {
    id: dialog
    EchoPopupLifecycle {
        surface: dialog
        suspensionExempt: dialog.verificationSurface
    }
    // Only the active security verification dialog may bypass the input cover.
    property bool verificationSurface: false
    property real uiScale: 1
    property real preferredWidth: 420
    property int headingPixelSize: 18
    property int descriptionPixelSize: 14
    property real descriptionSpacing: 8
    property string heading: ""
    property string headingGlyph: ""
    property string description: ""
    property int headingCount: -1
    property bool closable: true
    property bool managedDismiss: false
    property real windowHeightMargin: 240
    property real maximumHeight: Infinity
    signal dismissRequested
    function requestDismiss(): void {
        if (managedDismiss)
            dismissRequested();
        else
            close();
    }
    // Scroll-owning bodies reach the edge and supply their own 24px content inset.
    // Ordinary bodies receive that inset here.
    property bool flushBody: false
    property Component body
    property Component footerContent
    property Component headerActions
    // Entrances may name the control or refine its accessible label; layout,
    // hit target and dismissal stay owned by this shared dialog.
    readonly property alias closeButton: sharedClose
    readonly property real bodyHeightLimit: Math.max(0, (parent ? Math.min(maximumHeight, parent.height / uiScale - windowHeightMargin) : 600) - topPadding - bottomPadding - headingRow.implicitHeight - (descriptionLabel.visible ? descriptionLabel.implicitHeight + dialog.descriptionSpacing : 0) - footerLoader.implicitHeight - 24)
    parent: Overlay.overlay
    scale: uiScale
    transformOrigin: Popup.TopLeft
    width: Math.min(preferredWidth, parent ? Math.max(0, parent.width / uiScale - 32) : preferredWidth)
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    height: Math.min(maximumHeight, implicitHeight, parent ? Math.max(120, parent.height / uiScale - windowHeightMargin) : implicitHeight)
    x: parent ? (parent.width - width * uiScale) / 2 : 0
    y: parent ? Math.max(12 * uiScale, parent.height * 0.46 - height * uiScale / 2) : 0
    modal: true
    focus: true
    Shortcut {
        sequence: "Escape"
        enabled: dialog.enabled && dialog.visible && dialog.managedDismiss && dialog.closable && dialog.activeFocus
        onActivated: dialog.requestDismiss()
    }
    padding: 24
    rightPadding: 0
    header: null
    footer: null
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle {
        id: dimSurface
        color: Theme.dark ? "#5c000000" : "#4d000000"
        EchoPopupDismissGuard {
            overlay: dimSurface.parent
            dimmer: dimSurface
            enabled: dialog.enabled && dialog.visible && dialog.managedDismiss && dialog.closable
            onOutsidePressed: dialog.requestDismiss()
        }
    }
    background: EchoFloatingSurface {
        radius: Theme.dialogRadius
        border.width: 1
        border.color: Theme.floating.surfaceOutline
        dialogShadow: true
    }
    contentItem: ColumnLayout {
        id: content
        spacing: 8
        RowLayout {
            id: headingRow
            objectName: "dialogHeading"
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.rightMargin: 24
            spacing: 12
            EchoButtonContent {
                visible: dialog.headingGlyph.length > 0
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.minimumWidth: 0
                Layout.topMargin: 2
                horizontalAlignment: Qt.AlignLeft
                text: dialog.heading
                glyph: dialog.headingGlyph
                glyphSize: 18
                spacing: 8
                lineHeight: 28
                color: Theme.floatingText
                iconColor: Theme.floating.accentText
                font: Theme.font(dialog.headingPixelSize, Theme.boldWeight)
            }
            EchoTextTagRow {
                id: titleLabel
                visible: dialog.headingGlyph.length === 0
                objectName: "dialogHeadingTitle"
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.minimumWidth: 0
                // Center the 28px title line against 32px controls.
                Layout.topMargin: 2
                text: dialog.heading
                color: Theme.floatingText
                font: Theme.font(dialog.headingPixelSize, Theme.boldWeight)
                lineHeight: 28
                wrap: true
                spacing: 4
                EchoBadge {
                    id: countBadge
                    objectName: "dialogHeadingBadge"
                    visible: dialog.headingCount >= 0
                    count: dialog.headingCount
                    tone: EchoBadge.Muted
                    colors: Theme.floating
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: false
                Layout.fillHeight: false
                spacing: 8
                Loader {
                    id: headerLoader
                    objectName: "dialogHeaderActions"
                    Layout.fillHeight: false
                    visible: dialog.headerActions !== null
                    sourceComponent: dialog.headerActions
                }
                EchoDialogCloseButton {
                    id: sharedClose
                    surface: dialog
                    visible: dialog.closable
                    objectName: "dialogCloseButton"
                    onClicked: dialog.requestDismiss()
                }
            }
        }
        Label {
            id: descriptionLabel
            visible: dialog.description.length > 0
            Layout.fillWidth: true
            Layout.rightMargin: 24
            text: dialog.description
            Layout.bottomMargin: dialog.descriptionSpacing - content.spacing
            color: Theme.floatingSecondaryText
            font: Theme.font(dialog.descriptionPixelSize, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
        Loader {
            id: bodyLoader
            objectName: "dialogBody"
            Layout.fillWidth: true
            Layout.rightMargin: dialog.flushBody ? 0 : 24
            Layout.fillHeight: true
            Layout.preferredHeight: (item as Item) ? Math.min((item as Item).implicitHeight, dialog.bodyHeightLimit) : 0
            sourceComponent: dialog.body
            clip: true
        }
        Loader {
            id: footerLoader
            objectName: "dialogFooter"
            visible: dialog.footerContent !== null
            Layout.fillWidth: true
            Layout.rightMargin: 24
            Layout.topMargin: visible ? 8 : 0
            Layout.preferredHeight: (item as Item) ? (item as Item).implicitHeight : 0
            sourceComponent: dialog.footerContent
        }
    }
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.normalDuration
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.exitDuration
        }
    }
}
