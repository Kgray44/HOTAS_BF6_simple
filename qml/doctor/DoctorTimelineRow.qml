import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "DoctorTheme.js" as Theme

AbstractButton {
    id: root
    property string time: ""
    property string checkId: ""
    property string title: ""
    property string symbol: ""
    property string tone: "neutral"
    property string evidenceId: ""
    implicitHeight: 31
    enabled: evidenceId.length > 0
    focusPolicy: Qt.StrongFocus
    Accessible.name: time + " " + checkId + " " + title
    Accessible.description: enabled ? "Open related evidence" : "No retained evidence is linked to this lifecycle event"
    contentItem: RowLayout {
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 9
        Label { text: root.time; color: Theme.textMuted; font.family: Theme.mono; font.pixelSize: 10; Layout.preferredWidth: 77 }
        Label { text: root.checkId; color: Theme.information; font.family: Theme.mono; font.pixelSize: 10; Layout.preferredWidth: 82; elide: Text.ElideRight }
        Label { text: root.title; color: Theme.textSecondary; font.family: Theme.ui; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: Theme.tone(root.tone) }
    }
    background: Rectangle {
        radius: 1
        border.width: root.activeFocus ? 1 : 0
        border.color: Theme.focus
        color: !root.enabled ? "transparent" : root.down ? Theme.pressed : root.hovered ? Theme.hover : "transparent"
        opacity: root.enabled ? 1.0 : 0.5
    }
}
