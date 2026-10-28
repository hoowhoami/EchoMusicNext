pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

EchoAnchoredPopup {
    id: menu
    objectName: "titlebarMorePopup"
    required property ShellActionModel actions
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    function activate(key: string) {
        if (actions.activate(key))
            close();
    }
    contentItem: ColumnLayout {
        id: body
        spacing: 0
        ShellMoreHeading {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            title: qsTr("更多应用")
            trailingControl: Component {
                EchoSoftButton {
                    objectName: "restoreTitlebarLayout"
                    text: qsTr("恢复默认")
                    colors: Theme.floating
                    implicitHeight: 28
                    font: Theme.font(12, Theme.regularWeight)
                    leftPadding: 8
                    rightPadding: 8
                    onClicked: menu.actions.restoreDefaults()
                }
            }
        }
        EchoBoundedListView {
            id: list
            maximumViewportHeight: Math.max(0, menu.contentHeightLimit - 45)
            objectName: "titlebarMoreList"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            clip: true
            reuseItems: menu.visible
            cacheBuffer: 0
            spacing: 3
            model: ShellActionProjection {
                actions: menu.visible ? menu.actions : null
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: menu
                view: list
                resourceActive: menu.visible
            }
            delegate: Item {
                id: row
                required property string actionKey
                required property string actionTitle
                required property string actionIcon
                required property bool actionBusy
                required property bool actionHover
                required property string actionTooltip
                required property string badgeTitle
                required property bool actionEnabled
                required property string actionBadge
                required property bool badgeVisible
                required property int preferredZone
                required property int index
                property bool pooled: false
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                objectName: "titlebarMoreRow"
                width: list.width
                height: Math.max(38, itemLabel.implicitHeight + 16)
                RowLayout {
                    anchors.fill: parent
                    spacing: 4
                    Button {
                        id: action
                        objectName: "titlebarMenuAction-" + row.actionKey
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        enabled: row.actionEnabled
                        hoverEnabled: true
                        Accessible.name: row.actionTitle
                        onClicked: menu.activate(row.actionKey)
                        HoverHandler {
                            enabled: !row.pooled && row.visible
                            onHoveredChanged: if (hovered && row.actionHover && action.enabled)
                                menu.actions.activate(row.actionKey)
                        }
                        EchoToolTip {
                            parent: action
                            requestedVisible: action.hovered && action.enabled && !row.pooled
                            text: row.actionTooltip
                        }
                        EchoBadge {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.badgeVisible
                            count: row.actionBadge
                            colors: Theme.floating
                        }
                        padding: 0
                        background: Rectangle {
                            color: action.hovered || action.visualFocus ? Theme.floating.hover : "transparent"
                            radius: Theme.itemRadius
                        }
                        contentItem: Item {
                            Loader {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20
                                height: 20
                                active: row.actionBusy && !row.pooled && menu.visible
                                sourceComponent: EchoSpinner {
                                    anchors.fill: parent
                                    color: Theme.floatingAccentText
                                }
                            }
                            // The drag handle remains editable even for a command
                            // whose business provider has not become available.
                            EchoTextTagRow {
                                id: itemLabel
                                objectName: "titlebarMoreLabel-" + row.actionKey
                                x: 40
                                width: Math.max(0, parent.width - (row.badgeVisible ? 90 : 50))
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.actionTitle
                                color: action.hovered || action.visualFocus ? Theme.floatingAccentText : Theme.floatingText
                                opacity: row.actionEnabled ? 1 : 0.4
                                font: Theme.font(13, Theme.mediumWeight)
                                wrap: true
                                fillTextWidth: true
                            }
                        }
                        Keys.onPressed: event => {
                            if ((event.modifiers & Qt.AltModifier) && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                                menu.actions.shiftAction(row.actionKey, event.key === Qt.Key_Up ? -1 : 1);
                                event.accepted = true;
                            }
                        }
                    }
                    EchoIconButton {
                        id: pin
                        objectName: "titlebarPin-" + row.actionKey
                        glyph: row.preferredZone === ShellActionModel.Toolbar ? "pinned" : "pin"
                        text: (row.preferredZone === ShellActionModel.Toolbar ? qsTr("取消固定") : qsTr("固定")) + row.actionTitle
                        tooltipText: row.preferredZone === ShellActionModel.Toolbar ? qsTr("取消固定，收进更多") : qsTr("固定到顶栏")
                        colors: Theme.floating
                        foreground: row.preferredZone === ShellActionModel.Toolbar ? Theme.floatingAccentText : Theme.floatingSecondaryText
                        implicitWidth: 32
                        implicitHeight: 32
                        Accessible.role: Accessible.CheckBox
                        Accessible.checked: row.preferredZone === ShellActionModel.Toolbar
                        padding: 7
                        onClicked: menu.actions.setPlacement(row.actionKey, row.preferredZone === ShellActionModel.Toolbar ? ShellActionModel.More : ShellActionModel.Toolbar)
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: row.preferredZone === ShellActionModel.Toolbar ? Theme.withAlpha(Theme.accent, 0.1) : pin.hovered ? Theme.floating.hover : "transparent"
                        }
                    }
                }
                ShellActionDragHandle {
                    x: 10
                    y: (row.height - height) / 2
                    actionKey: row.actionKey
                    glyph: row.actionBusy ? "" : row.actionIcon
                    // Qt can retain a mouse-grabbing delegate offscreen instead of
                    // pooling it. Cancel its drag as soon as it leaves the viewport.
                    resourceActive: menu.visible && !row.pooled && row.y + row.height > list.contentY && row.y < list.contentY + list.height
                    onClicked: menu.activate(row.actionKey)
                }
                DropArea {
                    anchors.fill: parent
                    keys: ["titlebar-actions"]
                    onDropped: drop => {
                        const key = drop.source.objectName;
                        const target = row.actionKey;
                        const before = drop.y < height / 2;
                        // Apply after Drag.drop returns so model changes cannot
                        // destroy its source while the pointer release is active.
                        Qt.callLater(() => {
                            if (before)
                                menu.actions.moveBefore(key, target);
                            else
                                menu.actions.moveAfter(key, target);
                        });
                        drop.acceptProposedAction();
                    }
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.accent
                        visible: parent.containsDrag
                    }
                }
            }
        }
        Label {
            visible: list.count === 0
            text: qsTr("暂无可用操作")
            font: Theme.font(12, Theme.regularWeight)
            color: Theme.floatingSecondaryText
            padding: 12
        }
    }
}
