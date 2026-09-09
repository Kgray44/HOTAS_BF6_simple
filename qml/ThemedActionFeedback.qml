import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Shared outcome surface for operations with a non-obvious result. Direct
// toggles keep their immediate visual state and do not need this component.
Rectangle {
    id: root
    property var result: ({})
    property var theme
    property bool legacy: false
    readonly property string severity: result && result.severity ? result.severity
        : result && result.success ? "success" : "error"
    readonly property color readyColor: theme ? theme.ready : "#8fd5c9"
    readonly property color warningColor: theme ? theme.warning : "#d4ad69"
    readonly property color dangerColor: theme ? theme.danger : "#ca9090"
    readonly property color textColor: theme ? theme.text : "#e8eeee"
    readonly property color mutedColor: theme ? theme.textMuted : "#9dafb4"
    readonly property color tone: severity === "error" ? dangerColor
        : severity === "warning" ? warningColor : readyColor
    readonly property bool inProgress: !!(result && result.inProgress)

    visible: !!(result && result.title)
    implicitHeight: visible ? feedbackBody.implicitHeight + 24 : 0
    radius: legacy ? 4 : (theme ? theme.controlRadius : 4)
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.10)
    border.color: tone

    RowLayout {
        id: feedbackBody
        anchors.fill: parent
        anchors.margins: 12
        spacing: 9
        Rectangle { width: 8; height: 8; radius: root.legacy ? 1 : 4; color: root.tone; Layout.alignment: Qt.AlignTop }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { Layout.fillWidth: true; text: root.result.title || ""; color: root.textColor; font.pixelSize: 11; font.bold: true; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; visible: !!root.result.message; text: root.result.message || ""; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
        BusyIndicator { visible: root.inProgress; running: visible; implicitWidth: 24; implicitHeight: 24 }
    }
}
