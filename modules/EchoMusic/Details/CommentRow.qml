pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Account
import EchoMusic.Theme

Item {
    id: row
    objectName: replyRow ? "commentReplyRow" : "commentRootRow"
    required property var entry
    required property string rootId
    required property CommentsController controller
    property bool compact: false
    property real uiScale: 1
    property bool replyRow: false
    property bool resourceActive: true
    // The controller owns expansion across delegate recycling and enrichment.
    readonly property bool contentExpanded: !!entry.content_expanded
    readonly property var membershipPlate: {
        const membership = entry.membership || {};
        if (membership.user_type !== null && membership.user_type !== undefined && (membership.user_type & 16))
            return {
                kind: "super",
                text: qsTr("超级VIP")
            };
        const products = entry.products || [];
        for (const product of ["svip", "wvip", "tvip", "qvip"]) {
            const value = products.find(item => item.product === product && item.active === true);
            if (value)
                return {
                    kind: "concept",
                    text: product === "svip" ? qsTr("概念VIP") : product === "wvip" ? qsTr("周卡") : product === "tvip" ? qsTr("畅听VIP") : qsTr("季卡")
                };
        }
        if (entry.young_vip_type === 2 || entry.young_vip_type === 3)
            return {
                kind: "deluxe",
                text: qsTr("豪华VIP")
            };
        if (entry.young_vip_type === 1)
            return {
                kind: "music",
                text: qsTr("年费音乐包")
            };
        if ([1, 2, 3, 4, 6].indexOf(entry.vip_type) >= 0)
            return {
                kind: "deluxe",
                text: "VIP"
            };
        if (membership.m_type > 0)
            return {
                kind: "music",
                text: qsTr("音乐包")
            };
        return {
            kind: "none",
            text: ""
        };
    }
    readonly property var identityChips: {
        const info = entry.identity || {};
        const chips = [];
        for (const badge of [
            {
                flag: "student_status",
                text: qsTr("学生"),
                tone: "#0ea5e9"
            },
            {
                flag: "actor_status",
                text: qsTr("演员"),
                tone: "#07c160"
            },
            {
                flag: "biz_status",
                text: qsTr("认证"),
                tone: "#07c160"
            },
            {
                flag: "tme_star_status",
                text: qsTr("明星"),
                tone: "#07c160"
            }
        ])
            if (info[badge.flag] > 0)
                chips.push(badge);
        const auth = String(info.auth_info || "").trim();
        if (auth.length && [qsTr("达人"), qsTr("演唱者"), qsTr("歌手"), qsTr("学生")].indexOf(auth) < 0 && !chips.some(chip => chip.text === auth))
            chips.push({
                text: auth,
                tone: "#8b5cf6"
            });
        return chips;
    }
    function likeText(value: real): string {
        return value < 10000 ? String(value) : (value / 10000).toFixed(value >= 100000 ? 0 : 1).replace(/\.0$/, "") + "w";
    }
    signal replyRequested(var target, string rootId)
    signal deleteRequested(var target, string rootId, Item anchor)
    readonly property bool replyOpen: !!entry.reply_open
    readonly property bool threadExpanded: !!entry.thread_expanded
    readonly property real replyGap: Window.window && Window.window.width / uiScale <= 640 ? 8 : 12
    readonly property real contentLeft: replyRow ? surface.mainLeft + surface.floorPadding : surface.outerInset + surface.cardPadding
    readonly property real contentRight: replyRow ? surface.rightInset + surface.floorPadding : surface.rightInset
    readonly property real topInset: replyRow ? 16 : surface.cardPadding
    readonly property real bottomInset: replyRow ? (replyOpen ? 0 : 16) : surface.groupEnd ? surface.cardPadding + surface.cardGap : 0
    implicitHeight: topInset + content.implicitHeight + bottomInset
    CommentSurface {
        id: surface
        anchors.fill: parent
        compact: row.compact
        uiScale: row.uiScale
        part: row.replyRow ? "reply" : "comment"
        threadExpanded: row.threadExpanded
        replyOpen: row.replyOpen
    }
    Rectangle {
        objectName: "commentReplyDivider"
        visible: row.replyRow && !row.entry.reply_first
        x: surface.mainLeft + surface.floorPadding
        width: Math.max(0, row.width - x - row.contentRight)
        height: 1
        color: Theme.subtleBorder
    }
    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: row.contentLeft
        anchors.rightMargin: row.contentRight
        anchors.topMargin: row.topInset
        spacing: row.replyRow ? row.replyGap : 12
        EchoCover {
            objectName: "commentAvatar"
            Layout.alignment: Qt.AlignTop
            Layout.minimumWidth: row.replyRow || row.compact ? 30 : 36
            Layout.maximumWidth: Layout.minimumWidth
            Layout.minimumHeight: Layout.minimumWidth
            Layout.maximumHeight: Layout.minimumWidth
            resourceActive: row.resourceActive
            source: row.entry.avatar_url || ""
            circular: true
            placeholderGlyph: "user"
            placeholderGlyphSize: 14
            Image {
                width: row.replyRow ? 14 : 16
                height: width
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: row.replyRow ? 0 : 1
                anchors.bottomMargin: row.replyRow ? 0 : 1
                visible: !!row.entry.identity && String(row.entry.identity.pic || "").length > 0
                source: visible && row.resourceActive ? row.entry.identity.pic || "" : ""
                asynchronous: true
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    EchoTextTagRow {
                        objectName: "commentUserLine"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: row.entry.user_name || ""
                        font: Theme.font(row.replyRow ? (row.compact ? 11 : 12) : (row.compact ? 12 : 14), Theme.semiboldWeight)
                        color: Theme.text
                        spacing: 8
                        wrap: true
                        MembershipTag {
                            visible: row.membershipPlate.kind !== "none"
                            kind: row.membershipPlate.kind
                            text: row.membershipPlate.text
                        }
                        Repeater {
                            model: row.identityChips
                            delegate: EchoTag {
                                required property var modelData
                                text: modelData.text
                                tone: EchoTag.Custom
                                customTone: modelData.tone
                            }
                        }
                        Label {
                            objectName: "commentReplyTarget"
                            visible: row.replyRow && String(row.entry.quote_name || "").length > 0
                            text: qsTr("回复 %1").arg(row.entry.quote_name || "")
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                    }
                    Label {
                        objectName: "commentRootTime"
                        visible: !row.replyRow
                        Layout.fillWidth: true
                        text: [row.entry.created_at || "", row.entry.location || ""].filter(v => v.length).join(" · ")
                        color: Theme.secondaryText
                        font: Theme.font(10, Theme.regularWeight)
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 15
                        elide: Text.ElideRight
                    }
                }
                EchoBadge {
                    objectName: "commentLikeCount"
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredHeight: font.pixelSize * 1.5 + (row.compact ? 8 : 12)
                    horizontalPadding: row.compact ? 8 : 10
                    foreground: Theme.accentText
                    visible: !row.replyRow && !!row.entry.likes && row.entry.likes.count !== null && row.entry.likes.count !== undefined
                    count: row.entry.likes ? row.entry.likes.count : undefined
                    displayText: row.entry.likes && row.entry.likes.count !== null ? row.likeText(row.entry.likes.count) : ""
                    glyph: "thumb-up"
                    pill: true
                    font: Theme.font(row.compact ? 10 : 11, Theme.semiboldWeight)
                    tone: EchoBadge.Muted
                }
            }
            Label {
                visible: !row.replyRow && String(row.entry.quote_name || "").length > 0
                Layout.topMargin: 2
                text: qsTr("回复 %1").arg(row.entry.quote_name || "")
                color: Theme.secondaryText
                font: Theme.font(10, Theme.regularWeight)
                lineHeightMode: Text.FixedHeight
                lineHeight: 15
            }
            CommentBody {
                objectName: "commentContent"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.topMargin: row.replyRow ? 4 : row.compact ? 8 : 12
                font: Theme.font(row.replyRow ? (row.compact ? 12 : 13) : (row.compact ? 12 : 14), Theme.regularWeight)
                content: String(row.entry.body === undefined ? row.entry.content || "" : row.entry.body)
                expanded: row.contentExpanded
                onToggleRequested: row.controller.toggleContent(String(row.entry.id || ""))
            }
            CommentQuote {
                objectName: "commentQuote"
                resourceActive: row.resourceActive
                visible: String(row.entry.quote_name || "").length > 0
                Layout.fillWidth: true
                Layout.topMargin: 8
                author: row.entry.quote_name || ""
                content: row.entry.quote_content || ""
            }
            RowLayout {
                objectName: "commentActions"
                Layout.fillWidth: true
                Layout.topMargin: row.replyRow ? 8 : 10
                spacing: 16
                Label {
                    objectName: "commentReplyTime"
                    visible: row.replyRow
                    text: [row.entry.created_at || "", row.entry.location || ""].filter(v => v.length).join(" · ")
                    color: Theme.secondaryText
                    font: Theme.font(11, Theme.regularWeight)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 16.5
                }
                EchoSoftButton {
                    objectName: "commentThreadToggle"
                    visible: !row.replyRow && (row.entry.reply_count > 0 || !!row.entry.context_reply)
                    plain: true
                    implicitHeight: 24
                    leftPadding: 0
                    rightPadding: 0
                    glyph: row.entry.thread_expanded ? "chevron-up" : "message-circle"
                    glyphSize: 14
                    font: Theme.font(row.compact ? 11 : 12, Theme.mediumWeight)
                    foreground: Theme.accentText
                    text: row.entry.thread_expanded ? qsTr("收起回复") : row.entry.context_reply ? qsTr("查看上下文") : qsTr("查看%1条回复").arg(row.entry.reply_count)
                    onClicked: row.controller.toggleThread(row.rootId)
                }
                EchoSoftButton {
                    objectName: "commentReply"
                    visible: row.replyRow || !row.entry.context_reply
                    plain: true
                    implicitHeight: row.replyRow ? 18 : 24
                    leftPadding: 0
                    rightPadding: 0
                    font: Theme.font(row.replyRow ? 12 : row.compact ? 11 : 12, row.replyRow ? Theme.regularWeight : Theme.mediumWeight)
                    foreground: row.replyRow && !hovered ? Theme.secondaryText : Theme.accentText
                    text: qsTr("回复")
                    onClicked: row.replyRequested(row.entry, row.rootId)
                }
                EchoIconButton {
                    id: deleteButton
                    objectName: "commentDelete"
                    visible: row.controller.canDelete(row.entry)
                    implicitWidth: 36
                    implicitHeight: 36
                    padding: row.replyRow ? 12 : 11.5
                    hoverBackground: false
                    foreground: hovered ? Theme.colors.dangerText : Theme.colors.iconMain
                    text: row.replyRow ? qsTr("删除回复") : qsTr("删除评论")
                    glyph: "trash"
                    enabled: !row.controller.busy
                    onClicked: row.deleteRequested(row.entry, row.rootId, deleteButton)
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }
    }
}
