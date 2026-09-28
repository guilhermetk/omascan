import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// The right-hand panel: how the next scan is taken, and how this page looks.
Rectangle {
    id: inspector
    color: Theme.panelBg
    property var page: ({})
    readonly property bool hasPage: Pages.count > 0 && page.pageId !== undefined
    signal scanRequested()

    Rectangle { width: Theme.hairline; height: parent.height; color: Theme.border }

    // Sliders commit on a short timer so a drag doesn't queue a render per pixel.
    Timer {
        id: adjustTimer
        interval: 60
        property string name
        property int value
        onTriggered: Pages.setAdjustment(Pages.current, name, value)
    }
    function adjust(name, value) {
        if (adjustTimer.running && adjustTimer.name !== name)
            Pages.setAdjustment(Pages.current, adjustTimer.name, adjustTimer.value)
        adjustTimer.name = name
        adjustTimer.value = value
        adjustTimer.restart()
    }

    component AdjustRow: FieldRow {
        id: adjustRow
        property string name
        property int value
        property int from: -100
        property int to: 100
        property real origin: 0.5
        Slider {
            id: slider
            Layout.fillWidth: true
            from: adjustRow.from
            to: adjustRow.to
            stepSize: 1
            origin: adjustRow.origin
            value: adjustRow.value
            onPressedChanged: if (pressed) Pages.checkpoint(qsTr("Adjust %1").arg(adjustRow.label.toLowerCase()))
            onMoved: inspector.adjust(adjustRow.name, Math.round(value))
        }
        Label {
            Layout.preferredWidth: 30
            horizontalAlignment: Text.AlignRight
            text: Math.round(slider.value)
            color: Theme.textSecondary
            font.features: { "tnum": 1 }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.leftMargin: Theme.hairline
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.s2

            // ── Scanner ─────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s3
                Layout.preferredHeight: Theme.hNavHeader
                Label { text: qsTr("Scanner"); font.weight: Theme.wHeading; font.pixelSize: Theme.fsSection }
                Item { Layout.fillWidth: true }
                ToolButton {
                    visible: Scanner.available
                    icon.name: "refresh-cw"
                    enabled: !Scanner.discovering && !Scanner.scanning
                    onClicked: Scanner.refresh()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Look for scanners again")
                    RotationAnimation on rotation {
                        running: Scanner.discovering
                        from: 0; to: 360; duration: 1000; loops: Animation.Infinite
                        onStopped: parent.rotation = 0
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s4
                spacing: Theme.s2

                // No SANE, or no device: say what to do about it.
                Rectangle {
                    visible: !Scanner.available || (!Scanner.discovering && Scanner.devices.length === 0)
                    Layout.fillWidth: true
                    implicitHeight: notice.implicitHeight + 2 * Theme.s3
                    radius: Theme.rCard
                    color: Theme.withAlpha(Theme.warning, 0.08)
                    border.width: Theme.hairline
                    border.color: Theme.withAlpha(Theme.warning, 0.35)
                    RowLayout {
                        id: notice
                        anchors.fill: parent
                        anchors.margins: Theme.s3
                        spacing: Theme.s2
                        Icon { name: "triangle-alert"; color: Theme.warning; Layout.alignment: Qt.AlignTop }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsLabel
                            text: !Scanner.available
                                ? qsTr("SANE isn't installed. Install sane (and sane-airscan for network scanners), then restart OmaScan.")
                                : qsTr("No scanner found. USB scanners need to be switched on and plugged in; network scanners need sane-airscan.")
                        }
                    }
                }

                FieldRow {
                    label: qsTr("Device")
                    visible: Scanner.available
                    ComboBox {
                        Layout.fillWidth: true
                        model: Scanner.devices
                        textRole: "name"
                        enabled: !Scanner.scanning && Scanner.devices.length > 0
                        currentIndex: Scanner.deviceIndex
                        displayText: Scanner.discovering ? qsTr("Looking…")
                                   : Scanner.devices.length === 0 ? qsTr("None found") : currentText
                        onActivated: (index) => Scanner.deviceIndex = index
                    }
                }
                FieldRow {
                    label: qsTr("Source")
                    visible: Scanner.sources.length > 1
                    ComboBox {
                        Layout.fillWidth: true
                        model: Scanner.sources
                        enabled: !Scanner.scanning
                        currentIndex: Scanner.sources.indexOf(Scanner.source)
                        onActivated: (index) => Scanner.source = Scanner.sources[index]
                    }
                }
                FieldRow {
                    label: qsTr("Colour")
                    visible: Scanner.modes.length > 0
                    ComboBox {
                        Layout.fillWidth: true
                        model: Scanner.modes
                        enabled: !Scanner.scanning
                        currentIndex: Scanner.modes.indexOf(Scanner.mode)
                        onActivated: (index) => Scanner.mode = Scanner.modes[index]
                    }
                }
                FieldRow {
                    label: qsTr("Resolution")
                    visible: Scanner.resolutions.length > 0
                    ComboBox {
                        Layout.fillWidth: true
                        model: Scanner.resolutions.map(r => r + " dpi")
                        enabled: !Scanner.scanning
                        currentIndex: Scanner.resolutions.indexOf(Scanner.resolution)
                        onActivated: (index) => Scanner.resolution = Scanner.resolutions[index]
                    }
                }
                FieldRow {
                    label: qsTr("Paper")
                    visible: Scanner.paperSizes.length > 1
                    ComboBox {
                        Layout.fillWidth: true
                        model: Scanner.paperSizes
                        enabled: !Scanner.scanning
                        currentIndex: Scanner.paperSizes.indexOf(Scanner.paperSize)
                        onActivated: (index) => Scanner.paperSize = Scanner.paperSizes[index]
                    }
                }

                Button {
                    visible: Scanner.available && Scanner.devices.length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.s2
                    implicitHeight: Theme.hHeroButton - 4
                    property bool primary: !Scanner.scanning
                    text: Scanner.scanning ? qsTr("Stop scanning")
                        : Scanner.feeder ? qsTr("Scan from feeder") : qsTr("Scan page")
                    icon.name: Scanner.scanning ? "circle-stop" : "scan-line"
                    enabled: Scanner.ready || Scanner.scanning
                    onClicked: Scanner.scanning ? Scanner.cancel() : inspector.scanRequested()
                }
            }

            // ── Page ────────────────────────────────────────────────────────
            Rectangle {
                visible: inspector.hasPage
                Layout.fillWidth: true
                Layout.topMargin: Theme.s3
                implicitHeight: Theme.hairline
                color: Theme.border
            }

            RowLayout {
                visible: inspector.hasPage
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s3
                Layout.preferredHeight: Theme.hNavHeader
                Label {
                    text: qsTr("Page %1").arg(Pages.current + 1)
                    font.weight: Theme.wHeading
                    font.pixelSize: Theme.fsSection
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    icon.name: "rotate-ccw"
                    onClicked: Pages.rotate(Pages.current, -90)
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Rotate left ([)")
                }
                ToolButton {
                    icon.name: "rotate-cw"
                    onClicked: Pages.rotate(Pages.current, 90)
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Rotate right (])")
                }
            }

            ColumnLayout {
                visible: inspector.hasPage
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s4
                spacing: Theme.s2

                SectionLabel { text: qsTr("Look"); Layout.topMargin: 0 }
                Segmented {
                    Layout.fillWidth: true
                    columns: 2
                    current: inspector.page.filter ?? 1
                    options: [
                        { text: qsTr("Original"), tip: qsTr("As scanned (1)") },
                        { text: qsTr("Enhanced"), tip: qsTr("White paper, crisp ink (2)") },
                        { text: qsTr("Greyscale"), tip: qsTr("No colour (3)") },
                        { text: qsTr("Black & white"), tip: qsTr("Smallest files; best for text (4)") }
                    ]
                    onActivated: (index) => Pages.setFilter(Pages.current, index)
                }

                SectionLabel { text: qsTr("Adjust") }
                AdjustRow { label: qsTr("Brightness"); name: "brightness"; value: inspector.page.brightness ?? 0 }
                AdjustRow { label: qsTr("Contrast"); name: "contrast"; value: inspector.page.contrast ?? 0 }
                AdjustRow {
                    visible: inspector.page.filter === 3
                    label: qsTr("Threshold")
                    name: "threshold"
                    from: 0; to: 100; origin: 0
                    value: inspector.page.threshold ?? 50
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s2
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Reset")
                        enabled: (inspector.page.brightness ?? 0) !== 0 || (inspector.page.contrast ?? 0) !== 0
                                 || (inspector.page.threshold ?? 50) !== 50
                        onClicked: Pages.resetAdjustments(Pages.current)
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("Use on all pages")
                        enabled: Pages.count > 1
                        onClicked: Pages.applyLookToAll(Pages.current)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Give every page this look and these adjustments")
                    }
                }

                SectionLabel { text: qsTr("Size") }
                FieldRow {
                    label: qsTr("Pixels")
                    Label {
                        Layout.fillWidth: true
                        text: (inspector.page.pixelWidth ?? 0) + " × " + (inspector.page.pixelHeight ?? 0)
                        color: Theme.textSecondary
                        font.features: { "tnum": 1 }
                    }
                }
                FieldRow {
                    label: qsTr("Printed")
                    visible: (inspector.page.dpi ?? 0) > 0
                    Label {
                        Layout.fillWidth: true
                        readonly property real dpi: inspector.page.dpi ?? 0
                        text: dpi > 0 ? qsTr("%1 × %2 mm at %3 dpi")
                                        .arg(Math.round(inspector.page.pixelWidth / dpi * 25.4))
                                        .arg(Math.round(inspector.page.pixelHeight / dpi * 25.4))
                                        .arg(dpi) : ""
                        color: Theme.textSecondary
                        font.features: { "tnum": 1 }
                    }
                }
                Button {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.s1
                    text: qsTr("Remove crop")
                    icon.name: "crop"
                    visible: inspector.page.crop !== undefined
                             && (inspector.page.crop.width < 0.999 || inspector.page.crop.height < 0.999)
                    onClicked: Pages.resetCrop(Pages.current)
                }
                Item { Layout.preferredHeight: Theme.s4 }
            }
        }
    }
}
