import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

FlightDeckCard {
    id: root
    property var axis: ({})
    readonly property real numericValue: Number(axis.calibrated || 0)
    readonly property real mappedValue: Number(axis.virtualValue || 0)
    readonly property real fill: axis.unipolar ? Math.max(0, Math.min(1, numericValue)) : Math.max(0, Math.min(1, (numericValue + 1) / 2))
    readonly property string percent: (numericValue >= 0 ? "+" : "") + Math.round(numericValue * 100) + "%"
    signal editRequested(int axisIndex)

    visible: axis && axis.available
    implicitHeight: visible ? 58 : 0

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.tokens.space12
        spacing: root.tokens.space12

        ColumnLayout {
            // Keep labels compact so the live meter is the dominant visual
            // on wide cards, while still reserving enough room at small sizes.
            Layout.minimumWidth: root.width < 460 ? 92 : 120
            Layout.preferredWidth: Math.min(180, Math.max(120, root.width * 0.18))
            Layout.maximumWidth: root.width < 460 ? 124 : 180
            Layout.fillWidth: false
            spacing: 1
            Text {
                text: root.axis.label || "Axis"
                color: root.tokens.textPrimary
                font.family: root.tokens.displayFont
                font.pixelSize: root.tokens.scale(11)
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root.axis.virtualValid
                    ? "MAPPED · " + (root.axis.outputAlias || root.axis.target || "Virtual output")
                        + " " + (root.mappedValue >= 0 ? "+" : "") + Math.round(root.mappedValue * 100) + "%"
                    : root.axis.target === "Disabled" ? "UNASSIGNED · no virtual output"
                    : "NOT PUBLISHED · " + (root.axis.outputAlias || root.axis.target || "virtual output")
                color: root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: root.tokens.scale(8)
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.minimumWidth: Math.max(72, Math.min(320, root.width * 0.45))
            Layout.preferredHeight: 8
            radius: height / 2
            color: root.tokens.primarySurface
            border.width: 1
            border.color: root.tokens.border
            Rectangle {
                width: Math.max(4, parent.width * root.fill)
                height: parent.height
                radius: parent.radius
                color: root.axis.virtualValid ? root.tokens.healthy : root.tokens.informational
            }
            Rectangle {
                visible: !root.axis.unipolar
                width: 1
                height: parent.height + 6
                anchors.verticalCenter: parent.verticalCenter
                x: parent.width / 2
                color: root.tokens.textMuted
            }
        }
        Text {
            text: "PHYSICAL " + root.percent
            color: root.tokens.textPrimary
            font.family: root.tokens.telemetryFont
            font.pixelSize: root.tokens.scale(11)
            font.bold: true
            Layout.preferredWidth: 84
            horizontalAlignment: Text.AlignRight
        }
        Button {
            text: "EDIT"
            focusPolicy: Qt.StrongFocus
            implicitHeight: root.tokens.compactControlHeight
            onClicked: root.editRequested(Number(root.axis.index))
            background: Rectangle { radius: root.tokens.radiusControl; color: parent.down ? root.tokens.secondarySurface : "transparent"; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border; border.width: parent.activeFocus ? 2 : 1 }
            contentItem: Text { text: parent.text; color: root.tokens.textSecondary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.scale(8); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
    }
}
