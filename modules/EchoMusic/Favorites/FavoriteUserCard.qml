pragma ComponentBehavior: Bound
import EchoMusic.Music

// Search artists and followed people share the same circular media recipe.
ContentCard {
    objectName: "favoriteUserCard"
    required property string nickname
    required property string avatarUrl
    required property string metaText
    property bool singer: false
    property string targetId: ""
    navigationAllowed: singer && navigationAvailable && targetId.length > 0
    artist: true
    title: nickname
    artistText: metaText
    extraText: ""
    coverUrl: avatarUrl
    durationText: ""
    resourceId: targetId
}
