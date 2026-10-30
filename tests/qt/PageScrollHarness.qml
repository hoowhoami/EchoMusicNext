pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls

// A lifecycle/input fixture only; production tests still load formal SearchPage.
Window {
    id: window
    width: 500
    height: 400
    visible: true
    color: "#1254a8"
    property bool useGrid: false
    property bool useAuxiliary: false
    property int rowCount: 1000
    property int headerHeight: 60
    property int rowClicks: 0
    property int headerClicks: 0
    function locate(index: int): void {
        EchoListPositioning.positionIfNeeded(list, index, ListView.Center, page);
    }
    function resizeHeader(height: int) {
        page.preservePosition(() => {
            headerHeight = height;
        });
    }
    EchoPageScrollContainer {
        id: page
        objectName: "harnessPage"
        anchors.fill: parent
        scrollView: window.useAuxiliary ? auxiliary : window.useGrid ? grid : list
        coordinatedViews: [list, grid]
        resetViews: [auxiliary]
        stickyHeaders: [header]
        ListView {
            id: list
            objectName: "harnessList"
            anchors.fill: parent
            visible: !window.useGrid && !window.useAuxiliary
            model: page.pageActive ? window.rowCount : 0
            topMargin: window.headerHeight
            reuseItems: true
            cacheBuffer: 100
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPageScrollBar {
                view: list
                container: page
                viewActive: list.visible
            }
            delegate: Rectangle {
                width: list.width
                height: 50
                color: "#ed4538"
                MouseArea {
                    anchors.fill: parent
                    onClicked: window.rowClicks++
                }
            }
        }
        GridView {
            id: grid
            objectName: "harnessGrid"
            anchors.fill: parent
            visible: window.useGrid && !window.useAuxiliary
            model: page.pageActive ? window.rowCount : 0
            topMargin: window.headerHeight
            cellWidth: 100
            cellHeight: 50
            reuseItems: true
            cacheBuffer: 100
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPageScrollBar {
                view: grid
                container: page
                viewActive: grid.visible
            }
            delegate: Rectangle {
                width: 100
                height: 50
                color: "#ed4538"
            }
        }
        Flickable {
            id: auxiliary
            objectName: "harnessAuxiliary"
            anchors.fill: parent
            visible: window.useAuxiliary
            contentWidth: width
            contentHeight: 2000
            boundsBehavior: Flickable.StopAtBounds
            Rectangle {
                width: auxiliary.width
                height: 2000
                color: "#1254a8"
            }
            ScrollBar.vertical: EchoPageScrollBar {
                view: auxiliary
                container: page
                viewActive: auxiliary.visible
            }
        }
        EchoPageStickyHeader {
            id: header
            container: page
            height: window.headerHeight
            Button {
                width: 100
                height: parent.height
                text: "Header action"
                onClicked: window.headerClicks++
            }
        }
    }
}
