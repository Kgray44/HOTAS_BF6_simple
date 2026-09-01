import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Presentation-only drafts live here. The backend sees a configuration only
// when SAVE succeeds, then recompiles immutable runtime records off the input
// report path.
Item {
    id: root

    property var backendObject
    property var themeTokens: null
    property bool legacy: false
    property bool topGun: false
    property bool editing: false
    property bool draftDirty: false
    property var draft: ({})
    property string editingId: ""
    property bool advancedOpen: false
    // Keep an unsaved editor value without retaining the page, its dialogs,
    // or its large repeater tree while another page is active.
    property var presentationState: ({})
    signal presentationStateCaptured(var state)

    readonly property color panelFill: legacy ? "#e9161d23" : (themeTokens ? (topGun ? "#d80b1b20" : "#ed182128") : "#1a1d23")
    readonly property color raisedFill: legacy ? "#1b2a31" : (themeTokens ? themeTokens.panelRaised : "#20282d")
    readonly property color controlFill: legacy ? "#10171b" : (themeTokens ? themeTokens.control : "#10171b")
    readonly property color borderColor: legacy ? "#52717c" : (themeTokens ? themeTokens.border : "#435660")
    readonly property color strongBorder: legacy ? "#78aab9" : (themeTokens ? themeTokens.borderStrong : "#78aab9")
    readonly property color dividerColor: legacy ? "#335268" : (themeTokens ? themeTokens.divider : "#335268")
    readonly property color textColor: legacy ? "#f3f7f7" : (themeTokens ? themeTokens.textStrong : "#f3f7f7")
    readonly property color mutedColor: legacy ? "#9aa3a7" : (themeTokens ? themeTokens.textMuted : "#9aa3a7")
    readonly property color faintColor: legacy ? "#77919a" : (themeTokens ? themeTokens.textFaint : "#77919a")
    readonly property color accentColor: legacy ? "#78aab9" : (themeTokens ? themeTokens.orange : "#78aab9")
    readonly property color readyColor: legacy ? "#8fd5c9" : (themeTokens ? themeTokens.ready : "#8fd5c9")
    readonly property color warningColor: legacy ? "#d4ad69" : (themeTokens ? themeTokens.warning : "#d4ad69")
    readonly property color dangerColor: legacy ? "#ca9090" : (themeTokens ? themeTokens.danger : "#ca9090")
    readonly property string displayFont: topGun && themeTokens ? themeTokens.displayFont : "Segoe UI Variable"
    readonly property string telemetryFont: themeTokens ? themeTokens.telemetryFont : "Consolas"
    readonly property var rules: backendObject ? backendObject.automationRules : []
    readonly property var axisChoices: ["Roll", "Pitch", "Throttle", "Rotation X", "Rotation Y", "Yaw", "Additional axis 1", "Additional axis 2"]
    readonly property var requirementKinds: ["Choose what happens", "Button", "Axis", "POV / Hat", "Profile"]
    readonly property var buttonStates: ["is held", "is not held", "is pressed", "is released", "is pressed multiple times", "is held for a while"]
    readonly property var buttonStateTypes: [5, 6, 11, 12, 13, 14]
    readonly property var axisComparisons: ["is above", "is below", "is between", "is outside", "crosses above", "crosses below"]
    readonly property var axisComparisonTypes: [1, 2, 3, 4, 15, 16]
    readonly property var povStates: ["points", "is not pointing"]
    readonly property var profileStates: ["is selected", "is active"]
    readonly property var effectTypes: ["Choose an effect", "Press and hold virtual button", "Toggle virtual button", "Use profile while active", "Switch profile", "Change axis sensitivity", "Adjust axis output", "Limit axis output", "Force axis to value", "Mix one axis into another", "Make axis follow another", "Tap virtual button", "Turn mapping on", "Turn mapping off", "Toggle mapping", "Temporarily enable Adaptive Response", "Temporarily disable Adaptive Response", "Apply Adaptive Response preset"]
    readonly property var behaviorChoices: ["While the trigger is active", "Toggle on each trigger", "Run briefly"]
    readonly property var sourceStages: ["Controller input", "Current mapped output"]
    readonly property var directions: ["Up", "Up-Right", "Right", "Down-Right", "Down", "Down-Left", "Left", "Up-Left"]

    function clone(value) { return JSON.parse(JSON.stringify(value)) }
    function restorePresentationState() {
        const saved = presentationState || ({})
        if (!saved.editing) return
        editing = true
        draftDirty = !!saved.draftDirty
        draft = clone(saved.draft || ({}))
        editingId = saved.editingId || ""
        advancedOpen = !!saved.advancedOpen
    }
    function capturePresentationState() {
        presentationStateCaptured({
            editing: editing,
            draftDirty: draftDirty,
            draft: editing ? clone(draft) : ({}),
            editingId: editingId,
            advancedOpen: advancedOpen
        })
    }
    Component.onCompleted: restorePresentationState()
    Component.onDestruction: capturePresentationState()
    function setDraft(value) { draft = clone(value); draftDirty = true }
    function defaultRequirement(type) { return ({ type: type === undefined ? -1 : type, axis: 0, minimum: 0, maximum: 0, hysteresis: 0, button: 1, povHat: 1, povDirection: 1, profileId: "", pressCount: 2, multiPressWindowMs: 350, longPressDurationMs: 600 }) }
    function defaultEffect() { return ({ type: -1, virtualButton: 1, profileId: "", adaptiveResponsePresetId: "", targetAxis: 0, sourceAxis: 0, sourceStage: 1, value: 0, offset: 0, minimum: -1, maximum: 1, tapDurationMs: 80 }) }
    function includesType(types, type) { return types.indexOf(Number(type)) >= 0 }
    function buttonStateIndex(type) { const index = buttonStateTypes.indexOf(Number(type)); return index < 0 ? 0 : index }
    function axisComparisonIndex(type) { const index = axisComparisonTypes.indexOf(Number(type)); return index < 0 ? 0 : index }
    function behaviorMode(rule) { return rule && rule.activationMode !== undefined ? Number(rule.activationMode) : 0 }
    function isMultiPress(requirement) { return requirement && Number(requirement.type) === 13 }
    function isLongPress(requirement) { return requirement && Number(requirement.type) === 14 }
    function isTap(effect) { return effect && Number(effect.type) === 10 }
    function requirementKind(condition) {
        const type = condition && condition.type !== undefined ? Number(condition.type) : -1
        if (includesType(axisComparisonTypes, type)) return 2
        if (includesType(buttonStateTypes, type)) return 1
        if (type >= 7 && type <= 8) return 3
        if (type >= 9 && type <= 10) return 4
        return 0
    }
    function requirementType(kind, previous) {
        if (kind === 1) return includesType(buttonStateTypes, previous) ? previous : 5
        if (kind === 2) return includesType(axisComparisonTypes, previous) ? previous : 1
        if (kind === 3) return previous === 8 ? 8 : 7
        if (kind === 4) return previous === 10 ? 10 : 9
        return -1
    }
    function isAlwaysActive(rule) {
        const requirements = rule && rule.conditions ? rule.conditions : []
        return requirements.length === 1 && Number(requirements[0].type) === 0
    }
    function friendlyDraft(rule) {
        const next = clone(rule)
        const requirements = next.conditions || []
        const ordinary = requirements.filter(function(requirement) { return Number(requirement.type) !== 0 })
        const hasAlways = ordinary.length !== requirements.length
        if (hasAlways) {
            // Existing ALL + Always rules are equivalent without Always;
            // ANY + Always is equivalent to the rule-level all-time mode.
            next.conditions = next.matchMode === 1 || ordinary.length === 0 ? [defaultRequirement(0)] : ordinary
        }
        return next
    }
    function ruleForId(id) {
        for (let index = 0; index < rules.length; ++index) if (rules[index].id === id) return rules[index]
        return null
    }
    function openRule(rule) {
        if (!rule) return
        editingId = rule.id
        draft = friendlyDraft(rule)
        draftDirty = false
        advancedOpen = false
        editing = true
    }
    function openRuleById(id) { openRule(ruleForId(id)) }
    function closeEditor() { editing = false; draftDirty = false; editingId = ""; draft = ({}); advancedOpen = false }
    function returnToOverview() { if (draftDirty) discardDialog.open(); else closeEditor() }
    function newAutomation() { const id = backendObject.createAutomation(); if (id && id.length) openRuleById(id) }
    function duplicateAutomation(id) { const copyId = backendObject.duplicateAutomation(id); if (copyId && copyId.length) openRuleById(copyId) }
    function saveDraft() { if (canSaveDraft() && backendObject.saveAutomation(draft)) closeEditor() }
    function updateCondition(index, key, value) { const next = clone(draft); next.conditions[index][key] = value; setDraft(next) }
    function updateAction(index, key, value) { const next = clone(draft); next.actions[index][key] = value; setDraft(next) }
    function addRequirement(kind) { const next = clone(draft); next.conditions.push(defaultRequirement(requirementType(kind || 0, -1))); setDraft(next) }
    function addEffect() { const next = clone(draft); next.actions.push(defaultEffect()); setDraft(next) }
    function removeCondition(index) { const next = clone(draft); next.conditions.splice(index, 1); setDraft(next) }
    function removeAction(index) { const next = clone(draft); next.actions.splice(index, 1); setDraft(next) }
    function setRequirementKind(index, kind) { updateCondition(index, "type", requirementType(kind, Number(draft.conditions[index].type))) }
    function setEffectType(index, choice) {
        const next = clone(draft)
        const effect = next.actions[index]
        effect.type = choice - 1
        // The neutral defaults below prevent an unfinished builder row from
        // quietly changing an axis when a user is still choosing its details.
        if (effect.type === 4) effect.value = 1
        if (effect.type === 5 || effect.type === 8) effect.value = 0
        if (effect.type === 6) { effect.minimum = -1; effect.maximum = 1 }
        if (effect.type === 7) effect.value = 0
        if (effect.type === 9) { effect.value = 1; effect.offset = 0 }
        setDraft(next)
    }
    function setTriggerMode(alwaysActive) {
        const next = clone(draft)
        next.conditions = alwaysActive ? [defaultRequirement(0)] : []
        if (alwaysActive) next.activationMode = 0
        setDraft(next)
    }
    function setMatchMode(mode) { const next = clone(draft); next.matchMode = mode; setDraft(next) }
    function setBehaviorMode(mode) { const next = clone(draft); next.activationMode = mode; setDraft(next) }
    function percent(value) { return (Number(value) * 100).toFixed(1) + "%" }
    function conditionPercent(value, axis) { const displayed = axis === 2 ? (Number(value) + 1) * 50 : Number(value) * 100; return displayed.toFixed(1) + "%" }
    function conditionValue(text, axis) { const displayed = Number(String(text).replace("%", "")); return axis === 2 ? displayed / 50 - 1 : displayed / 100 }
    function profileIndex(id) { const choices = backendObject.profileTriggerChoices; for (let index = 0; index < choices.length; ++index) if (choices[index].id === id) return index; return -1 }
    function profileChoiceIndex(id) { const index = profileIndex(id); return index < 0 ? 0 : index + 1 }
    function profileChoiceId(index) { const choices = backendObject.profileTriggerChoices; return index > 0 && index <= choices.length ? choices[index - 1].id : "" }
    function profileChoicesWithPlaceholder() { return [{ label: "Choose a profile", id: "" }].concat(backendObject.profileTriggerChoices) }
    function adaptivePresetChoices() { return [{ label: "Choose an Adaptive Response preset", id: "" }].concat(backendObject.adaptiveResponsePresets || []) }
    function adaptivePresetIndex(id) { const choices = adaptivePresetChoices(); for (let index = 0; index < choices.length; ++index) if (choices[index].id === id) return index; return 0 }
    function adaptivePresetId(index) { const choices = adaptivePresetChoices(); return index > 0 && index < choices.length ? choices[index].id : "" }
    function adaptivePresetName(id) { const choices = adaptivePresetChoices(); const index = adaptivePresetIndex(id); return index > 0 ? choices[index].label : "a preset" }
    function profileName(id) { const index = profileIndex(id); const choices = backendObject.profileTriggerChoices; return index >= 0 && choices[index] ? choices[index].label : "a profile" }
    function axisName(index) { return index >= 0 && index < axisChoices.length ? axisChoices[index] : "axis" }
    function directionName(direction) { return direction >= 1 && direction <= directions.length ? directions[direction - 1] : "a direction" }
    function sourceStageName(stage) { return Number(stage) === 0 ? "controller input" : "current mapped output" }
    function requirementReady(requirement) {
        if (!requirement || Number(requirement.type) < 0) return false
        if (Number(requirement.type) === 9 || Number(requirement.type) === 10) return profileIndex(requirement.profileId) >= 0
        return true
    }
    function effectReady(effect) {
        if (!effect || Number(effect.type) < 0) return false
        if (Number(effect.type) === 2 || Number(effect.type) === 3) return profileIndex(effect.profileId) >= 0
        if (Number(effect.type) === 16) return adaptivePresetIndex(effect.adaptiveResponsePresetId) > 0
        return true
    }
    function requirementSummary(requirement) {
        if (!requirement || Number(requirement.type) < 0) return "Choose what this requirement should react to."
        switch (Number(requirement.type)) {
        case 1: return axisName(requirement.axis) + " is above " + conditionPercent(requirement.minimum, requirement.axis)
        case 2: return axisName(requirement.axis) + " is below " + conditionPercent(requirement.minimum, requirement.axis)
        case 3: return axisName(requirement.axis) + " is between " + conditionPercent(requirement.minimum, requirement.axis) + " and " + conditionPercent(requirement.maximum, requirement.axis)
        case 4: return axisName(requirement.axis) + " is outside " + conditionPercent(requirement.minimum, requirement.axis) + " to " + conditionPercent(requirement.maximum, requirement.axis)
        case 5: return "Button " + requirement.button + " is held"
        case 6: return "Button " + requirement.button + " is not held"
        case 11: return "Button " + requirement.button + " is pressed"
        case 12: return "Button " + requirement.button + " is released"
        case 13: return "Button " + requirement.button + " is pressed " + requirement.pressCount + " times"
        case 14: return "Button " + requirement.button + " is held for " + requirement.longPressDurationMs + " ms"
        case 15: return axisName(requirement.axis) + " crosses above " + conditionPercent(requirement.minimum, requirement.axis)
        case 16: return axisName(requirement.axis) + " crosses below " + conditionPercent(requirement.minimum, requirement.axis)
        case 7: return "POV " + requirement.povHat + " points " + directionName(requirement.povDirection)
        case 8: return "POV " + requirement.povHat + " is not pointing " + directionName(requirement.povDirection)
        case 9: return profileIndex(requirement.profileId) < 0 ? "Choose the selected profile" : "Selected profile is " + profileName(requirement.profileId)
        case 10: return profileIndex(requirement.profileId) < 0 ? "Choose the active profile" : "Active profile is " + profileName(requirement.profileId)
        }
        return "All the time"
    }
    function effectSummary(effect) {
        if (!effect || Number(effect.type) < 0) return "Choose what this automation should do."
        switch (Number(effect.type)) {
        case 0: return "Press and hold virtual button " + effect.virtualButton
        case 1: return "Toggle virtual button " + effect.virtualButton
        case 2: return profileIndex(effect.profileId) < 0 ? "Choose a profile to use while active" : "Use " + profileName(effect.profileId) + " while active"
        case 3: return profileIndex(effect.profileId) < 0 ? "Choose a profile to switch to" : "Switch to " + profileName(effect.profileId)
        case 4: return "Change " + axisName(effect.targetAxis) + " sensitivity to " + percent(effect.value)
        case 5: return "Adjust " + axisName(effect.targetAxis) + " output by " + percent(effect.value)
        case 6: return "Limit " + axisName(effect.targetAxis) + " output to " + percent(effect.minimum) + "–" + percent(effect.maximum)
        case 7: return "Force " + axisName(effect.targetAxis) + " to " + percent(effect.value)
        case 8: return "Mix " + axisName(effect.sourceAxis) + " from " + sourceStageName(effect.sourceStage) + " into " + axisName(effect.targetAxis) + " at " + percent(effect.value)
        case 9: return "Make " + axisName(effect.targetAxis) + " follow " + axisName(effect.sourceAxis)
            + " from " + sourceStageName(effect.sourceStage) + " at " + percent(effect.value)
            + (Number(effect.offset) === 0 ? "" : " with " + percent(effect.offset) + " offset")
        case 10: return "Tap virtual button " + effect.virtualButton
        case 11: return "Turn mapping on"
        case 12: return "Turn mapping off"
        case 13: return "Toggle mapping on or off"
        case 14: return "Temporarily enable Adaptive Response on " + axisName(effect.targetAxis)
        case 15: return "Temporarily disable Adaptive Response on " + axisName(effect.targetAxis)
        case 16: return "Temporarily apply Adaptive Response preset " + adaptivePresetName(effect.adaptiveResponsePresetId) + " to " + axisName(effect.targetAxis)
        }
        return "Choose what this automation should do."
    }
    function joinSummary(values, connector) {
        if (values.length === 0) return ""
        if (values.length === 1) return values[0]
        if (values.length === 2) return values[0] + " " + connector + " " + values[1]
        return values.slice(0, values.length - 1).join(", ") + ", " + connector + " " + values[values.length - 1]
    }
    function ruleSummary() {
        const requirements = draft.conditions || []
        const effects = draft.actions || []
        if (!isAlwaysActive(draft) && requirements.length === 0) return "Choose when this automation should run."
        if (!isAlwaysActive(draft) && requirements.some(function(requirement) { return !requirementReady(requirement) })) return "Finish choosing what this automation should react to."
        if (effects.length === 0) return "Add at least one effect."
        if (effects.some(function(effect) { return !effectReady(effect) })) return "Finish choosing what this automation should do."
        const effectText = joinSummary(effects.map(function(effect) { return effectSummary(effect) }), "and")
        if (isAlwaysActive(draft)) return "All the time, " + effectText.charAt(0).toLowerCase() + effectText.slice(1) + "."
        const whenText = joinSummary(requirements.map(function(requirement) { return requirementSummary(requirement) }), Number(draft.matchMode) === 1 ? "or" : "and")
        if (behaviorMode(draft) === 1) return "When " + whenText + ", toggle this automation on or off. While active, " + effectText.charAt(0).toLowerCase() + effectText.slice(1) + "."
        if (behaviorMode(draft) === 2) return "When " + whenText + ", activate this automation for " + draft.activeDurationMs + " ms. While active, " + effectText.charAt(0).toLowerCase() + effectText.slice(1) + "."
        return "When " + whenText + ", " + effectText.charAt(0).toLowerCase() + effectText.slice(1) + "."
    }
    function canSaveDraft() {
        const requirements = draft.conditions || []
        const effects = draft.actions || []
        return String(draft.name || "").trim().length > 0
            && (isAlwaysActive(draft) || (requirements.length > 0 && requirements.every(function(requirement) { return requirementReady(requirement) })))
            && effects.length > 0 && effects.every(function(effect) { return effectReady(effect) })
    }
    function ruleState(rule) { if (!rule.enabled) return rule.conditions.length === 0 || rule.actions.length === 0 ? "INCOMPLETE DRAFT" : "DISABLED"; if (rule.health === 2) return "NEEDS ATTENTION"; return rule.active ? "ACTIVE" : "STANDBY" }
    function ruleStateColor(rule) { if (!rule.enabled || rule.health === 2) return rule.conditions.length === 0 || rule.actions.length === 0 ? warningColor : dangerColor; return rule.active ? readyColor : mutedColor }

    // Reuse the established card components. The rich Top Gun panel details
    // therefore come from the same implementation as Axes, Buttons, Curves,
    // and Diagnostics rather than an Automation-specific approximation.
    component ThemedPanel: Item {
        id: panel
        property color surfaceColor: root.panelFill
        property color edgeColor: root.borderColor
        AviationPanel { anchors.fill: parent; visible: !root.legacy; theme: root.themeTokens; color: panel.surfaceColor; border.color: panel.edgeColor }
        // Legacy cards must use the exact same shared treatment as the
        // established Axes, Buttons, Curve Editor, and Diagnostics pages.
        LegacyAviationPanel { anchors.fill: parent; visible: root.legacy }
    }
    component FineLine: Rectangle { implicitHeight: 1; color: root.dividerColor }
    component ActionButton: Rectangle {
        id: actionButton
        property string label: "ACTION"
        property bool subdued: false
        property bool destructive: false
        property bool commandEnabled: true
        signal triggered()
        implicitWidth: Math.max(108, buttonLabel.implicitWidth + 28)
        implicitHeight: 34
        radius: root.topGun ? 1 : 4
        color: !commandEnabled ? root.controlFill : buttonMouse.containsMouse ? (destructive ? "#492728" : (subdued ? root.raisedFill : root.accentColor)) : (destructive ? "#352225" : (subdued ? root.controlFill : root.accentColor))
        border.color: destructive ? root.dangerColor : (subdued ? root.borderColor : root.strongBorder)
        opacity: commandEnabled ? 1.0 : 0.42
        Text { id: buttonLabel; anchors.centerIn: parent; text: parent.label; color: parent.destructive ? root.dangerColor : (parent.subdued ? root.textColor : "#081013"); font.pixelSize: 10; font.bold: true; font.family: root.displayFont }
        Rectangle { visible: root.topGun && !actionButton.subdued && !actionButton.destructive; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.rightMargin: 4; anchors.bottomMargin: 3; width: 22; height: 2; color: root.textColor }
        MouseArea { id: buttonMouse; anchors.fill: parent; hoverEnabled: true; enabled: actionButton.commandEnabled; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: actionButton.triggered() }
    }
    component EditorTextField: TextField {
        id: field
        implicitHeight: 34
        leftPadding: 10; rightPadding: 10; topPadding: 7; bottomPadding: 7
        color: root.textColor; placeholderTextColor: root.faintColor; selectByMouse: true
        font.pixelSize: 11; font.family: root.displayFont
        background: Rectangle { radius: root.topGun ? 1 : 4; color: !field.enabled ? "#0b1012" : (field.activeFocus ? root.raisedFill : root.controlFill); border.color: field.activeFocus ? root.accentColor : (field.hovered ? root.strongBorder : root.borderColor) }
    }
    component EditorCombo: ComboBox {
        id: combo
        implicitHeight: 34; leftPadding: 10; rightPadding: 28; font.pixelSize: 10; font.family: root.displayFont
        background: Rectangle { radius: root.topGun ? 1 : 4; color: combo.enabled ? (combo.hovered ? root.raisedFill : root.controlFill) : "#0b1012"; border.color: combo.activeFocus ? root.accentColor : (combo.hovered ? root.strongBorder : root.borderColor) }
        contentItem: Text { leftPadding: combo.leftPadding; rightPadding: combo.rightPadding; text: combo.displayText; color: combo.enabled ? root.textColor : root.faintColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font: combo.font }
        indicator: Text { x: combo.width - width - 9; y: (combo.height - height) / 2; text: "⌄"; color: root.mutedColor; font.pixelSize: 15 }
        delegate: ItemDelegate {
            width: combo.width; implicitHeight: 32; highlighted: combo.highlightedIndex === index
            contentItem: Text { text: combo.textAt(index); leftPadding: 10; rightPadding: 10; color: parent.highlighted ? root.textColor : root.mutedColor; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font.pixelSize: 10; font.family: root.displayFont }
            background: Rectangle { color: parent.highlighted ? root.raisedFill : "transparent"; border.color: parent.highlighted ? root.accentColor : "transparent" }
        }
        popup: Popup {
            y: combo.height + 4; width: combo.width; implicitHeight: Math.min(250, choices.contentHeight + 12); padding: 6
            contentItem: ListView { id: choices; clip: true; implicitHeight: contentHeight; model: combo.delegateModel; currentIndex: combo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator { } }
            background: Rectangle { color: root.raisedFill; border.color: root.strongBorder; radius: root.topGun ? 1 : 4 }
        }
    }
    component EditorSpin: SpinBox {
        id: spin
        implicitHeight: 34; editable: true; font.pixelSize: 10
        background: Rectangle { radius: root.topGun ? 1 : 4; color: root.controlFill; border.color: spin.activeFocus ? root.accentColor : (spin.hovered ? root.strongBorder : root.borderColor) }
        contentItem: TextInput { text: spin.textFromValue(spin.value, spin.locale); font: spin.font; color: root.textColor; horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter; readOnly: !spin.editable; validator: spin.validator; inputMethodHints: Qt.ImhFormattedNumbersOnly }
        up.indicator: Rectangle { implicitWidth: 25; implicitHeight: 16; color: spin.up.pressed ? root.raisedFill : "transparent"; Text { anchors.centerIn: parent; text: "+"; color: root.textColor; font.bold: true } }
        down.indicator: Rectangle { implicitWidth: 25; implicitHeight: 16; color: spin.down.pressed ? root.raisedFill : "transparent"; Text { anchors.centerIn: parent; text: "−"; color: root.textColor; font.bold: true } }
    }
    component EditorSwitch: Switch {
        id: toggle
        implicitHeight: 28; leftPadding: implicitIndicatorWidth + 8
        indicator: Rectangle {
            implicitWidth: 38; implicitHeight: 20; x: toggle.leftPadding - width; y: (toggle.height - height) / 2; radius: root.topGun ? 1 : 10
            color: toggle.checked ? root.readyColor : root.controlFill; border.color: toggle.checked ? root.readyColor : root.borderColor
            Rectangle { width: 14; height: 14; radius: root.topGun ? 1 : 7; x: toggle.checked ? parent.width - width - 3 : 3; anchors.verticalCenter: parent.verticalCenter; color: toggle.checked ? "#0b1012" : root.mutedColor; Behavior on x { NumberAnimation { duration: 110 } } }
        }
        contentItem: Text { text: toggle.text; color: root.mutedColor; font.pixelSize: 10; font.bold: true; verticalAlignment: Text.AlignVCenter; font.family: root.displayFont }
    }

    Flickable {
        id: overview
        anchors.fill: parent; visible: !root.editing; contentWidth: width; contentHeight: overviewContent.implicitHeight + 18; clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: overviewContent
            x: 1; width: overview.width - 10; spacing: 14
            RowLayout {
                width: parent.width
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Text { text: root.topGun ? "AUTOMATION  /  FLIGHT LOGIC" : "Automation"; color: root.textColor; font.pixelSize: root.topGun ? 40 : 26; font.bold: true; font.family: root.displayFont; style: root.topGun ? Text.Outline : Text.Normal; styleColor: "#8e3321" }
                    Text { text: "Compiled deterministic control rules · evaluated once per physical report"; color: root.mutedColor; font.pixelSize: 12; font.family: root.displayFont }
                }
                ActionButton { label: "+ NEW AUTOMATION"; onTriggered: root.newAutomation() }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: engineGrid.implicitHeight + 28; surfaceColor: root.topGun ? "#d80e1e1c" : root.panelFill
                GridLayout {
                    id: engineGrid
                    anchors.fill: parent; anchors.margins: 14; columns: width >= 900 ? 5 : (width >= 570 ? 3 : 2); columnSpacing: 18; rowSpacing: 12
                    ColumnLayout { Layout.fillWidth: true; spacing: 2
                        Text { text: "AUTOMATION ENGINE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                        Text { text: backendObject.automationEngineEnabled ? (root.topGun ? "ARMED" : "ENABLED") : "DISABLED"; color: backendObject.automationEngineEnabled ? root.readyColor : root.dangerColor; font.pixelSize: 17; font.bold: true; font.family: root.displayFont }
                    }
                    EditorSwitch { text: "ENGINE"; checked: backendObject.automationEngineEnabled; onToggled: backendObject.setAutomationEngineEnabled(checked) }
                    Repeater {
                        model: [{ label: "RULES", value: backendObject.automationRuleCount, tone: root.textColor }, { label: "ACTIVE RULES", value: backendObject.automationActiveRuleCount, tone: root.readyColor }, { label: "EVALUATION", value: backendObject.automationEvaluationUs + " µs", tone: root.textColor }]
                        delegate: ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: modelData.label; color: root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                            Text { text: modelData.value; color: modelData.tone; font.pixelSize: 17; font.bold: true; font.family: root.telemetryFont }
                        }
                    }
                }
            }
            ThemedPanel {
                visible: root.rules.length === 0; width: parent.width; implicitHeight: 180
                Column { anchors.centerIn: parent; width: Math.min(parent.width - 48, 560); spacing: 8
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "NO AUTOMATIONS"; color: root.textColor; font.pixelSize: 17; font.bold: true; font.family: root.displayFont }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: "Automation rules modify controller behavior from supported buttons, axes, POV states, and profiles. New rules begin disabled until you save a valid configuration."; color: root.mutedColor; font.pixelSize: 11 }
                    ActionButton { anchors.horizontalCenter: parent.horizontalCenter; label: "+ CREATE AUTOMATION"; onTriggered: root.newAutomation() }
                }
            }
            GridLayout {
                width: parent.width; columns: width >= 1060 ? 2 : 1; columnSpacing: 14; rowSpacing: 14
                Repeater {
                    model: root.rules
                    delegate: ThemedPanel {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: cardContent.implicitHeight + 28; surfaceColor: modelData.health === 2 ? "#2d2021" : root.panelFill; edgeColor: modelData.health === 2 ? root.dangerColor : (modelData.active ? root.readyColor : root.borderColor)
                        MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.openRule(modelData) }
                        ColumnLayout {
                            id: cardContent
                            anchors.fill: parent; anchors.margins: 14; spacing: 8
                            RowLayout { Layout.fillWidth: true
                                ColumnLayout { Layout.fillWidth: true; spacing: 2
                                    Text { text: modelData.name.toUpperCase(); color: root.textColor; font.pixelSize: 14; font.bold: true; font.family: root.displayFont; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: root.ruleState(modelData); color: root.ruleStateColor(modelData); font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                                }
                                EditorSwitch { checked: modelData.enabled; text: ""; onToggled: { if (!backendObject.setAutomationEnabled(modelData.id, checked)) checked = modelData.enabled } }
                            }
                            FineLine { Layout.fillWidth: true }
                            RowLayout { Layout.fillWidth: true
                                Text { text: modelData.enabled ? "Changes are active after Save" : "This automation is disabled"; color: root.mutedColor; font.pixelSize: 9; font.family: root.displayFont }
                                Item { Layout.fillWidth: true }
                                Text { text: "OPEN ›"; color: root.accentColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                            }
                            Text { text: modelData.behaviorLabel; color: root.accentColor; font.pixelSize: 9; font.bold: true; font.family: root.telemetryFont }
                            Text { text: "WHEN"; color: root.faintColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                            Text { text: modelData.conditions.length ? modelData.conditionSummary : "Nothing selected yet"; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { text: "DO"; color: root.faintColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                            Text { text: modelData.actions.length ? modelData.actionSummary : "Nothing selected yet"; color: modelData.actions.length ? root.accentColor : root.warningColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { visible: modelData.healthMessage.length > 0; text: modelData.healthMessage; color: root.dangerColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                                ActionButton { label: "DUPLICATE"; subdued: true; onTriggered: root.duplicateAutomation(modelData.id) }
                                ActionButton { label: "DELETE"; destructive: true; onTriggered: { deleteDialog.ruleId = modelData.id; deleteDialog.fromEditor = false; deleteDialog.open() } }
                            }
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: editorPage
        anchors.fill: parent; visible: root.editing; contentWidth: width; contentHeight: editorContent.implicitHeight + 18; clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: editorContent
            x: 1; width: editorPage.width - 10; spacing: 14
            RowLayout { width: parent.width
                ActionButton { label: "‹ AUTOMATION"; subdued: true; onTriggered: root.returnToOverview() }
                ColumnLayout { Layout.fillWidth: true; spacing: 2
                    Text { text: root.topGun ? "AUTOMATION  /  RULE BUILDER" : "Automation"; color: root.textColor; font.pixelSize: root.topGun ? 32 : 24; font.bold: true; font.family: root.displayFont; style: root.topGun ? Text.Outline : Text.Normal; styleColor: "#8e3321" }
                    Text { text: "Changes take effect after Save. The running mapper keeps the last saved automation."; color: root.mutedColor; font.pixelSize: 11; font.family: root.displayFont }
                }
                Text { text: root.draft.enabled ? "ENABLED ON SAVE" : "DISABLED"; color: root.draft.enabled ? root.readyColor : root.warningColor; font.pixelSize: 10; font.bold: true; font.family: root.displayFont }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: basicContent.implicitHeight + 28
                ColumnLayout {
                    id: basicContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 11
                    Text { text: "BASIC SETTINGS"; color: root.accentColor; font.pixelSize: 12; font.bold: true; font.family: root.displayFont }
                    GridLayout {
                        Layout.fillWidth: true; columns: width >= 650 ? 2 : 1; columnSpacing: 14; rowSpacing: 10
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "AUTOMATION NAME"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                            EditorTextField { Layout.fillWidth: true; text: root.draft.name || ""; placeholderText: "New Automation"; onTextEdited: { const next = root.clone(root.draft); next.name = text; root.setDraft(next) } }
                        }
                        ColumnLayout { Layout.fillWidth: true
                            Item { implicitHeight: 11 }
                            EditorSwitch { text: "Automation enabled"; checked: root.draft.enabled || false; onToggled: { const next = root.clone(root.draft); next.enabled = checked; root.setDraft(next) } }
                        }
                    }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: whenContent.implicitHeight + 28
                ColumnLayout {
                    id: whenContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 12
                    ColumnLayout { Layout.fillWidth: true; spacing: 7
                        Text { text: "WHEN SHOULD THIS HAPPEN?"; color: root.accentColor; font.pixelSize: 15; font.bold: true; font.family: root.displayFont }
                        RowLayout { Layout.fillWidth: true
                            Text { text: "This automation runs"; color: root.mutedColor; font.pixelSize: 10; Layout.fillWidth: true }
                            EditorCombo { width: 184; model: ["When something happens", "All the time"]; currentIndex: root.isAlwaysActive(root.draft) ? 1 : 0; onActivated: root.setTriggerMode(currentIndex === 1) }
                        }
                        Text { text: "Choose what the automation should react to."; color: root.mutedColor; font.pixelSize: 10 }
                    }
                    Text { visible: root.isAlwaysActive(root.draft); text: "This automation remains active whenever it is enabled."; color: root.readyColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    ThemedPanel {
                        visible: !root.isAlwaysActive(root.draft) && (root.draft.conditions || []).length === 0
                        Layout.fillWidth: true; implicitHeight: starterContent.implicitHeight + 24; surfaceColor: root.raisedFill
                        ColumnLayout {
                            id: starterContent
                            anchors.fill: parent; anchors.margins: 12; spacing: 10
                            Text { text: "Nothing selected yet."; color: root.textColor; font.pixelSize: 12; font.bold: true; font.family: root.displayFont }
                            Text { text: "Choose what this automation should react to:"; color: root.mutedColor; font.pixelSize: 10 }
                            Flow { Layout.fillWidth: true; spacing: 8
                                ActionButton { label: "BUTTON"; subdued: true; onTriggered: root.addRequirement(1) }
                                ActionButton { label: "AXIS"; subdued: true; onTriggered: root.addRequirement(2) }
                                ActionButton { label: "POV / HAT"; subdued: true; onTriggered: root.addRequirement(3) }
                                ActionButton { label: "PROFILE"; subdued: true; onTriggered: root.addRequirement(4) }
                                ActionButton { label: "ALWAYS ACTIVE"; subdued: true; onTriggered: root.setTriggerMode(true) }
                            }
                        }
                    }
                    Repeater {
                        model: root.isAlwaysActive(root.draft) ? [] : (root.draft.conditions || [])
                        delegate: ColumnLayout {
                            id: conditionCard
                            required property int index
                            required property var modelData
                            property int conditionIndex: index
                            Layout.fillWidth: true; spacing: 8
                            RowLayout { visible: conditionCard.conditionIndex > 0; Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                EditorCombo { width: 120; model: ["AND", "OR"]; currentIndex: Number(root.draft.matchMode) === 1 ? 1 : 0; onActivated: root.setMatchMode(currentIndex) }
                                Item { Layout.fillWidth: true }
                            }
                            ThemedPanel {
                                Layout.fillWidth: true; implicitHeight: requirementContent.implicitHeight + 24; surfaceColor: root.raisedFill
                                ColumnLayout {
                                    id: requirementContent
                                    anchors.fill: parent; anchors.margins: 12; spacing: 9
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: (conditionCard.conditionIndex + 1) + " · " + root.requirementSummary(modelData); color: root.textColor; font.pixelSize: 11; font.bold: true; font.family: root.displayFont; Layout.fillWidth: true; elide: Text.ElideRight }
                                        ActionButton { label: "REMOVE"; subdued: true; onTriggered: root.removeCondition(conditionCard.conditionIndex) }
                                    }
                                    Flow {
                                        id: requirementFlow
                                        Layout.fillWidth: true; spacing: 8
                                        EditorCombo { width: 144; model: root.requirementKinds; currentIndex: root.requirementKind(modelData); onActivated: function(choiceIndex) { root.setRequirementKind(conditionCard.conditionIndex, choiceIndex) } }
                                        EditorSpin { visible: root.requirementKind(modelData) === 1; width: 108; from: 1; to: 128; value: modelData.button; onValueModified: root.updateCondition(conditionCard.conditionIndex, "button", value) }
                                        EditorCombo { visible: root.requirementKind(modelData) === 1; width: 168; model: root.buttonStates; currentIndex: root.buttonStateIndex(modelData.type); onActivated: function(choiceIndex) { root.updateCondition(conditionCard.conditionIndex, "type", root.buttonStateTypes[choiceIndex]) } }
                                        EditorSpin { visible: root.isMultiPress(modelData); width: 78; from: 2; to: 5; value: modelData.pressCount; onValueModified: root.updateCondition(conditionCard.conditionIndex, "pressCount", value) }
                                        Text { visible: root.isMultiPress(modelData); text: "times"; color: root.mutedColor; font.pixelSize: 10; height: 34; verticalAlignment: Text.AlignVCenter }
                                        EditorSpin { visible: root.isMultiPress(modelData); width: 92; from: 150; to: 1000; value: modelData.multiPressWindowMs; onValueModified: root.updateCondition(conditionCard.conditionIndex, "multiPressWindowMs", value) }
                                        Text { visible: root.isMultiPress(modelData); text: "ms window"; color: root.mutedColor; font.pixelSize: 10; height: 34; verticalAlignment: Text.AlignVCenter }
                                        EditorSpin { visible: root.isLongPress(modelData); width: 92; from: 200; to: 3000; value: modelData.longPressDurationMs; onValueModified: root.updateCondition(conditionCard.conditionIndex, "longPressDurationMs", value) }
                                        Text { visible: root.isLongPress(modelData); text: "ms hold"; color: root.mutedColor; font.pixelSize: 10; height: 34; verticalAlignment: Text.AlignVCenter }
                                        EditorCombo { visible: root.requirementKind(modelData) === 2; width: 148; model: root.axisChoices; currentIndex: modelData.axis; onActivated: function(choiceIndex) { root.updateCondition(conditionCard.conditionIndex, "axis", choiceIndex) } }
                                        EditorCombo { visible: root.requirementKind(modelData) === 2; width: 142; model: root.axisComparisons; currentIndex: root.axisComparisonIndex(modelData.type); onActivated: function(choiceIndex) { root.updateCondition(conditionCard.conditionIndex, "type", root.axisComparisonTypes[choiceIndex]) } }
                                        EditorTextField { visible: root.requirementKind(modelData) === 2; width: 94; text: root.conditionPercent(modelData.minimum, modelData.axis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateCondition(conditionCard.conditionIndex, "minimum", root.conditionValue(text, modelData.axis)) } }
                                        Text { visible: Number(modelData.type) === 3 || Number(modelData.type) === 4; text: Number(modelData.type) === 3 ? "and" : "to"; color: root.mutedColor; font.pixelSize: 10; height: 34; verticalAlignment: Text.AlignVCenter }
                                        EditorTextField { visible: Number(modelData.type) === 3 || Number(modelData.type) === 4; width: 94; text: root.conditionPercent(modelData.maximum, modelData.axis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateCondition(conditionCard.conditionIndex, "maximum", root.conditionValue(text, modelData.axis)) } }
                                        EditorSpin { visible: root.requirementKind(modelData) === 3; width: 90; from: 1; to: 4; value: modelData.povHat; onValueModified: root.updateCondition(conditionCard.conditionIndex, "povHat", value) }
                                        EditorCombo { visible: root.requirementKind(modelData) === 3; width: 144; model: root.povStates; currentIndex: Number(modelData.type) === 8 ? 1 : 0; onActivated: function(choiceIndex) { root.updateCondition(conditionCard.conditionIndex, "type", choiceIndex === 1 ? 8 : 7) } }
                                        EditorCombo { visible: root.requirementKind(modelData) === 3; width: 124; model: root.directions; currentIndex: Number(modelData.povDirection) - 1; onActivated: function(choiceIndex) { root.updateCondition(conditionCard.conditionIndex, "povDirection", choiceIndex + 1) } }
                                        EditorCombo { visible: root.requirementKind(modelData) === 4; width: 132; model: root.profileStates; currentIndex: Number(modelData.type) === 10 ? 1 : 0; onActivated: function(choiceIndex) { root.updateCondition(conditionCard.conditionIndex, "type", choiceIndex === 1 ? 10 : 9) } }
                                        EditorCombo { visible: root.requirementKind(modelData) === 4; width: 170; model: root.profileChoicesWithPlaceholder(); textRole: "label"; currentIndex: root.profileChoiceIndex(modelData.profileId); onActivated: function(choiceIndex) { root.updateCondition(conditionCard.conditionIndex, "profileId", root.profileChoiceId(choiceIndex)) } }
                                    }
                                    RowLayout { visible: root.requirementKind(modelData) === 2; Layout.fillWidth: true
                                        Text { text: "Input stability"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorTextField { Layout.preferredWidth: 88; text: root.percent(modelData.hysteresis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateCondition(conditionCard.conditionIndex, "hysteresis", Number(text.replace("%", "")) / 100) } }
                                        Text { text: "Ignore tiny movements below this amount."; color: root.faintColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    }
                                }
                            }
                        }
                    }
                    ActionButton { visible: !root.isAlwaysActive(root.draft) && (root.draft.conditions || []).length > 0; label: "+ ADD REQUIREMENT"; subdued: true; commandEnabled: (root.draft.conditions || []).length < 4; onTriggered: root.addRequirement(0) }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: behaviorContent.implicitHeight + 28
                ColumnLayout {
                    id: behaviorContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text { text: "HOW SHOULD THIS AUTOMATION BEHAVE?"; color: root.accentColor; font.pixelSize: 15; font.bold: true; font.family: root.displayFont }
                    RowLayout { Layout.fillWidth: true
                        EditorCombo { Layout.preferredWidth: 260; model: root.behaviorChoices; enabled: !root.isAlwaysActive(root.draft); currentIndex: root.behaviorMode(root.draft); onActivated: root.setBehaviorMode(currentIndex) }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.mutedColor; font.pixelSize: 10; text: root.isAlwaysActive(root.draft) ? "All-the-time rules always stay active." : (root.behaviorMode(root.draft) === 0 ? "Effects apply only while the WHEN section is true." : root.behaviorMode(root.draft) === 1 ? "Each trigger switches this automation on or off." : "Each trigger activates this automation for a short time.") }
                    }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: effectsContent.implicitHeight + 28
                ColumnLayout {
                    id: effectsContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 12
                    ColumnLayout { Layout.fillWidth: true; spacing: 2
                        Text { text: "WHAT SHOULD IT DO?"; color: root.accentColor; font.pixelSize: 15; font.bold: true; font.family: root.displayFont }
                        Text { text: "Choose the controller behavior to apply whenever this automation matches."; color: root.mutedColor; font.pixelSize: 10 }
                    }
                    Text { visible: (root.draft.actions || []).length === 0; text: "Nothing selected yet."; color: root.warningColor; font.pixelSize: 11 }
                    Repeater {
                        model: root.draft.actions || []
                        delegate: ThemedPanel {
                            id: actionCard
                            required property int index
                            required property var modelData
                            property int actionIndex: index
                            Layout.fillWidth: true; implicitHeight: effectContent.implicitHeight + 24; surfaceColor: root.raisedFill
                            ColumnLayout {
                                id: effectContent
                                anchors.fill: parent; anchors.margins: 12; spacing: 9
                                RowLayout { Layout.fillWidth: true
                                    Text { text: (actionCard.actionIndex + 1) + " · " + root.effectSummary(modelData); color: root.textColor; font.pixelSize: 11; font.bold: true; font.family: root.displayFont; Layout.fillWidth: true; elide: Text.ElideRight }
                                    ActionButton { label: "REMOVE"; subdued: true; onTriggered: root.removeAction(actionCard.actionIndex) }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    EditorCombo { Layout.preferredWidth: 220; model: root.effectTypes; currentIndex: Number(modelData.type) + 1; onActivated: function(choiceIndex) { root.setEffectType(actionCard.actionIndex, choiceIndex) } }
                                    RowLayout { visible: Number(modelData.type) === 0 || Number(modelData.type) === 1; Layout.fillWidth: true
                                        Text { text: "Virtual button"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorSpin { Layout.preferredWidth: 92; from: 1; to: 128; value: modelData.virtualButton; onValueModified: root.updateAction(actionCard.actionIndex, "virtualButton", value) }
                                    }
                                    RowLayout { visible: Number(modelData.type) === 2 || Number(modelData.type) === 3; Layout.fillWidth: true
                                        Text { text: "Profile"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 220; model: root.profileChoicesWithPlaceholder(); textRole: "label"; currentIndex: root.profileChoiceIndex(modelData.profileId); onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "profileId", root.profileChoiceId(choiceIndex)) } }
                                    }
                                    RowLayout { visible: Number(modelData.type) === 4 || Number(modelData.type) === 5 || Number(modelData.type) === 7; Layout.fillWidth: true
                                        Text { text: "Target axis"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 148; model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "targetAxis", choiceIndex) } }
                                        Text { text: Number(modelData.type) === 5 ? "by" : "to"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorTextField { Layout.preferredWidth: 94; text: root.percent(modelData.value); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateAction(actionCard.actionIndex, "value", Number(text.replace("%", "")) / 100) } }
                                    }
                                    RowLayout { visible: Number(modelData.type) === 6; Layout.fillWidth: true
                                        Text { text: "Target axis"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 148; model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "targetAxis", choiceIndex) } }
                                        Text { text: "minimum"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorTextField { Layout.preferredWidth: 94; text: root.percent(modelData.minimum); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateAction(actionCard.actionIndex, "minimum", Number(text.replace("%", "")) / 100) } }
                                        Text { text: "maximum"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorTextField { Layout.preferredWidth: 94; text: root.percent(modelData.maximum); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateAction(actionCard.actionIndex, "maximum", Number(text.replace("%", "")) / 100) } }
                                    }
                                    RowLayout { visible: Number(modelData.type) === 8; Layout.fillWidth: true
                                        Text { text: "Source"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 132; model: root.axisChoices; currentIndex: modelData.sourceAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "sourceAxis", choiceIndex) } }
                                        EditorCombo { Layout.preferredWidth: 158; model: root.sourceStages; currentIndex: modelData.sourceStage; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "sourceStage", choiceIndex) } }
                                        Text { text: "Target"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 132; model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "targetAxis", choiceIndex) } }
                                        EditorTextField { Layout.preferredWidth: 94; text: root.percent(modelData.value); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateAction(actionCard.actionIndex, "value", Number(text.replace("%", "")) / 100) } }
                                    }
                                    RowLayout { visible: Number(modelData.type) === 9; Layout.fillWidth: true
                                        Text { text: "Target"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 132; model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "targetAxis", choiceIndex) } }
                                        Text { text: "follows"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 132; model: root.axisChoices; currentIndex: modelData.sourceAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "sourceAxis", choiceIndex) } }
                                        EditorCombo { Layout.preferredWidth: 158; model: root.sourceStages; currentIndex: modelData.sourceStage; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "sourceStage", choiceIndex) } }
                                        EditorTextField { Layout.preferredWidth: 86; text: root.percent(modelData.value); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateAction(actionCard.actionIndex, "value", Number(text.replace("%", "")) / 100) } }
                                        EditorTextField { Layout.preferredWidth: 86; text: root.percent(modelData.offset); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: function() { root.updateAction(actionCard.actionIndex, "offset", Number(text.replace("%", "")) / 100) } }
                                    }
                                    RowLayout { visible: Number(modelData.type) === 14 || Number(modelData.type) === 15; Layout.fillWidth: true
                                        Text { text: "Target axis"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 190; model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "targetAxis", choiceIndex) } }
                                    }
                                    RowLayout { visible: Number(modelData.type) === 16; Layout.fillWidth: true
                                        Text { text: "Target axis"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 150; model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "targetAxis", choiceIndex) } }
                                        Text { text: "Preset"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorCombo { Layout.preferredWidth: 240; model: root.adaptivePresetChoices(); textRole: "label"; currentIndex: root.adaptivePresetIndex(modelData.adaptiveResponsePresetId); onActivated: function(choiceIndex) { root.updateAction(actionCard.actionIndex, "adaptiveResponsePresetId", root.adaptivePresetId(choiceIndex)) } }
                                    }
                                    RowLayout { visible: root.isTap(modelData); Layout.fillWidth: true
                                        Text { text: "Virtual button"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorSpin { Layout.preferredWidth: 92; from: 1; to: 128; value: modelData.virtualButton; onValueModified: root.updateAction(actionCard.actionIndex, "virtualButton", value) }
                                        Text { text: "Tap duration (ms)"; color: root.mutedColor; font.pixelSize: 10 }
                                        EditorSpin { Layout.preferredWidth: 92; from: 20; to: 500; value: modelData.tapDurationMs; onValueModified: root.updateAction(actionCard.actionIndex, "tapDurationMs", value) }
                                    }
                                }
                            }
                        }
                    }
                    ActionButton { label: "+ ADD EFFECT"; subdued: true; commandEnabled: (root.draft.actions || []).length < 4; onTriggered: root.addEffect() }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: summaryContent.implicitHeight + 40; surfaceColor: root.topGun ? "#d80e1e1c" : root.panelFill
                ColumnLayout {
                    id: summaryContent
                    anchors.fill: parent; anchors.margins: 20; spacing: 12
                    Text { text: "RULE SUMMARY"; color: root.accentColor; font.pixelSize: 14; font.bold: true; font.family: root.displayFont }
                    Text { text: root.ruleSummary(); color: root.textColor; font.pixelSize: 16; font.weight: Font.DemiBold; font.family: root.displayFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: advancedContent.implicitHeight + 28
                ColumnLayout {
                    id: advancedContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: "ADVANCED"; color: root.accentColor; font.pixelSize: 12; font.bold: true; font.family: root.displayFont }
                            Text { text: "Timing and conflict handling."; color: root.mutedColor; font.pixelSize: 10 }
                        }
                        ActionButton { label: root.advancedOpen ? "HIDE" : "SHOW"; subdued: true; onTriggered: root.advancedOpen = !root.advancedOpen }
                    }
                    ColumnLayout { visible: root.advancedOpen; Layout.fillWidth: true; spacing: 8
                        RowLayout { Layout.fillWidth: true
                            Text { text: "Priority"; color: root.textColor; font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 110 }
                            EditorSpin { from: 0; to: 100; value: root.draft.priority === undefined ? 50 : root.draft.priority; onValueModified: { const next = root.clone(root.draft); next.priority = value; root.setDraft(next) } }
                            Text { text: "When automations force the same axis, the higher priority wins. Equal priorities use saved order."; color: root.faintColor; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        RowLayout { visible: root.behaviorMode(root.draft) === 2; Layout.fillWidth: true
                            Text { text: "Active duration"; color: root.textColor; font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 170 }
                            EditorSpin { from: 20; to: 5000; value: root.draft.activeDurationMs === undefined ? 250 : root.draft.activeDurationMs; onValueModified: { const next = root.clone(root.draft); next.activeDurationMs = value; root.setDraft(next) } }
                            Text { text: "ms · Each trigger restarts this short active window."; color: root.faintColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Repeater {
                            model: root.draft.conditions || []
                            delegate: RowLayout {
                                id: multiPressConditionRow
                                required property int index
                                required property var modelData
                                property int conditionIndex: index
                                visible: root.isMultiPress(modelData); Layout.fillWidth: true
                                Text { text: "Maximum time between presses"; color: root.textColor; font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 170 }
                                EditorSpin { from: 150; to: 1000; value: modelData.multiPressWindowMs; onValueModified: root.updateCondition(multiPressConditionRow.conditionIndex, "multiPressWindowMs", value) }
                                Text { text: "ms · Button " + modelData.button + " sequence gap."; color: root.faintColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                        Repeater {
                            model: root.draft.conditions || []
                            delegate: RowLayout {
                                id: longPressConditionRow
                                required property int index
                                required property var modelData
                                property int conditionIndex: index
                                visible: root.isLongPress(modelData); Layout.fillWidth: true
                                Text { text: "Long-press duration"; color: root.textColor; font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 170 }
                                EditorSpin { from: 200; to: 3000; value: modelData.longPressDurationMs; onValueModified: root.updateCondition(longPressConditionRow.conditionIndex, "longPressDurationMs", value) }
                                Text { text: "ms · Button " + modelData.button + " must stay held continuously."; color: root.faintColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                        Repeater {
                            model: root.draft.actions || []
                            delegate: RowLayout {
                                id: tapActionRow
                                required property int index
                                required property var modelData
                                property int actionIndex: index
                                visible: root.isTap(modelData); Layout.fillWidth: true
                                Text { text: "Virtual button tap duration"; color: root.textColor; font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 170 }
                                EditorSpin { from: 20; to: 500; value: modelData.tapDurationMs; onValueModified: root.updateAction(tapActionRow.actionIndex, "tapDurationMs", value) }
                                Text { text: "ms · Virtual button " + modelData.virtualButton + "."; color: root.faintColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: saveContent.implicitHeight + 28; surfaceColor: root.topGun ? "#d80e1e1c" : root.panelFill
                ColumnLayout {
                    id: saveContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    Text { visible: String(root.draft.name || "").trim().length === 0; text: "Give this automation a name."; color: root.warningColor; font.pixelSize: 10; Layout.fillWidth: true }
                    Text { visible: !root.isAlwaysActive(root.draft) && (root.draft.conditions || []).length === 0; text: "Choose when this automation should run, or select All the time."; color: root.warningColor; font.pixelSize: 10; Layout.fillWidth: true }
                    Text { visible: !root.isAlwaysActive(root.draft) && (root.draft.conditions || []).some(function(requirement) { return !root.requirementReady(requirement) }); text: "Finish each requirement before saving."; color: root.warningColor; font.pixelSize: 10; Layout.fillWidth: true }
                    Text { visible: (root.draft.actions || []).length === 0; text: "Add at least one effect."; color: root.warningColor; font.pixelSize: 10; Layout.fillWidth: true }
                    Text { visible: (root.draft.actions || []).some(function(effect) { return !root.effectReady(effect) }); text: "Choose what each effect should do."; color: root.warningColor; font.pixelSize: 10; Layout.fillWidth: true }
                    Text { visible: backendObject.automationValidationMessage.length > 0; text: backendObject.automationValidationMessage; color: root.dangerColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout { Layout.fillWidth: true
                        ActionButton { label: "DELETE AUTOMATION"; destructive: true; onTriggered: { deleteDialog.ruleId = root.editingId; deleteDialog.fromEditor = true; deleteDialog.open() } }
                        Text { text: root.draftDirty ? "UNSAVED CHANGES" : "SAVED DRAFT"; color: root.draftDirty ? root.warningColor : root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: root.telemetryFont; Layout.leftMargin: 8 }
                        Item { Layout.fillWidth: true }
                        ActionButton { label: "CANCEL"; subdued: true; onTriggered: root.returnToOverview() }
                        ActionButton { label: "SAVE AUTOMATION"; commandEnabled: root.canSaveDraft(); onTriggered: root.saveDraft() }
                    }
                }
            }
        }
    }
    Dialog {
        id: discardDialog
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: "Discard changes?"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 350; spacing: 14
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "This Automation has unsaved editor changes. The active compiled rule will remain unchanged."; color: root.textColor; font.pixelSize: 12 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ActionButton { label: "KEEP EDITING"; subdued: true; onTriggered: discardDialog.close() }
                ActionButton { label: "DISCARD"; destructive: true; onTriggered: { discardDialog.close(); root.closeEditor() } }
            }
        }
        background: Rectangle { color: root.raisedFill; border.color: root.strongBorder; radius: root.topGun ? 1 : 5 }
    }
    Dialog {
        id: deleteDialog
        property string ruleId: ""
        property bool fromEditor: false
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: "Delete Automation?"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 350; spacing: 14
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Delete this Automation rule? This removes its saved configuration and compiled runtime entry."; color: root.textColor; font.pixelSize: 12 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ActionButton { label: "CANCEL"; subdued: true; onTriggered: deleteDialog.close() }
                ActionButton { label: "DELETE AUTOMATION"; destructive: true; onTriggered: { const deleted = backendObject.deleteAutomation(deleteDialog.ruleId); deleteDialog.close(); if (deleted && deleteDialog.fromEditor) root.closeEditor() } }
            }
        }
        background: Rectangle { color: root.raisedFill; border.color: root.dangerColor; radius: root.topGun ? 1 : 5 }
    }
}
