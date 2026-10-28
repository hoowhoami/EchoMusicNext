pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Search
import EchoMusic.Shell

Rectangle {
    id: box
    required property SearchModel service
    required property RouteController router
    required property Item titlebarSurface
    required property Window window
    required property real uiScale
    property int activeSuggestion: -1
    implicitHeight: 30
    radius: Theme.controlRadius
    color: input.activeFocus ? Theme.fieldFocus : Theme.mutedSurface
    function dismissSearch() {
        if (panelOwner.item)
            panelOwner.close();
        if (input)
            input.focus = false;
    }
    // Explicit login dismisses search. A temporary verification cover keeps
    // the same discovery/suggestion surface and query until it is dismissed.
    onEnabledChanged: {
        if (!enabled && !EchoPopupCoordinator.interactionSuspended(box.window))
            dismissSearch();
    }
    onVisibleChanged: {
        if (!visible)
            dismissSearch();
    }
    function focusSearch() {
        input.forceActiveFocus();
        service.suggestions.setQuery(input.text);
        panelOwner.open();
    }
    function syncQuery() {
        input.text = router.current && router.current.name === "search" ? (router.current.query.q || "") : "";
    }
    function dismissIfFocusOutside() {
        if (!panelOwner.visible || EchoPopupCoordinator.preservingFocus(box.window))
            return;
        // Cocoa may temporarily have no active item while delivering a native
        // focus event. Only a concrete outside target means the user left;
        // clearing the editor here would cancel the ongoing keyboard entry.
        if (!window.activeFocusItem)
            return;
        for (let item = window.activeFocusItem; item; item = item.parent) {
            if (item === box || item === panelOwner.item.contentItem)
                return;
        }
        panelOwner.close();
    }
    function submit(keyword: string) {
        if (input.inputMethodComposing)
            return;
        const term = keyword.trim() || service.suggestions.defaultKeyword;
        if (!term.length)
            return;
        input.text = term;
        panelOwner.close();
        input.focus = false;
        if (router.current && router.current.name === "search" && router.current.query.q === term)
            router.refresh();
        else
            router.navigate("search", {
                q: term
            });
    }
    function highlight(text: string): string {
        function escape(value) {
            return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        }
        const query = input.text.trim();
        const position = query.length ? text.toLowerCase().indexOf(query.toLowerCase()) : -1;
        if (position < 0)
            return escape(text);
        return escape(text.slice(0, position)) + '<font color="' + Theme.floating.accentText + '">' + escape(text.slice(position, position + query.length)) + '</font>' + escape(text.slice(position + query.length));
    }
    function choose(delta: int) {
        if (input.inputMethodComposing || service.suggestions.loading)
            return;
        const count = service.suggestions.rowCount();
        if (!count)
            return;
        activeSuggestion = activeSuggestion < 0 ? delta > 0 ? 0 : count - 1 : (activeSuggestion + delta + count) % count;
        panelOwner.open();
        if (panelOwner.item)
            panelOwner.item["revealSuggestion"](activeSuggestion);
    }
    Component.onCompleted: syncQuery()
    Connections {
        target: box.window
        function onActiveFocusItemChanged() {
            // Wait for Qt to finish moving focus. A recommendation/history
            // action inside the popup must not dismiss itself before release.
            Qt.callLater(box.dismissIfFocusOutside);
        }
    }
    Connections {
        target: EchoPopupCoordinator
        function onPopupOpening(surface, overlay) {
            if (surface !== panelOwner.item && overlay === box.Overlay.overlay && !EchoPopupCoordinator.interactionSuspended(box.window)) {
                box.dismissSearch();
            }
        }
        function onNativeMenuOpening() {
            box.dismissSearch();
        }
    }
    Connections {
        target: box.router
        function onRouteChanged() {
            box.syncQuery();
        }
        function onDismissOverlays() {
            box.dismissSearch();
        }
    }
    Connections {
        target: box.service.suggestions
        function onChanged() {
            box.activeSuggestion = -1;
        }
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 4
        spacing: 5
        EchoIcon {
            name: "search"
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            color: Theme.iconSecondary
            TapHandler {
                onTapped: box.focusSearch()
            }
        }
        TextField {
            id: input
            objectName: "searchInput"
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            font: Theme.font(13, Theme.mediumWeight)
            color: Theme.text
            placeholderText: box.service.suggestions.defaultKeyword || qsTr("搜索音乐、歌手、专辑")
            placeholderTextColor: Theme.secondaryText
            selectionColor: Theme.accent
            background: null
            Accessible.role: Accessible.ComboBox
            Accessible.name: qsTr("搜索音乐、歌手、专辑")
            onActiveFocusChanged: {
                if (activeFocus && (focusReason === Qt.TabFocusReason || focusReason === Qt.BacktabFocusReason))
                    box.focusSearch();
            }
            onFocusReasonChanged: {
                // Qt can restore the editor as the active item after a popup.
                // A subsequent deliberate keyboard entry still opens search.
                if (activeFocus && (focusReason === Qt.TabFocusReason || focusReason === Qt.BacktabFocusReason))
                    box.focusSearch();
            }
            // TextField's own signal preserves native cursor/selection and IME;
            // focus alone does not fire after closing an already-focused field.
            onPressed: event => {
                if (event.button === Qt.LeftButton)
                    box.focusSearch();
            }
            onTextEdited: {
                box.activeSuggestion = -1;
                box.service.suggestions.setQuery(text);
                panelOwner.open();
            }
            Keys.onPressed: event => {
                if (input.inputMethodComposing)
                    return;
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                    box.choose(event.key === Qt.Key_Down ? 1 : -1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Escape) {
                    panelOwner.close();
                    input.focus = false;
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    box.submit(box.activeSuggestion >= 0 && panelOwner.opened ? box.service.suggestions.textAt(box.activeSuggestion) : text);
                    event.accepted = true;
                }
            }
        }
        EchoIconButton {
            glyph: "x"
            text: qsTr("清除搜索")
            visible: input.text.length > 0
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            padding: 5
            focusPolicy: Qt.NoFocus
            onClicked: {
                input.clear();
                box.service.suggestions.setQuery("");
                box.focusSearch();
            }
        }
    }
    EchoPopupLoader {
        id: panelOwner
        objectName: "searchSuggestionsPopupOwner"
        hostItem: box
        sourceComponent: Component {
            Popup {
                id: panel
                function revealSuggestion(index: int): void {
                    suggestionList.positionViewAtIndex(index, ListView.Contain);
                }
                objectName: "searchSuggestionsPopup"
                // Popup geometry uses the window overlay's coordinates. Mixing the
                // search-box local origin with overlay positioning clips wide discovery.
                parent: box.Overlay.overlay
                focus: false
                modal: false
                scale: box.uiScale
                transformOrigin: Popup.TopLeft
                readonly property bool hasQuery: input.text.trim().length > 0
                readonly property point searchOrigin: {
                    // mapToItem does not track transforms or the overlay's initial
                    // geometry correction. Include those binding dependencies explicitly.
                    const viewport = box.window.width + box.uiScale + box.titlebarSurface.width + box.x + panel.parent.x + panel.parent.y;
                    return box.mapToItem(panel.parent, 0, box.height + viewport * 0);
                }
                readonly property point titlebarOrigin: {
                    const viewport = box.window.width + box.uiScale + box.titlebarSurface.width + panel.parent.x + panel.parent.y;
                    return box.titlebarSurface.mapToItem(panel.parent, 0, box.titlebarSurface.height + viewport * 0);
                }
                width: Math.max(0, Math.min(hasQuery ? 440 : box.titlebarSurface.width - 24, box.window.width / box.uiScale - 24))
                x: Math.max(12 * box.uiScale, Math.min(hasQuery ? searchOrigin.x : titlebarOrigin.x + 12 * box.uiScale, box.window.width - 12 * box.uiScale - width * box.uiScale))
                y: (hasQuery ? searchOrigin.y : titlebarOrigin.y) + 8 * box.uiScale
                height: Math.max(0, Math.min(520, (box.window.height - y) / box.uiScale - 12, hasQuery ? box.service.suggestions.loading ? 188 : box.service.suggestions.rowCount() ? suggestionList.implicitHeight + 38 : 118 : discoveryColumn.implicitHeight + 16))
                padding: 0
                topPadding: hasQuery ? 0 : 8
                bottomPadding: topPadding
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                EchoPopupLifecycle {
                    surface: panel
                }
                onOpened: {
                    box.service.setDiscoveryActive(!hasQuery);
                    box.service.suggestions.setActive(true);
                }
                onClosed: {
                    box.service.setDiscoveryActive(false);
                    box.service.suggestions.setActive(false);
                    box.activeSuggestion = -1;
                    input.focus = false;
                }
                onHasQueryChanged: box.service.setDiscoveryActive(opened && !hasQuery)
                background: EchoFloatingSurface {
                    color: Theme.elevated
                    radius: Theme.cardRadius
                    border.width: 1
                    border.color: Theme.floating.subtleBorder
                }
                contentItem: Item {
                    clip: true
                    Flickable {
                        id: discoveryScroll
                        anchors.fill: parent
                        visible: !panel.hasQuery
                        contentWidth: width
                        contentHeight: discoveryColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: EchoPopupScrollBar {
                            surface: panel
                            view: discoveryScroll
                            resourceActive: panel.opened && discoveryScroll.visible
                        }
                        ColumnLayout {
                            id: discoveryColumn
                            x: 20
                            y: 14
                            width: discoveryScroll.width - 40
                            spacing: 12
                            SearchDiscovery {
                                Layout.fillWidth: true
                                service: box.service
                                pageActive: panel.opened && !panel.hasQuery
                                onKeywordPicked: keyword => box.submit(keyword)
                                // History edits retain the titlebar's keyboard owner;
                                // the page uses the same command without this focus rule.
                                onHistoryEdited: box.focusSearch()
                            }
                            Text {
                                visible: box.service.suggestions.promotions.length > 0
                                text: qsTr("推荐搜索")
                                color: Theme.floating.secondaryText
                                font: Theme.font(12, Theme.semiboldWeight)
                            }
                            Flow {
                                id: ads

                                Layout.fillWidth: true
                                spacing: 8
                                Repeater {
                                    model: box.service.suggestions.promotions
                                    delegate: EchoSoftButton {
                                        colors: Theme.floating
                                        required property string modelData
                                        text: modelData
                                        implicitHeight: 32
                                        focusPolicy: Qt.NoFocus
                                        onClicked: box.submit(modelData)
                                    }
                                }
                            }
                            Item {
                                Layout.preferredHeight: 20
                            }
                        }
                    }
                    EchoBoundedListView {
                        id: suggestionList
                        maximumViewportHeight: Math.max(0, Math.min(482, (box.window.height - panel.y) / box.uiScale - 50))
                        objectName: "searchSuggestionList"
                        anchors.fill: parent
                        anchors.bottomMargin: 38
                        visible: panel.hasQuery && !box.service.suggestions.loading && count > 0
                        model: panel.opened ? box.service.suggestions : null
                        clip: true
                        reuseItems: true
                        cacheBuffer: 120
                        ScrollBar.vertical: EchoPopupScrollBar {
                            surface: panel
                            view: suggestionList
                            resourceActive: panel.opened && suggestionList.visible
                        }
                        delegate: Item {
                            id: suggestion
                            required property int index
                            required property string suggestionText
                            required property string groupTitle
                            required property bool groupStart
                            width: suggestionList.width
                            height: 36 + (groupStart ? 33 : 0)
                            Text {
                                visible: suggestion.groupStart
                                x: 18
                                y: 10
                                textFormat: Text.PlainText
                                text: suggestion.groupTitle
                                color: Theme.floating.secondaryText
                                font: Theme.font(11, Theme.regularWeight)
                            }
                            Button {
                                id: choice
                                x: 8
                                y: suggestion.groupStart ? 33 : 0
                                width: parent.width - 16
                                height: 36
                                padding: 10
                                hoverEnabled: true
                                focusPolicy: Qt.NoFocus
                                Accessible.name: suggestion.suggestionText
                                Accessible.role: Accessible.ListItem
                                Accessible.selected: box.activeSuggestion === suggestion.index
                                contentItem: Text {
                                    text: box.highlight(suggestion.suggestionText)
                                    textFormat: Text.StyledText
                                    color: Theme.floating.text
                                    font: Theme.font(13, Theme.regularWeight)
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: Theme.controlRadius
                                    color: box.activeSuggestion === suggestion.index ? Theme.floating.selected : choice.hovered ? Theme.floating.hover : "transparent"
                                }
                                onHoveredChanged: {
                                    if (hovered)
                                        box.activeSuggestion = suggestion.index;
                                }
                                onClicked: box.submit(suggestion.suggestionText)
                            }
                        }
                    }
                    Column {
                        x: 16
                        y: 16
                        width: parent.width - 32
                        spacing: 12
                        visible: panel.hasQuery && box.service.suggestions.loading
                        Text {
                            text: qsTr("正在查找建议…")
                            color: Theme.floating.secondaryText
                            font: Theme.font(13, Theme.regularWeight)
                        }
                        Repeater {
                            model: 3
                            EchoSkeleton {
                                width: parent.width
                                height: 24
                                animate: panel.opened
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -19
                        visible: panel.hasQuery && !box.service.suggestions.loading && suggestionList.count === 0
                        text: qsTr("暂无相关建议")
                        color: Theme.floating.secondaryText
                        font: Theme.font(13, Theme.regularWeight)
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 38
                        visible: panel.hasQuery
                        color: Theme.elevated
                        Rectangle {
                            width: parent.width
                            height: 0.5
                            color: Theme.floating.subtleBorder
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12
                            Button {
                                Layout.fillWidth: true
                                padding: 0
                                focusPolicy: Qt.NoFocus
                                Accessible.name: qsTr("搜索%1").arg(input.text.trim())
                                contentItem: Text {
                                    textFormat: Text.PlainText
                                    text: qsTr("搜索“%1”").arg(input.text.trim())
                                    color: Theme.floating.accentText
                                    font: Theme.font(12, Theme.regularWeight)
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: null
                                onClicked: box.submit(input.text)
                            }
                            Text {
                                text: qsTr("↑↓ 选择 · Enter 确认")
                                color: Theme.floating.secondaryText
                                font: Theme.font(11, Theme.regularWeight)
                            }
                        }
                    }
                }
            }
        }
    }
}
