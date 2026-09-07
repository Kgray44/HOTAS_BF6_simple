import QtQuick 6.5
import QtQuick.Controls 6.5

TextField {
    id: control
    required property var theme
    property bool controlEnabled: true
    implicitHeight: 34
    enabled: controlEnabled
    color: theme.text
    placeholderTextColor: theme.textFaint
    font.pixelSize: 11
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: theme.controlRadius
        color: !control.enabled ? theme.controlDisabled
               : control.activeFocus ? theme.controlPressed
               : control.hovered ? theme.controlHover : theme.control
        border.color: control.activeFocus ? theme.borderStrong : theme.border
    }
}
