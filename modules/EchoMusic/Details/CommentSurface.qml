pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Adjacent virtual rows paint one root card and one inset reply panel. Keeping
// the surfaces in each delegate preserves viewport-bounded instantiation.
Item {
    id: surface
    property bool compact: false
    property real uiScale: 1
    property string part: "comment"
    property bool threadExpanded: false
    property bool replyOpen: false
    property bool nested: false
    readonly property real outerInset: compact ? 0 : 12
    readonly property real cardPadding: compact ? 14 : 20
    readonly property real cardGap: compact ? 8 : 12
    readonly property real mainLeft: outerInset + cardPadding + (compact ? 30 : 36) + 12
    readonly property real rightInset: outerInset + cardPadding
    readonly property real floorPadding: compact || (Window.window && Window.window.width / uiScale <= 640) ? 12 : 18
    readonly property real floorVerticalPadding: compact ? 10 : 16
    readonly property bool rootRow: part === "comment" || part === "singerComment"
    readonly property bool groupEnd: part === "threadFooter" || (rootRow && !threadExpanded && !replyOpen) || (part === "composer" && !nested && !threadExpanded)
    readonly property bool floorRow: part === "reply" || part === "threadHeading" || part === "threadFooter" || (part === "composer" && nested)
    Rectangle {
        objectName: "commentCardSurface"
        x: surface.outerInset
        width: Math.max(0, parent.width - 2 * x)
        height: Math.max(0, parent.height - (surface.groupEnd ? surface.cardGap : 0))
        color: Theme.withAlpha(Theme.colors.tone, .05)
        topLeftRadius: surface.rootRow ? (surface.compact ? Theme.popoverRadius : Theme.itemRadius) : 0
        topRightRadius: topLeftRadius
        bottomLeftRadius: surface.groupEnd ? (surface.compact ? Theme.popoverRadius : Theme.itemRadius) : 0
        bottomRightRadius: bottomLeftRadius
    }
    Rectangle {
        objectName: "commentFloorSurface"
        visible: surface.floorRow
        x: surface.mainLeft
        y: surface.part === "threadHeading" ? 12 : 0
        width: Math.max(0, parent.width - x - surface.rightInset)
        height: Math.max(0, parent.height - y - (surface.part === "threadFooter" ? surface.cardPadding + surface.cardGap : 0))
        color: Theme.mutedSurface
        topLeftRadius: surface.part === "threadHeading" ? Theme.cardRadius : 0
        topRightRadius: topLeftRadius
        bottomLeftRadius: surface.part === "threadFooter" ? Theme.cardRadius : 0
        bottomRightRadius: bottomLeftRadius
    }
}
