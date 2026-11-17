pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Rectangle {
    id: card
    required property var effect
    required property AudioEffectCatalog catalog
    required property int row
    property bool resourceActive: true
    readonly property string unavailable: effect.needs_validation ? "" : effect.unavailable_reason || (effect.vpf_urls && effect.vpf_urls.length > 0 ? qsTr("此音效需要支持 VPF 的音效引擎") : "")
    implicitHeight: Math.max(64, copy.implicitHeight + 20)
    radius: Theme.cardRadius
    color: hovered.hovered || effect.active ? Theme.mix(Theme.floating.elevated, Theme.floating.accent, .06) : Theme.floating.mutedSurface
    EchoDashedFrame {
        anchors.fill: parent
        solid: true
        strokeColor: hovered.hovered || effect.active ? Theme.withAlpha(Theme.floating.accent, .5) : Theme.floating.controlBorder
        radius: Theme.cardRadius
    }
    HoverHandler {
        id: hovered
    }
    EchoToolTip {
        parent: card
        text: card.effect.name || ""
        description: card.unavailable
        requestedVisible: hovered.hovered && (description.length > 0 || effectName.truncated)
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        EchoCover {
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            Layout.maximumWidth: 40
            Layout.maximumHeight: 40
            Layout.fillWidth: false
            Layout.fillHeight: false
            colors: Theme.floating
            cornerRadius: circular ? width / 2 : Theme.controlRadius
            placeholderColor: Theme.floating.elevated
            placeholderGlyph: card.effect.source === "headphone" ? "headphones" : "music"
            circular: card.effect.source === "artist"
            source: card.effect.icon_url || ""
            resourceActive: card.resourceActive
        }
        ColumnLayout {
            id: copy
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 3
            EchoTextTagRow {
                id: effectName
                Layout.fillWidth: true
                text: card.effect.name || ""
                font: Theme.font(12, 650)
                lineHeight: 18
                color: Theme.floating.text
            }
            EchoTextTagRow {
                Layout.fillWidth: true
                text: (card.effect.author || qsTr("音效创作者")) + " · " + (card.effect.source === "artist" ? qsTr("歌手音效") : card.effect.source === "headphone" ? qsTr("耳机音效") : qsTr("音效市场"))
                font: Theme.font(10, Theme.regularWeight)
                lineHeight: 14
                color: Theme.floating.secondaryText
            }
            Label {
                Layout.fillWidth: true
                visible: !!card.effect.aliases && card.effect.aliases.length > 0
                text: qsTr("适用型号：") + [card.effect.name].concat(Array.from(card.effect.aliases || [])).join(" / ")
                color: Theme.floating.secondaryText
                font: Theme.font(9, Theme.regularWeight)
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: card.effect.needs_validation || card.unavailable.length > 0 || card.effect.user_count > 0
                text: card.effect.needs_validation ? qsTr("本地缓存 · 使用前联网确认") : card.unavailable.length > 0 ? card.unavailable : card.effect.user_count >= 10000 ? (card.effect.user_count / 10000).toFixed(card.effect.user_count >= 100000 ? 0 : 1) + qsTr(" 万人使用") : String(card.effect.user_count) + qsTr(" 人使用")
                font: Theme.font(card.unavailable.length > 0 ? 9 : 10, Theme.regularWeight)
                color: Theme.floating.secondaryText
                wrapMode: Text.Wrap
                lineHeightMode: Text.ProportionalHeight
                lineHeight: 1.4
            }
        }
        EchoSoftButton {
            id: action
            Layout.fillWidth: false
            Layout.fillHeight: false
            Layout.maximumWidth: implicitWidth
            colors: Theme.floating
            implicitWidth: Math.max(58, contentItem.implicitWidth + 16)
            implicitHeight: 28
            leftPadding: 8
            rightPadding: 8
            font: Theme.font(10, 650)
            accent: !!card.effect.active
            enabled: card.unavailable.length === 0 && !card.effect.active && !card.catalog.downloading
            glyph: card.effect.active ? "check" : card.effect.downloaded || card.unavailable.length > 0 ? "" : "cloud-download"
            text: card.effect.active ? qsTr("使用中") : card.unavailable.length > 0 ? qsTr("不可用") : card.effect.downloaded ? qsTr("使用") : card.effect.needs_validation ? qsTr("验证并下载") : qsTr("下载")
            onClicked: card.catalog.activateKey(card.effect.key || "")
        }
    }
}
