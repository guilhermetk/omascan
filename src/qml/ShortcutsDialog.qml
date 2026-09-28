pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// Every key, in two columns.
Dialog {
    id: dialog
    title: qsTr("Keyboard shortcuts")
    modal: true
    width: Math.min(720, parent ? parent.width - 2 * Theme.s5 : 720)

    readonly property var groups: [
        { name: qsTr("Scanning"), keys: [
            [["Ctrl", "↵"], qsTr("Scan")],
            [["Esc"], qsTr("Stop scanning")],
            [["Ctrl", "E"], qsTr("Export")],
            [["Ctrl", "N"], qsTr("New document")] ] },
        { name: qsTr("Pages"), keys: [
            [["↑"], qsTr("Previous page")],
            [["↓"], qsTr("Next page")],
            [["Ctrl", "↑"], qsTr("Move page up")],
            [["Ctrl", "↓"], qsTr("Move page down")],
            [["Ctrl", "D"], qsTr("Duplicate page")],
            [["Del"], qsTr("Delete page")] ] },
        { name: qsTr("Editing"), keys: [
            [["["], qsTr("Rotate left")],
            [["]"], qsTr("Rotate right")],
            [["C"], qsTr("Crop")],
            [["1 – 4"], qsTr("Original, Enhanced, Greyscale, B&W")],
            [["Ctrl", "Z"], qsTr("Undo")],
            [["Ctrl", "⇧", "Z"], qsTr("Redo")] ] },
        { name: qsTr("View"), keys: [
            [["Ctrl", "+"], qsTr("Zoom in")],
            [["Ctrl", "−"], qsTr("Zoom out")],
            [["Ctrl", "0"], qsTr("Fit page")],
            [["Ctrl", "Scroll"], qsTr("Zoom at the pointer")],
            [["?"], qsTr("This list")] ] }
    ]

    contentItem: GridLayout {
        columns: 2
        columnSpacing: Theme.s5 * 2
        rowSpacing: Theme.s4
        Repeater {
            model: dialog.groups
            delegate: ColumnLayout {
                id: group
                required property var modelData
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                spacing: Theme.s2
                SectionLabel { text: group.modelData.name; Layout.topMargin: 0 }
                Repeater {
                    model: group.modelData.keys
                    delegate: RowLayout {
                        id: entry
                        required property var modelData
                        spacing: Theme.s1
                        Label {
                            text: entry.modelData[1]
                            color: Theme.textSecondary
                            Layout.fillWidth: true
                            Layout.minimumWidth: 150
                        }
                        Repeater {
                            model: entry.modelData[0]
                            delegate: KeyCap { required property string modelData; text: modelData }
                        }
                    }
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Close"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
}
