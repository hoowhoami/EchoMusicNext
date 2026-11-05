pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

Item {
    id: viewport
    required property ToastService service
    property Item anchorItem: null
    property real uiScale: 1
    // mapToItem alone does not subscribe to ancestor movement/zoom. Reuse the
    // popup geometry owner so a notice follows the final layout and resize.
    EchoPopupAnchor {
        id: anchorGeometry
        item: viewport.anchorItem
        coordinateItem: viewport.parent
    }
    readonly property point anchorStart: Qt.point(anchorGeometry.rect.x, anchorGeometry.rect.y)
    readonly property point anchorEnd: Qt.point(anchorGeometry.rect.x + anchorGeometry.rect.width, anchorGeometry.rect.y)
    readonly property bool anchored: anchorGeometry.available && anchorItem.height >= 24 && anchorItem.opacity > 0.05
    readonly property real logicalWidth: parent ? parent.width / uiScale : 460
    readonly property real logicalHeight: parent ? parent.height / uiScale : 0
    readonly property color stateColor: service.tone === ToastService.Warning ? "#f59e0b" : service.tone === ToastService.Danger ? "#ef4444" : service.tone === ToastService.Success ? "#10b981" : Theme.accent
    readonly property real borderRatio: service.tone === ToastService.Warning ? service.standard ? 0.58 : 0.40 : service.tone === ToastService.Danger ? service.standard ? 0.58 : 0.40 : service.tone === ToastService.Success ? service.standard ? 0.54 : 0.36 : service.standard ? 0.20 : 0
    width: Math.max(220, Math.min(460, logicalWidth - 24, anchored ? (anchorEnd.x - anchorStart.x) / uiScale - 24 : 460))
    height: card.height
    x: anchored ? (anchorStart.x + anchorEnd.x) / 2 - width * uiScale / 2 : (parent.width - width * uiScale) / 2
    y: Math.max(12, (anchored ? Math.min(anchorStart.y / uiScale - 12, logicalHeight - 24) : logicalHeight - 24) - height) * uiScale
    scale: uiScale
    transformOrigin: Item.TopLeft
    visible: service.visible
    z: 5000
    Accessible.role: Accessible.AlertMessage
    Accessible.name: service.title.length ? service.title + "：" + service.message : service.message
    Accessible.onPressAction: service.triggerAction(service.noticeId)
    Connections {
        target: viewport.Window.window
        function onActiveChanged() {
            if (!viewport.Window.window.active)
                viewport.service.resume(viewport.service.noticeId);
        }
    }
    // Timers/queues belong to the GUI service; passive notices never acquire
    // mouse or keyboard focus. Announce replacements as well as the first item.
    property double announcedId: 0
    property int lastRepetitions: 0
    EchoMotionScope {
        id: motion
        target: viewport
        animations: [appearance, countBump]
    }
    Connections {
        target: viewport.service
        function onChanged() {
            if (viewport.service.visible && viewport.announcedId !== viewport.service.noticeId) {
                viewport.announcedId = viewport.service.noticeId;
                viewport.Accessible.announce(viewport.Accessible.name, Accessible.Polite);
                if (motion.allowed)
                    appearance.restart();
            }
            if (viewport.service.repetitions > viewport.lastRepetitions && viewport.lastRepetitions > 0 && motion.allowed)
                countBump.restart();
            viewport.lastRepetitions = viewport.service.visible ? viewport.service.repetitions : 0;
        }
    }
    property real rise: 0
    ParallelAnimation {
        id: appearance
        NumberAnimation {
            target: card
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.noticeFadeDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeEmphasized
        }
        NumberAnimation {
            target: viewport
            property: "rise"
            from: 8
            to: 0
            duration: Theme.panelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeEmphasized
        }
        NumberAnimation {
            target: card
            property: "scale"
            from: 0.98
            to: 1
            duration: Theme.panelDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeEmphasized
        }
    }
    EchoFloatingSurface {
        id: card
        objectName: "toastCard"
        width: Math.min(viewport.width, content.implicitWidth + (viewport.service.standard ? viewport.service.title.length ? 28 : 32 : 24))
        height: viewport.service.standard ? Math.max(48, content.implicitHeight + (viewport.service.title.length ? 20 : 24)) : 36
        anchors.horizontalCenter: parent.horizontalCenter
        y: viewport.rise
        shadowEnabled: false
        border.color: Theme.mix(Theme.floating.subtleBorder, viewport.service.tone === ToastService.Info ? Theme.floatingText : viewport.stateColor, viewport.borderRatio)
        EchoSurfaceShadow {
            anchors.fill: parent
            surface: card
            z: -1
            radius: card.radius
            blur: 30
            offset: Qt.vector2d(0, 10)
            color: Qt.rgba(0, 0, 0, 0.14)
        }
        EchoSurfaceShadow {
            anchors.fill: parent
            surface: card
            z: -1
            radius: card.radius
            blur: 8
            offset: Qt.vector2d(0, 2)
            color: Qt.rgba(0, 0, 0, 0.08)
        }
        HoverHandler {
            enabled: viewport.service.standard
            onHoveredChanged: {
                if (hovered)
                    viewport.service.pause(viewport.service.noticeId);
                else
                    viewport.service.resume(viewport.service.noticeId);
            }
        }
        RowLayout {
            id: content
            anchors.fill: parent
            anchors.leftMargin: viewport.service.standard ? viewport.service.title.length ? 14 : 16 : 12
            anchors.rightMargin: anchors.leftMargin
            anchors.topMargin: viewport.service.standard ? viewport.service.title.length ? 10 : 12 : 0
            anchors.bottomMargin: anchors.topMargin
            spacing: viewport.service.standard ? viewport.service.title.length ? 10 : 12 : 8
            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                Layout.alignment: viewport.service.title.length ? Qt.AlignTop : Qt.AlignVCenter
                radius: Theme.controlRadius
                color: viewport.service.tone === ToastService.Info ? Theme.withAlpha(Theme.accent, 0.12) : "transparent"
                EchoIcon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    name: viewport.service.tone === ToastService.Warning ? "alert-circle" : viewport.service.tone === ToastService.Danger ? "circle-x" : viewport.service.tone === ToastService.Success ? "circle-check" : "info-circle"
                    color: viewport.service.tone === ToastService.Info ? Theme.floatingAccentText : viewport.stateColor
                }
            }
            ColumnLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                spacing: 2
                Label {
                    visible: viewport.service.title.length > 0
                    Layout.fillWidth: true
                    text: viewport.service.title
                    color: Theme.floatingText
                    font: Theme.font(14, Theme.semiboldWeight)
                    elide: Text.ElideRight
                }
                Label {
                    objectName: "toastMessage"
                    Layout.fillWidth: true
                    text: viewport.service.message
                    color: viewport.service.title.length ? Theme.floatingSecondaryText : Theme.floatingText
                    font: Theme.font(13, viewport.service.standard ? Theme.regularWeight : Theme.mediumWeight)
                    wrapMode: viewport.service.standard ? Text.Wrap : Text.NoWrap
                    maximumLineCount: viewport.service.title.length ? 32 : viewport.service.standard ? 2 : 1
                    elide: Text.ElideRight
                    lineHeight: 20
                    lineHeightMode: Text.FixedHeight
                }
            }
            EchoSoftButton {
                visible: viewport.service.actionLabel.length > 0
                text: viewport.service.actionLabel
                implicitHeight: 28
                leftPadding: 8
                rightPadding: 8
                colors: Theme.floating
                accent: true
                onClicked: viewport.service.triggerAction(viewport.service.noticeId)
            }
            EchoIconButton {
                visible: viewport.service.standard
                implicitWidth: 24
                implicitHeight: 24
                padding: 5
                glyph: "x"
                colors: Theme.floating
                text: qsTr("关闭提示")
                onClicked: viewport.service.dismiss(viewport.service.noticeId)
            }
        }
        EchoBadge {
            id: repeatBadge
            objectName: "toastRepetitionBadge"
            visible: viewport.service.repetitions > 1
            count: "×" + viewport.service.repetitions
            colors: Theme.floating
            tone: EchoBadge.Neutral
            placement: EchoBadge.Floating
        }
    }
    SequentialAnimation {
        id: countBump
        NumberAnimation {
            target: repeatBadge
            property: "scale"
            from: 1
            to: 1.12
            duration: Theme.noticeFadeDuration * 0.45
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeEmphasized
        }
        NumberAnimation {
            target: repeatBadge
            property: "scale"
            to: 1
            duration: Theme.noticeFadeDuration * 0.55
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.easeEmphasized
        }
    }
}
