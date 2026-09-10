import QtQuick 6.5
import QtQuick.Layouts 6.5

Rectangle {
    required property var tokens
    property string label: "STATUS"
    property string value: ""
    property string tone: "informational"
    implicitWidth: chipRow.implicitWidth + tokens.space24
    implicitHeight: tokens.compactControlHeight
    radius: tokens.radiusPill
    color: Qt.rgba(tokens.statusColor(tone).r, tokens.statusColor(tone).g, tokens.statusColor(tone).b, tokens.light ? 0.12 : 0.18)
    border.width: 1
    border.color: Qt.rgba(tokens.statusColor(tone).r, tokens.statusColor(tone).g, tokens.statusColor(tone).b, 0.72)

    RowLayout {
        id: chipRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: tokens.space12
        anchors.rightMargin: tokens.space12
        anchors.verticalCenter: parent.verticalCenter
        spacing: tokens.space8
        Rectangle {
            width: 7
            height: 7
            radius: width / 2
            color: parent.parent.tokens.statusColor(parent.parent.tone)
        }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: parent.parent.label + (parent.parent.value.length ? "  " + parent.parent.value : "")
            color: parent.parent.tokens.textPrimary
            font.family: parent.parent.tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            elide: Text.ElideRight
        }
    }
}
