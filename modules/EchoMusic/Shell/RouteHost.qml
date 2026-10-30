pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Shell
import EchoMusic.Theme

Item {
    id: host
    required property RouteController router
    required property ShellSettings settings
    readonly property bool presentationLive: visible && opacity > 0 && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    clip: true
    Repeater {
        model: host.router
        delegate: Loader {
            id: pageLoader
            required property RouteContext pageContext
            property bool leaving: false
            readonly property var recipe: Theme.store.extensionAppearance.pageTransition || {}
            readonly property real durationMs: recipe.durationMs === undefined ? 220 : recipe.durationMs
            readonly property var curve: recipe.easing ? [recipe.easing[0], recipe.easing[1], recipe.easing[2], recipe.easing[3], 1, 1] : Theme.easeStandard
            readonly property bool animateRoute: host.presentationLive && Theme.store.motionEnabled && recipe.enabled !== false && Object.keys(recipe).length > 0
            anchors.fill: parent
            visible: pageContext.active || leaving
            enabled: pageContext.active
            z: pageContext.active ? 1 : 0
            asynchronous: true
            transform: [
                Translate {
                    id: offset
                },
                Scale {
                    id: scale
                    origin.x: pageLoader.width / 2
                    origin.y: pageLoader.height / 2
                }
            ]
            function settle(): void {
                entering.stop();
                exiting.stop();
                leaving = false;
                opacity = 1;
                offset.x = 0;
                offset.y = 0;
                scale.xScale = 1;
                scale.yScale = 1;
            }
            function change(): void {
                settle();
                if (!animateRoute || status !== Loader.Ready)
                    return;
                if (pageContext.active) {
                    opacity = recipe.enterOpacity === undefined ? 0 : recipe.enterOpacity;
                    offset.x = recipe.enterX || 0;
                    offset.y = recipe.enterY || 0;
                    scale.xScale = recipe.enterScale || 1;
                    scale.yScale = scale.xScale;
                    entering.restart();
                } else {
                    leaving = true;
                    exiting.restart();
                }
            }
            onLoaded: if (pageContext.active && recipe.appear !== false)
                change()
            onAnimateRouteChanged: if (!animateRoute)
                settle()
            Connections {
                target: pageLoader.pageContext
                function onActiveChanged(): void {
                    pageLoader.change();
                }
            }
            SequentialAnimation {
                id: entering
                PauseAnimation {
                    duration: pageLoader.recipe.mode === "out-in" ? pageLoader.durationMs : 0
                }
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
                        target: scale
                        property: "xScale"
                        to: 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: scale
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
                PauseAnimation {
                    duration: pageLoader.recipe.mode === "in-out" ? pageLoader.durationMs : 0
                }
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
                        target: scale
                        property: "xScale"
                        to: pageLoader.recipe.leaveScale || 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                    NumberAnimation {
                        target: scale
                        property: "yScale"
                        to: pageLoader.recipe.leaveScale || 1
                        duration: pageLoader.durationMs
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: pageLoader.curve
                    }
                }
                ScriptAction {
                    script: pageLoader.leaving = false
                }
            }
            Component.onCompleted: setSource(pageContext.component, Object.assign({}, pageContext.initialProperties, pageContext.pluginOwned ? {
                context: pageContext
            } : {
                context: pageContext,
                router: host.router,
                settings: host.settings
            }))
        }
    }
}
