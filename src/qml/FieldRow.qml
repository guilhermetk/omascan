import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// Inspector row: a label in a fixed left column, the control filling the rest.
RowLayout {
    property alias label: caption.text
    Layout.fillWidth: true
    spacing: Theme.s2
    Label {
        id: caption
        Layout.preferredWidth: Theme.wFieldLabel
        Layout.alignment: Qt.AlignVCenter
        color: Theme.textSecondary
        elide: Text.ElideRight
    }
}
