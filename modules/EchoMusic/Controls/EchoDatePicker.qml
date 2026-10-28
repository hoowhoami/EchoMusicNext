pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme

Control {
    id: picker
    property int size: EchoControlSize.Standard
    property string value: ""
    property string minimum: "1900-01-01"
    property string maximum: Qt.formatDate(new Date(), "yyyy-MM-dd")
    property string placeholderText: qsTr("请选择日期")
    property string accessibleName: qsTr("日期")
    property bool clearable: true
    property real uiScale: 1
    property ThemeColors colors: Theme.colors
    signal picked(string date)
    font: Theme.font(size === EchoControlSize.Large ? 15 : size === EchoControlSize.Standard ? 13 : 12, Theme.mediumWeight)
    implicitHeight: EchoControlSize.heightFor(size)
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    Accessible.name: accessibleName
    readonly property date selectedDate: parse(value)
    readonly property bool hasDate: !isNaN(selectedDate.getTime())
    property date shownMonth: new Date(new Date().getFullYear(), new Date().getMonth(), 1, 12)
    property date focusedDate: new Date()
    property bool segmentDirty: false
    function parse(text: string): date {
        if (!/^\d{4}-\d{2}-\d{2}$/.test(text))
            return new Date(NaN);
        const parts = text.split("-");
        const date = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]), 12);
        return Qt.formatDate(date, "yyyy-MM-dd") === text ? date : new Date(NaN);
    }
    function allowed(date: date): bool {
        const text = Qt.formatDate(date, "yyyy-MM-dd");
        return !isNaN(date.getTime()) && text >= minimum && text <= maximum;
    }
    function commitSegments(): void {
        if (!segmentDirty)
            return;
        const date = new Date(Number(year.text), Number(month.text) - 1, Number(day.text), 12);
        if (date.getFullYear() === Number(year.text) && date.getMonth() + 1 === Number(month.text) && date.getDate() === Number(day.text) && allowed(date))
            picked(Qt.formatDate(date, "yyyy-MM-dd"));
    }
    function sync(): void {
        segmentDirty = false;
        // valueChanged may run before the derived hasDate/selectedDate bindings.
        // Read the new value directly so a restored birthday shows its own date.
        const selected = parse(value);
        const date = !isNaN(selected.getTime()) ? selected : new Date();
        year.text = String(date.getFullYear());
        month.text = String(date.getMonth() + 1).padStart(2, "0");
        day.text = String(date.getDate()).padStart(2, "0");
    }
    function changeMonth(delta: int): void {
        shownMonth = new Date(shownMonth.getFullYear(), shownMonth.getMonth() + delta, 1, 12);
    }
    function cellDate(index: int): date {
        const first = new Date(shownMonth.getFullYear(), shownMonth.getMonth(), 1, 12);
        return new Date(first.getFullYear(), first.getMonth(), 1 - first.getDay() + index, 12);
    }
    function moveFocus(delta: int): void {
        const next = new Date(focusedDate.getFullYear(), focusedDate.getMonth(), focusedDate.getDate() + delta, 12);
        if (!allowed(next))
            return;
        focusedDate = next;
        shownMonth = new Date(next.getFullYear(), next.getMonth(), 1, 12);
    }
    onValueChanged: sync()
    onEnabledChanged: if (!enabled && !EchoPopupCoordinator.interactionSuspended(picker.Window.window))
        calendarOwner.close()
    Component.onCompleted: sync()
    background: Rectangle {
        radius: Theme.controlRadius
        color: Theme.withAlpha(picker.colors.tone, picker.activeFocus ? 0.07 : 0.05)
    }
    contentItem: RowLayout {
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: implicitWidth
            // Report the editable date's natural width to the owning layout;
            // anchored children do not contribute an Item's implicit size.
            implicitWidth: Math.max(dateSegments.implicitWidth, placeholder.implicitWidth) + EchoControlSize.fieldPaddingFor(picker.size)
            implicitHeight: EchoControlSize.heightFor(picker.size)
            Label {
                id: placeholder
                anchors.left: parent.left
                anchors.leftMargin: EchoControlSize.fieldPaddingFor(picker.size)
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - EchoControlSize.fieldPaddingFor(picker.size)
                visible: !picker.hasDate && !segments.activeFocus
                elide: Text.ElideRight
                text: picker.placeholderText
                font: picker.font
                color: picker.colors.secondaryText
            }
            FocusScope {
                id: segments
                anchors.left: parent.left
                anchors.leftMargin: EchoControlSize.fieldPaddingFor(picker.size)
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - EchoControlSize.fieldPaddingFor(picker.size)
                height: 32
                opacity: picker.hasDate || activeFocus ? 1 : 0
                RowLayout {
                    id: dateSegments
                    spacing: 0
                    anchors.verticalCenter: parent.verticalCenter
                    TextField {
                        id: year
                        objectName: "dateYear"
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 28
                        padding: 2
                        color: picker.colors.text
                        font: picker.font
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator {
                            bottom: 1900
                            top: new Date().getFullYear()
                        }
                        background: Rectangle {
                            radius: Theme.detailRadius
                            color: year.activeFocus ? Theme.withAlpha(picker.colors.accent, 0.12) : "transparent"
                        }
                        onTextEdited: picker.segmentDirty = true
                        onEditingFinished: picker.commitSegments()
                        KeyNavigation.right: month
                        KeyNavigation.tab: month
                    }
                    Label {
                        text: "/"
                        color: picker.colors.text
                        font: Theme.font(13, Theme.regularWeight)
                    }
                    TextField {
                        id: month
                        objectName: "dateMonth"
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 28
                        padding: 2
                        color: picker.colors.text
                        font: picker.font
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator {
                            bottom: 1
                            top: 12
                        }
                        background: Rectangle {
                            radius: Theme.detailRadius
                            color: month.activeFocus ? Theme.withAlpha(picker.colors.accent, 0.12) : "transparent"
                        }
                        onTextEdited: picker.segmentDirty = true
                        onEditingFinished: picker.commitSegments()
                        KeyNavigation.left: year
                        KeyNavigation.right: day
                        KeyNavigation.tab: day
                    }
                    Label {
                        text: "/"
                        color: picker.colors.text
                        font: Theme.font(13, Theme.regularWeight)
                    }
                    TextField {
                        id: day
                        objectName: "dateDay"
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 28
                        padding: 2
                        color: picker.colors.text
                        font: picker.font
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator {
                            bottom: 1
                            top: 31
                        }
                        background: Rectangle {
                            radius: Theme.detailRadius
                            color: day.activeFocus ? Theme.withAlpha(picker.colors.accent, 0.12) : "transparent"
                        }
                        onTextEdited: picker.segmentDirty = true
                        onEditingFinished: picker.commitSegments()
                        KeyNavigation.left: month
                    }
                }
            }
        }
        EchoIconButton {
            objectName: "dateClear"
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            visible: picker.clearable && picker.hasDate
            glyph: "x"
            text: qsTr("清除日期")
            colors: picker.colors
            onClicked: picker.picked("")
        }
        EchoIconButton {
            objectName: "dateCalendar"
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.rightMargin: 8
            glyph: "calendar"
            text: qsTr("打开日期选择器")
            colors: picker.colors
            onClicked: calendarOwner.open()
        }
    }
    EchoPopupLoader {
        id: calendarOwner
        objectName: "datePickerCalendarOwner"
        hostItem: picker
        sourceComponent: Component {
            EchoAnchoredPopup {
                id: calendar
                objectName: "datePickerCalendar"
                anchorItem: picker
                alignEnd: false
                sideOffset: 7
                showArrow: false
                preferredWidth: 304
                uiScale: picker.uiScale
                maximumHeight: 370
                padding: 12
                onOpened: {
                    picker.focusedDate = picker.hasDate ? picker.selectedDate : new Date();
                    picker.shownMonth = new Date(picker.focusedDate.getFullYear(), picker.focusedDate.getMonth(), 1, 12);
                    grid.forceActiveFocus();
                }
                contentItem: ColumnLayout {
                    spacing: 8
                    RowLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        spacing: 2
                        EchoIconButton {
                            glyph: "chevrons-left"
                            text: qsTr("上一年")
                            implicitWidth: 34
                            implicitHeight: 34
                            colors: Theme.floating
                            enabled: Qt.formatDate(new Date(picker.shownMonth.getFullYear() - 1, picker.shownMonth.getMonth() + 1, 0), "yyyy-MM-dd") >= picker.minimum
                            onClicked: picker.changeMonth(-12)
                        }
                        EchoIconButton {
                            glyph: "chevron-left"
                            text: qsTr("上个月")
                            implicitWidth: 34
                            implicitHeight: 34
                            colors: Theme.floating
                            enabled: Qt.formatDate(new Date(picker.shownMonth.getFullYear(), picker.shownMonth.getMonth(), 0), "yyyy-MM-dd") >= picker.minimum
                            onClicked: picker.changeMonth(-1)
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: Qt.formatDate(picker.shownMonth, "yyyy年M月")
                            color: Theme.floatingText
                            font: Theme.font(13, Theme.extraBoldWeight)
                        }
                        EchoIconButton {
                            glyph: "chevron-right"
                            text: qsTr("下个月")
                            implicitWidth: 34
                            implicitHeight: 34
                            colors: Theme.floating
                            enabled: Qt.formatDate(new Date(picker.shownMonth.getFullYear(), picker.shownMonth.getMonth() + 1, 1), "yyyy-MM-dd") <= picker.maximum
                            onClicked: picker.changeMonth(1)
                        }
                        EchoIconButton {
                            glyph: "chevrons-right"
                            text: qsTr("下一年")
                            implicitWidth: 34
                            implicitHeight: 34
                            colors: Theme.floating
                            enabled: Qt.formatDate(new Date(picker.shownMonth.getFullYear() + 1, picker.shownMonth.getMonth(), 1), "yyyy-MM-dd") <= picker.maximum
                            onClicked: picker.changeMonth(12)
                        }
                    }
                    Row {
                        Layout.fillWidth: true
                        Repeater {
                            model: [qsTr("日"), qsTr("一"), qsTr("二"), qsTr("三"), qsTr("四"), qsTr("五"), qsTr("六")]
                            delegate: Label {
                                required property string modelData
                                width: (calendar.width - 24) / 7
                                height: 30
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData
                                color: Theme.floatingSecondaryText
                                font: Theme.font(10, Theme.extraBoldWeight)
                            }
                        }
                    }
                    Grid {
                        id: grid
                        Layout.fillWidth: true
                        columns: 7
                        spacing: 2
                        // Calendar is a bounded 42-cell surface, never a catalog list.
                        Repeater {
                            model: 42
                            delegate: Button {
                                id: cell
                                required property int index
                                readonly property date date: picker.cellDate(index)
                                readonly property string iso: Qt.formatDate(date, "yyyy-MM-dd")
                                width: (calendar.width - 24 - 12) / 7
                                height: 34
                                padding: 0
                                enabled: picker.allowed(date)
                                opacity: !enabled ? 0.25 : date.getMonth() !== picker.shownMonth.getMonth() ? 0.3 : 1
                                Accessible.name: iso
                                highlighted: iso === picker.value
                                contentItem: Label {
                                    text: cell.date.getDate()
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    color: cell.highlighted ? "#000000" : Theme.floatingText
                                    font: Theme.font(12, Theme.boldWeight)
                                }
                                background: Rectangle {
                                    radius: Theme.controlRadius
                                    color: cell.highlighted ? Theme.accent : cell.hovered ? Theme.floating.hover : cell.iso === Qt.formatDate(new Date(), "yyyy-MM-dd") ? Theme.withAlpha(Theme.accent, 0.16) : "transparent"
                                    border.width: !cell.highlighted && (cell.iso === Qt.formatDate(picker.focusedDate, "yyyy-MM-dd") || cell.iso === Qt.formatDate(new Date(), "yyyy-MM-dd")) ? 1 : 0
                                    border.color: Theme.withAlpha(Theme.accent, 0.72)
                                }
                                onClicked: {
                                    picker.picked(iso);
                                    calendar.close();
                                }
                            }
                        }
                        Keys.onLeftPressed: picker.moveFocus(-1)
                        Keys.onRightPressed: picker.moveFocus(1)
                        Keys.onUpPressed: picker.moveFocus(-7)
                        Keys.onDownPressed: picker.moveFocus(7)
                        Keys.onReturnPressed: if (picker.allowed(picker.focusedDate)) {
                            picker.picked(Qt.formatDate(picker.focusedDate, "yyyy-MM-dd"));
                            calendar.close();
                        }
                        Keys.onEnterPressed: if (picker.allowed(picker.focusedDate)) {
                            picker.picked(Qt.formatDate(picker.focusedDate, "yyyy-MM-dd"));
                            calendar.close();
                        }
                    }
                }
            }
        }
    }
}
