pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick
import EchoMusic.Controls

// Shared locate behavior for virtual lists. Cached delegates are not
// necessarily visible, and page headers can clip the top of the viewport.
QtObject {
    function positionIfNeeded(view: ListView, index: int, mode: int, container: EchoPageScrollContainer): void {
        if (!view || !view.visible || index < 0 || index >= view.count || view.height <= 0)
            return;
        const top = container ? Math.max(0, view.mapFromItem(container, 0, container.occlusionHeight).y) : 0;
        const bottom = container ? Math.min(view.height, view.mapFromItem(container, 0, container.height).y) : view.height;
        if (bottom <= top)
            return;
        const row = view.itemAtIndex(index);
        if (row && row.visible) {
            const rowTop = row.mapToItem(view, 0, 0).y;
            // Coordinate transforms at fractional application scales can put
            // an exact boundary a few floating-point bits outside the viewport.
            const epsilon = 0.001;
            if (rowTop + epsilon >= top && rowTop + row.height <= bottom + epsilon)
                return;
        }
        view.positionViewAtIndex(index, mode);
        if (container && container.scrollView === view)
            container.commitPosition();
    }
}
