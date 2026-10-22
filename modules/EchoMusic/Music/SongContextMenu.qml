pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import EchoMusic.Controls

// Shared context menu for song lists. Playback and library actions precede
// blacklist and page-owned destructive actions, followed by sharing and copying.
//
// The page supplies SongCatalogActions or SearchModel directly. Both expose
// blacklistRevision so live rows update when the shared song index changes.
EchoMenu {
    id: root
    required property var commands
    property string songId: ""
    /// Optional page-owned danger row (owner playlist / playback history).
    property bool removeVisible: false
    property string removeLabel: qsTr("从歌单删除")
    /// History registers its remove row inside the extension group behind the
    /// blacklist rows; Playlist shows its own danger group right before the
    /// trailing separator.
    property bool removeAfterExtensions: false
    /// Cloud deletion requires a non-zero numeric file ID. The cloud page supplies
    /// that check; other page actions remain enabled by default.
    property bool removeEnabled: true
    signal removeTriggered
    property var pluginContext: ({})
    property var pluginItems: []
    onAboutToShow: {
        pluginContext = root.ready && typeof root.commands.pluginMenuContext === "function" ? root.commands.pluginMenuContext(root.songId) : ({});
        pluginItems = EchoSongMenuRegistry.entries(pluginContext);
    }
    Connections {
        target: EchoSongMenuRegistry
        function onChanged() {
            if (root.visible)
                root.pluginItems = EchoSongMenuRegistry.entries(root.pluginContext);
        }
    }
    Instantiator {
        model: root.pluginItems
        delegate: Action {
            required property var modelData
            objectName: "pluginSongMenuAction-" + modelData.key
            text: modelData.text
            enabled: modelData.enabled
            property bool danger: modelData.danger
            onTriggered: {
                const current = root.commands.pluginMenuContext(root.songId);
                if (JSON.stringify(current) === JSON.stringify(root.pluginContext))
                    EchoSongMenuRegistry.invoke(modelData.key, modelData.token, current);
            }
        }
        onObjectAdded: (index, object) => root.insertAction(Math.max(0, root.count - 3), object)
        onObjectRemoved: (index, object) => root.removeAction(object)
    }
    clampToWindow: true
    readonly property bool ready: root.commands !== undefined && root.commands !== null
    // SearchModel names its immediate playback entry playSong; SongCatalogActions
    // takes play(id, replaceContext).
    readonly property bool searchStyle: root.ready && typeof root.commands.playSong === "function"
    function playNow(id: string): void {
        if (!root.ready || id.length === 0)
            return;
        if (typeof root.commands.playNow === "function")
            root.commands.playNow(id);
        else if (root.searchStyle)
            root.commands.playSong(id);
        else
            root.commands.play(id, false);
    }
    function playNext(id: string): void {
        if (root.ready)
            root.commands.appendSong(id, true);
    }
    function playLast(id: string): void {
        if (root.ready)
            root.commands.appendSong(id, false);
    }
    function addTo(id: string): void {
        if (root.ready)
            root.commands.addSong(id);
    }
    function share(id: string): void {
        if (root.ready)
            root.commands.shareSong(id);
    }
    function copy(id: string): void {
        if (root.ready)
            root.commands.copySong(id);
    }
    readonly property bool blacklistVisible: root.ready && root.commands.blacklistRevision >= 0 && root.commands.blacklistAvailable(root.songId)
    readonly property bool blacklisted: root.blacklistVisible && root.commands.blacklisted(root.songId)
    EchoMenuEntry {
        objectName: "songMenuPlayNow"
        text: qsTr("立即播放")
        onTriggered: root.playNow(root.songId)
    }
    EchoMenuEntry {
        objectName: "songMenuPlayNext"
        text: qsTr("下一首播放")
        onTriggered: root.playNext(root.songId)
    }
    EchoMenuEntry {
        objectName: "songMenuPlayLast"
        text: qsTr("排队候播")
        onTriggered: root.playLast(root.songId)
    }
    EchoMenuSeparator {}
    EchoMenuEntry {
        objectName: "songMenuAddTo"
        text: qsTr("添加到")
        onTriggered: root.addTo(root.songId)
    }
    EchoMenuSeparator {
        visible: root.blacklistVisible || (root.removeVisible && root.removeAfterExtensions)
    }
    EchoMenuEntry {
        objectName: "songMenuBlacklist"
        text: qsTr("不感兴趣")
        visible: root.blacklistVisible && !root.blacklisted
        // blacklistBusy() is not a tracked dependency; the revision term makes
        // the row re-evaluate when the shared writer reports progress.
        enabled: visible && root.commands.blacklistRevision >= 0 && !root.commands.blacklistBusy()
        onTriggered: if (root.ready)
            root.commands.addToBlacklist(root.songId)
    }
    EchoMenuEntry {
        objectName: "songMenuBlacklistUndo"
        text: qsTr("取消不感兴趣")
        visible: root.blacklistVisible && root.blacklisted
        enabled: visible && root.commands.blacklistRevision >= 0 && !root.commands.blacklistBusy()
        onTriggered: root.commands.removeFromBlacklist(root.songId)
    }
    EchoMenuEntry {
        objectName: "songMenuRemoveExtension"
        visible: root.removeVisible && root.removeAfterExtensions
        enabled: root.removeEnabled
        text: root.removeLabel
        danger: true
        onTriggered: root.removeTriggered()
    }
    EchoMenuSeparator {
        visible: root.removeVisible && !root.removeAfterExtensions
    }
    EchoMenuEntry {
        objectName: "songMenuRemove"
        visible: root.removeVisible && !root.removeAfterExtensions
        enabled: root.removeEnabled
        text: root.removeLabel
        danger: true
        onTriggered: root.removeTriggered()
    }
    EchoMenuSeparator {}
    EchoMenuEntry {
        objectName: "songMenuShare"
        text: qsTr("分享")
        onTriggered: root.share(root.songId)
    }
    EchoMenuEntry {
        objectName: "songMenuCopy"
        text: qsTr("复制信息")
        onTriggered: root.copy(root.songId)
    }
}
