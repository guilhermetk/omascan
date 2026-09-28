pragma ComponentBehavior: Bound
import QtQuick
import Omascan 1.0

// The crop editor, laid over the whole (uncropped) page. The rectangle is in
// unit coordinates of the rotated picture, which is what the model stores.
Item {
    id: root
    // Working copy; applied with Enter or Done.
    property real x0: 0
    property real y0: 0
    property real x1: 1
    property real y1: 1
    readonly property real minSize: 0.04

    function load(rect) {
        x0 = rect.x; y0 = rect.y
        x1 = rect.x + rect.width; y1 = rect.y + rect.height
    }
    function reset() { x0 = 0; y0 = 0; x1 = 1; y1 = 1 }

    readonly property rect area: Qt.rect(x0 * width, y0 * height, (x1 - x0) * width, (y1 - y0) * height)

    // Shade what will be cut away.
    Rectangle { color: Theme.cropShade; x: 0; y: 0; width: parent.width; height: root.area.y }
    Rectangle { color: Theme.cropShade; x: 0; y: root.area.y + root.area.height; width: parent.width; height: parent.height - y }
    Rectangle { color: Theme.cropShade; x: 0; y: root.area.y; width: root.area.x; height: root.area.height }
    Rectangle { color: Theme.cropShade; x: root.area.x + root.area.width; y: root.area.y; width: parent.width - x; height: root.area.height }

    Rectangle {
        id: frame
        x: root.area.x; y: root.area.y
        width: root.area.width; height: root.area.height
        color: "transparent"
        border.width: Theme.selectionRing
        border.color: Theme.accent

        // Thirds, to square things up by eye.
        Repeater {
            model: 2
            Rectangle {
                required property int index
                x: frame.width * (index + 1) / 3; width: Theme.hairline; height: frame.height
                color: Theme.withAlpha(Theme.accent, 0.35)
            }
        }
        Repeater {
            model: 2
            Rectangle {
                required property int index
                y: frame.height * (index + 1) / 3; height: Theme.hairline; width: frame.width
                color: Theme.withAlpha(Theme.accent, 0.35)
            }
        }

        // Drag the middle to move the whole rectangle.
        MouseArea {
            anchors.fill: parent
            anchors.margins: Theme.szHandle
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point last
            onPressed: (mouse) => last = mapToItem(root, mouse.x, mouse.y)
            onPositionChanged: (mouse) => {
                const p = mapToItem(root, mouse.x, mouse.y)
                let dx = (p.x - last.x) / root.width
                let dy = (p.y - last.y) / root.height
                dx = Math.max(-root.x0, Math.min(1 - root.x1, dx))
                dy = Math.max(-root.y0, Math.min(1 - root.y1, dy))
                root.x0 += dx; root.x1 += dx
                root.y0 += dy; root.y1 += dy
                last = p
            }
        }
    }

    // Eight handles: corners move two edges, sides move one.
    Repeater {
        model: [
            { h: -1, v: -1, cursor: Qt.SizeFDiagCursor }, { h: 0, v: -1, cursor: Qt.SizeVerCursor },
            { h: 1, v: -1, cursor: Qt.SizeBDiagCursor }, { h: 1, v: 0, cursor: Qt.SizeHorCursor },
            { h: 1, v: 1, cursor: Qt.SizeFDiagCursor }, { h: 0, v: 1, cursor: Qt.SizeVerCursor },
            { h: -1, v: 1, cursor: Qt.SizeBDiagCursor }, { h: -1, v: 0, cursor: Qt.SizeHorCursor }
        ]
        delegate: Rectangle {
            id: handle
            required property var modelData
            width: Theme.szHandle
            height: Theme.szHandle
            radius: Theme.rHandle
            color: Theme.accent
            border.width: Theme.hairline
            border.color: Theme.accentText
            x: root.area.x + (modelData.h + 1) / 2 * root.area.width - width / 2
            y: root.area.y + (modelData.v + 1) / 2 * root.area.height - height / 2

            MouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.s2
                cursorShape: handle.modelData.cursor
                onPositionChanged: (mouse) => {
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const nx = Math.max(0, Math.min(1, p.x / root.width))
                    const ny = Math.max(0, Math.min(1, p.y / root.height))
                    if (handle.modelData.h < 0) root.x0 = Math.min(nx, root.x1 - root.minSize)
                    if (handle.modelData.h > 0) root.x1 = Math.max(nx, root.x0 + root.minSize)
                    if (handle.modelData.v < 0) root.y0 = Math.min(ny, root.y1 - root.minSize)
                    if (handle.modelData.v > 0) root.y1 = Math.max(ny, root.y0 + root.minSize)
                }
            }
        }
    }
}
