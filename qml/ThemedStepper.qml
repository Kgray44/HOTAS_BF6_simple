import QtQuick 6.5
import QtQuick.Layouts 6.5

Item {
    id: control
    required property var theme
    property int value: 0
    property int from: 0
    property int to: 100
    property int stepSize: 1
    readonly property bool legacy: !!theme.legacy
    signal valueModified(int value)
    // Match Legacy's original FlightStepper shell while retaining the shared
    // button behavior used by every V2.4 form.
    implicitWidth: 108; implicitHeight: legacy ? 30 : 34
    function setClamped(next) { const bounded = Math.max(from, Math.min(to, next)); if (bounded !== value) { value = bounded; valueModified(value) } }
    Rectangle {
        anchors.fill: parent; radius: legacy ? 4 : theme.controlRadius
        color: legacy ? "#11191d" : theme.control
        border.color: legacy ? "#435c66" : theme.border
    }
    RowLayout { anchors.fill: parent; anchors.margins: 2; spacing: 2
        ThemedButton { theme: control.theme; text: "−"; compact: true; tone: "secondary"; commandEnabled: control.value > control.from; onTriggered: control.setClamped(control.value - control.stepSize) }
        Text { Layout.fillWidth: true; text: control.value; horizontalAlignment: Text.AlignHCenter; color: legacy ? "#e3eeee" : theme.text; font.bold: true; font.pixelSize: 11; font.family: theme.telemetryFont }
        ThemedButton { theme: control.theme; text: "+"; compact: true; tone: "secondary"; commandEnabled: control.value < control.to; onTriggered: control.setClamped(control.value + control.stepSize) }
    }
}
