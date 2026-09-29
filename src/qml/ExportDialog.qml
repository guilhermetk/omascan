import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// Where the pages go: one PDF, or one picture per page.
Dialog {
    id: dialog
    title: qsTr("Export")
    property string subtitle: Pages.count === 1 ? qsTr("1 page") : qsTr("%1 pages").arg(Pages.count)
    modal: true
    width: Math.min(Theme.wDialog, parent ? parent.width - 2 * Theme.s5 : Theme.wDialog)
    closePolicy: Exporter.busy ? Popup.NoAutoClose : Popup.CloseOnEscape

    // Asks the window for a file name; the answer comes back through choose().
    signal saveRequested(string kind, string suggestedName)

    Settings {
        id: prefs
        category: "export"
        property int scope: 0
        property int paper: 0
        property int quality: 1
        property bool ocr: false
        property string language: ""
        property int format: 0
    }

    readonly property var papers: ["match", "A4", "Letter", "Legal"]
    readonly property var qualities: ["best", "balanced", "small"]
    readonly property string scope: prefs.scope === 1 ? "current" : "all"

    function pdfOptions() {
        return {
            scope: dialog.scope,
            paper: papers[prefs.paper],
            quality: qualities[prefs.quality],
            ocr: prefs.ocr && Exporter.ocrAvailable,
            language: prefs.language !== "" ? prefs.language : Exporter.defaultOcrLanguage
        }
    }
    function imageOptions() {
        return { scope: dialog.scope, format: prefs.format === 1 ? "jpeg" : "png" }
    }

    // Called with the file the person picked. A name changed after the dialog
    // confirmed it must not overwrite anything.
    function choose(kind, url) {
        let path = url.toString()
        if (kind === "pdf") {
            const confirmed = /\.pdf$/i.test(path)
            if (!confirmed) path += ".pdf"
            Exporter.exportPdf(path, Object.assign(pdfOptions(), { confirmed: confirmed }))
        } else {
            const ext = prefs.format === 1 ? /\.(jpe?g)$/i : /\.png$/i
            const confirmed = ext.test(path)
            if (!confirmed) path = path.replace(/\.(png|jpe?g)$/i, "") + (prefs.format === 1 ? ".jpg" : ".png")
            Exporter.exportImages(path, Object.assign(imageOptions(), { confirmed: confirmed }))
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.s3

        FieldRow {
            label: qsTr("Pages")
            visible: Pages.count > 1
            enabled: !Exporter.busy
            Segmented {
                Layout.preferredWidth: 260
                current: prefs.scope
                options: [{ text: qsTr("All %1 pages").arg(Pages.count) }, { text: qsTr("Page %1 only").arg(Pages.current + 1) }]
                onActivated: (index) => prefs.scope = index
            }
            Item { Layout.fillWidth: true }
        }

        Card {
            Layout.fillWidth: true
            icon: "file-text"
            title: qsTr("PDF document")
            detail: qsTr("Every page in one file, ready to send or print.")
            enabled: !Exporter.busy

            FieldRow {
                label: qsTr("Paper")
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Size as scanned"), "A4", "Letter", "Legal"]
                    currentIndex: prefs.paper
                    onActivated: (index) => prefs.paper = index
                }
            }
            FieldRow {
                label: qsTr("Quality")
                Segmented {
                    Layout.fillWidth: true
                    current: prefs.quality
                    options: [
                        { text: qsTr("Best"), tip: qsTr("Full resolution") },
                        { text: qsTr("Balanced"), tip: qsTr("200 dpi — sharp on screen and paper") },
                        { text: qsTr("Small"), tip: qsTr("150 dpi — for email and forms") }
                    ]
                    onActivated: (index) => prefs.quality = index
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                CheckBox {
                    text: qsTr("Make the text searchable")
                    enabled: Exporter.ocrAvailable
                    checked: prefs.ocr && Exporter.ocrAvailable
                    onToggled: prefs.ocr = checked
                }
                Item { Layout.fillWidth: true }
                ComboBox {
                    visible: Exporter.ocrAvailable && Exporter.ocrLanguages.length > 1
                    enabled: prefs.ocr
                    Layout.preferredWidth: 110
                    model: Exporter.ocrLanguages
                    currentIndex: Math.max(0, Exporter.ocrLanguages.indexOf(prefs.language !== "" ? prefs.language : Exporter.defaultOcrLanguage))
                    onActivated: (index) => prefs.language = Exporter.ocrLanguages[index]
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fsLabel
                color: Theme.textMuted
                text: Exporter.ocrAvailable
                    ? qsTr("Reads the words on each page with Tesseract so they can be found and copied.")
                    : qsTr("Install tesseract and a language pack (e.g. tesseract-data-eng) to make text searchable.")
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Export PDF…")
                    icon.name: "file-output"
                    highlighted: true
                    onClicked: dialog.saveRequested("pdf", Exporter.suggestedName("pdf"))
                }
            }
        }

        Card {
            Layout.fillWidth: true
            icon: "file-image"
            title: qsTr("Pictures")
            detail: qsTr("One file per page, exactly as it looks here.")
            enabled: !Exporter.busy

            FieldRow {
                label: qsTr("Format")
                Segmented {
                    Layout.fillWidth: true
                    current: prefs.format
                    options: [{ text: qsTr("PNG · lossless") }, { text: qsTr("JPEG · smaller") }]
                    onActivated: (index) => prefs.format = index
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s3
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fsLabel
                    color: Theme.textMuted
                    visible: dialog.scope === "all" && Pages.count > 1
                    text: qsTr("Pages are numbered from the name you choose: scan-01, scan-02…")
                }
                Item { Layout.fillWidth: true; visible: !(dialog.scope === "all" && Pages.count > 1) }
                Button {
                    text: qsTr("Export pictures…")
                    icon.name: "file-image"
                    highlighted: true
                    onClicked: dialog.saveRequested("images", Exporter.suggestedName(prefs.format === 1 ? "jpg" : "png"))
                }
            }
        }

        // Progress, while writing.
        RowLayout {
            visible: Exporter.busy
            Layout.fillWidth: true
            spacing: Theme.s3
            Label {
                text: Exporter.status
                color: Theme.textSecondary
                Layout.preferredWidth: 220
                elide: Text.ElideRight
            }
            ProgressBar { Layout.fillWidth: true; value: Exporter.progress }
            Button { text: qsTr("Cancel"); onClicked: Exporter.cancel() }
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Close")
            enabled: !Exporter.busy
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
