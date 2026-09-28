import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omascan 1.0

// A framed block with an icon tile, a title and a line of explanation,
// then its own controls — the export sheet's PDF and Pictures blocks.
Rectangle {
    id: card
    property string icon: ""
    property string title: ""
    property string detail: ""
    default property alias content: body.data

    implicitHeight: column.implicitHeight + 2 * Theme.s4
    color: Theme.withAlpha(Theme.panelRaised, 0.6)
    radius: Theme.rCard
    border.width: Theme.hairline
    border.color: Theme.border

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.s4
        spacing: Theme.s3

        RowLayout {
            spacing: Theme.s3
            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: Theme.rCard
                color: Theme.controlBg
                border.width: Theme.hairline
                border.color: Theme.borderStrong
                Icon { anchors.centerIn: parent; name: card.icon; size: Theme.szIconLarge; color: Theme.textPrimary }
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Label { text: card.title; font.pixelSize: Theme.fsSection; font.weight: Theme.wHeading }
                Label {
                    text: card.detail
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: Theme.hairline; color: Theme.border }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: Theme.s2
        }
    }
}
