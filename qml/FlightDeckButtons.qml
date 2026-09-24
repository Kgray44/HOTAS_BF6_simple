import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Native Flight Deck Buttons presentation. It reads the established bounded
// button/POV UI snapshots and invokes the existing configuration commands;
// no input-report work or runtime action is initiated by rendering this page.
Flickable {
    id: root
    objectName: "flightDeckButtons"

    property var readinessModel
    // Presentation-only test seams. Production reads AppBackend snapshots.
    property var buttonPresentationOverride: null
    property var povPresentationOverride: null
    property var povInputsPresentationOverride: null
    property var automationPresentationOverride: null
    readonly property bool guidedPresentation: themeManager.guidanceLevel === "Guided"
    property string inputDeviceNameOverride: ""
    property int expandedButtonIndex: -1
    property int expandedHatIndex: -1
    property int expandedPovDirection: -1
    property string filterMode: "all"
    property string searchText: ""
    property int conflictButtonIndex: -1
    property int conflictHatIndex: -1
    property int conflictDirectionIndex: -1
    property int conflictTarget: 0
    property var conflictOwner: ({})
    signal navigateToPage(int page)
    signal navigateToProfile(string profileId)
    signal navigateToAutomation(string automationId)
    signal requestButtonLearning()
    signal requestQuickMap()
    signal requestPovLearning(int virtualButton)
    signal requestDevicePicker()
    signal requestSetup(string intent, var context)
    signal requestFullAccess(int page, var context)

    // The normal grid is permanently keyed by Virtual Output.  Changing the
    // top-bar Selected Device changes only the source chooser inside an
    // expanded card; it never replaces every card with that device's buttons.
    readonly property var buttonItems: buttonPresentationOverride !== null ? buttonPresentationOverride : backend.buttons
    readonly property var buttonTelemetry: backend.buttonTelemetry
    // Keep the selected-device input projection separate from the permanent
    // virtual-output grid.  These are bounded snapshots: configuration only
    // changes when routes change, while input telemetry is indexed directly
    // for live physical feedback without any per-card backend query.
    readonly property var buttonConfiguration: backend.buttonConfiguration
    readonly property var buttonInputTelemetry: backend.buttonInputTelemetry
    readonly property var povItems: povPresentationOverride !== null ? povPresentationOverride : backend.povs
    readonly property var povInputItems: povInputsPresentationOverride !== null ? povInputsPresentationOverride : backend.povInputs
    readonly property var automationItems: automationPresentationOverride !== null ? automationPresentationOverride : backend.automationRules
    readonly property var plainOutputChoices: backend.buttonOutputChoices
    readonly property var profileChoices: backend.profileTriggerChoices
    readonly property var behaviorChoices: backend.profileTriggerBehaviorChoices
    readonly property var mappingControlChoices: backend.mappingControlActionChoices
    readonly property var nativePovChoices: backend.nativePovTargetChoices
    readonly property bool behaviorExpanded: {
        themeManager.guidancePolicyRevision
        return themeManager.guidanceSectionExpanded("buttons-behavior")
    }
    readonly property string inputDeviceName: inputDeviceNameOverride.length > 0
        ? inputDeviceNameOverride : (backend.selectedDeviceLabel || "Selected controller")
    readonly property bool hasVisibleButtons: visibleButtonCount() > 0
    // The active mapper device is deliberately not used here.  The top-bar
    // Selected Device is the sole physical-input editor context.
    readonly property string selectedInputDeviceId: selectedDeviceId()
    readonly property bool selectedInputDeviceConnected: {
        const devices = backend.selectedDevices || []
        for (let index = 0; index < devices.length; ++index) {
            const device = devices[index] || ({})
            if (String(device.id || "") === selectedInputDeviceId)
                return Boolean(device.connected)
        }
        return false
    }
    readonly property var selectedControllerInfo: controllerForSelectedDevice()
    readonly property int knownControllerCount: (backend.controllers || []).length
    readonly property int selectedControlCapabilityCount: Number(selectedControllerInfo.buttonCount || 0)
        + Number(selectedControllerInfo.povCount || 0)
    readonly property int selectedAxisCapabilityCount: Number(selectedControllerInfo.axisCount || 0)
    readonly property int assignedControlCount: configuredControlCount()
    readonly property string inputPreparationState: preparationState()
    readonly property bool canShowInputContent: selectedInputDeviceId.length > 0
        && (selectedControlCapabilityCount > 0 || hasVisibleButtons || povItems.length > 0)

    contentWidth: width
    contentHeight: buttonsContent.implicitHeight + deck.space24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    FlightDeckTheme { id: deck }

    Rectangle {
        parent: root
        anchors.fill: parent
        color: deck.primarySurface
        z: -1
    }

    function lower(value) { return String(value || "").toLowerCase() }
    function selectedDeviceId() {
        const devices = backend.selectedDevices || []
        for (let index = 0; index < devices.length; ++index) {
            const device = devices[index] || ({})
            if (Boolean(device.selected)) return String(device.id || "")
        }
        return ""
    }
    function controllerForSelectedDevice() {
        const selectedId = selectedDeviceId()
        const controllers = backend.controllers || []
        for (let index = 0; index < controllers.length; ++index) {
            const controller = controllers[index] || ({})
            if (String(controller.id || controller.directInputId || "") === selectedId)
                return controller
        }
        return ({})
    }
    function configuredControlCount() {
        let count = assignedButtonCount()
        for (let index = 0; index < povInputItems.length; ++index) {
            if (Number((povInputItems[index] || {}).target || 0) > 0) ++count
        }
        return count
    }
    function preparationState() {
        if (selectedInputDeviceId.length === 0) {
            if (knownControllerCount === 0)
                return backend.controllerSetupInProgress ? "looking" : "connect"
            return "choose"
        }
        if (!selectedInputDeviceConnected) return "offline"
        if (selectedControllerInfo && selectedControllerInfo.verified === false) return "setup"
        if (selectedControlCapabilityCount === 0 && !hasVisibleButtons && povItems.length === 0)
            return "no-controls"
        if (buttonItems.length === 0 && povItems.length === 0) return "setup"
        if (!backend.vjoyReady) return "output"
        if (assignedControlCount === 0) return "first-assignment"
        return "ready"
    }
    function preparationHeading() {
        switch (inputPreparationState) {
        case "looking": return "Looking for controllers"
        case "connect": return "Connect your controller"
        case "choose": return "Choose a controller"
        case "setup": return "Set up " + inputDeviceName
        case "offline": return inputDeviceName + " is not connected"
        case "no-controls": return "This controller has no buttons or hat switches"
        case "output": return "Connection needs a check"
        case "first-assignment": return "Assign your first button"
        }
        return "Buttons ready"
    }
    function preparationDetail() {
        switch (inputPreparationState) {
        case "looking": return "HOTAS BF6 is checking the current controller list."
        case "connect": return "Plug in a controller, then scan for it here."
        case "choose": return "Choose the controller whose buttons or hat switches you want to map."
        case "setup": return "Finish setup for this exact controller before mapping it."
        case "offline": return "Saved routes stay editable. Live learning and tests wait for the controller to reconnect."
        case "no-controls": return "This is not a fault. Try Axes if this controller has analog controls."
        case "output": return "Your saved routes are still visible. Check this setup before testing mapped output."
        case "first-assignment": return "Choose a button or hat direction, then select the output it should control."
        }
        return "Choose a control to view its input, output, and simple mapping controls."
    }
    function preparationPills() {
        const controllerPill = selectedInputDeviceId.length > 0
            ? { label: "CONTROLLER", value: selectedInputDeviceConnected ? "CONNECTED" : "OFFLINE",
                tone: selectedInputDeviceConnected ? "healthy" : "attention" }
            : { label: "CONTROLLER", value: knownControllerCount > 0 ? "CHOOSE" : "NOT FOUND",
                tone: knownControllerCount > 0 ? "informational" : "attention" }
        const outputPill = { label: "OUTPUT", value: backend.vjoyReady ? "READY" : "CHECK",
            tone: backend.vjoyReady ? "healthy" : "attention" }
        return [controllerPill, outputPill]
    }
    function preparationPrimaryText() {
        switch (inputPreparationState) {
        case "connect": return "SCAN FOR CONTROLLERS"
        case "choose": return "CHOOSE CONTROLLER"
        case "setup": return "SET UP THIS CONTROLLER"
        case "offline": return "CHANGE CONTROLLER"
        case "no-controls": return selectedAxisCapabilityCount > 0 ? "OPEN AXES" : "OPEN DEVICES & SETUP"
        case "output": return "CHECK SETUP"
        case "first-assignment": return backend.selectedDeviceButtonChoices().length > 1 ? "QUICK MAP" : ""
        }
        return ""
    }
    function preparationSecondaryText() {
        return inputPreparationState === "offline" ? "CHECK CONNECTION" : ""
    }
    function invokePreparationPrimary() {
        switch (inputPreparationState) {
        case "connect": backend.refreshControllers(); break
        case "choose": requestDevicePicker(); break
        case "setup": requestSetup("independent", { controllerRecordId: selectedInputDeviceId, returnPage: 1 }); break
        case "offline": requestDevicePicker(); break
        case "no-controls": navigateToPage(selectedAxisCapabilityCount > 0 ? 0 : 2); break
        case "output": navigateToPage(2); break
        case "first-assignment": requestQuickMap(); break
        }
    }
    function invokePreparationSecondary() {
        if (inputPreparationState === "offline") navigateToPage(2)
    }
    function profileChoiceIndex(profileId) {
        for (let index = 0; index < profileChoices.length; ++index) {
            if (String(profileChoices[index].id || "") === String(profileId || "")) return index
        }
        return 0
    }
    function profileIdAt(index) {
        return index >= 0 && index < profileChoices.length ? String(profileChoices[index].id || "") : ""
    }
    function behaviorChoiceIndex(behavior) {
        const index = behaviorChoices.indexOf(String(behavior || ""))
        return index >= 0 ? index : 0
    }
    function nativeChoiceIndex(key) {
        for (let index = 0; index < nativePovChoices.length; ++index) {
            if (String(nativePovChoices[index].key || "") === String(key || "")) return index
        }
        return 0
    }
    function nativeChoiceKey(index) {
        return index >= 0 && index < nativePovChoices.length ? String(nativePovChoices[index].key || "") : ""
    }
    function outputChoiceIndex(choices, target) {
        for (let index = 0; index < choices.length; ++index) {
            if (Number(choices[index].target || 0) === Number(target || 0)) return index
        }
        return 0
    }
    function liveButtonState(index) {
        // The selected-device snapshot is ordered by the stable physical
        // button index.  Direct indexing avoids an O(buttons²) QML scan when
        // a rapid input transition publishes a new bounded snapshot.
        const position = Number(index || 0) - 1
        if (position >= 0 && position < buttonTelemetry.length)
            return buttonTelemetry[position] || ({})
        return ({ pressed: false, liveAvailable: false, virtualPressed: false })
    }
    function liveSelectedInputButtonState(index) {
        const position = Number(index || 0) - 1
        if (position >= 0 && position < buttonInputTelemetry.length)
            return buttonInputTelemetry[position] || ({})
        return ({ pressed: false, liveAvailable: false })
    }
    function visibleButtonCount() {
        let count = 0
        for (let index = 0; index < buttonItems.length; ++index) {
            if (buttonVisible(buttonItems[index])) ++count
        }
        return count
    }
    function assignedButtonCount() {
        let count = 0
        for (let index = 0; index < buttonItems.length; ++index) {
            if (isAssigned(buttonItems[index])) ++count
        }
        return count
    }
    function isAssigned(button) {
        return Number(button && button.sourceCount || 0) > 0
    }
    function buttonVisible(button) {
        if (!button) return false
        const needle = lower(searchText).trim()
        if (needle.length === 0) return true
        const terms = [button.label, button.hardwareLabel, button.targetLabel,
            button.sourceSummary]
        for (let index = 0; index < terms.length; ++index) {
            if (lower(terms[index]).indexOf(needle) >= 0) return true
        }
        const automations = automationForButton(Number(button.index))
        for (let index = 0; index < automations.length; ++index) {
            if (lower(automations[index].name).indexOf(needle) >= 0) return true
        }
        return false
    }
    function automationForButton(buttonIndex) {
        const related = []
        for (let ruleIndex = 0; ruleIndex < automationItems.length; ++ruleIndex) {
            const rule = automationItems[ruleIndex] || ({})
            const conditions = rule.conditions || []
            for (let conditionIndex = 0; conditionIndex < conditions.length; ++conditionIndex) {
                const condition = conditions[conditionIndex] || ({})
                const type = Number(condition.type)
                if ([5, 6, 11, 12, 13, 14].indexOf(type) >= 0
                        && Number(condition.button) === Number(buttonIndex)) {
                    related.push(rule)
                    break
                }
            }
        }
        return related
    }
    function automationsForSources(button) {
        const related = []
        const seen = ({})
        const sources = button && button.sources ? button.sources : []
        for (let sourceIndex = 0; sourceIndex < sources.length; ++sourceIndex) {
            const source = sources[sourceIndex] || ({})
            const sourceRules = automationForButton(Number(source.physicalButton || 0))
            for (let ruleIndex = 0; ruleIndex < sourceRules.length; ++ruleIndex) {
                const rule = sourceRules[ruleIndex] || ({})
                const id = String(rule.id || rule.name || ruleIndex)
                if (!seen[id]) {
                    seen[id] = true
                    related.push(rule)
                }
            }
        }
        return related
    }
    function automationForPov(hat, direction) {
        const related = []
        for (let ruleIndex = 0; ruleIndex < automationItems.length; ++ruleIndex) {
            const rule = automationItems[ruleIndex] || ({})
            const conditions = rule.conditions || []
            for (let conditionIndex = 0; conditionIndex < conditions.length; ++conditionIndex) {
                const condition = conditions[conditionIndex] || ({})
                const type = Number(condition.type)
                if ([7, 8].indexOf(type) >= 0 && Number(condition.povHat) === Number(hat)
                        && Number(condition.povDirection) === Number(direction) + 1) {
                    related.push(rule)
                    break
                }
            }
        }
        return related
    }
    function assignmentForButton(button) {
        if (button.profileControlEnabled) {
            return button.profileControlTargetAvailable
                ? String(button.profileControlMode) + " profile · " + String(button.profileControlTargetName)
                : "Profile target needs attention"
        }
        if (String(button.mappingControlKey || "none") !== "none")
            return String(button.mappingControl)
        if (Number(button.target) > 0) return String(button.targetLabel)
        const related = automationForButton(Number(button.index))
        if (related.length > 0) return "Automation · " + String(related[0].name || "Rule")
        return "Unassigned"
    }
    function assignmentForPov(input) {
        if (input.profileControlEnabled) {
            return input.profileControlTargetAvailable
                ? String(input.profileControlMode) + " profile · " + String(input.profileControlTargetName)
                : "Profile target needs attention"
        }
        if (Number(input.target) > 0) return String(input.targetLabel)
        const related = automationForPov(Number(input.hat), Number(input.direction))
        if (related.length > 0) return "Automation · " + String(related[0].name || "Rule")
        return "Unassigned"
    }
    function directionsForHat(hat) {
        const directions = []
        for (let index = 0; index < povInputItems.length; ++index) {
            if (Number(povInputItems[index].hat) === Number(hat)) directions.push(povInputItems[index])
        }
        return directions
    }
    function directionForHat(hat, direction) {
        const directions = directionsForHat(hat)
        for (let index = 0; index < directions.length; ++index) {
            if (Number(directions[index].direction) === Number(direction)) return directions[index]
        }
        return ({})
    }
    function setExpandedButton(buttonIndex) {
        expandedButtonIndex = expandedButtonIndex === buttonIndex ? -1 : buttonIndex
        expandedHatIndex = -1
        expandedPovDirection = -1
    }
    function setExpandedPov(hat, direction) {
        expandedHatIndex = hat
        expandedPovDirection = direction
        expandedButtonIndex = -1
    }
    function requestButtonMapping(buttonIndex, target, explicitOverride) {
        if (backend.assignSelectedDeviceButtonToVirtualOutput(target, buttonIndex, explicitOverride)) return true
        if (!explicitOverride) {
            const collision = backend.buttonMappingCollision(buttonIndex, target)
            if (!collision.exists) return false
            conflictButtonIndex = buttonIndex
            conflictHatIndex = -1
            conflictDirectionIndex = -1
            conflictTarget = target
            conflictOwner = collision
            mappingConflict.open()
        }
        return false
    }
    function requestPovMapping(hat, direction, target, explicitOverride) {
        if (backend.setPovMapping(hat, direction, target, explicitOverride)) return true
        if (!explicitOverride) {
            conflictButtonIndex = -1
            conflictHatIndex = hat
            conflictDirectionIndex = direction
            conflictTarget = target
            conflictOwner = ({})
            mappingConflict.open()
        }
        return false
    }

    component SectionLabel: Text {
        color: deck.textMuted
        font.family: deck.telemetryFont
        font.pixelSize: deck.scale(9)
        font.bold: true
        Layout.fillWidth: true
    }

    component SummaryChip: Rectangle {
        property string label: ""
        property string tone: "informational"
        implicitHeight: 22
        implicitWidth: labelText.implicitWidth + deck.space16
        radius: deck.radiusPill
        color: Qt.rgba(deck.statusColor(tone).r, deck.statusColor(tone).g, deck.statusColor(tone).b,
                       deck.light ? 0.11 : 0.17)
        border.width: 1
        border.color: Qt.rgba(deck.statusColor(tone).r, deck.statusColor(tone).g, deck.statusColor(tone).b, 0.70)
        Text {
            id: labelText
            anchors.centerIn: parent
            text: parent.label
            color: deck.statusColor(parent.tone)
            font.family: deck.telemetryFont
            font.pixelSize: deck.scale(8)
            font.bold: true
        }
    }

    component DeckButton: Button {
        id: control
        property bool subdued: false
        implicitHeight: deck.compactControlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: control.text
            color: control.enabled ? (control.subdued ? deck.textSecondary : deck.primarySurface) : deck.disabled
            font.family: deck.telemetryFont
            font.pixelSize: deck.scale(9)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: !control.enabled ? deck.secondarySurface : control.down ? deck.accentMuted
                : control.hovered ? (control.subdued ? deck.selected : deck.focus)
                : (control.subdued ? deck.secondarySurface : deck.accent)
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
    }

    component DeckCombo: ComboBox {
        id: control
        implicitHeight: deck.controlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            leftPadding: deck.space12
            rightPadding: deck.space24
            text: control.displayText
            color: control.enabled ? deck.textPrimary : deck.disabled
            font.family: deck.telemetryFont
            font.pixelSize: deck.scale(10)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - deck.space12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: deck.textSecondary
            font.pixelSize: deck.scale(16)
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: control.pressed ? deck.selected : deck.primarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            objectName: control.objectName + "Choice_" + index
            width: ListView.view.width
            height: 34
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                leftPadding: deck.popupRowPadding
                rightPadding: deck.popupRowPadding
                text: control.textAt(index)
                color: deck.textPrimary
                font.family: deck.telemetryFont
                font.pixelSize: deck.scale(10)
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle { color: parent.highlighted ? deck.selected : deck.elevatedSurface }
        }
        popup: Popup {
            objectName: control.objectName + "Popup"
            y: control.height - 1
            width: control.width
            padding: deck.popupPadding
            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 224)
                model: control.delegateModel
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                radius: deck.radiusControl
                color: deck.elevatedSurface
                border.color: deck.border
            }
        }
    }

    component DeckField: TextField {
        id: control
        implicitHeight: deck.controlHeight
        selectByMouse: true
        onAccepted: focus = false
        color: deck.textPrimary
        font.family: deck.telemetryFont
        font.pixelSize: deck.scale(10)
        leftPadding: deck.space12
        rightPadding: deck.space12
        background: Rectangle {
            radius: deck.radiusControl
            color: deck.primarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
    }

    component FilterButton: Button {
        id: control
        property string filterValue: "all"
        implicitHeight: 28
        implicitWidth: filterText.implicitWidth + deck.space16
        focusPolicy: Qt.StrongFocus
        onClicked: root.filterMode = filterValue
        contentItem: Text {
            id: filterText
            text: control.text
            color: root.filterMode === control.filterValue ? deck.primarySurface : deck.textSecondary
            font.family: deck.telemetryFont
            font.pixelSize: deck.scale(8)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: deck.radiusPill
            color: root.filterMode === control.filterValue ? deck.accent : deck.secondarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
    }

    // The stable page surface is keyed by vJoy output button. A card never
    // becomes another device merely because the top-bar source selector
    // changes; it renders the actual persisted owner(s) of that output.
    component VirtualButtonCard: FlightDeckCard {
        id: card
        tokens: deck
        property var button: ({})
        readonly property int buttonIndex: Number(button.index)
        readonly property bool expanded: root.expandedButtonIndex === buttonIndex
        readonly property var live: root.liveButtonState(buttonIndex)
        readonly property var selectedInputLive: root.liveSelectedInputButtonState(selectedOwnedSourceButton)
        readonly property var automations: root.automationsForSources(button)
        // Read selectedDeviceId inside the binding as well as consulting the
        // backend.  This makes a top-bar device change refresh the exact
        // capability list even when both controllers expose the same number
        // of buttons.
        readonly property var sourceChoices: {
            const selectedId = root.selectedDeviceId()
            if (selectedId.length === 0) return []
            return backend.selectedDeviceButtonChoices()
        }
        readonly property bool pressed: Boolean(live.pressed)
        readonly property bool virtualPressed: Boolean(live.virtualPressed)
        readonly property int selectedOwnedSourceButton: {
            const selectedId = root.selectedDeviceId()
            const sources = button.sources || []
            for (let index = 0; index < sources.length; ++index) {
                if (String(sources[index].controllerRecordId || "") === selectedId)
                    return Number(sources[index].physicalButton || 0)
            }
            return 0
        }
        // Retain the stable card identity used by the original grid.  The
        // semantic change is that a card now represents one virtual output,
        // while its source list truthfully identifies whichever device owns
        // each route.
        objectName: "flightDeckButtonCard_" + buttonIndex
        width: root.width >= 1180 ? (virtualButtonFlow.width - deck.space12) / 2 : virtualButtonFlow.width
        visible: root.buttonVisible(button)
        implicitHeight: visible ? content.implicitHeight + contentPadding * 2 : 0
        color: pressed ? deck.selected : deck.elevatedSurface
        border.color: pressed ? deck.accent : deck.border

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: parent.contentPadding
            spacing: deck.space12

            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    Text {
                        text: String(button.label || "Virtual Button " + buttonIndex)
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: deck.scale(16)
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: String(button.sourceSummary || "No physical input assigned")
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                SummaryChip {
                    label: pressed ? "PRESSED" : "RELEASED"
                    tone: pressed ? "healthy" : "informational"
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Text { text: "→"; color: deck.accent; font.pixelSize: deck.scale(20); font.bold: true }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: root.guidedPresentation
                            ? String(button.targetLabel || "Button " + buttonIndex).replace(/^vJoy\s+/i, "")
                            : String(button.targetLabel || "vJoy Button " + buttonIndex)
                        color: deck.textPrimary
                        font.pixelSize: deck.scale(13)
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Text {
                        text: Number(button.sourceCount || 0) === 0 ? "UNASSIGNED"
                            : Number(button.sourceCount || 0) === 1 ? "ONE PHYSICAL SOURCE"
                            : "MIXED · " + Number(button.sourceCount || 0) + " PHYSICAL SOURCES"
                        color: Number(button.sourceCount || 0) > 1 ? deck.accent : deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: !root.guidedPresentation && card.automations.length > 0
                        text: "Automation · " + card.automations.map(function(rule) { return rule.name || "Rule" }).join(" · ")
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                DeckButton { text: card.expanded ? "CLOSE" : "CONFIGURE"; subdued: true; onClicked: root.setExpandedButton(card.buttonIndex) }
            }

            ColumnLayout {
                visible: card.expanded
                Layout.fillWidth: true
                spacing: deck.space12
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                SectionLabel { text: "SOURCE DEVICE" }
                Text {
                    Layout.fillWidth: true
                    text: root.selectedDeviceId().length > 0
                        ? root.inputDeviceName
                        : "Select a controller above"
                    color: root.selectedDeviceId().length > 0 ? deck.textPrimary : deck.attention
                    font.family: deck.telemetryFont
                    font.pixelSize: deck.scale(10)
                    elide: Text.ElideRight
                }
                SectionLabel { text: "INPUT BUTTON" }
                Text {
                    Layout.fillWidth: true
                    text: root.selectedDeviceId().length > 0
                        ? "Selected Device · " + root.inputDeviceName + ". Choose the physical button that should control " + String(button.targetLabel || "this output") + "."
                        : "Choose a specific controller from SELECTED DEVICE in the top bar before assigning a physical source."
                    color: root.selectedDeviceId().length > 0 ? deck.textSecondary : deck.attention
                    font.pixelSize: deck.scale(10)
                    wrapMode: Text.WordWrap
                }
                DeckCombo {
                    id: sourceSelector
                    objectName: "flightDeckVirtualButtonSource_" + card.buttonIndex
                    Layout.fillWidth: true
                    enabled: root.selectedDeviceId().length > 0 && card.sourceChoices.length > 0
                    model: card.sourceChoices
                    textRole: "label"
                    currentIndex: {
                        for (let index = 0; index < card.sourceChoices.length; ++index) {
                            if (Number(card.sourceChoices[index].button || 0) === card.selectedOwnedSourceButton)
                                return index
                        }
                        return card.sourceChoices.length > 0 ? 0 : -1
                    }
                    onActivated: function(index) {
                        const source = Number(card.sourceChoices[index].button || 0)
                        if (source > 0) {
                            root.requestButtonMapping(source, card.buttonIndex, false)
                        } else if (card.selectedOwnedSourceButton > 0) {
                            root.requestButtonMapping(card.selectedOwnedSourceButton, 0, false)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    DeckButton {
                        text: "CLEAR SELECTED SOURCE"
                        subdued: true
                        enabled: card.selectedOwnedSourceButton > 0
                        onClicked: root.requestButtonMapping(card.selectedOwnedSourceButton, 0, false)
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: card.virtualPressed ? (root.guidedPresentation ? "Output is pressed" : "vJoy output is pressed")
                            : (Boolean(card.selectedInputLive.pressed) ? "Selected input is pressed" : "")
                        color: deck.healthy
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                    }
                }
                Rectangle {
                    objectName: "flightDeckButtonsBehaviorDisclosure_" + card.buttonIndex
                    visible: !root.guidedPresentation
                    Layout.fillWidth: true
                    implicitHeight: virtualBehaviorDisclosure.implicitHeight + deck.space16
                    radius: deck.radiusControl
                    color: deck.secondarySurface
                    border.color: deck.border
                    ColumnLayout {
                        id: virtualBehaviorDisclosure
                        anchors.fill: parent
                        anchors.margins: deck.space8
                        spacing: deck.space4
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                SectionLabel { text: "OPTIONAL RELATIONSHIPS" }
                                Text {
                                    text: Number(button.sourceCount || 0) + " physical source"
                                        + (Number(button.sourceCount || 0) === 1 ? "" : "s") + " · "
                                        + (card.automations.length ? String(card.automations.length) + " linked automation"
                                            + (card.automations.length === 1 ? "" : "s") : "no linked automation")
                                    color: deck.textSecondary
                                    font.pixelSize: deck.scale(9)
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                            DeckButton {
                                objectName: "flightDeckButtonsBehaviorToggle_" + card.buttonIndex
                                text: root.behaviorExpanded ? "HIDE OPTIONS" : "SHOW OPTIONS"
                                subdued: true
                                onClicked: themeManager.setGuidanceSectionExpanded("buttons-behavior", !root.behaviorExpanded)
                            }
                        }
                        Text {
                            visible: !root.behaviorExpanded
                            text: "Keep the source mapping above for normal setup. Open options to inspect each linked automation without changing its route."
                            color: deck.textMuted
                            font.pixelSize: deck.scale(9)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        DeckButton {
                            visible: themeManager.guidanceSectionHasExplicitPreference("buttons-behavior")
                            text: "FOLLOW GUIDANCE LEVEL"
                            subdued: true
                            onClicked: themeManager.followGuidanceLevelForSection("buttons-behavior")
                        }
                    }
                }
                ColumnLayout {
                    objectName: "flightDeckButtonsBehaviorControls_" + card.buttonIndex
                    visible: !root.guidedPresentation && root.behaviorExpanded
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Text {
                        text: card.selectedOwnedSourceButton > 0
                            ? "Selected input: Button " + card.selectedOwnedSourceButton + " on " + root.inputDeviceName
                            : "No source from the selected controller is assigned to this output."
                        color: deck.textMuted
                        font.pixelSize: deck.scale(9)
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: card.automations
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: "Automation · " + String(modelData.name || "Rule")
                                color: deck.textSecondary
                                font.pixelSize: deck.scale(9)
                                elide: Text.ElideRight
                            }
                            DeckButton {
                                text: "OPEN"
                                subdued: true
                                onClicked: root.navigateToAutomation(String(modelData.id || ""))
                            }
                        }
                    }
                }
            }
        }
    }

    component ButtonCard: FlightDeckCard {
        id: card
        tokens: deck
        property var button: ({})
        readonly property int buttonIndex: Number(button.index)
        readonly property bool expanded: root.expandedButtonIndex === buttonIndex
        readonly property var live: root.liveButtonState(buttonIndex)
        readonly property bool pressed: Boolean(live.pressed)
        readonly property bool virtualPressed: Boolean(live.virtualPressed)
        readonly property var automations: root.automationForButton(buttonIndex)
        // Shared-output ownership belongs to the stable configuration row.
        // It must not call back into C++ from every card on a live tick.
        readonly property var sharedOutput: button.sharedOutput || ({ mixed: false })
        objectName: "flightDeckButtonCard_" + buttonIndex
        width: root.width >= 1180 ? (virtualButtonFlow.width - deck.space12) / 2 : virtualButtonFlow.width
        visible: root.isAssigned(button) && root.buttonVisible(button)
        implicitHeight: visible ? content.implicitHeight + contentPadding * 2 : 0
        color: pressed ? deck.selected : deck.elevatedSurface
        border.color: pressed ? deck.accent : deck.border

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: parent.contentPadding
            spacing: deck.space12
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    Text {
                        text: String(button.label || button.hardwareLabel || "Button")
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: deck.scale(16)
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Text {
                        text: String(button.sourceDevice || root.inputDeviceName) + " · "
                            + String(button.hardwareLabel || "Button " + buttonIndex)
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }
                SummaryChip {
                    label: pressed ? "PRESSED" : "RELEASED"
                    tone: pressed ? "healthy" : "informational"
                    // Keep pressed-state feedback out of the card geometry:
                    // a label change may recolor the chip, never reflow Flow.
                    Layout.preferredWidth: 78
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Text {
                    text: "→"
                    color: deck.accent
                    font.pixelSize: deck.scale(20)
                    font.bold: true
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: root.assignmentForButton(button)
                        color: button.profileControlEnabled && !button.profileControlTargetAvailable
                            ? deck.attention : deck.textPrimary
                        font.pixelSize: deck.scale(13)
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: !root.guidedPresentation && Boolean(button.profileControlEnabled) && Number(button.target) > 0
                        text: "Saved game route · " + String(button.targetLabel)
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: !root.guidedPresentation && automations.length > 0
                        text: "Automation · " + automations.map(function(rule) { return rule.name || "Rule" }).join(" · ")
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                DeckButton {
                    text: card.expanded ? "CLOSE" : "CONFIGURE"
                    subdued: true
                    Layout.alignment: Qt.AlignTop
                    onClicked: root.setExpandedButton(card.buttonIndex)
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: deck.space8
                SummaryChip {
                    visible: !root.guidedPresentation && Boolean(button.profileControlEnabled)
                    label: String(button.profileControlMode || "Profile").toUpperCase()
                    tone: button.profileControlTargetAvailable ? "healthy" : "attention"
                }
                SummaryChip {
                    visible: !root.guidedPresentation && String(button.mappingControlKey || "none") !== "none"
                    label: "MAPPING CONTROL"
                    tone: "informational"
                }
                SummaryChip {
                    visible: Number(button.target) > 0 && !backend.vjoyReady
                    label: "OUTPUT OFFLINE"
                    tone: "attention"
                }
                SummaryChip {
                    visible: Number(button.target) > 0 && virtualPressed
                    label: "VIRTUAL PRESSED"
                    tone: "healthy"
                }
                SummaryChip {
                    visible: Boolean(card.sharedOutput.mixed)
                    label: "MIXED · " + Number(card.sharedOutput.participants ? card.sharedOutput.participants.length : 0) + " SOURCES"
                    tone: "informational"
                }
            }
            ColumnLayout {
                visible: card.expanded
                Layout.fillWidth: true
                spacing: deck.space12
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                SectionLabel { text: "ASSIGNMENT" }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 620 ? 2 : 1
                    columnSpacing: deck.space12
                    rowSpacing: deck.space8
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "CONTROL LABEL" }
                        DeckField {
                            Layout.fillWidth: true
                            text: String(button.customName || "")
                            placeholderText: String(button.hardwareLabel || "Button")
                            onEditingFinished: backend.setButtonCustomName(card.buttonIndex, text)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "GAME OUTPUT" }
                        DeckCombo {
                            id: buttonMappingSelector
                            objectName: "flightDeckButtonMappingSelector_" + card.buttonIndex
                            Layout.fillWidth: true
                            model: backend.buttonOutputChoiceDetailsForSource(card.buttonIndex)
                            textRole: "label"
                            currentIndex: root.outputChoiceIndex(model, button.target)
                            onActivated: function(index) {
                                const target = Number(model[index].target || 0)
                                if (!root.requestButtonMapping(card.buttonIndex, target, false))
                                    currentIndex = root.outputChoiceIndex(model, button.target)
                            }
                        }
                    }
                }
                Rectangle {
                    visible: Boolean(card.sharedOutput.mixed)
                    Layout.fillWidth: true
                    implicitHeight: mixedDetail.implicitHeight + deck.space16
                    radius: deck.radiusControl
                    color: deck.secondarySurface
                    border.color: deck.border
                    ColumnLayout {
                        id: mixedDetail
                        anchors.fill: parent
                        anchors.margins: deck.space8
                        spacing: 4
                        SectionLabel { text: "MIXED OUTPUT · " + String(card.sharedOutput.target || "") }
                        Text {
                            Layout.fillWidth: true
                            text: "With: " + String(card.sharedOutput.with || "another source")
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(9)
                            elide: Text.ElideRight
                        }
                        DeckCombo {
                            Layout.fillWidth: true
                            model: [
                                { key: "sum-clamped", label: "Sum / Clamp" },
                                { key: "highest-magnitude", label: "Larger Value" },
                                { key: "average", label: "Average" }
                            ]
                            textRole: "label"
                            currentIndex: String(card.sharedOutput.modeKey || "") === "sum-clamped" ? 0
                                : String(card.sharedOutput.modeKey || "") === "average" ? 2 : 1
                            onActivated: function(index) {
                                backend.setSharedButtonMixerMode(Number(button.target || 0), model[index].key)
                            }
                        }
                    }
                }
                Text {
                        text: root.guidedPresentation
                            ? "Physical control to game button. Changes apply to the current profile."
                            : "Physical control → existing vJoy button route. Changes apply through the current profile command path."
                    color: deck.textMuted
                    font.pixelSize: deck.scale(9)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    DeckButton {
                        text: "CLEAR GAME ROUTE"
                        subdued: true
                        enabled: Number(button.target) > 0
                        onClicked: root.requestButtonMapping(card.buttonIndex, 0, false)
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Number(button.target) > 0 && virtualPressed
                            ? (root.guidedPresentation ? "Output is pressed" : "vJoy output is pressed") : ""
                        color: deck.healthy
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                    }
                }
                Rectangle {
                    objectName: "flightDeckButtonsBehaviorDisclosure_" + card.buttonIndex
                    visible: !root.guidedPresentation
                    Layout.fillWidth: true
                    implicitHeight: behaviorDisclosure.implicitHeight + deck.space16
                    radius: deck.radiusControl
                    color: deck.secondarySurface
                    border.color: deck.border
                    ColumnLayout {
                        id: behaviorDisclosure
                        anchors.fill: parent
                        anchors.margins: deck.space8
                        spacing: deck.space4
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                SectionLabel { text: "OPTIONAL BEHAVIOR & RELATIONSHIPS" }
                                Text {
                                    text: (Boolean(button.profileControlEnabled) ? "Profile control · " + String(button.profileControlTargetName || "needs attention") : "No profile control")
                                        + " · " + (String(button.mappingControlKey || "none") !== "none" ? "mapping control set" : "no mapping control")
                                        + " · " + (automations.length ? String(automations.length) + " automation link" + (automations.length === 1 ? "" : "s") : "no automation links")
                                    color: deck.textSecondary
                                    font.pixelSize: deck.scale(9)
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                            DeckButton {
                                text: root.behaviorExpanded ? "HIDE OPTIONS" : "SHOW OPTIONS"
                                subdued: true
                                onClicked: themeManager.setGuidanceSectionExpanded("buttons-behavior", !root.behaviorExpanded)
                            }
                        }
                        Text {
                            visible: !root.behaviorExpanded
                            text: "The game route above is the normal setup. Open options only to make this input switch profiles, control mapping, or inspect linked automation."
                            color: deck.textMuted
                            font.pixelSize: deck.scale(9)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        DeckButton {
                            visible: themeManager.guidanceSectionHasExplicitPreference("buttons-behavior")
                            text: "FOLLOW GUIDANCE LEVEL"
                            subdued: true
                            onClicked: themeManager.followGuidanceLevelForSection("buttons-behavior")
                        }
                    }
                }
                ColumnLayout {
                    objectName: "flightDeckButtonsBehaviorControls_" + card.buttonIndex
                    visible: !root.guidedPresentation && root.behaviorExpanded
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                    SectionLabel { text: "PROFILE CONTROL" }
                    Text {
                        text: "A profile control consumes this physical input while retaining its saved game route for restoration when cleared."
                        color: deck.textMuted
                        font.pixelSize: deck.scale(9)
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 620 ? 2 : 1
                    columnSpacing: deck.space12
                    rowSpacing: deck.space8
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "WHEN PRESSED" }
                        DeckCombo {
                            id: profileSelector
                            objectName: "flightDeckButtonProfileSelector_" + card.buttonIndex
                            Layout.fillWidth: true
                            model: root.profileChoices
                            textRole: "label"
                            valueRole: "id"
                            currentIndex: root.profileChoiceIndex(button.profileControlTargetId)
                            onActivated: backend.setProfileTrigger(card.buttonIndex,
                                root.profileIdAt(currentIndex), behaviorSelector.currentText)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "BEHAVIOR" }
                        DeckCombo {
                            id: behaviorSelector
                            objectName: "flightDeckButtonBehaviorSelector_" + card.buttonIndex
                            Layout.fillWidth: true
                            model: root.behaviorChoices
                            currentIndex: root.behaviorChoiceIndex(button.profileControlMode)
                            onActivated: {
                                const profileId = root.profileIdAt(profileSelector.currentIndex)
                                if (profileId.length > 0)
                                    backend.setProfileTrigger(card.buttonIndex, profileId, currentText)
                            }
                        }
                    }
                }
                    RowLayout {
                    visible: Boolean(button.profileControlEnabled)
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: button.profileControlTargetAvailable
                            ? "References " + String(button.profileControlTargetName)
                            : "The referenced profile is unavailable. Choose another profile or clear this control."
                        color: button.profileControlTargetAvailable ? deck.textSecondary : deck.attention
                        font.pixelSize: deck.scale(10)
                        wrapMode: Text.WordWrap
                    }
                    DeckButton {
                        text: "OPEN PROFILE"
                        subdued: true
                        enabled: Boolean(button.profileControlTargetAvailable)
                        onClicked: root.navigateToProfile(String(button.profileControlTargetId || ""))
                    }
                }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                    SectionLabel { text: "MAPPING CONTROL" }
                    Text {
                    text: "This existing global control is independent of the game route above."
                    color: deck.textMuted
                    font.pixelSize: deck.scale(9)
                    Layout.fillWidth: true
                }
                    DeckCombo {
                    objectName: "flightDeckButtonMappingControlSelector_" + card.buttonIndex
                    Layout.fillWidth: true
                    model: root.mappingControlChoices
                    currentIndex: Math.max(0, root.mappingControlChoices.indexOf(String(button.mappingControl || "None")))
                    onActivated: backend.setMappingControl(card.buttonIndex, currentText)
                }
                    ColumnLayout {
                    visible: automations.length > 0
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                    SectionLabel { text: "AUTOMATION RELATIONSHIPS" }
                    Repeater {
                        model: card.automations
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData.name || "Automation") + " · " + String(modelData.conditionSummary || "Uses this button")
                                color: deck.textSecondary
                                font.pixelSize: deck.scale(10)
                                elide: Text.ElideRight
                            }
                            DeckButton {
                                text: "OPEN AUTOMATION"
                                subdued: true
                                onClicked: root.navigateToAutomation(String(modelData.id || ""))
                            }
                        }
                    }
                    }
                }
            }
        }
    }

    component UnassignedButton: Button {
        id: control
        property var button: ({})
        objectName: "flightDeckUnassignedButton_" + Number(button.index)
        visible: !root.isAssigned(button) && root.buttonVisible(button)
        implicitWidth: Math.max(112, label.implicitWidth + deck.space24)
        implicitHeight: deck.controlHeight
        focusPolicy: Qt.StrongFocus
        onClicked: root.setExpandedButton(Number(button.index))
        contentItem: Text {
            id: label
            text: String(button.label || button.hardwareLabel || "Button") + "  +"
            color: control.hovered ? deck.accent : deck.textSecondary
            font.family: deck.telemetryFont
            font.pixelSize: deck.scale(10)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: control.down ? deck.selected : control.hovered ? deck.primarySurface : deck.secondarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
    }

    component UnassignedEditor: FlightDeckCard {
        id: editor
        tokens: deck
        property var button: ({})
        readonly property int buttonIndex: Number(button.index)
        visible: !root.isAssigned(button) && root.expandedButtonIndex === buttonIndex && root.buttonVisible(button)
        Layout.fillWidth: true
        implicitHeight: visible ? editorContent.implicitHeight + contentPadding * 2 : 0
        color: deck.elevatedSurface
        ColumnLayout {
            id: editorContent
            anchors.fill: parent
            anchors.margins: parent.contentPadding
            spacing: deck.space12
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Text {
                        text: String(button.label || button.hardwareLabel || "Button")
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: deck.scale(16)
                        font.bold: true
                    }
                    Text {
                        text: String(button.sourceDevice || root.inputDeviceName) + " · "
                            + String(button.hardwareLabel || "Button " + buttonIndex) + " · Unassigned"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                    }
                }
                DeckButton { text: "CLOSE"; subdued: true; onClicked: root.expandedButtonIndex = -1 }
            }
            SectionLabel { text: "ASSIGN TO GAME OUTPUT" }
            DeckCombo {
                objectName: "flightDeckButtonMappingSelector_" + editor.buttonIndex
                Layout.fillWidth: true
                model: backend.buttonOutputChoiceDetailsForSource(editor.buttonIndex)
                textRole: "label"
                currentIndex: 0
                onActivated: function(index) {
                    if (!root.requestButtonMapping(editor.buttonIndex,
                                                   Number(model[index].target || 0), false)) currentIndex = 0
                }
            }
            Text {
                text: "Unassigned is intentional. Choose an existing vJoy button only when this control should have a game output."
                color: deck.textMuted
                font.pixelSize: deck.scale(9)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    component HatDirectionTile: Button {
        id: tile
        property var directionInput: ({})
        readonly property int directionIndex: Number(directionInput.direction)
        readonly property bool selected: root.expandedHatIndex === Number(directionInput.hat)
            && root.expandedPovDirection === directionIndex
        objectName: "flightDeckHatDirection_" + Number(directionInput.hat) + "_" + directionIndex
        implicitHeight: 44
        Layout.fillWidth: true
        focusPolicy: Qt.StrongFocus
        onClicked: root.setExpandedPov(Number(directionInput.hat), directionIndex)
        contentItem: Column {
            anchors.centerIn: parent
            width: parent.width - deck.space8
            spacing: 1
            Text {
                width: parent.width
                text: String(directionInput.label || "Direction")
                color: tile.selected || directionInput.active ? deck.textPrimary : deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: deck.scale(9)
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.assignmentForPov(directionInput)
                color: directionInput.active ? deck.healthy : deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: deck.scale(8)
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: directionInput.active ? deck.selected : tile.selected ? deck.accentMuted
                : tile.hovered ? deck.primarySurface : deck.secondarySurface
            border.width: tile.activeFocus ? 2 : 1
            border.color: tile.activeFocus ? deck.focus : (directionInput.active ? deck.accent : deck.border)
        }
    }

    component HatCard: FlightDeckCard {
        id: card
        tokens: deck
        property var hat: ({})
        readonly property int hatIndex: Number(hat.index)
        readonly property var directions: root.directionsForHat(hatIndex)
        objectName: "flightDeckHatCard_" + hatIndex
        Layout.fillWidth: true
        implicitHeight: content.implicitHeight + contentPadding * 2
        color: hat.centered ? deck.elevatedSurface : deck.selected
        border.color: hat.centered ? deck.border : deck.accent
        function directionAt(index) { return root.directionForHat(hatIndex, index) }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: parent.contentPadding
            spacing: deck.space12
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "HAT " + card.hatIndex
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: deck.scale(16)
                        font.bold: true
                    }
                    Text {
                        text: root.inputDeviceName + " · POV " + card.hatIndex
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                    }
                }
                SummaryChip {
                    label: hat.centered ? "CENTERED" : String(hat.direction || "ACTIVE").toUpperCase()
                    tone: hat.centered ? "informational" : "healthy"
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: deck.space8
                rowSpacing: deck.space8
                HatDirectionTile { directionInput: card.directionAt(7) }
                HatDirectionTile { directionInput: card.directionAt(0) }
                HatDirectionTile { directionInput: card.directionAt(1) }
                HatDirectionTile { directionInput: card.directionAt(6) }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    radius: deck.radiusControl
                    color: deck.primarySurface
                    border.color: deck.border
                    Text {
                        anchors.centerIn: parent
                        text: hat.centered ? "●" : "◉"
                        color: hat.centered ? deck.textMuted : deck.accent
                        font.pixelSize: deck.scale(18)
                    }
                }
                HatDirectionTile { directionInput: card.directionAt(2) }
                HatDirectionTile { directionInput: card.directionAt(5) }
                HatDirectionTile { directionInput: card.directionAt(4) }
                HatDirectionTile { directionInput: card.directionAt(3) }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        visible: !root.guidedPresentation
                        text: "NATIVE vJOY POV"
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        font.bold: true
                    }
                    Text {
                    visible: !root.guidedPresentation
                    text: String(hat.nativeTargetLabel || "Off") + " · " + String(hat.nativeStatus || "OFF")
                        color: hat.nativeAvailable || !hat.nativeEnabled ? deck.textMuted : deck.attention
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                DeckButton {
                    visible: !root.guidedPresentation
                    text: hat.nativeEnabled ? "DISABLE POV" : "ENABLE POV"
                    subdued: true
                    enabled: hat.nativeEnabled || root.nativePovChoices.length > 0
                    onClicked: backend.setNativePovOutput(card.hatIndex, !hat.nativeEnabled,
                        root.nativeChoiceKey(nativePovSelector.currentIndex))
                }
            }
            DeckCombo {
                id: nativePovSelector
                objectName: "flightDeckNativePovSelector_" + card.hatIndex
                visible: !root.guidedPresentation
                Layout.fillWidth: true
                model: root.nativePovChoices
                textRole: "label"
                valueRole: "key"
                enabled: root.nativePovChoices.length > 0
                currentIndex: root.nativeChoiceIndex(hat.nativeTargetKey)
                onActivated: backend.setNativePovOutput(card.hatIndex, hat.nativeEnabled,
                    root.nativeChoiceKey(currentIndex))
            }
            // A whole-hat destination is an everyday mapping decision.  Keep
            // the native-descriptor name and its extended diagnostics in Full,
            // but expose the same canonical command plainly in Guided.
            ColumnLayout {
                visible: root.guidedPresentation
                Layout.fillWidth: true
                spacing: deck.space8
                Text {
                    text: "WHOLE-HAT DESTINATION"
                    color: deck.textSecondary
                    font.family: deck.telemetryFont
                    font.pixelSize: deck.scale(9)
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: hat.nativeEnabled
                        ? "This whole hat sends " + String(hat.nativeTargetLabel || "the selected POV") + "."
                        : "Choose a game POV for the whole hat, or map directions separately above."
                    color: deck.textMuted
                    font.pixelSize: deck.scale(9)
                    wrapMode: Text.WordWrap
                }
                DeckCombo {
                    id: guidedPovSelector
                    objectName: "flightDeckGuidedPovSelector_" + card.hatIndex
                    Layout.fillWidth: true
                    model: root.nativePovChoices
                    textRole: "label"
                    valueRole: "key"
                    enabled: root.nativePovChoices.length > 0
                    currentIndex: root.nativeChoiceIndex(hat.nativeTargetKey)
                    onActivated: backend.setNativePovOutput(card.hatIndex, hat.nativeEnabled,
                        root.nativeChoiceKey(currentIndex))
                }
                DeckButton {
                    text: hat.nativeEnabled ? "TURN OFF WHOLE-HAT POV" : "USE WHOLE HAT AS POV"
                    subdued: !hat.nativeEnabled
                    enabled: hat.nativeEnabled || root.nativePovChoices.length > 0
                    onClicked: backend.setNativePovOutput(card.hatIndex, !hat.nativeEnabled,
                        root.nativeChoiceKey(guidedPovSelector.currentIndex))
                }
                Text {
                    visible: root.nativePovChoices.length === 0
                    Layout.fillWidth: true
                    text: "No game POV target is available for this output. Direction mappings remain available."
                    color: deck.attention
                    font.pixelSize: deck.scale(9)
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                visible: !root.guidedPresentation && root.nativePovChoices.length === 0
                text: "The selected vJoy device exposes no native POV target. Direction routes above remain available."
                color: deck.attention
                font.pixelSize: deck.scale(9)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ColumnLayout {
                id: povDetail
                visible: root.expandedHatIndex === card.hatIndex && root.expandedPovDirection >= 0
                Layout.fillWidth: true
                spacing: deck.space12
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                readonly property var selectedDirection: card.directionAt(root.expandedPovDirection)
                SectionLabel {
                    text: "HAT " + card.hatIndex + " · " + String(povDetail.selectedDirection.label || "DIRECTION").toUpperCase()
                }
                Text {
                    text: root.guidedPresentation
                        ? "Physical POV direction to the selected game button."
                        : "Physical POV direction → existing vJoy button route."
                    color: deck.textMuted
                    font.pixelSize: deck.scale(9)
                }
                DeckCombo {
                    id: povMappingSelector
                    objectName: "flightDeckPovMappingSelector_" + card.hatIndex + "_" + root.expandedPovDirection
                    Layout.fillWidth: true
                    model: root.plainOutputChoices
                    currentIndex: Math.max(0, Number(povDetail.selectedDirection.target || 0))
                    onActivated: {
                        if (!root.requestPovMapping(card.hatIndex, root.expandedPovDirection, currentIndex, false))
                            currentIndex = Math.max(0, Number(povDetail.selectedDirection.target || 0))
                    }
                }
                RowLayout {
                    visible: Number(card.directionAt(root.expandedPovDirection).target || 0) > 0
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: root.guidedPresentation
                            ? "Learn a physical hat direction for the selected game button."
                            : "Learn a physical hat direction for the current vJoy button route."
                        color: deck.textMuted
                        font.pixelSize: deck.scale(9)
                        wrapMode: Text.WordWrap
                    }
                    DeckButton {
                        objectName: "flightDeckPovLearn_" + card.hatIndex + "_" + root.expandedPovDirection
                        text: "LEARN INPUT"
                        subdued: true
                        enabled: backend.physicalConnected
                        onClicked: root.requestPovLearning(Number(card.directionAt(root.expandedPovDirection).target || 0))
                    }
                }
                Rectangle {
                    objectName: "flightDeckPovBehaviorDisclosure_" + card.hatIndex + "_" + root.expandedPovDirection
                    visible: !root.guidedPresentation
                    Layout.fillWidth: true
                    implicitHeight: povBehaviorDisclosure.implicitHeight + deck.space16
                    radius: deck.radiusControl
                    color: deck.secondarySurface
                    border.color: deck.border
                    ColumnLayout {
                        id: povBehaviorDisclosure
                        anchors.fill: parent
                        anchors.margins: deck.space8
                        spacing: deck.space4
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "OPTIONAL PROFILE & AUTOMATION"; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; Layout.fillWidth: true }
                            DeckButton { text: root.behaviorExpanded ? "HIDE OPTIONS" : "SHOW OPTIONS"; subdued: true; onClicked: themeManager.setGuidanceSectionExpanded("buttons-behavior", !root.behaviorExpanded) }
                        }
                        Text { visible: !root.behaviorExpanded; text: "The POV route above remains unchanged. Open options to make this direction control a profile or inspect linked automation."; color: deck.textMuted; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }
                ColumnLayout {
                    visible: !root.guidedPresentation && root.behaviorExpanded
                    Layout.fillWidth: true
                    spacing: deck.space12
                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 620 ? 2 : 1
                    columnSpacing: deck.space12
                    rowSpacing: deck.space8
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "PROFILE CONTROL" }
                        DeckCombo {
                            id: povProfileSelector
                            Layout.fillWidth: true
                            model: root.profileChoices
                            textRole: "label"
                            valueRole: "id"
                            currentIndex: root.profileChoiceIndex(povDetail.selectedDirection.profileControlTargetId)
                            onActivated: backend.setPovProfileTrigger(card.hatIndex, root.expandedPovDirection,
                                root.profileIdAt(currentIndex), povBehaviorSelector.currentText)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "BEHAVIOR" }
                        DeckCombo {
                            id: povBehaviorSelector
                            Layout.fillWidth: true
                            model: root.behaviorChoices
                            currentIndex: root.behaviorChoiceIndex(povDetail.selectedDirection.profileControlMode)
                            onActivated: {
                                const profileId = root.profileIdAt(povProfileSelector.currentIndex)
                                if (profileId.length > 0) backend.setPovProfileTrigger(card.hatIndex,
                                    root.expandedPovDirection, profileId, currentText)
                            }
                        }
                    }
                }
                RowLayout {
                    visible: Boolean(povDetail.selectedDirection.profileControlEnabled)
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: povDetail.selectedDirection.profileControlTargetAvailable
                            ? "References " + String(povDetail.selectedDirection.profileControlTargetName)
                            : "The referenced profile is unavailable."
                        color: povDetail.selectedDirection.profileControlTargetAvailable ? deck.textSecondary : deck.attention
                        font.pixelSize: deck.scale(10)
                    }
                    DeckButton {
                        text: "OPEN PROFILE"
                        subdued: true
                        enabled: Boolean(povDetail.selectedDirection.profileControlTargetAvailable)
                        onClicked: root.navigateToProfile(String(povDetail.selectedDirection.profileControlTargetId || ""))
                    }
                }
                Repeater {
                    model: root.automationForPov(card.hatIndex, root.expandedPovDirection)
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Automation · " + String(modelData.name || "Rule")
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(10)
                            elide: Text.ElideRight
                        }
                        DeckButton {
                            text: "OPEN AUTOMATION"
                            subdued: true
                            onClicked: root.navigateToAutomation(String(modelData.id || ""))
                        }
                    }
                }
                }
            }
        }
    }

    ColumnLayout {
        id: buttonsContent
        x: deck.space4
        width: root.width - deck.space8
        spacing: deck.space16

        FlightDeckInputStatusCard {
            id: buttonsInputStatus
            objectName: "flightDeckButtonsInputStatus"
            tokens: deck
            Layout.fillWidth: true
            heading: root.preparationHeading()
            detail: root.preparationDetail()
            statusPills: root.preparationPills()
            primaryText: root.preparationPrimaryText()
            secondaryText: root.preparationSecondaryText()
            // Retain the established quick-map control identity while making
            // its presentation conditional on a usable next action.
            primaryObjectName: "flightDeckButtonsQuickMap"
            secondaryObjectName: "flightDeckButtonsInputSecondary"
            onPrimaryAction: root.invokePreparationPrimary()
            onSecondaryAction: root.invokePreparationSecondary()
        }

        RowLayout {
            visible: root.canShowInputContent
            Layout.fillWidth: true
            spacing: deck.space8
            Text {
                text: "Each card is one virtual output. Its source summary shows the controller and physical button that currently own it."
                color: deck.textMuted
                font.pixelSize: deck.scale(9)
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: true }
            DeckField {
                id: controlSearch
                objectName: "flightDeckControlSearch"
                Layout.preferredWidth: root.width >= 900 ? 250 : 190
                placeholderText: "Search controls…"
                text: root.searchText
                onTextEdited: root.searchText = text
            }
        }

        SectionLabel { visible: root.canShowInputContent; text: "VIRTUAL BUTTONS" }
        Text {
            visible: root.canShowInputContent && buttonItems.length === 0
            text: "No button outputs are available for this setup yet."
            color: deck.textMuted
            font.pixelSize: deck.scale(10)
            Layout.fillWidth: true
        }
        Flow {
            id: virtualButtonFlow
            visible: root.canShowInputContent
            Layout.fillWidth: true
            spacing: deck.space12
            Repeater {
                model: root.buttonItems
                delegate: VirtualButtonCard {
                    required property var modelData
                    button: modelData
                }
            }
        }

        SectionLabel { visible: root.canShowInputContent && root.povItems.length > 0; text: "HATS / POV" }
        Text {
            visible: root.canShowInputContent && root.povItems.length > 0
            text: root.guidedPresentation
                ? "Map directions to game buttons, or send the whole hat to a game POV."
                : "Map a hat direction to a button, or choose a whole-hat POV destination when one is available."
            color: deck.textMuted
            font.pixelSize: deck.scale(10)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Repeater {
            model: root.canShowInputContent ? root.povItems : []
            delegate: HatCard {
                required property var modelData
                hat: modelData
            }
        }

        Item { Layout.preferredHeight: deck.space12 }
    }

    FlightDeckDialog {
        id: mappingConflict
        tokens: deck
        heading: conflictButtonIndex > 0 ? "Virtual output is already in use" : "Output already assigned"
        tone: "attention"
        preferredWidth: 520
        contentItem: ColumnLayout {
            width: mappingConflict.availableWidth
            spacing: deck.space12
            Text {
                Layout.fillWidth: true
                text: conflictButtonIndex > 0
                    ? "Existing source:\n    " + String(conflictOwner.ownerLabel || "Configured source")
                      + "\n\nNew source:\n    " + root.inputDeviceName + " · Button " + conflictButtonIndex
                    : "This vJoy button is already routed. Replace moves the existing route. POV directions do not support shared output routes."
                color: deck.textSecondary
                font.pixelSize: deck.scale(11)
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                DeckButton { text: "CANCEL"; subdued: true; onClicked: mappingConflict.close() }
                Item { Layout.fillWidth: true }
                DeckButton {
                    visible: conflictButtonIndex > 0 && !root.guidedPresentation
                    text: "MIX"
                    subdued: true
                    onClicked: {
                        mappingConflict.close()
                        mixerModeDialog.open()
                    }
                }
                DeckButton {
                    text: "REPLACE"
                    onClicked: {
                        const changed = conflictButtonIndex > 0
                            ? root.requestButtonMapping(conflictButtonIndex, conflictTarget, true)
                            : root.requestPovMapping(conflictHatIndex, conflictDirectionIndex, conflictTarget, true)
                        if (changed) mappingConflict.close()
                    }
                }
                DeckButton {
                    visible: conflictButtonIndex > 0 && root.guidedPresentation
                    text: "OPEN IN FULL"
                    subdued: true
                    onClicked: {
                        mappingConflict.close()
                        root.requestFullAccess(11, { source: "button-conflict",
                            buttonIndex: root.conflictButtonIndex,
                            hatIndex: root.conflictHatIndex,
                            directionIndex: root.conflictDirectionIndex,
                            target: root.conflictTarget, owner: root.conflictOwner })
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: mixerModeDialog
        tokens: deck
        heading: "Choose mixer mode"
        tone: "informational"
        preferredWidth: 460
        contentItem: ColumnLayout {
            width: mixerModeDialog.availableWidth
            spacing: deck.space12
            Text {
                Layout.fillWidth: true
                text: "Create one canonical shared-output mixer for " + root.inputDeviceName
                    + " · Button " + root.conflictButtonIndex + " and "
                    + String(root.conflictOwner.ownerLabel || "the existing source") + "."
                color: deck.textSecondary
                font.pixelSize: deck.scale(11)
                wrapMode: Text.WordWrap
            }
            DeckCombo {
                id: mixerModeSelector
                Layout.fillWidth: true
                model: [
                    { key: "sum-clamped", label: "Sum / Clamp" },
                    { key: "highest-magnitude", label: "Larger Value" },
                    { key: "average", label: "Average" }
                ]
                textRole: "label"
            }
            RowLayout {
                Layout.fillWidth: true
                DeckButton { text: "CANCEL"; subdued: true; onClicked: mixerModeDialog.close() }
                Item { Layout.fillWidth: true }
                DeckButton {
                    text: "MIX"
                    onClicked: {
                        const result = backend.mixButtonMapping(root.conflictButtonIndex,
                            root.conflictTarget, mixerModeSelector.model[mixerModeSelector.currentIndex].key)
                        if (result.success) mixerModeDialog.close()
                    }
                }
            }
        }
    }
}
