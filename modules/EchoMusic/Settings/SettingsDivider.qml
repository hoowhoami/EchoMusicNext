pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import EchoMusic.Theme

Rectangle {
    id: divider
    Layout.fillWidth: true
    implicitHeight: 1
    color: Theme.withAlpha(Theme.floatingText, 0.12)
    readonly property real dpr: Screen.devicePixelRatio
    // 分数缩放（如 1.25）下 1 逻辑像素线落在不同物理像素网格位置：
    // 有的恰好 1 物理像素，有的跨 2 像素混合——粗细随位置漂移。把
    // 渲染位置吸附到整物理像素网格，使所有分割线粗细一致。
    transform: Translate {
        id: snapOffset
        y: 0
    }
    function snapToPixel(): void {
        const dpr = divider.dpr;
        if (dpr <= 0 || Math.abs(dpr - Math.round(dpr)) < 0.001)
            return;
        const sceneY = divider.mapToItem(null, 0, 0).y;
        const base = sceneY - snapOffset.y;
        const target = Math.round(base * dpr) / dpr;
        snapOffset.y = target - base;
    }
    onYChanged: snapToPixel()
    onVisibleChanged: snapToPixel()
    Component.onCompleted: snapToPixel()
}
