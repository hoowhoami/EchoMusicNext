pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Playback

Item {
    id: plaza
    required property AudioEffectCatalog catalog
    required property Popup surface
    property bool resourceActive: false
    ColumnLayout {
        anchors.fill: parent
        spacing: 9
        RowLayout {
            Layout.fillHeight: false
            spacing: 5
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.bottomMargin: 1
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            ColumnLayout {
                Layout.fillHeight: false
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 5
                EchoTextTagRow {
                    text: qsTr("音效广场")
                    color: Theme.floating.text
                    font: Theme.font(14, Theme.boldWeight)
                    lineHeight: 21
                }
                Label {
                    Layout.fillWidth: true
                    text: plaza.catalog.category === "artist" ? qsTr("为歌声量身调校，发现熟悉旋律的另一面") : plaza.catalog.category === "headphone" ? qsTr("选择耳机品牌与型号，找到适合设备的调音") : qsTr("发现创作者的调音作品，收藏不同的听感")
                    font: Theme.font(10, Theme.regularWeight)
                    color: Theme.floating.secondaryText
                    elide: Text.ElideRight
                }
            }
            EchoIconButton {
                Layout.fillWidth: false
                Layout.fillHeight: false
                Layout.maximumWidth: 26
                Layout.maximumHeight: 26
                colors: Theme.floating
                implicitWidth: 26
                implicitHeight: 26
                padding: 6
                glyph: "refresh"
                text: qsTr("刷新音效")
                enabled: !plaza.catalog.loading
                onClicked: plaza.catalog.refresh()
            }
        }
        EchoTabBar {
            objectName: "effectPlazaCategoryTabs"
            compact: true
            colors: Theme.floating
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            labels: [qsTr("歌手音效"), qsTr("耳机专属"), qsTr("音效市场")]
            currentIndex: ["artist", "headphone", "market"].indexOf(plaza.catalog.category)
            onPicked: index => plaza.catalog.selectCategory(["artist", "headphone", "market"][index])
        }
        RowLayout {
            Layout.fillHeight: false
            spacing: 5
            visible: plaza.catalog.category === "market" || plaza.catalog.brandName.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            EchoTabBar {
                objectName: "effectPlazaSortTabs"
                compact: true
                colors: Theme.floating
                visible: plaza.catalog.category === "market"
                Layout.preferredWidth: 220
                labels: [qsTr("推荐"), qsTr("最热"), qsTr("最新")]
                currentIndex: plaza.catalog.sort - 2
                onPicked: index => plaza.catalog.sort = index + 2
            }
            EchoSoftButton {
                colors: Theme.floating
                visible: plaza.catalog.brandName.length > 0
                implicitHeight: 28
                glyph: "chevron-left"
                text: qsTr("全部品牌")
                font: Theme.font(10, Theme.semiboldWeight)
                onClicked: plaza.catalog.allBrands()
            }
            Label {
                text: plaza.catalog.brandName
                color: Theme.floating.text
                font: Theme.font(11, Theme.boldWeight)
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Label {
            visible: plaza.catalog.category === "artist"
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            text: qsTr("手动应用于播放，不会按歌曲歌手自动切换。")
            font: Theme.font(10, Theme.regularWeight)
            color: Theme.floating.secondaryText
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: effects
                anchors.fill: parent
                visible: !plaza.catalog.brands
                clip: true
                reuseItems: true
                EchoViewPoolGuard {
                    targetView: effects
                }
                cacheBuffer: 256
                spacing: 8
                leftMargin: 12
                rightMargin: 12
                bottomMargin: 12
                model: plaza.resourceActive && visible ? plaza.catalog : null
                delegate: PlaybackEffectCard {
                    required property int index
                    catalog: plaza.catalog
                    row: index
                    width: effects.width - 24
                    property bool pooled: false
                    ListView.onPooled: pooled = true
                    ListView.onReused: pooled = false
                    resourceActive: plaza.resourceActive && !pooled
                }
                footer: Item {
                    width: effects.width - 24
                    height: 36
                    EchoSoftButton {
                        colors: Theme.floating
                        anchors.centerIn: parent
                        width: parent.width
                        implicitHeight: 30
                        visible: plaza.catalog.hasMore && plaza.catalog.rowCount() > 0 && plaza.catalog.errorMessage.length === 0
                        enabled: !plaza.catalog.loading
                        text: plaza.catalog.loading ? qsTr("加载中…") : qsTr("加载更多")
                        onClicked: plaza.catalog.loadMore()
                    }
                }
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: plaza.surface
                    view: effects
                    resourceActive: plaza.resourceActive && effects.visible
                }
            }
            GridView {
                id: brands
                anchors.fill: parent
                visible: plaza.catalog.brands
                clip: true
                reuseItems: true
                EchoViewPoolGuard {
                    targetView: brands
                }
                cacheBuffer: 192
                leftMargin: 12
                rightMargin: 12
                cellWidth: (width - 24) / 3
                cellHeight: 64
                model: plaza.resourceActive && visible ? plaza.catalog : null
                header: Column {
                    width: brands.width - 24
                    height: visible ? implicitHeight : 0
                    spacing: 8
                    visible: Object.keys(plaza.catalog.commonEffect).length > 0
                    Label {
                        text: qsTr("通用耳机")
                        color: Theme.floating.text
                        font: Theme.font(11, Theme.boldWeight)
                    }
                    PlaybackEffectCard {
                        width: parent.width
                        effect: plaza.catalog.commonEffect
                        catalog: plaza.catalog
                        row: -1
                        resourceActive: plaza.resourceActive
                    }
                    Label {
                        text: qsTr("按品牌查找")
                        color: Theme.floating.text
                        font: Theme.font(11, Theme.boldWeight)
                    }
                }
                delegate: Button {
                    id: brand
                    required property int index
                    required property var effect
                    width: brands.cellWidth - 7
                    height: brands.cellHeight - 7
                    padding: 8
                    hoverEnabled: true
                    contentItem: RowLayout {
                        spacing: 8
                        EchoIcon {
                            name: "headphones"
                            color: Theme.floating.accentText
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                        }
                        ColumnLayout {
                            Layout.fillHeight: false
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                Layout.fillWidth: true
                                text: brand.effect.name
                                elide: Text.ElideRight
                                color: Theme.floating.text
                                font: Theme.font(11, Theme.boldWeight)
                            }
                            Label {
                                text: brand.effect.model_count + qsTr(" 款")
                                color: Theme.floating.secondaryText
                                font: Theme.font(9, Theme.regularWeight)
                            }
                        }
                    }
                    background: Rectangle {
                        radius: Theme.cardRadius
                        border.width: 1
                        border.color: brand.hovered ? Theme.floating.accent : Theme.floating.controlBorder
                        color: brand.hovered ? Theme.withAlpha(Theme.floating.accent, .07) : Theme.floating.mutedSurface
                    }
                    onClicked: plaza.catalog.selectBrand(index)
                }
                footer: Item {
                    width: brands.width - 24
                    height: 36
                    EchoSoftButton {
                        colors: Theme.floating
                        anchors.centerIn: parent
                        width: parent.width
                        implicitHeight: 30
                        visible: plaza.catalog.hasMore && plaza.catalog.rowCount() > 0 && plaza.catalog.errorMessage.length === 0
                        enabled: !plaza.catalog.loading
                        text: plaza.catalog.loading ? qsTr("加载中…") : qsTr("更多品牌")
                        onClicked: plaza.catalog.loadMore()
                    }
                }
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: plaza.surface
                    view: brands
                    resourceActive: plaza.resourceActive && brands.visible
                }
            }
            Label {
                anchors.centerIn: parent
                visible: plaza.catalog.rowCount() === 0 && !plaza.catalog.errorMessage.length
                text: plaza.catalog.loading ? qsTr("正在加载音效…") : qsTr("暂无音效")
                color: Theme.floating.secondaryText
                font: Theme.font(11, Theme.regularWeight)
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.floating.controlBorder
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 9
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("下载后可在「音效 · 我的音效」中使用")
            color: Theme.floating.secondaryText
            font: Theme.font(9, Theme.regularWeight)
        }
        Label {
            visible: plaza.catalog.errorMessage.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            text: plaza.catalog.errorMessage
            color: Theme.floating.dangerText
            font: Theme.font(11, Theme.regularWeight)
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillHeight: false
            spacing: 5
            visible: plaza.catalog.downloading
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Label {
                text: qsTr("正在下载音效")
                font: Theme.font(10, Theme.regularWeight)
                color: Theme.floating.secondaryText
            }
            ProgressBar {
                Layout.fillWidth: true
                value: plaza.catalog.downloadProgress
            }
        }
    }
}
