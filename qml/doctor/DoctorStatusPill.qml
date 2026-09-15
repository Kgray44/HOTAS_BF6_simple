import QtQuick
import QtQuick.Controls
import "DoctorTheme.js" as Theme

Rectangle {
    id: root
    property string text: ""
    property string tone: "neutral"
    implicitHeight: 23
    implicitWidth: label.implicitWidth + 18
    radius: 2
    color: Theme.inset
    border.color: Theme.tone(tone)
    border.width: 1
    Label { id: label; anchors.centerIn: parent; text: root.text; color: Theme.tone(root.tone); font.family: Theme.ui; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 0.7 }
}
