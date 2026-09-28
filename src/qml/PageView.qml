import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// The middle of the window: the current page lying on the pasteboard, or the
// way in when there are no pages yet.
Rectangle {
    id: view
    color: Theme.pasteboard

    property var page: ({})
    property bool cropping: false
    signal scanRequested()
    signal cropFinished()

    // 1 fits the page to the window; more zooms in from there.
    property real zoomFactor: 1
    readonly property var zoomSteps: [1, 1.5, 2, 3, 4, 6, 8]
    readonly property bool hasPage: Pages.count > 0 && page.pageId !== undefined
    readonly property real pageW: cropping ? (page.fullWidth || 1) : (page.pixelWidth || 1)
    readonly property real pageH: cropping ? (page.fullHeight || 1) : (page.pixelHeight || 1)
    // Room kept free under the page for the crop bar, so it never covers a handle.
    readonly property real barRoom: cropping ? Theme.hHeroButton + Theme.s4 : 0
    readonly property real fitScale: Math.min((width - 2 * Theme.pagePadding) / pageW,
                                              (height - 2 * Theme.pagePadding - barRoom) / pageH)
    readonly property real pageScale: Math.max(0.01, fitScale * zoomFactor)
    readonly property int zoomPercent: Math.round(pageScale * 100)

    // Where the paper sits in the flickable's content at a given zoom. The
    // same arithmetic as the bindings below, so a zoom can be worked out
    // before it happens.
    function layoutAt(factor) {
        const s = Math.max(0.01, fitScale * factor)
        const w = Math.round(pageW * s), h = Math.round(pageH * s)
        const cw = Math.max(flick.width, pageW * s + 2 * Theme.pagePadding)
        const ch = Math.max(flick.height, pageH * s + 2 * Theme.pagePadding)
        return { x: Math.round((cw - w) / 2), y: Math.round((ch - barRoom - h) / 2), w: w, h: h, cw: cw, ch: ch }
    }

    // Zoom so that the spot under `anchor` (a point in this view) stays put:
    // the pointer for the wheel, the middle of the view for keys and buttons.
    function zoomTo(factor, anchor) {
        factor = Math.max(zoomSteps[0], Math.min(zoomSteps[zoomSteps.length - 1], factor))
        if (Math.abs(factor - zoomFactor) < 0.001)
            return
        const at = anchor ?? Qt.point(flick.width / 2, flick.height / 2)
        const before = layoutAt(zoomFactor)
        const u = (flick.contentX + at.x - before.x) / before.w
        const v = (flick.contentY + at.y - before.y) / before.h
        const after = layoutAt(factor)
        zoomFactor = factor
        flick.contentX = Math.max(0, Math.min(after.cw - flick.width, after.x + u * after.w - at.x))
        flick.contentY = Math.max(0, Math.min(after.ch - flick.height, after.y + v * after.h - at.y))
    }
    function zoomIn(anchor) {
        for (const s of zoomSteps) if (s > zoomFactor + 0.01) { zoomTo(s, anchor); return }
    }
    function zoomOut(anchor) {
        for (let i = zoomSteps.length - 1; i >= 0; --i)
            if (zoomSteps[i] < zoomFactor - 0.01) { zoomTo(zoomSteps[i], anchor); return }
    }
    function zoomFit() { zoomTo(1) }

    function beginCrop() {
        if (!hasPage) return
        zoomFit()
        crop.load(page.crop)
        cropping = true
    }
    function applyCrop() {
        if (!cropping) return
        Pages.setCrop(Pages.current, crop.x0, crop.y0, crop.x1 - crop.x0, crop.y1 - crop.y0)
        cropping = false
        cropFinished()
    }
    function cancelCrop() { cropping = false; cropFinished() }

    // A different page, or the same page changed under the editor, ends cropping.
    onPageChanged: if (cropping && page.pageId !== cropId) cancelCrop()
    property var cropId
    onCroppingChanged: cropId = cropping ? page.pageId : undefined

    Flickable {
        id: flick
        anchors.fill: parent
        visible: view.hasPage
        clip: true
        contentWidth: Math.max(width, view.pageW * view.pageScale + 2 * Theme.pagePadding)
        contentHeight: Math.max(height, view.pageH * view.pageScale + 2 * Theme.pagePadding)
        interactive: view.zoomFactor > 1 && !view.cropping
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        Item {
            id: sheet
            width: flick.contentWidth
            height: flick.contentHeight

            // A soft shadow, drawn with plain rectangles so it also renders
            // offscreen where shader effects do not.
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    x: paper.x - index * 2 + 2
                    y: paper.y - index * 2 + 5
                    width: paper.width + index * 4
                    height: paper.height + index * 4
                    radius: Theme.rHandle + index * 2
                    color: Theme.withAlpha(Theme.paperShadow, 0.12)
                }
            }

            Rectangle {
                id: paper
                width: Math.round(view.pageW * view.pageScale)
                height: Math.round(view.pageH * view.pageScale)
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - view.barRoom - height) / 2)
                color: "white"

                PageImage {
                    id: picture
                    anchors.fill: parent
                    source: view.hasPage ? (view.cropping ? view.page.fullUrl : view.page.imageUrl) : ""
                    requestedEdge: {
                        const edge = Math.max(paper.width, paper.height) * Screen.devicePixelRatio
                        return Math.min(6000, Math.ceil(edge / 400) * 400)
                    }
                }

                CropOverlay {
                    id: crop
                    anchors.fill: parent
                    visible: view.cropping
                }
            }
        }

        // Ctrl + wheel zooms at the pointer. It sits on the content so it hears
        // the wheel before the Flickable scrolls with it. Deltas add up to one
        // step per notch (120), so a touchpad or smooth wheel, which send many
        // small deltas, doesn't race through every zoom level at once.
        //
        // WheelHandler takes only mouse wheels unless told otherwise, and on
        // Wayland Qt reports scrolling as coming from a touchpad, even from a
        // mouse wheel. Without TouchPad here, Ctrl + wheel does nothing.
        WheelHandler {
            id: wheelZoom
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier
            property real pending: 0
            onWheel: (event) => {
                const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
                if (delta === 0)
                    return
                if (Math.sign(delta) !== Math.sign(pending))
                    pending = 0
                pending += delta
                const at = view.mapFromItem(sheet, point.position.x, point.position.y)
                while (pending >= 120) { pending -= 120; view.zoomIn(at) }
                while (pending <= -120) { pending += 120; view.zoomOut(at) }
            }
        }
    }

    // ── The crop bar ────────────────────────────────────────────────────────
    Rectangle {
        visible: view.cropping
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.s4
        width: cropRow.implicitWidth + 2 * Theme.s2
        height: Theme.hHeroButton
        radius: Theme.rMenu
        color: Theme.panelRaised
        border.width: Theme.hairline
        border.color: Theme.borderStrong
        RowLayout {
            id: cropRow
            anchors.centerIn: parent
            spacing: Theme.s2
            Label { text: qsTr("Drag the edges to crop"); color: Theme.textSecondary; Layout.leftMargin: Theme.s2 }
            ToolSeparator {}
            Button { text: qsTr("Whole page"); flat: true; onClicked: crop.reset() }
            Button { text: qsTr("Cancel"); onClicked: view.cancelCrop() }
            Button { text: qsTr("Crop"); icon.name: "check"; property bool primary: true; onClicked: view.applyCrop() }
        }
    }

    // ── Nothing yet ─────────────────────────────────────────────────────────
    ColumnLayout {
        visible: Pages.count === 0
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.s5, 460)
        spacing: Theme.s3

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.s2
            width: 88
            height: 88
            radius: 18
            color: Theme.withAlpha(Theme.accent, 0.08)
            border.width: Theme.hairline
            border.color: Theme.withAlpha(Theme.accent, 0.35)
            Icon { anchors.centerIn: parent; name: "scan-line"; size: Theme.szIconHero; color: Theme.accent; strokeWidth: 1.75 }
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Scan a document")
            font.pixelSize: Theme.fsHero
            font.weight: Theme.wHeading
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            text: !Scanner.available ? qsTr("OmaScan talks to scanners through SANE, which isn't installed yet. Install it, then open OmaScan again.")
                : Scanner.discovering ? qsTr("Looking for scanners…")
                : Scanner.devices.length === 0 ? qsTr("No scanner found. Check it is switched on and connected, then look again.")
                : Scanner.feeder ? qsTr("Put the pages in the feeder, then scan. Each page appears here as it comes through.")
                : qsTr("Put the page face down on the glass, then scan. Scan again for the next page.")
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.s3
            spacing: Theme.s2
            Button {
                visible: Scanner.available && Scanner.devices.length > 0
                text: Scanner.feeder ? qsTr("Scan from feeder") : qsTr("Scan")
                icon.name: "scan-line"
                property bool primary: true
                enabled: Scanner.ready
                implicitHeight: Theme.hHeroButton
                leftPadding: Theme.s4
                rightPadding: Theme.s4
                onClicked: view.scanRequested()
            }
            Button {
                visible: Scanner.available && Scanner.devices.length === 0 && !Scanner.discovering
                text: qsTr("Look again")
                icon.name: "refresh-cw"
                implicitHeight: Theme.hHeroButton
                onClicked: Scanner.refresh()
            }
        }
        Label {
            visible: !Scanner.available
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.s3
            text: "sudo pacman -S sane sane-airscan"
            font.family: Theme.monoFamily
            color: Theme.textSecondary
            padding: Theme.s2
            leftPadding: Theme.s3
            rightPadding: Theme.s3
            background: Rectangle { color: Theme.controlBg; radius: Theme.rControl; border.width: Theme.hairline; border.color: Theme.border }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.s4
            spacing: Theme.s1
            opacity: 0.8
            KeyCap { text: "Ctrl" }
            KeyCap { text: "↵" }
            Label { text: qsTr("scan"); color: Theme.textMuted; Layout.rightMargin: Theme.s3 }
            KeyCap { text: "?" }
            Label { text: qsTr("all keys"); color: Theme.textMuted }
        }
    }

    // ── Scanning ────────────────────────────────────────────────────────────
    Rectangle {
        visible: Scanner.scanning
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.s4
        width: scanRow.implicitWidth + 2 * Theme.s3
        height: Theme.hHeroButton
        radius: Theme.rMenu
        color: Theme.panelRaised
        border.width: Theme.hairline
        border.color: Theme.withAlpha(Theme.accent, 0.5)
        RowLayout {
            id: scanRow
            anchors.centerIn: parent
            spacing: Theme.s3
            Icon {
                name: "loader-circle"
                color: Theme.accent
                RotationAnimation on rotation { running: Scanner.scanning; from: 0; to: 360; duration: 1000; loops: Animation.Infinite }
            }
            Label {
                text: Scanner.status + (Scanner.progress >= 0 ? "  " + Math.round(Scanner.progress * 100) + "%" : "")
                font.features: { "tnum": 1 }
            }
            ProgressBar {
                Layout.preferredWidth: 140
                indeterminate: Scanner.progress < 0
                value: Math.max(0, Scanner.progress)
            }
            Button { text: qsTr("Stop"); icon.name: "circle-stop"; onClicked: Scanner.cancel() }
        }
    }
}
