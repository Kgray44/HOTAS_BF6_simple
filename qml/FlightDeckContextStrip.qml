import QtQuick 6.5
import QtQuick.Layouts 6.5

// A passive, identity-first context line. Names are helpful labels, but IDs
// are the canonical contract: a runtime override can use the same Profile ID
// as the selected editor context and labels are not unique identity.
Rectangle {
    id: root
    required property var backendObject
    required property var tokens

    readonly property string editingId: String(backendObject.selectedProfileId || "")
    readonly property string activeId: String(backendObject.activeProfileId || "")
    readonly property string effectiveId: String(backendObject.effectiveProfileId || "")
    readonly property string editingName: String(backendObject.selectedProfileDisplayName || "No profile selected")
    readonly property string activeName: String(backendObject.activeProfileDisplayName || "No profile active")
    readonly property string effectiveName: String(backendObject.effectiveProfileDisplayName || "No effective profile")
    readonly property string sourceLabel: String(backendObject.profileSourceLabel || "Manual base profile")
    readonly property bool runtimeOverride: effectiveId.length > 0
        && (effectiveId !== activeId || sourceLabel !== "Manual base profile")

    implicitHeight: contextContent.implicitHeight + tokens.space12
    radius: tokens.radiusControl
    color: tokens.secondarySurface
    border.color: tokens.border

    ColumnLayout {
        id: contextContent
        anchors.fill: parent
        anchors.leftMargin: tokens.space12
        anchors.rightMargin: tokens.space12
        anchors.topMargin: tokens.space6
        anchors.bottomMargin: tokens.space6
        spacing: tokens.space6
        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= 880 ? (root.runtimeOverride ? 3 : 2) : 1
            columnSpacing: tokens.space12
            rowSpacing: tokens.space4
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: root.width >= 880 ? 150 : 0
                text: "EDITING · " + root.editingName + "  [" + (root.editingId || "missing") + "]"
                color: tokens.textPrimary
                font.family: tokens.telemetryFont
                font.pixelSize: 9
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: root.width >= 880 ? 150 : 0
                text: "ACTIVE · " + root.activeName + "  [" + (root.activeId || "none") + "]"
                color: root.editingId.length > 0 && root.editingId === root.activeId
                    ? tokens.healthy : tokens.textSecondary
                font.family: tokens.telemetryFont
                font.pixelSize: 9
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                visible: root.runtimeOverride
                Layout.fillWidth: visible
                Layout.minimumWidth: visible && root.width >= 880 ? 170 : 0
                text: "EFFECTIVE · " + root.effectiveName + "  [" + root.effectiveId + "] · " + root.sourceLabel
                color: tokens.focus
                font.family: tokens.telemetryFont
                font.pixelSize: 9
                font.bold: true
                elide: Text.ElideRight
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.width >= 720
            text: "RIG · " + String(backendObject.activeDeviceRigName || "No active rig")
                + "   INPUT · " + String(backendObject.selectedDeviceLabel || "All saved devices")
                + "   OUTPUT · " + String(backendObject.activeOutputLayoutName || "No virtual output")
                + "   ·   viewing never activates"
            color: tokens.textMuted
            font.family: tokens.telemetryFont
            font.pixelSize: 8
            font.bold: true
            elide: Text.ElideRight
        }
    }
}
