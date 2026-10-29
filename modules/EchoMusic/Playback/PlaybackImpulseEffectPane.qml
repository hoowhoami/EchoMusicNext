pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

ColumnLayout {
    id: pane
    required property PlaybackAudioControls audio
    required property Popup surface
    property real uiScale: 1
    property int libraryTab: 0
    property int sourceTab: Math.max(0, sources.indexOf(selectedFile ? selectedFile.source : "local"))
    signal libraryPicked(int index)
    signal plazaRequested(string source)
    readonly property var sources: ["local", "artist", "headphone", "market"]
    readonly property var sourceLabels: [qsTr("本地导入"), qsTr("歌手音效"), qsTr("耳机音效"), qsTr("音效市场")]
    readonly property var files: audio.impulseResponses.filter(file => file.source === sources[sourceTab])
    readonly property var selectedFile: audio.impulseResponses.find(file => file.path === audio.impulseResponse)
    readonly property bool effectActive: audio.impulseEnabled || audio.providerEffectActive
    readonly property bool myLibraryContainsActive: audio.impulseEnabled && selectedFile !== undefined
    readonly property bool engineLibraryContainsActive: !audio.impulseEnabled && audio.providerEffectActive
    spacing: 8
    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        Layout.topMargin: -2
        implicitHeight: 59
        radius: Theme.cardRadius
        color: pane.effectActive ? Theme.withAlpha(Theme.floating.accent, 0.07) : Theme.floating.mutedSurface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.topMargin: 9
            anchors.bottomMargin: 9
            spacing: 12
            ColumnLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                spacing: 2
                Button {
                    id: currentEffect
                    Layout.fillWidth: true
                    implicitHeight: 26
                    padding: 0
                    hoverEnabled: true
                    text: pane.audio.impulseEnabled && pane.selectedFile ? pane.selectedFile.name : pane.audio.providerEffectActive ? pane.audio.providerPresets.filter(p => p.id === pane.audio.providerPresetId).map(p => p.label).join("") || qsTr("自定义调节") : qsTr("原声")
                    font: Theme.font(12, Theme.boldWeight)
                    contentItem: EchoTextTagRow {
                        text: currentEffect.text
                        font: currentEffect.font
                        color: pane.effectActive ? Theme.floating.accentText : Theme.floating.text
                        lineHeight: 26
                    }
                    background: Item {}
                    onClicked: {
                        if (pane.audio.impulseEnabled && pane.selectedFile) {
                            pane.sourceTab = pane.sources.indexOf(pane.selectedFile.source);
                            pane.libraryPicked(1);
                        } else {
                            pane.libraryPicked(0);
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: pane.audio.providerId.length > 0 ? pane.audio.providerName : qsTr("由内置音效引擎处理")
                    color: Theme.floating.secondaryText
                    font: Theme.font(9, 550)
                    elide: Text.ElideRight
                }
            }
            EchoSoftButton {
                Layout.fillWidth: false
                Layout.fillHeight: false
                Layout.maximumWidth: implicitWidth
                text: qsTr("原声")
                colors: Theme.floating
                implicitHeight: 26
                implicitWidth: Math.max(46, contentItem.implicitWidth + 20)
                leftPadding: 10
                rightPadding: 10
                font: Theme.font(10, Theme.boldWeight)
                accent: !pane.effectActive
                enabled: pane.effectActive && !pane.audio.effectPending
                opacity: 1
                onClicked: {
                    if (pane.audio.providerId.length > 0)
                        pane.audio.selectProviderPreset("");
                    else
                        pane.audio.impulseEnabled = false;
                }
                EchoStatusDot {
                    objectName: "originalEffectCurrentDot"
                    visible: !pane.effectActive
                    x: parent.width - 7
                    y: 3
                    width: 4
                    height: 4
                    haloWidth: 2
                    color: Theme.floating.accent
                }
            }
        }
    }
    Rectangle {
        visible: pane.audio.providerId.length === 0
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        implicitHeight: mixContent.implicitHeight + 17
        radius: Theme.cardRadius
        color: Theme.floating.mutedSurface
        ColumnLayout {
            id: mixContent
            x: 10
            y: 9
            width: parent.width - 20
            spacing: 5
            RowLayout {
                Layout.fillHeight: false
                spacing: 5
                Layout.fillWidth: true
                Label {
                    text: qsTr("音效强度")
                    font: Theme.font(11, Theme.boldWeight)
                    color: Theme.floating.text
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    text: pane.audio.impulseEnabled ? qsTr("原声与当前音效的混合比例") : qsTr("启用空间音效后可调节")
                    font: Theme.font(9, Theme.regularWeight)
                    color: Theme.floating.secondaryText
                }
            }
            EchoSlider {
                Layout.fillWidth: true
                colors: Theme.floating
                showValue: true
                valueSuffix: "%"
                from: 0
                to: 100
                stepSize: 1
                value: pane.audio.impulseMix * 100
                enabled: pane.audio.impulseEnabled
                Accessible.name: qsTr("音效强度")
                onMoved: pane.audio.impulseMix = value / 100
            }
            RowLayout {
                Layout.fillHeight: false
                spacing: 5
                Layout.fillWidth: true
                Label {
                    text: qsTr("原声")
                    font: Theme.font(9, Theme.semiboldWeight)
                    color: Theme.floating.secondaryText
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("完整音效")
                    font: Theme.font(9, Theme.semiboldWeight)
                    color: Theme.floating.secondaryText
                }
            }
        }
    }
    EchoTabBar {
        objectName: "effectLibraryTabs"
        // Compact content navigation leaves the 32px sidebar unchanged.
        compact: true
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        colors: Theme.floating
        labels: [qsTr("引擎预设"), qsTr("我的音效")]
        markedTabs: [pane.engineLibraryContainsActive, pane.myLibraryContainsActive]
        markerDescription: qsTr("当前音效位于%1")
        currentIndex: pane.libraryTab
        onPicked: index => pane.libraryPicked(index)
    }
    Item {
        visible: pane.libraryTab === 1
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        implicitHeight: 34
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.floating.controlBorder
        }
        RowLayout {
            width: parent.width
            spacing: 4
            Repeater {
                model: pane.sourceLabels
                delegate: Button {
                    id: sourceButton
                    required property int index
                    required property string modelData
                    readonly property bool containsActive: pane.myLibraryContainsActive && pane.selectedFile.source === pane.sources[index]
                    objectName: "myEffectSource-" + index
                    Layout.preferredWidth: 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    padding: 2
                    text: modelData
                    hoverEnabled: true
                    contentItem: Item {
                        EchoTextTagRow {
                            id: sourceText
                            anchors.centerIn: parent
                            text: sourceButton.text
                            font: Theme.font(11, Theme.semiboldWeight)
                            lineHeight: 20
                            spacing: 6
                            color: pane.sourceTab === sourceButton.index || sourceButton.hovered || sourceButton.containsActive ? Theme.floating.accentText : Theme.floating.secondaryText
                            EchoStatusDot {
                                objectName: "myEffectSourceCurrentDot"
                                visible: sourceButton.containsActive
                                color: Theme.floating.accent
                                accessibleName: qsTr("当前音效位于%1").arg(sourceButton.text)
                            }
                            EchoBadge {
                                count: pane.audio.impulseResponses.filter(file => file.source === pane.sources[sourceButton.index]).length
                                colors: Theme.floating
                                tone: pane.sourceTab === sourceButton.index || sourceButton.containsActive ? EchoBadge.Accent : EchoBadge.Muted
                            }
                        }
                    }
                    background: Item {
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: pane.sourceTab === sourceButton.index ? Theme.floating.accent : "transparent"
                        }
                    }
                    onClicked: pane.sourceTab = index
                }
            }
        }
    }
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Column {
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 5
            visible: pane.libraryTab === 0 && pane.audio.providerId.length === 0 && (pane.audio.providers.busy || pane.audio.providers.count === 0)
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: pane.audio.providers.busy ? qsTr("正在查找音效引擎") : qsTr("尚未导入音效引擎")
                font: Theme.font(12, Theme.boldWeight)
                color: Theme.floating.text
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: pane.audio.providers.busy ? qsTr("正在读取已安装的第三方音效引擎") : qsTr("请前往“设置 → 音效管理”导入第三方音效引擎")
                font: Theme.font(10, Theme.semiboldWeight)
                color: Theme.floating.secondaryText
            }
        }
        ListView {
            id: installedProviders
            anchors.fill: parent
            visible: pane.libraryTab === 0 && pane.audio.providerId.length === 0 && !pane.audio.providers.busy && pane.audio.providers.count > 0
            model: pane.surface.visible && visible ? pane.audio.providers : null
            leftMargin: 10
            rightMargin: 10
            bottomMargin: 12
            spacing: 14
            reuseItems: true
            EchoViewPoolGuard {
                targetView: installedProviders
            }
            clip: true
            delegate: DspInstalledProviderCard {
                width: installedProviders.width - 20
                height: implicitHeight
                audio: pane.audio
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: pane.surface
                view: installedProviders
                resourceActive: pane.surface.visible && installedProviders.visible
            }
        }
        DspProviderPanel {
            anchors.fill: parent
            audio: pane.audio
            editorHost: pane.parent
            visible: pane.libraryTab === 0 && pane.audio.providerId.length > 0
            uiScale: pane.uiScale
        }
        GridView {
            id: localEffects
            anchors.fill: parent
            topMargin: 8
            leftMargin: 8
            rightMargin: 8
            visible: pane.libraryTab === 1
            cellWidth: (width - 16) / 3
            cellHeight: 50
            model: pane.surface.visible && visible ? pane.files : null
            reuseItems: true
            EchoViewPoolGuard {
                targetView: localEffects
            }
            clip: true
            cacheBuffer: cellHeight * 3
            delegate: PlaybackEffectPresetButton {
                required property var modelData
                width: localEffects.cellWidth - 8
                minimumHeight: 42
                font: Theme.font(12, Theme.semiboldWeight)
                text: modelData.name
                enabled: modelData.available
                accent: pane.audio.impulseEnabled && pane.audio.impulseResponse === modelData.path
                onClicked: pane.audio.selectImpulseResponse(modelData.path)
            }
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: pane.surface
                view: localEffects
                resourceActive: pane.surface.visible && localEffects.visible
            }
        }
        Column {
            x: 16
            y: 32
            width: parent.width - 32
            spacing: 7
            visible: pane.libraryTab === 1 && localEffects.count === 0
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: pane.sourceTab === 0 ? qsTr("暂无本地导入音效") : qsTr("此分类暂无已下载音效")
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floating.secondaryText
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: pane.sourceTab === 0 ? qsTr("在「设置 → 音效管理」中导入音效文件") : qsTr("前往音效广场，找到喜欢的音效")
                font: Theme.font(10, Theme.regularWeight)
                color: Theme.floating.secondaryText
            }
            EchoSoftButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: pane.sourceTab !== 0
                text: qsTr("去音效广场")
                implicitHeight: 28
                colors: Theme.floating
                font: Theme.font(11, Theme.regularWeight)
                onClicked: pane.plazaRequested(pane.sources[pane.sourceTab])
            }
        }
    }
}
