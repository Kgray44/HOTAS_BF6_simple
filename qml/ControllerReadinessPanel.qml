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
    readonly property bool topGun: themeTokens && themeTokens.topGun
    property bool instructionsExpanded: false
    signal closeRequested()
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
                    ? root.readyColor : root.backendObject && root.backendObject.controllerReadinessState === "ACTION REQUIRED"
                        ? root.dangerColor : root.warningColor }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: root.topGun ? "HOTAS SYSTEM CHECK" : "HOTAS SETUP & VERIFICATION"
                    color: root.textColor; font.pixelSize: 17; font.bold: true
                    font.family: root.topGun ? root.themeTokens.displayFont : undefined }
                Text { text: root.topGun ? "PHYSICAL INPUT · VJOY OUTPUT · HIDHIDE ISOLATION"
                                         : "Verify your physical HOTAS, vJoy, and HidHide configuration."
                    color: root.mutedColor; font.pixelSize: 10 }
            }
            Text { text: root.backendObject ? root.backendObject.controllerReadinessState : "NOT CHECKED"
                color: root.severityColor(root.backendObject && root.backendObject.controllerReadinessState === "READY" ? "ready"
                    : root.backendObject && root.backendObject.controllerReadinessState === "ACTION REQUIRED" ? "error" : "warning")
                font.pixelSize: 10; font.bold: true; font.family: root.topGun ? root.themeTokens.telemetryFont : undefined }
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
                Text { text: root.topGun ? "SYSTEM TELEMETRY" : "VERIFICATION RESULTS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                Repeater {
                    model: root.backendObject ? root.backendObject.controllerReadinessChecks : []
                    delegate: RowLayout {
                        width: checksColumn.width
                        spacing: 8
                        Rectangle { implicitWidth: 6; implicitHeight: 6; radius: root.topGun ? 0 : 3; color: root.severityColor(modelData.severity) }
                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                            RowLayout { Layout.fillWidth: true
                                Text { text: modelData.name; color: root.textColor; font.pixelSize: 10; font.bold: true
                                    font.family: root.topGun ? root.themeTokens.telemetryFont : undefined }
                                Item { Layout.fillWidth: true }
                                Text { text: modelData.state; color: root.severityColor(modelData.severity); font.pixelSize: 9; font.bold: true
                                    font.family: root.topGun ? root.themeTokens.telemetryFont : undefined }
                            }
                            Text { Layout.fillWidth: true; text: modelData.message; color: root.mutedColor
                                font.pixelSize: 9; wrapMode: Text.WordWrap }
                        }
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

        Rectangle {
            visible: root.instructionsExpanded
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? instructionsText.implicitHeight + 22 : 0
            color: root.panelColor
            border.color: root.warningColor
            radius: root.radius
            Text {
                id: instructionsText
                anchors.fill: parent
                anchors.margins: 11
                text: "Resolve the highlighted condition, then choose VERIFY AGAIN. HOTAS BF6 will never change vJoy or HidHide automatically unless it offers FIX AUTOMATICALLY for that specific issue."
                color: root.textColor
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.backendObject && root.backendObject.controllerSetupInProgress
                ? "Verification is running. Mapping is restored to the state it had before this check."
                : "Full verification temporarily releases HOTAS BF6's vJoy ownership only when needed, then restores your prior Mapping On/Off state. It never clears unrelated HidHide rules or alters unrelated vJoy devices."
            color: root.mutedColor
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: root.backendObject ? "Last verified: " + root.backendObject.controllerReadinessLastChecked : "Last verified: Not yet verified"
            color: root.mutedColor
            font.pixelSize: 9
            horizontalAlignment: Text.AlignRight
            font.family: root.topGun ? root.themeTokens.telemetryFont : undefined
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            Button {
                id: recheckButton
                text: root.backendObject && root.backendObject.controllerSetupInProgress ? "VERIFYING..." : "VERIFY AGAIN"
                enabled: root.backendObject && !root.backendObject.controllerSetupInProgress
                onClicked: root.backendObject.verifyHotasSetup()
                contentItem: Text { text: recheckButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.secondaryButtonColor; border.color: root.borderColor }
            }
            Button {
                id: undoButton
                visible: root.backendObject && root.backendObject.controllerSetupCanUndo
                text: "UNDO AUTOMATIC REPAIR"
                onClicked: undoDialog.open()
                contentItem: Text { text: undoButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.secondaryButtonColor; border.color: root.warningColor }
            }
            Button {
                id: contextualActionButton
                visible: root.backendObject && root.backendObject.controllerReadinessRecommendedAction.length > 0
                text: root.backendObject ? root.backendObject.controllerReadinessRecommendedAction : ""
                enabled: root.backendObject && !root.backendObject.controllerSetupInProgress
                onClicked: {
                    if (text === "FIX AUTOMATICALLY") applyDialog.open()
                    else if (text === "RUN FULL VERIFICATION") root.backendObject.verifyHotasSetup()
                    else root.instructionsExpanded = true
                }
                contentItem: Text { text: contextualActionButton.text; color: contextualActionButton.enabled ? root.textColor : root.mutedColor; font.pixelSize: 10; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: contextualActionButton.enabled ? root.buttonColor : root.insetColor
                    border.color: contextualActionButton.enabled ? root.readyColor : root.borderColor }
            }
            Button {
                id: closeButton
                text: "CLOSE"
                onClicked: root.closeRequested()
                contentItem: Text { text: closeButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.secondaryButtonColor; border.color: root.borderColor }
            }
        }
    }

    Dialog {
        id: applyDialog
        parent: Overlay.overlay
        modal: true
        title: "Fix HOTAS configuration automatically?"
        standardButtons: Dialog.Cancel
        width: Math.min(560, root.width)
        background: Rectangle { color: root.panelColor; border.color: root.warningColor; radius: root.radius }
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "HOTAS BF6 will make only the changes shown above. Your prior Mapping On/Off state is restored afterward. Windows will ask for approval before a driver configuration command runs."; wrapMode: Text.WordWrap; color: root.textColor; font.pixelSize: 11 }
            Button { id: confirmApplyButton; text: "FIX AUTOMATICALLY"; Layout.alignment: Qt.AlignRight
                onClicked: { applyDialog.close(); root.backendObject.applyControllerReadiness() }
                contentItem: Text { text: confirmApplyButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.buttonColor; border.color: root.readyColor } }
        }
    }

    Dialog {
        id: undoDialog
        parent: Overlay.overlay
        modal: true
        title: "Undo automatic HOTAS repair?"
        standardButtons: Dialog.Cancel
        width: Math.min(540, root.width)
        background: Rectangle { color: root.panelColor; border.color: root.warningColor; radius: root.radius }
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; text: "HOTAS BF6 will reverse only the entries it added in this session. Existing HidHide allowlist entries, hidden devices, and unrelated vJoy devices are preserved."; wrapMode: Text.WordWrap; color: root.textColor; font.pixelSize: 11 }
            Button { id: confirmUndoButton; text: "UNDO AUTOMATIC REPAIR"; Layout.alignment: Qt.AlignRight
                onClicked: { undoDialog.close(); root.backendObject.undoControllerReadiness() }
                contentItem: Text { text: confirmUndoButton.text; color: root.textColor; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: root.radius; color: root.secondaryButtonColor; border.color: root.warningColor } }
        }
    }
}
