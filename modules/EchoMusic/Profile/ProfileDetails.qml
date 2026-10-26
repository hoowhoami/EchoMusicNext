pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

Rectangle {
    id: details
    required property AccountService account
    property real uiScale: 1
    property Item expiryAnchor: null
    property var expiryMembership: null
    function closePopups(): void {
        expiryClose.stop();
        expiryPopupOwner.close();
    }
    readonly property real contentPadding: Window.window && Window.window.width <= 600 ? 16 : 24
    implicitHeight: body.implicitHeight + contentPadding * 2
    color: Theme.mutedSurface
    radius: Theme.cardRadius
    ColumnLayout {
        id: body
        width: parent.width - details.contentPadding * 2
        x: details.contentPadding
        y: details.contentPadding
        spacing: 24
        // Basics, listening history and student details use separate three-column groups.
        // On narrow layouts, location spans the basics row.
        Repeater {
            model: [[
                    {
                        label: qsTr("用户 ID"),
                        value: String(details.account.userId)
                    },
                    {
                        label: qsTr("性别"),
                        value: details.account.genderLabel
                    },
                    {
                        label: qsTr("所在地区"),
                        value: details.account.location
                    }
                ], [
                    {
                        label: qsTr("乐龄"),
                        value: details.account.accountAge
                    },
                    {
                        label: qsTr("累计听歌"),
                        value: details.account.listeningDuration || "—"
                    }
                ], [
                    {
                        label: qsTr("学生学校"),
                        value: details.account.studentSchool
                    },
                    {
                        label: qsTr("学生认证到期"),
                        value: details.account.studentExpiry
                    }
                ].filter(item => item.value.length > 0)]
            delegate: GridLayout {
                id: group
                required property var modelData
                required property int index
                objectName: "profileAccountGroup" + index
                visible: modelData.length > 0
                Layout.fillWidth: true
                uniformCellWidths: true
                columns: body.width <= 360 && index > 0 ? 1 : body.width <= 440 ? 2 : 3
                columnSpacing: 24
                rowSpacing: 18
                Repeater {
                    model: group.modelData
                    delegate: ColumnLayout {
                        id: detail
                        required property var modelData
                        required property int index
                        objectName: "profileAccountField"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.columnSpan: group.index === 0 && index === 2 && group.columns === 2 ? 2 : 1
                        spacing: 8
                        Label {
                            objectName: "profileAccountFieldLabel"
                            text: detail.modelData.label
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.regularWeight)
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 18
                        }
                        Label {
                            Layout.fillWidth: true
                            text: detail.modelData.value
                            color: Theme.text
                            font: Theme.font(13, Theme.mediumWeight)
                            wrapMode: Text.WrapAnywhere
                            lineHeightMode: Text.FixedHeight
                            lineHeight: 19.5
                        }
                    }
                }
                // CSS retains the third track even when a group has two cells.
                Repeater {
                    model: Math.max(0, group.columns - group.modelData.length)
                    delegate: Item {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.subtleBorder
        }
        GridLayout {
            Layout.fillWidth: true
            uniformCellWidths: true
            columns: body.width <= 440 ? 2 : 4
            columnSpacing: 0
            rowSpacing: 0
            Repeater {
                model: [
                    {
                        kind: "super",
                        label: qsTr("超级VIP")
                    },
                    {
                        kind: "deluxe",
                        label: qsTr("豪华VIP")
                    },
                    {
                        kind: "concept",
                        label: qsTr("概念会员")
                    },
                    {
                        kind: "music",
                        label: qsTr("畅听会员")
                    }
                ]
                delegate: Item {
                    id: member
                    required property var modelData
                    required property int index
                    readonly property bool stacked: body.width <= 680 && body.width > 440
                    readonly property real inset: 12
                    readonly property bool first: index % (body.width <= 440 ? 2 : 4) === 0
                    readonly property bool last: index % (body.width <= 440 ? 2 : 4) === (body.width <= 440 ? 1 : 3)
                    readonly property color tone: modelData.kind === "super" ? "#f97316" : modelData.kind === "deluxe" ? "#e8a317" : modelData.kind === "concept" ? "#8b5cf6" : "#10b981"
                    readonly property var membership: details.account.memberships.find(item => item.kind === member.modelData.kind)
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    implicitHeight: memberContent.implicitHeight + (body.width <= 440 ? 24 : 0)
                    Rectangle {
                        visible: !member.first
                        width: 1
                        height: parent.height
                        color: Theme.subtleBorder
                    }
                    Rectangle {
                        visible: body.width <= 440 && member.index >= 2
                        width: parent.width
                        height: 1
                        color: Theme.subtleBorder
                    }
                    GridLayout {
                        id: memberContent
                        x: member.first ? 0 : member.inset
                        y: body.width <= 440 ? 12 : 0
                        width: parent.width - x - (member.last ? 0 : member.inset)
                        columns: member.stacked ? 1 : 2
                        columnSpacing: body.width <= 440 ? 8 : 12
                        rowSpacing: 8
                        Item {
                            Layout.preferredWidth: member.stacked ? 24 : 32
                            Layout.preferredHeight: member.stacked ? 24 : 32
                            EchoIcon {
                                anchors.centerIn: parent
                                name: "diamond"
                                width: 20
                                height: 20
                                color: Qt.rgba(member.tone.r * 0.68 + Theme.text.r * 0.32, member.tone.g * 0.68 + Theme.text.g * 0.32, member.tone.b * 0.68 + Theme.text.b * 0.32, 1)
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: member.modelData.label
                                color: Theme.text
                                font: Theme.font(13, Theme.mediumWeight)
                                wrapMode: Text.WrapAnywhere
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.alignment: Qt.AlignLeft
                                spacing: 8
                                Label {
                                    Layout.fillWidth: true
                                    text: member.membership ? member.membership.expires : qsTr("未开通")
                                    color: Theme.secondaryText
                                    font: Theme.font(12, Theme.regularWeight)
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                }
                                EchoIconButton {
                                    id: expiryInfo
                                    objectName: "profileMembershipInfo-" + member.modelData.kind
                                    visible: !!member.membership
                                    glyph: "info-circle"
                                    text: qsTr("查看%1到期时间").arg(member.modelData.label)
                                    // The anchored dates popup is this button's explanation.
                                    // A second hover tooltip would stack on top of it.
                                    tooltipText: ""
                                    implicitWidth: 20
                                    implicitHeight: 20
                                    padding: 3.5
                                    hoverBackground: false
                                    onHoveredChanged: {
                                        if (hovered) {
                                            expiryClose.stop();
                                            details.expiryAnchor = expiryInfo;
                                            details.expiryMembership = member.membership;
                                            expiryPopupOwner.open();
                                        } else {
                                            expiryClose.restart();
                                        }
                                    }
                                    onClicked: {
                                        details.expiryAnchor = expiryInfo;
                                        details.expiryMembership = member.membership;
                                        expiryPopupOwner.open();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Timer {
        id: expiryClose
        interval: 150
        onTriggered: expiryPopupOwner.close()
    }
    EchoPopupLoader {
        id: expiryPopupOwner
        objectName: "profileMembershipDatesOwner"
        hostItem: details
        sourceComponent: Component {
            EchoAnchoredPopup {
                id: expiryPopup
                objectName: "profileMembershipDates"
                anchorItem: details.expiryAnchor
                uiScale: details.uiScale
                preferredWidth: Math.max(190, dateRows.implicitWidth) + leftPadding + rightPadding
                preferAbove: true
                contentAlign: "center"
                sideOffset: 6
                modal: false
                focus: false
                leftPadding: 14
                rightPadding: 14
                topPadding: 12
                bottomPadding: 12
                contentItem: ColumnLayout {
                    id: dateRows
                    spacing: 8
                    Repeater {
                        model: [
                            {
                                label: qsTr("开始时间"),
                                value: details.expiryMembership ? details.expiryMembership.beginsAt : "--"
                            },
                            {
                                label: qsTr("到期时间"),
                                value: details.expiryMembership ? details.expiryMembership.expiresAt : "--"
                            }
                        ]
                        delegate: RowLayout {
                            id: expiryRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 16
                            Label {
                                text: expiryRow.modelData.label
                                color: Theme.floatingSecondaryText
                                font: Theme.font(12, Theme.regularWeight)
                            }
                            Label {
                                Layout.fillWidth: true
                                text: expiryRow.modelData.value || "--"
                                horizontalAlignment: Text.AlignRight
                                color: Theme.floatingText
                                font: Theme.font(12, Theme.mediumWeight)
                            }
                        }
                    }
                    HoverHandler {
                        id: expiryHover
                        onHoveredChanged: {
                            if (hovered)
                                expiryClose.stop();
                            else
                                expiryClose.restart();
                        }
                    }
                }
            }
        }
    }
    onVisibleChanged: if (!visible) {
        expiryClose.stop();
        expiryPopupOwner.close();
    }
    Connections {
        target: details.account
        function onChanged(): void {
            expiryPopupOwner.close();
        }
    }
}
