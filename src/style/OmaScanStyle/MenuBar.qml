import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

T.MenuBar {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    spacing: 0
    delegate: MenuBarItem {}
    contentItem: Row {
        spacing: control.spacing
        Repeater { model: control.contentModel }
    }
    background: Item {}
}
