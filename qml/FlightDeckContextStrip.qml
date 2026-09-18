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
    // The task journal itself lives in AppBackend. This is Flight Deck's one
    // setup entry; it resumes a task or starts neutral guidance only.
    signal setupActionRequested()

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
    readonly property bool profilesMatch: hasEditingProfile && editingId === activeId
    readonly property bool showActiveRigSummary: activeRigName.length > 0 && width >= tokens.scale(880)
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

    // This is a toolbar below the selectors, not a second header.  The Flow
    // only takes an additional line when the available content width and the
    // selected text size genuinely require it.
    implicitHeight: contextToolbar.implicitHeight + tokens.space12
    radius: tokens.radiusControl
    color: tokens.secondarySurface
    border.width: 1
    border.color: tokens.border

    RowLayout {
        id: contextToolbar
        anchors.fill: parent
        anchors.leftMargin: tokens.space12
        anchors.rightMargin: tokens.space12
        anchors.topMargin: tokens.space6
        anchors.bottomMargin: tokens.space6
        spacing: tokens.space8

        Flow {
            id: contextGroups
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: tokens.space12

            Text {
                objectName: "flightDeckContextEditing"
                text: root.primaryEditingText
                color: root.hasEditingProfile ? tokens.textPrimary : tokens.textMuted
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodyStrong
                font.weight: Font.DemiBold
                width: Math.min(implicitWidth, tokens.scale(300))
                elide: Text.ElideRight
                Accessible.name: text
            }
            Text {
                objectName: "flightDeckContextActive"
                text: root.profilesMatch ? "Active profile" : root.activeProfileText
                color: root.hasActiveProfile ? tokens.textPrimary : tokens.textMuted
                font.family: tokens.bodyFont
                font.pixelSize: tokens.body
                width: Math.min(implicitWidth, tokens.scale(300))
                elide: Text.ElideRight
                Accessible.name: root.activeProfileText
            }
            Text {
                objectName: "flightDeckContextTemporary"
                visible: root.runtimeOverride
                text: root.temporaryProfileText
                color: tokens.attention
                font.family: tokens.bodyFont
                font.pixelSize: tokens.body
                font.weight: Font.DemiBold
                width: Math.min(implicitWidth, tokens.scale(340))
                elide: Text.ElideRight
                Accessible.name: text
            }
            Text {
                objectName: "flightDeckContextActiveRig"
                visible: root.showActiveRigSummary
                text: "Active rig: " + root.activeRigName
                color: tokens.textSecondary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                width: Math.min(implicitWidth, tokens.scale(240))
                elide: Text.ElideRight
                Accessible.name: text
            }
        }

        Button {
            id: setupAction
            objectName: "flightDeckContextSetupAction"
            text: backendObject.hasSetupAssistantTask ? "Continue setup" : "Guided setup"
            implicitHeight: tokens.compactControlHeight
            leftPadding: tokens.space8
            rightPadding: tokens.space8
            font.family: tokens.bodyFont
            font.pixelSize: tokens.bodySmall
            Accessible.name: text
            onClicked: root.setupActionRequested()
            contentItem: Text {
                text: setupAction.text
                color: tokens.textSecondary
                font: setupAction.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: tokens.radiusControl
                color: setupAction.hovered ? tokens.elevatedSurface : "transparent"
                border.width: 1
                border.color: tokens.border
            }
        }

        Button {
            id: technicalToggle
            objectName: "flightDeckContextTechnicalToggle"
            text: root.technicalDetailsVisible ? "HIDE DETAILS" : "DETAILS"
            implicitHeight: tokens.compactControlHeight
            leftPadding: tokens.space8
            rightPadding: tokens.space8
            font.family: tokens.bodyFont
            font.pixelSize: tokens.bodySmall
            Accessible.name: text + ", context information"
            onClicked: {
                if (detailsPopover.opened)
                    detailsPopover.close()
                else
                    detailsPopover.open()
            }
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

    Popup {
        id: detailsPopover
        objectName: "flightDeckContextDetailsPopover"
        parent: Overlay.overlay
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: tokens.space12
        width: Math.min(tokens.scale(560), Math.max(tokens.scale(300), parent.width - tokens.space24))
        x: {
            const point = technicalToggle.mapToItem(parent, technicalToggle.width, 0)
            return Math.max(tokens.space12, Math.min(point.x - width, parent.width - width - tokens.space12))
        }
        y: technicalToggle.mapToItem(parent, 0, technicalToggle.height).y + tokens.space6
        onOpened: {
            root.technicalDetailsVisible = true
            detailsClose.forceActiveFocus()
        }
        onClosed: {
            root.technicalDetailsVisible = false
            technicalToggle.forceActiveFocus()
        }
        background: Rectangle {
            radius: tokens.radiusPanel
            color: tokens.elevatedSurface
            border.width: 1
            border.color: tokens.border
        }
        contentItem: ColumnLayout {
            width: detailsPopover.availableWidth
            spacing: tokens.space8
            Text {
                Layout.fillWidth: true
                text: "Context details"
                color: tokens.textPrimary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodyStrong
                font.weight: Font.DemiBold
            }
            Text {
                objectName: "flightDeckContextScopeHelp"
                Layout.fillWidth: true
                text: "Viewing a controller does not activate it. Changes to the active profile can still affect live output."
                color: tokens.textSecondary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "flightDeckContextTechnicalDetails"
                Layout.fillWidth: true
                text: "Active rig: " + (root.activeRigName.length > 0 ? root.activeRigName : "None")
                    + "\nViewing controller: " + (root.viewedControllerName.length > 0 ? root.viewedControllerName : "None")
                    + "\nActive rig output: " + (root.activeOutputName.length > 0 ? root.activeOutputName : "None")
                    + "\n\n" + root.technicalDetailsText
                color: tokens.textMuted
                font.family: tokens.telemetryFont
                font.pixelSize: tokens.caption
                wrapMode: Text.WrapAnywhere
            }
            Button {
                id: detailsClose
                objectName: "flightDeckContextDetailsClose"
                text: "CLOSE DETAILS"
                Layout.alignment: Qt.AlignRight
                implicitHeight: tokens.compactControlHeight
                font.family: tokens.bodyFont
                font.pixelSize: tokens.bodySmall
                onClicked: detailsPopover.close()
            }
        }
    }
}
