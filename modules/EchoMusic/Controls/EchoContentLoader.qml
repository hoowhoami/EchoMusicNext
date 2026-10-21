pragma ComponentBehavior: Bound
import QtQuick

// Both Control content/background items and surface content loaders have their
// semantic owner as native visual parent. An alias adds no forwarding binding.
Loader {
    id: host
    readonly property alias owner: host.parent
}
