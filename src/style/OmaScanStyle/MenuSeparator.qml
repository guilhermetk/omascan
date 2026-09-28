import QtQuick
import QtQuick.Templates as T
import Omascan 1.0

T.MenuSeparator {
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: Theme.s1
    leftPadding: Theme.s2
    rightPadding: Theme.s2
    contentItem: Rectangle { implicitWidth: 180; implicitHeight: Theme.hairline; color: Theme.border }
}
