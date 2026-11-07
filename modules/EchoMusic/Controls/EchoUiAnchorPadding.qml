pragma ComponentBehavior: Bound
import QtQml

QtObject {
    id: reservation
    required property QtObject control
    required property var bases
    property var extra: ({})
    readonly property Binding left: Binding {
        target: reservation.control
        property: "leftPadding"
        when: reservation.extra.left > 0
        value: reservation.bases.left + (reservation.extra.left || 0)
        restoreMode: Binding.RestoreBindingOrValue
    }
    readonly property Binding right: Binding {
        target: reservation.control
        property: "rightPadding"
        when: reservation.extra.right > 0
        value: reservation.bases.right + (reservation.extra.right || 0)
        restoreMode: Binding.RestoreBindingOrValue
    }
    readonly property Binding top: Binding {
        target: reservation.control
        property: "topPadding"
        when: reservation.extra.top > 0
        value: reservation.bases.top + (reservation.extra.top || 0)
        restoreMode: Binding.RestoreBindingOrValue
    }
    readonly property Binding bottom: Binding {
        target: reservation.control
        property: "bottomPadding"
        when: reservation.extra.bottom > 0
        value: reservation.bases.bottom + (reservation.extra.bottom || 0)
        restoreMode: Binding.RestoreBindingOrValue
    }
}
