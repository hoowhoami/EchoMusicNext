import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls

ApplicationWindow {
    id: host
    width: 400
    height: 300
    visible: true
    property bool intent: false
    property alias popup: owner.item
    function openEmpty(): void {
        owner.open();
    }
    function openConfigured(): void {
        owner.open({
            payload: 17,
            entry: {
                key: "theme"
            }
        });
    }
    function closePopup(): void {
        owner.close();
    }
    EchoPopupLoader {
        id: owner
        hostItem: host.contentItem
        requestedVisible: host.intent
        sourceComponent: Popup {
            width: 120
            height: 100
            property int payload: 0
            property var entry
        }
    }
}
