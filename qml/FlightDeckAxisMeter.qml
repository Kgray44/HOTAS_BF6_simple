import QtQuick 6.5
import QtQuick.Layouts 6.5

FlightDeckCard {
    id: root
    property var axis: ({})
    readonly property real numericValue: Number(axis.calibrated || 0)
    readonly property real fill: axis.unipolar ? Math.max(0, Math.min(1, numericValue)) : Math.max(0, Math.min(1, (numericValue + 1) / 2))
    readonly property string percent: (numericValue >= 0 ? "+" : "") + Math.round(numericValue * 100) + "%"

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
                font.pixelSize: 11
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root.axis.virtualValid ? (root.axis.outputAlias || root.axis.target || "Routed") : (root.axis.target || "Not routed")
                color: root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: 8
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
            text: root.percent
            color: root.tokens.textPrimary
            font.family: root.tokens.telemetryFont
            font.pixelSize: 11
            font.bold: true
            Layout.preferredWidth: 46
            horizontalAlignment: Text.AlignRight
        }
    }
}
