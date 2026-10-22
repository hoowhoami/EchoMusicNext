pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell

EchoDialog {
    id: dialog
    objectName: "taskCenterDialog"
    required property TaskCenterModel service
    heading: qsTr("任务中心")
    preferredWidth: 600
    // Preserve 24px clearance from the window edge at narrow widths.
    width: Math.min(preferredWidth, parent ? Math.max(0, parent.width / uiScale - 48) : preferredWidth)
    windowHeightMargin: 64
    maximumHeight: 720
    flushBody: true
    onAboutToShow: service.resetDetailExpansion()
    closeButton.objectName: "taskCenterClose"
    closeButton.text: qsTr("关闭任务中心")
    component TaskActionButton: EchoSoftButton {
        id: taskActionButton
        required property var descriptor
        required property string taskId
        required property string taskToken
        property string detailId: ""
        property real maximumWidth: Infinity
        implicitWidth: Math.min(maximumWidth, contentItem.implicitWidth + leftPadding + rightPadding)
        objectName: "taskAction-" + taskId + "/" + detailId + "/" + descriptor.id
        size: detailId.length ? EchoControlSize.Compact : EchoControlSize.Standard
        text: descriptor.label
        enabled: descriptor.enabled
        opacity: enabled ? 1 : 0.4
        colors: Theme.floating
        font: Theme.font(12, Theme.semiboldWeight)
        accent: descriptor.variant === 1
        danger: descriptor.variant === 2
        EchoToolTip {
            parent: taskActionButton
            text: taskActionButton.text
            requestedVisible: taskActionButton.hovered && (taskActionButton.contentItem as EchoButtonContent).truncated
        }
        onClicked: dialog.service.invokeAction(taskId, taskToken, descriptor.id, detailId)
    }
    component TaskActionRow: Item {
        id: group
        required property var descriptors
        required property string taskId
        required property string taskToken
        property bool dismissible: false
        objectName: "taskActionRow-" + taskId
        // Use separate 8px row and column gaps with right-aligned wrapped actions.
        // Only this bounded action set is laid out; task lists remain virtualized.
        function schedule() {
            Qt.callLater(layoutActions);
        }
        function layoutActions() {
            const buttons = children.filter(item => item.visible && item.implicitWidth > 0 && item.implicitHeight > 0);
            let natural = 0, row = {
                items: [],
                width: 0,
                height: 0
            };
            const rows = [];
            for (const button of buttons) {
                const w = button.implicitWidth, h = button.implicitHeight;
                natural += (natural > 0 ? 8 : 0) + w;
                if (row.items.length && row.width + 8 + w > width) {
                    rows.push(row);
                    row = {
                        items: [],
                        width: 0,
                        height: 0
                    };
                }
                row.width += (row.items.length ? 8 : 0) + w;
                row.height = Math.max(row.height, h);
                row.items.push(button);
            }
            if (row.items.length)
                rows.push(row);
            let y = 0;
            for (const line of rows) {
                let x = width - line.width;
                for (const button of line.items) {
                    button.width = button.implicitWidth;
                    button.height = button.implicitHeight;
                    button.x = x;
                    button.y = y + (line.height - button.height) / 2;
                    x += button.width + 8;
                }
                y += line.height + 8;
            }
            implicitWidth = natural;
            implicitHeight = rows.length ? y - 8 : 0;
        }
        onWidthChanged: schedule()
        Component.onCompleted: schedule()
        Repeater {
            model: group.descriptors
            onItemAdded: group.schedule()
            onItemRemoved: group.schedule()
            delegate: TaskActionButton {
                required property var modelData
                descriptor: modelData
                maximumWidth: group.width
                taskId: group.taskId
                taskToken: group.taskToken
                onImplicitWidthChanged: group.schedule()
                onImplicitHeightChanged: group.schedule()
                onVisibleChanged: group.schedule()
            }
        }
        EchoSoftButton {
            visible: group.dismissible
            text: qsTr("移除记录")
            colors: Theme.floating
            onImplicitWidthChanged: group.schedule()
            onImplicitHeightChanged: group.schedule()
            onVisibleChanged: group.schedule()
            onClicked: dialog.service.dismiss(group.taskId, group.taskToken)
        }
    }
    body: ColumnLayout {
        spacing: 0
        ColumnLayout {
            objectName: "taskCenterEmpty"
            visible: dialog.service.count === 0
            Layout.fillWidth: true
            Layout.rightMargin: 24
            Layout.topMargin: 36
            Layout.bottomMargin: 36
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 56
                implicitHeight: 56
                radius: Theme.cardRadius
                color: Theme.floating.mutedSurface
                EchoIcon {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    name: "clipboard-list"
                    color: Theme.floatingSecondaryText
                }
            }
            EchoTextTagRow {
                Layout.alignment: Qt.AlignHCenter
                objectName: "taskCenterEmptyTitle"
                text: qsTr("暂无任务")
                font: Theme.font(14, Theme.mediumWeight)
                color: Theme.floatingText
                lineHeight: 21
            }
            EchoTextTagRow {
                Layout.fillWidth: true
                text: qsTr("应用和插件的任务会显示在这里")
                fillTextWidth: true
                font: Theme.font(12, Theme.regularWeight)
                color: Theme.floatingSecondaryText
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 18
            }
        }
        EchoBoundedListView {
            id: tasks
            maximumViewportHeight: dialog.bodyHeightLimit
            objectName: "taskCenterList"
            visible: dialog.service.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: implicitHeight
            clip: true
            model: dialog.visible ? dialog.service : null
            // Each activation creates a new dialog. Drain the delegate pool on close;
            // keep reuse enabled while browsing.
            reuseItems: dialog.visible
            cacheBuffer: 0
            spacing: 12
            ScrollBar.vertical: EchoPopupScrollBar {
                objectName: "taskCenterScrollBar"
                surface: dialog
                view: tasks
                resourceActive: dialog.visible
            }
            delegate: Item {
                id: task
                required property string taskId
                required property string taskToken
                required property string taskIcon
                required property string taskName
                required property int taskStatus
                required property string taskProgressLabel
                required property string taskStatusLabel
                required property bool taskHasProgress
                required property real taskPercent
                required property string taskError
                required property var taskActions
                required property TaskDetailsModel taskDetails
                required property bool taskDismissible
                property bool pooled: false
                readonly property bool inViewport: !pooled && y + height > tasks.contentY && y < tasks.contentY + tasks.height
                readonly property bool detailsExpanded: task.taskDetails && task.taskDetails.expanded
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                objectName: "taskCenterEntry"
                width: Math.max(0, tasks.width - 24)
                height: taskBody.implicitHeight + 32
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.cardRadius
                    color: Theme.floating.mutedSurface
                    border.width: 1
                    border.color: Theme.floating.subtleBorder
                }
                ColumnLayout {
                    id: taskBody
                    x: 16
                    y: 16
                    width: parent.width - 32
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        spacing: 10
                        Rectangle {
                            implicitWidth: 36
                            implicitHeight: 36
                            radius: Theme.controlRadius
                            color: Theme.floating.mutedSurface
                            EchoIcon {
                                anchors.centerIn: parent
                                width: 20
                                height: 20
                                name: task.taskIcon.length ? task.taskIcon : "clipboard-list"
                                color: Theme.floatingAccentText
                            }
                        }
                        EchoTextTagRow {
                            objectName: "taskName-" + task.taskId
                            Layout.fillWidth: true
                            Layout.fillHeight: false
                            Layout.minimumWidth: 0
                            text: task.taskName
                            font: Theme.font(14, Theme.semiboldWeight)
                            color: Theme.floatingText
                            lineHeight: 20
                            wrap: true
                        }
                        EchoTag {
                            text: task.taskStatusLabel
                            tone: task.taskStatus === TaskCenterModel.Running ? EchoTag.Accent : task.taskStatus === TaskCenterModel.Error || task.taskStatus === TaskCenterModel.Completed ? EchoTag.Custom : EchoTag.Neutral
                            customTone: task.taskStatus === TaskCenterModel.Error ? Theme.colors.danger : Theme.colors.success
                            colors: Theme.floating
                        }
                    }
                    RowLayout {
                        spacing: 0
                        visible: task.taskProgressLabel.length > 0 || task.taskPercent >= 0
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        EchoTextTagRow {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: task.taskProgressLabel
                            color: Theme.floatingSecondaryText
                            font: Theme.font(12, Theme.regularWeight)
                            lineHeight: 18
                        }
                        EchoTextTagRow {
                            visible: task.taskPercent >= 0
                            text: task.taskPercent.toFixed(task.taskPercent % 1 ? 1 : 0) + "%"
                            color: Theme.floatingText
                            font: Theme.font(12, Theme.regularWeight)
                            lineHeight: 18
                        }
                    }
                    ProgressBar {
                        id: taskProgress
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        Layout.topMargin: -6
                        from: 0
                        to: 100
                        value: Math.max(0, task.taskPercent)
                        visible: task.taskHasProgress
                        indeterminate: task.taskPercent < 0 && task.inViewport && dialog.visible
                        contentItem: Item {
                            clip: true
                            Rectangle {
                                id: movingProgress
                                width: (taskProgress.indeterminate ? 0.35 : taskProgress.visualPosition) * parent.width
                                height: 5
                                radius: 2.5
                                x: taskProgress.indeterminate ? -width : 0
                                color: task.taskStatus === TaskCenterModel.Error ? Theme.colors.danger : task.taskStatus === TaskCenterModel.Completed ? Theme.colors.success : Theme.accent
                                NumberAnimation on x {
                                    running: taskProgress.indeterminate && dialog.visible && !task.pooled
                                    from: -movingProgress.width
                                    to: movingProgress.parent.width
                                    loops: Animation.Infinite
                                    duration: 1600
                                    easing.type: Easing.InOutQuad
                                }
                            }
                        }
                        background: Rectangle {
                            radius: 2.5
                            color: Theme.floating.hover
                        }
                    }
                    Button {
                        id: detailToggle
                        objectName: "taskDetailsToggle-" + task.taskId
                        Layout.fillWidth: true
                        visible: task.taskDetails && task.taskDetails.count > 0
                        implicitHeight: 36
                        padding: 0
                        text: qsTr("任务明细 %1 项").arg(task.taskDetails ? task.taskDetails.count : 0)
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        contentItem: RowLayout {
                            spacing: 0
                            EchoTextTagRow {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: detailToggle.text
                                font: Theme.font(12, Theme.regularWeight)
                                color: Theme.floatingSecondaryText
                            }
                            EchoIcon {
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                name: "chevron-down"
                                rotation: task.detailsExpanded ? 180 : 0
                                color: Theme.floatingSecondaryText
                            }
                        }
                        background: Item {}
                        onClicked: task.taskDetails.expanded = !task.detailsExpanded
                    }
                    EchoBoundedListView {
                        id: details
                        maximumViewportHeight: 260
                        objectName: "taskCenterDetails"
                        visible: task.detailsExpanded && count > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? implicitHeight : 0
                        Layout.topMargin: 2 // Source details margin is 10, versus the common 8px gap.
                        model: dialog.visible && !task.pooled && task.detailsExpanded ? task.taskDetails : null
                        reuseItems: dialog.visible && !task.pooled
                        cacheBuffer: 0
                        clip: true
                        ScrollBar.vertical: EchoScrollBar {
                            resourceActive: dialog.visible && !task.pooled && task.detailsExpanded
                        }
                        delegate: Item {
                            id: detail
                            required property string detailId
                            required property string detailName
                            required property string detailDescription
                            required property string detailError
                            required property string detailStatus
                            required property var detailActions
                            objectName: "taskCenterDetailRow"
                            width: details.width
                            height: detailBody.implicitHeight + 16
                            Rectangle {
                                width: parent.width
                                height: 1
                                color: Theme.floating.mutedSurface
                            }
                            RowLayout {
                                id: detailBody
                                anchors.fill: parent
                                anchors.topMargin: 8
                                anchors.bottomMargin: 8
                                spacing: 8
                                ColumnLayout {
                                    Layout.fillHeight: false
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: detail.detailName
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        font: Theme.font(12, Theme.semiboldWeight)
                                        color: Theme.floatingText
                                        wrapMode: Text.WrapAnywhere
                                    }
                                    Label {
                                        text: detail.detailDescription
                                        textFormat: Text.PlainText
                                        visible: text.length > 0
                                        Layout.fillWidth: true
                                        font: Theme.font(12, Theme.regularWeight)
                                        color: Theme.floatingSecondaryText
                                        wrapMode: Text.WrapAnywhere
                                    }
                                    Label {
                                        text: detail.detailError
                                        textFormat: Text.PlainText
                                        visible: text.length > 0
                                        Layout.fillWidth: true
                                        font: Theme.font(12, Theme.regularWeight)
                                        color: Theme.floating.dangerText
                                        wrapMode: Text.WrapAnywhere
                                    }
                                }
                                Label {
                                    text: detail.detailStatus
                                    textFormat: Text.PlainText
                                    visible: text.length > 0
                                    font: Theme.font(12, Theme.regularWeight)
                                    color: Theme.floatingSecondaryText
                                }
                                Repeater {
                                    model: detail.detailActions
                                    delegate: TaskActionButton {
                                        required property var modelData
                                        descriptor: modelData
                                        taskId: task.taskId
                                        taskToken: task.taskToken
                                        detailId: detail.detailId
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        visible: task.taskError.length > 0
                        implicitHeight: taskErrorCopy.implicitHeight + 20
                        radius: Theme.controlRadius
                        color: Theme.withAlpha(Theme.colors.danger, 0.08)
                        RowLayout {
                            id: taskErrorCopy
                            x: 10
                            y: 10
                            width: parent.width - 20
                            spacing: 8
                            EchoIcon {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                                Layout.topMargin: 2
                                name: "alert-triangle"
                                color: Theme.floating.dangerText
                            }
                            EchoTextTagRow {
                                Layout.alignment: Qt.AlignTop
                                Layout.fillWidth: true
                                Layout.fillHeight: false
                                Layout.minimumWidth: 0
                                text: task.taskError
                                font: Theme.font(12, Theme.regularWeight)
                                color: Theme.floating.dangerText
                                lineHeight: 18
                                wrap: true
                            }
                        }
                    }
                    TaskActionRow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        descriptors: task.taskActions
                        taskId: task.taskId
                        taskToken: task.taskToken
                        dismissible: task.taskDismissible
                    }
                }
            }
        }
    }
    Connections {
        target: dialog.service
        function onClosePanelRequested() {
            dialog.close();
        }
    }
}
