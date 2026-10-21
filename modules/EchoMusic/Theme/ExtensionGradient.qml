pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Shape {
    id: root
    required property var recipe
    readonly property var stops: recipe.stops || [
        {
            position: 0,
            color: recipe.color || "transparent"
        },
        {
            position: 1,
            color: "transparent"
        }
    ]
    readonly property real angle: Number(recipe.angle || 0) * Math.PI / 180
    function stop(index: int): var {
        return stops[Math.min(index, stops.length - 1)] || {
            position: 1,
            color: "transparent"
        };
    }
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        strokeWidth: -1
        fillGradient: LinearGradient {
            x1: root.width / 2 - Math.sin(root.angle) * root.width / 2
            y1: root.height / 2 - Math.cos(root.angle) * root.height / 2
            x2: root.width / 2 + Math.sin(root.angle) * root.width / 2
            y2: root.height / 2 + Math.cos(root.angle) * root.height / 2
            GradientStop {
                position: root.stop(0).position
                color: root.stop(0).color
            }
            GradientStop {
                position: root.stop(1).position
                color: root.stop(1).color
            }
            GradientStop {
                position: root.stop(2).position
                color: root.stop(2).color
            }
            GradientStop {
                position: root.stop(3).position
                color: root.stop(3).color
            }
            GradientStop {
                position: root.stop(4).position
                color: root.stop(4).color
            }
            GradientStop {
                position: root.stop(5).position
                color: root.stop(5).color
            }
            GradientStop {
                position: root.stop(6).position
                color: root.stop(6).color
            }
            GradientStop {
                position: root.stop(7).position
                color: root.stop(7).color
            }
        }
        startX: 0
        startY: 0
        PathLine {
            x: root.width
            y: 0
        }
        PathLine {
            x: root.width
            y: root.height
        }
        PathLine {
            x: 0
            y: root.height
        }
        PathLine {
            x: 0
            y: 0
        }
    }
}
