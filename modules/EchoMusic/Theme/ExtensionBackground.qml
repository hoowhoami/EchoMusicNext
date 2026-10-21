pragma ComponentBehavior: Bound
import QtQuick
import QtMultimedia

Item {
    id: root
    required property var recipe
    required property AppearanceEntry entry
    opacity: recipe.opacity === undefined ? 1 : recipe.opacity
    Rectangle {
        anchors.fill: parent
        color: root.recipe.color || "transparent"
    }
    Loader {
        anchors.fill: parent
        active: root.opacity > 0 && root.recipe.kind === "gradient"
        sourceComponent: ExtensionGradient {
            recipe: root.recipe
        }
    }
    Loader {
        anchors.fill: parent
        active: root.opacity > 0 && root.recipe.kind === "image"
        sourceComponent: Image {
            source: root.recipe.source || ""
            sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
            fillMode: root.recipe.fit === "contain" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            asynchronous: true
            onStatusChanged: if (status === Image.Error)
                Theme.store.extensions.fail(root.entry, qsTr("主题背景图片无法加载"))
        }
    }
    Loader {
        anchors.fill: parent
        active: root.opacity > 0 && root.recipe.kind === "video"
        sourceComponent: Item {
            MediaPlayer {
                id: video
                source: root.recipe.source || ""
                videoOutput: output
                loops: MediaPlayer.Infinite
                autoPlay: true
                onErrorOccurred: Theme.store.extensions.fail(root.entry, errorString)
            }
            VideoOutput {
                id: output
                anchors.fill: parent
                fillMode: root.recipe.fit === "contain" ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        color: root.recipe.shadeColor || "black"
        opacity: root.recipe.shade || 0
    }
}
