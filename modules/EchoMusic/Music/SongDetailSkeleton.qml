pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// Eight bounded decorative rows accompany the loading header and tabs.
// The real song ListView retains its model.
Item {
    id: skeleton
    objectName: "songDetailSkeleton"
    property string typeLabel: ""
    property bool coverRound: false
    property real expandedHeight: 176
    property real coverSize: expandedHeight - 26
    property bool resourceActive: true
    readonly property real inset: width < 768 ? 18 : 24
    readonly property real infoWidth: Math.max(0, body.width - inset * 2 - (width < 768 ? 0 : coverSize + 20))
    SongListGeometry {
        id: skeletonGeometry
    }
    EchoPageScrollContainer {
        id: viewport
        anchors.fill: parent
        pageActive: skeleton.resourceActive
        scrollView: body
        Flickable {
            id: body
            anchors.fill: parent
            contentWidth: width
            contentHeight: content.implicitHeight + 58
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: EchoPageScrollBar {
                view: body
                container: viewport
                viewActive: skeleton.resourceActive
            }
            ColumnLayout {
                id: content
                x: skeleton.inset
                y: 10
                width: Math.max(0, body.width - skeleton.inset * 2)
                spacing: 0
                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.minimumHeight: skeleton.expandedHeight - 56
                    columns: skeleton.width < 768 ? 1 : 2
                    columnSpacing: 20
                    rowSpacing: 16
                    Item {
                        Layout.preferredWidth: skeleton.coverSize
                        Layout.preferredHeight: skeleton.coverSize
                        Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                        EchoSurfaceShadow {
                            anchors.fill: parent
                            surface: cover
                            z: -1
                            radius: skeleton.coverRound ? width / 2 : 16
                            blur: 3
                            offset: Qt.vector2d(0, 1)
                            color: Qt.rgba(0, 0, 0, Theme.dark ? .18 : .1)
                        }
                        EchoSkeleton {
                            id: cover
                            objectName: "songDetailInitialCoverSkeleton"
                            anchors.fill: parent
                            radius: skeleton.coverRound ? width / 2 : 16
                            animate: skeleton.resourceActive
                        }
                    }
                    ColumnLayout {
                        id: info
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        // Resolve percentages from the available column, avoiding a
                        // child preferred width -> parent implicit width loop.
                        Layout.preferredWidth: skeleton.infoWidth
                        Layout.preferredHeight: skeleton.coverSize
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: false
                            spacing: 12
                            EchoSkeleton {
                                variant: EchoSkeleton.Text
                                Layout.preferredWidth: Math.min(420, skeleton.infoWidth * .72)
                                Layout.preferredHeight: 28
                                animate: skeleton.resourceActive
                            }
                            EchoTag {
                                visible: skeleton.typeLabel.length > 0
                                text: skeleton.typeLabel
                                tone: EchoTag.Accent
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 8
                            Layout.bottomMargin: 8
                            spacing: 10
                            RowLayout {
                                Layout.fillHeight: false
                                spacing: 10
                                EchoSkeleton {
                                    variant: EchoSkeleton.Circle
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    animate: skeleton.resourceActive
                                }
                                EchoSkeleton {
                                    variant: EchoSkeleton.Text
                                    Layout.preferredWidth: 118
                                    Layout.preferredHeight: 13
                                    animate: skeleton.resourceActive
                                }
                                EchoSkeleton {
                                    variant: EchoSkeleton.Text
                                    Layout.preferredWidth: 86
                                    Layout.preferredHeight: 11
                                    animate: skeleton.resourceActive
                                }
                            }
                            RowLayout {
                                Layout.fillHeight: false
                                spacing: 10
                                EchoSkeleton {
                                    variant: EchoSkeleton.Text
                                    Layout.preferredWidth: 72
                                    Layout.preferredHeight: 11
                                    animate: skeleton.resourceActive
                                }
                                EchoSkeleton {
                                    variant: EchoSkeleton.Text
                                    Layout.preferredWidth: 96
                                    Layout.preferredHeight: 11
                                    animate: skeleton.resourceActive
                                }
                                EchoSkeleton {
                                    variant: EchoSkeleton.Text
                                    Layout.preferredWidth: 62
                                    Layout.preferredHeight: 11
                                    animate: skeleton.resourceActive
                                }
                            }
                            EchoSkeleton {
                                variant: EchoSkeleton.Text
                                Layout.preferredWidth: Math.min(520, skeleton.infoWidth * .86)
                                Layout.preferredHeight: 12
                                animate: skeleton.resourceActive
                            }
                        }
                        RowLayout {
                            Layout.fillHeight: false
                            spacing: 10
                            EchoSkeleton {
                                variant: EchoSkeleton.Text
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 34
                                animate: skeleton.resourceActive
                            }
                            EchoSkeleton {
                                variant: EchoSkeleton.Text
                                Layout.preferredWidth: 82
                                Layout.preferredHeight: 34
                                animate: skeleton.resourceActive
                            }
                            EchoSkeleton {
                                variant: EchoSkeleton.Circle
                                Layout.preferredWidth: 34
                                Layout.preferredHeight: 34
                                animate: skeleton.resourceActive
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.topMargin: 18
                    Layout.bottomMargin: 12
                    Layout.fillHeight: false
                    spacing: 8
                    EchoSkeleton {
                        variant: EchoSkeleton.Text
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 30
                        animate: skeleton.resourceActive
                    }
                    EchoSkeleton {
                        variant: EchoSkeleton.Text
                        Layout.preferredWidth: 76
                        Layout.preferredHeight: 30
                        animate: skeleton.resourceActive
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.subtleBorder
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    Layout.bottomMargin: 16
                    spacing: 0
                    Repeater {
                        model: 8
                        SongSkeletonRow {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            geometry: skeletonGeometry
                            showAlbum: skeleton.width >= 768
                            animate: skeleton.resourceActive
                        }
                    }
                }
            }
        }
    }
}
