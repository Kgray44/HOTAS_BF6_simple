import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5
import HOTASMapper 1.0

// Native Flight Deck Automation presentation. It reads AppBackend's existing
// low-frequency rule projection and only saves through the established rule
// commands. Rendering, browsing, and deep links never enter AutomationRuntime.
Flickable {
    id: root
    objectName: "flightDeckAutomation"

    property var automationPresentationOverride: null
    property var profilePresentationOverride: null
    property var buttonPresentationOverride: null
    property var presentationState: ({})
    property bool editing: false
    property string editingId: ""
    property var draft: ({})
    property bool draftDirty: false
    property string filterMode: "all"
    property string searchText: ""

    signal navigateToProfile(string profileId)
    signal navigateToButton(int buttonIndex)
    signal presentationStateCaptured(var state)

    readonly property var rules: automationPresentationOverride !== null ? automationPresentationOverride : backend.automationRules
    readonly property var profiles: profilePresentationOverride !== null ? profilePresentationOverride : backend.profiles
    readonly property var buttonItems: buttonPresentationOverride !== null ? buttonPresentationOverride : backend.buttons
    readonly property bool usingPresentationFixture: automationPresentationOverride !== null || profilePresentationOverride !== null || buttonPresentationOverride !== null
    readonly property var axisLabels: ["Roll", "Pitch", "Throttle", "Rotation X", "Rotation Y", "Yaw", "Additional axis 1", "Additional axis 2"]
    readonly property var directionLabels: ["Up", "Up-Right", "Right", "Down-Right", "Down", "Down-Left", "Left", "Up-Left"]
    readonly property var conditionTypes: ["All the time", "Axis is above", "Axis is below", "Axis is between", "Axis is outside range", "Button is held", "Button is not held", "POV points direction", "POV is not pointing direction", "Selected profile is", "Active profile is", "Button is pressed", "Button is released", "Button is pressed multiple times", "Button is held for a while", "Axis crosses above", "Axis crosses below"]
    readonly property var actionTypes: ["Press and hold virtual button", "Toggle virtual button", "Use profile while active", "Switch to profile", "Change axis sensitivity", "Adjust axis output", "Limit axis output", "Force axis output", "Mix one axis into another", "Make one axis follow another", "Tap virtual button", "Turn mapping on", "Turn mapping off", "Toggle mapping", "Temporarily enable Adaptive Response", "Temporarily disable Adaptive Response", "Apply Adaptive Response preset"]

    contentWidth: width
    contentHeight: automationContent.implicitHeight + deck.space24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    FlightDeckTheme {
        id: deck
    }

    Rectangle {
        parent: root
        anchors.fill: parent
        color: deck.primarySurface
        z: -1
    }

    function copyValue(value) {
        return JSON.parse(JSON.stringify(value || ({})));
    }
    function lower(value) {
        return String(value || "").toLowerCase();
    }
    function ruleById(id) {
        for (let index = 0; index < rules.length; ++index) {
            if (String(rules[index].id || "") === String(id || ""))
                return rules[index];
        }
        return null;
    }
    function profileById(id) {
        for (let index = 0; index < profiles.length; ++index) {
            if (String(profiles[index].id || "") === String(id || ""))
                return profiles[index];
        }
        return null;
    }
    function profilePath(id) {
        const profile = profileById(id);
        if (!profile)
            return "Missing profile";
        const name = String(profile.displayName || profile.name || "Unnamed profile");
        const category = String(profile.categoryName || "");
        return category.length > 0 ? category + " / " + name : name;
    }
    function profileChoices() {
        const result = [];
        for (let index = 0; index < profiles.length; ++index) {
            const profile = profiles[index] || ({});
            result.push({
                id: String(profile.id || ""),
                label: profilePath(profile.id)
            });
        }
        return result;
    }
    function profileChoiceIndex(id) {
        const choices = profileChoices();
        for (let index = 0; index < choices.length; ++index) {
            if (choices[index].id === String(id || ""))
                return index;
        }
        return -1;
    }
    function profileChoiceId(index) {
        const choices = profileChoices();
        return index >= 0 && index < choices.length ? String(choices[index].id || "") : "";
    }
    function buttonChoices() {
        const result = [];
        for (let button = 1; button <= 128; ++button) {
            let label = "Button " + button;
            for (let index = 0; index < buttonItems.length; ++index) {
                const item = buttonItems[index] || ({});
                if (Number(item.index) === button) {
                    label = String(item.label || item.hardwareLabel || label);
                    break;
                }
            }
            result.push({
                value: button,
                label: label
            });
        }
        return result;
    }
    function buttonChoiceIndex(button) {
        return Math.max(0, Math.min(127, Number(button || 1) - 1));
    }
    function adaptivePresetChoices() {
        const source = backend.adaptiveResponsePresets || [];
        const result = [];
        for (let index = 0; index < source.length; ++index) {
            const preset = source[index] || ({});
            result.push({
                id: String(preset.id || ""),
                label: String(preset.name || preset.id || "Unnamed preset")
            });
        }
        return result;
    }
    function adaptivePresetIndex(id) {
        const choices = adaptivePresetChoices();
        for (let index = 0; index < choices.length; ++index) {
            if (choices[index].id === String(id || ""))
                return index;
        }
        return -1;
    }
    function adaptivePresetId(index) {
        const choices = adaptivePresetChoices();
        return index >= 0 && index < choices.length ? String(choices[index].id || "") : "";
    }
    function defaultCondition(type) {
        return {
            type: Number(type),
            axis: 0,
            minimum: 0.0,
            maximum: 0.0,
            hysteresis: 0.0,
            button: 1,
            povHat: 1,
            povDirection: 1,
            profileId: "",
            pressCount: 2,
            multiPressWindowMs: 350,
            longPressDurationMs: 600
        };
    }
    function defaultAction(type) {
        return {
            type: Number(type),
            virtualButton: 1,
            profileId: "",
            adaptiveResponsePresetId: "",
            targetAxis: 0,
            sourceAxis: 0,
            sourceStage: 1,
            value: Number(type) === 4 ? 1.0 : 0.0,
            offset: 0.0,
            minimum: -1.0,
            maximum: 1.0,
            tapDurationMs: 80
        };
    }
    function setDraft(next) {
        draft = copyValue(next);
        draftDirty = true;
    }
    function updateDraft(key, value) {
        const next = copyValue(draft);
        next[key] = value;
        setDraft(next);
    }
    function updateCondition(index, key, value) {
        const next = copyValue(draft);
        if (!next.conditions || !next.conditions[index])
            return;
        next.conditions[index][key] = value;
        setDraft(next);
    }
    function updateAction(index, key, value) {
        const next = copyValue(draft);
        if (!next.actions || !next.actions[index])
            return;
        next.actions[index][key] = value;
        setDraft(next);
    }
    function setConditionType(index, type) {
        const next = copyValue(draft);
        if (Number(type) === 0)
            next.conditions = [defaultCondition(0)];
        else
            next.conditions[index] = defaultCondition(type);
        setDraft(next);
    }
    function setActionType(index, type) {
        const next = copyValue(draft);
        next.actions[index] = defaultAction(type);
        setDraft(next);
    }
    function addCondition(type) {
        const next = copyValue(draft);
        if ((next.conditions || []).length >= 4)
            return;
        if (Number(type) === 0)
            next.conditions = [defaultCondition(0)];
        else
            next.conditions.push(defaultCondition(type === undefined ? 5 : type));
        setDraft(next);
    }
    function removeCondition(index) {
        const next = copyValue(draft);
        next.conditions.splice(index, 1);
        setDraft(next);
    }
    function addAction(type) {
        const next = copyValue(draft);
        if ((next.actions || []).length >= 4)
            return;
        next.actions.push(defaultAction(type === undefined ? 0 : type));
        setDraft(next);
    }
    function removeAction(index) {
        const next = copyValue(draft);
        next.actions.splice(index, 1);
        setDraft(next);
    }
    function isAxisCondition(condition) {
        return [1, 2, 3, 4, 15, 16].indexOf(Number(condition.type)) >= 0;
    }
    function isButtonCondition(condition) {
        return [5, 6, 11, 12, 13, 14].indexOf(Number(condition.type)) >= 0;
    }
    function isPovCondition(condition) {
        return [7, 8].indexOf(Number(condition.type)) >= 0;
    }
    function isProfileCondition(condition) {
        return [9, 10].indexOf(Number(condition.type)) >= 0;
    }
    function usesProfileAction(action) {
        return [2, 3].indexOf(Number(action.type)) >= 0;
    }
    function usesVirtualButton(action) {
        return [0, 1, 10].indexOf(Number(action.type)) >= 0;
    }
    function usesAxisValue(action) {
        return [4, 5, 7].indexOf(Number(action.type)) >= 0;
    }
    function usesAxisRange(action) {
        return Number(action.type) === 6;
    }
    function usesAxisPair(action) {
        return [8, 9].indexOf(Number(action.type)) >= 0;
    }
    function usesAdaptiveAxis(action) {
        return [14, 15, 16].indexOf(Number(action.type)) >= 0;
    }
    function isReadyCondition(condition) {
        return !isProfileCondition(condition) || profileChoiceIndex(condition.profileId) >= 0;
    }
    function isReadyAction(action) {
        if (usesProfileAction(action))
            return profileChoiceIndex(action.profileId) >= 0;
        if (Number(action.type) === 16)
            return adaptivePresetIndex(action.adaptiveResponsePresetId) >= 0;
        return true;
    }
    function canSaveDraft() {
        const conditions = draft.conditions || [];
        const actions = draft.actions || [];
        return String(draft.name || "").trim().length > 0 && String(draft.name || "").trim().length <= 64 && conditions.length >= 1 && conditions.length <= 4 && actions.length >= 1 && actions.length <= 4 && conditions.every(function (condition) {
            return root.isReadyCondition(condition);
        }) && actions.every(function (action) {
            return root.isReadyAction(action);
        });
    }
    function openRuleById(id) {
        const rule = ruleById(id);
        if (!rule)
            return false;
        editingId = String(rule.id || "");
        draft = copyValue(rule);
        draftDirty = false;
        editing = true;
        contentY = 0;
        return true;
    }
    function closeEditor() {
        editing = false;
        editingId = "";
        draft = ({});
        draftDirty = false;
        contentY = 0;
    }
    function createAutomation() {
        if (usingPresentationFixture)
            return "";
        const id = backend.createAutomation();
        if (String(id || "").length > 0)
            openRuleById(id);
        return id;
    }
    function duplicateAutomation(id) {
        if (usingPresentationFixture)
            return "";
        const copyId = backend.duplicateAutomation(String(id || ""));
        if (String(copyId || "").length > 0)
            openRuleById(copyId);
        return copyId;
    }
    function saveDraft() {
        if (usingPresentationFixture || !canSaveDraft())
            return false;
        if (!backend.saveAutomation(draft))
            return false;
        closeEditor();
        return true;
    }
    function ruleState(rule) {
        if (Number(rule.health) === 2)
            return "NEEDS ATTENTION";
        if (!rule.enabled)
            return ((rule.conditions || []).length === 0 || (rule.actions || []).length === 0) ? "INCOMPLETE DRAFT" : "DISABLED";
        return rule.active ? "ACTIVE NOW" : "ENABLED";
    }
    function ruleTone(rule) {
        if (Number(rule.health) === 2)
            return "fault";
        if (!rule.enabled)
            return "attention";
        return rule.active ? "healthy" : "informational";
    }
    function visibleRules() {
        const result = [];
        const needle = lower(searchText).trim();
        for (let index = 0; index < rules.length; ++index) {
            const rule = rules[index] || ({});
            if (filterMode === "enabled" && !rule.enabled)
                continue;
            if (filterMode === "disabled" && rule.enabled)
                continue;
            if (needle.length > 0 && [rule.name, rule.conditionSummary, rule.actionSummary, rule.healthMessage].every(function (value) {
                return lower(value).indexOf(needle) < 0;
            }))
                continue;
            result.push(rule);
        }
        return result;
    }
    function profileReferences(rule) {
        const ids = [];
        const conditions = rule.conditions || [];
        const actions = rule.actions || [];
        for (let index = 0; index < conditions.length; ++index) {
            if (isProfileCondition(conditions[index]) && String(conditions[index].profileId || "").length > 0)
                ids.push(String(conditions[index].profileId));
        }
        for (let index = 0; index < actions.length; ++index) {
            if (usesProfileAction(actions[index]) && String(actions[index].profileId || "").length > 0)
                ids.push(String(actions[index].profileId));
        }
        return ids.filter(function (id, index) {
            return ids.indexOf(id) === index;
        });
    }
    function firstButtonReference(rule) {
        const conditions = rule.conditions || [];
        for (let index = 0; index < conditions.length; ++index) {
            if (isButtonCondition(conditions[index]))
                return Number(conditions[index].button || 0);
        }
        return 0;
    }
    function capturePresentationState() {
        presentationStateCaptured({
            editing: editing,
            editingId: editingId,
            draft: copyValue(draft),
            draftDirty: draftDirty,
            filterMode: filterMode,
            searchText: searchText,
            contentY: contentY
        });
    }
    function restorePresentationState() {
        const state = presentationState || ({});
        filterMode = state.filterMode || "all";
        searchText = state.searchText || "";
        if (state.editing && String(state.editingId || "").length > 0 && ruleById(state.editingId)) {
            editing = true;
            editingId = state.editingId;
            draft = copyValue(state.draft || ruleById(state.editingId));
            draftDirty = Boolean(state.draftDirty);
        }
        contentY = Number(state.contentY || 0);
    }

    Component.onCompleted: restorePresentationState()
    Component.onDestruction: capturePresentationState()

    component SectionLabel: RowLayout {
        property string label: "SECTION"
        Layout.fillWidth: true
        spacing: deck.space8
        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: deck.accent
        }
        Text {
            text: parent.label
            color: deck.textMuted
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: deck.divider
        }
    }

    component DeckButton: Button {
        id: control
        property bool subdued: false
        property bool destructive: false
        implicitHeight: deck.compactControlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: control.text
            color: control.enabled ? (control.destructive ? deck.fault : control.subdued ? deck.textSecondary : deck.primarySurface) : deck.disabled
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: !control.enabled ? deck.secondarySurface : control.down ? (control.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.2) : deck.accentMuted) : control.hovered ? (control.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.15) : control.subdued ? deck.selected : deck.focus) : control.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.08) : control.subdued ? deck.secondarySurface : deck.accent
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : control.destructive ? deck.fault : deck.border
        }
    }

    component DeckField: TextField {
        id: control
        implicitHeight: deck.controlHeight
        selectByMouse: true
        color: deck.textPrimary
        placeholderTextColor: deck.textMuted
        font.family: deck.telemetryFont
        font.pixelSize: 10
        leftPadding: deck.space12
        rightPadding: deck.space12
        background: Rectangle {
            radius: deck.radiusControl
            color: deck.primarySurface
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
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - deck.space12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: deck.textSecondary
            font.pixelSize: 16
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: control.pressed ? deck.selected : deck.primarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
        delegate: ItemDelegate {
            width: ListView.view.width
            height: 34
            highlighted: control.highlightedIndex === index
            objectName: control.objectName + "Choice_" + index
            contentItem: Text {
                text: control.textAt(index)
                color: deck.textPrimary
                font.family: deck.telemetryFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: control.highlightedIndex === index ? deck.selected : deck.elevatedSurface
            }
        }
        popup: Popup {
            objectName: control.objectName + "Popup"
            y: control.height - 1
            width: control.width
            padding: 4
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

    component DeckSpin: SpinBox {
        id: control
        implicitHeight: deck.controlHeight
        editable: true
        font.family: deck.telemetryFont
        font.pixelSize: 10
        contentItem: TextInput {
            z: 2
            text: control.displayText
            color: deck.textPrimary
            font: control.font
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !control.editable
            validator: control.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: deck.primarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
    }

    component DeckSwitch: Switch {
        id: control
        property string statusText: ""
        focusPolicy: Qt.StrongFocus
        indicator: Rectangle {
            implicitWidth: 42
            implicitHeight: 22
            radius: 11
            color: control.checked ? deck.accent : deck.secondarySurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
            Rectangle {
                width: 16
                height: 16
                radius: 8
                anchors.verticalCenter: parent.verticalCenter
                x: control.checked ? parent.width - width - 3 : 3
                color: control.checked ? deck.primarySurface : deck.textMuted
            }
        }
        contentItem: Text {
            text: control.text
            leftPadding: control.indicator.width + deck.space8
            color: deck.textPrimary
            font.family: deck.telemetryFont
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
        }
    }

    ColumnLayout {
        id: automationContent
        x: deck.space24
        width: Math.max(0, root.width - deck.space48)
        spacing: deck.space16

        ColumnLayout {
            visible: !root.editing
            Layout.fillWidth: true
            spacing: deck.space16

            FlightDeckCard {
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: hero.implicitHeight + deck.space32
                color: deck.elevatedSurface
                ColumnLayout {
                    id: hero
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space12
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: "AUTOMATION"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: backend.automationEngineEnabled ? "Automation is enabled" : "Automation is disabled"
                                color: deck.textPrimary
                                font.family: deck.displayFont
                                font.pixelSize: 24
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: Number(rules.length) + " rule" + (rules.length === 1 ? "" : "s") + " configured"
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 10
                            }
                        }
                        DeckSwitch {
                            id: engineToggle
                            objectName: "flightDeckAutomationEngineToggle"
                            text: backend.automationEngineEnabled ? "ENGINE ON" : "ENGINE OFF"
                            checked: backend.automationEngineEnabled
                            enabled: !root.usingPresentationFixture
                            onToggled: backend.setAutomationEngineEnabled(checked)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: deck.space12
                        Rectangle {
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                            radius: 4
                            color: backend.automationEngineEnabled ? deck.healthy : deck.attention
                        }
                        Text {
                            text: backend.automationEngineEnabled ? "Configured rules evaluate through the existing Automation engine." : "Rules stay configured while the existing Automation engine is off."
                            color: deck.textSecondary
                            font.family: deck.telemetryFont
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            visible: backend.automationActiveRuleCount > 0
                            text: backend.automationActiveRuleCount + " ACTIVE NOW"
                            color: deck.healthy
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                SectionLabel {
                    label: "RULES"
                    Layout.fillWidth: true
                }
                DeckButton {
                    objectName: "flightDeckAutomationNewRule"
                    text: "NEW RULE"
                    onClicked: root.createAutomation()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                DeckField {
                    Layout.fillWidth: true
                    placeholderText: "Search rules..."
                    text: root.searchText
                    onTextEdited: root.searchText = text
                }
                Repeater {
                    model: [
                        {
                            label: "ALL",
                            value: "all"
                        },
                        {
                            label: "ENABLED",
                            value: "enabled"
                        },
                        {
                            label: "DISABLED",
                            value: "disabled"
                        }
                    ]
                    delegate: DeckButton {
                        required property var modelData
                        text: modelData.label
                        subdued: root.filterMode !== modelData.value
                        onClicked: root.filterMode = modelData.value
                    }
                }
            }

            FlightDeckCard {
                tokens: deck
                visible: rules.length === 0
                Layout.fillWidth: true
                implicitHeight: emptyContent.implicitHeight + deck.space32
                ColumnLayout {
                    id: emptyContent
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    Text {
                        text: "No automation rules yet"
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 18
                        font.bold: true
                    }
                    Text {
                        text: "Automation can change configured controller behavior when its existing conditions are met."
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    DeckButton {
                        text: "CREATE RULE"
                        onClicked: root.createAutomation()
                    }
                }
            }

            Text {
                visible: rules.length > 0 && visibleRules().length === 0
                text: "No rules match this presentation-only filter."
                color: deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: 10
            }

            Repeater {
                model: root.visibleRules()
                delegate: FlightDeckCard {
                    tokens: deck
                    required property var modelData
                    property var rule: modelData
                    objectName: "flightDeckAutomationRule_" + String(rule.id || "")
                    Layout.fillWidth: true
                    implicitHeight: ruleCard.implicitHeight + deck.space24
                    color: rule.enabled ? deck.elevatedSurface : deck.secondarySurface
                    border.color: Number(rule.health) === 2 ? deck.fault : rule.enabled ? deck.border : deck.attention
                    ColumnLayout {
                        id: ruleCard
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: String(rule.name || "Unnamed rule")
                                    color: deck.textPrimary
                                    font.family: deck.displayFont
                                    font.pixelSize: 17
                                    font.bold: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: "Priority " + Number(rule.priority)
                                    color: deck.textMuted
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 9
                                }
                            }
                            Rectangle {
                                width: 7
                                height: 7
                                radius: 4
                                color: deck.statusColor(root.ruleTone(rule))
                            }
                            Text {
                                text: root.ruleState(rule)
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            DeckButton {
                                objectName: "flightDeckAutomationEdit_" + String(rule.id || "")
                                text: "EDIT"
                                subdued: true
                                onClicked: root.openRuleById(rule.id)
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: deck.divider
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: "WHEN"
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: String(rule.conditionSummary || "No condition configured")
                                color: deck.textPrimary
                                font.family: deck.telemetryFont
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: "DO"
                                color: deck.accent
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: String(rule.actionSummary || "No action configured")
                                color: deck.textPrimary
                                font.family: deck.telemetryFont
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                        Text {
                            visible: Number(rule.health) !== 0 && String(rule.healthMessage || "").length > 0
                            text: String(rule.healthMessage)
                            color: Number(rule.health) === 2 ? deck.fault : deck.attention
                            font.family: deck.telemetryFont
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: deck.space8
                            Repeater {
                                model: root.profileReferences(rule)
                                delegate: DeckButton {
                                    required property string modelData
                                    text: root.profilePath(modelData)
                                    subdued: true
                                    onClicked: root.navigateToProfile(modelData)
                                }
                            }
                            DeckButton {
                                visible: root.firstButtonReference(rule) > 0
                                text: "OPEN CONTROL"
                                subdued: true
                                onClicked: root.navigateToButton(root.firstButtonReference(rule))
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DeckSwitch {
                                objectName: "flightDeckAutomationEnabled_" + String(rule.id || "")
                                text: rule.enabled ? "RULE ENABLED" : "RULE DISABLED"
                                checked: Boolean(rule.enabled)
                                enabled: !root.usingPresentationFixture
                                onToggled: {
                                    if (!backend.setAutomationEnabled(String(rule.id || ""), checked))
                                        checked = Boolean(rule.enabled);
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            DeckButton {
                                objectName: "flightDeckAutomationDuplicate_" + String(rule.id || "")
                                text: "DUPLICATE"
                                subdued: true
                                enabled: !root.usingPresentationFixture
                                onClicked: root.duplicateAutomation(rule.id)
                            }
                            DeckButton {
                                objectName: "flightDeckAutomationDelete_" + String(rule.id || "")
                                text: "DELETE"
                                subdued: true
                                destructive: true
                                enabled: !root.usingPresentationFixture
                                onClicked: {
                                    deleteDialog.ruleId = String(rule.id || "");
                                    deleteDialog.ruleName = String(rule.name || "this rule");
                                    deleteDialog.open();
                                }
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            visible: root.editing
            Layout.fillWidth: true
            spacing: deck.space16

            RowLayout {
                Layout.fillWidth: true
                DeckButton {
                    text: "BACK TO RULES"
                    subdued: true
                    onClicked: root.closeEditor()
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: String(root.draft.name || "New Automation")
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 21
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "Editing changes the saved configuration only. It never runs this rule."
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                    }
                }
                DeckButton {
                    objectName: "flightDeckAutomationSave"
                    text: "SAVE RULE"
                    enabled: root.canSaveDraft() && !root.usingPresentationFixture
                    onClicked: root.saveDraft()
                }
            }

            FlightDeckCard {
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: identity.implicitHeight + deck.space32
                ColumnLayout {
                    id: identity
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space12
                    SectionLabel {
                        label: "RULE"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: "Name"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            DeckField {
                                objectName: "flightDeckAutomationName"
                                Layout.fillWidth: true
                                text: String(root.draft.name || "")
                                placeholderText: "New Automation"
                                onTextEdited: root.updateDraft("name", text)
                            }
                        }
                        DeckSwitch {
                            text: root.draft.enabled ? "RULE ENABLED" : "RULE DISABLED"
                            checked: Boolean(root.draft.enabled)
                            onToggled: root.updateDraft("enabled", checked)
                        }
                    }
                    Text {
                        visible: String(root.draft.name || "").trim().length === 0
                        text: "Give this rule a name before saving."
                        color: deck.attention
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                    }
                }
            }

            FlightDeckCard {
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: whenContent.implicitHeight + deck.space32
                ColumnLayout {
                    id: whenContent
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space12
                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel {
                            label: "WHEN"
                            Layout.fillWidth: true
                        }
                        DeckCombo {
                            objectName: "flightDeckAutomationMatchMode"
                            width: 180
                            model: ["ALL conditions", "ANY condition"]
                            currentIndex: Number(root.draft.matchMode || 0)
                            enabled: !((root.draft.conditions || []).length === 1 && Number(root.draft.conditions[0].type) === 0)
                            onActivated: root.updateDraft("matchMode", currentIndex)
                        }
                    }
                    Text {
                        text: (root.draft.conditions || []).length === 1 && Number(root.draft.conditions[0].type) === 0 ? "This rule is active whenever it is enabled." : (Number(root.draft.matchMode || 0) === 0 ? "Every condition below must be true." : "Any one condition below can trigger this rule.")
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: (root.draft.conditions || []).length
                        delegate: FlightDeckCard {
                            tokens: deck
                            required property int modelData
                            property int conditionIndex: modelData
                            property var condition: (root.draft.conditions || [])[conditionIndex] || ({})
                            Layout.fillWidth: true
                            implicitHeight: conditionContent.implicitHeight + deck.space24
                            color: deck.secondarySurface
                            ColumnLayout {
                                id: conditionContent
                                anchors.fill: parent
                                anchors.margins: deck.space12
                                spacing: deck.space8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "CONDITION " + (index + 1)
                                        color: deck.textMuted
                                        font.family: deck.telemetryFont
                                        font.pixelSize: 9
                                        font.bold: true
                                        Layout.fillWidth: true
                                    }
                                    DeckButton {
                                        text: "REMOVE"
                                        subdued: true
                                        enabled: (root.draft.conditions || []).length > 1
                                        onClicked: root.removeCondition(conditionIndex)
                                    }
                                }
                                DeckCombo {
                                    objectName: "flightDeckAutomationConditionType_" + index
                                    Layout.fillWidth: true
                                    model: root.conditionTypes
                                    currentIndex: Number(condition.type)
                                    onActivated: root.setConditionType(conditionIndex, currentIndex)
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: deck.space8
                                    RowLayout {
                                        visible: root.isAxisCondition(condition)
                                        spacing: deck.space6
                                        Text {
                                            text: "Axis"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            width: 170
                                            model: root.axisLabels
                                            currentIndex: Number(condition.axis || 0)
                                            onActivated: root.updateCondition(conditionIndex, "axis", currentIndex)
                                        }
                                        Text {
                                            text: [3, 4].indexOf(Number(condition.type)) >= 0 ? "From" : "Threshold"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckSpin {
                                            from: -100
                                            to: 100
                                            value: Math.round(Number(condition.minimum || 0) * 100)
                                            onValueModified: root.updateCondition(conditionIndex, "minimum", value / 100.0)
                                        }
                                        Text {
                                            visible: [3, 4].indexOf(Number(condition.type)) >= 0
                                            text: "To"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckSpin {
                                            visible: [3, 4].indexOf(Number(condition.type)) >= 0
                                            from: -100
                                            to: 100
                                            value: Math.round(Number(condition.maximum || 0) * 100)
                                            onValueModified: root.updateCondition(conditionIndex, "maximum", value / 100.0)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.isButtonCondition(condition)
                                        spacing: deck.space6
                                        Text {
                                            text: "Control"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            objectName: "flightDeckAutomationConditionButton_" + conditionIndex
                                            width: 210
                                            model: root.buttonChoices()
                                            textRole: "label"
                                            currentIndex: root.buttonChoiceIndex(condition.button)
                                            onActivated: root.updateCondition(conditionIndex, "button", currentIndex + 1)
                                        }
                                        DeckSpin {
                                            visible: Number(condition.type) === 13
                                            from: 2
                                            to: 5
                                            value: Number(condition.pressCount || 2)
                                            onValueModified: root.updateCondition(conditionIndex, "pressCount", value)
                                        }
                                        DeckSpin {
                                            visible: Number(condition.type) === 13
                                            from: 150
                                            to: 1000
                                            stepSize: 50
                                            value: Number(condition.multiPressWindowMs || 350)
                                            onValueModified: root.updateCondition(conditionIndex, "multiPressWindowMs", value)
                                        }
                                        DeckSpin {
                                            visible: Number(condition.type) === 14
                                            from: 200
                                            to: 3000
                                            stepSize: 50
                                            value: Number(condition.longPressDurationMs || 600)
                                            onValueModified: root.updateCondition(conditionIndex, "longPressDurationMs", value)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.isPovCondition(condition)
                                        spacing: deck.space6
                                        Text {
                                            text: "Hat"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckSpin {
                                            from: 1
                                            to: 4
                                            value: Number(condition.povHat || 1)
                                            onValueModified: root.updateCondition(conditionIndex, "povHat", value)
                                        }
                                        DeckCombo {
                                            width: 150
                                            model: root.directionLabels
                                            currentIndex: Math.max(0, Number(condition.povDirection || 1) - 1)
                                            onActivated: root.updateCondition(conditionIndex, "povDirection", currentIndex + 1)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.isProfileCondition(condition)
                                        spacing: deck.space6
                                        Text {
                                            text: "Profile"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            objectName: "flightDeckAutomationConditionProfile_" + index
                                            width: 250
                                            model: root.profileChoices()
                                            textRole: "label"
                                            currentIndex: Math.max(0, root.profileChoiceIndex(condition.profileId))
                                            onActivated: root.updateCondition(conditionIndex, "profileId", root.profileChoiceId(currentIndex))
                                        }
                                        DeckButton {
                                            visible: String(condition.profileId || "").length > 0
                                            text: "OPEN PROFILE"
                                            subdued: true
                                            onClicked: root.navigateToProfile(String(condition.profileId))
                                        }
                                    }
                                }
                            }
                        }
                    }
                    DeckButton {
                        text: "ADD CONDITION"
                        subdued: true
                        enabled: (root.draft.conditions || []).length < 4 && !((root.draft.conditions || []).length === 1 && Number(root.draft.conditions[0].type) === 0)
                        onClicked: root.addCondition(5)
                    }
                    Text {
                        text: "Game detection is configured in Profiles. This existing Automation engine uses control, axis, POV, and profile conditions; no game trigger is invented here."
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            FlightDeckCard {
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: doContent.implicitHeight + deck.space32
                ColumnLayout {
                    id: doContent
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space12
                    SectionLabel {
                        label: "DO"
                    }
                    Text {
                        text: "Choose the existing controller effect this rule applies while it is active."
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: (root.draft.actions || []).length
                        delegate: FlightDeckCard {
                            tokens: deck
                            required property int modelData
                            property int actionIndex: modelData
                            property var action: (root.draft.actions || [])[actionIndex] || ({})
                            Layout.fillWidth: true
                            implicitHeight: actionContent.implicitHeight + deck.space24
                            color: deck.secondarySurface
                            ColumnLayout {
                                id: actionContent
                                anchors.fill: parent
                                anchors.margins: deck.space12
                                spacing: deck.space8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "ACTION " + (index + 1)
                                        color: deck.textMuted
                                        font.family: deck.telemetryFont
                                        font.pixelSize: 9
                                        font.bold: true
                                        Layout.fillWidth: true
                                    }
                                    DeckButton {
                                        text: "REMOVE"
                                        subdued: true
                                        enabled: (root.draft.actions || []).length > 1
                                        onClicked: root.removeAction(actionIndex)
                                    }
                                }
                                DeckCombo {
                                    objectName: "flightDeckAutomationActionType_" + index
                                    Layout.fillWidth: true
                                    model: root.actionTypes
                                    currentIndex: Number(action.type)
                                    onActivated: root.setActionType(actionIndex, currentIndex)
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: deck.space8
                                    RowLayout {
                                        visible: root.usesVirtualButton(action)
                                        spacing: deck.space6
                                        Text {
                                            text: "Virtual button"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            objectName: "flightDeckAutomationActionButton_" + actionIndex
                                            width: 190
                                            model: root.buttonChoices()
                                            textRole: "label"
                                            currentIndex: root.buttonChoiceIndex(action.virtualButton)
                                            onActivated: root.updateAction(actionIndex, "virtualButton", currentIndex + 1)
                                        }
                                        DeckSpin {
                                            visible: Number(action.type) === 10
                                            from: 20
                                            to: 500
                                            stepSize: 10
                                            value: Number(action.tapDurationMs || 80)
                                            onValueModified: root.updateAction(actionIndex, "tapDurationMs", value)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.usesProfileAction(action)
                                        spacing: deck.space6
                                        Text {
                                            text: "Profile"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            objectName: "flightDeckAutomationActionProfile_" + index
                                            width: 250
                                            model: root.profileChoices()
                                            textRole: "label"
                                            currentIndex: Math.max(0, root.profileChoiceIndex(action.profileId))
                                            onActivated: root.updateAction(actionIndex, "profileId", root.profileChoiceId(currentIndex))
                                        }
                                        DeckButton {
                                            visible: String(action.profileId || "").length > 0
                                            text: "OPEN PROFILE"
                                            subdued: true
                                            onClicked: root.navigateToProfile(String(action.profileId))
                                        }
                                    }
                                    RowLayout {
                                        visible: root.usesAxisValue(action)
                                        spacing: deck.space6
                                        Text {
                                            text: "Target axis"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            width: 150
                                            model: root.axisLabels
                                            currentIndex: Number(action.targetAxis || 0)
                                            onActivated: root.updateAction(actionIndex, "targetAxis", currentIndex)
                                        }
                                        Text {
                                            text: "Percent"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckSpin {
                                            from: -100
                                            to: 100
                                            value: Math.round(Number(action.value || 0) * 100)
                                            onValueModified: root.updateAction(actionIndex, "value", value / 100.0)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.usesAxisRange(action)
                                        spacing: deck.space6
                                        Text {
                                            text: "Target axis"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            width: 150
                                            model: root.axisLabels
                                            currentIndex: Number(action.targetAxis || 0)
                                            onActivated: root.updateAction(actionIndex, "targetAxis", currentIndex)
                                        }
                                        DeckSpin {
                                            from: -100
                                            to: 100
                                            value: Math.round(Number(action.minimum || 0) * 100)
                                            onValueModified: root.updateAction(actionIndex, "minimum", value / 100.0)
                                        }
                                        DeckSpin {
                                            from: -100
                                            to: 100
                                            value: Math.round(Number(action.maximum || 0) * 100)
                                            onValueModified: root.updateAction(actionIndex, "maximum", value / 100.0)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.usesAxisPair(action)
                                        spacing: deck.space6
                                        Text {
                                            text: "Target"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            width: 130
                                            model: root.axisLabels
                                            currentIndex: Number(action.targetAxis || 0)
                                            onActivated: root.updateAction(actionIndex, "targetAxis", currentIndex)
                                        }
                                        Text {
                                            text: "Source"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            width: 130
                                            model: root.axisLabels
                                            currentIndex: Number(action.sourceAxis || 0)
                                            onActivated: root.updateAction(actionIndex, "sourceAxis", currentIndex)
                                        }
                                        DeckCombo {
                                            width: 150
                                            model: ["Controller input", "Mapped output"]
                                            currentIndex: Number(action.sourceStage || 0)
                                            onActivated: root.updateAction(actionIndex, "sourceStage", currentIndex)
                                        }
                                        DeckSpin {
                                            from: -100
                                            to: 100
                                            value: Math.round(Number(action.value || 0) * 100)
                                            onValueModified: root.updateAction(actionIndex, "value", value / 100.0)
                                        }
                                        DeckSpin {
                                            visible: Number(action.type) === 9
                                            from: -100
                                            to: 100
                                            value: Math.round(Number(action.offset || 0) * 100)
                                            onValueModified: root.updateAction(actionIndex, "offset", value / 100.0)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.usesAdaptiveAxis(action)
                                        spacing: deck.space6
                                        Text {
                                            text: "Axis"
                                            color: deck.textSecondary
                                            font.family: deck.telemetryFont
                                            font.pixelSize: 9
                                        }
                                        DeckCombo {
                                            width: 160
                                            model: root.axisLabels
                                            currentIndex: Number(action.targetAxis || 0)
                                            onActivated: root.updateAction(actionIndex, "targetAxis", currentIndex)
                                        }
                                        DeckCombo {
                                            visible: Number(action.type) === 16
                                            width: 230
                                            model: root.adaptivePresetChoices()
                                            textRole: "label"
                                            currentIndex: Math.max(0, root.adaptivePresetIndex(action.adaptiveResponsePresetId))
                                            onActivated: root.updateAction(actionIndex, "adaptiveResponsePresetId", root.adaptivePresetId(currentIndex))
                                        }
                                    }
                                }
                            }
                        }
                    }
                    DeckButton {
                        text: "ADD ACTION"
                        subdued: true
                        enabled: (root.draft.actions || []).length < 4
                        onClicked: root.addAction(0)
                    }
                }
            }

            FlightDeckCard {
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: behaviorContent.implicitHeight + deck.space32
                ColumnLayout {
                    id: behaviorContent
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space12
                    SectionLabel {
                        label: "BEHAVIOR / OPTIONS"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: "Rule behavior"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            DeckCombo {
                                objectName: "flightDeckAutomationBehavior"
                                Layout.fillWidth: true
                                model: ["Apply while conditions are active", "Toggle on trigger", "Run briefly after trigger"]
                                currentIndex: Number(root.draft.activationMode || 0)
                                onActivated: root.updateDraft("activationMode", currentIndex)
                            }
                        }
                        ColumnLayout {
                            visible: Number(root.draft.activationMode) === 2
                            Layout.preferredWidth: 170
                            spacing: 4
                            Text {
                                text: "Duration (ms)"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            DeckSpin {
                                from: 20
                                to: 5000
                                stepSize: 10
                                value: Number(root.draft.activeDurationMs || 250)
                                onValueModified: root.updateDraft("activeDurationMs", value)
                            }
                        }
                        ColumnLayout {
                            Layout.preferredWidth: 150
                            spacing: 4
                            Text {
                                text: "Priority"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            DeckSpin {
                                objectName: "flightDeckAutomationPriority"
                                from: 0
                                to: 100
                                value: Number(root.draft.priority || 50)
                                onValueModified: root.updateDraft("priority", value)
                            }
                        }
                    }
                    Text {
                        text: "Higher priority wins when multiple active rules force the same axis property. Equal priorities use saved rule order."
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            FlightDeckCard {
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: summaryContent.implicitHeight + deck.space32
                color: deck.secondarySurface
                ColumnLayout {
                    id: summaryContent
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    SectionLabel {
                        label: "REVIEW"
                    }
                    Text {
                        text: root.canSaveDraft() ? "This rule will be validated and saved through the existing Automation command path." : "Complete the required WHEN and DO selections to save this rule."
                        color: root.canSaveDraft() ? deck.textSecondary : deck.attention
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: String(backend.automationValidationMessage || "").length > 0
                        text: backend.automationValidationMessage
                        color: deck.fault
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        DeckButton {
                            text: "DELETE RULE"
                            destructive: true
                            subdued: true
                            enabled: !root.usingPresentationFixture
                            onClicked: {
                                deleteDialog.ruleId = root.editingId;
                                deleteDialog.ruleName = String(root.draft.name || "this rule");
                                deleteDialog.open();
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        DeckButton {
                            text: "SAVE RULE"
                            objectName: "flightDeckAutomationSaveBottom"
                            enabled: root.canSaveDraft() && !root.usingPresentationFixture
                            onClicked: root.saveDraft()
                        }
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: deleteDialog
        property string ruleId: ""
        property string ruleName: ""
        tokens: deck
        heading: "Delete “" + ruleName + "”?"
        tone: "fault"
        preferredWidth: 420
        contentItem: ColumnLayout {
            width: 340
            spacing: deck.space12
            Text {
                text: "This removes the automation rule. It does not delete a referenced profile or control."
                color: deck.textSecondary
                font.family: deck.telemetryFont
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                DeckButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: deleteDialog.close()
                }
                DeckButton {
                    objectName: "flightDeckAutomationDeleteConfirm"
                    text: "DELETE RULE"
                    destructive: true
                    onClicked: {
                        if (!root.usingPresentationFixture && backend.deleteAutomation(deleteDialog.ruleId)) {
                            const wasEditing = root.editingId === deleteDialog.ruleId;
                            deleteDialog.close();
                            if (wasEditing)
                                root.closeEditor();
                        }
                    }
                }
            }
        }
    }
}
