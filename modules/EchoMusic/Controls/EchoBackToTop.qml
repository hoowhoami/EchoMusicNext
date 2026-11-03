pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Nonvisual owner. Create the complete control and shadow above the scroll
// threshold; dispose after exit. Inactive pages do not observe scroll offsets.
Item {
    id: owner
    required property Flickable scrollView
    property bool viewActive: true
    property real threshold: 300
    readonly property bool resourceActive: viewActive && visible && enabled && width > 0 && height > 0 && Window.window !== null && Window.window.visible && Window.window.visibility !== Window.Minimized
    readonly property bool wanted: resourceActive && scrollView !== null && scrollView.contentY - (scrollView.originY - scrollView.topMargin) > threshold
    readonly property bool animate: resourceActive && Theme.store.motionEnabled
    readonly property Item control: body.item as Item
    signal returnedToTop

    function returnToTop(): void {
        if (!resourceActive || !scrollView)
            return;
        scrollView.cancelFlick();
        const list = scrollView as ListView;
        const grid = scrollView as GridView;
        // Native item views own their estimated origin and delegate layout.
        if (list) {
            list.positionViewAtBeginning();
            list.forceLayout();
        } else if (grid) {
            grid.positionViewAtBeginning();
            grid.forceLayout();
        }
        // ItemView's beginning positions the first item. The page also owns
        // topMargin (flow headers); restore that real edge after native layout.
        scrollView.contentY = scrollView.originY - scrollView.topMargin;
        returnedToTop();
    }
    function retire(): void {
        if (!wanted)
            body.active = false;
    }
    function sync(): void {
        if (wanted && !body.active)
            body.active = true;
        else if (body.surface)
            body.surface.sync();
    }
    onWantedChanged: sync()
    onAnimateChanged: sync()
    Component.onCompleted: sync()

    Loader {
        id: body
        active: false
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: 36
        height: 36
        readonly property Surface surface: item as Surface
        onLoaded: body.surface.sync()
        sourceComponent: Surface {}
    }
    component Surface: Item {
        id: surface
        opacity: 0
        enabled: owner.wanted
        function sync(): void {
            fade.stop();
            if (owner.animate) {
                fade.to = owner.wanted ? 1 : 0;
                fade.start();
            } else {
                opacity = owner.wanted ? 1 : 0;
                owner.retire();
            }
        }
        NumberAnimation {
            id: fade
            target: surface
            property: "opacity"
            duration: Theme.fastDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: owner.wanted ? Theme.easeEnter : Theme.easeExit
            onFinished: owner.retire()
        }
        EchoIconButton {
            id: button
            objectName: "backToTopButton"
            anchors.fill: parent
            glyph: "arrow-up"
            text: qsTr("回到顶部")
            padding: 10
            foreground: hovered || visualFocus ? colors.text : colors.iconMain
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (!event.isAutoRepeat)
                        button.clicked();
                    event.accepted = true;
                }
            }
            scale: down && owner.animate ? 0.96 : 1
            Behavior on scale {
                enabled: owner.animate
                NumberAnimation {
                    duration: Theme.fastDuration
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.easeStandard
                }
            }
            transform: Translate {
                y: owner.animate && (button.hovered || button.visualFocus) ? -1 : 0
                Behavior on y {
                    enabled: owner.animate
                    NumberAnimation {
                        duration: Theme.fastDuration
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Theme.easeStandard
                    }
                }
            }
            contentItem: EchoIcon {
                name: button.glyph
                color: button.foreground
                transform: Translate {
                    y: owner.animate && button.hovered ? -2 : 0
                    Behavior on y {
                        enabled: owner.animate
                        NumberAnimation {
                            duration: Theme.fastDuration
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: Theme.easeStandard
                        }
                    }
                }
            }
            background: EchoActionBackground {
                colors: button.colors
                pressed: button.down
                highlighted: button.hovered || button.visualFocus
                border.width: button.visualFocus ? 2 : 1
                border.color: button.visualFocus ? Theme.accent : Theme.withAlpha(button.colors.text, button.hovered ? 0.36 : 0.22)
            }
            onClicked: owner.returnToTop()
        }
    }
}
