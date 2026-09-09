import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Popup {
    id: root
    property var health: ({})
    property var issues: []
    property var theme
    property bool legacy: false
    signal navigationRequested(var target)

    readonly property color panelColor: legacy ? "#182126" : (theme ? theme.panel : "#1a1d23")
    readonly property color insetColor: legacy ? "#10191d" : (theme ? theme.panelInset : "#10171b")
    readonly property color borderColor: legacy ? "#49616b" : (theme ? theme.border : "#435660")
    readonly property color textColor: legacy ? "#eef5f5" : (theme ? theme.text : "#e8eeee")
    readonly property color mutedColor: legacy ? "#9fb1b5" : (theme ? theme.textMuted : "#9dafb4")
    readonly property color readyColor: legacy ? "#9fcbbf" : (theme ? theme.ready : "#8fd5c9")
    readonly property color warningColor: legacy ? "#d6bd78" : (theme ? theme.warning : "#d4ad69")
    readonly property color dangerColor: legacy ? "#c98e97" : (theme ? theme.danger : "#ca9090")

    parent: Overlay.overlay
    modal: false
    focus: true
    padding: 0
    width: Math.min(470, Math.max(310, parent ? parent.width - 32 : 470))
    x: Math.max(0, Math.round(((parent ? parent.width : width) - width) / 2))
    y: Math.max(0, Math.round(((parent ? parent.height : height) - height) / 2))
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: root.legacy ? 4 : (root.theme ? root.theme.panelRadius : 5) }

    function tone(issue) {
        if (!issue || issue.severity === "note" || issue.severity === "info") return mutedColor
        if (issue.severity === "error" || issue.severity === "critical" || issue.severity === "offline") return dangerColor
        return warningColor
    }
    function openIssue(issue) {
        if (!issue) return
        navigationRequested(issue.navigationTarget || ({}))
        close()
    }

    contentItem: ColumnLayout {
        width: root.width - 28
        spacing: 9
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "APP HEALTH"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                Text { text: root.health.ready ? "Everything important is ready" : root.health.label || "Needs attention"; color: root.health.ready ? root.readyColor : root.warningColor; font.pixelSize: 16; font.bold: true }
            }
            Button { text: "×"; onClicked: root.close(); background: Rectangle { color: "transparent" } contentItem: Text { text: parent.text; color: root.mutedColor; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } }
        }
        Text { visible: root.health.ready && root.health.noteCount > 0; Layout.fillWidth: true; text: root.health.noteCount + " non-blocking note" + (root.health.noteCount === 1 ? "" : "s") + " remain available below."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
        Repeater {
            model: root.issues
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                visible: modelData.severity !== "waiting"
                implicitHeight: visible ? issueBody.implicitHeight + 18 : 0
                color: root.insetColor
                border.color: root.tone(modelData)
                radius: root.legacy ? 3 : (root.theme ? root.theme.controlRadius : 4)
                RowLayout {
                    id: issueBody
                    anchors.fill: parent; anchors.margins: 9; spacing: 8
                    Rectangle { width: 7; height: 7; radius: root.legacy ? 1 : 4; color: root.tone(modelData); Layout.alignment: Qt.AlignTop }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Text { Layout.fillWidth: true; text: modelData.title || "Needs attention"; color: root.textColor; font.pixelSize: 11; font.bold: true; wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; text: modelData.explanation || "Review this item for the next step."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                    Button { visible: !!(modelData.navigationTarget && modelData.navigationTarget.page !== undefined); text: "REVIEW"; onClicked: root.openIssue(modelData)
                        contentItem: Text { text: parent.text; color: root.textColor; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: parent.hovered ? (root.theme ? root.theme.buttonSecondaryHover : "#303d44") : (root.theme ? root.theme.buttonSecondary : "#222c32"); border.color: root.borderColor; radius: root.legacy ? 3 : (root.theme ? root.theme.controlRadius : 4) }
                    }
                }
            }
        }
        Text { visible: root.issues.length === 0; Layout.fillWidth: true; text: "No unresolved application issues."; color: root.mutedColor; font.pixelSize: 10 }
        Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor }
        Button { text: "OPEN DIAGNOSTICS"; Layout.alignment: Qt.AlignRight; onClicked: { root.navigationRequested({ page: 3, objectType: "diagnostics" }); root.close() }
            contentItem: Text { text: parent.text; color: root.textColor; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.hovered ? (root.theme ? root.theme.buttonSecondaryHover : "#303d44") : "transparent"; border.color: root.borderColor; radius: root.legacy ? 3 : (root.theme ? root.theme.controlRadius : 4) }
        }
    }
}
