pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell

Item {
    id: bar
    EchoUiPart {
        id: publicSurface
        target: bar
        role: "shell.titlebar"
        key: "shell.titlebar"
        properties: ["background", "borderColor", "borderWidth", "radius", "opacity"]
    }
    opacity: publicSurface.value("opacity", 1)
    Loader {
        anchors.fill: parent
        z: -0.5
        active: bar.visible && bar.Window.window !== null && bar.Window.window.visible && bar.Window.window.visibility !== Window.Minimized && Object.keys(publicSurface.overrides).some(name => name !== "opacity")
        sourceComponent: Rectangle {
            color: publicSurface.value("background", "transparent")
            radius: publicSurface.value("radius", 0)
            border.color: publicSurface.value("borderColor", "transparent")
            border.width: publicSurface.value("borderWidth", 0)
        }
    }
    required property RouteController router
    required property WindowController windowService
    required property Window window
    required property real uiScale
    required property ShellController controller
    required property ShellSettings settings
    signal focusSearchRequested
    implicitHeight: Math.max(46, 35 / uiScale)
    // Feature-owned search UI is composed by App, not imported into Shell.
    required property Component searchControl
    function focusSearch() {
        focusSearchRequested();
    }
    function updateCapacity() {
        // Use the fixed nav/window budget, never the current toolbar count.
        // Measuring the growing toolbar would make capacity oscillate.
        const fixed = 350 + windowActions.implicitWidth + 34 + 12 + 4 + 64;
        controller.titlebarActions.setCapacity(ShellActionModel.Toolbar, width < 400 ? 0 : Math.max(0, Math.floor((width - 28 - fixed) / 38)));
    }
    onWidthChanged: updateCapacity()
    Component.onCompleted: updateCapacity()
    // Native move/resize keeps snap, multi-monitor and Wayland behavior in the OS.
    EchoWindowDragArea {
        anchors.fill: parent
        excludedItems: [navigation, toolbar, more, windowActions]
        onMoveRequested: bar.window.startSystemMove()
        onDoubleClickRequested: bar.windowService.toggleMaximized()
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 4
        RowLayout {
            id: navigation
            Layout.fillHeight: false
            visible: bar.width >= 400
            Layout.preferredWidth: 350
            Layout.maximumWidth: 350
            Layout.minimumWidth: 0
            Layout.fillWidth: true
            spacing: 4
            EchoIconButton {
                hoverWhenDisabled: true
                objectName: "titlebarBackAction"
                glyph: "chevron-left"
                text: qsTr("后退")
                enabled: bar.router.canGoBack
                onClicked: bar.router.back()
                padding: 6
            }
            EchoIconButton {
                hoverWhenDisabled: true
                objectName: "titlebarForwardAction"
                glyph: "chevron-right"
                text: qsTr("前进")
                enabled: bar.router.canGoForward
                onClicked: bar.router.forward()
                padding: 6
            }
            EchoIconButton {
                hoverWhenDisabled: true
                glyph: "refresh"
                text: qsTr("刷新")
                onClicked: bar.router.refresh()
            }
            Loader {
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                Layout.maximumWidth: 220
                Layout.preferredHeight: 30
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                sourceComponent: bar.searchControl
            }
        }
        ListView {
            id: toolbar
            objectName: "titlebarToolbar"
            visible: bar.width >= 400 && count > 0
            Layout.preferredWidth: count * 38
            Layout.preferredHeight: 34
            orientation: ListView.Horizontal
            interactive: false
            reuseItems: true
            cacheBuffer: 0
            spacing: 4
            model: ShellActionProjection {
                actions: bar.controller.titlebarActions
                zone: ShellActionModel.Toolbar
            }
            readonly property bool motionActive: visible && bar.window.visible && bar.window.visibility !== Window.Minimized && Theme.store.motionEnabled
            onMotionActiveChanged: if (!motionActive) {
                moveAnimation.complete();
                displacedAnimation.complete();
            }
            move: Transition {
                enabled: toolbar.motionActive
                NumberAnimation {
                    id: moveAnimation
                    properties: "x,y"
                    duration: Theme.normalDuration
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.easeStandard
                }
            }
            moveDisplaced: Transition {
                enabled: toolbar.motionActive
                NumberAnimation {
                    id: displacedAnimation
                    properties: "x,y"
                    duration: Theme.normalDuration
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.easeStandard
                }
            }
            delegate: ShellActionButton {
                id: actionButton
                actions: bar.controller.titlebarActions
                projection: ShellActionModel.Toolbar
                property bool pooled: false
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                visualContentActive: !pooled
                ShellActionDragHandle {
                    anchors.fill: parent
                    button: actionButton
                    actionKey: actionButton.actionKey
                    glyph: actionButton.glyph
                    foreground: actionButton.foreground
                    iconOpacity: actionButton.enabled ? 1 : actionButton.disabledOpacity
                    dragLayer: bar
                    resourceActive: !actionButton.pooled && toolbar.visible && bar.window.visible && bar.window.visibility !== Window.Minimized
                }
                DropArea {
                    // Unavailable commands remain valid reorder destinations.
                    // Keep hit testing outside the disabled button subtree.
                    parent: toolbar.contentItem
                    x: actionButton.x
                    y: actionButton.y
                    width: actionButton.width
                    height: actionButton.height
                    keys: ["titlebar-actions"]
                    onDropped: drop => {
                        const key = drop.source.objectName;
                        const target = actionButton.actionKey;
                        const before = drop.x < width / 2;
                        const actions = bar.controller.titlebarActions;
                        Qt.callLater(() => {
                            if (before)
                                actions.moveBefore(key, target, ShellActionModel.Toolbar);
                            else
                                actions.moveAfter(key, target, ShellActionModel.Toolbar);
                        });
                        drop.acceptProposedAction();
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            EchoWindowDragArea {
                anchors.fill: parent
                onMoveRequested: bar.window.startSystemMove()
                onDoubleClickRequested: bar.windowService.toggleMaximized()
            }
        }
        EchoIconButton {
            id: more
            hoverWhenDisabled: true
            objectName: "titlebarMoreAction"
            visible: bar.width >= 400
            Layout.leftMargin: 12
            glyph: "dots"
            text: qsTr("更多")
            onClicked: moreMenuOwner.open()
        }
        Rectangle {
            visible: bar.width >= 400
            Layout.preferredWidth: 1
            Layout.preferredHeight: 20
            Layout.leftMargin: 1.5
            Layout.rightMargin: 1.5
            color: Theme.border
        }
        WindowActions {
            id: windowActions
            objectName: "titlebarWindowActions"
            service: bar.windowService
            settings: bar.settings
            compact: bar.width < 400
            onImplicitWidthChanged: bar.updateCapacity()
        }
    }
    EchoPopupLoader {
        id: moreMenuOwner
        objectName: "titlebarMorePopupOwner"
        hostItem: bar
        sourceComponent: Component {
            TitleBarMorePopup {
                id: moreMenu
                anchorItem: more
                actions: bar.controller.titlebarActions
                uiScale: bar.uiScale
            }
        }
    }
    Connections {
        target: bar.router
        function onDismissOverlays() {
            moreMenuOwner.close();
        }
    }
}
