pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

// The marketplace uses six bounded loading placeholders.
Item {
    id: grid
    objectName: "pluginMarketplaceSkeleton"
    property int columns: 1
    property bool resourceActive: true
    implicitHeight: Math.ceil(6 / columns) * 287 + (Math.ceil(6 / columns) - 1) * 14
    Repeater {
        model: 6
        delegate: Rectangle {
            id: skeletonCard
            required property int index
            width: (grid.width - (grid.columns - 1) * 14) / grid.columns
            height: 287
            x: (index % grid.columns) * (width + 14)
            y: Math.floor(index / grid.columns) * 301
            radius: Theme.cardRadius
            color: Theme.mutedSurface
            ColumnLayout {
                x: 17
                y: 17
                width: parent.width - 34
                spacing: 12
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 10
                    EchoSkeleton {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        radius: 8
                        shimmer: true
                        animate: grid.resourceActive
                    }
                    ColumnLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        spacing: 5.6
                        RowLayout {
                            Layout.fillHeight: false
                            spacing: 5
                            Layout.fillWidth: true
                            Layout.preferredHeight: 22
                            EchoSkeleton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 15
                                variant: EchoSkeleton.Text
                                shimmer: true
                                animate: grid.resourceActive
                            }
                            EchoSkeleton {
                                Layout.preferredWidth: 54
                                Layout.preferredHeight: 20
                                variant: EchoSkeleton.Text
                                shimmer: true
                                animate: grid.resourceActive
                            }
                        }
                        RowLayout {
                            Layout.fillHeight: false
                            spacing: 5
                            Layout.fillWidth: true
                            EchoSkeleton {
                                Layout.preferredWidth: (skeletonCard.width - 88) * 0.38
                                Layout.preferredHeight: 12
                                variant: EchoSkeleton.Text
                                shimmer: true
                                animate: grid.resourceActive
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            EchoSkeleton {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 12
                                variant: EchoSkeleton.Text
                                shimmer: true
                                animate: grid.resourceActive
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    spacing: 8
                    EchoSkeleton {
                        Layout.preferredWidth: (skeletonCard.width - 34) * 0.94
                        Layout.preferredHeight: 12
                        variant: EchoSkeleton.Text
                        shimmer: true
                        animate: grid.resourceActive
                    }
                    EchoSkeleton {
                        Layout.preferredWidth: (skeletonCard.width - 34) * 0.72
                        Layout.preferredHeight: 12
                        variant: EchoSkeleton.Text
                        shimmer: true
                        animate: grid.resourceActive
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    spacing: 6
                    Repeater {
                        model: 3
                        EchoSkeleton {
                            Layout.preferredWidth: Math.min(68, (skeletonCard.width - 46) / 3)
                            Layout.preferredHeight: 24
                            variant: EchoSkeleton.Text
                            shimmer: true
                            animate: grid.resourceActive
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    spacing: 5
                    Layout.fillWidth: true
                    EchoSkeleton {
                        Layout.preferredWidth: (skeletonCard.width - 34) * 0.4
                        Layout.preferredHeight: 16
                        variant: EchoSkeleton.Text
                        shimmer: true
                        animate: grid.resourceActive
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSkeleton {
                        Layout.preferredWidth: (skeletonCard.width - 34) * 0.45
                        Layout.preferredHeight: 16
                        variant: EchoSkeleton.Text
                        shimmer: true
                        animate: grid.resourceActive
                    }
                }
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    spacing: 5
                    Repeater {
                        model: 3
                        EchoSkeleton {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 18
                            variant: EchoSkeleton.Text
                            shimmer: true
                            animate: grid.resourceActive
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 43
                    EchoDivider {
                        width: parent.width
                        lineColor: Theme.subtleBorder
                    }
                    RowLayout {
                        y: 11
                        width: parent.width
                        spacing: 6
                        EchoSkeleton {
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 32
                            variant: EchoSkeleton.Text
                            shimmer: true
                            animate: grid.resourceActive
                        }
                        EchoSkeleton {
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 32
                            variant: EchoSkeleton.Text
                            shimmer: true
                            animate: grid.resourceActive
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        EchoSkeleton {
                            Layout.preferredWidth: 78
                            Layout.preferredHeight: 32
                            variant: EchoSkeleton.Text
                            shimmer: true
                            animate: grid.resourceActive
                        }
                    }
                }
            }
        }
    }
}
