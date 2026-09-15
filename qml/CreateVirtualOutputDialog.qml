import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// The one canonical Virtual Output creation surface.  Entry points provide
// only their optional Rig context; capability selection and the backend
// transaction stay identical everywhere else in the application.
FlightDeckDialog {
    id: dialog
    objectName: "canonicalCreateVirtualOutputDialog"

    required property var backendObject
    property var rigItems: []
    property string viewedRigId: ""
    property bool attachToViewedRig: false
    property string mode: "match-existing" // match-existing, preset, custom
    property string matchExistingId: ""
    property int presetIndex: 0
    property int selectedDeviceId: 1
    property var customAxes: [1, 2, 3, 6]
    property int customButtons: 32
    property int customContinuousPovs: 0
    property int customDiscretePovs: 0
    property string initialNamePrefix: "Flight Deck Output"

    heading: "Create Virtual Output"
    tone: "informational"
    preferredWidth: 720

    signal created(var result)
    signal failed(var result)

    // Keep the user-facing output names neutral.  X/Rx/slider identifiers
    // are vJoy driver slots, not a useful default name for a configurable
    // channel; the persisted capability still uses the same canonical IDs.
    readonly property var axisOptions: [
        { id: 1, label: "Axis 1" }, { id: 2, label: "Axis 2" }, { id: 3, label: "Axis 3" },
        { id: 4, label: "Axis 4" }, { id: 5, label: "Axis 5" }, { id: 6, label: "Axis 6" },
        { id: 7, label: "Axis 7" }, { id: 8, label: "Axis 8" }
    ]
    readonly property var presets: [
        { name: "BF6 Standard", axes: [1, 2, 3, 6], buttons: 32, continuousPovs: 0, discretePovs: 0 },
        { name: "Star Citizen / Extended", axes: [1, 2, 3, 4, 5, 6, 7, 8], buttons: 64, continuousPovs: 0, discretePovs: 0 },
        { name: "Full vJoy", axes: [1, 2, 3, 4, 5, 6, 7, 8], buttons: 128, continuousPovs: 4, discretePovs: 0 },
        { name: "Minimal / Basic", axes: [1, 2], buttons: 16, continuousPovs: 0, discretePovs: 0 }
    ]
    readonly property var outputLayouts: backendObject ? backendObject.virtualOutputLayouts : []
    readonly property var selectedExisting: outputFor(matchExistingId)
    readonly property var selectedPreset: presets[presetIndex] || presets[0]
    readonly property var activeCapabilities: mode === "match-existing" ? selectedExisting
        : mode === "preset" ? selectedPreset
        : ({ axesList: customAxes, buttons: customButtons, continuousPovs: customContinuousPovs,
             discretePovs: customDiscretePovs })
    readonly property string validationReason: validationMessage()
    readonly property bool saveEnabled: validationReason.length === 0

    function outputFor(id) {
        for (let index = 0; index < outputLayouts.length; ++index) {
            const output = outputLayouts[index] || ({})
            if (String(output.id || "") === String(id || "")) return output
        }
        return ({})
    }

    function rigName() {
        for (let index = 0; index < rigItems.length; ++index) {
            const rig = rigItems[index] || ({})
            if (String(rig.id || "") === String(viewedRigId || "")) return String(rig.name || "Device Rig")
        }
        return "Device Rig"
    }

    function outputIndex(id) {
        for (let index = 0; index < outputLayouts.length; ++index)
            if (String((outputLayouts[index] || {}).id || "") === String(id || "")) return index
        return outputLayouts.length ? 0 : -1
    }

    function axisLabels(axes) {
        if (typeof axes === "string") return axes.length ? axes : "None"
        const values = axes || []
        const labels = []
        for (let index = 0; index < axisOptions.length; ++index) {
            const option = axisOptions[index]
            if (values.indexOf(option.id) >= 0) labels.push(option.label)
        }
        return labels.length ? labels.join(" · ") : "None"
    }

    function capabilityAxes(capabilities) {
        return capabilities && capabilities.axes !== undefined ? capabilities.axes : capabilityAxesList(capabilities)
    }

    function capabilityAxesList(capabilities) {
        if (!capabilities) return []
        if (capabilities.axesList !== undefined) return capabilities.axesList || []
        if (capabilities.axes !== undefined && typeof capabilities.axes !== "string") return capabilities.axes || []
        return []
    }

    function deviceAssignment() {
        for (let index = 0; index < outputLayouts.length; ++index) {
            const output = outputLayouts[index] || ({})
            if (Number(output.deviceId || 0) === Number(selectedDeviceId)) return output
        }
        return null
    }

    function deviceState() {
        const assigned = deviceAssignment()
        return assigned
            ? "OWNED BY " + String(assigned.name || "another output")
            : "AVAILABLE IN HOTAS BF6 · Setup Health will inspect the driver"
    }

    function axisEnabled(axis) { return customAxes.indexOf(axis) >= 0 }
    function setAxisEnabled(axis, enabled) {
        const next = customAxes.filter(function(entry) { return Number(entry) !== Number(axis) })
        if (enabled) next.push(axis)
        customAxes = next
    }

    function capabilitySummary(capabilities) {
        const values = capabilities || ({})
        return "Axes\n    " + axisLabels(capabilityAxes(values))
            + "\n\nButtons\n    " + Number(values.buttons || 0)
            + "\n\nPOVs\n    " + Number(values.continuousPovs || 0) + " continuous"
            + "\n    " + Number(values.discretePovs || 0) + " discrete"
    }

    function validationMessage() {
        if (!outputName.text.trim().length) return "Enter an output name."
        if (selectedDeviceId < 1 || selectedDeviceId > 16) return "Choose a vJoy Device from 1 through 16."
        if (deviceAssignment()) return "vJoy Device " + selectedDeviceId + " is already owned by "
            + String(deviceAssignment().name || "another output") + "."
        if (mode === "match-existing" && !String(matchExistingId || "").length)
            return "Choose an existing Virtual Output to match."
        if (mode === "match-existing" && !selectedExisting.id)
            return "The selected Virtual Output is no longer available."
        if (mode === "custom" && customAxes.length === 0)
            return "Enable at least one virtual axis."
        if (customButtons < 0 || customButtons > 128)
            return "Choose a button count from 0 through 128."
        if (customContinuousPovs < 0 || customContinuousPovs > 4
                || customDiscretePovs < 0 || customDiscretePovs > 4)
            return "Choose up to four POVs."
        if (customContinuousPovs > 0 && customDiscretePovs > 0)
            return "Choose continuous or discrete POVs, not both."
        if (attachToViewedRig && !String(viewedRigId || "").length)
            return "Choose a Device Rig before adding this output to one."
        return ""
    }

    function selectedModeForBackend() {
        return mode === "match-existing" ? "copy-output" : "custom"
    }

    function selectedSourceForBackend() {
        return mode === "match-existing" ? String(matchExistingId || "") : ""
    }

    function selectedAxesForBackend() {
        return mode === "preset" ? selectedPreset.axes : customAxes
    }

    function selectedButtonsForBackend() {
        return mode === "preset" ? Number(selectedPreset.buttons || 0) : customButtons
    }

    function selectedContinuousPovsForBackend() {
        return mode === "preset" ? Number(selectedPreset.continuousPovs || 0) : customContinuousPovs
    }

    function selectedDiscretePovsForBackend() {
        return mode === "preset" ? Number(selectedPreset.discretePovs || 0) : customDiscretePovs
    }

    function resetDraft() {
        const suggested = Number(backendObject ? backendObject.suggestedVirtualOutputDeviceId() : 0)
        selectedDeviceId = suggested > 0 ? suggested : 1
        outputName.text = suggested > 0 ? initialNamePrefix + " " + suggested : ""
        mode = "match-existing"
        matchExistingId = outputLayouts.length ? String((outputLayouts[0] || {}).id || "") : ""
        presetIndex = 0
        customAxes = [1, 2, 3, 6]
        customButtons = 32
        customContinuousPovs = 0
        customDiscretePovs = 0
        attachToViewedRig = String(viewedRigId || "").length > 0
    }

    function openFor(rigId) {
        viewedRigId = String(rigId || "")
        resetDraft()
        open()
    }

    function saveOutput() {
        if (!saveEnabled || !backendObject) return
        const result = backendObject.createVirtualOutputLayoutResult(outputName.text,
            selectedDeviceId, selectedModeForBackend(), selectedSourceForBackend(), selectedAxesForBackend(),
            selectedButtonsForBackend(), selectedContinuousPovsForBackend(), selectedDiscretePovsForBackend(),
            String(viewedRigId || ""), attachToViewedRig, false)
        if (!result.success) {
            failed(result)
            return
        }
        created(result)
        close()
    }

    onOpened: resetDraft()

    component DeckButton: Button {
        id: button
        property bool selected: false
        property bool subdued: false
        implicitHeight: tokens.compactControlHeight
        leftPadding: tokens.space12
        rightPadding: tokens.space12
        focusPolicy: Qt.StrongFocus
        background: Rectangle {
            radius: tokens.radiusControl
            color: !button.enabled ? tokens.disabled
                : button.down ? tokens.selected
                : button.selected ? tokens.accent
                : button.subdued ? tokens.secondarySurface : tokens.elevatedSurface
            border.width: button.activeFocus ? 2 : 1
            border.color: button.activeFocus ? tokens.focus
                : button.selected ? tokens.accent : tokens.border
        }
        contentItem: Text {
            text: button.text
            color: !button.enabled ? tokens.textMuted
                : button.selected ? (tokens.light ? "white" : tokens.primarySurface) : tokens.textSecondary
            font.family: tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component DeckToggle: Rectangle {
        id: toggle
        property string label: ""
        property bool checked: false
        signal toggled(bool checked)
        implicitHeight: tokens.compactControlHeight
        implicitWidth: Math.max(78, toggleLabel.implicitWidth + tokens.space24)
        radius: tokens.radiusControl
        color: mouse.containsMouse ? tokens.selected : checked ? tokens.accentMuted : tokens.secondarySurface
        border.width: 1
        border.color: checked ? tokens.accent : tokens.border
        Text {
            id: toggleLabel
            anchors.centerIn: parent
            text: toggle.label
            color: toggle.checked ? tokens.textPrimary : tokens.textSecondary
            font.family: tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: toggle.toggled(!toggle.checked)
        }
    }

    component DeckCombo: ComboBox {
        id: combo
        implicitHeight: tokens.controlHeight
        leftPadding: tokens.space12
        rightPadding: tokens.space32
        font.family: tokens.telemetryFont
        font.pixelSize: 10
        contentItem: Text {
            text: combo.displayText
            color: combo.enabled ? tokens.textPrimary : tokens.textMuted
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: combo.width - width - tokens.space12
            anchors.verticalCenter: parent.verticalCenter
            text: combo.popup.visible ? "⌃" : "⌄"
            color: tokens.textMuted
            font.pixelSize: 12
        }
        background: Rectangle {
            radius: tokens.radiusControl
            color: tokens.elevatedSurface
            border.width: combo.activeFocus ? 2 : 1
            border.color: combo.activeFocus ? tokens.focus : tokens.border
        }
        delegate: ItemDelegate {
            width: ListView.view ? ListView.view.width : combo.width
            height: tokens.controlHeight
            highlighted: combo.highlightedIndex === index
            contentItem: Text {
                text: combo.textAt(index)
                color: tokens.textPrimary
                font.family: tokens.telemetryFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle { color: highlighted ? tokens.selected : "transparent" }
        }
        popup: Popup {
            y: combo.height + 4
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight, 240)
            padding: 4
            background: Rectangle { color: tokens.elevatedSurface; radius: tokens.radiusControl; border.color: tokens.border }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
        }
    }

    contentItem: ScrollView {
        id: dialogScroll
        clip: true
        implicitWidth: dialog.availableWidth
        implicitHeight: Math.min(dialog.maximumBodyHeight, creationForm.implicitHeight + tokens.space4)
        contentWidth: availableWidth
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ColumnLayout {
            id: creationForm
            width: dialogScroll.availableWidth
            spacing: tokens.space12

            Text {
                Layout.fillWidth: true
                text: "Define a reusable virtual-controller capability contract. Creating it does not provision vJoy; Setup Health performs that explicit driver step."
                color: tokens.textSecondary
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text { text: "CREATION MODE"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: tokens.space8
                DeckButton { Layout.fillWidth: true; text: "MATCH EXISTING"; selected: dialog.mode === "match-existing"; onClicked: dialog.mode = "match-existing" }
                DeckButton { Layout.fillWidth: true; text: "USE PRESET"; selected: dialog.mode === "preset"; onClicked: dialog.mode = "preset" }
                DeckButton { Layout.fillWidth: true; text: "CUSTOM"; selected: dialog.mode === "custom"; onClicked: dialog.mode = "custom" }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: tokens.space8
                visible: dialog.mode === "match-existing"
                Text { text: "SOURCE OUTPUT"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
                DeckCombo {
                    id: existingOutputPicker
                    Layout.fillWidth: true
                    model: dialog.outputLayouts
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: dialog.outputIndex(dialog.matchExistingId)
                    onActivated: dialog.matchExistingId = String(currentValue || "")
                }
                Text { Layout.fillWidth: true; text: dialog.selectedExisting.id ? dialog.capabilitySummary(dialog.selectedExisting) : "Choose an existing Virtual Output to inspect its capability contract."; color: tokens.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: tokens.space8
                visible: dialog.mode === "preset"
                Text { text: "CAPABILITY PRESET"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
                DeckCombo {
                    Layout.fillWidth: true
                    model: dialog.presets
                    textRole: "name"
                    currentIndex: dialog.presetIndex
                    onActivated: dialog.presetIndex = currentIndex
                }
                Text { Layout.fillWidth: true; text: dialog.capabilitySummary(dialog.selectedPreset); color: tokens.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: tokens.space8
                visible: dialog.mode === "custom"
                Text { text: "AXES"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
                Flow {
                    Layout.fillWidth: true
                    spacing: tokens.space8
                    Repeater {
                        model: dialog.axisOptions
                        delegate: DeckToggle {
                            required property var modelData
                            label: modelData.label
                            checked: dialog.axisEnabled(modelData.id)
                            onToggled: function(checked) { dialog.setAxisEnabled(modelData.id, checked) }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "BUTTONS"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
                    Item { Layout.fillWidth: true }
                    DeckButton { text: "−"; subdued: true; enabled: dialog.customButtons > 0; onClicked: dialog.customButtons -= 1 }
                    Text { text: String(dialog.customButtons); color: tokens.textPrimary; font.family: tokens.telemetryFont; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 42 }
                    DeckButton { text: "+"; subdued: true; enabled: dialog.customButtons < 128; onClicked: dialog.customButtons += 1 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "CONTINUOUS POVs"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
                    Item { Layout.fillWidth: true }
                    DeckButton { text: "−"; subdued: true; enabled: dialog.customContinuousPovs > 0; onClicked: dialog.customContinuousPovs -= 1 }
                    Text { text: String(dialog.customContinuousPovs); color: tokens.textPrimary; font.family: tokens.telemetryFont; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 42 }
                    DeckButton { text: "+"; subdued: true; enabled: dialog.customContinuousPovs < 4; onClicked: { dialog.customContinuousPovs += 1; dialog.customDiscretePovs = 0 } }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "DISCRETE POVs"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
                    Item { Layout.fillWidth: true }
                    DeckButton { text: "−"; subdued: true; enabled: dialog.customDiscretePovs > 0; onClicked: dialog.customDiscretePovs -= 1 }
                    Text { text: String(dialog.customDiscretePovs); color: tokens.textPrimary; font.family: tokens.telemetryFont; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 42 }
                    DeckButton { text: "+"; subdued: true; enabled: dialog.customDiscretePovs < 4; onClicked: { dialog.customDiscretePovs += 1; dialog.customContinuousPovs = 0 } }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: tokens.divider }
            Text { text: "COMMON OUTPUT SETTINGS"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
            Text { text: "OUTPUT NAME"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
            TextField {
                id: outputName
                objectName: "canonicalVirtualOutputName"
                Layout.fillWidth: true
                placeholderText: "e.g. Flight Deck Output 2"
                selectByMouse: true
                color: tokens.textPrimary
                font.family: tokens.bodyFont
                background: Rectangle { radius: tokens.radiusControl; color: tokens.elevatedSurface; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? tokens.focus : tokens.border }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "VJOY DEVICE"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                DeckButton { text: "−"; subdued: true; enabled: dialog.selectedDeviceId > 1; onClicked: dialog.selectedDeviceId -= 1 }
                Text { text: String(dialog.selectedDeviceId); color: tokens.textPrimary; font.family: tokens.telemetryFont; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 42 }
                DeckButton { text: "+"; subdued: true; enabled: dialog.selectedDeviceId < 16; onClicked: dialog.selectedDeviceId += 1 }
            }
            Text { Layout.fillWidth: true; text: "vJoy " + dialog.selectedDeviceId + " · " + dialog.deviceState(); color: dialog.deviceAssignment() ? tokens.attention : tokens.ready; font.family: tokens.telemetryFont; font.pixelSize: 9; wrapMode: Text.WordWrap }

            DeckToggle {
                Layout.fillWidth: true
                visible: String(dialog.viewedRigId || "").length > 0
                label: "ADD TO " + dialog.rigName().toUpperCase()
                checked: dialog.attachToViewedRig
                onToggled: function(checked) { dialog.attachToViewedRig = checked }
            }
            Text { Layout.fillWidth: true; visible: String(dialog.viewedRigId || "").length > 0 && dialog.attachToViewedRig; text: "The output will be added to this Rig but will not replace its primary output or activate the Rig."; color: tokens.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: summaryText.implicitHeight + tokens.space24
                radius: tokens.radiusControl
                color: tokens.secondarySurface
                border.color: tokens.border
                Text {
                    id: summaryText
                    anchors.fill: parent
                    anchors.margins: tokens.space12
                    text: "NEW VIRTUAL OUTPUT\n\n" + (outputName.text.trim() || "Untitled output")
                        + "\n\nvJoy Device\n    " + dialog.selectedDeviceId
                        + "\n\n" + dialog.capabilitySummary(dialog.activeCapabilities)
                        + (dialog.attachToViewedRig && dialog.viewedRigId.length ? "\n\nAdd to Rig\n    " + dialog.rigName() : "")
                    color: tokens.textPrimary
                    font.family: tokens.telemetryFont
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
            Text { Layout.fillWidth: true; visible: dialog.validationReason.length > 0; text: dialog.validationReason; color: tokens.attention; font.pixelSize: 10; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                DeckButton { text: "CANCEL"; subdued: true; onClicked: dialog.close() }
                DeckButton { objectName: "canonicalCreateVirtualOutputConfirm"; text: "SAVE OUTPUT"; selected: true; enabled: dialog.saveEnabled; onClicked: dialog.saveOutput() }
            }
        }
    }
}
