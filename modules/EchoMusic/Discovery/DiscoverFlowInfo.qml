pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

// Presentation snapshots retain the old text through the exit transition,
// then adopt the latest row. Business identity stays on the page.
Item {
    id: info
    property var item: null
    property var qualityTags: []
    property string progressLabel: ""
    property bool narrow: false
    property bool shortPage: false
    property bool resourceActive: true
    property var shownItem: null
    property var shownTags: []
    property string shownProgress: ""
    property int swapDuration: 0
    readonly property bool running: swap.running
    implicitHeight: content.implicitHeight
    function adopt(): void {
        shownItem = item;
        shownTags = qualityTags;
        shownProgress = progressLabel;
    }
    function settle(): void {
        swap.stop();
        adopt();
        content.opacity = 1;
        slide.y = 0;
    }
    function updateItem(): void {
        if (!resourceActive || !Theme.store.motionEnabled || !shownItem || !item) {
            settle();
            return;
        }
        if (String(shownItem.resourceId) === String(item.resourceId)) {
            if (!swap.running)
                adopt();
            return;
        }
        swapDuration = Theme.contentSwapDuration;
        swap.restart();
    }
    onItemChanged: updateItem()
    onQualityTagsChanged: {
        if (!swap.running)
            shownTags = qualityTags;
    }
    onProgressLabelChanged: {
        if (!swap.running)
            shownProgress = progressLabel;
    }
    onResourceActiveChanged: settle()
    Component.onCompleted: settle()
    Column {
        id: content
        objectName: "discoverFlowInfoContent"
        width: parent.width
        // Keep fractional line boxes in a full-height slot.
        // RowLayout rounds bottom-aligned child positions.
        y: info.height - height
        spacing: 10
        transform: Translate {
            id: slide
        }
        Item {
            width: parent.width
            height: meta.implicitHeight + 4
            Flow {
                id: meta
                objectName: "discoverFlowMeta"
                width: parent.width
                spacing: 8
                Item {
                    width: progress.implicitWidth + 4
                    height: info.shownTags.length > 0 ? 24 : 19.5
                    EchoTextTagRow {
                        id: progress
                        objectName: "discoverFlowProgress"
                        anchors.verticalCenter: parent.verticalCenter
                        width: implicitWidth
                        height: 19.5
                        text: info.shownProgress
                        color: Theme.withAlpha(Theme.text, 0.8)
                        font: {
                            let counter = Theme.font(13, Theme.regularWeight);
                            counter.features = {
                                "tnum": 1
                            };
                            return counter;
                        }
                        lineHeight: 19.5
                    }
                }
                Repeater {
                    model: info.shownTags
                    delegate: EchoTextTagRow {
                        required property string modelData
                        objectName: "discoverFlowQuality"
                        text: modelData
                        width: implicitWidth
                        height: 24
                        font: Theme.font(16, Theme.regularWeight)
                        color: Theme.text
                        lineHeight: 24
                    }
                }
            }
        }
        Text {
            id: title
            objectName: "discoverFlowTitle"
            width: parent.width
            height: Math.max(1, lineCount) * lineHeight
            text: info.shownItem ? String(info.shownItem.title || "") : ""
            textFormat: Text.PlainText
            color: Theme.text
            font: Theme.font(info.narrow || info.shortPage ? 24 : 32, Theme.boldWeight)
            lineHeightMode: Text.FixedHeight
            lineHeight: font.pixelSize * 1.3
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 2
            elide: Text.ElideRight
            wrapMode: Text.Wrap
            HoverHandler {
                id: titleHover
            }
            EchoToolTip {
                parent: title
                requestedVisible: info.resourceActive && titleHover.hovered && title.truncated
                text: title.text
            }
        }
        EchoTextTagRow {
            id: artist
            objectName: "discoverFlowArtist"
            width: parent.width
            height: lineHeight
            text: info.shownItem ? String(info.shownItem.artistText || "") : ""
            color: Theme.withAlpha(Theme.text, 0.8)
            font: Theme.font(info.narrow ? 14 : 16, Theme.regularWeight)
            lineHeight: font.pixelSize * 1.5
            HoverHandler {
                id: artistHover
                parent: artist
            }
            EchoToolTip {
                parent: artist
                requestedVisible: info.resourceActive && artistHover.hovered && artist.truncated
                text: artist.text
            }
        }
        EchoTextTagRow {
            id: album
            objectName: "discoverFlowAlbum"
            width: parent.width
            height: 19.5
            visible: text.length > 0
            text: info.shownItem ? String(info.shownItem.albumText || "") : ""
            color: Theme.withAlpha(Theme.text, 0.8)
            font: Theme.font(13, Theme.regularWeight)
            lineHeight: 19.5
            HoverHandler {
                id: albumHover
                parent: album
            }
            EchoToolTip {
                parent: album
                requestedVisible: info.resourceActive && albumHover.hovered && album.truncated
                text: album.text
            }
        }
    }
    SequentialAnimation {
        id: swap
        ParallelAnimation {
            NumberAnimation {
                target: content
                property: "opacity"
                to: 0
                duration: info.swapDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeDefault
            }
            NumberAnimation {
                target: slide
                property: "y"
                to: -8
                duration: info.swapDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeDefault
            }
        }
        ScriptAction {
            script: {
                info.adopt();
                slide.y = 8;
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: content
                property: "opacity"
                to: 1
                duration: info.swapDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeDefault
            }
            NumberAnimation {
                target: slide
                property: "y"
                to: 0
                duration: info.swapDuration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.easeDefault
            }
        }
    }
    Connections {
        target: Theme.store
        function onChanged(): void {
            if (!Theme.store.motionEnabled)
                info.settle();
        }
    }
}
