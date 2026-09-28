import QtQuick
import QtQuick.Window
import Omascan 1.0

// One icon family (Lucide), one stroke weight. The colour is baked into an SVG
// data URI rather than tinted with a shader, so icons also render offscreen;
// Qt caches by URL, so each (icon, colour, size) is rasterised once.
Item {
    id: root

    // Lucide icon name, e.g. "scan-line".
    property string name: ""
    property color color: Theme.textSecondary
    property int size: Theme.szIcon
    // Lucide draws at stroke 2 on a 24 grid; 2.25 renders as 1.5 px at 16 px.
    property real strokeWidth: 2.25

    implicitWidth: size
    implicitHeight: size

    readonly property string shape: Icons.shapes[name] !== undefined ? Icons.shapes[name] : ""

    Image {
        anchors.fill: parent
        visible: root.shape !== ""
        smooth: true
        fillMode: Image.PreserveAspectFit
        readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
        sourceSize.width: root.size * dpr
        sourceSize.height: root.size * dpr
        source: root.shape === "" ? "" :
            "data:image/svg+xml;utf8," + encodeURIComponent(
                '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"'
                + ' fill="none" stroke="' + root.color
                + '" stroke-width="' + root.strokeWidth
                + '" stroke-linecap="round" stroke-linejoin="round">'
                + root.shape + '</svg>')
    }
}
