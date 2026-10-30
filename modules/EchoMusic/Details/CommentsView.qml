pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

Item {
    id: root
    objectName: "commentsView"
    required property CommentsController controller
    property EchoPageScrollContainer pageContainer: null
    property bool resourceActive: true
    property bool compact: false
    property Item scrollbarHost: null
    readonly property bool listLoading: compact ? controller.fetching : controller.loading
    readonly property bool listStateVisible: compact && showComments && !controller.hasComments
    readonly property real bodyHeight: extent.totalHeight + (listStateVisible ? stateBody.implicitHeight : 0) || (compact && list.count > 0 ? CommentListState.LoadingHeight : 0)
    property bool showComments: true
    property bool showSections: true
    property real topMargin: 0
    property real contentInset: 24
    property real uiScale: 1
    property Component detailContent: null
    signal sectionPicked(int index)
    readonly property alias listView: list
    property bool retiringProjection: false
    // Cached comments can create a new TableView before CommentExtent's queued
    // first measurement. Its temporary zero height is not the final page bound.
    readonly property bool extentReady: !showComments || !controller.hasComments || extent.totalHeight > 0
    Binding {
        target: root.pageContainer
        property: "extentReady"
        when: root.pageContainer !== null && root.pageContainer.scrollView === list
        value: root.extentReady
    }
    function checkMore(): void {
        if (root.resourceActive && root.showComments && root.controller.hasMore && !root.listLoading && (root.compact || !root.controller.errorMessage.length) && list.contentHeight - list.contentY - list.height < (root.compact ? 300 : 400))
            root.controller.loadMore();
    }
    onResourceActiveChanged: if (!resourceActive) {
        deleteOwner.close();
    }
    CommentExtent {
        id: extent
        compact: root.compact
        controller: root.showComments ? root.controller : null
        active: root.resourceActive
        layoutWidth: list.width
        viewportWidth: root.Window.window ? root.Window.window.width / root.uiScale : root.width
        bodyFont: Theme.font(root.compact ? 12 : 14, Theme.regularWeight)
        timeFont: Theme.font(10, Theme.regularWeight)
        quoteFont: Theme.font(12, Theme.regularWeight)
        // Replacing the provider schedules Qt's own deferred viewport rebuild;
        // forceLayout can re-enter an asynchronously incubating table layout.
        onChanged: if (root.resourceActive)
            list.rowHeightProvider = row => extent.heightAt(row)
    }
    TableView {
        id: list
        objectName: "commentsList"
        x: root.contentInset
        width: Math.max(0, parent.width - 2 * root.contentInset)
        height: parent.height
        topMargin: root.topMargin + flowHeader.height
        contentY: -topMargin
        bottomMargin: (root.showComments ? composer.height + (root.compact ? 48 : 52) : 40) + (flowFooter.visible ? flowFooter.height : 0)
        model: root.showComments && !root.retiringProjection ? root.controller.model : null
        reuseItems: true
        pointerNavigationEnabled: false
        keyNavigationEnabled: false
        flickableDirection: Flickable.VerticalFlick
        readonly property int count: rows
        readonly property real spacing: rowSpacing
        rowSpacing: 0
        columnWidthProvider: column => width
        rowHeightProvider: row => extent.heightAt(row)
        contentWidth: width
        contentHeight: root.showComments ? root.bodyHeight : 0
        function rowItemAt(index: int): Item {
            return itemAtCell(Qt.point(0, index));
        }
        function positionViewAtBeginning(): void {
            cancelFlick();
            contentY = -topMargin;
        }
        function positionViewAtEnd(): void {
            cancelFlick();
            contentY = Math.max(-topMargin, contentHeight - height + bottomMargin);
        }
        boundsBehavior: Flickable.StopAtBounds
        onMovementEnded: root.checkMore()
        onContentYChanged: root.checkMore()
        ScrollBar.vertical: EchoPageScrollBar {
            parent: root.scrollbarHost || (root.pageContainer ? root.pageContainer.scrollbarLayer : list)
            view: list
            container: root.pageContainer
            viewActive: root.resourceActive
        }
        Column {
            id: flowHeader
            parent: list.contentItem
            y: -height
            width: list.width
            spacing: 0
            // Include the sticky main-tab's 14px bottom margin,
            // then the comment shell and sub-tab's 8px top spacing.
            Item {
                visible: root.showComments && root.showSections
                width: parent.width
                height: 22
            }
            Loader {
                width: parent.width
                active: !root.showComments && root.detailContent !== null
                visible: active
                sourceComponent: root.detailContent
            }
            EchoTabBar {
                objectName: "commentSections"
                width: parent.width
                visible: root.showComments && root.showSections
                labels: [qsTr("全部评论"), qsTr("分类评论"), qsTr("热词评论")]
                currentIndex: root.controller.tab
                onPicked: index => {
                    root.controller.tab = index;
                    root.sectionPicked(index);
                }
            }
            Item {
                visible: root.showComments && root.showSections
                width: parent.width
                height: 16
            }
            ListView {
                id: chips
                objectName: "commentFilterChips"
                visible: root.showComments && root.showSections && root.controller && root.controller.tab > 0
                width: parent.width
                // The chip row includes top/bottom padding and a 12px margin before the list.
                height: visible ? 54 : 0
                orientation: ListView.Horizontal
                spacing: 8
                clip: true
                // Inactive/hidden chips must not incubate delegates whose Bound
                // context is being retired during detail -> artist navigation.
                model: root.resourceActive && visible && root.controller ? (root.controller.tab === 1 ? root.controller.categories : root.controller.hotWords) : null
                delegate: EchoSoftButton {
                    id: chip
                    required property var modelData
                    readonly property CommentsController ownerController: root ? root.controller : null
                    readonly property bool validData: ownerController !== null && modelData !== undefined && modelData !== null
                    readonly property string value: !validData ? "" : ownerController.tab === 1 ? String(modelData.id) : String(modelData.content)
                    y: 8
                    height: 30
                    font: Theme.font(11, Theme.semiboldWeight)
                    readonly property bool selected: validData && value === ownerController.selection
                    foreground: selected ? "white" : Theme.secondaryText
                    text: !validData ? "" : (ownerController.tab === 1 ? modelData.label : modelData.content) + (modelData.count || modelData.cnt ? " " + (modelData.count || modelData.cnt) : "")
                    background: Rectangle {
                        radius: Theme.itemRadius
                        color: chip.selected ? Theme.accent : Theme.withAlpha(Theme.text, chip.hovered ? .10 : .06)
                        border.width: chip.visualFocus ? 2 : 1
                        border.color: chip.selected || chip.visualFocus ? Theme.accent : Theme.withAlpha(Theme.text, .12)
                    }
                    enabled: validData
                    onClicked: if (validData)
                        ownerController.select(value)
                }
            }
        }
        delegate: Loader {
            id: delegate
            required property var entry
            required property string kind
            required property string rootId
            property bool pooled: false
            readonly property string extentKey: kind + "/" + rootId + "/" + String(entry && entry.id || "")
            function reportExtent(): void {
                const body = item as Item;
                if (root.resourceActive && !pooled && body && body.implicitHeight > 0)
                    extent.record(extentKey, body.implicitHeight);
            }
            // TableView owns the row extent while the body retires, including
            // the interval in which pooled roles are reassigned.
            active: !pooled && entry !== undefined && kind !== undefined
            onHeightChanged: {
                reportTask.restart();
            }
            onImplicitHeightChanged: reportTask.restart()
            onExtentKeyChanged: reportTask.restart()
            onLoaded: reportTask.restart()
            Timer {
                id: reportTask
                interval: 0
                onTriggered: delegate.reportExtent()
            }
            TableView.onPooled: pooled = true
            TableView.onReused: {
                pooled = false;
                reportTask.restart();
            }
            sourceComponent: kind === "composer" ? inlineComposer : kind === "comment" || kind === "singerComment" || kind === "reply" ? commentRow : kind === "singerHeading" || kind === "singerDivider" ? singerSection : kind === "threadHeading" || kind === "threadFooter" ? threadState : null
            Component {
                id: singerSection
                Item {
                    readonly property bool heading: delegate.kind === "singerHeading"
                    // Account for the existing list gap in the outer margins.
                    implicitHeight: heading ? (root.compact ? 12 : 18) + singerTitle.implicitHeight + (root.compact ? 8 : 10) - list.spacing : (root.compact ? 24 : 32) + 1 - 2 * list.spacing
                    Label {
                        id: singerTitle
                        objectName: "commentSingerHeading"
                        visible: parent.heading
                        x: root.compact ? 4 : 12
                        y: root.compact ? 12 : 18
                        text: qsTr("歌手说")
                        font: Theme.font(13, Theme.boldWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: font.pixelSize * 1.5
                        color: root.compact ? Theme.secondaryText : Theme.text
                    }
                    Rectangle {
                        objectName: "commentSingerDivider"
                        visible: !parent.heading
                        x: root.compact ? 4 : 12
                        y: (root.compact ? 12 : 16) - list.spacing
                        width: Math.max(0, parent.width - (root.compact ? 8 : 24))
                        height: 1
                        color: Theme.subtleBorder
                    }
                }
            }
            Component {
                id: commentRow
                CommentRow {
                    compact: root.compact
                    uiScale: root.uiScale
                    entry: delegate.entry || ({})
                    rootId: delegate.rootId
                    controller: root.controller
                    replyRow: delegate.kind === "reply"
                    resourceActive: root.resourceActive && !delegate.pooled
                    onReplyRequested: (target, rootId) => root.controller.beginReply(target, rootId)
                    onDeleteRequested: (target, rootId, anchor) => deleteOwner.open({
                            target: target,
                            rootId: rootId,
                            anchorItem: anchor
                        })
                }
            }
            Component {
                id: inlineComposer
                Item {
                    id: composerRow
                    readonly property bool nested: !!delegate.entry.reply_nested
                    readonly property bool expanded: !!delegate.entry.thread_expanded
                    implicitHeight: 12 + form.implicitHeight + (nested ? 28 : expanded ? 0 : 12 + composerSurface.cardPadding + composerSurface.cardGap)
                    CommentSurface {
                        id: composerSurface
                        anchors.fill: parent
                        compact: root.compact
                        uiScale: root.uiScale
                        part: "composer"
                        nested: composerRow.nested
                        threadExpanded: composerRow.expanded
                    }
                    FloorReplyComposer {
                        id: form
                        x: composerSurface.mainLeft + (composerRow.nested ? composerSurface.floorPadding + 30 + (root.Window.window && root.Window.window.width / root.uiScale <= 640 ? 8 : 12) : 0)
                        y: 12
                        width: Math.max(0, parent.width - x - composerSurface.rightInset - (composerRow.nested ? composerSurface.floorPadding : 0))
                        controller: root.controller
                        target: delegate.entry || ({})
                        nested: composerRow.nested
                    }
                }
            }
            Component {
                id: threadState
                Item {
                    id: threadStateItem
                    readonly property var rowState: delegate.entry || ({})
                    readonly property bool heading: delegate.kind === "threadHeading"
                    readonly property bool loading: !!rowState.loading
                    readonly property bool error: String(rowState.error || "").length > 0
                    readonly property real stateHeight: loading || error || rowState.empty ? 42 : rowState.hasMore ? 48 : 28.5
                    implicitHeight: heading ? 12 + threadSurface.floorVerticalPadding + 20 : stateHeight + threadSurface.floorVerticalPadding + threadSurface.cardPadding + threadSurface.cardGap
                    CommentSurface {
                        id: threadSurface
                        anchors.fill: parent
                        compact: root.compact
                        uiScale: root.uiScale
                        part: delegate.kind
                    }
                    RowLayout {
                        objectName: "commentFloorHeading"
                        visible: threadStateItem.heading
                        x: threadSurface.mainLeft + threadSurface.floorPadding
                        y: 12 + threadSurface.floorVerticalPadding
                        width: Math.max(0, parent.width - x - threadSurface.rightInset - threadSurface.floorPadding)
                        height: 18
                        spacing: 8
                        Label {
                            text: threadStateItem.rowState.context ? qsTr("所在楼层的回复") : qsTr("全部回复")
                            font: Theme.font(12, Theme.semiboldWeight)
                            color: Theme.secondaryText
                        }
                        Label {
                            objectName: "commentFloorCount"
                            text: threadStateItem.rowState.count === undefined ? "" : String(threadStateItem.rowState.count)
                            font: Theme.font(11, Theme.semiboldWeight)
                            color: Theme.secondaryText
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                    Item {
                        visible: !threadStateItem.heading
                        x: threadSurface.mainLeft + threadSurface.floorPadding
                        width: Math.max(0, parent.width - x - threadSurface.rightInset - threadSurface.floorPadding)
                        height: threadStateItem.stateHeight
                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 8
                            EchoSpinner {
                                visible: threadStateItem.loading
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                resourceActive: root.resourceActive && visible
                            }
                            Label {
                                visible: threadStateItem.loading || threadStateItem.error || threadStateItem.rowState.empty || !threadStateItem.rowState.hasMore
                                text: threadStateItem.loading ? qsTr("加载中…") : threadStateItem.rowState.error || (threadStateItem.rowState.empty ? qsTr("暂无回复") : qsTr("已加载全部回复"))
                                font: Theme.font(threadStateItem.loading || threadStateItem.error || threadStateItem.rowState.empty ? 12 : 11, Theme.semiboldWeight)
                                color: Theme.secondaryText
                            }
                            EchoSoftButton {
                                objectName: "commentFloorMore"
                                plain: true
                                size: EchoControlSize.Standard
                                leftPadding: 12
                                rightPadding: 12
                                visible: !threadStateItem.loading && (threadStateItem.error || !!threadStateItem.rowState.hasMore)
                                foreground: Theme.accentText
                                text: threadStateItem.error ? qsTr("重试") : qsTr("加载更多回复")
                                onClicked: root.controller.loadThread(delegate.rootId, threadStateItem.error)
                            }
                        }
                    }
                }
            }
        }
        Loader {
            id: stateBody
            parent: list.contentItem
            y: extent.totalHeight
            width: list.width
            active: root.listStateVisible
            visible: active
            sourceComponent: CommentListState {
                loading: root.listLoading
                resourceActive: root.resourceActive
            }
        }
        ColumnLayout {
            id: flowFooter
            parent: list.contentItem
            y: root.bodyHeight
            width: list.width
            visible: root.showComments && (!root.compact || root.listLoading || (!root.controller.hasMore && root.controller.hasComments))
            spacing: 12
            RowLayout {
                visible: root.compact
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 18
                Layout.bottomMargin: 12
                spacing: 10
                EchoSpinner {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    visible: root.listLoading
                    resourceActive: root.resourceActive
                }
                EchoTextTagRow {
                    text: root.listLoading ? qsTr("加载中...") : qsTr("已加载全部评论")
                    color: Theme.secondaryText
                    font: Theme.font(12, Theme.semiboldWeight)
                }
            }
            Label {
                visible: !root.compact
                Layout.fillWidth: true
                Layout.topMargin: 18
                horizontalAlignment: Text.AlignHCenter
                text: root.listLoading ? qsTr("加载中…") : root.controller.errorMessage || (list.count === 0 ? qsTr("暂无评论") : !root.controller.hasMore ? qsTr("已加载全部评论") : "")
                color: Theme.secondaryText
                font: Theme.font(12, Theme.semiboldWeight)
                wrapMode: Text.Wrap
            }
            EchoSoftButton {
                Layout.alignment: Qt.AlignHCenter
                visible: !root.compact && root.controller.errorMessage.length > 0
                text: qsTr("重试")
                onClicked: root.controller.reload()
            }
        }
    }
    CommentComposer {
        id: composer
        objectName: "commentComposer"
        width: Math.min(720, Math.max(0, root.width - root.contentInset * 2))
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        visible: root.showComments
        controller: root.controller
        compact: root.compact
    }
    Connections {
        target: root.controller
        function onScopeReset(): void {
            deleteOwner.close();
        }
    }
    Connections {
        target: root.controller.model
        // Qt 6.12 may still be incubating a buffered delegate when an entire
        // stream is removed. Detach while the old indices are valid, then
        // reconnect the same model; pagination stays incremental.
        function onRowsAboutToBeRemoved(parentIndex, first, last): void {
            if (first === 0 && last === list.count - 1)
                root.retiringProjection = true;
        }
        function onRowsRemoved(): void {
            root.retiringProjection = false;
        }
    }
    EchoPopupLoader {
        id: deleteOwner
        objectName: "commentDeleteOwner"
        hostItem: root
        sourceComponent: Component {
            EchoPopconfirm {
                id: confirm
                required property var target
                required property string rootId
                title: target.reply_code === 1 ? qsTr("确认删除这条回复？") : qsTr("确认删除这条评论？")
                confirmText: qsTr("删除")
                danger: true
                confirmDisabled: root.controller.busy
                uiScale: root.uiScale
                onConfirmed: root.controller.remove(target, rootId)
            }
        }
    }
}
