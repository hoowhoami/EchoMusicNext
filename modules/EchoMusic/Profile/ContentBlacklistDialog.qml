pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    objectName: "contentBlacklistDialog"
    required property ContentBlacklistService service
    readonly property string sectionTitle: service.singer ? qsTr("已屏蔽的歌手") : qsTr("不感兴趣的歌曲")
    readonly property string sectionDescription: service.singer ? qsTr("屏蔽后，该歌手的内容将不再出现在个性化推荐中。") : qsTr("这些歌曲及相似内容将减少出现在个性化推荐中。")
    readonly property bool initialLoading: service.loading && !service.loaded
    preferredWidth: 500
    maximumHeight: 540
    windowHeightMargin: 96
    height: Math.min(540, parent ? Math.max(120, parent.height / uiScale - 96) : 540)
    heading: qsTr("黑名单管理")
    flushBody: true
    onAboutToShow: service.activate()
    onAboutToHide: service.deactivate()
    body: ColumnLayout {
        spacing: 0
        Connections {
            target: dialog
            function onOpened(): void {
                list.positionViewAtBeginning();
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.rightMargin: 24
            text: qsTr("管理个性化推荐中不想看到的歌曲和歌手。")
            font: Theme.font(13, Theme.regularWeight)
            lineHeightMode: Text.FixedHeight
            lineHeight: 20.8
            color: Theme.floatingSecondaryText
            wrapMode: Text.Wrap
        }
        EchoTabBar {
            id: categories
            objectName: "contentBlacklistCategories"
            Layout.fillWidth: true
            Layout.rightMargin: 24
            Layout.topMargin: 10
            labels: [qsTr("不感兴趣"), qsTr("已屏蔽歌手")]
            counts: [dialog.service.songCount, dialog.service.singerCount]
            countTone: EchoBadge.Neutral
            countKnown: [dialog.service.songCount.length > 0, dialog.service.singerCount.length > 0]
            colors: Theme.floating
            currentIndex: dialog.service.category
            accessibleName: qsTr("黑名单管理分类")
            onPicked: index => {
                dialog.service.selectCategory(index);
                list.positionViewAtBeginning();
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: 24
            Layout.topMargin: 14
            Layout.bottomMargin: 10
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.fillHeight: false
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: dialog.sectionTitle
                    font: Theme.font(13, Theme.boldWeight)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 19.5
                    color: Theme.floatingText
                }
                Label {
                    Layout.fillWidth: true
                    text: dialog.sectionDescription
                    font: Theme.font(11, Theme.mediumWeight)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 17.05
                    wrapMode: Text.Wrap
                    color: Theme.floatingSecondaryText
                }
            }
            EchoIconButton {
                id: refresh
                objectName: "contentBlacklistRefresh"
                Layout.alignment: Qt.AlignTop
                implicitWidth: 32
                implicitHeight: 32
                padding: 8.5
                colors: Theme.floating
                glyph: "refresh"
                text: qsTr("刷新%1").arg(dialog.sectionTitle)
                enabled: !dialog.service.loading && !dialog.service.removing
                contentItem: EchoIcon {
                    name: "refresh"
                    color: refresh.foreground
                    opacity: refresh.enabled ? 1 : refresh.disabledOpacity
                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 1000
                        loops: Animation.Infinite
                        running: dialog.visible && dialog.service.loading && Theme.store.motionEnabled && refresh.Window.window !== null && refresh.Window.window.visible && refresh.Window.window.visibility !== Window.Minimized
                    }
                }
                onClicked: dialog.service.refresh()
            }
        }
        Rectangle {
            visible: dialog.service.error.length > 0 && dialog.service.entries.count > 0
            Layout.fillWidth: true
            Layout.rightMargin: 24
            Layout.bottomMargin: 8
            implicitHeight: errorRow.implicitHeight + 16
            radius: Theme.controlRadius
            color: Theme.withAlpha(Theme.floating.danger, 0.08)
            RowLayout {
                id: errorRow
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: dialog.service.error
                    elide: Text.ElideRight
                    font: Theme.font(12, Theme.boldWeight)
                    color: Theme.floating.danger
                }
                EchoSoftButton {
                    text: qsTr("重试")
                    colors: Theme.floating
                    enabled: !dialog.service.loading && !dialog.service.removing
                    onClicked: dialog.service.refresh()
                }
            }
        }
        ListView {
            id: list
            objectName: "contentBlacklistList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            implicitHeight: 240
            clip: true
            spacing: 2
            topMargin: 2
            bottomMargin: 4
            reuseItems: dialog.visible
            EchoViewPoolGuard {
                targetView: list
            }
            cacheBuffer: dialog.visible ? 116 : 0
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.visible ? dialog.service.entries : null
            ScrollBar.vertical: EchoPopupScrollBar {
                surface: dialog
                view: list
                resourceActive: dialog.visible
            }
            delegate: ContentBlacklistRow {
                width: list.width - 24
                service: dialog.service
                property bool pooled: false
                resourceActive: dialog.visible && !pooled && y + height > list.contentY && y < list.contentY + list.height
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
            }
            header: dialog.service.entries.count === 0 ? emptyState : null
            footer: Item {
                width: list.width - 24
                height: dialog.service.hasMore ? 52 : 0
                visible: dialog.service.hasMore
                EchoButton {
                    objectName: "contentBlacklistLoadMore"
                    anchors.centerIn: parent
                    text: qsTr("加载更多")
                    accent: false
                    colors: Theme.floating
                    loading: dialog.service.loading
                    enabled: !dialog.service.loading && !dialog.service.removing
                    onClicked: dialog.service.loadMore()
                }
            }
        }
        Component {
            id: emptyState
            Item {
                width: list.width - 24
                height: Math.max(240, list.height - 6)
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - 48)
                    spacing: 12
                    EchoIcon {
                        id: loadingIcon
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        visible: dialog.initialLoading
                        name: "refresh"
                        color: Theme.floatingSecondaryText
                        RotationAnimator on rotation {
                            from: 0
                            to: 360
                            duration: 1000
                            loops: Animation.Infinite
                            running: dialog.visible && loadingIcon.visible && Theme.store.motionEnabled && loadingIcon.Window.window !== null && loadingIcon.Window.window.visible && loadingIcon.Window.window.visibility !== Window.Minimized
                        }
                    }
                    Rectangle {
                        visible: !dialog.initialLoading && dialog.service.error.length === 0
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        Layout.bottomMargin: 2
                        radius: Theme.cardRadius
                        color: Theme.floating.mutedSurface
                        EchoIcon {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            name: dialog.service.singer ? "eye-off" : "heart-off"
                            color: Theme.floatingSecondaryText
                        }
                    }
                    Label {
                        objectName: "contentBlacklistState"
                        Layout.fillWidth: true
                        text: dialog.initialLoading ? qsTr("正在加载") : dialog.service.error.length > 0 ? dialog.service.error : dialog.service.singer ? qsTr("还没有屏蔽任何歌手") : qsTr("还没有标记不感兴趣的歌曲")
                        font: Theme.font(12, dialog.initialLoading || dialog.service.error.length > 0 ? Theme.boldWeight : Theme.blackWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 18
                        color: dialog.service.error.length > 0 ? Theme.floating.danger : Theme.floatingSecondaryText
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        visible: !dialog.initialLoading && dialog.service.error.length === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        text: qsTr("你可以随时在歌曲或歌手页面添加")
                        font: Theme.font(10, Theme.mediumWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 15
                        color: Theme.floatingSecondaryText
                        horizontalAlignment: Text.AlignHCenter
                    }
                    EchoButton {
                        objectName: "contentBlacklistRetry"
                        visible: !dialog.initialLoading && dialog.service.error.length > 0
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("重新加载")
                        accent: false
                        background: EchoActionBackground {
                            colors: Theme.floating
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.floatingBorder
                        }
                        colors: Theme.floating
                        enabled: !dialog.service.loading
                        onClicked: dialog.service.refresh()
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: confirmationOwner
        objectName: "contentBlacklistConfirmationOwner"
        hostItem: dialog.contentItem
        requestedVisible: dialog.visible && dialog.service.pendingRemoval
        sourceComponent: Component {
            EchoDialog {
                id: confirmation
                objectName: "contentBlacklistConfirmation"
                uiScale: dialog.uiScale
                heading: dialog.service.pendingSinger ? qsTr("取消屏蔽歌手") : qsTr("撤销不感兴趣")
                description: dialog.service.pendingSinger ? qsTr("确定取消屏蔽“%1”吗？该歌手的内容可能会再次出现在推荐中。").arg(dialog.service.pendingName) : qsTr("确定撤销对“%1”的不感兴趣吗？这首歌可能会再次出现在推荐中。").arg(dialog.service.pendingName)
                closable: !dialog.service.removing
                managedDismiss: true
                closePolicy: Popup.NoAutoClose
                onDismissRequested: dialog.service.cancelRemoval()
                footerContent: RowLayout {
                    spacing: 5
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoButton {
                        text: qsTr("取消")
                        colors: Theme.floating
                        accent: false
                        background: EchoActionBackground {
                            colors: Theme.floating
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.floatingBorder
                        }
                        enabled: !dialog.service.removing
                        onClicked: dialog.service.cancelRemoval()
                    }
                    EchoButton {
                        objectName: "contentBlacklistConfirmUndo"
                        text: dialog.service.pendingSinger ? qsTr("取消屏蔽") : qsTr("确认撤销")
                        colors: Theme.floating
                        loading: dialog.service.removing
                        onClicked: dialog.service.confirmRemoval()
                    }
                }
            }
        }
    }
}
