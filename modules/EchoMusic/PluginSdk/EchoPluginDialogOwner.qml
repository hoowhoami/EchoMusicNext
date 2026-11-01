pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.PluginSdk
import EchoMusic.Theme

QtObject {
    id: owner
    required property Item hostItem
    required property EchoPluginDialog handle
    required property var options
    property EchoPluginContext plugin: null
    property real uiScale: 1
    signal ready
    signal closing
    signal finished
    signal failed
    function open(): void {
        popup.open();
        if (!popup.item)
            failed();
    }
    function close(): void {
        if (popup.item)
            popup.close();
        else
            finished();
    }
    property EchoPopupLoader popup: EchoPopupLoader {
        id: popup
        hostItem: owner.hostItem
        keepOnMinimize: true
        sourceComponent: Component {
            EchoDialog {
                id: surface
                objectName: "pluginDialog"
                preferredWidth: owner.options.width
                heading: owner.options.title || ""
                modal: owner.options.modal
                closePolicy: Popup.CloseOnEscape | (owner.options.closeOnMask ? Popup.CloseOnPressOutside : 0)
                uiScale: owner.uiScale
                windowHeightMargin: 32
                y: parent ? Math.max(0, (parent.height - height * uiScale) / 2) : 0
                property bool bodyReady: false
                onOpened: if (bodyReady)
                    owner.ready()
                onAboutToHide: owner.closing()
                onClosed: owner.finished()
                body: Component {
                    Loader {
                        id: contentLoader
                        width: parent ? parent.width : 0
                        asynchronous: true
                        Component.onCompleted: setSource(owner.options.component, {
                            plugin: owner.plugin,
                            dialog: owner.handle
                        })
                        onStatusChanged: {
                            if (status === Loader.Error || (status === Loader.Ready && !(item instanceof Item)))
                                owner.failed();
                            else if (status === Loader.Ready) {
                                surface.bodyReady = true;
                                if (surface.opened)
                                    owner.ready();
                            }
                        }
                    }
                }
            }
        }
    }
}
