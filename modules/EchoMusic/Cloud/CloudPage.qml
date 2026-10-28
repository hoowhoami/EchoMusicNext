pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Shell
import EchoMusic.Controls
import EchoMusic.Music
import EchoMusic.Theme

// Cloud catalog and deletion UI; uploads are owned by the application service.
RoutePage {
    id: page
    required property CloudService service
    objectName: "cloudPage"
    readonly property real expandedHeaderHeight: 176
    readonly property real toolbarRowHeight: 56
    readonly property real toolbarHeight: page.toolbarRowHeight + 44
    readonly property bool capacityKnown: page.service.storageCapacityBytes >= 0
    readonly property real capacityUsed: Math.max(0, page.service.storageCapacityBytes - page.service.storageAvailableBytes)
    readonly property real capacityRatio: page.service.storageCapacityBytes > 0 ? Math.min(1, page.capacityUsed / page.service.storageCapacityBytes) : 0
    // 容量计数缺失时使用已投影行数，避免有歌曲却显示零计数。
    readonly property int displaySongCount: page.service.totalCount > 0 ? page.service.totalCount : page.service.songs.count
    property string menuSongId: ""
    property var pendingRemoveIds: []
    property bool removePending: false
    // Format byte values with no decimals for integers or values >=100;
    // otherwise use one or two decimals.
    function formatBytes(value: real): string {
        if (!isFinite(value) || value <= 0)
            return "0 B";
        const units = ["B", "KB", "MB", "GB", "TB"];
        let size = value;
        let unitIndex = 0;
        while (size >= 1024 && unitIndex < units.length - 1) {
            size /= 1024;
            unitIndex += 1;
        }
        const digits = size >= 100 || unitIndex === 0 ? 0 : size >= 10 ? 1 : 2;
        return size.toFixed(digits) + " " + units[unitIndex];
    }
    function showSongMenu(id: string, sceneX: real, sceneY: real): void {
        page.menuSongId = id;
        songMenuOwner.open({
            songId: id,
            x: sceneX,
            y: sceneY,
            // 只有非零数字 file_id 的行可删除，与 CloudService.canRemoveSong 共用判据。
            removeEnabled: page.service.canRemoveSong(id)
        });
    }
    // Ineligible rows (no cloud file target) are greyed out through
    // canRemoveSong; a batch that still skips rows reports it via the
    // service notice.
    function requestSingleRemove(id: string): void {
        page.pendingRemoveIds = [id];
        singleRemoveOwner.open();
    }
    function requestBatchRemove(ids: var): void {
        if (!ids || ids.length === 0)
            return;
        page.pendingRemoveIds = ids;
        page.removePending = true;
        page.service.removeSongs(ids);
    }
    function openUploadIfRequested(): void {
        if (page.pageActive && page.service.upload.dialogRequested) {
            uploadOwner.open();
            if (uploadOwner.item)
                page.service.upload.acknowledgeOpen();
        }
    }
    Component.onCompleted: openUploadIfRequested()
    Connections {
        target: page.service.upload
        function onChanged(): void {
            page.openUploadIfRequested();
        }
    }
    EchoPopupLoader {
        id: uploadOwner
        objectName: "cloudUploadOwner"
        hostItem: page
        sourceComponent: Component {
            CloudUploadDialog {
                service: page.service.upload
            }
        }
    }
    onPageActiveChanged: {
        if (pageActive)
            openUploadIfRequested();
        else {
            uploadOwner.close();
            batchOwner.close();
            songMenuOwner.close();
            singleRemoveOwner.close();
        }
    }
    Connections {
        target: page.service
        function onChanged(): void {
            if (!page.removePending || page.service.busy)
                return;
            page.removePending = false;
            // 成功后才清空并关闭确认状态；失败由服务显示原因并保留选择。
            // 恢复取消和确认入口，使单删与批量删除均可重试。
            if (page.service.errorMessage.length === 0) {
                page.pendingRemoveIds = [];
                singleRemoveOwner.close();
                batchOwner.close();
            }
        }
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        visible: page.service.signedIn
        pageActive: page.pageActive
        scrollView: songList.listView
        stickyHeaders: [header, toolbar]
        EchoPageStickyHeader {
            id: header
            container: viewport
            flowPosition: 0
            height: (heading.item as Item)?.implicitHeight ?? page.expandedHeaderHeight
            Loader {
                id: heading
                anchors.fill: parent
                sourceComponent: Component {
                    SongDetailHeader {
                        id: cloudHeader
                        heading: qsTr("音乐云盘")
                        typeLabel: "CLOUD"
                        playable: page.service.songs.count > 0
                        resourceActive: page.pageActive
                        scrollTop: viewport.scrollTop
                        coverComponent: Component {
                            ThemedIconCover {
                                cornerRadius: Theme.mediaRadius / cloudHeader.coverScale
                                iconPath: "M6.657 18C4.085 18 2 15.993 2 13.517c0-2.475 2.085-4.482 4.657-4.482c.393-1.762 1.794-3.2 3.675-3.773c1.88-.572 3.956-.193 5.444 1c1.488 1.19 2.162 3.007 1.77 4.769h.99c1.913 0 3.464 1.56 3.464 3.486c0 1.927-1.551 3.487-3.465 3.487H6.657"
                            }
                        }
                        // Each metadata row participates independently in the vertical distribution.
                        detailsComponent: Component {
                            EchoButtonContent {
                                objectName: "cloudDetailCount"
                                horizontalAlignment: Qt.AlignLeft
                                glyph: "play"
                                glyphSize: 12
                                spacing: 6
                                lineHeight: 18
                                text: qsTr("%1 首歌曲").arg(page.displaySongCount)
                                font: Theme.font(12, Theme.mediumWeight)
                                color: Theme.secondaryText
                            }
                        }
                        secondaryDetailsComponent: page.capacityKnown ? cloudCapacityDetails : null
                        Component {
                            id: cloudCapacityDetails
                            Item {
                                implicitHeight: capacity.implicitHeight
                                ColumnLayout {
                                    id: capacity
                                    objectName: "cloudCapacity"
                                    width: Math.min(232, parent.width)
                                    spacing: 6
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12
                                        Text {
                                            text: qsTr("已用 %1").arg(page.formatBytes(page.capacityUsed))
                                            color: Theme.secondaryText
                                            font: Theme.font(11, Theme.mediumWeight)
                                            lineHeight: 16
                                            lineHeightMode: Text.FixedHeight
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: page.formatBytes(page.service.storageCapacityBytes)
                                            color: Theme.secondaryText
                                            font: Theme.font(11, Theme.mediumWeight)
                                            lineHeight: 16
                                            lineHeightMode: Text.FixedHeight
                                        }
                                    }
                                    Rectangle {
                                        objectName: "cloudCapacityTrack"
                                        Layout.fillWidth: true
                                        height: 4
                                        radius: 2
                                        color: Theme.withAlpha(Theme.colors.tone, 0.12)
                                        Accessible.role: Accessible.ProgressBar
                                        Accessible.name: qsTr("云盘容量使用率")
                                        Accessible.description: qsTr("已用 %1，总容量 %2，可用 %3").arg(page.formatBytes(page.capacityUsed)).arg(page.formatBytes(page.service.storageCapacityBytes)).arg(page.formatBytes(page.service.storageAvailableBytes))
                                        Rectangle {
                                            objectName: "cloudCapacityValue"
                                            width: page.capacityRatio * parent.width
                                            height: parent.height
                                            radius: parent.radius
                                            color: Theme.accent
                                        }
                                    }
                                }
                            }
                        }
                        additionalActions: uploadAction
                        collapsedActions: Component {
                            EchoIconButton {
                                objectName: "cloudUploadCollapsed"
                                text: qsTr("上传")
                                glyph: "cloud-upload"
                                onClicked: page.service.upload.open()
                            }
                        }
                        Component {
                            id: uploadAction
                            EchoSoftButton {
                                objectName: "cloudUploadEntry"
                                text: qsTr("上传")
                                glyph: "cloud-upload"
                                onClicked: page.service.upload.open()
                            }
                        }
                        onPlayRequested: page.service.actions.play()
                        onBatchRequested: batchOwner.open()
                    }
                }
            }
        }
        EchoPageStickyHeader {
            id: toolbar
            container: viewport
            flowPosition: page.expandedHeaderHeight
            stickyTop: header.height
            height: page.toolbarHeight
            Rectangle {
                objectName: "cloudTabDivider"
                x: 24
                y: page.toolbarRowHeight - 1
                width: Math.max(0, parent.width - 48)
                height: 1
                color: Theme.subtleBorder
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    objectName: "cloudTabToolbarRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.toolbarRowHeight
                    Layout.minimumHeight: page.toolbarRowHeight
                    Layout.maximumHeight: page.toolbarRowHeight
                    Layout.fillHeight: false
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    spacing: 8
                    EchoDetailTabs {
                        objectName: "cloudTabs"
                        Layout.preferredHeight: 56
                        labels: [qsTr("歌曲")]
                        counts: [String(page.displaySongCount)]
                        objectNames: ["cloudTabSongs"]
                        currentIndex: 0
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoTextField {
                        objectName: "cloudSongFilter"
                        Layout.preferredWidth: 160
                        Layout.minimumWidth: 80
                        placeholderText: qsTr("搜索歌曲...")
                        text: page.service.songs.query
                        clearable: true
                        onTextEdited: page.service.songs.query = text
                    }
                    SongLocateButton {
                        objectName: "cloudLocate"
                        onClicked: {
                            const index = page.service.songs.indexOf(page.service.actions.currentTrackId);
                            if (index >= 0)
                                EchoListPositioning.positionIfNeeded(songList.listView, index, ListView.Center, songList.pageContainer);
                        }
                    }
                }
                SongListHeader {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    geometry: songList.geometry
                    sortField: page.service.songs.sortField
                    sortDirection: page.service.songs.sortDirection
                    onSortPicked: field => page.service.songs.toggleSort(field)
                }
            }
        }
        CatalogSongList {
            id: songList
            anchors.fill: parent
            collection: page.service.songs
            actions: page.service.actions
            pageContainer: viewport
            contentInset: 24
            topMargin: page.expandedHeaderHeight + page.toolbarHeight
            loading: page.service.loading && page.service.songs.totalCount === 0
            loadingMore: page.service.loading && page.service.songs.count > 0
            hasMore: page.service.hasMore
            errorMessage: page.service.errorMessage
            resourceActive: page.pageActive && visible
            menuSongId: page.menuSongId
            emptyComponent: cloudEmpty
            onRetryRequested: {
                if (page.service.hasMore)
                    page.service.loadMore();
                else
                    page.service.refresh();
            }
            onMoreRequested: (id, sceneX, sceneY) => page.showSongMenu(id, sceneX, sceneY)
        }
        EchoEnter {
            target: songList
            active: page.pageActive
        }
    }
    // The empty card provides the same upload command.
    Component {
        id: cloudEmpty
        Item {
            objectName: "cloudEmptyState"
            implicitHeight: 320
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(420, Math.max(0, parent.width - 48))
                spacing: 0
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 64
                    implicitHeight: 64
                    radius: Theme.cardRadius
                    color: Theme.withAlpha(Theme.accent, .1)
                    EchoIcon {
                        anchors.centerIn: parent
                        width: 28
                        height: 28
                        name: "cloud"
                        color: Theme.accentText
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 16
                    text: qsTr("云盘暂无歌曲")
                    color: Theme.text
                    font: Theme.font(18, Theme.semiboldWeight)
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                    text: qsTr("上传后会展示在这里")
                    color: Theme.secondaryText
                    font: Theme.font(13, Theme.mediumWeight)
                }
                EchoButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 16
                    text: qsTr("上传音乐")
                    glyph: "cloud-upload"
                    onClicked: page.service.upload.open()
                }
            }
        }
    }
    // Signed-out state.
    Item {
        anchors.fill: parent
        visible: !page.service.signedIn
        Rectangle {
            anchors.centerIn: parent
            width: 72
            height: 72
            radius: Theme.dialogRadius
            color: Theme.withAlpha(Theme.accent, .1)
            EchoIcon {
                anchors.centerIn: parent
                width: 32
                height: 32
                name: "cloud"
                color: Theme.accentText
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.verticalCenter
            anchors.topMargin: 58
            text: qsTr("登录后查看云盘")
            color: Theme.text
            font: Theme.font(22, Theme.semiboldWeight)
        }
    }
    EchoPopupLoader {
        id: batchOwner
        objectName: "cloudBatchDrawerOwner"
        hostItem: page
        sourceComponent: Component {
            SongCatalogBatchDrawer {
                id: batch
                actions: page.service.actions
                collection: page.service.songs
                uiScale: page.settings.uiScale
                enableRemove: true
                removeBusy: page.service.busy
                onRemoveConfirmed: ids => page.requestBatchRemove(ids)
            }
        }
    }
    // Single-row deletion confirmation.
    EchoPopupLoader {
        id: singleRemoveOwner
        objectName: "cloudSingleRemoveOwner"
        hostItem: page
        sourceComponent: Component {
            EchoDialog {
                id: singleRemove
                objectName: "cloudSingleRemoveDialog"
                heading: qsTr("删除云盘歌曲")
                // 模型仅提供 title；标题缺失时显示「这首歌」。
                description: {
                    const id = page.pendingRemoveIds.length > 0 ? page.pendingRemoveIds[0] : "";
                    const row = page.service.songs.item(Math.max(0, page.service.songs.indexOf(id)));
                    const name = String(row.title ?? "");
                    return qsTr("确认从云盘删除『%1』？此操作无法撤销。").arg(name.length > 0 ? name : qsTr("这首歌"));
                }
                preferredWidth: 440
                uiScale: page.settings.uiScale
                closable: !page.service.busy
                footerContent: Component {
                    RowLayout {
                        spacing: 8
                        Item {
                            Layout.fillWidth: true
                        }
                        EchoSoftButton {
                            text: qsTr("取消")
                            enabled: !page.service.busy
                            onClicked: {
                                page.pendingRemoveIds = [];
                                singleRemoveOwner.close();
                            }
                        }
                        EchoSoftButton {
                            objectName: "cloudSingleRemoveConfirm"
                            danger: true
                            text: qsTr("确认删除")
                            loading: page.service.busy
                            enabled: !page.service.busy
                            onClicked: {
                                page.removePending = true;
                                page.service.removeSongs(page.pendingRemoveIds);
                            }
                        }
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: songMenuOwner
        objectName: "cloudSongMenuOwner"
        hostItem: page
        sourceComponent: Component {
            SongContextMenu {
                id: songMenu
                parent: Overlay.overlay
                scale: page.settings.uiScale
                transformOrigin: Popup.TopLeft
                commands: page.service.actions
                removeVisible: true
                removeAfterExtensions: true
                removeLabel: qsTr("从云盘删除")
                onRemoveTriggered: page.requestSingleRemove(songMenu.songId)
                onClosed: page.menuSongId = ""
            }
        }
    }
}
