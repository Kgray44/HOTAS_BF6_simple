import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

FlightDeckCard {
    id: root
    property string eyebrow: "SYSTEM"
    property string title: "Checking"
    property string detail: ""
    property string tone: "informational"
    property string actionLabel: ""
    signal actionRequested

    Layout.fillWidth: true
    implicitHeight: healthContent.implicitHeight + contentPadding * 2

    ColumnLayout {
        id: healthContent
        anchors.fill: parent
        anchors.margins: root.contentPadding
        spacing: root.tokens.space8

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.eyebrow
                color: root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: 9
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Rectangle {
                width: 8
                height: 8
                radius: width / 2
                color: root.tokens.statusColor(root.tone)
                Accessible.name: root.tone + " status"
            }
        }
        Text {
            text: root.title
            color: root.tokens.textPrimary
            font.family: root.tokens.displayFont
            font.pixelSize: 15
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            text: root.detail
            color: root.tokens.textSecondary
            font.pixelSize: 10
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Button {
            visible: root.actionLabel.length > 0
            text: root.actionLabel
            implicitHeight: root.tokens.compactControlHeight
            leftPadding: root.tokens.space12
            rightPadding: root.tokens.space12
            focusPolicy: Qt.StrongFocus
            Accessible.name: root.actionLabel
            onClicked: root.actionRequested()
            background: Rectangle {
                radius: root.tokens.radiusControl
                color: parent.down ? root.tokens.accentMuted : parent.hovered ? root.tokens.secondarySurface : "transparent"
                border.width: parent.activeFocus ? 2 : 1
                border.color: parent.activeFocus ? root.tokens.focus : root.tokens.accent
            }
            contentItem: Text {
                text: parent.text
                color: root.tokens.accent
                font.family: root.tokens.telemetryFont
                font.pixelSize: 9
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
