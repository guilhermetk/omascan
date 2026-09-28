import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

T.ProgressBar {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    contentItem: Item {
        implicitWidth: Theme.wInspector / 2
        implicitHeight: 4
        clip: true
        Rectangle {
            id: bar
            width: control.indeterminate ? parent.width / 3 : control.position * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
            // Indeterminate: a segment sweeping across.
            SequentialAnimation on x {
                running: control.indeterminate && control.visible
                loops: Animation.Infinite
                NumberAnimation { from: -bar.width; to: control.width; duration: 1200; easing.type: Easing.InOutQuad }
            }
        }
        Binding { target: bar; property: "x"; value: 0; when: !control.indeterminate }
    }
    background: Rectangle {
        implicitWidth: Theme.wInspector / 2
        implicitHeight: 4
        y: (control.height - height) / 2
        height: 4
        radius: 2
        color: Theme.borderStrong
    }
}
