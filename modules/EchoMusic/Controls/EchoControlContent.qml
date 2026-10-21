pragma Singleton
pragma ComponentBehavior: Bound
import QtQuick
import EchoMusic.Theme

// Read owners through typed native properties: calling a per-instance QML
// helper here makes Qt 6.12 fall back from AOT to JS for each dependent binding.
// Engine-owned recipes only. Hosts own active visuals and their state; the
// shared templates never retain an individual control, page or window.
QtObject {
    readonly property Component icon: Component {
        EchoIcon {
            id: glyphIcon
            name: ((glyphIcon.parent as EchoContentLoader)?.owner as EchoIconButton)?.glyph ?? ""
            color: {
                const owner = ((glyphIcon.parent as EchoContentLoader)?.owner as EchoIconButton);
                return owner ? owner.visualForeground : Qt.rgba(0, 0, 0, 0);
            }
        }
    }
    readonly property Component playPauseIcon: Component {
        EchoPlayPauseIcon {
            id: transitionIcon
            playing: ((transitionIcon.parent as EchoContentLoader)?.owner as EchoIconButton)?.glyph === "pause"
            covered: ((transitionIcon.parent as EchoContentLoader)?.owner as EchoIconButton)?.iconCovered ?? false
            color: {
                const owner = ((transitionIcon.parent as EchoContentLoader)?.owner as EchoIconButton);
                return owner ? owner.visualForeground : Qt.rgba(0, 0, 0, 0);
            }
            playOffset: ((transitionIcon.parent as EchoContentLoader)?.owner as EchoIconButton)?.playIconOffset ?? 0
            visible: ((transitionIcon.parent as EchoContentLoader)?.owner as EchoIconButton)?.glyph === "play" || ((transitionIcon.parent as EchoContentLoader)?.owner as EchoIconButton)?.glyph === "pause"
        }
    }
    readonly property Component iconButtonBackground: Component {
        Rectangle {
            id: plate
            radius: ((plate.parent as EchoContentLoader)?.owner as EchoIconButton)?.publicPart.value("radius", Theme.controlRadius) ?? Theme.controlRadius
            color: ((plate.parent as EchoContentLoader)?.owner as EchoIconButton)?.publicPart.value("background", ((plate.parent as EchoContentLoader)?.owner as EchoIconButton)?.hoverBackground && ((plate.parent as EchoContentLoader)?.owner as EchoIconButton)?.hoverHighlighted ? ((plate.parent as EchoContentLoader)?.owner as EchoIconButton).colors.hover : "transparent") ?? Qt.rgba(0, 0, 0, 0)
            border.width: ((plate.parent as EchoContentLoader)?.owner as EchoIconButton)?.publicPart.value("borderWidth", ((plate.parent as EchoContentLoader)?.owner as EchoIconButton)?.visualFocus ? 2 : 0) ?? 0
            border.color: ((plate.parent as EchoContentLoader)?.owner as EchoIconButton)?.publicPart.value("borderColor", Theme.accent) ?? Theme.accent
        }
    }
    function contentWidth(item: Item): real {
        const label = item as LabelText;
        return label ? label.naturalWidth : item ? item.implicitWidth : 0;
    }
    function contentHeight(item: Item, wrap: bool, small: bool): real {
        const label = item as LabelText;
        const minimum = small ? 22 : 18;
        return wrap && label ? Math.max(minimum, label.contentHeight + 4) : minimum;
    }
    readonly property Component text: Component {
        LabelText {}
    }
    component LabelText: Text {
        id: label
        readonly property real naturalWidth: implicitWidth
        height: implicitHeight
        y: {
            const surface = (label.parent as EchoContentLoader)?.owner as EchoLabelSurface;
            return !surface ? 0 : surface.wrap ? (surface.height - height) / 2 : ink.hasInk ? surface.height / 2 - baselineOffset - ink.inkCenterFromBaseline : (surface.height - height) / 2;
        }
        text: ((label.parent as EchoContentLoader)?.owner as EchoLabelSurface)?.text ?? ""
        textFormat: Text.PlainText
        color: {
            const owner = (label.parent as EchoContentLoader)?.owner as EchoLabelSurface;
            return owner ? owner.paintedForeground : Qt.rgba(0, 0, 0, 0);
        }
        font: {
            const owner = (label.parent as EchoContentLoader)?.owner as EchoLabelSurface;
            return owner ? owner.paintedFont : Theme.font(10, Theme.semiboldWeight);
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        wrapMode: ((label.parent as EchoContentLoader)?.owner as EchoLabelSurface)?.wrap ? Text.WrapAnywhere : Text.NoWrap
        lineHeightMode: ((label.parent as EchoContentLoader)?.owner as EchoLabelSurface)?.wrap ? Text.FixedHeight : Text.ProportionalHeight
        lineHeight: ((label.parent as EchoContentLoader)?.owner as EchoLabelSurface)?.wrap ? (((label.parent as EchoContentLoader)?.owner as EchoLabelSurface)?.small ? 11 : 10) * 1.4 : 1
        EchoTextInkMetrics {
            id: ink
            font: label.font
            text: label.text
        }
    }
}
