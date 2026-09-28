import QtQuick
import QtQuick.Controls
import Omascan 1.0

// One key, drawn as a key: for the shortcuts sheet and empty-state hints.
Rectangle {
    property alias text: label.text
    implicitWidth: Math.max(Theme.szKey, label.implicitWidth + Theme.s2 * 2)
    implicitHeight: Theme.szKey
    radius: Theme.rControl
    color: Theme.controlBg
    border.width: Theme.hairline
    border.color: Theme.borderStrong
    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: Theme.fsLabel
        color: Theme.textSecondary
    }
}
