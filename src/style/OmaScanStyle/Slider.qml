import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

// A thin rail, an accent fill, a round thumb.
T.Slider {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)

    padding: Theme.s1
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    // Where the fill starts: 0.5 for a slider centred on zero.
    property real origin: 0

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: Theme.szIcon - 2
        implicitHeight: Theme.szIcon - 2
        radius: width / 2
        color: control.pressed ? Theme.accentPressed : Theme.accent
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.textPrimary
    }

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: Theme.wInspector / 2
        implicitHeight: 4
        width: control.availableWidth
        height: implicitHeight
        radius: 2
        color: Theme.borderStrong

        Rectangle {
            x: Math.min(control.origin, control.visualPosition) * parent.width
            width: Math.abs(control.visualPosition - control.origin) * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }
}
