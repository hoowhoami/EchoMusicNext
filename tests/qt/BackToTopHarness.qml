pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Controls
import EchoMusic.Theme

Window {
    id: window
    width: 640
    height: 480
    visible: true
    color: Theme.main
    property real uiScale: 1
    property int viewKind: 0
    property bool pageActive: true
    property bool hideAction: false
    property real threshold: 300
    property bool variableHeight: false
    property int delegateCount: 0
    Item {
        width: window.width / window.uiScale
        height: window.height / window.uiScale
        scale: window.uiScale
        transformOrigin: Item.TopLeft
        EchoPageScrollContainer {
            id: page
            objectName: "viewport"
            anchors.fill: parent
            pageActive: window.pageActive
            hideBackToTop: window.hideAction
            backToTopThreshold: window.threshold
            scrollView: window.viewKind === 0 ? list : window.viewKind === 1 ? grid : plain
            coordinatedViews: [list, grid]
            ListView {
                id: list
                objectName: "list"
                anchors.fill: parent
                visible: window.viewKind === 0
                model: 100000
                topMargin: 64
                reuseItems: true
                cacheBuffer: 80
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property int index
                    width: list.width
                    height: window.variableHeight ? 28 + index % 3 * 39 : 48
                    color: Theme.withAlpha(Theme.text, index % 2 ? 0.03 : 0.06)
                    Component.onCompleted: window.delegateCount++
                    Component.onDestruction: window.delegateCount--
                }
            }
            GridView {
                id: grid
                objectName: "grid"
                anchors.fill: parent
                visible: window.viewKind === 1
                model: 100000
                topMargin: 64
                cellWidth: 100
                cellHeight: 72
                reuseItems: true
                cacheBuffer: 80
                delegate: Rectangle {
                    width: 98
                    height: 70
                    color: Theme.withAlpha(Theme.text, 0.04)
                    Component.onCompleted: window.delegateCount++
                    Component.onDestruction: window.delegateCount--
                }
            }
            Flickable {
                id: plain
                objectName: "plain"
                anchors.fill: parent
                visible: window.viewKind === 2
                topMargin: 64
                contentWidth: width
                contentHeight: 10000
            }
        }
    }
}
