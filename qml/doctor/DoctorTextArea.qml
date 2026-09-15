import QtQuick
import QtQuick.Controls
import "DoctorTheme.js" as Theme

TextArea {
    id: control
    readOnly: true
    selectByMouse: true
    color: Theme.textSecondary
    font.family: Theme.ui
    font.pixelSize: 11
    selectionColor: Theme.selected
    selectedTextColor: Theme.textPrimary
    padding: 9
    background: Rectangle { color: Theme.inset; border.color: control.activeFocus ? Theme.focus : Theme.separator; border.width: control.activeFocus ? 2 : 1; radius: 2 }
}
