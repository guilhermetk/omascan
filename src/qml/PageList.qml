pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// The pages down the left: numbered thumbnails, in document order. Drag one
// to move it; right-click for the rest.
Rectangle {
    id: nav
    color: Theme.panelBg

    property int dragFrom: -1
    property int dragTo: -1

    function positionOf(index) { list.positionViewAtIndex(index, ListView.Contain) }
    Connections {
        target: Pages
        function onCurrentChanged() { if (Pages.current >= 0) nav.positionOf(Pages.current) }
    }

    Rectangle {
        anchors.right: parent.right
        width: Theme.hairline
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: Theme.hairline
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.hNavHeader
            Layout.leftMargin: Theme.s3
            Layout.rightMargin: Theme.s2
            spacing: Theme.s2
            Label { text: qsTr("Pages"); font.weight: Theme.wHeading; font.pixelSize: Theme.fsSection }
            Label {
                visible: Pages.count > 0
                text: Pages.count
                color: Theme.textMuted
            }
            Item { Layout.fillWidth: true }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Pages
            currentIndex: Pages.current
            spacing: Theme.s3
            topMargin: Theme.s1
            bottomMargin: Theme.s4
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: false
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: row
                required property int index
                required property url imageUrl
                required property int pixelWidth
                required property int pixelHeight

                readonly property bool current: index === Pages.current
                readonly property real thumbWidth: list.width - number.width - Theme.s3 - Theme.s3
                readonly property real thumbHeight: Math.min(thumbWidth * 1.6,
                    thumbWidth * (pixelWidth > 0 ? pixelHeight / pixelWidth : 1.414))

                width: list.width
                height: thumbHeight
                opacity: nav.dragFrom === index ? 0.4 : 1

                Label {
                    id: number
                    x: Theme.s2
                    width: 20
                    text: row.index + 1
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Theme.fsLabel
                    color: row.current ? Theme.accent : Theme.textMuted
                    font.weight: row.current ? Theme.wHeading : Theme.wNormal
                }

                Rectangle {
                    id: frame
                    x: number.x + number.width + Theme.s2
                    width: row.thumbWidth
                    height: row.thumbHeight
                    radius: Theme.rHandle
                    color: "white"
                    border.width: row.current ? Theme.selectionRing : Theme.hairline
                    border.color: row.current ? Theme.accent : hover.hovered ? Theme.borderStrong : Theme.border

                    PageImage {
                        anchors.fill: parent
                        anchors.margins: row.current ? Theme.selectionRing : Theme.hairline
                        source: row.imageUrl
                        requestedEdge: 480
                    }
                    HoverHandler { id: hover }
                }

                // Where a dragged page would land.
                Rectangle {
                    visible: nav.dragFrom >= 0 && nav.dragTo === row.index && nav.dragFrom !== row.index
                    x: frame.x
                    width: frame.width
                    height: 3
                    radius: 1.5
                    y: nav.dragTo < nav.dragFrom ? -Theme.s2 : row.height + Theme.s2 - height
                    color: Theme.accent
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property point pressedAt
                    property bool dragging: false
                    onPressed: (mouse) => {
                        pressedAt = Qt.point(mouse.x, mouse.y)
                        Pages.current = row.index
                        list.forceActiveFocus()
                        if (mouse.button === Qt.RightButton)
                            pageMenu.popup()
                    }
                    onPositionChanged: (mouse) => {
                        if (!(mouse.buttons & Qt.LeftButton))
                            return
                        if (!dragging && Math.abs(mouse.y - pressedAt.y) > 6) {
                            dragging = true
                            nav.dragFrom = row.index
                        }
                        if (dragging) {
                            const p = mapToItem(list.contentItem, mouse.x, mouse.y)
                            let target = list.indexAt(list.width / 2, p.y)
                            if (target < 0)
                                target = p.y < 0 ? 0 : Pages.count - 1
                            nav.dragTo = target
                            // Scroll when dragging past the edges.
                            const inView = mapToItem(list, mouse.x, mouse.y).y
                            if (inView < 24) list.contentY = Math.max(list.originY, list.contentY - 12)
                            else if (inView > list.height - 24)
                                list.contentY = Math.min(list.contentHeight - list.height + list.originY, list.contentY + 12)
                        }
                    }
                    onReleased: {
                        if (dragging && nav.dragTo >= 0 && nav.dragTo !== nav.dragFrom)
                            Pages.move(nav.dragFrom, nav.dragTo)
                        dragging = false
                        nav.dragFrom = -1
                        nav.dragTo = -1
                    }
                    onCanceled: { dragging = false; nav.dragFrom = -1; nav.dragTo = -1 }
                }
            }

            // A page on its way: while the scanner works, its place in the list.
            footer: Item {
                width: list.width
                height: Scanner.scanning ? placeholder.height + Theme.s3 : 0
                visible: Scanner.scanning
                Rectangle {
                    id: placeholder
                    y: Theme.s3
                    x: Theme.s2 + 20 + Theme.s2
                    width: list.width - x - Theme.s3
                    height: width * 1.3
                    radius: Theme.rHandle
                    color: Theme.withAlpha(Theme.accent, 0.06)
                    border.width: Theme.hairline
                    border.color: Theme.withAlpha(Theme.accent, 0.5)
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.s3
                        spacing: Theme.s2
                        Icon {
                            Layout.alignment: Qt.AlignHCenter
                            name: "scan-line"
                            size: Theme.szIconLarge
                            color: Theme.accent
                            SequentialAnimation on opacity {
                                running: Scanner.scanning
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.35; duration: 700; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
                            }
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            indeterminate: Scanner.progress < 0
                            value: Math.max(0, Scanner.progress)
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: pageMenu
        MenuItem { text: qsTr("Rotate left"); icon.name: "rotate-ccw"; onTriggered: Pages.rotate(Pages.current, -90) }
        MenuItem { text: qsTr("Rotate right"); icon.name: "rotate-cw"; onTriggered: Pages.rotate(Pages.current, 90) }
        MenuItem { text: qsTr("Duplicate"); icon.name: "copy"; onTriggered: Pages.duplicate(Pages.current) }
        MenuSeparator {}
        MenuItem { text: qsTr("Move up"); icon.name: "arrow-up"; enabled: Pages.current > 0; onTriggered: Pages.move(Pages.current, Pages.current - 1) }
        MenuItem { text: qsTr("Move down"); icon.name: "arrow-down"; enabled: Pages.current < Pages.count - 1; onTriggered: Pages.move(Pages.current, Pages.current + 1) }
        MenuSeparator {}
        MenuItem { text: qsTr("Delete page"); icon.name: "trash"; onTriggered: Pages.remove(Pages.current) }
    }
}
