pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme

Control {
    id: control
    EchoUiPart {
        id: formPart
        target: control
        role: "control.select"
        key: control.objectName
        properties: ["foreground", "secondaryForeground", "background", "borderColor", "borderWidth", "radius", "font", "fontSize", "fontWeight", "letterSpacing"]
    }
    property int size: EchoControlSize.Standard
    property ThemeColors colors: Theme.colors
    property var options: undefined
    property var value: undefined
    property bool multiple: false
    property bool filterable: false
    property bool clearable: false
    property bool wrapOptions: false
    property int maxTagCount: 1
    property string placeholder: qsTr("请选择")
    property string contentAlign: "end"
    property list<bool> availableOptions: []
    property string searchTerm: ""
    property bool popupActive: false
    property bool navigationActive: false
    // Selection remains caller-owned. activated(sourceIndex) reports the
    // original option index, never an index into the filtered view.
    signal valueEdited(var nextValue)
    readonly property var multiValues: multiple && value && typeof value !== "string" && value.length !== undefined ? Array.from(value) : []
    readonly property var selectedTags: projection.count >= 0 ? multiValues.map(v => projection.optionForValue(v)).filter(o => o.label !== undefined) : []
    readonly property var visibleTags: selectedTags.slice(0, Math.max(0, maxTagCount))
    readonly property int overflowCount: Math.max(0, selectedTags.length - Math.max(0, maxTagCount))
    readonly property bool showClear: clearable && hovered && (multiple ? multiValues.length > 0 : value !== undefined ? value !== "" : currentIndex >= 0 && currentValue !== "")
    readonly property color foreground: formPart.value("foreground", popupActive ? colors.accentText : colors.text)
    readonly property alias optionModel: projection
    implicitHeight: EchoControlSize.heightFor(size)
    implicitWidth: 160
    leftPadding: EchoControlSize.fieldPaddingFor(size)
    rightPadding: showClear ? leftPadding + (size === EchoControlSize.Small ? 20 : 24) : leftPadding + 22
    font: Theme.font(size === EchoControlSize.Large ? 15 : size === EchoControlSize.Standard ? 13 : 12, Theme.mediumWeight)
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    property alias currentIndex: projection.currentIndex
    readonly property string currentText: projection.count >= 0 ? (projection.optionAtSource(currentIndex).label || "") : ""
    readonly property var currentValue: projection.count >= 0 ? projection.optionAtSource(currentIndex).value : undefined
    property string displayText: currentText
    property var model: options === undefined ? [] : options
    property string textRole: options === undefined ? "" : "label"
    property string valueRole: options === undefined ? "" : "value"
    readonly property var popup: selectPopupOwner.item
    readonly property EchoBoundedListView optionList: selectPopupOwner.item ? selectPopupOwner.item.contentItem as EchoBoundedListView : null
    function openPopup(): void {
        selectPopupOwner.open();
    }
    function closePopup(): void {
        selectPopupOwner.close();
    }
    signal activated(int index)
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ComboBox
    Accessible.description: multiple ? selectedTags.map(t => t.label).join("、") : displayText || placeholder
    function syncValue(): void {
        if (!multiple && value !== undefined) {
            const option = projection.optionForValue(value);
            projection.setCurrentIndex(option.sourceIndex === undefined ? -1 : option.sourceIndex);
        }
    }
    onValueChanged: syncValue()
    onOptionsChanged: Qt.callLater(syncValue)
    onEnabledChanged: if (!enabled && !EchoPopupCoordinator.interactionSuspended(control.Window.window))
        closePopup()
    onVisibleChanged: if (!visible)
        closePopup()
    function choose(row: int): void {
        const option = projection.option(row);
        if (!enabled || option.disabled || option.sourceIndex === undefined)
            return;
        if (multiple) {
            const next = multiValues.slice();
            const position = next.indexOf(option.value);
            if (position < 0)
                next.push(option.value);
            else
                next.splice(position, 1);
            valueEdited(next);
        } else {
            projection.setCurrentIndex(option.sourceIndex);
            activated(option.sourceIndex);
            valueEdited(option.value);
            closePopup();
        }
        searchTerm = "";
    }
    function clearSelection(): void {
        if (!enabled)
            return;
        if (!multiple)
            projection.setCurrentIndex(-1);
        valueEdited(multiple ? [] : "");
        searchTerm = "";
    }
    function removeTag(valueToRemove: var): void {
        if (enabled)
            valueEdited(multiValues.filter(v => v !== valueToRemove));
    }
    function keyboard(event: KeyEvent): void {
        if (!enabled || searchInput.inputMethodComposing)
            return;
        if (event.key === Qt.Key_Escape) {
            closePopup();
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || (event.key === Qt.Key_Space && !filterable)) {
            if (popupActive && optionList.currentIndex >= 0)
                choose(optionList.currentIndex);
            else if (popupActive)
                closePopup();
            else
                openPopup();
            event.accepted = true;
        } else if ([Qt.Key_Down, Qt.Key_Up, Qt.Key_Home, Qt.Key_End].includes(event.key)) {
            if (!popupActive) {
                openPopup();
                if (!multiple)
                    optionList.currentIndex = projection.filteredIndex(currentIndex);
            }
            const forward = event.key === Qt.Key_Down || event.key === Qt.Key_Home;
            const initial = event.key === Qt.Key_Home ? -1 : event.key === Qt.Key_End ? projection.count : optionList.currentIndex < 0 ? (forward ? -1 : projection.count) : optionList.currentIndex;
            optionList.currentIndex = projection.nextEnabled(initial, forward ? 1 : -1);
            navigationActive = true;
            if (optionList.currentIndex >= 0)
                optionList.positionViewAtIndex(optionList.currentIndex, ListView.Contain);
            event.accepted = true;
        }
    }
    MouseArea {
        anchors.fill: parent
        z: -0.1
        onClicked: {
            control.forceActiveFocus(Qt.MouseFocusReason);
            control.popupActive ? control.closePopup() : control.openPopup();
        }
    }
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: event => keyboard(event)
    EchoSelectModel {
        id: projection
        source: control.model
        textRole: control.textRole
        valueRole: control.valueRole
        available: Array.from(control.availableOptions)
        query: control.filterable ? control.searchTerm : ""
        font: formPart.styledFont(control.font)
        onChanged: {
            control.navigationActive = false;
            if (control.optionList) {
                control.optionList.currentIndex = -1;
                control.optionList.positionViewAtBeginning();
            }
            Qt.callLater(control.syncValue);
        }
    }
    contentItem: RowLayout {
        spacing: 4
        clip: true
        Repeater {
            model: control.multiple ? control.visibleTags : []
            delegate: EchoTag {
                id: selectedTag
                required property var modelData
                size: EchoTag.Small
                tone: EchoTag.Accent
                colors: control.colors
                contentComponent: RowLayout {
                    spacing: 2
                    Text {
                        Layout.preferredWidth: Math.min(80, implicitWidth)
                        text: selectedTag.modelData.label
                        color: formPart.value("foreground", control.colors.accentText)
                        font: formPart.styledFont(Theme.font(11, Theme.semiboldWeight))
                        elide: Text.ElideRight
                    }
                    Button {
                        id: removeTagAction
                        objectName: "selectTagRemoveAction"
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        padding: 2
                        Accessible.name: qsTr("移除 %1").arg(selectedTag.modelData.label)
                        onClicked: control.removeTag(selectedTag.modelData.value)
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: removeTagAction.hovered ? Theme.withAlpha(control.colors.accent, 0.2) : "transparent"
                        }
                        contentItem: EchoIcon {
                            name: "x"
                            color: formPart.value("foreground", control.colors.accentText)
                        }
                    }
                }
            }
        }
        EchoTag {
            visible: control.multiple && control.overflowCount > 0
            size: EchoTag.Small
            tone: EchoTag.Muted
            colors: control.colors
            text: "+" + control.overflowCount
        }
        TextField {
            id: searchInput
            objectName: "selectSearchInput"
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            visible: control.filterable
            text: control.searchTerm
            padding: 0
            selectByMouse: true
            // 可过滤下拉统一不显示输入光标：点击展开时只过滤文本，不再闪烁。
            cursorVisible: false
            onActiveFocusChanged: cursorVisible = false
            font: formPart.styledFont(control.font)
            color: formPart.value("foreground", control.colors.text)
            placeholderText: control.multiple ? (control.selectedTags.length === 0 ? control.placeholder : "") : control.displayText || control.placeholder
            placeholderTextColor: formPart.value("secondaryForeground", control.colors.secondaryText)
            background: null
            Accessible.name: control.Accessible.name || qsTr("搜索选项")
            onTextEdited: {
                const query = text;
                if (!control.popupActive)
                    control.openPopup();
                control.searchTerm = query;
            }
            onPressed: if (!control.popupActive)
                control.openPopup()
            Keys.onPressed: event => control.keyboard(event)
        }
        EchoTextLine {
            id: triggerLabel
            Layout.fillWidth: true
            visible: !control.filterable && (!control.multiple || control.selectedTags.length === 0)
            text: control.multiple ? control.placeholder : control.displayText || control.placeholder
            color: (!control.multiple && control.displayText.length > 0) ? formPart.value("foreground", control.colors.text) : formPart.value("secondaryForeground", control.colors.secondaryText)
            font: formPart.styledFont(control.font)
            fillTextWidth: true
        }
    }
    TextMetrics {
        id: searchPlaceholderMetrics
        font: searchInput.font
        text: searchInput.placeholderText
    }
    EchoToolTip {
        parent: control
        text: control.multiple ? control.selectedTags.map(t => t.label).join("、") : control.displayText || control.placeholder
        requestedVisible: !control.popupActive && control.hovered && !control.searchTerm.length && (control.multiple ? control.overflowCount > 0 : control.filterable ? searchPlaceholderMetrics.advanceWidth > searchInput.width : triggerLabel.isTruncated())
    }
    Button {
        objectName: "selectIndicatorAction"
        x: control.width - width - 12
        y: (control.height - height) / 2
        width: control.showClear ? 16 : 14
        height: width
        padding: control.showClear ? 2 : 0
        Accessible.name: control.showClear ? qsTr("清空选择") : qsTr("展开选项")
        onClicked: control.showClear ? control.clearSelection() : control.popupActive ? control.closePopup() : control.openPopup()
        background: Rectangle {
            visible: control.showClear
            radius: Theme.controlRadius
            color: control.colors.hover
        }
        contentItem: EchoIcon {
            name: control.showClear ? "x" : "chevron-down"
            color: formPart.value("secondaryForeground", control.colors.secondaryText)
            rotation: !control.showClear && control.popupActive ? 180 : 0
            Behavior on rotation {
                NumberAnimation {
                    duration: Theme.fastDuration
                }
            }
        }
    }
    background: EchoActionBackground {
        publicPart: formPart
        colors: control.colors
        accent: control.popupActive
        highlighted: !control.popupActive && control.enabled && (control.hovered || control.visualFocus)
    }
    // ListView remains virtualized with variable-height wrapped delegates too;
    // never inherit the reference DOM's non-virtual wrapped-list limitation.
    EchoPopupLoader {
        id: selectPopupOwner
        hostItem: control
        sourceComponent: Component {
            EchoAnchoredPopup {
                id: selectPopup
                anchorItem: control
                uiScale: control.width > 0 ? anchorRect.width / control.width : 1
                preferredWidth: Math.min(480, Math.max(control.width, Math.ceil(projection.labelWidth) + 60))
                contentAlign: control.contentAlign
                sideOffset: 6
                maximumHeight: 332
                padding: 6
                // The search input is in the trigger, outside Popup's focus scope.
                // Taking focus during enter would swallow characters after the first.
                focus: !control.filterable
                modal: false
                // Tags, clear action and search belong to the same trigger interaction.
                // Keep them clickable while Qt still dismisses presses elsewhere.
                closePolicy: Popup.CloseOnEscape | (control.hovered ? Popup.NoAutoClose : Popup.CloseOnPressOutside)
                implicitHeight: optionList.implicitHeight + 12
                onAboutToShow: {
                    control.popupActive = true;
                    control.navigationActive = false;
                    control.searchTerm = "";
                    optionList.currentIndex = -1;
                    optionList.positionViewAtBeginning();
                }
                onOpened: if (control.filterable) {
                    searchInput.forceActiveFocus(Qt.PopupFocusReason);
                    searchInput.cursorVisible = false;
                }
                onAboutToHide: {
                    // Closing animation visibility is not an open interaction state.
                    // Restore input immediately so rapid keyboard reopening works.
                    control.popupActive = false;
                    control.searchTerm = "";
                    if (control.enabled && control.visible)
                        control.forceActiveFocus(Qt.PopupFocusReason);
                }
                onClosed: control.searchTerm = ""
                background: EchoFloatingSurface {
                    radius: Theme.popoverRadius
                    border.width: 1
                    border.color: Theme.floating.surfaceOutline
                }
                contentItem: EchoBoundedListView {
                    id: optionList
                    maximumViewportHeight: 320
                    emptyViewportHeight: 42
                    objectName: "selectOptionList"
                    focus: !control.filterable
                    clip: true
                    reuseItems: true
                    cacheBuffer: 76
                    spacing: 2
                    currentIndex: -1
                    model: selectPopup.visible ? projection : null
                    delegate: EchoSelectOption {
                        objectName: "selectOption-" + sourceIndex
                        required property string label
                        required property var optionValue
                        required property bool optionDisabled
                        required property int sourceIndex
                        required property int index
                        width: optionList.width
                        text: label
                        selected: control.multiple ? control.multiValues.includes(optionValue) : sourceIndex === control.currentIndex
                        navigationFocus: control.navigationActive && optionList.currentIndex === index
                        wrap: control.wrapOptions
                        enabled: control.enabled && !optionDisabled
                        onClicked: control.choose(index)
                    }
                    ScrollBar.vertical: EchoPopupScrollBar {
                        surface: selectPopup
                        view: optionList
                        resourceActive: selectPopup.visible
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: projection.count === 0
                        text: qsTr("无匹配项")
                        color: Theme.floating.secondaryText
                        font: formPart.styledFont(control.font)
                    }
                    Keys.onPressed: event => control.keyboard(event)
                }
            }
        }
    }
}
