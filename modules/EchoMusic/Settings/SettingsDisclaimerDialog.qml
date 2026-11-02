pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Controls
import EchoMusic.Theme

EchoDialog {
    id: dialog
    objectName: "settingsDisclaimerDialog"
    heading: qsTr("免责声明")
    preferredWidth: 620
    maximumHeight: 560
    windowHeightMargin: 140
    flushBody: true
    // License and content ownership wording describe this application and its actual behavior.
    readonly property var sections: [
        {
            title: qsTr("项目定位"),
            content: qsTr("本项目是基于公开接口开发的第三方音乐客户端，仅用于技术学习和研究，不涉及任何商业行为。")
        },
        {
            title: qsTr("数据来源"),
            content: qsTr("音乐数据通过公开 API 接口获取，音乐内容版权归原平台及版权方所有。")
        },
        {
            title: qsTr("使用限制"),
            content: qsTr("使用者应遵守当地法律法规，尊重知识产权。")
        },
        {
            title: qsTr("责任声明"),
            content: qsTr("因使用本项目产生的直接或间接损失，包括法律纠纷、数据丢失、设备故障等，由使用者依法承担。")
        },
        {
            title: qsTr("版权尊重"),
            content: qsTr("音乐创作不易，请支持正版，购买官方会员服务。")
        },
        {
            title: qsTr("开源协议"),
            content: qsTr("EchoMusic 基于 GPL-3.0-only 协议开源，第三方组件保留各自的版权与许可。")
        },
        {
            title: qsTr("争议解决"),
            content: qsTr("如版权方认为本项目侵犯其权益，请通过 GitHub Issues 联系，我们将积极配合处理。")
        }
    ]
    body: SettingsScrollBody {
        surface: dialog
        Repeater {
            model: dialog.sections
            EchoLegalSection {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: false
                heading: modelData.title
                text: modelData.content
            }
        }
    }
    footerContent: RowLayout {
        Item {
            Layout.fillWidth: true
        }
        EchoSoftButton {
            Layout.minimumWidth: 96
            text: qsTr("我知道了")
            accent: true
            colors: Theme.floating
            onClicked: dialog.close()
        }
    }
}
