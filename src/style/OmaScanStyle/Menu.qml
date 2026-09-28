import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

T.Menu {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    margins: 0
    padding: Theme.s1
    overlap: 1
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    delegate: MenuItem {}
    contentItem: ListView {
        implicitHeight: contentHeight
        implicitWidth: {
            let w = 220
            for (let i = 0; i < count; ++i) {
                const item = itemAtIndex(i)
                if (item) w = Math.max(w, item.implicitWidth)
            }
            return w
        }
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: control.currentIndex
    }
    background: Rectangle {
        implicitWidth: 220
        color: Theme.panelRaised
        radius: Theme.rMenu
        border.width: Theme.hairline
        border.color: Theme.borderStrong
    }
    T.Overlay.modal: Rectangle { color: Theme.withAlpha(Theme.scrim, 0.3) }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
