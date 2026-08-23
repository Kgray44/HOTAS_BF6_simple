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
    readonly property var conditionTypes: ["Always", "Axis Above", "Axis Below", "Axis Between", "Axis Outside Range", "Button Held", "Button Released", "POV Active", "POV Inactive", "Base Profile Is", "Effective Profile Is"]
    readonly property var actionTypes: ["vJoy Button · Hold", "vJoy Button · Toggle", "Profile · Hold", "Profile · Toggle", "Axis Scale", "Axis Offset", "Axis Clamp", "Axis Override", "Axis Mix", "Axis Follow"]
    readonly property var stages: ["Physical", "Processed"]
    readonly property var directions: ["Up", "Up-Right", "Right", "Down-Right", "Down", "Down-Left", "Left", "Up-Left"]

    function clone(value) { return JSON.parse(JSON.stringify(value)) }
    function setDraft(value) { draft = clone(value); draftDirty = true }
    function ruleForId(id) {
        for (let index = 0; index < rules.length; ++index) if (rules[index].id === id) return rules[index]
        return null
    }
    function openRule(rule) {
        if (!rule) return
        editingId = rule.id
        draft = clone(rule)
        draftDirty = false
        editing = true
    }
    function openRuleById(id) { openRule(ruleForId(id)) }
    function closeEditor() { editing = false; draftDirty = false; editingId = ""; draft = ({}) }
    function returnToOverview() { if (draftDirty) discardDialog.open(); else closeEditor() }
    function newAutomation() { const id = backendObject.createAutomation(); if (id && id.length) openRuleById(id) }
    function duplicateAutomation(id) { const copyId = backendObject.duplicateAutomation(id); if (copyId && copyId.length) openRuleById(copyId) }
    function saveDraft() { if (backendObject.saveAutomation(draft)) closeEditor() }
    function updateCondition(index, key, value) { const next = clone(draft); next.conditions[index][key] = value; setDraft(next) }
    function updateAction(index, key, value) { const next = clone(draft); next.actions[index][key] = value; setDraft(next) }
    function addCondition() { const next = clone(draft); next.conditions.push({ type: 0, axis: 0, minimum: 0, maximum: 0, hysteresis: 0, button: 1, povHat: 1, povDirection: 1, profileId: "" }); setDraft(next) }
    function addAction() { const next = clone(draft); next.actions.push({ type: 0, virtualButton: 1, profileId: "", targetAxis: 0, sourceAxis: 0, sourceStage: 1, value: 0, offset: 0, minimum: -1, maximum: 1 }); setDraft(next) }
    function removeCondition(index) { const next = clone(draft); next.conditions.splice(index, 1); setDraft(next) }
    function removeAction(index) { const next = clone(draft); next.actions.splice(index, 1); setDraft(next) }
    function percent(value) { return (Number(value) * 100).toFixed(1) + "%" }
    function conditionPercent(value, axis) { const displayed = axis === 2 ? (Number(value) + 1) * 50 : Number(value) * 100; return displayed.toFixed(1) + "%" }
    function conditionValue(text, axis) { const displayed = Number(String(text).replace("%", "")); return axis === 2 ? displayed / 50 - 1 : displayed / 100 }
    function profileIndex(id) { const choices = backendObject.profileTriggerChoices; for (let index = 0; index < choices.length; ++index) if (choices[index].id === id) return index; return 0 }
    function profileId(index) { const choices = backendObject.profileTriggerChoices; return index >= 0 && index < choices.length ? choices[index].id : "" }
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
        LegacyAviationPanel { anchors.fill: parent; visible: root.legacy; color: panel.surfaceColor; border.color: panel.edgeColor }
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
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: "Automation rules modify HOTAS behavior from supported buttons, axes, POV states, and profiles. New rules begin disabled until you save a valid configuration."; color: root.mutedColor; font.pixelSize: 11 }
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
                                Text { text: "PRIORITY " + modelData.priority; color: root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: root.telemetryFont }
                                Text { text: modelData.matchMode === 0 ? "MATCH ALL" : "MATCH ANY"; color: root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: root.telemetryFont }
                                Item { Layout.fillWidth: true }
                                Text { text: "OPEN ›"; color: root.accentColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                            }
                            Text { text: "WHEN"; color: root.faintColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                            Text { text: modelData.conditions.length ? modelData.conditionSummary : "No conditions configured"; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { text: "THEN"; color: root.faintColor; font.pixelSize: 9; font.bold: true; font.family: root.displayFont }
                            Text { text: modelData.actions.length ? modelData.actionSummary : "No actions configured"; color: modelData.actions.length ? root.accentColor : root.warningColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                    Text { text: root.topGun ? "AUTOMATION  /  RULE EDITOR" : "Automation / Edit"; color: root.textColor; font.pixelSize: root.topGun ? 32 : 24; font.bold: true; font.family: root.displayFont; style: root.topGun ? Text.Outline : Text.Normal; styleColor: "#8e3321" }
                    Text { text: "Draft configuration only · active runtime rules change after SAVE AUTOMATION"; color: root.mutedColor; font.pixelSize: 11; font.family: root.displayFont }
                }
                Text { text: root.draft.enabled ? "ARMED ON SAVE" : "DISABLED DRAFT"; color: root.draft.enabled ? root.readyColor : root.warningColor; font.pixelSize: 10; font.bold: true; font.family: root.displayFont }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: generalContent.implicitHeight + 28
                ColumnLayout {
                    id: generalContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 11
                    RowLayout { Layout.fillWidth: true
                        Text { text: "GENERAL"; color: root.accentColor; font.pixelSize: 12; font.bold: true; font.family: root.displayFont }
                        Item { Layout.fillWidth: true }
                        ActionButton { label: "DELETE AUTOMATION"; destructive: true; onTriggered: { deleteDialog.ruleId = root.editingId; deleteDialog.fromEditor = true; deleteDialog.open() } }
                    }
                    GridLayout {
                        Layout.fillWidth: true; columns: width >= 900 ? 4 : (width >= 590 ? 2 : 1); columnSpacing: 12; rowSpacing: 10
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "AUTOMATION NAME"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                            EditorTextField { Layout.fillWidth: true; text: root.draft.name || ""; placeholderText: "New Automation"; onTextEdited: { const next = root.clone(root.draft); next.name = text; root.setDraft(next) } }
                        }
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "MATCH MODE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                            EditorCombo { Layout.fillWidth: true; model: ["ALL conditions", "ANY condition"]; currentIndex: root.draft.matchMode || 0; onActivated: { const next = root.clone(root.draft); next.matchMode = currentIndex; root.setDraft(next) } }
                        }
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "PRIORITY"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                            RowLayout { Layout.fillWidth: true
                                EditorSpin { from: 0; to: 100; value: root.draft.priority === undefined ? 50 : root.draft.priority; onValueModified: { const next = root.clone(root.draft); next.priority = value; root.setDraft(next) } }
                                Text { Layout.fillWidth: true; text: "Higher values win competing axis overrides."; color: root.faintColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                            }
                        }
                        ColumnLayout { Layout.fillWidth: true
                            Text { text: "RUNTIME STATE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                            EditorSwitch { text: root.draft.enabled ? "ENABLED" : "DISABLED"; checked: root.draft.enabled || false; onToggled: { const next = root.clone(root.draft); next.enabled = checked; root.setDraft(next) } }
                        }
                    }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: whenContent.implicitHeight + 28
                ColumnLayout {
                    id: whenContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: "WHEN"; color: root.accentColor; font.pixelSize: 15; font.bold: true; font.family: root.displayFont }
                            Text { text: "Conditions are evaluated together once per physical report."; color: root.mutedColor; font.pixelSize: 10 }
                        }
                        Text { text: "MATCH: " + (root.draft.matchMode === 1 ? "ANY" : "ALL"); color: root.textColor; font.pixelSize: 10; font.bold: true; font.family: root.telemetryFont }
                    }
                    Text { visible: (root.draft.conditions || []).length === 0; text: "Add at least one condition before saving this automation."; color: root.warningColor; font.pixelSize: 11 }
                    Repeater {
                        model: root.draft.conditions || []
                        delegate: ThemedPanel {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true; implicitHeight: conditionContent.implicitHeight + 24; surfaceColor: root.raisedFill
                            ColumnLayout {
                                id: conditionContent
                                anchors.fill: parent; anchors.margins: 12; spacing: 9
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "CONDITION " + (index + 1); color: root.textColor; font.pixelSize: 11; font.bold: true; font.family: root.displayFont }
                                    Item { Layout.fillWidth: true }
                                    ActionButton { label: "REMOVE"; subdued: true; onTriggered: root.removeCondition(index) }
                                }
                                GridLayout {
                                    Layout.fillWidth: true; columns: width >= 820 ? 4 : (width >= 500 ? 2 : 1); columnSpacing: 10; rowSpacing: 9
                                    ColumnLayout { Layout.fillWidth: true
                                        Text { text: "TYPE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: root.conditionTypes; currentIndex: modelData.type; onActivated: root.updateCondition(index, "type", currentIndex) }
                                    }
                                    ColumnLayout { visible: modelData.type >= 1 && modelData.type <= 4; Layout.fillWidth: true
                                        Text { text: "SOURCE AXIS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: root.axisChoices; currentIndex: modelData.axis; onActivated: root.updateCondition(index, "axis", currentIndex) }
                                    }
                                    ColumnLayout { visible: modelData.type >= 1 && modelData.type <= 4; Layout.fillWidth: true
                                        Text { text: modelData.type === 2 ? "MAXIMUM" : "THRESHOLD / MINIMUM"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorTextField { Layout.fillWidth: true; text: root.conditionPercent(modelData.minimum, modelData.axis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.updateCondition(index, "minimum", root.conditionValue(text, modelData.axis)) }
                                    }
                                    ColumnLayout { visible: modelData.type === 3 || modelData.type === 4; Layout.fillWidth: true
                                        Text { text: "MAXIMUM"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorTextField { Layout.fillWidth: true; text: root.conditionPercent(modelData.maximum, modelData.axis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.updateCondition(index, "maximum", root.conditionValue(text, modelData.axis)) }
                                    }
                                    ColumnLayout { visible: modelData.type >= 1 && modelData.type <= 4; Layout.fillWidth: true
                                        Text { text: "HYSTERESIS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorTextField { Layout.fillWidth: true; text: root.percent(modelData.hysteresis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.updateCondition(index, "hysteresis", Number(text.replace("%", "")) / 100) }
                                    }
                                    ColumnLayout { visible: modelData.type === 5 || modelData.type === 6; Layout.fillWidth: true
                                        Text { text: "PHYSICAL BUTTON"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorSpin { from: 1; to: 128; value: modelData.button; onValueModified: root.updateCondition(index, "button", value) }
                                    }
                                    ColumnLayout { visible: modelData.type === 7 || modelData.type === 8; Layout.fillWidth: true
                                        Text { text: "POV HAT"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorSpin { from: 1; to: 4; value: modelData.povHat; onValueModified: root.updateCondition(index, "povHat", value) }
                                    }
                                    ColumnLayout { visible: modelData.type === 7 || modelData.type === 8; Layout.fillWidth: true
                                        Text { text: "DIRECTION"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: root.directions; currentIndex: modelData.povDirection - 1; onActivated: root.updateCondition(index, "povDirection", currentIndex + 1) }
                                    }
                                    ColumnLayout { visible: modelData.type === 9 || modelData.type === 10; Layout.fillWidth: true
                                        Text { text: "PROFILE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: backendObject.profileTriggerChoices; textRole: "label"; currentIndex: root.profileIndex(modelData.profileId); onActivated: root.updateCondition(index, "profileId", root.profileId(currentIndex)) }
                                    }
                                }
                                Text { visible: modelData.type === 0; text: "This condition always matches. Add another condition if the rule should respond to a control state."; color: root.faintColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                        }
                    }
                    ActionButton { label: "+ ADD CONDITION"; subdued: true; commandEnabled: (root.draft.conditions || []).length < 4; onTriggered: root.addCondition() }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: thenContent.implicitHeight + 28
                ColumnLayout {
                    id: thenContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    ColumnLayout { Layout.fillWidth: true; spacing: 2
                        Text { text: "THEN"; color: root.accentColor; font.pixelSize: 15; font.bold: true; font.family: root.displayFont }
                        Text { text: "Actions apply only after the rule is saved, compiled, and enabled."; color: root.mutedColor; font.pixelSize: 10 }
                    }
                    Text { visible: (root.draft.actions || []).length === 0; text: "Add at least one action before saving this automation."; color: root.warningColor; font.pixelSize: 11 }
                    Repeater {
                        model: root.draft.actions || []
                        delegate: ThemedPanel {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true; implicitHeight: actionContent.implicitHeight + 24; surfaceColor: root.raisedFill
                            ColumnLayout {
                                id: actionContent
                                anchors.fill: parent; anchors.margins: 12; spacing: 9
                                RowLayout { Layout.fillWidth: true
                                    Text { text: "ACTION " + (index + 1); color: root.textColor; font.pixelSize: 11; font.bold: true; font.family: root.displayFont }
                                    Item { Layout.fillWidth: true }
                                    ActionButton { label: "REMOVE"; subdued: true; onTriggered: root.removeAction(index) }
                                }
                                GridLayout {
                                    Layout.fillWidth: true; columns: width >= 820 ? 4 : (width >= 500 ? 2 : 1); columnSpacing: 10; rowSpacing: 9
                                    ColumnLayout { Layout.fillWidth: true
                                        Text { text: "TYPE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: root.actionTypes; currentIndex: modelData.type; onActivated: root.updateAction(index, "type", currentIndex) }
                                    }
                                    ColumnLayout { visible: modelData.type === 0 || modelData.type === 1; Layout.fillWidth: true
                                        Text { text: "VJOY BUTTON"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorSpin { from: 1; to: 128; value: modelData.virtualButton; onValueModified: root.updateAction(index, "virtualButton", value) }
                                    }
                                    ColumnLayout { visible: modelData.type === 2 || modelData.type === 3; Layout.fillWidth: true
                                        Text { text: "TARGET PROFILE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: backendObject.profileTriggerChoices; textRole: "label"; currentIndex: root.profileIndex(modelData.profileId); onActivated: root.updateAction(index, "profileId", root.profileId(currentIndex)) }
                                    }
                                    ColumnLayout { visible: modelData.type >= 4; Layout.fillWidth: true
                                        Text { text: "TARGET AXIS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: root.updateAction(index, "targetAxis", currentIndex) }
                                    }
                                    ColumnLayout { visible: modelData.type === 8 || modelData.type === 9; Layout.fillWidth: true
                                        Text { text: "SOURCE AXIS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: root.axisChoices; currentIndex: modelData.sourceAxis; onActivated: root.updateAction(index, "sourceAxis", currentIndex) }
                                    }
                                    ColumnLayout { visible: modelData.type === 8 || modelData.type === 9; Layout.fillWidth: true
                                        Text { text: "SOURCE STAGE"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorCombo { Layout.fillWidth: true; model: root.stages; currentIndex: modelData.sourceStage; onActivated: root.updateAction(index, "sourceStage", currentIndex) }
                                    }
                                    ColumnLayout { visible: modelData.type >= 4; Layout.fillWidth: true
                                        Text { text: modelData.type === 6 ? "MINIMUM" : (modelData.type === 9 ? "FOLLOW GAIN" : "VALUE"); color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorTextField { Layout.fillWidth: true; text: root.percent(modelData.type === 6 ? modelData.minimum : modelData.value); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.updateAction(index, modelData.type === 6 ? "minimum" : "value", Number(text.replace("%", "")) / 100) }
                                    }
                                    ColumnLayout { visible: modelData.type === 6; Layout.fillWidth: true
                                        Text { text: "MAXIMUM"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorTextField { Layout.fillWidth: true; text: root.percent(modelData.maximum); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.updateAction(index, "maximum", Number(text.replace("%", "")) / 100) }
                                    }
                                    ColumnLayout { visible: modelData.type === 9; Layout.fillWidth: true
                                        Text { text: "OFFSET"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                                        EditorTextField { Layout.fillWidth: true; text: root.percent(modelData.offset); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: root.updateAction(index, "offset", Number(text.replace("%", "")) / 100) }
                                    }
                                }
                            }
                        }
                    }
                    ActionButton { label: "+ ADD ACTION"; subdued: true; commandEnabled: (root.draft.actions || []).length < 4; onTriggered: root.addAction() }
                }
            }
            ThemedPanel {
                width: parent.width; implicitHeight: saveContent.implicitHeight + 28; surfaceColor: root.topGun ? "#d80e1e1c" : root.panelFill
                ColumnLayout {
                    id: saveContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    Text { visible: backendObject.automationValidationMessage.length > 0; text: backendObject.automationValidationMessage; color: root.dangerColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { visible: (root.draft.conditions || []).length === 0 || (root.draft.actions || []).length === 0; text: "This disabled draft is safe to keep, but it needs at least one WHEN condition and one THEN action before it can be saved as a runtime rule."; color: root.warningColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    RowLayout { Layout.fillWidth: true
                        Text { text: root.draftDirty ? "UNSAVED CHANGES" : "SAVED DRAFT"; color: root.draftDirty ? root.warningColor : root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: root.telemetryFont }
                        Item { Layout.fillWidth: true }
                        ActionButton { label: "CANCEL"; subdued: true; onTriggered: root.returnToOverview() }
                        ActionButton { label: "SAVE AUTOMATION"; onTriggered: root.saveDraft() }
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
