pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls
import EchoMusic.PluginSdk

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
        popupOwner.open();
        if (!popupOwner.item)
            failed();
    }
    function close(): void {
        if (popupOwner.item)
            popupOwner.close();
        else
            finished();
    }
    property EchoPopupLoader popup: EchoPopupLoader {
        id: popupOwner
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
                readonly property real reservedHeight: owner.options.height || 0
                Binding {
                    target: surface
                    property: "height"
                    when: surface.reservedHeight > 0
                    value: Math.min(surface.reservedHeight, surface.maximumHeight, surface.parent ? Math.max(120, surface.parent.height / surface.uiScale - surface.windowHeightMargin) : surface.reservedHeight)
                }
                onOpened: if (bodyReady)
                    owner.ready()
                onAboutToHide: owner.closing()
                onClosed: owner.finished()
                body: Component {
                    Loader {
                        id: contentLoader
                        EchoUiScope.owner: owner.plugin ? owner.plugin.id : ""
                        EchoUiScope.surface: "dialog"
                        EchoUiScope.key: "dialog:" + owner.options.component.toString().split("/").pop()
                        width: parent ? parent.width : 0
                        asynchronous: true
                        property bool loadStarted: false
                        function loadBody(): void {
                            if (loadStarted || !owner.plugin || !owner.plugin.active || (surface.reservedHeight > 0 && !surface.visible))
                                return;
                            loadStarted = true;
                            setSource(owner.options.component, {
                                plugin: owner.plugin,
                                dialog: owner.handle
                            });
                        }
                        EchoSpinner {
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            visible: surface.reservedHeight > 0 && !surface.bodyReady
                            resourceActive: visible && surface.visible
                        }
                        // Reserved surfaces paint their shell before starting the
                        // asynchronous body. Natural-height callers keep their order.
                        Component.onCompleted: if (surface.reservedHeight <= 0)
                            loadBody()
                        Connections {
                            target: owner.hostItem ? owner.hostItem.Window.window : null
                            enabled: surface.reservedHeight > 0 && surface.visible && !contentLoader.loadStarted
                            function onFrameSwapped(): void {
                                Qt.callLater(contentLoader.loadBody);
                            }
                        }
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
