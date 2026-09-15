import QtQuick
import QtQuick.Controls
import "DoctorTheme.js" as Theme

Item {
    id: root
    property var values: []
    property string currentValue: ""
    property string accessibleName: "Segmented control"
    signal activated(string value)
    implicitWidth: segmentRow.implicitWidth + 4
    implicitHeight: 32
    Accessible.name: accessibleName
    Rectangle { anchors.fill: parent; color: Theme.inset; border.color: Theme.separator; border.width: 1; radius: 2 }
    Row {
        id: segmentRow
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2
        Repeater {
            model: root.values
            delegate: AbstractButton {
                id: segment
                required property string modelData
                width: Math.max(56, label.implicitWidth + 20)
                height: parent.height
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData
                Accessible.checked: root.currentValue === modelData
                onClicked: root.activated(modelData)
                contentItem: Label {
                    id: label
                    text: modelData
                    color: root.currentValue === modelData ? Theme.textPrimary : Theme.textMuted
                    font.family: Theme.ui
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 1
                    border.width: segment.activeFocus ? 2 : 0
                    border.color: Theme.focus
                    color: segment.down ? Theme.pressed : root.currentValue === modelData ? Theme.selected : segment.hovered ? Theme.hover : "transparent"
                }
            }
        }
    }
}
