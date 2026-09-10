import QtQuick 6.5
import QtQuick.Controls 6.5

TextField {
    id: control
    required property var theme
    property bool controlEnabled: true
    implicitHeight: 34
    enabled: controlEnabled
    color: theme.legacy ? "#e7f0f1" : theme.text
    placeholderTextColor: theme.legacy ? "#72848a" : theme.textFaint
    font.pixelSize: 11
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    onAccepted: focus = false
    background: Rectangle {
        radius: theme.controlRadius
        color: !control.enabled ? theme.controlDisabled
               : control.activeFocus ? (theme.legacy ? "#16262d" : theme.controlPressed)
               : control.hovered ? (theme.legacy ? "#142128" : theme.controlHover) : theme.control
        border.color: control.activeFocus ? theme.borderStrong
                     : control.hovered ? (theme.legacy ? "#527482" : theme.border)
                                       : (theme.legacy ? "#435660" : theme.border)
    }
}
