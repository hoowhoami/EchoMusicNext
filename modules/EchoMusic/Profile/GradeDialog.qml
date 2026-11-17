pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import EchoMusic.Account
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    objectName: "profileGradeDialog"
    required property AccountService account
    heading: qsTr("我的等级")
    headingPixelSize: 16
    descriptionPixelSize: 12
    descriptionSpacing: 14
    preferredWidth: 400
    topPadding: 26
    bottomPadding: 28
    maximumHeight: 720
    windowHeightMargin: 108
    description: qsTr("每一次聆听，都在积累成长。")
    body: ColumnLayout {
        spacing: 0
        Item {
            id: gradeCard
            objectName: "profileGradeCard"
            Layout.fillWidth: true
            implicitHeight: Math.max(176, card.implicitHeight + 42)
            Layout.minimumHeight: 176
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillGradient: LinearGradient {
                        // Project the diagonal gradient onto the bounding rectangle
                        // so its direction is independent of the card aspect ratio.
                        x1: (gradeCard.width - (gradeCard.width + gradeCard.height) / 2) / 2
                        y1: (gradeCard.height - (gradeCard.width + gradeCard.height) / 2) / 2
                        x2: gradeCard.width - x1
                        y2: gradeCard.height - y1
                        GradientStop {
                            position: 0
                            color: Theme.withAlpha(Theme.accent, 0.14)
                        }
                        GradientStop {
                            position: 1
                            color: Theme.withAlpha(Theme.accent, 0.03)
                        }
                    }
                    PathRectangle {
                        width: gradeCard.width
                        height: gradeCard.height
                        radius: Theme.cardRadius
                    }
                }
            }
            // CSS paints the gradient beneath a translucent border. A centered
            // Shape stroke protrudes/clips and does not cover the same border box.
            EchoDashedFrame {
                objectName: "profileGradeCardOutline"
                anchors.fill: parent
                solid: true
                strokeColor: Theme.floating.subtleBorder
                radius: Theme.cardRadius
            }
            ColumnLayout {
                id: card
                x: 21
                y: 23
                width: parent.width - 42
                spacing: 18
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        Layout.maximumWidth: Number.POSITIVE_INFINITY
                        spacing: 4
                        Label {
                            text: qsTr("当前等级")
                            color: Theme.floatingSecondaryText
                            font: Theme.spacedFont(11, Theme.semiboldWeight, 0.88)
                        }
                        Label {
                            text: "Lv." + dialog.account.grade
                            color: Theme.floatingText
                            font: Theme.spacedFont(30, 800, -1.2)
                        }
                    }
                    GradePlanet {
                        objectName: "profileGradePlanet"
                        Layout.fillWidth: false
                        Layout.fillHeight: false
                        Layout.minimumWidth: 88
                        Layout.maximumWidth: 88
                        Layout.preferredHeight: 76
                    }
                }
                ColumnLayout {
                    visible: dialog.account.gradeAvailable
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillHeight: false
                        spacing: 5
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("距 Lv.%1 还差 %2 经验").arg(dialog.account.nextGrade).arg(Math.max(0, dialog.account.nextGradePoints - dialog.account.gradePoints).toLocaleString(Qt.locale(), 'f', 0))
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, Theme.regularWeight)
                            elide: Text.ElideRight
                        }
                        EchoRollingNumber {
                            text: dialog.account.gradePoints.toLocaleString(Qt.locale(), 'f', 0) + " / " + dialog.account.nextGradePoints.toLocaleString(Qt.locale(), 'f', 0)
                            resourceActive: dialog.visible
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, Theme.regularWeight)
                        }
                    }
                    Rectangle {
                        objectName: "profileGradeTrack"
                        Layout.fillWidth: true
                        implicitHeight: 5
                        radius: 99
                        color: Theme.floating.subtleBorder
                        Rectangle {
                            width: parent.width * Math.min(1, dialog.account.gradePoints / Math.max(1, dialog.account.nextGradePoints))
                            height: parent.height
                            radius: 99
                            color: Theme.accent
                        }
                    }
                }
                Label {
                    visible: !dialog.account.gradeAvailable
                    Layout.fillWidth: true
                    text: dialog.account.gradeLoading ? qsTr("正在获取升级进度…") : qsTr("暂未获取到下一等级进度，请稍后刷新")
                    color: Theme.floatingSecondaryText
                    font: Theme.font(12, Theme.regularWeight)
                    wrapMode: Text.Wrap
                }
            }
        }
        RowLayout {
            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.topMargin: 16
            Layout.bottomMargin: 4
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            spacing: 12
            Label {
                text: qsTr("累计听歌")
                color: Theme.floatingSecondaryText
                font: Theme.font(12, Theme.regularWeight)
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: dialog.account.listeningDuration
                color: Theme.floatingText
                font: Theme.font(12, Theme.semiboldWeight)
            }
        }
    }
    footerContent: RowLayout {
        spacing: 12
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            objectName: "profileGradeRefresh"
            text: qsTr("刷新进度")
            colors: Theme.floating
            enabled: !dialog.account.gradeLoading
            onClicked: dialog.account.loadGrade()
        }
        EchoButton {
            text: qsTr("继续听歌")
            colors: Theme.floating
            size: EchoControlSize.Standard
            onClicked: dialog.close()
        }
    }
}
