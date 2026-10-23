pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Settings

SettingsSection {
    id: section
    objectName: "backupDataSettings"
    required property ShellSettings settings
    required property WindowController windowService
    required property SettingsDataService dataService
    property LogService logging: null
    property string exportProvider: "local"
    property string restoreProvider: "local"
    property string selectedBackupId: ""
    readonly property var providerOptions: [
        {
            label: qsTr("本地文件"),
            value: "local"
        }
    ].concat(dataService.providers.map(p => ({
                label: p.name === p.pluginName ? p.name : p.name + " · " + p.pluginName,
                value: p.key
            })))
    property bool exportSettings: true
    property bool exportPlugins: true
    property bool importSettings: true
    property bool importPlugins: true
    heading: qsTr("数据与安全")
    glyph: "shield"
    SettingsRow {
        heading: qsTr("备份与恢复")
        description: qsTr("备份应用设置、已安装插件及插件数据，或在其他设备恢复")
        controlGap: 8
        EchoSoftButton {
            objectName: "settingsCreateBackup"
            text: qsTr("创建备份")
            enabled: !section.dataService.busy && !section.dataService.providerBusy
            colors: Theme.floating
            onClicked: {
                section.exportSettings = true;
                section.exportPlugins = true;
                exportDialogOwner.open();
            }
        }
        EchoSoftButton {
            objectName: "settingsRestoreBackup"
            text: qsTr("恢复备份")
            enabled: !section.dataService.busy && !section.dataService.providerBusy
            colors: Theme.floating
            onClicked: sourceDialogOwner.open()
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("查看运行日志")
        description: qsTr("打开本地日志目录以供排查问题")
        EchoSoftButton {
            text: qsTr("立即查看")
            enabled: section.logging !== null
            colors: Theme.floating
            onClicked: section.logging.openDirectory()
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("清除应用数据")
        description: qsTr("移除所有持久化设置及缓存信息")
        EchoSoftButton {
            objectName: "settingsClearData"
            text: qsTr("立即清除")
            danger: true
            enabled: !section.dataService.busy && !section.dataService.providerBusy
            colors: Theme.floating
            onClicked: clearDialogOwner.open()
        }
    }
    EchoPopupLoader {
        id: exportDialogOwner
        objectName: "settingsBackupExportDialogOwner"
        hostItem: section
        sourceComponent: Component {
            EchoDialog {
                id: exportDialog
                onClosed: section.dataService.cancelProvider()
                objectName: "settingsBackupExportDialog"
                uiScale: section.settings.uiScale
                preferredWidth: 520
                flushBody: true
                windowHeightMargin: 80
                heading: qsTr("创建备份")
                description: qsTr("选择要写入备份文件的内容。账号登录状态、设备身份和本机文件路径不会被备份。")
                closable: !section.dataService.busy
                closePolicy: section.dataService.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
                body: SettingsScrollBody {
                    surface: exportDialog
                    Label {
                        text: qsTr("保存位置")
                        color: Theme.floatingText
                        font: Theme.font(13, Theme.semiboldWeight)
                    }
                    EchoSelect {
                        Layout.fillWidth: true
                        objectName: "backupExportLocation"
                        options: section.providerOptions
                        value: section.exportProvider
                        enabled: !section.dataService.busy && !section.dataService.providerBusy
                        onValueEdited: next => section.exportProvider = String(next)
                        colors: Theme.floating
                        Accessible.name: qsTr("备份保存位置")
                    }
                    SettingsRow {
                        heading: qsTr("应用设置")
                        description: qsTr("主题、播放和界面等可跨设备恢复的偏好")
                        EchoSwitch {
                            checked: section.exportSettings
                            enabled: !section.dataService.busy && !section.dataService.providerBusy
                            colors: Theme.floating
                            Accessible.name: qsTr("备份应用设置")
                            onToggled: section.exportSettings = checked
                        }
                    }
                    SettingsRow {
                        heading: qsTr("插件与插件数据")
                        description: qsTr("已安装插件及其持久化数据")
                        EchoSwitch {
                            checked: section.exportPlugins
                            enabled: !section.dataService.busy && !section.dataService.providerBusy
                            colors: Theme.floating
                            Accessible.name: qsTr("备份插件与插件数据")
                            onToggled: section.exportPlugins = checked
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("备份内容未加密，任何拿到数据的人都可能读取其中的插件账号与数据。")
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        wrapMode: Text.Wrap
                    }
                }
                footerContent: RowLayout {
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("取消")
                        enabled: !section.dataService.busy
                        colors: Theme.floating
                        onClicked: exportDialog.close()
                    }
                    EchoSoftButton {
                        objectName: "backupCreateToStorage"
                        loading: section.dataService.busy || section.dataService.providerBusy
                        text: section.dataService.busy || section.dataService.providerBusy ? qsTr("创建中...") : section.exportProvider === "local" ? qsTr("选择位置并创建") : qsTr("创建并保存")
                        accent: true
                        enabled: !section.dataService.busy && !section.dataService.providerBusy && (section.exportSettings || section.exportPlugins)
                        colors: Theme.floating
                        onClicked: section.exportProvider === "local" ? exportPickerOwner.open() : section.dataService.saveToProvider(section.exportProvider, section.exportSettings, section.exportPlugins)
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: sourceDialogOwner
        hostItem: section
        sourceComponent: Component {
            EchoDialog {
                id: sourceDialog
                objectName: "settingsBackupSourceDialog"
                onClosed: section.dataService.cancelProvider()
                uiScale: section.settings.uiScale
                preferredWidth: 520
                flushBody: true
                windowHeightMargin: 80
                heading: qsTr("选择备份")
                description: qsTr("选择备份所在位置。插件存储会由对应插件列出和读取远端备份。")
                closable: !section.dataService.busy
                closePolicy: section.dataService.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
                body: SettingsScrollBody {
                    surface: sourceDialog
                    EchoSelect {
                        Layout.fillWidth: true
                        objectName: "backupRestoreLocation"
                        options: section.providerOptions
                        value: section.restoreProvider
                        enabled: !section.dataService.busy && !section.dataService.providerBusy
                        onValueEdited: next => {
                            section.restoreProvider = String(next);
                            section.selectedBackupId = "";
                            if (section.restoreProvider !== "local")
                                section.dataService.refreshProvider(section.restoreProvider);
                        }
                        colors: Theme.floating
                        Accessible.name: qsTr("备份来源")
                    }
                    Label {
                        Layout.fillWidth: true
                        text: section.restoreProvider === "local" ? qsTr("从本机选择一个 EchoMusicNext 备份文件") : section.dataService.providers.find(p => p.key === section.restoreProvider)?.description || qsTr("由插件负责读取备份数据")
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        wrapMode: Text.Wrap
                    }
                    Loader {
                        id: providerList
                        Layout.fillWidth: true
                        active: section.restoreProvider !== "local"
                        sourceComponent: Component {
                            SettingsBackupProviderList {
                                uiScale: section.settings.uiScale
                                service: section.dataService
                                providerKey: section.restoreProvider
                                onSelectedIdChanged: section.selectedBackupId = selectedId
                            }
                        }
                    }
                }
                footerContent: RowLayout {
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("取消")
                        enabled: !section.dataService.busy
                        colors: Theme.floating
                        onClicked: sourceDialog.close()
                    }
                    EchoSoftButton {
                        text: section.dataService.busy || section.dataService.providerBusy ? qsTr("读取中...") : section.restoreProvider === "local" ? qsTr("选择本地备份") : qsTr("读取所选备份")
                        accent: true
                        colors: Theme.floating
                        objectName: "backupReadSelected"
                        loading: section.dataService.busy
                        enabled: !section.dataService.busy && !section.dataService.providerBusy && (section.restoreProvider === "local" || section.selectedBackupId !== "")
                        onClicked: section.restoreProvider === "local" ? restorePickerOwner.open() : section.dataService.loadFromProvider(section.restoreProvider, section.selectedBackupId)
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: restoreDialogOwner
        objectName: "settingsBackupRestoreDialogOwner"
        hostItem: section
        sourceComponent: Component {
            EchoDialog {
                id: restoreDialog
                objectName: "settingsBackupRestoreDialog"
                uiScale: section.settings.uiScale
                preferredWidth: 520
                flushBody: true
                windowHeightMargin: 80
                heading: qsTr("从备份恢复")
                description: qsTr("恢复会覆盖所选项目的同名设置，并在完成后自动重启 EchoMusicNext。目标设备已有的其他插件不会被删除。")
                closable: !section.dataService.busy
                closePolicy: section.dataService.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onClosed: section.dataService.releaseInspection()
                body: SettingsScrollBody {
                    surface: restoreDialog
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("备份版本：EchoMusicNext %1\n创建时间：%2").arg(section.dataService.summary.appVersion || "").arg(section.dataService.summary.createdAt || "")
                        font: Theme.font(12, Theme.regularWeight)
                        color: Theme.floatingSecondaryText
                        wrapMode: Text.Wrap
                    }
                    SettingsRow {
                        heading: qsTr("应用设置")
                        description: qsTr("%1 项可恢复设置").arg(section.dataService.summary.settingCount || 0)
                        EchoSwitch {
                            checked: section.importSettings
                            enabled: !section.dataService.busy && !section.dataService.providerBusy && !!section.dataService.summary.settings
                            colors: Theme.floating
                            Accessible.name: qsTr("恢复应用设置")
                            onToggled: section.importSettings = checked
                        }
                    }
                    SettingsRow {
                        heading: qsTr("插件与插件数据")
                        description: qsTr("%1 个插件").arg(section.dataService.summary.pluginCount || 0)
                        EchoSwitch {
                            checked: section.importPlugins
                            enabled: !section.dataService.busy && !section.dataService.providerBusy && !!section.dataService.summary.plugins
                            colors: Theme.floating
                            Accessible.name: qsTr("恢复插件与插件数据")
                            onToggled: section.importPlugins = checked
                        }
                    }
                }
                footerContent: RowLayout {
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("取消")
                        enabled: !section.dataService.busy
                        colors: Theme.floating
                        onClicked: restoreDialog.close()
                    }
                    EchoSoftButton {
                        text: section.dataService.busy ? qsTr("恢复中...") : qsTr("恢复并重启")
                        accent: true
                        enabled: section.dataService.canRestore && (section.importSettings || section.importPlugins)
                        colors: Theme.floating
                        onClicked: section.dataService.restore(section.importSettings, section.importPlugins)
                    }
                }
            }
        }
    }
    EchoPopupLoader {
        id: clearDialogOwner
        hostItem: section
        sourceComponent: Component {
            EchoDialog {
                id: clearDialog
                uiScale: section.settings.uiScale
                heading: qsTr("确认清除应用数据？")
                description: qsTr("此操作将移除所有持久化设置与缓存，无法撤销。")
                footerContent: RowLayout {
                    Item {
                        Layout.fillWidth: true
                    }
                    EchoSoftButton {
                        text: qsTr("取消")
                        colors: Theme.floating
                        onClicked: clearDialog.close()
                    }
                    EchoSoftButton {
                        text: qsTr("确认清除")
                        danger: true
                        colors: Theme.floating
                        onClicked: {
                            section.dataService.clearApplicationData();
                            clearDialog.close();
                        }
                    }
                }
            }
        }
    }
    EchoFileDialogLoader {
        id: exportPickerOwner
        hostItem: section
        sourceComponent: Component {
            NativeDialogs.FileDialog {
                id: exportPicker
                title: qsTr("创建备份")
                fileMode: NativeDialogs.FileDialog.SaveFile
                nameFilters: [qsTr("EchoMusicNext 备份 (*.echonext-backup)")]
                defaultSuffix: "echonext-backup"
                onAccepted: section.dataService.create(selectedFile, section.exportSettings, section.exportPlugins)
            }
        }
    }
    EchoFileDialogLoader {
        id: restorePickerOwner
        hostItem: section
        sourceComponent: Component {
            NativeDialogs.FileDialog {
                id: restorePicker
                title: qsTr("选择备份")
                fileMode: NativeDialogs.FileDialog.OpenFile
                nameFilters: [qsTr("EchoMusicNext 备份 (*.echonext-backup)")]
                onAccepted: section.dataService.inspect(selectedFile)
            }
        }
    }
    Connections {
        target: section.dataService
        function onChanged(): void {
            if (!section.dataService.canRestore && !section.dataService.busy && restoreDialogOwner.opened)
                restoreDialogOwner.close();
        }
        function onProvidersChanged(): void {
            if (section.exportProvider !== "local" && !section.dataService.providers.some(p => p.key === section.exportProvider))
                section.exportProvider = "local";
            if (section.restoreProvider !== "local" && !section.dataService.providers.some(p => p.key === section.restoreProvider)) {
                section.restoreProvider = "local";
                section.selectedBackupId = "";
            }
        }
        function onProviderSaved(): void {
            exportDialogOwner.close();
        }
        function onCreated(): void {
            exportDialogOwner.close();
        }
        function onInspected(): void {
            section.importSettings = !!section.dataService.summary.settings;
            section.importPlugins = !!section.dataService.summary.plugins;
            sourceDialogOwner.close();
            restoreDialogOwner.open();
        }
    }
    Component.onDestruction: {
        exportPickerOwner.close();
        restorePickerOwner.close();
        exportDialogOwner.close();
        sourceDialogOwner.close();
        restoreDialogOwner.close();
        clearDialogOwner.close();
        if (dataService) {
            dataService.cancelUi();
        }
    }
}
