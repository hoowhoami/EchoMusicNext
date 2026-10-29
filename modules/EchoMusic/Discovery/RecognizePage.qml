pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Discovery

RoutePage {
    id: page
    objectName: "recognizePage"
    required property RecognitionService service
    property Item sourceAnchor: null
    property bool stageReady: false
    readonly property Component stageComponent: page.service.state === RecognitionService.Success ? result : recorder
    onStageComponentChanged: {
        if (!stageReady)
            return;
        if (Theme.store.motionEnabled && page.resourceActive)
            stageTransition.restart();
        else
            stage.sourceComponent = stageComponent;
    }
    component SourceOption: EchoSoftButton {
        id: option
        property bool selected: false
        colors: Theme.floating
        font: Theme.font(12, Theme.mediumWeight)
        leftPadding: 10
        rightPadding: 10
        topPadding: 7
        bottomPadding: 7
        implicitHeight: Math.max(32, sourceContent.implicitHeight + 14)
        foreground: selected ? colors.accentText : hovered ? colors.text : colors.secondaryText
        contentItem: RowLayout {
            id: sourceContent
            spacing: 6
            EchoIcon {
                visible: option.glyph.length > 0
                name: option.glyph
                color: option.foreground
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
            }
            EchoTextTagRow {
                text: option.text
                font: option.font
                color: option.foreground
                wrap: true
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: option.selected ? Theme.withAlpha(option.colors.accent, 0.1) : option.hovered ? Theme.withAlpha(option.colors.text, 0.06) : "transparent"
            border.width: option.visualFocus ? 2 : 0
            border.color: Theme.accent
        }
    }
    readonly property bool resourceActive: pageActive && visible && (!Window.window || (Window.window.visible && Window.window.visibility !== Window.Minimized))
    readonly property string sourceGlyph: page.service.source === "mic" ? "microphone" : "device-speaker"
    readonly property var inputs: [
        {
            id: "default",
            name: qsTr("系统默认")
        }
    ].concat(page.service.audio.inputDevices)
    readonly property string sourceLabel: page.service.source === "system" ? qsTr("系统音频") : (inputs.find(device => device.id === page.service.audio.inputDevice)?.name || qsTr("麦克风"))
    onResourceActiveChanged: {
        page.service.setAvailable(resourceActive);
        if (!resourceActive) {
            sourceOwner.close();
            stageTransition.stop();
            stage.opacity = 1;
            stage.entranceOffset = 0;
            stage.sourceComponent = page.stageComponent;
        } else
            page.service.audio.refreshInputs();
    }
    Component.onCompleted: {
        stage.sourceComponent = page.stageComponent;
        page.stageReady = true;
        page.service.setAvailable(resourceActive);
        if (resourceActive)
            page.service.audio.refreshInputs();
    }
    Component.onDestruction: page.service.setAvailable(false)
    Flickable {
        id: scroll
        objectName: "recognitionScroll"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, heading.y + heading.implicitHeight + 40 + stage.implicitHeight)
        ScrollBar.vertical: EchoScrollBar {}
        Label {
            id: heading
            x: 32
            y: 24
            text: qsTr("听歌识曲")
            font: Theme.spacedFont(22, Theme.extraBoldWeight, -0.3)
            color: Theme.text
        }
        Loader {
            id: stage
            objectName: "recognitionStage"
            x: 32
            y: heading.y + heading.implicitHeight + entranceOffset
            width: Math.max(0, scroll.width - 64)
            height: Math.max(0, scroll.contentHeight - heading.y - heading.implicitHeight - 40)
            property real entranceOffset: 0
            opacity: 1
        }
        SequentialAnimation {
            id: stageTransition
            ParallelAnimation {
                NumberAnimation {
                    target: stage
                    property: "opacity"
                    to: 0
                    duration: 350
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.4, 0, 0.2, 1, 1, 1]
                }
                NumberAnimation {
                    target: stage
                    property: "entranceOffset"
                    to: -12
                    duration: 350
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.4, 0, 0.2, 1, 1, 1]
                }
            }
            ScriptAction {
                script: {
                    stage.sourceComponent = page.stageComponent;
                    stage.entranceOffset = 16;
                }
            }
            ParallelAnimation {
                NumberAnimation {
                    target: stage
                    property: "opacity"
                    to: 1
                    duration: 350
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.4, 0, 0.2, 1, 1, 1]
                }
                NumberAnimation {
                    target: stage
                    property: "entranceOffset"
                    to: 0
                    duration: 350
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.4, 0, 0.2, 1, 1, 1]
                }
            }
        }
    }
    EchoBackToTop {
        anchors.fill: parent
        z: 50
        scrollView: scroll
        viewActive: page.resourceActive
    }
    Component {
        id: recorder
        Item {
            implicitHeight: controls.implicitHeight
            ColumnLayout {
                id: controls
                anchors.centerIn: parent
                width: Math.min(280, parent.width)
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Layout.bottomMargin: 40
                    text: page.service.state === RecognitionService.Recording ? qsTr("正在聆听... %1s").arg(page.service.recordingSeconds) : page.service.state === RecognitionService.Recognizing ? qsTr("识别中，请稍候...") : page.service.state === RecognitionService.Failed ? page.service.errorMessage : qsTr("让我听听周围在放什么歌")
                    color: Theme.secondaryText
                    font: Theme.font(13, Theme.regularWeight)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                RecognitionOrb {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 24
                    recording: page.service.state === RecognitionService.Recording
                    recognizing: page.service.state === RecognitionService.Recognizing
                    resourceActive: page.resourceActive
                    glyph: page.sourceGlyph
                    onClicked: page.service.state === RecognitionService.Recording ? page.service.stop() : page.service.start()
                }
                EchoSoftButton {
                    id: sourceToggle
                    objectName: "recognitionSourceToggle"
                    Component.onCompleted: page.sourceAnchor = sourceToggle
                    EchoToolTip {
                        parent: sourceToggle
                        requestedVisible: sourceToggle.hovered && sourceLabelText.truncated
                        text: page.sourceLabel
                    }
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 220
                    implicitHeight: 32
                    leftPadding: 14
                    rightPadding: 14
                    contentItem: RowLayout {
                        spacing: 5
                        EchoIcon {
                            name: page.sourceGlyph
                            color: sourceToggle.foreground
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                        }
                        EchoTextTagRow {
                            id: sourceLabelText

                            text: page.sourceLabel
                            font: sourceToggle.font
                            color: sourceToggle.foreground
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            fillTextWidth: true
                        }
                        EchoIcon {
                            name: "chevron-down"
                            color: Theme.colors.iconMain
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            Layout.leftMargin: 2
                        }
                    }
                    text: page.sourceLabel
                    glyph: page.sourceGlyph
                    glyphSize: 14
                    enabled: !page.service.busy
                    opacity: enabled ? 1 : 0.35
                    font: Theme.font(12, Theme.mediumWeight)
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: "transparent"
                        border.width: 1
                        border.color: sourceToggle.hovered || sourceToggle.visualFocus ? Theme.accent : Theme.withAlpha(Theme.text, 0.1)
                    }
                    foreground: hovered ? Theme.accentText : Theme.secondaryText
                    onClicked: {
                        page.service.audio.refreshInputs();
                        sourceOwner.open();
                    }
                }
                EchoSoftButton {
                    objectName: "recognitionRetry"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 16
                    visible: page.service.state === RecognitionService.Failed
                    text: qsTr("重新识别")
                    onClicked: page.service.start()
                }
            }
        }
    }
    Component {
        id: result
        ColumnLayout {
            spacing: 0
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 20
                Layout.bottomMargin: 16
                text: qsTr("识别结果 · 共 %1 个匹配").arg(page.service.matches.count)
                font: Theme.spacedFont(12, Theme.semiboldWeight, 1.2)
                color: Theme.withAlpha(Theme.accentText, 0.7)
            }
            ListView {
                id: candidates
                objectName: "recognitionMatches"
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(640, parent.width)
                clip: true
                spacing: 8
                reuseItems: true
                EchoViewPoolGuard {
                    targetView: candidates
                }
                cacheBuffer: 80
                model: page.service.matches
                leftMargin: 2
                rightMargin: 8
                topMargin: 2
                bottomMargin: 2
                ScrollBar.vertical: EchoScrollBar {}
                delegate: RecognitionMatchRow {
                    required property int index
                    required property string resourceId
                    required property string artistText
                    required property string albumText
                    required property string coverUrl
                    width: candidates.width - 10
                    songId: resourceId
                    artist: artistText
                    album: albumText
                    cover: coverUrl
                    score: page.service.confidence(index)
                    actions: page.service.actions
                    onAddSongRequested: songId => page.service.addSong(songId)
                    resourceActive: page.resourceActive && page.service.state === RecognitionService.Success
                }
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 20
                text: qsTr("重新识别")
                glyph: "microphone"
                glyphSize: 15
                font: Theme.font(13, Theme.semiboldWeight)
                onClicked: page.service.reset()
            }
        }
    }
    FontMetrics {
        id: deviceMetrics
        font: Theme.font(12, Theme.mediumWeight)
    }
    EchoPopupLoader {
        id: sourceOwner
        objectName: "recognitionSourceOwner"
        hostItem: page
        sourceComponent: EchoAnchoredPopup {
            id: sourceMenu
            objectName: "recognitionSourceMenu"
            anchorItem: page.sourceAnchor
            uiScale: page.settings.uiScale
            contentAlign: "center"
            sideOffset: 6
            preferredWidth: Math.min(480, Math.max(180, ...page.inputs.map(device => deviceMetrics.advanceWidth(device.name) + 44)))
            maximumHeight: 384
            contentItem: ColumnLayout {
                spacing: 0
                Label {
                    Layout.leftMargin: 10
                    Layout.topMargin: 4
                    Layout.bottomMargin: 2
                    text: qsTr("系统")
                    font: Theme.font(11, Theme.semiboldWeight)
                    color: Theme.floating.secondaryText
                }
                SourceOption {
                    Layout.fillWidth: true
                    text: qsTr("系统音频")
                    glyph: "device-speaker"
                    glyphSize: 13
                    selected: page.service.source === "system"
                    colors: Theme.floating
                    onClicked: {
                        page.service.source = "system";
                        sourceMenu.close();
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    implicitHeight: 1
                    color: Theme.withAlpha(Theme.floating.text, 0.08)
                }
                Label {
                    Layout.leftMargin: 10
                    Layout.topMargin: 4
                    Layout.bottomMargin: 2
                    text: qsTr("麦克风")
                    font: Theme.font(11, Theme.semiboldWeight)
                    color: Theme.floating.secondaryText
                }
                ListView {
                    id: microphones
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(240, model.length * 32)
                    clip: true
                    model: page.inputs
                    reuseItems: true
                    EchoViewPoolGuard {
                        targetView: microphones
                    }
                    ScrollBar.vertical: EchoPopupScrollBar {
                        surface: sourceMenu
                        view: microphones
                    }
                    delegate: SourceOption {
                        required property var modelData
                        width: microphones.width
                        text: modelData.name
                        colors: Theme.floating
                        selected: page.service.source === "mic" && page.service.audio.inputDevice === modelData.id
                        onClicked: {
                            page.service.audio.inputDevice = modelData.id;
                            page.service.source = "mic";
                            sourceMenu.close();
                        }
                    }
                }
            }
        }
    }
}
