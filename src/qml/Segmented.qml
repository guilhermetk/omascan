pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// A row (or grid) of mutually exclusive choices in one well. The chosen one
// takes the accent tint, as a checked Button does.
Rectangle {
    id: root
    property var options: []      // [{ text, icon?, tip? }]
    property int current: 0
    property int columns: options.length
    signal activated(int index)

    implicitHeight: grid.implicitHeight + 4
    implicitWidth: grid.implicitWidth + 4
    radius: Theme.rControl
    color: Theme.withAlpha(Theme.controlBg, 0.55)
    border.width: Theme.hairline
    border.color: Theme.border

    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.margins: 2
        columns: root.columns
        rowSpacing: 2
        columnSpacing: 2
        Repeater {
            model: root.options
            delegate: Button {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: Theme.hControl - 2
                flat: true
                checked: root.current === index
                text: modelData.text
                icon.name: modelData.icon ?? ""
                ToolTip.visible: hovered && (modelData.tip ?? "") !== ""
                ToolTip.text: modelData.tip ?? ""
                onClicked: root.activated(index)
            }
        }
    }
}
