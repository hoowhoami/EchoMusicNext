pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls

Item {
    id: header
    objectName: "songDetailHeader"
    property real scrollTop: 0
    property string heading: ""
    property string typeLabel: ""
    property string subtitle: ""
    property string metadata: ""
    property string description: ""
    property string day: ""
    property bool ranking: false
    property bool playable: false
    property bool resourceActive: true
    property bool coverRound: false
    property Component coverComponent
    property Component detailsComponent
    property Component secondaryDetailsComponent: metadata.length > 0 ? defaultMetadata : null
    property Component tertiaryDetailsComponent
    property Component collapsedActions
    property Component additionalActions
    property bool showPlaybackActions: true
    /// Pages control the visibility of the shared header actions.
    property bool showBatchAction: true
    property bool compactExpandedActions: false
    property Component leadingActions
    property bool loading: false
    property real expandedHeight: 176 + (description.length > 0 ? 28 : 0)
    readonly property real progress: Math.max(0, Math.min(1, scrollTop / (expandedHeight - 56)))
    property real coverSize: 150 + (description.length > 0 ? 28 : 0)
    readonly property real coverScale: 1 - progress * (1 - 32 / coverSize)
    signal playRequested
    signal batchRequested
    signal selectRequested
    signal introRequested
    implicitHeight: expandedHeight - (expandedHeight - 56) * progress
    // Main owns the composited skin/material and atmosphere. EchoMusic's
    // final page-sticky-item rule makes the sliver transparent; painting
    // bg-main here would erase that material directly below the titlebar.
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 20 - header.progress * 8
        Item {
            objectName: "songDetailCoverSlot"
            Layout.preferredWidth: header.coverSize * header.coverScale
            Layout.preferredHeight: header.coverSize * header.coverScale
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 10 + 2 * header.progress
            Loader {
                width: header.coverSize
                height: width
                scale: header.coverScale
                transformOrigin: Item.TopLeft
                sourceComponent: header.coverComponent
                EchoSkeleton {
                    objectName: "songDetailCoverSkeleton"
                    anchors.fill: parent
                    variant: header.coverRound ? EchoSkeleton.Circle : EchoSkeleton.Block
                    visible: header.loading
                    animate: visible && header.resourceActive
                }
            }
        }
        ColumnLayout {
            // The metadata column follows scaled artwork height.
            // Collapsed columns share a 32px slot at y=12.
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: false
            Layout.preferredHeight: header.coverSize * header.coverScale
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 10 + 2 * header.progress
            Layout.bottomMargin: 16 * (1 - header.progress)
            spacing: 0
            EchoTextTagRow {
                objectName: "songDetailTitleRow"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: header.heading
                color: Theme.text
                font: Theme.font(24 - 6.5 * header.progress, Theme.boldWeight)
                lineHeight: font.pixelSize * 1.25
                // Keep the glyph row centered in that slot using shared ink
                // metrics, rather than retaining the expanded top alignment.
                Layout.preferredHeight: lineHeight + (32 - lineHeight) * header.progress
                Layout.alignment: Qt.AlignTop
                textItem.objectName: "songDetailTitleText"
                fillTextWidth: true
                spacing: 12
                // DetailPageSliverHeader's justify-between title row keeps
                // the shared type-badge at its right edge, including for long
                // titles. Opacity fades without changing the reserved width.
                EchoTag {
                    objectName: "songDetailTypeTag"
                    visible: header.typeLabel.length > 0
                    text: header.typeLabel.toUpperCase()
                    tone: EchoTag.Accent
                    font: Theme.spacedFont(10, Theme.semiboldWeight, .3)
                    opacity: Math.max(0, 1 - header.progress * 3.5)
                }
            }
            ColumnLayout {
                id: detailsArea
                enabled: header.progress <= .4
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                visible: header.progress < .72
                opacity: Math.max(0, 1 - header.progress * 3.5)
                spacing: 0
                // All current EchoMusic detail pages opt into space-evenly.
                // Each metadata row must be a separate slot, including the
                // default subtitle/metadata, so new pages inherit that rule.
                Item {
                    Layout.fillHeight: true
                    Layout.preferredHeight: 0
                }
                Loader {
                    id: primaryDetails
                    objectName: "songDetailPrimaryDetails"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.fillHeight: false
                    active: header.detailsComponent !== null || header.subtitle.length > 0
                    visible: active
                    sourceComponent: header.detailsComponent ? header.detailsComponent : defaultDetails
                }
                Item {
                    visible: primaryDetails.active && (secondaryDetails.active || tertiaryDetails.active || introduction.visible)
                    Layout.fillHeight: true
                    Layout.preferredHeight: 0
                }
                Loader {
                    id: secondaryDetails
                    objectName: "songDetailSecondaryDetails"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.fillHeight: false
                    active: header.secondaryDetailsComponent !== null
                    visible: active
                    sourceComponent: header.secondaryDetailsComponent
                }
                Item {
                    visible: secondaryDetails.active && (tertiaryDetails.active || introduction.visible)
                    Layout.fillHeight: true
                    Layout.preferredHeight: 0
                }
                Loader {
                    id: tertiaryDetails
                    objectName: "songDetailTertiaryDetails"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.fillHeight: false
                    active: header.tertiaryDetailsComponent !== null
                    visible: active
                    sourceComponent: header.tertiaryDetailsComponent
                }
                Item {
                    visible: tertiaryDetails.active && introduction.visible
                    Layout.fillHeight: true
                    Layout.preferredHeight: 0
                }
                RowLayout {
                    id: introduction
                    objectName: "songDetailDescription"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.fillHeight: false
                    visible: preview.text.length > 0
                    spacing: 8
                    EchoTextTagRow {
                        id: preview
                        objectName: "songDetailDescriptionPreview"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.fillHeight: false
                        text: header.description.replace(/\s+/g, " ").trim()
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.secondaryText
                        lineHeight: 20
                        fillTextWidth: true
                    }
                    TextMetrics {
                        id: introductionMetrics
                        font: preview.font
                        text: preview.text
                    }
                    Button {
                        id: detailsAction
                        objectName: "songDetailDescriptionAction"
                        // Measure overflow against the full line before reserving button space.
                        visible: introductionMetrics.advanceWidth > introduction.width
                        text: qsTr("查看详情")
                        Accessible.name: qsTr("查看完整简介")
                        font: Theme.font(11, Theme.semiboldWeight)
                        padding: 0
                        Layout.fillHeight: false
                        hoverEnabled: true
                        HoverHandler {
                            enabled: detailsAction.enabled
                            cursorShape: Qt.PointingHandCursor
                        }
                        enabled: header.progress <= .4
                        onClicked: header.introRequested()
                        contentItem: EchoTextTagRow {
                            text: detailsAction.text
                            font: detailsAction.font
                            lineHeight: 20
                            color: Theme.accentText
                        }
                        background: Rectangle {
                            color: "transparent"
                            radius: Theme.detailRadius
                            border.width: detailsAction.visualFocus ? 1 : 0
                            border.color: Theme.accent
                        }
                    }
                }
                Item {
                    Layout.fillHeight: true
                    Layout.preferredHeight: 0
                }
            }
            RowLayout {
                objectName: "songDetailActions"
                enabled: header.progress <= .4
                Layout.fillWidth: true
                Layout.fillHeight: false
                visible: header.progress < .72
                opacity: Math.max(0, 1 - header.progress * 3.5)
                spacing: 8
                Loader {
                    visible: header.leadingActions !== null
                    sourceComponent: header.leadingActions
                    Layout.fillHeight: false
                }
                EchoSoftButton {
                    objectName: "catalogPlayAll"
                    display: header.compactExpandedActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    visible: header.showPlaybackActions
                    text: qsTr("播放")
                    glyph: "play"
                    accent: true
                    enabled: header.playable
                    onClicked: header.playRequested()
                }
                EchoSoftButton {
                    objectName: "catalogBatch"
                    display: header.compactExpandedActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    visible: header.showPlaybackActions && header.showBatchAction
                    text: qsTr("批量")
                    glyph: "list"
                    enabled: header.playable
                    onClicked: header.batchRequested()
                }
                Loader {
                    sourceComponent: header.additionalActions
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }
        RowLayout {
            visible: header.progress >= .72
            spacing: 8
            EchoIconButton {
                visible: header.ranking
                glyph: "chevron-down"
                text: qsTr("选择排行榜")
                onClicked: header.selectRequested()
            }
            EchoIconButton {
                visible: header.showPlaybackActions
                glyph: "play"
                text: qsTr("播放")
                foreground: Theme.accentText
                enabled: header.playable
                onClicked: header.playRequested()
            }
            EchoIconButton {
                visible: header.showPlaybackActions && header.showBatchAction
                glyph: "list"
                text: qsTr("批量")
                enabled: header.playable
                onClicked: header.batchRequested()
            }
            Loader {
                sourceComponent: header.collapsedActions
            }
        }
    }
    Component {
        id: defaultDetails
        EchoTextTagRow {
            text: header.subtitle
            color: Theme.secondaryText
            font: Theme.font(13, Theme.semiboldWeight)
            fillTextWidth: true
        }
    }
    Component {
        id: defaultMetadata
        EchoTextTagRow {
            text: header.metadata
            color: Theme.secondaryText
            font: Theme.font(11, Theme.semiboldWeight)
            fillTextWidth: true
        }
    }
}
