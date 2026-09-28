import QtQuick

// A page picture that never blinks. Page pictures render on another thread,
// so a plain Image goes blank for a moment every time a slider moves; this
// keeps the last finished picture up until the next one is ready.
Item {
    id: root
    property url source
    property int requestedEdge: 1200
    readonly property bool ready: front.status === Image.Ready
    readonly property bool loading: String(back.source) !== "" && back.status === Image.Loading
                                    || front.status === Image.Loading

    property Image front: a
    property Image back: b

    onSourceChanged: load()
    onRequestedEdgeChanged: load()
    Component.onCompleted: load()

    function load() {
        if (String(root.source) === "") {
            a.source = ""; b.source = ""
            return
        }
        if (front.status !== Image.Ready || String(front.source) === "") {
            front.sourceSize = Qt.size(root.requestedEdge, root.requestedEdge)
            front.source = root.source
            return
        }
        back.sourceSize = Qt.size(root.requestedEdge, root.requestedEdge)
        back.source = root.source
    }

    function settle(image) {
        if (image !== back || image.status !== Image.Ready)
            return
        const old = front
        front = back
        back = old
        back.source = ""
    }

    Image {
        id: a
        anchors.fill: parent
        asynchronous: true
        cache: false
        smooth: true
        mipmap: true
        fillMode: Image.Stretch
        visible: root.front === a
        onStatusChanged: root.settle(a)
    }
    Image {
        id: b
        anchors.fill: parent
        asynchronous: true
        cache: false
        smooth: true
        mipmap: true
        fillMode: Image.Stretch
        visible: root.front === b
        onStatusChanged: root.settle(b)
    }
}
