pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.PluginSdk
import EchoMusic.Plugins

Item {
    id: page
    required property PluginManager manager
    required property RouteController router
    required property RouteContext context
    required property ShellSettings settings
    readonly property bool compactLayout: width <= 720
    // loaded tracks the network pass, while catalogCount includes disk cache
    // before filtering. A refresh must not insert placeholders above that cache.
    readonly property bool initialLoading: marketplace && manager.catalogLoading && !manager.loaded && manager.catalogCount === 0
    readonly property bool marketplace: context.query.view === "marketplace"
    property string uninstallId: ""
    property string failureId: ""
    property string settingsId: ""
    property EchoPluginContext settingsScope: null
    objectName: "pluginManagementPage"
    function openMarketplace() {
        if (marketplace && context.active)
            manager.refreshCatalog();
    }
    function closeDialogs() {
        sourcesOwner.close();
        uninstallOwner.close();
        failureOwner.close();
        pluginSettingsOwner.close();
        grantsOwner.close();
        dataOwner.close();
        packageFileOwner.close();
        settingsScope = null;
    }
    Component.onDestruction: closeDialogs()
    onMarketplaceChanged: openMarketplace()
    Component.onCompleted: openMarketplace()
    Connections {
        target: page.context
        function onActiveChanged() {
            if (!page.context.active)
                page.closeDialogs();
            else
                page.openMarketplace();
        }
    }
    Connections {
        target: page.manager.runtime
        function onCatalogChanged() {
            if (page.settingsScope && !page.settingsScope.active) {
                pluginSettingsOwner.close();
                page.settingsScope = null;
            }
            if (page.uninstallId && !page.manager.runtime.record(page.uninstallId).installationId)
                uninstallOwner.close();
        }
        function onManagementFinished(installation, success, error) {
            if (installation === page.uninstallId) {
                uninstallOwner.close();
                page.uninstallId = "";
            }
        }
    }
    EchoBackToTop {
        anchors.fill: parent
        z: 50
        scrollView: list
        viewActive: page.context.active
    }
    PluginGridModel {
        id: rows
        source: page.marketplace ? page.manager.catalog : page.manager.runtime
        columns: Math.max(1, Math.floor((page.width - 48 + 14) / 314))
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.fillWidth: true
            implicitHeight: header.implicitHeight + 28
            ColumnLayout {
                id: header
                x: 24
                y: 16
                width: parent.width - 48
                spacing: 16
                // Fixed icon/actions columns around the expanding title/path column.
                RowLayout {
                    Layout.fillHeight: false
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: Theme.controlRadius
                        color: Theme.accent
                        EchoIcon {
                            anchors.centerIn: parent
                            objectName: "pluginHeaderIcon"
                            name: "puzzle"
                            width: 20
                            height: 20
                            color: Theme.accentForeground
                        }
                    }
                    ColumnLayout {
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        Label {
                            text: qsTr("插件管理")
                            font: Theme.font(18, Theme.boldWeight)
                            Layout.preferredHeight: 27
                            color: Theme.text
                        }
                        Label {
                            Layout.fillWidth: true
                            objectName: "pluginDirectory"
                            text: page.manager.runtime.directory
                            font: Theme.font(12, Theme.regularWeight)
                            Layout.preferredHeight: 18
                            color: Theme.secondaryText
                            elide: Text.ElideRight
                        }
                    }
                    RowLayout {
                        objectName: "pluginHeaderActions"
                        // Keep actions fixed-width; only the title and path column grows.
                        Layout.fillWidth: false
                        Layout.fillHeight: false
                        spacing: 8
                        PluginActionButton {
                            objectName: "pluginInstallPackage"
                            iconOnly: true
                            text: qsTr("安装插件")
                            tooltipText: text
                            glyph: "plus"
                            enabled: !page.manager.busy && !page.manager.runtime.loading
                            onClicked: packageFileOwner.open()
                        }
                        PluginActionButton {
                            objectName: "pluginDataManagement"
                            iconOnly: true
                            glyph: "database"
                            text: qsTr("插件数据")
                            tooltipText: text
                            onClicked: dataOwner.open()
                        }
                        PluginActionButton {
                            iconOnly: true
                            glyph: "lock-open"
                            text: qsTr("插件授权")
                            tooltipText: text
                            implicitWidth: 36
                            implicitHeight: 36
                            onClicked: grantsOwner.open()
                        }
                        PluginActionButton {
                            iconOnly: true
                            glyph: "folder-open"
                            text: qsTr("打开插件目录")
                            implicitWidth: 36
                            implicitHeight: 36
                            onClicked: page.manager.openDirectory()
                        }
                        PluginActionButton {
                            iconOnly: true
                            objectName: "pluginRefresh"
                            glyph: "refresh"
                            loading: page.manager.catalogLoading || page.manager.runtime.loading
                            text: qsTr("刷新插件列表")
                            implicitWidth: 36
                            implicitHeight: 36
                            enabled: !page.manager.busy && !page.manager.catalogLoading && !page.manager.runtime.loading
                            onClicked: page.marketplace ? page.manager.refreshCatalog(true) : page.manager.runtime.refresh()
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: page.compactLayout ? 44 + 16 + safeSurface.height : 44
                    EchoTabBar {
                        objectName: "pluginViewTabs"
                        contentSpacing: 7
                        labelLetterSpacing: 0
                        width: page.compactLayout ? parent.width : Math.min(280, parent.width)
                        glyphs: ["puzzle", "cloud"]
                        counts: [String(page.manager.runtime.count), page.manager.loaded || page.manager.catalogCount > 0 ? String(page.manager.catalogCount) : "-"]
                        labels: [qsTr("已安装"), qsTr("在线插件")]
                        accessibleName: qsTr("插件视图")
                        currentIndex: page.marketplace ? 1 : 0
                        onPicked: index => page.router.replaceQuery({
                                view: index === 1 ? "marketplace" : "installed"
                            })
                    }
                    Rectangle {
                        id: safeSurface
                        x: parent.width - width
                        y: page.compactLayout ? 60 : (44 - height) / 2
                        width: safe.implicitWidth + 24
                        height: safe.implicitHeight + 12
                        radius: Theme.controlRadius
                        color: Theme.mutedSurface
                        border.color: Theme.colors.controlBorder
                        RowLayout {
                            id: safe
                            anchors.centerIn: parent
                            spacing: 8
                            EchoIcon {
                                name: "shield"
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                color: Theme.accentText
                            }
                            Label {
                                text: qsTr("安全模式")
                                color: Theme.text
                                font: Theme.font(12, Theme.mediumWeight)
                            }
                            EchoSwitch {
                                objectName: "pluginSafeMode"
                                checked: page.manager.runtime.safeMode
                                enabled: !page.manager.runtime.loading && !page.manager.busy
                                Accessible.name: qsTr("安全模式")
                                onToggled: page.manager.runtime.setSafeMode(checked)
                            }
                        }
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.subtleBorder
            }
        }
        ListView {
            id: list
            objectName: "pluginCardList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: rows
            reuseItems: true
            EchoViewPoolGuard {
                targetView: list
            }
            cacheBuffer: 320
            spacing: 14
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: 24
            ScrollBar.vertical: EchoPageScrollBar {
                view: list
                viewActive: page.context.active
            }
            header: Item {
                width: list.width
                height: headerContent.implicitHeight + 20 + (heading.visible ? 14 : 0)
                ColumnLayout {
                    id: headerContent
                    x: 24
                    y: 20
                    width: list.width - 48
                    spacing: 14
                    Flow {

                        Layout.fillWidth: true
                        visible: page.marketplace
                        spacing: 10
                        EchoTextField {
                            objectName: "pluginMarketplaceSearch"
                            width: page.compactLayout ? parent.width : Math.min(352, parent.width)
                            clearable: true
                            placeholderText: qsTr("搜索插件、作者或标签")
                            text: page.manager.search
                            onTextEdited: page.manager.search = text
                        }
                        RowLayout {
                            width: page.compactLayout ? parent.width : Math.min(318, parent.width)
                            spacing: 10
                            EchoSelect {
                                Layout.preferredWidth: parent.width - 46
                                options: page.manager.sourceOptions
                                value: page.manager.sourceFilter
                                Accessible.name: qsTr("筛选插件源")
                                onValueEdited: nextValue => page.manager.sourceFilter = nextValue
                            }
                            PluginActionButton {
                                objectName: "pluginSourceManage"
                                iconOnly: true
                                glyph: "cloud"
                                text: qsTr("管理插件源")
                                tooltipText: text
                                onClicked: sourcesOwner.open()
                            }
                        }
                        PluginActionButton {
                            visible: page.manager.updateCount > 0 || page.manager.updatingAll
                            accent: true
                            implicitHeight: 36
                            leftPadding: 16
                            rightPadding: 16
                            font: Theme.font(12, Theme.blackWeight)
                            text: page.manager.updateAllLabel
                            glyph: "arrow-bar-down"
                            loading: page.manager.updatingAll
                            enabled: !page.manager.busy && !page.manager.catalogLoading
                            onClicked: page.manager.updateAll()
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(36, sourceErrorRow.implicitHeight + 16)
                        visible: page.marketplace && page.manager.sourceError.length > 0
                        radius: Theme.controlRadius
                        color: Theme.withAlpha("#f59e0b", 0.08)
                        border.color: Theme.withAlpha("#f59e0b", 0.22)
                        RowLayout {
                            id: sourceErrorRow
                            x: 12
                            y: 8
                            width: parent.width - 24
                            spacing: 8
                            EchoIcon {
                                name: "alert-triangle"
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                                color: Theme.dark ? "#fbbf24" : "#b45309"
                            }
                            Label {
                                Layout.fillWidth: true
                                text: page.manager.sourceError
                                color: Theme.dark ? "#fbbf24" : "#b45309"
                                font: Theme.font(12, Theme.semiboldWeight)
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    RowLayout {
                        id: heading
                        objectName: "pluginCatalogHeading"
                        Layout.fillHeight: false
                        Layout.fillWidth: true
                        Layout.preferredHeight: 21
                        visible: page.marketplace || page.manager.runtime.count > 0
                        spacing: 16
                        Label {
                            text: page.marketplace ? qsTr("在线插件") : qsTr("已安装插件")
                            color: Theme.text
                            font: Theme.font(14, Theme.boldWeight)
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Label {
                            Layout.minimumWidth: 0
                            Layout.fillWidth: page.compactLayout
                            horizontalAlignment: Text.AlignRight
                            text: page.marketplace ? qsTr("%1 个可浏览").arg(page.manager.catalog.count) + " · " + page.manager.sourceSummary + (page.manager.fetchedAt ? " · " + new Date(page.manager.fetchedAt).toLocaleString(Qt.locale(), "yyyy/M/d HH:mm:ss") : "") : qsTr("%1/%2 个已启用").arg(page.manager.runtime.enabledCount).arg(page.manager.runtime.count)
                            color: Theme.secondaryText
                            font: Theme.font(12, Theme.semiboldWeight)
                            wrapMode: Text.Wrap
                        }
                    }
                    Loader {
                        Layout.fillWidth: true
                        active: page.initialLoading && page.context.active
                        visible: active
                        Layout.preferredHeight: active && item ? (item as Item).implicitHeight : 0
                        sourceComponent: PluginMarketplaceSkeleton {
                            columns: rows.columns
                            resourceActive: page.context.active
                        }
                    }
                }
            }
            delegate: Item {
                id: row
                required property var cards
                width: list.width
                // Native layout observes child insertion and implicit size changes,
                // including Repeater delegates completed after async page creation.
                height: cardLayout.implicitHeight
                RowLayout {
                    id: cardLayout
                    x: 24
                    spacing: 14
                    Repeater {
                        id: cardRepeater
                        model: row.cards
                        delegate: PluginCard {
                            required property var modelData
                            objectName: "pluginCard-" + (modelData.id || "invalid")
                            Layout.preferredWidth: Math.max(0, (list.width - 48 - (rows.columns - 1) * 14) / rows.columns)
                            Layout.fillHeight: true
                            record: modelData
                            marketplace: page.marketplace
                            manager: page.manager
                            onUninstallRequested: installation => {
                                page.uninstallId = installation;
                                uninstallOwner.open();
                            }
                            onFailureRequested: installation => {
                                page.failureId = installation;
                                failureOwner.open();
                            }
                            onSettingsRequested: installation => {
                                page.settingsId = installation;
                                page.settingsScope = page.manager.requestSettings(installation);
                                if (page.settingsScope)
                                    pluginSettingsOwner.open();
                            }
                        }
                    }
                }
            }
            footer: Item {
                width: list.width
                height: rows.count === 0 && !page.initialLoading ? 245 : 0
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: parent.height > 0
                    spacing: 0
                    EchoIcon {
                        objectName: "pluginEmptyIcon"
                        Layout.alignment: Qt.AlignHCenter
                        name: page.marketplace ? "cloud" : "puzzle"
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        color: Theme.withAlpha(Theme.text, 0.2)
                    }
                    Label {
                        text: page.marketplace ? qsTr("暂无在线插件") : qsTr("暂无插件")
                        Layout.topMargin: 16
                        font: Theme.font(16, Theme.mediumWeight)
                        color: Theme.secondaryText
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        Layout.topMargin: 8
                        Layout.maximumWidth: list.width - 48
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        text: page.marketplace ? qsTr("添加插件源或刷新在线列表后再试") : qsTr("安装 .echomod 插件包，或放入上方目录后刷新")
                        font: Theme.font(14, Theme.regularWeight)
                        color: Theme.secondaryText
                    }
                }
            }
        }
    }
    DropArea {
        id: drop
        anchors.fill: parent
        enabled: page.context.active && !page.manager.busy
        keys: ["text/uri-list"]
        onDropped: event => {
            if (event.urls.length && page.manager.installLocalBatch(event.urls)) {
                event.acceptProposedAction();
            }
        }
    }
    EchoPopupLoader {
        id: dataOwner
        hostItem: page
        sourceComponent: PluginDataDialog {
            runtime: page.manager.runtime
        }
    }
    EchoPopupLoader {
        id: deferredSurfaceOwner
        objectName: "pluginLocalInstallOverlayOwner"
        hostItem: page
        requestedVisible: page.context.active && page.visible && (drop.containsDrag || page.manager.busy && !page.manager.installingKey.length)
        sourceComponent: Component {
            PluginLocalInstallOverlay {
                id: deferredSurface
                coveredItem: page
                uiScale: page.settings.uiScale
                resourceActive: page.context.active
                manager: page.manager
                dragging: drop.containsDrag
            }
        }
    }
    EchoPopupLoader {
        id: sourcesOwner
        objectName: "pluginSourceDialogOwner"
        hostItem: page
        sourceComponent: Component {
            PluginSourceDialog {
                id: sources
                manager: page.manager
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: uninstallOwner
        objectName: "pluginUninstallDialogOwner"
        hostItem: page
        sourceComponent: Component {
            PluginUninstallDialog {
                id: uninstall
                runtime: page.manager.runtime
                installationId: page.uninstallId
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: failureOwner
        objectName: "pluginFailureDialogOwner"
        hostItem: page
        sourceComponent: Component {
            PluginFailureDialog {
                id: failure
                runtime: page.manager.runtime
                installationId: page.failureId
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoPopupLoader {
        id: pluginSettingsOwner
        objectName: "pluginSettingsDialogOwner"
        hostItem: page
        sourceComponent: Component {
            PluginSettingsDialog {
                id: pluginSettings
                runtime: page.manager.runtime
                installationId: page.settingsId
                uiScale: page.settings.uiScale
                scope: page.settingsScope
                onClosed: page.settingsScope = null
            }
        }
    }
    EchoPopupLoader {
        id: grantsOwner
        hostItem: page
        sourceComponent: Component {
            PluginGrantsDialog {
                manager: page.manager
                uiScale: page.settings.uiScale
            }
        }
    }
    EchoFileDialogLoader {
        id: packageFileOwner
        hostItem: page
        sourceComponent: Component {
            EchoFileDialog {
                objectName: "pluginPackageFileDialog"
                title: qsTr("安装 EchoMusicNext 插件")
                fileMode: EchoFileDialog.OpenFiles
                nameFilters: [qsTr("EchoMusicNext 插件 (*.echomod)")]
                onAccepted: page.manager.installLocalBatch(selectedFiles)
                onVisibleChanged: if (visible)
                    EchoPopupCoordinator.popupOpening(null, page)
            }
        }
    }
}
