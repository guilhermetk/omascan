import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// The small uppercase caption over a group of inspector controls.
Label {
    color: Theme.textMuted
    font.pixelSize: Theme.fsCaption
    font.weight: Theme.wHeading
    font.letterSpacing: Theme.capsTracking
    font.capitalization: Font.AllUppercase
    Layout.topMargin: Theme.s2
}
