import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

// The one button, in four tones:
//   default      outlined well — secondary actions
//   flat         no chrome until hovered — toolbar actions
//   highlighted  accent outline and label — the forward action (Scan, Export)
//   checked      accent tint — an active mode (Crop)
// `icon.name` takes a Lucide name. Opt into a trailing chevron with
// `property bool opensMenu: true`, or a solid accent fill with
// `property bool primary: true`.
T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    readonly property bool iconOnly: display === T.AbstractButton.IconOnly || (text === "" && icon.name !== "")
    readonly property bool accentTone: highlighted || checked
    readonly property bool solid: control["primary"] === true
    readonly property bool menuChevron: control["opensMenu"] === true

    padding: 0
    leftPadding: iconOnly ? Theme.s1 : Theme.s3
    rightPadding: leftPadding
    topPadding: Theme.s1
    bottomPadding: Theme.s1
    spacing: Theme.s2
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    font.weight: solid ? Theme.wHeading : Theme.wNormal
    icon.width: Theme.szIcon
    icon.height: Theme.szIcon
    opacity: enabled ? 1 : Theme.disabledOpacity

    readonly property color ink: solid ? Theme.accentText
                               : accentTone ? Theme.accent
                               : control.flat && !control.hovered ? Theme.textSecondary
                               : Theme.textPrimary

    contentItem: Item {
        readonly property bool hasIcon: control.icon.name !== "" && control.display !== T.AbstractButton.TextOnly
        readonly property bool hasText: control.text !== "" && !control.iconOnly
        readonly property real chevronWidth: control.menuChevron ? Theme.szIcon - 2 + control.spacing : 0
        implicitWidth: (hasIcon ? glyph.width : 0) + (hasIcon && hasText ? control.spacing : 0)
                       + (hasText ? label.implicitWidth : 0) + chevronWidth
        implicitHeight: Math.max(hasIcon ? glyph.height : 0, hasText ? label.implicitHeight : 0)

        Icon {
            id: glyph
            visible: parent.hasIcon
            name: control.icon.name
            size: control.icon.width
            color: control.ink
            x: !parent.hasText ? (parent.width - width) / 2 : (parent.width - parent.implicitWidth) / 2
            y: (parent.height - height) / 2
        }
        Text {
            id: label
            visible: parent.hasText
            text: control.text
            textFormat: Text.PlainText
            font: control.font
            color: control.ink
            elide: Text.ElideRight
            width: Math.min(implicitWidth, parent.width - (parent.hasIcon ? glyph.width + control.spacing : 0) - parent.chevronWidth)
            x: parent.hasIcon ? glyph.x + glyph.width + control.spacing : (parent.width - parent.implicitWidth) / 2
            y: (parent.height - implicitHeight) / 2
        }
        Icon {
            visible: control.menuChevron
            name: "chevron-down"
            size: Theme.szIcon - 2
            color: control.solid ? control.ink : Theme.textMuted
            x: (parent.hasText ? label.x + label.width : glyph.x + glyph.width) + control.spacing
            y: (parent.height - height) / 2
        }
    }

    background: Rectangle {
        implicitWidth: control.iconOnly ? Theme.szIconHit : Theme.hControl * 2
        implicitHeight: Theme.hControl
        radius: Theme.rControl
        color: {
            if (control.solid)
                return control.down ? Theme.accentPressed : control.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent
            if (control.accentTone)
                return Theme.withAlpha(Theme.accent, control.down ? 0.22 : control.hovered ? 0.16 : 0.10)
            if (control.down)
                return Theme.pressedOn(Theme.controlBg)
            if (control.hovered)
                return Theme.controlHover
            return control.flat ? "transparent" : Theme.withAlpha(Theme.controlBg, 0.55)
        }
        border.width: control.solid && !control.visualFocus ? 0
                    : control.flat && !control.accentTone && !control.visualFocus ? 0 : Theme.hairline
        border.color: control.visualFocus ? (control.solid ? Theme.textPrimary : Theme.accent)
                    : control.accentTone ? Theme.accent : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing } }
    }
}
