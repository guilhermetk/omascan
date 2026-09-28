import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

// A menu row: optional icon, label, and the shortcut in muted text on the right.
T.MenuItem {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: Theme.s1
    leftPadding: Theme.s2 + 2
    rightPadding: Theme.s3
    spacing: Theme.s2
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    opacity: enabled ? 1 : Theme.disabledOpacity

    readonly property string shortcutText: action ? Keys.text(action.shortcut) : ""

    contentItem: Item {
        implicitWidth: Theme.szIcon + control.spacing + label.implicitWidth
                       + (keys.text !== "" ? Theme.s5 + keys.implicitWidth : 0)
        implicitHeight: Math.max(label.implicitHeight, Theme.szIcon)
        Icon {
            id: glyph
            name: control.checkable ? (control.checked ? "check" : "") : control.icon.name
            size: Theme.szIcon
            color: control.checked ? Theme.accent : control.highlighted ? Theme.textPrimary : Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: label
            x: glyph.width + control.spacing
            text: control.text
            textFormat: Text.PlainText
            font: control.font
            color: control.highlighted ? Theme.textPrimary : Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: keys
            text: control.shortcutText
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
            color: Theme.textMuted
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    arrow: Icon {
        visible: control.subMenu
        name: "chevron-right"
        x: control.width - width - Theme.s2
        y: (control.height - height) / 2
        color: Theme.textMuted
    }
    background: Rectangle {
        implicitHeight: Theme.hRow
        radius: Theme.rControl
        color: control.down ? Theme.pressedOn(Theme.controlHover)
             : control.highlighted ? Theme.controlHover : "transparent"
    }
}
