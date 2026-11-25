pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Native horizontal virtualization, finite motion and exterior navigation.
ListView {
    id: carousel
    property bool resourceActive: true
    property string navigationKey: ""
    property string navigationPrefix: "recommendation"
    property int initialIndex: 0
    property real itemWidthHint: 320
    signal indexRemembered(int index)
    orientation: ListView.Horizontal
    snapMode: ListView.SnapToItem
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.HorizontalFlick
    spacing: 12
    clip: true
    reuseItems: true
    // Prepare neighboring presentation slots, not a second song collection.
    cacheBuffer: width > 0 && height > 0 ? previewWidth + spacing : 0
    property real previewWidth: compositionWidth
    readonly property int compositionColumns: Math.max(1, Math.floor((parent.width + spacing) / (itemWidthHint + spacing)))
    readonly property real compositionWidth: Math.max(0, (width - spacing * (compositionColumns - 1)) / compositionColumns)
    EchoViewPoolGuard {
        targetView: carousel
    }
    property bool restored: false
    property bool viewReady: false
    activeFocusOnTab: count > 1
    HoverHandler {
        id: carouselHover
        parent: carousel.parent
        margin: 48
        blocking: false
        enabled: carousel.resourceActive
    }
    property bool keyboardNavigation: false
    readonly property bool navigationShown: carousel.resourceActive && count > 1 && (carouselHover.hovered || previous.hovered || next.hovered || keyboardNavigation && activeFocus || previous.visualFocus || next.visualFocus)
    onActiveFocusChanged: if (!activeFocus)
        keyboardNavigation = false
    Keys.onLeftPressed: {
        keyboardNavigation = true;
        step(-1);
    }
    Keys.onRightPressed: {
        keyboardNavigation = true;
        step(1);
    }
    CarouselArrow {
        id: previous
        parent: carousel.parent
        objectName: carousel.navigationPrefix + "CarouselPrevious-" + carousel.navigationKey
        anchors.right: carousel.left
        anchors.rightMargin: 8
        anchors.verticalCenter: carousel.verticalCenter
        visible: carousel.navigationShown && !carousel.atXBeginning
        glyph: "chevron-left"
        text: qsTr("上一组")
        onClicked: carousel.step(-1)
    }
    CarouselArrow {
        id: next
        parent: carousel.parent
        objectName: carousel.navigationPrefix + "CarouselNext-" + carousel.navigationKey
        anchors.left: carousel.right
        anchors.leftMargin: 8
        anchors.verticalCenter: carousel.verticalCenter
        visible: carousel.navigationShown && !carousel.atXEnd
        glyph: "chevron-right"
        text: qsTr("下一组")
        onClicked: carousel.step(1)
    }
    function restoreIndex(): void {
        if (viewReady && !restored && count > 0 && width > 0) {
            forceLayout();
            currentIndex = Math.min(carousel.initialIndex, count - 1);
            positionViewAtIndex(currentIndex, ListView.Beginning);
            restored = true;
        }
    }
    function step(delta: int): void {
        const index = Math.max(0, Math.min(count - 1, currentIndex + delta));
        // cancelFlick may finish native snap correction. Continue from
        // the presented position when reversing an in-flight transition.
        const start = contentX;
        slide.stop();
        cancelFlick();
        // Cached native positions and uniform slots avoid a viewport
        // round trip just to calculate the animation destination.
        const item = itemAtIndex(index);
        const requested = item ? item.x : originX + index * (previewWidth + spacing);
        const destination = Math.max(originX, Math.min(requested, originX + Math.max(0, contentWidth - width)));
        currentIndex = index;
        carousel.indexRemembered(currentIndex);
        if (motion.allowed && Math.abs(destination - start) > .5) {
            contentX = start;
            slide.from = start;
            slide.to = destination;
            slide.start();
        } else
            contentX = destination;
    }
    EchoMotionScope {
        id: motion
        target: carousel
        active: carousel.resourceActive
        animations: [slide]
    }
    NumberAnimation {
        id: slide
        objectName: carousel.navigationPrefix + "CarouselSlide-" + carousel.navigationKey
        target: carousel
        property: "contentX"
        duration: Theme.panelDuration
        easing.type: Easing.BezierSpline
        easing.bezierCurve: Theme.easeStandard
    }
    onMovementStarted: slide.stop()
    onCountChanged: restoreIndex()
    onWidthChanged: {
        if (restored) {
            slide.stop();
            forceLayout();
            positionViewAtIndex(currentIndex, ListView.Beginning);
        } else
            restoreIndex();
    }
    Component.onCompleted: {
        viewReady = true;
        restoreIndex();
    }
    onMovementEnded: {
        if (slide.running)
            return;
        const left = indexAt(contentX + 1, height / 2);
        if (left >= 0) {
            currentIndex = left;
            carousel.indexRemembered(left);
        }
    }
    footer: Item {
        width: 1
        height: carousel.height
    }
    component CarouselArrow: EchoIconButton {
        id: arrow
        width: 40
        height: 48
        padding: 6
        topPadding: 10
        bottomPadding: 10
        z: 2
        foreground: hovered || visualFocus ? Theme.accentText : Theme.text
        visualContentActive: carousel.resourceActive && visible
        hoverBackground: false
        background: null
    }
}
