pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: overview
    required property AccountService account
    readonly property bool compact: width <= 420 || (Window.window && Window.window.width <= 600)
    property bool uploadingAvatar: false
    signal gradeRequested
    signal socialRequested(int tab)
    signal avatarRequested
    implicitHeight: content.implicitHeight
    color: Theme.mutedSurface
    radius: Theme.cardRadius
    border.color: "transparent"
    ColumnLayout {
        id: content
        width: parent.width
        spacing: 0
        GridLayout {
            id: main
            Layout.fillWidth: true
            Layout.margins: overview.compact ? 16 : 18
            Layout.leftMargin: overview.compact ? 16 : 20
            Layout.rightMargin: overview.compact ? 16 : 20
            columns: overview.compact ? 1 : 2
            columnSpacing: 24
            rowSpacing: 12
            RowLayout {
                Layout.fillHeight: false
                // Reserve the summary row even while its asynchronous data is empty.
                // Sharing cell (0,0) transiently makes GridLayout reject the summary.
                Layout.row: main.columns === 1 ? 1 : 0
                Layout.column: 0
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: overview.compact ? 12 : 14
                Button {
                    id: avatarButton
                    objectName: "profileChangeAvatar"
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    // The source avatar is shrink-0: name/tag pressure must
                    // never turn the circular image or its overlay into an oval.
                    Layout.minimumWidth: 56
                    Layout.maximumWidth: 56
                    Layout.minimumHeight: 56
                    Layout.maximumHeight: 56
                    padding: 0
                    // Basic.Button keeps padding + 2 horizontal padding by
                    // default, which otherwise makes the inner avatar 52x56.
                    horizontalPadding: 0
                    enabled: !overview.uploadingAvatar
                    hoverEnabled: true
                    Accessible.name: qsTr("修改头像")
                    onClicked: overview.avatarRequested()
                    background: Rectangle {
                        x: -4
                        y: -4
                        width: avatarButton.width + 8
                        height: avatarButton.height + 8
                        color: "transparent"
                        radius: width / 2
                        border.width: avatarButton.visualFocus ? 2 : 0
                        border.color: Theme.accent
                    }
                    contentItem: Item {
                        AccountAvatar {
                            objectName: "profileAccountAvatar"
                            anchors.fill: parent
                            source: overview.account.avatarUrl
                            identityIcon: overview.account.avatarIdentityIcon
                            identityLabel: overview.account.avatarIdentityLabel
                            identityInset: 3
                        }
                        Rectangle {
                            id: avatarOverlay
                            anchors.fill: parent
                            radius: width / 2
                            color: "#73000000"
                            opacity: avatarButton.hovered || avatarButton.visualFocus || overview.uploadingAvatar ? 1 : 0
                            visible: opacity > 0
                            Behavior on opacity {
                                NumberAnimation {
                                    duration: Theme.fastDuration
                                }
                            }
                            EchoIcon {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                name: "pencil"
                                color: "white"
                                visible: !overview.uploadingAvatar
                            }
                            EchoSpinner {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                visible: overview.uploadingAvatar
                                resourceActive: overview.visible && (!overview.Window.window || overview.Window.window.visible && overview.Window.window.visibility !== Window.Minimized)
                                color: "white"
                            }
                        }
                    }
                    EchoToolTip {
                        parent: avatarButton
                        requestedVisible: avatarButton.hovered && avatarButton.enabled
                        text: qsTr("修改头像")
                    }
                }
                ColumnLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 8
                    EchoTextTagRow {
                        objectName: "profileNameTags"
                        Layout.fillWidth: true
                        spacing: 8
                        textTagSpacing: 12
                        lineHeight: 26
                        wrap: true
                        text: overview.account.nickname
                        color: Theme.text
                        font: Theme.font(overview.compact ? 18 : 20, Theme.boldWeight)
                        MembershipTag {
                            objectName: "profileMembershipTag"
                            visible: overview.account.memberships.length > 0
                            kind: overview.account.memberships.length ? overview.account.memberships[0].kind : "none"
                            text: overview.account.membershipLabel
                        }
                        EchoTag {
                            objectName: "profileRegionTag"
                            visible: overview.account.ipLocation.length > 0
                            text: overview.account.ipLocation
                            tone: EchoTag.Muted
                            Accessible.name: qsTr("地域：") + text
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: overview.account.biography
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        wrapMode: Text.WrapAnywhere
                        color: Theme.secondaryText
                        font: Theme.font(13, Theme.regularWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 20.8
                    }
                }
            }
            UserIdentitySummary {
                id: identitySummary
                objectName: "profileIdentitySummary"
                Layout.alignment: main.columns === 1 ? Qt.AlignLeft : Qt.AlignRight | Qt.AlignTop
                Layout.preferredWidth: Math.min(naturalWidth, main.width * (main.columns === 1 ? 1 : 0.6))
                Layout.maximumWidth: main.width * (main.columns === 1 ? 1 : 0.6)
                Layout.row: 0
                Layout.column: main.columns === 1 ? 0 : 1
                horizontalAlignment: main.columns === 1 ? Qt.AlignLeft : Qt.AlignRight
                badges: overview.account.identityBadges
                tags: overview.account.identityTags
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.subtleBorder
        }
        GridLayout {
            objectName: "profileStatistics"
            columns: 5
            uniformCellWidths: true
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.leftMargin: overview.compact ? 8 : 20
            Layout.rightMargin: overview.compact ? 8 : 20
            Layout.topMargin: overview.compact ? 14 : 12
            Layout.bottomMargin: overview.compact ? 14 : 12
            columnSpacing: 0
            rowSpacing: 0
            Repeater {
                model: [qsTr("等级"), qsTr("好友"), qsTr("关注"), qsTr("粉丝"), qsTr("访客")]
                delegate: Button {
                    id: stat
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 0
                    implicitHeight: Math.max(32, contentItem.implicitHeight + topPadding + bottomPadding)
                    padding: overview.compact ? 2 : 4
                    horizontalPadding: overview.compact ? 4 : 8
                    enabled: true
                    Accessible.name: qsTr("查看") + modelData
                    onClicked: {
                        if (index === 0)
                            overview.gradeRequested();
                        else
                            overview.socialRequested(index - 1);
                    }
                    background: Item {
                        Rectangle {
                            visible: stat.index > 0
                            width: 1
                            height: 18
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.subtleBorder
                        }
                    }
                    contentItem: GridLayout {
                        columns: overview.compact ? 1 : 4
                        columnSpacing: 10
                        rowSpacing: 2
                        Item {
                            visible: !overview.compact
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            objectName: "profileStatisticValue"
                            Layout.fillWidth: false
                            Layout.fillHeight: false
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 2
                            Label {
                                text: stat.index === 0 ? "Lv." + overview.account.grade : stat.index === 4 ? overview.account.visitors : overview.account.statistics.length > stat.index - 1 ? overview.account.statistics[stat.index - 1].value : "—"
                                color: stat.hovered && stat.enabled ? Theme.accentText : Theme.text
                                font: Theme.font(stat.index === 0 ? 16 : 18, Theme.semiboldWeight)
                                lineHeightMode: Text.FixedHeight
                                lineHeight: (stat.index === 0 ? 16 : 18) * 1.35
                                verticalAlignment: Text.AlignVCenter
                            }
                            EchoIcon {
                                visible: stat.index === 0
                                name: "chevron-right"
                                Layout.preferredWidth: 12
                                Layout.preferredHeight: 12
                                color: stat.hovered ? Theme.accentText : Theme.text
                            }
                        }
                        Label {
                            objectName: "profileStatisticLabel"
                            text: stat.modelData
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.regularWeight)
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Item {
                            visible: !overview.compact
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
