import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// This strip is passive context: it describes the canonical control plane
// without changing selection, activation, or mapping state.
Rectangle {
    id: root
    required property var backendObject
    required property var tokens

    // Presentation-only test seam. Production values continue to come from
    // the canonical backend properties below; this does not issue a command.
    property var presentationOverride: null
    property bool technicalDetailsVisible: false

    function contextValue(name, fallback) {
        if (presentationOverride === null || presentationOverride === undefined)
            return fallback
        const value = presentationOverride[name]
        return value === undefined || value === null ? fallback : value
    }

    function selectedControllerIdentity() {
        const devices = backendObject.selectedDevices || []
        if (devices.length === 1)
            return String(devices[0].id || "")
        return String(backendObject.activeControllerRecordId || "")
    }

    function activeOutputIdentity() {
        const outputs = backendObject.virtualOutputLayouts || []
        for (let index = 0; index < outputs.length; ++index) {
            if (Boolean(outputs[index].active))
                return String(outputs[index].id || "")
        }
        for (let index = 0; index < outputs.length; ++index) {
            if (String(outputs[index].name || "") === String(backendObject.activeOutputLayoutName || ""))
                return String(outputs[index].id || "")
        }
        return ""
    }

    readonly property string editingId: String(contextValue("editingId", backendObject.selectedProfileId || ""))
    readonly property string activeId: String(contextValue("activeId", backendObject.activeProfileId || ""))
    readonly property string effectiveId: String(contextValue("effectiveId", backendObject.effectiveProfileId || ""))
    readonly property string editingName: String(contextValue("editingName", backendObject.selectedProfileDisplayName || ""))
    readonly property string activeName: String(contextValue("activeName", backendObject.activeProfileDisplayName || ""))
    readonly property string effectiveName: String(contextValue("effectiveName", backendObject.effectiveProfileDisplayName || ""))
    readonly property string sourceLabel: String(contextValue("sourceLabel", backendObject.profileSourceLabel || "Manual base profile"))
    readonly property string activeRigName: String(contextValue("activeRigName", backendObject.activeDeviceRigName || ""))
    readonly property string activeRigId: String(contextValue("activeRigId", backendObject.activeDeviceRigId || ""))
    readonly property string viewedControllerName: String(contextValue("viewedControllerName", backendObject.selectedDeviceLabel || ""))
    readonly property string viewedControllerId: String(contextValue("viewedControllerId", selectedControllerIdentity()))
    readonly property string activeOutputName: String(contextValue("activeOutputName", backendObject.activeOutputLayoutName || ""))
    readonly property string activeOutputId: String(contextValue("activeOutputId", activeOutputIdentity()))
    readonly property bool hasEditingProfile: editingId.length > 0
    readonly property bool hasActiveProfile: activeId.length > 0
    readonly property bool runtimeOverride: effectiveId.length > 0
        && (effectiveId !== activeId || sourceLabel !== "Manual base profile")
    readonly property bool wideProfileLayout: width >= tokens.scale(runtimeOverride ? 1040 : 760)
    readonly property string primaryEditingText: hasEditingProfile
        ? "Editing: " + editingName : "Choose a profile to edit"
    readonly property string activeProfileText: hasActiveProfile
        ? "Active profile: " + activeName : "No active profile"
    readonly property string temporaryProfileText: runtimeOverride
        ? "Temporary profile: " + effectiveName
          + (sourceLabel.length > 0 ? " — " + sourceLabel : "") : ""
    readonly property string technicalDetailsText: "Profile IDs — editing: "
        + (editingId.length > 0 ? editingId : "none")
        + " | active: " + (activeId.length > 0 ? activeId : "none")
        + " | effective: " + (effectiveId.length > 0 ? effectiveId : "none")
        + "\nRig ID: " + (activeRigId.length > 0 ? activeRigId : "none")
        + " | Controller ID: " + (viewedControllerId.length > 0 ? viewedControllerId : "none")
        + " | Output ID: " + (activeOutputId.length > 0 ? activeOutputId : "none")

    implicitHeight: contextContent.implicitHeight + tokens.space16
    radius: tokens.radiusControl
    color: tokens.secondarySurface
    border.width: 1
    border.color: tokens.border

    ColumnLayout {
        id: contextContent
        anchors.fill: parent
        anchors.leftMargin: tokens.space12
        anchors.rightMargin: tokens.space12
        anchors.topMargin: tokens.space8
        anchors.bottomMargin: tokens.space8
        spacing: tokens.space8

        GridLayout {
            Layout.fillWidth: true
            columns: root.wideProfileLayout ? (root.runtimeOverride ? 3 : 2) : 1
            columnSpacing: tokens.space12
            rowSpacing: tokens.space4

            Text {
                objectName: "flightDeckContextEditing"
                Layout.fillWidth: true
                Layout.minimumWidth: root.wideProfileLayout ? tokens.scale(210) : 0
                text: root.primaryEditingText
                color: root.hasEditingProfile ? tokens.textPrimary : tokens.textMuted
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodyStrong
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "flightDeckContextActive"
                Layout.fillWidth: true
                Layout.minimumWidth: root.wideProfileLayout ? tokens.scale(210) : 0
                text: root.activeProfileText
                color: root.hasActiveProfile ? tokens.textPrimary : tokens.textMuted
                font.family: tokens.bodyFont
                font.pixelSize: tokens.body
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "flightDeckContextTemporary"
                Layout.fillWidth: true
                Layout.minimumWidth: root.wideProfileLayout ? tokens.scale(240) : 0
                visible: root.runtimeOverride
                text: root.temporaryProfileText
                color: tokens.attention
                font.family: tokens.bodyFont
                font.pixelSize: tokens.body
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: tokens.divider
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= tokens.scale(840) ? 3 : 1
            columnSpacing: tokens.space12
            rowSpacing: tokens.space4

            Text {
                objectName: "flightDeckContextActiveRig"
                Layout.fillWidth: true
                text: "Active rig: " + (root.activeRigName.length > 0 ? root.activeRigName : "None")
                color: tokens.textSecondary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "flightDeckContextViewedController"
                Layout.fillWidth: true
                text: "Viewing controller: " + (root.viewedControllerName.length > 0 ? root.viewedControllerName : "None")
                color: tokens.textSecondary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "flightDeckContextActiveOutput"
                Layout.fillWidth: true
                text: "Active rig output: " + (root.activeOutputName.length > 0 ? root.activeOutputName : "None")
                color: tokens.textSecondary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: tokens.space8
            Text {
                Layout.fillWidth: true
                text: "Viewing a controller does not activate it. Changes to the active profile can still affect live output."
                color: tokens.textMuted
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                wrapMode: Text.WordWrap
            }
            Button {
                id: technicalToggle
                objectName: "flightDeckContextTechnicalToggle"
                text: root.technicalDetailsVisible ? "HIDE TECHNICAL DETAILS" : "TECHNICAL DETAILS"
                implicitHeight: tokens.compactControlHeight
                leftPadding: tokens.space8
                rightPadding: tokens.space8
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                onClicked: root.technicalDetailsVisible = !root.technicalDetailsVisible
                contentItem: Text {
                    text: technicalToggle.text
                    color: tokens.textMuted
                    font: technicalToggle.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: tokens.radiusControl
                    color: technicalToggle.hovered ? tokens.elevatedSurface : "transparent"
                    border.width: 1
                    border.color: tokens.border
                }
            }
        }

        Text {
            objectName: "flightDeckContextTechnicalDetails"
            Layout.fillWidth: true
            visible: root.technicalDetailsVisible
            text: root.technicalDetailsText
            color: tokens.textMuted
            font.family: tokens.telemetryFont
            font.pixelSize: tokens.caption
            wrapMode: Text.WrapAnywhere
        }
    }
}
