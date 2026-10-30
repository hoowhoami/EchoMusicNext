pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme
import EchoMusic.Shell
import EchoMusic.Settings

SettingsSection {
    id: section
    required property ShellSettings settings
    required property WindowController windowService
    required property NetworkPreferences network
    property string modeDraft: network.proxyMode
    property string pacDraft: network.proxyPacScript
    property string rulesDraft: network.proxyRules
    property string usernameDraft: network.proxyUsername
    property string bypassDraft: network.proxyBypassRules
    property double apiDraft: network.apiTimeoutSeconds
    property double playerDraft: network.playerTimeoutSeconds
    property bool clearPassword: false
    readonly property list<string> modes: ["direct", "system", "auto_detect", "pac_script", "fixed_servers"]
    heading: qsTr("网络设置")
    glyph: "cloud"
    // 输入框焦点移开即提交全部草稿（设置自动保存，无需手动应用）。
    function applyDrafts() {
        if (network.busy)
            return;
        network.apply(modeDraft, pacDraft, rulesDraft, usernameDraft, password.text,
                      clearPassword, bypassDraft, apiDraft, playerDraft);
    }
    function resetDrafts() {
        modeDraft = network.proxyMode;
        pacDraft = network.proxyPacScript;
        rulesDraft = network.proxyRules;
        usernameDraft = network.proxyUsername;
        bypassDraft = network.proxyBypassRules;
        apiDraft = network.apiTimeoutSeconds;
        playerDraft = network.playerTimeoutSeconds;
        password.clear();
        clearPassword = false;
    }
    Connections {
        target: section.network
        function onApplied() {
            section.resetDrafts();
        }
    }
    Component.onDestruction: password.clear()
    SettingsNotice {
        text: qsTr("全局代理在输入框焦点移开后自动保存并用于新连接；加速站是否直连由 bypass、系统或 PAC 决定")
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("全局网络代理")
        description: qsTr("同时用于接口、封面、音频直链、更新和其他应用网络请求")
        EchoSelect {
            objectName: "settingsProxyMode"
            colors: Theme.floating
            implicitWidth: 180
            enabled: !section.network.busy
            model: [qsTr("直接连接"), qsTr("跟随系统"), qsTr("自动检测（WPAD）"), qsTr("PAC 脚本"), qsTr("手动代理")]
            currentIndex: section.modes.indexOf(section.modeDraft)
            Accessible.name: qsTr("网络代理模式")
            onActivated: {
                section.modeDraft = section.modes[currentIndex];
                section.applyDrafts();
            }
        }
    }
    SettingsDivider {
        visible: section.modeDraft === "pac_script"
    }
    SettingsRow {
        visible: section.modeDraft === "pac_script"
        heading: qsTr("PAC 脚本地址")
        description: qsTr("支持 HTTP、HTTPS 和 file:// 本地文件地址")
        EchoTextField {
            objectName: "settingsProxyPac"
            colors: Theme.floating
            implicitWidth: 240
            clearable: true
            text: section.pacDraft
            placeholderText: "https://example.com/proxy.pac"
            Accessible.name: qsTr("PAC 脚本地址")
            onTextEdited: section.pacDraft = text
            onEditingFinished: section.applyDrafts()
        }
    }
    SettingsDivider {
        visible: section.modeDraft === "fixed_servers"
    }
    SettingsRow {
        visible: section.modeDraft === "fixed_servers"
        heading: qsTr("代理规则")
        description: qsTr("例如 http://127.0.0.1:7890 或 socks5://127.0.0.1:1080")
        EchoTextField {
            objectName: "settingsProxyRules"
            colors: Theme.floating
            implicitWidth: 240
            clearable: true
            text: section.rulesDraft
            placeholderText: "http://127.0.0.1:7890"
            Accessible.name: qsTr("代理规则")
            onTextEdited: section.rulesDraft = text
            onEditingFinished: section.applyDrafts()
        }
    }
    SettingsDivider {
        visible: section.modeDraft !== "direct"
    }
    SettingsRow {
        visible: section.modeDraft !== "direct"
        heading: qsTr("代理认证")
        description: qsTr("HTTP/HTTPS 和 SOCKS5 使用安全保存的代理凭证")
        ColumnLayout {
            Layout.fillHeight: false
            Layout.preferredWidth: 240
            spacing: 8
            EchoTextField {
                objectName: "settingsProxyUsername"
                Layout.fillWidth: true
                colors: Theme.floating
                clearable: true
                text: section.usernameDraft
                placeholderText: qsTr("用户名（可选）")
                Accessible.name: qsTr("代理用户名")
                onTextEdited: section.usernameDraft = text
                onEditingFinished: section.applyDrafts()
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                spacing: 8
                EchoTextField {
                    id: password
                    objectName: "settingsProxyPassword"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    colors: Theme.floating
                    clearable: true
                    echoMode: TextInput.Password
                    placeholderText: section.network.hasProxyPassword ? qsTr("已安全保存，留空保持不变") : qsTr("密码（可选）")
                    Accessible.name: qsTr("代理密码")
                    onTextEdited: {
                        if (text.length)
                            section.clearPassword = false;
                    }
                    onEditingFinished: section.applyDrafts()
                }
                EchoSoftButton {
                    visible: section.network.hasProxyPassword
                    danger: true
                    colors: Theme.floating
                    text: section.clearPassword ? qsTr("取消清除") : qsTr("清除")
                    onClicked: {
                        password.clear();
                        section.clearPassword = !section.clearPassword;
                    }
                }
            }
        }
    }
    SettingsDivider {
        visible: section.modeDraft === "fixed_servers"
    }
    SettingsRow {
        visible: section.modeDraft === "fixed_servers"
        heading: qsTr("不代理地址")
        description: qsTr("逗号分隔；可加入 GitHub 加速站域名使其直连，默认 <local> 保护本机地址")
        EchoTextField {
            objectName: "settingsProxyBypass"
            colors: Theme.floating
            implicitWidth: 240
            clearable: true
            text: section.bypassDraft
            placeholderText: "<local>"
            Accessible.name: qsTr("不代理地址")
            onTextEdited: section.bypassDraft = text
            onEditingFinished: section.applyDrafts()
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("GitHub 加速地址")
        description: qsTr("用于更新和在线插件；失败后自动切回原始 GitHub，两段均服从全局代理规则")
        EchoTextField {
            objectName: "settingsGithubProxyUrl"
            colors: Theme.floating
            implicitWidth: 240
            clearable: true
            text: section.network.githubProxyUrl
            placeholderText: "https://ghfast.top"
            Accessible.name: qsTr("GitHub 加速地址")
            onTextEdited: section.network.githubProxyUrl = text
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("酷狗 API 超时")
        description: qsTr("接口请求超过该时间后中止，设为 0 使用默认行为")
        EchoSpinBox {
            objectName: "settingsApiTimeout"
            colors: Theme.floating
            implicitWidth: 180
            from: 0
            to: 300
            stepSize: 10
            decimals: 12
            suffix: qsTr("秒")
            value: section.apiDraft
            Accessible.name: qsTr("酷狗 API 超时")
            onValueModified: section.apiDraft = value
            onActiveFocusChanged: if (!activeFocus)
                section.applyDrafts()
        }
    }
    SettingsDivider {}
    SettingsRow {
        heading: qsTr("播放器网络超时")
        description: qsTr("播放器打开音频直链的网络等待时间")
        EchoSpinBox {
            objectName: "settingsPlayerNetworkTimeout"
            colors: Theme.floating
            implicitWidth: 180
            from: 1
            to: 300
            stepSize: 10
            decimals: 12
            suffix: qsTr("秒")
            value: section.playerDraft
            Accessible.name: qsTr("播放器网络超时")
            onValueModified: section.playerDraft = value
            onActiveFocusChanged: if (!activeFocus)
                section.applyDrafts()
        }
    }
}
