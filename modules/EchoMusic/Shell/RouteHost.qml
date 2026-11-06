pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Shell
import EchoMusic.Theme
import EchoMusic.Controls

Item {
    id: host
    required property RouteController router
    required property ShellSettings settings
    property bool hasPresentedPage: false
    // Initial construction/readiness, independent of API data or the entry
    // animation. The existing startup surface can bridge this first gap.
    property bool initialPageReady: false
    property int outgoingCount: 0
    property string finishedEntry: ""
    signal exitsFinished
    // Readiness and input are independent of the visual entry duration.
    signal pageReady(string instanceId)
    signal entryFinished(string instanceId)
    onOutgoingCountChanged: if (outgoingCount === 0)
        exitsFinished()
    function finishEntry(instanceId: string): void {
        finishedEntry = instanceId;
        entryFinished(instanceId);
    }
    // Stable value identity: unrelated theme/accent updates must not cancel a
    // route animation. Plugin contributions still replace/revoke atomically.
    readonly property string transitionSignature: JSON.stringify(Theme.store.extensionAppearance.pageTransition || {})
    readonly property bool presentationLive: visible && opacity > 0 && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    clip: true
    Repeater {
        model: host.router
        delegate: Loader {
            id: pageLoader
            required property RouteContext pageContext
            EchoPageState.scope: pageContext.viewState
            EchoPageState.presentation: presentation
            property EchoPagePresentation presentation: EchoPagePresentation {}
            property bool hasLoadedBefore: false
            property bool skipEntry: false
            // The context and domain models stay in the bounded router cache.
            // Only an active page or its short exit transition owns visual UI.
            active: false
            onActiveChanged: if (!active && presentation)
                presentation.cancel()
            property bool leaving: false
            property bool waitingEnter: false
            property bool waitingExit: false
            property string waitingForEntry: ""
            onLeavingChanged: host.outgoingCount += leaving ? 1 : -1
            readonly property var recipe: JSON.parse(host.transitionSignature)
            readonly property bool customizedRoute: Object.keys(recipe).length > 0
            readonly property real durationMs: recipe.durationMs === undefined ? Theme.pageDuration : recipe.durationMs
            readonly property var curve: recipe.easing ? [recipe.easing[0], recipe.easing[1], recipe.easing[2], recipe.easing[3], 1, 1] : Theme.easeStandard
            readonly property bool animateRoute: host.presentationLive && Theme.store.motionEnabled && recipe.enabled !== false
            property bool componentReady: false
            onRecipeChanged: if (componentReady) {
                settle();
                if (!pageContext.active)
                    active = false;
                else if (presentation.ready)
                    completeEntry();
                else
                    opacity = 0;
            }
            anchors.fill: parent
            visible: active
            enabled: pageContext.active && presentation.ready
            z: pageContext.active ? 1 : 0
            // Reuse the compiled component and cached data in one GUI turn on
            // return; cold construction still yields to the window incubator.
            asynchronous: !hasLoadedBefore
            transform: [
                Translate {
                    id: offset
                },
                Scale {
                    id: pageScale
                    origin.x: pageLoader.width / 2
                    origin.y: pageLoader.height / 2
                }
            ]
            function settle(): void {
                entering.stop();
                exiting.stop();
                waitingEnter = false;
                waitingExit = false;
                leaving = false;
                opacity = 1;
                offset.x = 0;
                offset.y = 0;
                pageScale.xScale = 1;
                pageScale.yScale = 1;
            }
            function completeEntry(): void {
                presentation.presented();
                host.finishEntry(pageContext.instanceId);
            }
            function change(): void {
                settle();
                if (pageContext.active && !active) {
                    presentation.begin(!customizedRoute || hasLoadedBefore);
                    opacity = 0;
                    active = true;
                    return;
                }
                // Object creation can finish before scroll/layout recovery.
                // Recovery must stay outside the first visible frame.
                if (pageContext.active && (status !== Loader.Ready || !presentation.ready)) {
                    opacity = 0;
                    return;
                }
                // A canceled recovery has never been visible. It has no exit
                // frame to animate, including for custom in-out transitions.
                if (!pageContext.active && !presentation.ready) {
                    active = false;
                    return;
                }
                // Default navigation never retains the old view, even while
                // the incoming page is fading in. Only explicit plugin recipes
                // may request an exit animation or ordering between pages.
                if (!pageContext.active && !customizedRoute) {
                    active = false;
                    return;
                }
                if (!animateRoute || status !== Loader.Ready) {
                    if (!pageContext.active)
                        active = false;
                    else
                        completeEntry();
                    return;
                }
                if (pageContext.active) {
                    if (skipEntry) {
                        skipEntry = false;
                        completeEntry();
                        return;
                    }
                    opacity = recipe.enterOpacity === undefined ? 0 : recipe.enterOpacity;
                    offset.x = recipe.enterX || 0;
                    offset.y = recipe.enterY || 0;
                    pageScale.xScale = recipe.enterScale || 1;
                    pageScale.yScale = pageScale.xScale;
                    if (recipe.mode === "out-in" && host.outgoingCount > 0)
                        waitingEnter = true;
                    else
                        entering.restart();
                } else {
                    leaving = true;
                    const incoming = host.router.current;
                    if (recipe.mode === "in-out" && incoming && host.finishedEntry !== incoming.instanceId) {
                        waitingForEntry = incoming.instanceId;
                        waitingExit = true;
                    } else
                        exiting.restart();
                }
            }
            onLoaded: if (pageContext.active) {
                const initial = !host.hasPresentedPage;
                host.hasPresentedPage = true;
                hasLoadedBefore = true;
                // appear controls the initial presentation, not every newly
                // created page after navigation or cache-instance release.
                skipEntry = initial && recipe.appear === false;
                presentation.finishLoading();
            }
            onStatusChanged: if (status === Loader.Error && pageContext.active) {
                host.initialPageReady = true;
            }
            onAnimateRouteChanged: if (!animateRoute) {
                settle();
                if (!pageContext.active)
                    active = false;
                else if (presentation.ready)
                    completeEntry();
                else
                    opacity = 0;
            }
            Connections {
                target: pageLoader.presentation
                function onReadyChanged(): void {
                    if (pageLoader.presentation.ready && pageLoader.pageContext.active) {
                        host.initialPageReady = true;
                        host.pageReady(pageLoader.pageContext.instanceId);
                        pageLoader.change();
                    }
                }
            }
            Connections {
                target: pageLoader.pageContext
                function onActiveChanged(): void {
                    if (!pageLoader.pageContext.active)
                        pageLoader.change();
                }
            }
            Connections {
                target: host
                function onExitsFinished(): void {
                    if (pageLoader.waitingEnter && pageLoader.pageContext.active && pageLoader.animateRoute) {
                        pageLoader.waitingEnter = false;
                        entering.restart();
                    }
                }
                function onEntryFinished(instanceId: string): void {
                    if (pageLoader.waitingExit && pageLoader.waitingForEntry === instanceId) {
                        pageLoader.waitingExit = false;
                        exiting.restart();
                    }
                }
            }
            Connections {
                target: host.router
                // Business coordinators connect before the visual host. Build
                // against their prepared bucket/models, not the outgoing page.
                function onPageActivated(context: RouteContext): void {
                    if (context === pageLoader.pageContext && (!pageLoader.active || pageLoader.leaving))
                        pageLoader.change();
                }
                function onRouteChanged(): void {
                    if (pageLoader.waitingExit && host.router.current)
                        pageLoader.waitingForEntry = host.router.current.instanceId;
                }
            }
            ParallelAnimation {
                id: entering
                objectName: "routeEntryAnimation"
                onFinished: pageLoader.completeEntry()
                ParallelAnimation {
                    NumberAnimation {
                        target: pageLoader
                        property: "opacity"
                        to: 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: offset
                        property: "x"
                        to: 0
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: offset
                        property: "y"
                        to: 0
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: pageScale
                        property: "xScale"
                        to: 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: pageScale
                        property: "yScale"
                        to: 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                }
            }
            SequentialAnimation {
                id: exiting
                objectName: "routeExitAnimation"
                ParallelAnimation {
                    NumberAnimation {
                        target: pageLoader
                        property: "opacity"
                        to: pageLoader.recipe.leaveOpacity === undefined ? 0 : pageLoader.recipe.leaveOpacity
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: offset
                        property: "x"
                        to: pageLoader.recipe.leaveX || 0
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: offset
                        property: "y"
                        to: pageLoader.recipe.leaveY || 0
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: pageScale
                        property: "xScale"
                        to: pageLoader.recipe.leaveScale || 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: pageScale
                        property: "yScale"
                        to: pageLoader.recipe.leaveScale || 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                }
                ScriptAction {
                    script: {
                        pageLoader.leaving = false;
                        if (!pageLoader.pageContext.active)
                            pageLoader.active = false;
                    }
                }
            }
            Component.onCompleted: {
                componentReady = true;
                setSource(pageContext.component, Object.assign({}, pageContext.initialProperties, pageContext.pluginOwned ? {
                    context: pageContext
                } : {
                    context: pageContext,
                    router: host.router,
                    settings: host.settings
                }));
                if (pageContext.active)
                    change();
            }
            Component.onDestruction: {
                presentation.cancel();
                if (leaving)
                    host.outgoingCount--;
            }
        }
    }
}
