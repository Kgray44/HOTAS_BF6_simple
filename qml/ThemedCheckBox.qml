import QtQuick 6.5
import QtQuick.Layouts 6.5

Item {
    id: control
    required property var theme
    property string text: ""
    property bool checked: false
    property bool controlEnabled: true
    signal toggled(bool checked)
    implicitWidth: row.implicitWidth
    implicitHeight: Math.max(26, row.implicitHeight)

    RowLayout {
        id: row
        anchors.fill: parent
        spacing: 7
        Rectangle {
            implicitWidth: 17; implicitHeight: 17; radius: theme.controlRadius
            color: control.checked ? theme.selection : theme.control
            border.color: control.checked ? theme.orange : theme.border
            Text { anchors.centerIn: parent; visible: control.checked; text: "✓"; color: theme.textStrong; font.bold: true; font.pixelSize: 12 }
        }
        Text { text: control.text; color: control.controlEnabled ? theme.text : theme.textFaint; font.pixelSize: 10; font.bold: true }
    }
    MouseArea {
        anchors.fill: parent; enabled: control.controlEnabled; hoverEnabled: true
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: { control.checked = !control.checked; control.toggled(control.checked) }
    }
}
