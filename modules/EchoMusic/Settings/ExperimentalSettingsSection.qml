pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import EchoMusic.Theme
import EchoMusic.Controls
import EchoMusic.Shell
import EchoMusic.Settings

SettingsSection {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    required property SettingsDataService dataService
    property ExperimentalSettingsService experimental: null
    property LogService logging: null
    property RelaunchService relaunch: null
    property string exportProvider: "local"
    property string restoreProvider: "local"
    property string selectedBackupId: ""
    readonly property var providerOptions: [
        {
            label: qsTr("本地文件"),
            value: "local"
        }
    ].concat(dataService ? dataService.providers.map(p => ({
                label: p.name === p.pluginName ? p.name : p.name + " · " + p.pluginName,
                value: p.key
            })) : [])
    property bool exportSettings: true
    property bool exportPlugins: true
    property bool importSettings: true
    property bool importPlugins: true
    heading: qsTr("实验性功能")
    glyph: "flask"
    SettingsRow {
        heading: qsTr("页面缓存")
        description: qsTr("保留已访问的页面，返回时无需重新加载")
        EchoSwitch {
            objectName: "settingsKeepAlive"
            checked: section.settings.keepAliveEnabled
            Accessible.name: qsTr("页面缓存")
            onToggled: section.settings.keepAliveEnabled = checked
        }
    }
    SettingsDivider {
        visible: section.settings.keepAliveEnabled
    }
    SettingsRow {
        visible: section.settings.keepAliveEnabled
        heading: qsTr("最大缓存页面数")
        description: qsTr("超出后自动释放最早缓存的页面，避免占用过多内存")
        EchoSpinBox {
            objectName: "settingsCacheMaximum"
            implicitWidth: 180
            from: 3
            to: 30
            value: section.settings.keepAliveMax
            Accessible.name: qsTr("最大缓存页面数")
            onValueModified: section.settings.keepAliveMax = value
        }
    }
    SettingsDivider {
        visible: section.logging !== null
    }
    SettingsRow {
        visible: section.logging !== null
        heading: qsTr("日志级别")
        description: qsTr("标准模式只记录关键运行信息、警告和错误")
        EchoSelect {
            objectName: "settingsLogLevel"
            implicitWidth: 180
            options: [
                {
                    label: qsTr("标准"),
                    value: "info"
                },
                {
                    label: qsTr("详细"),
                    value: "debug"
                },
                {
                    label: qsTr("极详细"),
                    value: "verbose"
                },
                {
                    label: qsTr("仅警告"),
                    value: "warn"
                },
                {
                    label: qsTr("仅错误"),
                    value: "error"
                }
            ]
            value: section.logging ? section.logging.level : "info"
            colors: Theme.floating
            Accessible.name: qsTr("日志级别")
            onValueEdited: value => {
                if (section.logging)
                    section.logging.level = value;
            }
        }
    }
    SettingsDivider {
        visible: section.logging !== null
    }
    SettingsRow {
        visible: section.logging !== null
        heading: qsTr("API 响应体日志")
        description: qsTr("仅排查接口问题时开启，内容会裁剪并脱敏")
        EchoSwitch {
            objectName: "settingsLogApiResponse"
            checked: section.logging ? section.logging.apiResponseBody : false
            Accessible.name: qsTr("API 响应体日志")
            onToggled: if (section.logging)
                section.logging.apiResponseBody = checked
        }
    }
    SettingsDivider {
        visible: section.logging !== null
    }
    SettingsRow {
        visible: section.logging !== null
        heading: qsTr("临时诊断日志")
        controlGap: 8
        description: section.logging && section.logging.diagnosticActive ? qsTr("剩余约 %1 分钟").arg(section.logging.diagnosticMinutes) : qsTr("未启用")
        EchoSoftButton {
            objectName: "settingsDisableDiagnostics"
            visible: section.logging && section.logging.diagnosticActive
            text: qsTr("关闭")
            colors: Theme.floating
            onClicked: if (section.logging)
                section.logging.disableTemporaryDiagnostics()
        }
        EchoSoftButton {
            objectName: "settingsEnableDiagnostics"
            text: qsTr("10 分钟")
            colors: Theme.floating
            onClicked: if (section.logging)
                section.logging.enableTemporaryDiagnostics()
        }
        EchoSoftButton {
            objectName: "settingsRestartDiagnostics"
            text: section.relaunch && section.relaunch.automaticRestart ? qsTr("诊断并重启") : qsTr("开启启动诊断")
            enabled: section.relaunch !== null && !section.relaunch.busy
            colors: Theme.floating
            onClicked: {
                if (section.logging && section.logging.enableTemporaryDiagnostics())
                    section.relaunch.restart();
            }
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
        heading: qsTr("用户信息")
        description: qsTr("查看当前账号与设备信息，可复制鉴权头用于调试接口")
        EchoSoftButton {
            objectName: "settingsViewUserInfo"
            text: qsTr("查看")
            enabled: section.experimental !== null && !section.experimental.busy && !section.experimental.resetPending
            colors: Theme.floating
            onClicked: userInfoOwner.open()
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("重置设备身份")
        description: qsTr("清除本机 guid、mid、dfid，并在重启后重新生成设备身份")
        statusText: section.experimental && section.experimental.resetPending ? qsTr("设备身份已清除，请重启应用后再登录") : ""
        EchoSoftButton {
            objectName: "settingsResetDeviceIdentity"
            text: qsTr("重置")
            danger: true
            enabled: section.experimental !== null && !section.experimental.busy && !section.experimental.resetPending
            colors: Theme.floating
            onClicked: resetOwner.open()
        }
    }
    // 数据服务由宿主按需注入；嵌入方可以不提供（如部分测试会话），
    // 行级按钮在缺失时禁用即可，弹层内部绑定只在服务存在时才会求值。
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("备份与恢复")
        description: qsTr("备份应用设置、已安装插件及插件数据，或在其他设备恢复")
        controlGap: 8
        EchoSoftButton {
            objectName: "settingsCreateBackup"
            text: qsTr("创建备份")
            enabled: section.dataService && !section.dataService.busy && !section.dataService.providerBusy
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
            enabled: section.dataService && !section.dataService.busy && !section.dataService.providerBusy
            colors: Theme.floating
            onClicked: sourceDialogOwner.open()
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
            enabled: section.dataService && !section.dataService.busy && !section.dataService.providerBusy
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
        objectName: "settingsBackupSourceDialogOwner"
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
            EchoFileDialog {
                id: exportPicker
                title: qsTr("创建备份")
                fileMode: EchoFileDialog.SaveFile
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
            EchoFileDialog {
                id: restorePicker
                title: qsTr("选择备份")
                fileMode: EchoFileDialog.OpenFile
                nameFilters: [qsTr("EchoMusicNext 备份 (*.echonext-backup)")]
                onAccepted: section.dataService.inspect(selectedFile)
            }
        }
    }
    EchoPopupLoader {
        id: userInfoOwner
        objectName: "settingsUserInfoOwner"
        hostItem: section
        sourceComponent: SettingsUserInfoDialog {
            service: section.experimental
            uiScale: section.settings.uiScale
        }
    }
    EchoPopupLoader {
        id: resetOwner
        objectName: "settingsResetDeviceIdentityOwner"
        hostItem: section
        sourceComponent: EchoDialog {
            id: resetDialog
            objectName: "settingsResetDeviceIdentityDialog"
            uiScale: section.settings.uiScale
            heading: qsTr("重置设备身份")
            description: qsTr("这会清除本机设备标识并退出当前账号。重启后会生成新的设备身份，酷狗可能会把它视为一台新设备。")
            footerContent: RowLayout {
                Item {
                    Layout.fillWidth: true
                }
                EchoSoftButton {
                    objectName: "settingsCancelDeviceReset"
                    text: qsTr("取消")
                    colors: Theme.floating
                    onClicked: resetDialog.close()
                }
                EchoSoftButton {
                    objectName: "settingsConfirmDeviceReset"
                    text: qsTr("确认重置")
                    danger: true
                    colors: Theme.floating
                    onClicked: {
                        resetDialog.close();
                        section.experimental.resetDeviceIdentity();
                    }
                }
            }
        }
    }
    Connections {
        target: section.experimental
        function onIdentityChanged(): void {
            resetOwner.close();
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
