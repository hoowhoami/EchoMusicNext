pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

GridLayout {
    id: activity
    required property var stats
    property bool resourceActive: true
    property bool animate: true
    readonly property bool stacked: width < 700 * Theme.store.globalFontSizeScale / 100
    objectName: "historyActivityGrid"
    columns: stacked ? 1 : 2
    columnSpacing: 40
    rowSpacing: 30
    HistoryStatsPanel {
        id: calendar
        objectName: "historyWeekCard"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1.3
        flat: true
        heading: qsTr("听歌留下的日常")
        description: qsTr("%1 · 每首歌最后出现的日期").arg(activity.stats.calendarLabel || "")
        detail: qsTr("%1 首足迹").arg(activity.stats.calendarSongs || 0)
        resourceActive: activity.resourceActive
        animate: activity.animate
        GridLayout {
            objectName: "historyCalendar"
            Layout.fillWidth: true
            columns: 7
            columnSpacing: 6
            rowSpacing: 6
            Repeater {
                model: [qsTr("一"), qsTr("二"), qsTr("三"), qsTr("四"), qsTr("五"), qsTr("六"), qsTr("日")]
                delegate: EchoTextTagRow {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 20
                    horizontalAlignment: Text.AlignHCenter
                    fillTextWidth: true
                    text: modelData
                    font: Theme.font(10, Theme.mediumWeight)
                    color: Theme.secondaryText
                }
            }
            Repeater {
                model: activity.stats.calendarDays || []
                delegate: Rectangle {
                    id: day
                    required property var modelData
                    objectName: "historyCalendarDay"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 35 * Theme.store.globalFontSizeScale / 100
                    radius: 5
                    color: day.modelData.count > 0 ? Theme.mix(Theme.mutedSurface, Theme.accent, .18 + .72 * day.modelData.ratio) : Theme.withAlpha(Theme.text, .035)
                    opacity: day.modelData.future ? .25 : 1
                    EchoDashedFrame {
                        anchors.fill: parent
                        solid: true
                        strokeColor: Theme.accentText
                        radius: 5
                        visible: day.modelData.today
                    }
                    EchoTextTagRow {
                        anchors.fill: parent
                        horizontalAlignment: Text.AlignHCenter
                        fillTextWidth: true
                        text: String(day.modelData.day)
                        font: Theme.font(11, day.modelData.today ? Theme.boldWeight : Theme.mediumWeight)
                        color: day.modelData.ratio > .6 ? Theme.accentForeground : Theme.text
                    }
                    HoverHandler {
                        id: dayHover
                        enabled: activity.resourceActive && activity.animate
                    }
                    EchoToolTip {
                        parent: day
                        requestedVisible: dayHover.hovered
                        text: day.modelData.date
                        description: qsTr("%1 首歌曲最后播放于这一天").arg(day.modelData.count)
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("%1，%2 首歌曲").arg(day.modelData.date).arg(day.modelData.count)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            EchoTextTagRow {
                Layout.fillWidth: true
                text: qsTr("颜色越深，留下的歌曲越多")
                color: Theme.secondaryText
                font: Theme.font(10, Theme.regularWeight)
            }
            Repeater {
                model: 4
                delegate: Rectangle {
                    required property int index
                    Layout.preferredWidth: 9
                    Layout.preferredHeight: 9
                    radius: 2
                    color: Theme.mix(Theme.mutedSurface, Theme.accent, .12 + index * .25)
                }
            }
        }
    }
    HistoryStatsPanel {
        objectName: "historyTimeCard"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 1
        flat: true
        heading: qsTr("你的音乐时钟")
        description: qsTr("24 小时 · 每首歌最后出现的时刻")
        resourceActive: activity.resourceActive
        animate: activity.animate
        order: 1
        Item {
            id: clockFrame
            objectName: "historyClockFrame"
            Layout.fillWidth: true
            Layout.preferredHeight: 244
            Item {
                id: dial
                objectName: "historyClockDial"
                // Preserve the fractional logical center under application zoom.
                x: (clockFrame.width - width) / 2
                y: (clockFrame.height - height) / 2
                width: 244
                height: 244
                Repeater {
                    model: activity.stats.hourlyFootprint || []
                    delegate: Item {
                        id: hour
                        required property var modelData
                        required property int index
                        objectName: "historyHourSpoke"
                        x: (dial.width - width) / 2
                        y: dial.height / 2 - height
                        width: 8
                        height: 100
                        rotation: index * 15
                        transformOrigin: Item.Bottom
                        HistoryStatsBar {
                            id: mark
                            objectName: "historyDayBar"
                            width: 6
                            height: 29
                            vertical: true
                            ratio: .16 + .84 * Number(hour.modelData.ratio || 0)
                            tint: hour.modelData.count > 0 ? Theme.accent : Theme.withAlpha(Theme.text, .12)
                            color: "transparent"
                            resourceActive: activity.resourceActive
                            animate: activity.animate
                        }
                        HoverHandler {
                            id: hourHover
                            enabled: activity.resourceActive && activity.animate
                        }
                        EchoToolTip {
                            parent: mark
                            requestedVisible: hourHover.hovered
                            text: qsTr("%1:00–%1:59").arg(hour.modelData.hour)
                            description: qsTr("%1 首歌曲的最后播放时段").arg(hour.modelData.count)
                        }
                    }
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4
                    EchoIcon {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 23
                        Layout.preferredHeight: 23
                        name: "headphones"
                        color: Theme.accentText
                        resourceActive: activity.resourceActive
                    }
                    EchoTextTagRow {
                        Layout.alignment: Qt.AlignHCenter
                        text: activity.stats.peakHourLabel || "—"
                        font: Theme.font(25, Theme.semiboldWeight)
                    }
                    EchoTextTagRow {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("最密集的时刻")
                        font: Theme.font(10, Theme.regularWeight)
                        color: Theme.secondaryText
                    }
                }
                Repeater {
                    model: ["00", "06", "12", "18"]
                    delegate: EchoTextTagRow {
                        required property string modelData
                        required property int index
                        x: index === 1 ? dial.width - width : index === 3 ? 0 : (dial.width - width) / 2
                        y: index === 0 ? 0 : index === 2 ? dial.height - height : (dial.height - height) / 2
                        text: modelData
                        color: Theme.secondaryText
                        font: Theme.font(10, Theme.mediumWeight)
                    }
                }
            }
        }
    }
}
