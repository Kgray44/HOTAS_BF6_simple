import QtQuick
import QtQuick.Controls
import "DoctorTheme.js" as Theme

ScrollBar {
    id: control
    implicitWidth: 12
    implicitHeight: 12
    policy: ScrollBar.AsNeeded
    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        color: control.pressed ? Theme.focus : control.hovered ? Theme.information : "#52616c"
        opacity: control.active || control.hovered ? 0.92 : 0.55
    }
    background: Rectangle { color: "transparent" }
}
