import QtQuick 6.5
import QtQuick.Layouts 6.5

Item {
    id: control
    required property var theme
    property string text: ""
    property bool checked: false
    property bool controlEnabled: true
    // Legacy has its own compact, squared selection treatment.  Keep it in
    // this shared semantic control so Devices dialogs and the top-bar scope
    // chooser inherit the established Legacy language rather than a Standard
    // checkmark with a different palette.
    readonly property bool legacy: !!theme.legacy
    signal toggled(bool checked)
    implicitWidth: row.implicitWidth
    implicitHeight: Math.max(26, row.implicitHeight)

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 7
        Rectangle {
            implicitWidth: 17; implicitHeight: 17; radius: control.legacy ? 3 : theme.controlRadius
            color: !control.controlEnabled ? theme.controlDisabled
                   : control.checked ? (control.legacy ? "#244650" : theme.selection)
                   : (checkHit.containsMouse && control.legacy ? "#142128" : theme.control)
            border.color: !control.controlEnabled ? (control.legacy ? "#182f3539" : theme.border)
                          : control.checked ? (control.legacy ? "#78aab9" : theme.orange)
                          : checkHit.containsMouse && control.legacy ? "#527482"
                                                               : (control.legacy ? "#435660" : theme.border)
            Text { anchors.centerIn: parent; visible: control.checked; text: "✓"; color: control.legacy ? "#f0f4f5" : theme.textStrong; font.bold: true; font.pixelSize: 12 }
        }
        Text { text: control.text; color: control.controlEnabled ? (control.legacy ? "#dce7e8" : theme.text) : (control.legacy ? "#879196" : theme.textFaint); font.pixelSize: 10; font.bold: true }
    }
    MouseArea {
        id: checkHit
        anchors.fill: parent; enabled: control.controlEnabled; hoverEnabled: true
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: { control.checked = !control.checked; control.toggled(control.checked) }
    }
}
