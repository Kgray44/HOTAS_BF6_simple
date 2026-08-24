import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Shared by Legacy, Standard, and Top Gun. The caller supplies the existing
// theme tokens, so readiness adds no competing visual system.
Item {
    id: root
    property var backendObject
    property var themeTokens: null
    property bool legacy: false
    readonly property color panelColor: themeTokens ? themeTokens.panel : "#1a1d23"
    readonly property color insetColor: themeTokens ? themeTokens.panelInset : "#10171b"
    readonly property color borderColor: themeTokens ? themeTokens.border : "#435660"
    readonly property color textColor: themeTokens ? themeTokens.text : "#e8eeee"
    readonly property color mutedColor: themeTokens ? themeTokens.textMuted : "#9dafb4"
    readonly property color readyColor: themeTokens ? themeTokens.ready : "#8fd5c9"
    readonly property color warningColor: themeTokens ? themeTokens.warning : "#d4ad69"
    readonly property color dangerColor: themeTokens ? themeTokens.danger : "#ca9090"
    readonly property color buttonColor: themeTokens ? themeTokens.buttonSurface : "#324f5a"
    readonly property color secondaryButtonColor: themeTokens ? themeTokens.buttonSecondary : "#222c32"
    readonly property int radius: themeTokens ? themeTokens.panelRadius : 4

    implicitWidth: 680
    implicitHeight: readinessColumn.implicitHeight

    function severityColor(severity) {
        if (severity === "warning") return warningColor
        if (severity === "error") return dangerColor
        if (severity === "ready") return readyColor
        return mutedColor
    }

    ColumnLayout {
        id: readinessColumn
        width: parent.width
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle { implicitWidth: 9; implicitHeight: 9; radius: root.radius > 2 ? 5 : 1
                color: root.backendObject && root.backendObject.controllerReadinessState === "READY"
                    ? root.readyColor : root.warningColor }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "CONTROLLER SETUP"; color: root.textColor; font.pixelSize: 17; font.bold: true }
                Text { text: "Detect → Explain → Fix → Verify → Ready"; color: root.mutedColor; font.pixelSize: 10 }
            }
            Text { text: root.backendObject ? root.backendObject.controllerReadinessState : "IDLE"
                color: root.readyColor; font.pixelSize: 10; font.bold: true }
        }

        Text {
            Layout.fillWidth: true
            text: root.backendObject ? root.backendObject.controllerReadinessStatus : "Inspecting controller readiness…"
            color: root.textColor
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(88, checksColumn.implicitHeight + 22)
            color: root.insetColor
            border.color: root.borderColor
            radius: root.radius
            ColumnLayout {
                id: checksColumn
                anchors.fill: parent
                anchors.margins: 11
                spacing: 7
                Text { text: "WHAT HOTAS BF6 FOUND"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                Repeater {
                    model: root.backendObject ? root.backendObject.controllerReadinessChecks : []
                    delegate: RowLayout {
                        width: checksColumn.width
                        spacing: 8
                        Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: root.severityColor(modelData.severity) }
                        Text { Layout.fillWidth: true; text: modelData.message; color: root.textColor
                            font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }
            }
        }

        Rectangle {
            visible: root.backendObject && root.backendObject.controllerReadinessProposedChanges.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Math.max(72, changesColumn.implicitHeight + 22) : 0
            color: root.panelColor
            border.color: root.warningColor
            radius: root.radius
            ColumnLayout {
                id: changesColumn
                anchors.fill: parent
                anchors.margins: 11
                spacing: 6
                Text { text: "HOTAS BF6 RECOMMENDS"; color: root.warningColor; font.pixelSize: 9; font.bold: true }
                Repeater {
                    model: root.backendObject ? root.backendObject.controllerReadinessProposedChanges : []
                    delegate: RowLayout {
                        width: changesColumn.width
                        spacing: 8
                        Text { text: "•"; color: root.warningColor; font.bold: true }
                        Text { Layout.fillWidth: true; text: modelData.message; color: root.textColor
                            font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "Automatic setup stops Mapping, releases vJoy Device 1, asks Windows for administrator approval only for driver changes, then verifies the result. It never clears other HidHide rules or alters unrelated vJoy devices."
            color: root.mutedColor
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            Button {
                id: recheckButton
                text: "RECHECK"
                onClicked: root.backendObject.inspectControllerReadiness()
                contentItem: Text { text: recheckButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.secondaryButtonColor; border.color: root.borderColor }
            }
            Button {
                id: undoButton
                visible: root.backendObject && root.backendObject.controllerSetupCanUndo
                text: "UNDO AUTOMATIC SETUP"
                onClicked: undoDialog.open()
                contentItem: Text { text: undoButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.secondaryButtonColor; border.color: root.warningColor }
            }
            Button {
                id: applyButton
                text: "APPLY AUTOMATICALLY"
                enabled: root.backendObject && root.backendObject.controllerSetupCanApply && !root.backendObject.controllerSetupInProgress
                onClicked: applyDialog.open()
                contentItem: Text { text: applyButton.text; color: applyButton.enabled ? root.textColor : root.mutedColor; font.pixelSize: 10; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: parent.enabled ? root.buttonColor : root.insetColor
                    border.color: parent.enabled ? root.readyColor : root.borderColor }
            }
        }
    }

    Dialog {
        id: applyDialog
        parent: Overlay.overlay
        modal: true
        title: "Apply automatic controller setup?"
        standardButtons: Dialog.Cancel
        width: Math.min(560, root.width)
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "HOTAS BF6 will make only the changes shown above. Mapping will remain Off. Windows will ask for approval before a driver configuration command runs."; wrapMode: Text.WordWrap; color: root.textColor; font.pixelSize: 11 }
            Button { id: confirmApplyButton; text: "APPLY AUTOMATICALLY"; Layout.alignment: Qt.AlignRight
                onClicked: { applyDialog.close(); root.backendObject.applyControllerReadiness() }
                contentItem: Text { text: confirmApplyButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.buttonColor; border.color: root.readyColor } }
        }
    }

    Dialog {
        id: undoDialog
        parent: Overlay.overlay
        modal: true
        title: "Undo automatic setup?"
        standardButtons: Dialog.Cancel
        width: Math.min(540, root.width)
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "HOTAS BF6 will reverse only the entries it added in this session. Existing HidHide allowlist entries, hidden devices, and unrelated vJoy devices are preserved."; wrapMode: Text.WordWrap; color: root.textColor; font.pixelSize: 11 }
            Button { id: confirmUndoButton; text: "UNDO AUTOMATIC SETUP"; Layout.alignment: Qt.AlignRight
                onClicked: { undoDialog.close(); root.backendObject.undoControllerReadiness() }
                contentItem: Text { text: confirmUndoButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.secondaryButtonColor; border.color: root.warningColor } }
        }
    }
}
