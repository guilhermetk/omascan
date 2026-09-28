import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

// Anything still drawn by the Basic style reads these palette roles, so they
// are set to the Oma tokens too.
T.ApplicationWindow {
    color: Theme.windowBg
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    palette.window: Theme.panelBg
    palette.windowText: Theme.textPrimary
    palette.base: Theme.controlBg
    palette.alternateBase: Theme.panelRaised
    palette.text: Theme.textPrimary
    palette.button: Theme.controlBg
    palette.buttonText: Theme.textPrimary
    palette.brightText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.light: Theme.controlHover
    palette.midlight: Theme.controlHover
    palette.mid: Theme.borderStrong
    palette.dark: Theme.border
    palette.shadow: Theme.scrim
    palette.toolTipBase: Theme.panelRaised
    palette.toolTipText: Theme.textPrimary
    palette.placeholderText: Theme.textMuted
    palette.link: Theme.accent
}
