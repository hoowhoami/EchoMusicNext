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
    property var handoffLoader: null
    signal exitsFinished
    signal entryStarted(string instanceId)
    signal entryFinished(string instanceId)
    onOutgoingCountChanged: if (outgoingCount === 0)
        exitsFinished()
    function finishEntry(instanceId: string): void {
        finishedEntry = instanceId;
        entryFinished(instanceId);
    }
    function retainHandoff(loader: var): bool {
        if (handoffLoader && handoffLoader !== loader)
            handoffLoader.endHandoff(true);
        if (!router.retainTransition(loader.pageContext))
            return false;
        handoffLoader = loader;
        return true;
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
            property bool handoff: false
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
            // Keep visibility stable through deactivation: child artwork must
            // not tear down between the active signal and handoff ownership.
            visible: active
            enabled: pageContext.active && presentation.ready
            z: pageContext.active ? 1 : 0
            asynchronous: true
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
                endHandoff(false);
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
            function endHandoff(unload: bool): void {
                if (!handoff)
                    return;
                handoffDim.stop();
                handoffExit.stop();
                handoff = false;
                presentation.retiring = false;
                if (host.handoffLoader === pageLoader)
                    host.handoffLoader = null;
                if (host.router)
                    host.router.releaseTransition(pageContext);
                if (unload && !pageContext.active)
                    active = false;
            }
            function startEntry(): void {
                entering.restart();
                host.entryStarted(pageContext.instanceId);
            }
            function completeEntry(): void {
                presentation.presented();
                host.finishEntry(pageContext.instanceId);
            }
            function change(): void {
                const previousOpacity = opacity;
                const returningHandoff = handoff;
                const wasVisible = active && status === Loader.Ready && presentation.ready && previousOpacity > 0.001;
                settle();
                if (pageContext.active && !active) {
                    presentation.begin(hasLoadedBefore);
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
                    opacity = returningHandoff && !customizedRoute ? previousOpacity : recipe.enterOpacity === undefined ? 0 : recipe.enterOpacity;
                    offset.x = recipe.enterX || 0;
                    offset.y = recipe.enterY || 0;
                    pageScale.xScale = recipe.enterScale || 1;
                    pageScale.yScale = pageScale.xScale;
                    if (recipe.mode === "out-in" && host.outgoingCount > 0)
                        waitingEnter = true;
                    else
                        startEntry();
                } else {
                    if (!customizedRoute) {
                        // Immediate feedback, then keep only this one outgoing
                        // view until the restored page can take over. No image
                        // snapshot or background page cache is involved.
                        if (wasVisible && host.retainHandoff(pageLoader)) {
                            handoff = true;
                            presentation.retiring = true;
                            // Signal navigation before the first animation
                            // tick, then front-load the remaining response.
                            opacity = previousOpacity * 0.92;
                            handoffDim.to = Math.min(opacity, 0.65);
                            handoffDim.restart();
                        } else
                            active = false;
                        return;
                    }
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
                if (host.handoffLoader)
                    host.handoffLoader.endHandoff(true);
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
                        pageLoader.change();
                    }
                }
            }
            Connections {
                target: pageLoader.pageContext
                function onActiveChanged(): void {
                    pageLoader.change();
                }
            }
            Connections {
                target: host
                function onExitsFinished(): void {
                    if (pageLoader.waitingEnter && pageLoader.pageContext.active && pageLoader.animateRoute) {
                        pageLoader.waitingEnter = false;
                        pageLoader.startEntry();
                    }
                }
                function onEntryStarted(instanceId: string): void {
                    if (pageLoader.handoff && host.router.current && host.router.current.instanceId === instanceId) {
                        handoffDim.stop();
                        handoffExit.restart();
                    }
                }
                function onEntryFinished(instanceId: string): void {
                    if (pageLoader.handoff && host.router.current && host.router.current.instanceId === instanceId)
                        pageLoader.endHandoff(true);
                    if (pageLoader.waitingExit && pageLoader.waitingForEntry === instanceId) {
                        pageLoader.waitingExit = false;
                        exiting.restart();
                    }
                }
            }
            Connections {
                target: host.router
                function onRouteChanged(): void {
                    if (pageLoader.handoff && !host.router.current)
                        pageLoader.endHandoff(true);
                    if (pageLoader.waitingExit && host.router.current)
                        pageLoader.waitingForEntry = host.router.current.instanceId;
                }
            }
            NumberAnimation {
                id: handoffDim
                target: pageLoader
                property: "opacity"
                duration: Theme.pageResponseDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeEnter
            }
            NumberAnimation {
                id: handoffExit
                target: pageLoader
                property: "opacity"
                to: 0
                duration: pageLoader.durationMs
                easing.type: Easing.BezierSpline
                easing.bezierCurve: pageLoader.curve
                onFinished: pageLoader.endHandoff(true)
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
                endHandoff(false);
                presentation.cancel();
                if (leaving)
                    host.outgoingCount--;
            }
        }
    }
}
