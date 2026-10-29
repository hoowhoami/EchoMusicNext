pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Controls

ListView {
    id: strip
    required property ShellActionModel actions
    required property int zone
    property bool compact: false
    property bool playbackLoading: false
    property bool lyricStyle: false
    property ThemeColors colors: Theme.colors
    signal actionAboutToActivate(string id, Item anchor)
    implicitHeight: compact ? 28 : 40
    implicitWidth: count > 0 ? count * (compact ? 28 : 36) + (count - 1) * spacing + (zone === ShellActionModel.Center ? 2 : 0) : 0
    orientation: ListView.Horizontal
    spacing: 6
    interactive: false
    reuseItems: true
    EchoViewPoolGuard {
        targetView: strip
    }
    cacheBuffer: 0
    model: ShellActionProjection {
        actions: strip.actions
        zone: strip.zone
    }
    delegate: Item {
        id: slot
        required property string actionKey
        required property string actionId
        required property string actionTitle
        required property string actionIcon
        required property bool actionEnabled
        required property bool actionActive
        required property string actionBadge
        required property bool actionBusy
        required property bool actionHover
        required property string actionTooltip
        required property string badgeTitle
        required property bool badgeVisible
        property bool pooled: false
        ListView.onPooled: pooled = true
        ListView.onReused: pooled = false
        width: button.implicitWidth
        height: strip.height
        ShellActionButton {
            id: button
            anchors.verticalCenter: parent.verticalCenter
            colors: strip.colors
            actions: strip.actions
            actionKey: slot.actionKey
            actionId: slot.actionId
            actionTitle: slot.actionTitle
            actionIcon: slot.actionIcon
            actionEnabled: slot.actionEnabled
            actionActive: slot.actionActive
            actionBadge: slot.actionBadge
            badgeVisible: slot.badgeVisible
            actionBusy: slot.actionBusy
            actionHover: slot.actionHover
            actionTooltip: slot.actionTooltip
            badgeTitle: slot.badgeTitle
            visualContentActive: !slot.pooled
            projection: strip.zone
            player: true
            playbackLoading: strip.playbackLoading
            lyricStyle: strip.lyricStyle
            compact: strip.compact
            onAboutToActivate: (id, anchor) => strip.actionAboutToActivate(id, anchor)
        }
    }
}
