pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// A page viewport, not another scrolling view. ListView/GridView keep their
// own viewport, delegate pool and attached scrollbar connection.
EchoWheelForwarder {
    id: page
    default property alias contentData: contentSurface.data
    property string publicScrollKey: objectName.length > 0 ? objectName : "page-scroll"
    EchoUiPart {
        target: page
        role: "scroll.container"
        key: page.publicScrollKey
        active: page.pageActive && page.scrollView !== null
        actions: ["scrollTo", "scrollBy", "top", "bottom"]
        state: observed ? ({
                position: page.scrollTop,
                minimum: 0,
                maximum: page.scrollView ? Math.max(0, page.lastY(page.scrollView) - page.firstY(page.scrollView)) : 0,
                viewportHeight: page.height,
                contentHeight: page.scrollView ? page.scrollView.contentHeight : 0,
                moving: page.scrollView ? page.scrollView.moving : false,
                atTop: page.scrollView ? page.scrollView.atYBeginning : true,
                atBottom: page.scrollView ? page.scrollView.atYEnd : true
            }) : ({})
        onActionRequested: (action, args) => {
            if (action === "scrollTo")
                page.scrollTo(args.position);
            else if (action === "scrollBy")
                page.scrollBy(args.position);
            else if (action === "top")
                page.scrollTo(0);
            else if (action === "bottom" && page.scrollView)
                page.scrollTo(page.lastY(page.scrollView) - page.firstY(page.scrollView));
        }
    }
    property Flickable scrollView: null
    property bool pageActive: true
    property bool hideScrollbar: false
    property bool hideBackToTop: false
    property real backToTopThreshold: 300
    // Views in this group represent alternate presentations of the same page.
    // Other views (e.g. loading/discovery) retain their independent positions.
    property list<Flickable> coordinatedViews: []
    // Additional presentations to reset together (loading/discovery). They do
    // not participate in the results group's shared scrolling offset.
    property list<Flickable> resetViews: []
    property list<EchoPageStickyHeader> stickyHeaders: []
    readonly property alias stickyLayer: stickySurface
    readonly property alias flowHeaderLayer: flowHeaderSurface
    readonly property alias scrollbarLayer: scrollbarSurface
    readonly property real scrollTop: scrollView ? viewOffset(scrollView) : 0
    readonly property real occlusionHeight: {
        let bottom = 0;
        for (const header of stickyHeaders)
            if (header.visible)
                bottom = Math.max(bottom, header.y + header.height);
        return Math.max(0, Math.min(height, bottom));
    }
    // Scrolling header content belongs below the pinned headers as well.
    // Transparent theme materials cannot hide it with an opaque background.
    readonly property real pinnedOcclusionHeight: {
        let bottom = 0;
        for (const header of stickyHeaders)
            // A later sticky toolbar must not clip preceding normal-flow
            // headings before that toolbar has reached its pinned position.
            if (header.visible && header.sticky && header.y <= header.stickyTop)
                bottom = Math.max(bottom, header.y + header.height);
        return Math.max(0, Math.min(height, bottom));
    }
    signal scrolled
    clip: true
    wheelView: scrollView
    forwardOutsideViewOnly: true
    forwardingEnabled: pageActive
    Binding {
        target: page.scrollView ? page.scrollView.EchoWheelScroll : null
        property: "enabled"
        value: page.pageActive
    }
    Binding {
        target: page.scrollView ? page.scrollView.EchoWheelScroll : null
        property: "motionEnabled"
        value: Theme.store.motionEnabled
    }

    function firstY(view: Flickable): real {
        return view.originY - view.topMargin;
    }
    function viewOffset(view: Flickable): real {
        return Math.max(0, view.contentY - firstY(view));
    }
    function isCoordinated(view: Flickable): bool {
        return coordinatedViews.indexOf(view) >= 0;
    }
    function lastY(view: Flickable): real {
        return Math.max(firstY(view), view.originY + view.contentHeight - view.height + view.bottomMargin);
    }
    function capturePosition() {
        if (!pageActive || state.adjusting || !scrollView)
            return;
        // Signal handlers can run before the derived scrollTop binding updates.
        // Read the source properties to avoid caching the preceding frame.
        if (isCoordinated(scrollView))
            state.groupOffset = viewOffset(scrollView);
        else
            state.independentOffset = viewOffset(scrollView);
        scrolled();
    }
    function restorePosition() {
        if (!pageActive || !scrollView || state.adjusting)
            return;
        // Extent changes (e.g. a responsive sidebar animation) can schedule a
        // restore during user movement. Qt owns that movement and its bounds;
        // refreshing the cache must not overwrite/cancel the active timeline.
        if (scrollView.moving || scrollView.EchoWheelScroll.running) {
            capturePosition();
            return;
        }
        state.adjusting = true;
        const offset = isCoordinated(scrollView) ? state.groupOffset : state.independentOffset;
        const position = Math.max(firstY(scrollView), Math.min(lastY(scrollView), firstY(scrollView) + offset));
        // Qt's setContentY resets its movement timeline even for the same
        // value. A no-op layout/extent restore must not cancel native scrolling.
        if (Math.abs(scrollView.contentY - position) > 0.001)
            scrollView.contentY = position;
        state.adjusting = false;
        capturePosition();
    }
    function scheduleRestore() {
        restoreTask.restart();
    }
    // Coalesce extent notifications within this viewport's lifetime. A queued
    // JS function can outlive the QML creation context of a loading Loader;
    // an owned Timer retires the callback together with the viewport.
    Timer {
        id: restoreTask
        interval: 0
        repeat: false
        onTriggered: page.restorePosition()
    }
    // Qt can adjust contentY synchronously inside setTopMargin, before emitting
    // topMarginChanged. Preserve the offset around the complete header change.
    function preservePosition(change) {
        if (state.adjusting)
            return;
        const offset = scrollView ? viewOffset(scrollView) : 0;
        state.adjusting = true;
        try {
            change();
            if (scrollView)
                scrollView.contentY = firstY(scrollView) + offset;
        } finally {
            state.adjusting = false;
        }
        capturePosition();
        scheduleRestore();
    }
    function scrollTo(position: real) {
        if (!pageActive || !scrollView)
            return;
        scrollView.cancelFlick();
        scrollView.contentY = Math.max(firstY(scrollView), Math.min(lastY(scrollView), firstY(scrollView) + position));
        capturePosition();
    }
    function scrollBy(distance: real) {
        if (scrollView)
            scrollTo(viewOffset(scrollView) + distance);
    }
    function resetPosition() {
        state.adjusting = true;
        state.groupOffset = 0;
        state.independentOffset = 0;
        for (const view of coordinatedViews) {
            view.cancelFlick();
            view.contentY = firstY(view);
        }
        for (const view of resetViews) {
            view.cancelFlick();
            view.contentY = firstY(view);
        }
        if (scrollView) {
            scrollView.cancelFlick();
            scrollView.contentY = firstY(scrollView);
        }
        state.adjusting = false;
        scheduleRestore();
    }
    onScrollViewChanged: {
        // A fresh ListView may still report contentY=0 before laying out its
        // positive topMargin. That is not a user scroll; initial offset is zero.
        const hadView = state.boundView !== null;
        if (state.boundView && state.boundView !== scrollView)
            state.boundView.cancelFlick();
        state.boundView = scrollView;
        // Switching to a non-coordinated view does not consume the results cache.
        if (hadView && pageActive && scrollView && !isCoordinated(scrollView))
            state.independentOffset = viewOffset(scrollView);
        scheduleRestore();
    }
    onPageActiveChanged: {
        if (!pageActive) {
            if (scrollView)
                scrollView.cancelFlick();
            for (const view of coordinatedViews)
                view.cancelFlick();
            for (const view of resetViews)
                view.cancelFlick();
        } else
            scheduleRestore();
    }
    Component.onCompleted: scheduleRestore()

    // Explicit data keeps implementation layers outside the public content slot.
    data: [
        QtObject {
            id: state
            property bool adjusting: false
            property Flickable boundView: null
            property real groupOffset: 0
            property real independentOffset: 0
        },
        Item {
            objectName: "pageContentClip"
            y: page.occlusionHeight
            width: page.width
            height: Math.max(0, page.height - y)
            clip: true
            Item {
                id: contentSurface
                y: -page.occlusionHeight
                width: page.width
                height: page.height
            }
        },
        Item {
            objectName: "pageFlowHeaderClip"
            y: page.pinnedOcclusionHeight
            width: page.width
            height: Math.max(0, page.height - y)
            z: 100
            clip: true
            Item {
                id: flowHeaderSurface
                y: -page.pinnedOcclusionHeight
                width: page.width
                height: page.height
            }
        },
        Item {
            id: stickySurface
            objectName: "pageStickyLayer"
            anchors.fill: parent
            z: 150
            clip: true
            EchoWheelForwarder {
                objectName: "pageHeaderWheelRegion"
                width: page.width
                height: page.occlusionHeight
                wheelView: page.scrollView
                forwardingEnabled: page.pageActive
            }
        },
        Item {
            id: scrollbarSurface
            objectName: "pageScrollbarLayer"
            anchors.fill: parent
            z: 160
        },
        EchoBackToTop {
            objectName: "pageBackToTop"
            anchors.fill: parent
            z: 50
            scrollView: page.scrollView
            viewActive: page.pageActive && !page.hideBackToTop
            threshold: page.backToTopThreshold
            onReturnedToTop: page.capturePosition()
        },
        Connections {
            target: page.scrollView
            enabled: page.pageActive
            function onContentYChanged() {
                page.capturePosition();
            }
            function onContentHeightChanged() {
                page.scheduleRestore();
            }
            function onTopMarginChanged() {
                page.scheduleRestore();
            }
            function onBottomMarginChanged() {
                page.scheduleRestore();
            }
            function onHeightChanged() {
                page.scheduleRestore();
            }
        }
    ]
}
