import QtQuick 6.5
import QtQuick.Layouts 6.5

Item {
    id: control
    required property var theme
    property int value: 0
    property int from: 0
    property int to: 100
    property int stepSize: 1
    signal valueModified(int value)
    implicitWidth: 108; implicitHeight: 34
    function setClamped(next) { const bounded = Math.max(from, Math.min(to, next)); if (bounded !== value) { value = bounded; valueModified(value) } }
    Rectangle { anchors.fill: parent; radius: theme.controlRadius; color: theme.control; border.color: theme.border }
    RowLayout { anchors.fill: parent; anchors.margins: 2; spacing: 2
        ThemedButton { theme: control.theme; text: "−"; compact: true; tone: "secondary"; commandEnabled: control.value > control.from; onTriggered: control.setClamped(control.value - control.stepSize) }
        Text { Layout.fillWidth: true; text: control.value; horizontalAlignment: Text.AlignHCenter; color: theme.text; font.bold: true; font.pixelSize: 11 }
        ThemedButton { theme: control.theme; text: "+"; compact: true; tone: "secondary"; commandEnabled: control.value < control.to; onTriggered: control.setClamped(control.value + control.stepSize) }
    }
}
