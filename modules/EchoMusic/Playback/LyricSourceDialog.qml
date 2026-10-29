pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Playback

EchoDialog {
    id: dialog
    objectName: "lyricSourceDialog"
    required property PlaybackService playback
    readonly property LyricSourceController sources: playback.lyricSource
    heading: qsTr("选择歌词")
    preferredWidth: 800
    width: Math.min(preferredWidth, parent ? parent.width / uiScale * .92 : preferredWidth)
    maximumHeight: Infinity
    windowHeightMargin: 64
    onAboutToShow: sources.openSelection()
    onOpened: sources.preview.setPosition(playback.positionMs)
    onAboutToHide: sources.closeSelection()
    Connections {
        target: dialog.sources
        function onApplied() {
            dialog.close();
        }
    }
    Connections {
        target: dialog.playback
        function onPositionChanged() {
            if (dialog.visible)
                dialog.sources.preview.setPosition(dialog.playback.positionMs);
        }
    }
    body: Component {
        Item {
            objectName: "lyricSourceBody"
            implicitHeight: searchForm.implicitHeight + 20 + (dialog.sources.count > 0 ? Math.max(144, Math.min(320, (dialog.parent ? dialog.parent.height / dialog.uiScale : 800) - 350)) : 144)
            ColumnLayout {
                anchors.fill: parent
                spacing: 20
                LyricSourceSearchForm {
                    id: searchForm
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    sources: dialog.sources
                    narrow: dialog.width <= 480
                }
                Label {
                    Layout.fillWidth: true
                    visible: dialog.sources.errorMessage.length > 0 && dialog.sources.count > 0
                    text: dialog.sources.errorMessage
                    font: Theme.font(12, Theme.regularWeight)
                    color: Theme.floatingSecondaryText
                    wrapMode: Text.Wrap
                }
                Flickable {
                    id: paneViewport
                    objectName: "lyricSourcePanesViewport"
                    visible: dialog.sources.count > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    contentWidth: width
                    contentHeight: panes.columns === 1 && dialog.parent && dialog.parent.height / dialog.uiScale <= 600 ? Math.max(height, 396) : height
                    interactive: contentHeight > height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    GridLayout {
                        id: panes
                        objectName: "lyricSourcePanes"
                        columns: dialog.width <= 680 ? 1 : 2
                        width: paneViewport.width
                        height: paneViewport.contentHeight
                        columnSpacing: 20
                        rowSpacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            Layout.minimumHeight: 0
                            Layout.preferredWidth: panes.columns === 1 ? panes.width : Math.max(0, panes.width - panes.columnSpacing) * .45
                            Layout.preferredHeight: panes.columns === 1 ? Math.max(0, panes.height - panes.rowSpacing) * 4 / 9 : -1
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: false
                                spacing: 8
                                Label {
                                    text: qsTr("歌词版本")
                                    font: Theme.font(12, Theme.semiboldWeight)
                                    color: Theme.floatingSecondaryText
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: qsTr("%1 个").arg(dialog.sources.count) + (dialog.sources.loading ? qsTr(" · 搜索中") : "")
                                    font: Theme.font(12, Theme.regularWeight)
                                    color: Theme.floatingSecondaryText
                                }
                            }
                            ListView {
                                id: candidates
                                objectName: "lyricSourceCandidates"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: 0
                                clip: true
                                reuseItems: true
                                EchoViewPoolGuard {
                                    targetView: candidates
                                }
                                cacheBuffer: 200
                                spacing: 8
                                model: dialog.visible ? dialog.sources : null
                                delegate: Item {
                                    id: candidate
                                    objectName: "lyricSourceCandidate-" + index
                                    required property int index
                                    required property string sourceId
                                    required property string sourceTitle
                                    required property string sourceArtist
                                    required property string sourceNickname
                                    required property bool hasTranslation
                                    required property bool hasRomanization
                                    required property real durationMs
                                    required property string sourceLabel
                                    required property string language
                                    required property bool wordTimed
                                    required property bool scrollable
                                    required property bool recommended
                                    required property bool current
                                    required property int score
                                    readonly property bool selected: index === dialog.sources.selectedIndex
                                    readonly property string displayTitle: sourceTitle || dialog.playback.title || qsTr("未知歌曲")
                                    readonly property string displayArtist: sourceArtist || dialog.playback.artist
                                    width: candidates.width
                                    height: candidateButton.implicitHeight
                                    Button {
                                        id: candidateButton
                                        objectName: "lyricSourceChoice-" + candidate.index
                                        x: 0
                                        width: parent.width - 4
                                        implicitHeight: contentItem.implicitHeight + 22
                                        padding: 11
                                        leftPadding: 12
                                        rightPadding: 12
                                        hoverEnabled: true
                                        enabled: !dialog.sources.applying
                                        Accessible.name: candidate.displayTitle + (candidate.displayArtist.length ? " - " + candidate.displayArtist : "")
                                        Accessible.role: Accessible.RadioButton
                                        Accessible.checked: candidate.selected
                                        onClicked: dialog.sources.select(candidate.index)
                                        background: Rectangle {
                                            radius: Theme.itemRadius
                                            color: candidate.selected ? Theme.mix(Theme.floating.elevated, Theme.accent, candidate.recommended ? .10 : .07) : candidateButton.hovered ? Theme.mix(Theme.floating.elevated, Theme.accent, .07) : candidate.recommended ? Theme.mix(Theme.floating.elevated, Theme.accent, .06) : Theme.withAlpha(Theme.floating.tone, .07)
                                            border.width: 1
                                            border.color: candidate.selected ? Theme.mix(Theme.floating.controlBorder, Theme.accent, candidate.recommended ? .36 : .22) : candidateButton.hovered ? Theme.mix(Theme.floating.controlBorder, Theme.accent, .22) : candidate.recommended ? Theme.mix(Theme.floating.controlBorder, Theme.accent, .16) : Theme.floating.controlBorder
                                        }
                                        contentItem: ColumnLayout {
                                            spacing: 8
                                            Label {
                                                Layout.fillWidth: true
                                                Layout.rightMargin: 24
                                                text: candidate.displayTitle + (candidate.displayArtist.length ? " - " + candidate.displayArtist : "")
                                                font: Theme.font(13, Theme.boldWeight)
                                                color: Theme.floatingText
                                                elide: Text.ElideRight
                                            }
                                            RowLayout {
                                                Layout.fillHeight: false
                                                Layout.fillWidth: true
                                                spacing: 6
                                                EchoTag {
                                                    visible: candidate.recommended
                                                    text: qsTr("智能推荐")
                                                    tone: EchoTag.Accent
                                                    colors: Theme.floating
                                                }
                                                Item {
                                                    Layout.fillWidth: true
                                                }
                                                Row {
                                                    visible: candidate.sourceId !== "embedded"
                                                    spacing: 1
                                                    Accessible.name: qsTr("推荐 %1 星").arg(Math.round(candidate.score / 10) / 2)
                                                    Repeater {
                                                        model: 5
                                                        Item {
                                                            id: star
                                                            required property int index
                                                            width: 12
                                                            height: 14
                                                            Label {
                                                                anchors.fill: parent
                                                                text: "★"
                                                                font: Theme.font(12, Theme.regularWeight)
                                                                color: Theme.floatingSecondaryText
                                                            }
                                                            Item {
                                                                width: Math.max(0, Math.min(1, Math.round(candidate.score / 10) / 2 - star.index)) * 12
                                                                height: 14
                                                                clip: true
                                                                Label {
                                                                    width: 12
                                                                    text: "★"
                                                                    font: Theme.font(12, Theme.regularWeight)
                                                                    color: Theme.floating.accentText
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            Row {
                                                clip: true
                                                Layout.minimumWidth: 0
                                                Layout.fillWidth: true
                                                spacing: 6
                                                EchoTag {
                                                    visible: candidate.sourceId === "embedded" || candidate.sourceLabel === qsTr("官方推荐歌词")
                                                    text: candidate.sourceId === "embedded" ? qsTr("内嵌歌词") : qsTr("官方推荐")
                                                    tone: candidate.sourceLabel === qsTr("官方推荐歌词") ? EchoTag.Accent : EchoTag.Neutral
                                                    colors: Theme.floating
                                                }
                                                EchoTag {
                                                    visible: candidate.sourceId === "embedded" && candidate.scrollable
                                                    text: qsTr("带时间轴")
                                                    colors: Theme.floating
                                                }
                                                EchoTag {
                                                    visible: candidate.wordTimed
                                                    text: qsTr("逐字")
                                                    colors: Theme.floating
                                                }
                                                EchoTag {
                                                    visible: !candidate.scrollable
                                                    text: candidate.sourceId === "embedded" ? qsTr("纯文本") : qsTr("无法滚动")
                                                    colors: Theme.floating
                                                }
                                                EchoTag {
                                                    visible: candidate.language.length > 0
                                                    text: candidate.language
                                                    colors: Theme.floating
                                                }
                                                EchoTag {
                                                    visible: candidate.hasTranslation
                                                    text: qsTr("翻译")
                                                    colors: Theme.floating
                                                }
                                                EchoTag {
                                                    visible: candidate.hasRomanization
                                                    text: qsTr("音译")
                                                    colors: Theme.floating
                                                }
                                                EchoTag {
                                                    readonly property int difference: candidate.durationMs > 0 && dialog.playback.durationMs > 0 ? Math.round((candidate.durationMs - dialog.playback.durationMs) / 1000) : 0
                                                    visible: difference !== 0
                                                    text: (difference > 0 ? "+" : "") + difference + "s"
                                                    colors: Theme.floating
                                                }
                                            }
                                        }
                                        EchoSelectionBadge {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 10
                                            visible: candidate.selected
                                        }
                                    }
                                }
                                ScrollBar.vertical: EchoPageScrollBar {
                                    view: candidates
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            Layout.minimumHeight: 0
                            Layout.preferredWidth: panes.columns === 1 ? panes.width : Math.max(0, panes.width - panes.columnSpacing) * .55
                            Layout.preferredHeight: panes.columns === 1 ? Math.max(0, panes.height - panes.rowSpacing) * 5 / 9 : -1
                            spacing: 10
                            Label {
                                text: qsTr("歌词预览")
                                font: Theme.font(12, Theme.semiboldWeight)
                                color: Theme.floatingSecondaryText
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumWidth: 0
                                Layout.minimumHeight: 0
                                radius: Theme.cardRadius
                                color: Theme.floating.elevated
                                border.width: 1
                                border.color: Theme.floating.subtleBorder
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 0
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.margins: 12
                                        Layout.fillHeight: false
                                        spacing: 12
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 4
                                            Label {
                                                Layout.fillWidth: true
                                                text: dialog.sources.selectedTitle || dialog.playback.title || qsTr("未知歌曲")
                                                elide: Text.ElideRight
                                                font: Theme.font(13, Theme.boldWeight)
                                                color: Theme.floatingText
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: (dialog.sources.selectedSource === "ugc" ? qsTr("用户上传") : dialog.sources.selectedSource === qsTr("官方推荐歌词") ? qsTr("官方推荐") : dialog.sources.selectedSource || qsTr("未知来源")) + (dialog.sources.selectedNickname.length ? " · " + dialog.sources.selectedNickname : "")
                                                elide: Text.ElideRight
                                                font: Theme.font(12, Theme.regularWeight)
                                                color: Theme.floatingSecondaryText
                                            }
                                        }
                                        RowLayout {
                                            Layout.alignment: Qt.AlignTop
                                            spacing: 6
                                            Label {
                                                text: qsTr("%1 行").arg(dialog.sources.preview.count)
                                                font: Theme.font(11, Theme.regularWeight)
                                                color: Theme.floatingSecondaryText
                                            }
                                            Label {
                                                visible: dialog.sources.preview.scrollable
                                                text: qsTr("实时预览")
                                                font: Theme.font(11, Theme.regularWeight)
                                                color: Theme.floatingSecondaryText
                                            }
                                            Label {
                                                visible: dialog.sources.preview.hasTranslation
                                                text: qsTr("翻译")
                                                font: Theme.font(11, Theme.regularWeight)
                                                color: Theme.floatingSecondaryText
                                            }
                                            Label {
                                                visible: dialog.sources.preview.hasRomanization
                                                text: qsTr("音译")
                                                font: Theme.font(11, Theme.regularWeight)
                                                color: Theme.floatingSecondaryText
                                            }
                                        }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 1
                                        color: Theme.floating.subtleBorder
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.margins: visible ? 12 : 0
                                        visible: dialog.sources.selectedDuration > 0 && dialog.playback.durationMs > 0 && Math.abs(dialog.sources.selectedDuration - dialog.playback.durationMs) > 5000
                                        text: qsTr("这份歌词时长差异较大，可能不同步")
                                        color: Theme.floating.dangerText
                                        font: Theme.font(12, Theme.regularWeight)
                                        wrapMode: Text.Wrap
                                    }
                                    ListView {
                                        id: preview
                                        objectName: "lyricSourcePreview"
                                        topMargin: 12
                                        bottomMargin: 12
                                        spacing: 10
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        reuseItems: true
                                        EchoViewPoolGuard {
                                            targetView: preview
                                        }
                                        cacheBuffer: 200
                                        model: dialog.visible ? dialog.sources.previewRows : null
                                        delegate: Item {
                                            id: line
                                            required property int index
                                            required property string lineText
                                            required property string translation
                                            required property string romanization
                                            readonly property bool current: index === dialog.sources.preview.currentIndex
                                            objectName: "lyricPreviewSlot-" + index
                                            width: preview.width
                                            height: textLines.implicitHeight + 12
                                            // ListView owns slot positions; the 12px content inset belongs to the inner surface.
                                            Rectangle {
                                                objectName: "lyricPreviewLine-" + line.index
                                                x: 12
                                                width: parent.width - 24
                                                height: parent.height
                                                radius: Theme.cardRadius
                                                color: line.current ? Qt.rgba(20 / 255, 184 / 255, 166 / 255, .1) : "transparent"
                                                Column {
                                                    id: textLines
                                                    x: 8
                                                    y: 6
                                                    width: parent.width - 16
                                                    spacing: 3
                                                    Label {
                                                        width: parent.width
                                                        text: line.lineText
                                                        wrapMode: Text.Wrap
                                                        horizontalAlignment: Text.AlignHCenter
                                                        color: line.current ? Theme.floatingText : Theme.floatingSecondaryText
                                                        font: Theme.font(14, line.current ? 800 : Theme.regularWeight)
                                                    }
                                                    Label {
                                                        width: parent.width
                                                        visible: text.length > 0
                                                        text: [line.translation, line.romanization].filter(value => value.length).join("\n")
                                                        wrapMode: Text.Wrap
                                                        horizontalAlignment: Text.AlignHCenter
                                                        color: Theme.floatingSecondaryText
                                                        font: Theme.font(12, Theme.regularWeight)
                                                    }
                                                }
                                            }
                                        }
                                        Connections {
                                            target: dialog.sources.preview
                                            function onChanged() {
                                                if (dialog.visible) {
                                                    dialog.sources.preview.setPosition(dialog.playback.positionMs);
                                                    if (!dialog.sources.preview.scrollable)
                                                        preview.positionViewAtBeginning();
                                                }
                                            }
                                            function onCurrentChanged() {
                                                if (dialog.visible && !preview.moving && dialog.sources.preview.currentIndex >= 0)
                                                    preview.positionViewAtIndex(dialog.sources.preview.currentIndex, ListView.Center);
                                            }
                                        }
                                        ScrollBar.vertical: EchoPageScrollBar {
                                            view: preview
                                        }
                                        Label {
                                            anchors.centerIn: parent
                                            visible: dialog.sources.previewLoading || dialog.sources.preview.count === 0
                                            text: dialog.sources.previewLoading ? qsTr("正在加载预览…") : qsTr("暂无歌词预览")
                                            font: Theme.font(12, Theme.regularWeight)
                                            color: Theme.floatingSecondaryText
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: dialog.visible && dialog.sources.loading && dialog.sources.count === 0
                    visible: active
                    sourceComponent: Column {
                        spacing: 16
                        Repeater {
                            model: 2
                            Item {
                                width: parent.width
                                height: 53
                                Column {
                                    x: 12
                                    y: 10
                                    width: parent.width - 24
                                    spacing: 8
                                    EchoSkeleton {
                                        width: parent.width * .48
                                        height: 14
                                    }
                                    EchoSkeleton {
                                        width: parent.width * .28
                                        height: 11
                                    }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    objectName: "lyricSourceEmpty"
                    visible: !dialog.sources.loading && dialog.sources.count === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    Item {
                        Layout.fillHeight: true
                    }
                    EchoIcon {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 4
                        implicitWidth: 28
                        implicitHeight: 28
                        name: dialog.sources.errorMessage.length ? "alert-triangle" : "search"
                        color: Theme.floatingSecondaryText
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: dialog.sources.errorMessage.length ? qsTr("歌词搜索失败") : qsTr("没有找到匹配的歌词")
                        color: Theme.floatingText
                        font: Theme.font(14, Theme.semiboldWeight)
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: dialog.sources.errorMessage.length ? dialog.sources.errorMessage : qsTr("试试修改歌名或歌手后重新搜索")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(13, Theme.regularWeight)
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
        }
    }
    footerContent: Component {
        RowLayout {
            spacing: 12
            Item {
                Layout.fillWidth: true
            }
            EchoButton {
                objectName: "lyricSourceRefreshAction"
                size: EchoControlSize.Standard
                colors: Theme.floating
                accent: false
                text: qsTr("重新匹配")
                enabled: !dialog.sources.loading && !dialog.sources.applying
                onClicked: dialog.sources.refresh()
            }
            EchoButton {
                objectName: "lyricSourceRestoreAction"
                visible: dialog.sources.manual
                size: EchoControlSize.Standard
                colors: Theme.floating
                accent: false
                text: qsTr("恢复智能推荐")
                enabled: !dialog.sources.applying
                onClicked: dialog.sources.restoreAuto()
            }
            EchoButton {
                objectName: "lyricSourceApplyAction"
                visible: dialog.sources.selectedIndex >= 0
                size: EchoControlSize.Standard
                colors: Theme.floating
                loading: dialog.sources.applying
                text: qsTr("应用")
                enabled: !dialog.sources.applying
                onClicked: dialog.sources.applySelected()
            }
        }
    }
}
