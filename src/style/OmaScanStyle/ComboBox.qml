pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

// Dropdown: a filled well with a hairline and a chevron.
T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    leftPadding: Theme.s2 + 2
    rightPadding: Theme.s2 + (indicator ? indicator.width + Theme.s2 : 0)
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity

    delegate: T.ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: Theme.hRow
        leftPadding: Theme.s2 + 2
        rightPadding: Theme.s2
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole] : modelData[control.textRole] ?? modelData)
                               : modelData
        font: control.font
        contentItem: Row {
            spacing: Theme.s2
            Icon {
                name: "check"
                size: Theme.szIcon - 2
                color: Theme.accent
                opacity: control.currentIndex === option.index ? 1 : 0
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: option.text
                textFormat: Text.PlainText
                font: option.font
                color: option.highlighted ? Theme.textPrimary : Theme.textSecondary
                elide: Text.ElideRight
                width: option.availableWidth - Theme.szIcon - Theme.s2
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        background: Rectangle {
            radius: Theme.rControl
            color: option.highlighted ? Theme.controlHover : "transparent"
        }
    }

    indicator: Icon {
        name: "chevron-down"
        size: Theme.szIcon
        color: control.hovered ? Theme.textSecondary : Theme.textMuted
        x: control.width - width - Theme.s2
        y: control.topPadding + (control.availableHeight - height) / 2
    }

    contentItem: Text {
        text: control.displayText
        textFormat: Text.PlainText
        font: control.font
        color: control.flat ? Theme.textSecondary : Theme.textPrimary
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: Theme.wInspector / 2
        implicitHeight: Theme.hControl
        radius: Theme.rControl
        color: control.down ? Theme.pressedOn(Theme.controlBg)
             : control.hovered ? Theme.controlHover
             : control.flat ? "transparent" : Theme.controlBg
        border.width: control.flat && !control.visualFocus ? 0 : Theme.hairline
        border.color: control.visualFocus ? Theme.accent : Theme.border
    }

    popup: T.Popup {
        y: control.height + 2
        width: Math.max(control.width, Theme.wInspector / 2)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         control.Window.height - topMargin - bottomMargin)
        topMargin: Theme.s2
        bottomMargin: Theme.s2
        padding: Theme.s1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
        }
        background: Rectangle {
            color: Theme.panelRaised
            radius: Theme.rMenu
            border.width: Theme.hairline
            border.color: Theme.borderStrong
        }
    }
}
