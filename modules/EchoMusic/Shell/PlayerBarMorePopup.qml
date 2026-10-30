pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

EchoAnchoredPopup {
    id: menu
    objectName: "playerbarMorePopup"
    required property ShellActionModel actions
    property bool lyricStyle: false
    property bool editing: false
    preferredWidth: editing ? 860 : 280
    maximumHeight: editing ? 620 : 520
    preferAbove: true
    alignEnd: false
    showArrow: false
    sideOffset: 8
    implicitHeight: contents.implicitHeight + topPadding + bottomPadding
    onClosed: editing = false
    contentItem: ColumnLayout {
        id: contents
        spacing: 6
        ShellMoreHeading {
            Layout.fillWidth: true
            Layout.bottomMargin: 2
            title: menu.editing ? qsTr("编辑布局") : qsTr("更多功能")
            leadingControl: menu.editing ? backControl : null
            trailingControl: Component {
                RowLayout {
                    spacing: 5
                    EchoIconButton {
                        objectName: "editPlayerbarLayout"
                        visible: !menu.editing
                        glyph: "layout-bottombar"
                        text: qsTr("编辑播放栏布局")
                        tooltipText: text
                        colors: Theme.floating
                        implicitWidth: 28
                        implicitHeight: 28
                        padding: 6
                        onClicked: menu.editing = true
                    }
                    EchoSoftButton {
                        objectName: "restorePlayerbarLayout"
                        visible: menu.editing
                        text: qsTr("恢复默认")
                        colors: Theme.floating
                        implicitHeight: 28
                        font: Theme.font(12, Theme.mediumWeight)
                        leftPadding: 8
                        rightPadding: 8
                        onClicked: menu.actions.restoreDefaults()
                    }
                }
            }
        }
        ListView {
            id: list
            objectName: "playerbarOverflowList"
            visible: !menu.editing && count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(count * 38 + Math.max(0, count - 1) * spacing, Math.max(0, menu.contentHeightLimit - 45))
            clip: true
            spacing: 3
            reuseItems: menu.visible && !menu.editing
            EchoViewPoolGuard {
                targetView: list
            }
            cacheBuffer: 0
            model: ShellActionProjection {
                actions: menu.visible && !menu.editing ? menu.actions : null
                zone: ShellActionModel.More
                includePlaybackPageActions: menu.lyricStyle
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: menu
                view: list
                resourceActive: menu.visible && !menu.editing
            }
            delegate: Button {
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
                property bool pooled: false
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                objectName: "playerbarMenuAction-" + actionKey
                width: list.width
                height: 38
                enabled: actionEnabled
                opacity: enabled ? 1 : 0.56
                hoverEnabled: true
                leftPadding: 10
                rightPadding: 10
                Accessible.name: actionTitle
                HoverHandler {
                    enabled: row.visible && !row.pooled
                    onHoveredChanged: if (hovered && row.actionHover && row.enabled)
                        menu.actions.activate(row.actionKey)
                }
                EchoToolTip {
                    parent: row
                    requestedVisible: !row.pooled && row.hovered && row.enabled && row.actionTooltip.length > 0
                    text: row.actionTooltip + (row.badgeVisible && row.badgeTitle.length > 0 ? " · " + row.badgeTitle + ": " + row.actionBadge : "")
                }
                onClicked: {
                    if (menu.actions.activate(actionKey))
                        menu.close();
                }
                background: Rectangle {
                    radius: Theme.itemRadius
                    color: row.hovered || row.visualFocus ? Theme.floating.hover : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    Item {
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 19
                        EchoIcon {
                            anchors.fill: parent
                            visible: !row.actionBusy
                            name: row.actionIcon
                            color: row.hovered || row.visualFocus ? Theme.floatingAccentText : Theme.floatingSecondaryText
                        }
                        Loader {
                            anchors.fill: parent
                            active: row.actionBusy && !row.pooled && menu.visible
                            sourceComponent: EchoSpinner {
                                anchors.fill: parent
                                color: Theme.floatingAccentText
                            }
                        }
                    }
                    EchoTextTagRow {
                        objectName: "playerbarMoreLabel-" + row.actionKey
                        text: row.actionTitle
                        color: row.hovered || row.visualFocus ? Theme.floatingAccentText : Theme.floatingText
                        font: Theme.font(13, Theme.mediumWeight)
                        fillTextWidth: true
                        Layout.fillWidth: true
                    }
                    EchoTextTagRow {
                        visible: row.badgeVisible
                        text: row.actionBadge
                        font: Theme.font(10, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        Layout.maximumWidth: 64
                    }
                }
            }
        }
        Label {
            visible: !menu.editing && list.count === 0
            text: qsTr("暂无收纳功能")
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.semiboldWeight)
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            topPadding: 14
            bottomPadding: 10
        }
        Label {
            visible: menu.editing
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            text: qsTr("拖动按钮调整位置，上方复选框控制徽标显示。")
            color: Theme.floatingSecondaryText
            font: Theme.font(12, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
        ScrollView {
            id: editScroll
            visible: menu.editing
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(board.implicitHeight, Math.max(0, menu.contentHeightLimit - 79))
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: menu
                view: editScroll
                resourceActive: menu.visible && menu.editing
            }
            PlayerLayoutBoard {
                id: board
                width: editScroll.availableWidth
                actions: menu.actions
                lyricStyle: menu.lyricStyle
                resourceActive: menu.visible && menu.editing
                narrow: menu.parent ? menu.parent.width / menu.uiScale <= 640 : false
            }
        }
    }
    Component {
        id: backControl
        EchoIconButton {
            objectName: "returnPlayerbarFunctions"
            glyph: "chevron-left"
            text: qsTr("返回更多功能")
            colors: Theme.floating
            implicitWidth: 28
            implicitHeight: 28
            padding: 5
            onClicked: menu.editing = false
        }
    }
}
