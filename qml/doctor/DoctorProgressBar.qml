import QtQuick
import QtQuick.Controls
import "DoctorTheme.js" as Theme

Control {
    id: control
    property real value: 0
    property real from: 0
    property real to: 100
    property string tone: "information"
    implicitHeight: 5
    implicitWidth: 96
    Accessible.name: "Progress " + Math.round(value) + "%"
    contentItem: Rectangle {
        color: "#0f1317"
        radius: 2
        Rectangle {
            width: Math.max(0, Math.min(parent.width, parent.width * ((control.value - control.from) / Math.max(1, control.to - control.from))))
            height: parent.height
            radius: 2
            color: Theme.tone(control.tone)
        }
    }
}
