import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// A short message at the bottom of the canvas, with up to two actions:
//   toast.show(text, "success", [{ text: "Open", run: () => … }, …])
Rectangle {
    id: toast
    property string text: ""
    property string tone: "info" // info | success | error
    property var actions: []
    readonly property string actionText: actions.length > 0 ? actions[0].text : ""
    readonly property string secondaryText: actions.length > 1 ? actions[1].text : ""

    function show(message, kind, choices) {
        toast.text = message
        toast.tone = kind || "info"
        toast.actions = choices || []
        toast.opacity = 1
        hide.restart()
    }
    function run(index) {
        const choice = toast.actions[index]
        toast.opacity = 0
        if (choice) choice.run()
    }

    visible: opacity > 0
    opacity: 0
    implicitWidth: row.implicitWidth + 2 * Theme.s3
    implicitHeight: Theme.hHeroButton
    radius: Theme.rMenu
    color: Theme.panelRaised
    border.width: Theme.hairline
    border.color: Theme.borderStrong
    Behavior on opacity { NumberAnimation { duration: Theme.dSlow; easing.type: Theme.easing } }

    HoverHandler { id: hover }
    Timer {
        id: hide
        interval: toast.actionText !== "" ? Theme.toastDuration * 2 : Theme.toastDuration
        onTriggered: if (hover.hovered) restart(); else toast.opacity = 0
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.s2
        Icon {
            name: toast.tone === "error" ? "circle-alert" : toast.tone === "success" ? "circle-check" : "info"
            color: toast.tone === "error" ? Theme.danger : toast.tone === "success" ? Theme.success : Theme.accent
        }
        Label { text: toast.text; Layout.maximumWidth: 520; elide: Text.ElideRight }
        Button {
            visible: toast.actionText !== ""
            text: toast.actionText
            flat: true
            highlighted: true
            onClicked: toast.run(0)
        }
        Button {
            visible: toast.secondaryText !== ""
            text: toast.secondaryText
            flat: true
            onClicked: toast.run(1)
        }
        ToolButton {
            icon.name: "x"
            onClicked: toast.opacity = 0
        }
    }
}
