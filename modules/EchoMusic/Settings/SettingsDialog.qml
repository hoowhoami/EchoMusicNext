pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Playback
import EchoMusic.Plugins
import EchoMusic.Settings

Popup {
    id: dialog
    EchoPopupLifecycle {
        surface: dialog
    }
    objectName: "settingsDialog"
    required property ShellSettings settings
    required property WindowController windowService
    property PluginManager pluginManager: null
    property RouteController router: null
    property PlaybackService playback: null
    property NetworkPreferences network: null
    property LogService logging: null
    property RelaunchService relaunch: null
    property ShortcutService shortcuts: null
    property AboutService about: null
    property ExperimentalSettingsService experimental: null
    property SettingsDataService dataService: null
    readonly property SettingsCatalog catalog: navigation
    property bool searchExpanded: false
    property string initialSection: ""
    onInitialSectionChanged: {
        if (opened && initialSection.length) {
            navigation.select(initialSection);
            initialSection = "";
        }
    }
    function openSection(section) {
        initialSection = section;
        if (!opened)
            open();
    }
    readonly property real uiScale: settings.uiScale
    readonly property real overlayAvailableWidth: parent ? parent.width / uiScale : 820
    readonly property real overlayAvailableHeight: parent ? parent.height / uiScale : 620
    // Popup items live in the unscaled overlay; center the scaled item once.
    parent: Overlay.overlay
    scale: uiScale
    transformOrigin: Popup.TopLeft
    width: Math.min(820, Math.max(0, overlayAvailableWidth - (overlayAvailableWidth <= 640 ? 16 : 48)))
    height: Math.min(620, Math.max(0, overlayAvailableHeight - (overlayAvailableWidth <= 640 ? 32 : 96)))
    x: parent ? (parent.width - width * uiScale) / 2 : 0
    y: parent ? (parent.height - height * uiScale) / 2 : 0
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        search.clear();
        navigation.reset();
        searchExpanded = false;
        if (initialSection.length) {
            navigation.select(initialSection);
            initialSection = "";
        }
    }
    onClosed: {
        search.clear();
        searchExpanded = false;
        navigation.reset();
    }
    background: EchoFloatingSurface {
        radius: Theme.dialogRadius
        border.color: Theme.floating.surfaceOutline
        dialogShadow: true
    }
    Overlay.modal: Rectangle {
        color: Theme.dark ? "#5c000000" : "#4d000000"
    }
    SettingsCatalog {
        id: navigation
        playbackAvailable: dialog.playback !== null
    }
    Connections {
        target: navigation
        function onChanged() {
            content.cancelFlick();
            content.contentY = 0;
        }
    }
    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10
            Label {
                text: qsTr("偏好设置")
                font: Theme.font(18, Theme.boldWeight)
                color: Theme.floatingText
            }
            Label {
                visible: dialog.width > 640
                text: qsTr("全局设置")
                font: Theme.font(11, Theme.regularWeight)
                color: Theme.floatingSecondaryText
            }
            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            EchoIconButton {
                visible: !dialog.searchExpanded
                objectName: "settingsSearchTrigger"
                glyph: "search"
                text: qsTr("搜索设置")
                tooltipText: ""
                colors: Theme.floating
                implicitWidth: 30
                implicitHeight: 30
                padding: 6
                onClicked: {
                    dialog.searchExpanded = true;
                    search.forceActiveFocus();
                }
            }
            EchoTextField {
                id: search
                objectName: "settingsSearchInput"
                clearable: true
                visible: dialog.searchExpanded
                Layout.preferredWidth: Math.min(236, dialog.width / 3)
                Layout.minimumWidth: 0
                implicitHeight: 30
                font: Theme.font(12, Theme.mediumWeight)
                colors: Theme.floating
                placeholderText: qsTr("搜索设置")
                Accessible.name: qsTr("搜索设置")
                onTextChanged: navigation.filter = text
                onActiveFocusChanged: {
                    if (!activeFocus && !text.trim().length)
                        dialog.searchExpanded = false;
                }
                Keys.onEscapePressed: event => {
                    text = "";
                    dialog.searchExpanded = false;
                    event.accepted = true;
                }
            }
            EchoIconButton {
                objectName: "settingsClose"
                glyph: "x"
                text: qsTr("关闭设置")
                tooltipText: ""
                colors: Theme.floating
                implicitWidth: 30
                implicitHeight: 30
                padding: 6
                onClicked: dialog.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: dialog.width <= 640 ? 8 : 16
            Layout.topMargin: 4
            Layout.bottomMargin: dialog.width <= 640 ? 10 : 16
            spacing: 0
            Item {
                Layout.preferredWidth: dialog.width <= 640 ? 120 : 188
                Layout.fillHeight: true
                ListView {
                    id: categories
                    objectName: "settingsCategories"
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 16
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    clip: true
                    model: navigation
                    reuseItems: true
                    EchoViewPoolGuard {
                        targetView: categories
                    }
                    cacheBuffer: 41 * 2
                    spacing: 3
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Button {
                        id: entry
                        required property string sectionKey
                        required property string sectionLabel
                        required property string sectionGlyph
                        required property bool sectionAvailable
                        width: categories.width
                        height: 38
                        leftPadding: dialog.width <= 640 ? 8 : 12
                        rightPadding: leftPadding
                        enabled: sectionAvailable
                        hoverEnabled: true
                        Accessible.name: sectionLabel
                        onClicked: navigation.select(sectionKey)
                        contentItem: EchoButtonContent {
                            glyph: entry.sectionGlyph
                            text: entry.sectionLabel
                            glyphSize: 16
                            spacing: dialog.width <= 640 ? 6 : 10
                            horizontalAlignment: Qt.AlignLeft
                            color: navigation.current === entry.sectionKey ? Theme.floatingAccentText : entry.hovered ? Theme.floatingText : Theme.floatingSecondaryText
                            font: Theme.font(dialog.width <= 640 ? 12 : 13, Theme.semiboldWeight)
                            opacity: entry.enabled ? 1 : 0.35
                        }
                        background: Rectangle {
                            radius: Theme.itemRadius
                            color: navigation.current === entry.sectionKey ? Theme.withAlpha(Theme.accent, 0.14) : entry.hovered && entry.enabled ? Theme.floating.hover : "transparent"
                        }
                    }
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.floating.subtleBorder
                }
            }
            Flickable {
                id: content
                objectName: "settingsContentViewport"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: Math.max(height, loader.y + loader.height + 20)
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.vertical: EchoPopupScrollBar {
                    surface: dialog
                    view: content
                    resourceActive: dialog.visible
                }
                Loader {
                    id: loader
                    objectName: "settingsSectionLoader"
                    x: 18
                    y: 14
                    width: Math.max(0, content.width - 34)
                    height: item ? (item as Item).implicitHeight : 0
                    asynchronous: true
                    // Keep asynchronous creation, but present the complete
                    // section at once instead of exposing rows across frames.
                    visible: status === Loader.Ready
                    active: dialog.visible && navigation.source.toString().length > 0
                    // Inject typed service references before bindings evaluate.
                    // No section QML is instantiated until selected and visible.
                    onActiveChanged: reload()
                    Connections {
                        target: navigation
                        function onChanged() {
                            loader.reload();
                        }
                    }
                    function reload() {
                        const targetSource = active ? navigation.source : "";
                        // Navigation/filter notifications and Loader activation
                        // can arrive in the same frame. Reapplying setSource to
                        // an unchanged URL recreates even a loaded section and
                        // discards focus/drafts during unrelated notifications.
                        if (source.toString() === targetSource.toString())
                            return;
                        const properties = {
                            settings: dialog.settings,
                            windowService: dialog.windowService
                        };
                        if (["lyric", "audioDevice", "spatialAudio", "playback", "player", "cast"].indexOf(navigation.current) >= 0)
                            properties.playback = dialog.playback;
                        if (navigation.current === "cast")
                            properties.surface = dialog;
                        if (navigation.current === "plugins") {
                            properties.pluginManager = dialog.pluginManager;
                            properties.router = dialog.router;
                        }
                        if (navigation.current === "network")
                            properties.network = dialog.network;
                        if (navigation.current === "shortcuts")
                            properties.shortcuts = dialog.shortcuts;
                        if (navigation.current === "about")
                            properties.about = dialog.about;
                        if (navigation.current === "data") {
                            properties.dataService = dialog.dataService;
                            properties.logging = dialog.logging;
                        }
                        if (navigation.current === "experimental") {
                            properties.logging = dialog.logging;
                            properties.relaunch = dialog.relaunch;
                            properties.experimental = dialog.experimental;
                        }
                        setSource(targetSource, properties);
                    }
                    Component.onCompleted: reload()
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: navigation.count === 0
                    spacing: 12
                    EchoIcon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "search"
                        color: Theme.floatingSecondaryText
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                    }
                    Label {
                        text: qsTr("没有找到匹配的设置")
                        font: Theme.font(13, Theme.mediumWeight)
                        color: Theme.floatingSecondaryText
                    }
                    EchoSoftButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("清空搜索")
                        colors: Theme.floating
                        onClicked: search.clear()
                    }
                }
            }
        }
    }
}
