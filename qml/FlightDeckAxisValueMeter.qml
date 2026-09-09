import QtQuick 6.5
import QtQuick.Layouts 6.5

// A small, snapshot-driven instrument used by the scan view. It only renders
// values already published by AppBackend; it never owns a sampling timer.
Item {
    id: root

    property QtObject tokens
    property string caption: "INPUT"
    property real value: 0
    property bool valid: true
    property bool unipolar: false
    property string unavailableText: "Unavailable"

    readonly property real boundedValue: unipolar ? Math.max(0, Math.min(1, value)) : Math.max(-1, Math.min(1, value))
    readonly property real fill: unipolar ? boundedValue : (boundedValue + 1) * 0.5
    readonly property string percentage: unipolar ? Math.round(boundedValue * 100) + "%" : (boundedValue >= 0 ? "+" : "") + Math.round(boundedValue * 100) + "%"

    implicitHeight: 48

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.caption
                color: root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: 8
                font.bold: true
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                text: root.valid ? root.percentage : root.unavailableText
                color: root.valid ? root.tokens.textPrimary : root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: 10
                font.bold: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            radius: height / 2
            color: root.tokens.primarySurface
            border.width: 1
            border.color: root.tokens.border

            Rectangle {
                visible: root.valid
                width: Math.max(3, parent.width * root.fill)
                height: parent.height
                radius: parent.radius
                color: root.tokens.accent
            }
            Rectangle {
                visible: !root.unipolar
                width: 1
                height: parent.height + 6
                anchors.verticalCenter: parent.verticalCenter
                x: parent.width / 2
                color: root.tokens.textMuted
            }
        }
    }
}
