import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as D
import QtQuick.Layouts
import Omascan 1.0

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: Pages.count === 0 ? Theme.appName
         : qsTr("Page %1 of %2").arg(Pages.current + 1).arg(Pages.count) + " — " + Theme.appName

    // ── The current page, as plain values ───────────────────────────────────
    // Pages.page() is a snapshot; the tick makes the binding re-read it
    // whenever the model says anything changed.
    property int pageTick: 0
    Connections {
        target: Pages
        function onDataChanged() { win.pageTick++ }
        function onModelReset() { win.pageTick++ }
        function onRowsMoved() { win.pageTick++ }
        function onRowsRemoved() { win.pageTick++ }
        function onRowsInserted() { win.pageTick++ }
        function onMessage(text) { toast.show(text, "info") }
    }
    readonly property var page: { win.pageTick; Pages.count; return Pages.page(Pages.current) }
    readonly property bool hasPage: Pages.count > 0
    readonly property bool dialogOpen: exportDialog.visible || shortcutsDialog.visible || newDialog.visible
    // Single keys stand down while a control that uses them has the keyboard.
    readonly property bool keysFree: !dialogOpen && !(activeFocusItem instanceof ComboBox)
                                     && !(activeFocusItem instanceof Slider)
                                     && !(activeFocusItem instanceof TextField)

    // ── Files out ───────────────────────────────────────────────────────────
    property string pending: ""
    property string pendingName: ""

    function saveAs(kind, suggested) {
        pending = kind
        pendingName = suggested
        const pdf = kind === "pdf"
        FileChooser.saveFile(pdf ? qsTr("Export PDF") : qsTr("Export pictures"), suggested,
                             pdf ? qsTr("PDF document") : qsTr("Pictures"),
                             pdf ? ["*.pdf"] : ["*.png", "*.jpg", "*.jpeg"])
    }
    function chosen(url) {
        const kind = pending
        pending = ""
        if (kind === "pdf" || kind === "images")
            exportDialog.choose(kind, url)
    }
    Connections {
        target: FileChooser
        function onSelected(urls) { win.chosen(urls[0]) }
        function onCanceled() { win.pending = "" }
        // No desktop portal: fall back to Qt's own dialog.
        function onFailed() {
            fallbackDialog.nameFilters = win.pending === "pdf" ? ["PDF (*.pdf)"] : [qsTr("Pictures") + " (*.png *.jpg)"]
            fallbackDialog.selectedFile = "file://" + win.pendingName
            fallbackDialog.open()
        }
    }
    D.FileDialog {
        id: fallbackDialog
        fileMode: D.FileDialog.SaveFile
        onAccepted: win.chosen(selectedFile)
        onRejected: win.pending = ""
    }

    function scan() {
        if (canvas.cropping) canvas.applyCrop()
        if (Scanner.ready) Scanner.scan()
    }

    Connections {
        target: Scanner
        function onFailed(message) {
            toast.show(message, "error", [{ text: qsTr("Try again"), run: () => win.scan() }])
        }
    }
    Connections {
        target: Exporter
        function onFinished(ok, message, file) {
            if (ok) {
                exportDialog.close()
                const name = decodeURIComponent(file.toString().split("/").pop())
                toast.show(qsTr("Saved %1").arg(name), "success", [
                    { text: qsTr("Open"), run: () => Exporter.open(file) },
                    { text: qsTr("Show in folder"), run: () => Exporter.showInFolder(file) }
                ])
            } else if (message !== "") {
                toast.show(message, "error")
            }
        }
    }

    // Pages left from last time were never exported: say so, and offer the
    // fresh start in one click.
    Component.onCompleted: {
        if (Pages.restoredCount > 0)
            toast.show(Pages.restoredCount === 1
                           ? qsTr("Your unexported page from last time is still here")
                           : qsTr("Your %1 unexported pages from last time are still here").arg(Pages.restoredCount),
                       "info", [{ text: qsTr("Start new"), run: () => Pages.clear() }])
    }

    // ── Actions: one place for each command, its key and its state ──────────
    Action { id: scanAction; text: Scanner.feeder ? qsTr("Scan from feeder") : qsTr("Scan"); icon.name: "scan-line"; shortcut: "Ctrl+Return"; enabled: Scanner.ready; onTriggered: win.scan() }
    Action { id: exportAction; text: qsTr("Export…"); icon.name: "file-output"; shortcut: "Ctrl+E"; enabled: win.hasPage && !win.dialogOpen; onTriggered: { canvas.applyCrop(); exportDialog.open() } }
    Action { id: saveAction; shortcut: StandardKey.Save; enabled: win.hasPage && !win.dialogOpen; onTriggered: exportAction.trigger() }
    Action { id: newAction; text: qsTr("New document"); icon.name: "files"; shortcut: StandardKey.New; enabled: win.hasPage && !Scanner.scanning; onTriggered: newDialog.open() }
    Action { id: quitAction; text: qsTr("Quit"); shortcut: StandardKey.Quit; onTriggered: Qt.quit() }
    Action { id: undoAction; text: Pages.canUndo ? qsTr("Undo %1").arg(Pages.undoText.toLowerCase()) : qsTr("Undo"); icon.name: "undo-2"; shortcut: StandardKey.Undo; enabled: Pages.canUndo && !canvas.cropping && !win.dialogOpen; onTriggered: Pages.undo() }
    Action { id: redoAction; text: Pages.canRedo ? qsTr("Redo %1").arg(Pages.redoText.toLowerCase()) : qsTr("Redo"); icon.name: "redo-2"; shortcut: StandardKey.Redo; enabled: Pages.canRedo && !canvas.cropping && !win.dialogOpen; onTriggered: Pages.redo() }
    Action { id: rotateLeftAction; text: qsTr("Rotate left"); icon.name: "rotate-ccw"; shortcut: "["; enabled: win.hasPage && win.keysFree && !canvas.cropping; onTriggered: Pages.rotate(Pages.current, -90) }
    Action { id: rotateRightAction; text: qsTr("Rotate right"); icon.name: "rotate-cw"; shortcut: "]"; enabled: win.hasPage && win.keysFree && !canvas.cropping; onTriggered: Pages.rotate(Pages.current, 90) }
    Action { id: cropAction; text: qsTr("Crop"); icon.name: "crop"; shortcut: "C"; checkable: true; checked: canvas.cropping; enabled: win.hasPage && win.keysFree; onTriggered: canvas.cropping ? canvas.applyCrop() : canvas.beginCrop() }
    Action { id: deleteAction; text: qsTr("Delete page"); icon.name: "trash"; shortcut: StandardKey.Delete; enabled: win.hasPage && win.keysFree && !canvas.cropping; onTriggered: Pages.remove(Pages.current) }
    Action { id: duplicateAction; text: qsTr("Duplicate page"); icon.name: "copy"; shortcut: "Ctrl+D"; enabled: win.hasPage && !canvas.cropping; onTriggered: Pages.duplicate(Pages.current) }
    Action { id: moveUpAction; text: qsTr("Move page up"); icon.name: "arrow-up"; shortcut: "Ctrl+Up"; enabled: Pages.current > 0 && win.keysFree; onTriggered: Pages.move(Pages.current, Pages.current - 1) }
    Action { id: moveDownAction; text: qsTr("Move page down"); icon.name: "arrow-down"; shortcut: "Ctrl+Down"; enabled: win.hasPage && Pages.current < Pages.count - 1 && win.keysFree; onTriggered: Pages.move(Pages.current, Pages.current + 1) }
    Action { id: lookAllAction; text: qsTr("Use this look on all pages"); icon.name: "wand-sparkles"; enabled: Pages.count > 1; onTriggered: Pages.applyLookToAll(Pages.current) }
    Action { id: zoomInAction; text: qsTr("Zoom in"); icon.name: "zoom-in"; shortcut: StandardKey.ZoomIn; enabled: win.hasPage; onTriggered: canvas.zoomIn() }
    Action { id: zoomOutAction; text: qsTr("Zoom out"); icon.name: "zoom-out"; shortcut: StandardKey.ZoomOut; enabled: win.hasPage; onTriggered: canvas.zoomOut() }
    Action { id: zoomFitAction; text: qsTr("Fit page"); icon.name: "maximize"; shortcut: "Ctrl+0"; enabled: win.hasPage; onTriggered: canvas.zoomFit() }
    Action { id: shortcutsAction; text: qsTr("Keyboard shortcuts"); icon.name: "keyboard"; shortcut: "?"; enabled: !win.dialogOpen; onTriggered: shortcutsDialog.open() }

    readonly property var looks: [qsTr("Original"), qsTr("Enhanced"), qsTr("Greyscale"), qsTr("Black & white")]
    Repeater {
        model: 4
        Item {
            required property int index
            Shortcut {
                sequence: String(index + 1)
                enabled: win.hasPage && win.keysFree
                onActivated: Pages.setFilter(Pages.current, index)
            }
        }
    }
    Shortcut { sequences: ["Ctrl+Enter"]; enabled: Scanner.ready; onActivated: win.scan() }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: redoAction.enabled; onActivated: Pages.redo() }
    // Zoom in is Ctrl and "+", which needs Shift on most layouts; take the plain key too.
    Shortcut { sequence: "Ctrl+="; enabled: zoomInAction.enabled; onActivated: canvas.zoomIn() }
    Shortcut { sequences: ["Up", "PgUp"]; enabled: win.keysFree && Pages.current > 0 && !canvas.cropping; onActivated: Pages.current = Pages.current - 1 }
    Shortcut { sequences: ["Down", "PgDown"]; enabled: win.keysFree && Pages.current < Pages.count - 1 && !canvas.cropping; onActivated: Pages.current = Pages.current + 1 }
    Shortcut { sequence: "Home"; enabled: win.keysFree && win.hasPage; onActivated: Pages.current = 0 }
    Shortcut { sequence: "End"; enabled: win.keysFree && win.hasPage; onActivated: Pages.current = Pages.count - 1 }
    Shortcut { sequences: ["Return", "Enter"]; enabled: canvas.cropping && !win.dialogOpen; onActivated: canvas.applyCrop() }
    Shortcut { sequence: "F1"; enabled: !win.dialogOpen; onActivated: shortcutsDialog.open() }
    Shortcut {
        sequence: "Escape"
        enabled: (canvas.cropping || Scanner.scanning) && !win.dialogOpen
        onActivated: canvas.cropping ? canvas.cancelCrop() : Scanner.cancel()
    }

    // ── Chrome ──────────────────────────────────────────────────────────────
    header: Column {
        // The menu row, as in OmaShow: the app's name, then the menus.
        Rectangle {
            width: parent.width
            height: Theme.hTitleBar
            color: Theme.windowBg
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s4
                anchors.rightMargin: Theme.s2
                spacing: Theme.s3
                Label { text: Theme.appName; font.weight: Font.Bold; font.pixelSize: Theme.fsTitle }
                MenuBar {
                    Menu {
                        title: qsTr("File")
                        MenuItem { action: newAction }
                        MenuSeparator {}
                        MenuItem { action: scanAction }
                        MenuItem { action: exportAction }
                        MenuSeparator {}
                        MenuItem { action: quitAction }
                    }
                    Menu {
                        title: qsTr("Edit")
                        MenuItem { action: undoAction }
                        MenuItem { action: redoAction }
                        MenuSeparator {}
                        MenuItem { action: duplicateAction }
                        MenuItem { action: deleteAction }
                    }
                    Menu {
                        title: qsTr("Page")
                        MenuItem { action: rotateLeftAction }
                        MenuItem { action: rotateRightAction }
                        MenuItem { action: cropAction }
                        MenuSeparator {}
                        Repeater {
                            model: 4
                            delegate: MenuItem {
                                required property int index
                                text: win.looks[index]
                                checkable: true
                                checked: win.page.filter === index
                                enabled: win.hasPage
                                onTriggered: Pages.setFilter(Pages.current, index)
                            }
                        }
                        MenuItem { action: lookAllAction }
                        MenuSeparator {}
                        MenuItem { action: moveUpAction }
                        MenuItem { action: moveDownAction }
                    }
                    Menu {
                        id: viewMenu
                        title: qsTr("View")
                        MenuItem { action: zoomInAction }
                        MenuItem { action: zoomOutAction }
                        MenuItem { action: zoomFitAction }
                    }
                    Menu {
                        title: qsTr("Help")
                        MenuItem { action: shortcutsAction }
                    }
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    action: shortcutsAction
                    display: AbstractButton.IconOnly
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Keyboard shortcuts (?)")
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
        }

        // The toolbar: the next thing to do on the left, where it goes on the right.
        Rectangle {
            width: parent.width
            height: Theme.hToolbar
            color: Theme.panelBg
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s3
                anchors.rightMargin: Theme.s3
                spacing: Theme.s1

                Button {
                    visible: !Scanner.scanning
                    action: scanAction
                    property bool primary: true
                    ToolTip.visible: hovered
                    ToolTip.text: Scanner.devices.length > 0 ? qsTr("Scan with %1 (Ctrl+Enter)").arg(Scanner.devices[Scanner.deviceIndex]?.name ?? "")
                                                            : qsTr("No scanner connected")
                }
                Button {
                    visible: Scanner.scanning
                    text: qsTr("Stop")
                    icon.name: "circle-stop"
                    highlighted: true
                    onClicked: Scanner.cancel()
                }
                ToolSeparator {}
                Button { action: rotateLeftAction; flat: true; display: AbstractButton.IconOnly; ToolTip.visible: hovered; ToolTip.text: qsTr("Rotate left ([)") }
                Button { action: rotateRightAction; flat: true; display: AbstractButton.IconOnly; ToolTip.visible: hovered; ToolTip.text: qsTr("Rotate right (])") }
                Button { action: cropAction; flat: true; ToolTip.visible: hovered; ToolTip.text: qsTr("Crop (C)") }
                Button { action: deleteAction; flat: true; display: AbstractButton.IconOnly; ToolTip.visible: hovered; ToolTip.text: qsTr("Delete page (Del)") }
                Item { Layout.fillWidth: true }
                Button { action: undoAction; flat: true; display: AbstractButton.IconOnly; ToolTip.visible: hovered; ToolTip.text: undoAction.text + " (Ctrl+Z)" }
                Button { action: redoAction; flat: true; display: AbstractButton.IconOnly; ToolTip.visible: hovered; ToolTip.text: redoAction.text + " (Ctrl+Shift+Z)" }
                ToolSeparator {}
                Button { action: exportAction; highlighted: true; ToolTip.visible: hovered; ToolTip.text: qsTr("Save as PDF or pictures (Ctrl+E)") }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
        }
    }

    footer: Rectangle {
        height: Theme.hStatusBar
        color: Theme.windowBg
        Rectangle { width: parent.width; height: Theme.hairline; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.s4
            anchors.rightMargin: Theme.s3
            spacing: Theme.s3
            component StatusText: Label { font.pixelSize: Theme.fsLabel; color: Theme.textSecondary; font.features: { "tnum": 1 } }
            component StatusRule: Rectangle { implicitWidth: Theme.hairline; implicitHeight: Theme.s4; color: Theme.border }

            StatusText { text: win.hasPage ? qsTr("Page %1 of %2").arg(Pages.current + 1).arg(Pages.count) : qsTr("No pages") }
            StatusRule { visible: win.hasPage }
            StatusText {
                visible: win.hasPage
                text: (win.page.pixelWidth ?? 0) + " × " + (win.page.pixelHeight ?? 0)
                      + ((win.page.dpi ?? 0) > 0 ? "  ·  " + win.page.dpi + " dpi" : "")
            }
            Item { Layout.fillWidth: true }

            Rectangle {
                implicitWidth: Theme.szStatusDot
                implicitHeight: Theme.szStatusDot
                radius: width / 2
                color: !Scanner.available || (Scanner.devices.length === 0 && !Scanner.discovering) ? Theme.danger
                     : Scanner.scanning || Scanner.discovering || Scanner.loadingOptions ? Theme.warning
                     : Theme.success
            }
            StatusText {
                text: {
                    const name = Scanner.devices[Scanner.deviceIndex]?.name ?? ""
                    return name !== "" && !Scanner.discovering ? name + "  ·  " + Scanner.status : Scanner.status
                }
                Layout.maximumWidth: 420
                elide: Text.ElideRight
            }
            StatusRule { visible: win.hasPage }
            ToolButton { visible: win.hasPage; action: zoomOutAction; display: AbstractButton.IconOnly; implicitHeight: 22; implicitWidth: 22 }
            StatusText { visible: win.hasPage; text: canvas.zoomPercent + "%"; Layout.preferredWidth: 36; horizontalAlignment: Text.AlignHCenter }
            ToolButton { visible: win.hasPage; action: zoomInAction; display: AbstractButton.IconOnly; implicitHeight: 22; implicitWidth: 22 }
            ToolButton { visible: win.hasPage; action: zoomFitAction; display: AbstractButton.IconOnly; implicitHeight: 22; implicitWidth: 22; checked: canvas.zoomFactor === 1 }
        }
    }

    // ── Body ────────────────────────────────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        PageList {
            Layout.preferredWidth: Theme.wNavigator
            Layout.fillHeight: true
            visible: Pages.count > 0 || Scanner.scanning
        }

        PageView {
            id: canvas
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            page: win.page
            onScanRequested: win.scan()
            onCropFinished: canvas.forceActiveFocus()
            TapHandler { onTapped: canvas.forceActiveFocus() }

            Toast {
                id: toast
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: canvas.cropping ? Theme.s4 + Theme.hHeroButton + Theme.s2 : Theme.s4
            }
        }

        Inspector {
            Layout.preferredWidth: Theme.wInspector
            Layout.fillHeight: true
            page: win.page
            onScanRequested: win.scan()
        }
    }

    // ── Sheets ──────────────────────────────────────────────────────────────
    ExportDialog {
        id: exportDialog
        parent: Overlay.overlay
        onSaveRequested: (kind, name) => win.saveAs(kind, name)
    }
    ShortcutsDialog { id: shortcutsDialog; parent: Overlay.overlay }
    Dialog {
        id: newDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("Start a new document?")
        width: 440
        contentItem: Label {
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            text: Pages.count === 1
                ? qsTr("The page here will be removed. Export it first if you still need it.")
                : qsTr("The %1 pages here will be removed. Export them first if you still need them.").arg(Pages.count)
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Keep pages"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: qsTr("Remove pages"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: Pages.clear()
    }

    // For --ui-shot: put the window in a known state before each capture.
    function shotState(state) {
        exportDialog.close()
        shortcutsDialog.close()
        viewMenu.close()
        if (canvas.cropping) canvas.cancelCrop()
        if (state === "scan") win.scan()
        else if (state === "crop") canvas.beginCrop()
        else if (state === "export") exportDialog.open()
        else if (state === "shortcuts") shortcutsDialog.open()
        else if (state === "menu") viewMenu.open()
    }
}
