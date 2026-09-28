import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

// A raised card over a dimmed window: title, body, buttons on the right.
T.Dialog {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth,
                            implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    padding: Theme.s5
    topPadding: Theme.s2
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    anchors.centerIn: T.Overlay.overlay

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.dNormal; easing.type: Theme.easing } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.dFast; easing.type: Theme.easing } }

    background: Rectangle {
        color: Theme.panelBg
        radius: Theme.rCard
        border.width: Theme.hairline
        border.color: Theme.borderStrong
    }

    header: Column {
        visible: control.title !== ""
        leftPadding: Theme.s5
        rightPadding: Theme.s5
        topPadding: Theme.s5
        bottomPadding: Theme.s2
        spacing: Theme.s1
        Text {
            text: control.title
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsHero
            font.weight: Theme.wHeading
            color: Theme.textPrimary
        }
        Text {
            visible: text !== ""
            text: control["subtitle"] ?? ""
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsControl
            color: Theme.textSecondary
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    T.Overlay.modal: Rectangle { color: Theme.withAlpha(Theme.scrim, 0.6) }
    T.Overlay.modeless: Rectangle { color: Theme.withAlpha(Theme.scrim, 0.12) }
}
