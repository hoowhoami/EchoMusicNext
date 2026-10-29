pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

EchoDrawer {
    id: drawer
    objectName: "profileSocialDrawer"
    required property ProfileService profile
    readonly property list<string> labels: [qsTr("好友"), qsTr("关注"), qsTr("粉丝"), qsTr("访客")]
    readonly property list<string> emptyLabels: [qsTr("暂无好友记录"), qsTr("暂无关注记录"), qsTr("暂无粉丝记录"), qsTr("暂无访客记录")]
    onClosed: profile.closeChat()
    Component.onDestruction: profile.closeChat()
    contentItem: ColumnLayout {
        spacing: 0
        EchoDrawerHeader {
            objectName: "profileSocialHeader"
            Layout.fillWidth: true
            leadingContent: drawer.profile.chatActive ? chatBack : null
            titleContent: Component {
                ColumnLayout {
                    spacing: 2
                    EchoTextTagRow {
                        Layout.fillWidth: true
                        text: drawer.profile.chatActive ? qsTr("私信会话") : qsTr("个人互动")
                        color: Theme.floatingSecondaryText
                        font: Theme.font(11, Theme.boldWeight)
                        lineHeight: 16.5
                    }
                    EchoTextTagRow {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: drawer.profile.chatActive ? drawer.profile.chatTarget.nickname : drawer.labels[drawer.profile.socialTab]
                        color: Theme.floatingText
                        font: Theme.font(18, Theme.blackWeight)
                        lineHeight: 21.6
                    }
                }
            }
            closeButton.objectName: "profileSocialClose"
            actions: drawer.profile.chatActive ? null : refreshAction
            Component {
                id: refreshAction
                EchoIconButton {
                    implicitWidth: 32
                    implicitHeight: 32
                    padding: 9
                    glyph: "refresh"
                    text: qsTr("刷新列表")
                    colors: Theme.floating
                    enabled: !drawer.profile.socialLoading
                    onClicked: drawer.profile.refreshSocial()
                }
            }
            onCloseRequested: drawer.close()
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.floatingBorder
        }
        EchoTabBar {
            visible: !drawer.profile.chatActive
            colors: Theme.floating
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            labels: drawer.labels
            currentIndex: drawer.profile.socialTab
            onPicked: index => drawer.profile.openSocial(index)
        }
        Item {
            visible: !drawer.profile.chatActive
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: list
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: drawer
                    view: list
                    resourceActive: drawer.visible
                }
                objectName: "profileSocialList"
                anchors.fill: parent
                clip: true
                topMargin: 4
                bottomMargin: 12
                spacing: 8
                reuseItems: drawer.visible
                EchoViewPoolGuard {
                    targetView: list
                }
                cacheBuffer: drawer.visible ? 160 : 0
                boundsBehavior: Flickable.StopAtBounds
                model: drawer.visible ? drawer.profile.socialUsers : null
                delegate: SocialUserRow {
                    id: socialRow
                    // ListView owns delegate positioning, including on reuse.
                    // Insets belong to its child card, never the delegate x.
                    width: list.width
                    horizontalInset: 12
                    currentUserId: drawer.profile.account.userId
                    pending: {
                        drawer.profile.socialTab;
                        drawer.profile.socialLoading;
                        return drawer.profile.socialPending(userId);
                    }
                    listLoading: drawer.profile.socialLoading
                    onFollowRequested: drawer.profile.toggleSocialFollow(entryKey)
                    onMessageRequested: drawer.profile.openChat(entryKey)
                    property bool pooled: false
                    // Qt may retain currentItem even far outside the viewport.
                    // Bound image work to the same visible/prefetch range as reuse.
                    resourceActive: drawer.visible && !pooled && y + height > list.contentY - list.cacheBuffer && y < list.contentY + list.height + list.cacheBuffer
                    ListView.onPooled: pooled = true
                    ListView.onReused: pooled = false
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 280)
                visible: drawer.profile.socialUsers.count === 0
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: drawer.profile.socialLoading ? qsTr("正在加载") + drawer.labels[drawer.profile.socialTab] : drawer.profile.socialError.length ? drawer.profile.socialError : drawer.emptyLabels[drawer.profile.socialTab]
                    color: Theme.floatingSecondaryText
                    font: Theme.font(12, Theme.boldWeight)
                }
                EchoSoftButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: drawer.profile.socialError.length > 0
                    text: qsTr("重试")
                    colors: Theme.floating
                    onClicked: drawer.profile.refreshSocial()
                }
            }
        }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: active
            active: drawer.profile.chatActive
            sourceComponent: SocialChatPanel {
                profile: drawer.profile
                surface: drawer
            }
        }
    }
    Component {
        id: chatBack
        EchoIconButton {
            objectName: "profileChatBack"
            implicitWidth: 32
            implicitHeight: 32
            padding: 7
            glyph: "chevron-left"
            text: qsTr("返回互动列表")
            colors: Theme.floating
            onClicked: drawer.profile.closeChat()
        }
    }
}
