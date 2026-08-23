import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Item {
    id: root
    property var backendObject
    property bool topGun: false
    readonly property color background: topGun ? "#11130f" : "#142025"
    readonly property color card: topGun ? "#241b15" : "#1b2a31"
    readonly property color border: topGun ? "#9d6331" : "#4a6873"
    readonly property color text: topGun ? "#f1d6aa" : "#e7f3f4"
    readonly property color muted: topGun ? "#ba9a75" : "#90aab1"
    readonly property color accent: topGun ? "#e88035" : "#66d5e3"
    readonly property color good: "#8ccfa6"
    readonly property color fault: "#e18c80"
    property var rules: backendObject ? backendObject.automationRules : []
    readonly property var axisChoices: ["Roll", "Pitch", "Throttle", "Rotation X", "Rotation Y", "Yaw", "Additional axis 1", "Additional axis 2"]
    readonly property var conditionTypes: ["Always", "Axis Above", "Axis Below", "Axis Between", "Axis Outside Range", "Button Held", "Button Released", "POV Active", "POV Inactive", "Base Profile Is", "Effective Profile Is"]
    readonly property var actionTypes: ["vJoy Button · Hold", "vJoy Button · Toggle", "Profile · Hold", "Profile · Toggle", "Axis Scale", "Axis Offset", "Axis Clamp", "Axis Override", "Axis Mix", "Axis Follow"]
    readonly property var stages: ["Physical", "Processed"]
    readonly property var directions: ["Up", "Up-Right", "Right", "Down-Right", "Down", "Down-Left", "Left", "Up-Left"]

    function clone(value) { return JSON.parse(JSON.stringify(value)) }
    function percent(value) { return (Number(value) * 100).toFixed(1) + "%" }
    function conditionPercent(value, axis) {
        const displayed = axis === 2 ? (Number(value) + 1) * 50 : Number(value) * 100
        return displayed.toFixed(1) + "%"
    }
    function conditionValue(text, axis) {
        const displayed = Number(String(text).replace("%", ""))
        return axis === 2 ? displayed / 50 - 1 : displayed / 100
    }
    function profileIndex(id) {
        const choices = backendObject.profileTriggerChoices
        for (let i = 0; i < choices.length; ++i) if (choices[i].id === id) return i
        return 0
    }
    function profileId(index) {
        const choices = backendObject.profileTriggerChoices
        return index >= 0 && index < choices.length ? choices[index].id : ""
    }
    function edit(rule) { editor.working = clone(rule); editor.open() }
    function removeCondition(index) { editor.working.conditions.splice(index, 1); editor.working = clone(editor.working) }
    function removeAction(index) { editor.working.actions.splice(index, 1); editor.working = clone(editor.working) }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 18
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: content
            x: 1
            width: parent.width - 10
            spacing: 14
            RowLayout {
                width: parent.width
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: root.topGun ? "AUTOMATION  /  FLIGHT LOGIC" : "Automation"; color: root.text; font.pixelSize: 26; font.bold: true }
                    Text { text: "Compiled deterministic controller rules · evaluated once per physical report"; color: root.muted; font.pixelSize: 12 }
                }
                Button { text: "+ NEW AUTOMATION"; onClicked: { if (backendObject.createAutomation()) { const list = backendObject.automationRules; if (list.length) root.edit(list[list.length - 1]) } } }
            }
            Rectangle {
                width: parent.width; implicitHeight: 88; radius: 6; color: root.card; border.color: root.border
                RowLayout { anchors.fill: parent; anchors.margins: 16; spacing: 18
                    Column { spacing: 3
                        Text { text: root.topGun ? "AUTOMATION ENGINE" : "ENGINE"; color: root.muted; font.pixelSize: 10; font.bold: true }
                        Text { text: backendObject.automationEngineEnabled ? (root.topGun ? "ARMED" : "ENABLED") : "DISABLED"; color: backendObject.automationEngineEnabled ? root.good : root.fault; font.pixelSize: 16; font.bold: true }
                    }
                    Switch { checked: backendObject.automationEngineEnabled; onToggled: backendObject.setAutomationEngineEnabled(checked) }
                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: root.border }
                    Column { spacing: 3
                        Text { text: "RULES"; color: root.muted; font.pixelSize: 10; font.bold: true }
                        Text { text: backendObject.automationRuleCount; color: root.text; font.pixelSize: 18; font.bold: true }
                    }
                    Column { spacing: 3
                        Text { text: "ACTIVE"; color: root.muted; font.pixelSize: 10; font.bold: true }
                        Text { text: backendObject.automationActiveRuleCount; color: root.good; font.pixelSize: 18; font.bold: true }
                    }
                    Column { spacing: 3
                        Text { text: "EVAL"; color: root.muted; font.pixelSize: 10; font.bold: true }
                        Text { text: backendObject.automationEvaluationUs + " µs"; color: root.text; font.pixelSize: 18; font.bold: true }
                    }
                    Item { Layout.fillWidth: true }
                    Text { visible: backendObject.automationValidationMessage.length > 0; text: backendObject.automationValidationMessage; color: root.fault; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.preferredWidth: 240 }
                }
            }
            Text { visible: rules.length === 0; width: parent.width; text: "No Automation rules. Add a rule to combine physical axes, buttons, POV, and profile state without macros or scripts."; color: root.muted; font.pixelSize: 13; wrapMode: Text.WordWrap }
            GridLayout {
                width: parent.width
                columns: width >= 1000 ? 2 : 1
                columnSpacing: 14; rowSpacing: 14
                Repeater {
                    model: root.rules
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: 184; radius: 6; color: root.card
                        border.color: modelData.health === 2 ? root.fault : modelData.active ? root.good : root.border
                        ColumnLayout { anchors.fill: parent; anchors.margins: 15; spacing: 7
                            RowLayout { Layout.fillWidth: true
                                ColumnLayout { Layout.fillWidth: true; spacing: 2
                                    Text { text: modelData.name.toUpperCase(); color: root.text; font.pixelSize: 14; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: !modelData.enabled ? "DISABLED" : modelData.health === 2 ? "⚠ NEEDS ATTENTION" : modelData.active ? (root.topGun ? "ACTIVE" : "● ACTIVE") : (root.topGun ? "STANDBY" : "○ INACTIVE"); color: !modelData.enabled || modelData.health === 2 ? root.fault : modelData.active ? root.good : root.muted; font.pixelSize: 10; font.bold: true }
                                }
                                Switch { checked: modelData.enabled; onToggled: backendObject.setAutomationEnabled(modelData.id, checked) }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                            Text { text: "WHEN"; color: root.muted; font.pixelSize: 9; font.bold: true }
                            Text { text: modelData.conditionSummary; color: root.text; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { text: "THEN"; color: root.muted; font.pixelSize: 9; font.bold: true }
                            Text { text: modelData.actionSummary; color: root.accent; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { visible: modelData.healthMessage.length > 0; text: modelData.healthMessage; color: root.fault; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                                Button { text: "EDIT"; onClicked: root.edit(modelData) }
                                Button { text: "DUPLICATE"; onClicked: backendObject.duplicateAutomation(modelData.id) }
                                Button { text: "DELETE"; onClicked: { deleteDialog.ruleId = modelData.id; deleteDialog.open() } }
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        property string ruleId: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete Automation?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: backendObject.deleteAutomation(ruleId)
        contentItem: Text { text: "This removes the Automation rule. It cannot be recovered from this configuration."; color: root.text; wrapMode: Text.WordWrap; width: 340 }
    }

    Dialog {
        id: editor
        property var working: ({})
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(860, Overlay.overlay.width - 36)
        height: Math.min(760, Overlay.overlay.height - 36)
        title: root.topGun ? "FLIGHT LOGIC" : "Edit Automation"
        standardButtons: Dialog.NoButton
        contentItem: ScrollView {
            clip: true
            ColumnLayout { width: editor.width - 44; spacing: 12
                Label { text: "NAME"; color: root.muted; font.bold: true }
                TextField { text: editor.working.name || ""; Layout.fillWidth: true; onTextEdited: { editor.working.name = text; editor.working = root.clone(editor.working) } }
                RowLayout { Layout.fillWidth: true
                    ColumnLayout { Layout.fillWidth: true; Label { text: "MATCH"; color: root.muted; font.bold: true }
                        ComboBox { model: ["ALL", "ANY"]; currentIndex: editor.working.matchMode || 0; onActivated: { editor.working.matchMode = currentIndex; editor.working = root.clone(editor.working) } }
                    }
                    ColumnLayout { Layout.fillWidth: true; Label { text: "ADVANCED PRIORITY"; color: root.muted; font.bold: true }
                        SpinBox { from: 0; to: 100; value: editor.working.priority === undefined ? 50 : editor.working.priority; onValueModified: { editor.working.priority = value; editor.working = root.clone(editor.working) } }
                    }
                    Switch { text: "Enabled"; checked: editor.working.enabled; onToggled: { editor.working.enabled = checked; editor.working = root.clone(editor.working) } }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                Label { text: "WHEN"; color: root.accent; font.bold: true }
                Repeater {
                    model: editor.working.conditions || []
                    delegate: Rectangle {
                        required property int index; required property var modelData
                        Layout.fillWidth: true; implicitHeight: conditionColumn.implicitHeight + 16; color: root.card; border.color: root.border; radius: 4
                        ColumnLayout { id: conditionColumn; anchors.fill: parent; anchors.margins: 8; spacing: 7
                            RowLayout { Layout.fillWidth: true
                                ComboBox { Layout.fillWidth: true; model: root.conditionTypes; currentIndex: modelData.type; onActivated: { editor.working.conditions[index].type = currentIndex; editor.working = root.clone(editor.working) } }
                                Button { text: "REMOVE"; enabled: editor.working.conditions.length > 1; onClicked: root.removeCondition(index) }
                            }
                            RowLayout { visible: modelData.type >= 1 && modelData.type <= 4; Layout.fillWidth: true
                                ComboBox { Layout.fillWidth: true; model: root.axisChoices; currentIndex: modelData.axis; onActivated: { editor.working.conditions[index].axis = currentIndex; editor.working = root.clone(editor.working) } }
                                TextField { Layout.preferredWidth: 96; text: root.conditionPercent(modelData.minimum, modelData.axis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: { editor.working.conditions[index].minimum = root.conditionValue(text, modelData.axis); editor.working = root.clone(editor.working) } }
                                TextField { visible: modelData.type === 3 || modelData.type === 4; Layout.preferredWidth: 96; text: root.conditionPercent(modelData.maximum, modelData.axis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: { editor.working.conditions[index].maximum = root.conditionValue(text, modelData.axis); editor.working = root.clone(editor.working) } }
                                TextField { Layout.preferredWidth: 96; text: "Hys " + root.percent(modelData.hysteresis); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: { editor.working.conditions[index].hysteresis = Number(text.replace("Hys", "").replace("%", "")) / 100; editor.working = root.clone(editor.working) } }
                            }
                            RowLayout { visible: modelData.type === 5 || modelData.type === 6; Layout.fillWidth: true
                                Label { text: "BUTTON"; color: root.muted }
                                SpinBox { from: 1; to: 128; value: modelData.button; onValueModified: { editor.working.conditions[index].button = value; editor.working = root.clone(editor.working) } }
                            }
                            RowLayout { visible: modelData.type === 7 || modelData.type === 8; Layout.fillWidth: true
                                Label { text: "POV"; color: root.muted }
                                SpinBox { from: 1; to: 4; value: modelData.povHat; onValueModified: { editor.working.conditions[index].povHat = value; editor.working = root.clone(editor.working) } }
                                ComboBox { model: root.directions; currentIndex: modelData.povDirection - 1; onActivated: { editor.working.conditions[index].povDirection = currentIndex + 1; editor.working = root.clone(editor.working) } }
                            }
                            ComboBox { visible: modelData.type === 9 || modelData.type === 10; Layout.fillWidth: true; model: backendObject.profileTriggerChoices; textRole: "label"; currentIndex: root.profileIndex(modelData.profileId); onActivated: { editor.working.conditions[index].profileId = root.profileId(currentIndex); editor.working = root.clone(editor.working) } }
                        }
                    }
                }
                Button { text: "+ CONDITION"; enabled: (editor.working.conditions || []).length < 4; onClicked: { editor.working.conditions.push({ type: 0, axis: 0, minimum: 0, maximum: 0, hysteresis: 0, button: 1, povHat: 1, povDirection: 1, profileId: "" }); editor.working = root.clone(editor.working) } }
                Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                Label { text: "THEN"; color: root.accent; font.bold: true }
                Repeater {
                    model: editor.working.actions || []
                    delegate: Rectangle {
                        required property int index; required property var modelData
                        Layout.fillWidth: true; implicitHeight: actionColumn.implicitHeight + 16; color: root.card; border.color: root.border; radius: 4
                        ColumnLayout { id: actionColumn; anchors.fill: parent; anchors.margins: 8; spacing: 7
                            RowLayout { Layout.fillWidth: true
                                ComboBox { Layout.fillWidth: true; model: root.actionTypes; currentIndex: modelData.type; onActivated: { editor.working.actions[index].type = currentIndex; editor.working = root.clone(editor.working) } }
                                Button { text: "REMOVE"; enabled: editor.working.actions.length > 1; onClicked: root.removeAction(index) }
                            }
                            RowLayout { visible: modelData.type === 0 || modelData.type === 1; Layout.fillWidth: true
                                Label { text: "VJOY BUTTON"; color: root.muted }
                                SpinBox { from: 1; to: 128; value: modelData.virtualButton; onValueModified: { editor.working.actions[index].virtualButton = value; editor.working = root.clone(editor.working) } }
                            }
                            ComboBox { visible: modelData.type === 2 || modelData.type === 3; Layout.fillWidth: true; model: backendObject.profileTriggerChoices; textRole: "label"; currentIndex: root.profileIndex(modelData.profileId); onActivated: { editor.working.actions[index].profileId = root.profileId(currentIndex); editor.working = root.clone(editor.working) } }
                            RowLayout { visible: modelData.type >= 4; Layout.fillWidth: true
                                Label { text: "TARGET"; color: root.muted }
                                ComboBox { model: root.axisChoices; currentIndex: modelData.targetAxis; onActivated: { editor.working.actions[index].targetAxis = currentIndex; editor.working = root.clone(editor.working) } }
                                Label { visible: modelData.type === 8 || modelData.type === 9; text: "SOURCE"; color: root.muted }
                                ComboBox { visible: modelData.type === 8 || modelData.type === 9; model: root.axisChoices; currentIndex: modelData.sourceAxis; onActivated: { editor.working.actions[index].sourceAxis = currentIndex; editor.working = root.clone(editor.working) } }
                                ComboBox { visible: modelData.type === 8 || modelData.type === 9; model: root.stages; currentIndex: modelData.sourceStage; onActivated: { editor.working.actions[index].sourceStage = currentIndex; editor.working = root.clone(editor.working) } }
                            }
                            RowLayout { visible: modelData.type >= 4; Layout.fillWidth: true
                                Label { text: modelData.type === 6 ? "MIN" : modelData.type === 9 ? "GAIN" : "VALUE"; color: root.muted }
                                TextField { Layout.preferredWidth: 100; text: root.percent(modelData.type === 6 ? modelData.minimum : modelData.value); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: { if (modelData.type === 6) editor.working.actions[index].minimum = Number(text.replace("%", "")) / 100; else editor.working.actions[index].value = Number(text.replace("%", "")) / 100; editor.working = root.clone(editor.working) } }
                                Label { visible: modelData.type === 6; text: "MAX"; color: root.muted }
                                TextField { visible: modelData.type === 6; Layout.preferredWidth: 100; text: root.percent(modelData.maximum); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: { editor.working.actions[index].maximum = Number(text.replace("%", "")) / 100; editor.working = root.clone(editor.working) } }
                                Label { visible: modelData.type === 9; text: "OFFSET"; color: root.muted }
                                TextField { visible: modelData.type === 9; Layout.preferredWidth: 100; text: root.percent(modelData.offset); inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: { editor.working.actions[index].offset = Number(text.replace("%", "")) / 100; editor.working = root.clone(editor.working) } }
                            }
                        }
                    }
                }
                Button { text: "+ ACTION"; enabled: (editor.working.actions || []).length < 4; onClicked: { editor.working.actions.push({ type: 4, virtualButton: 1, profileId: "", targetAxis: 0, sourceAxis: 0, sourceStage: 1, value: 1, offset: 0, minimum: -1, maximum: 1 }); editor.working = root.clone(editor.working) } }
                Text { text: backendObject.automationValidationMessage; color: root.fault; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout { Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button { text: "CANCEL"; onClicked: editor.close() }
                    Button { text: "SAVE AUTOMATION"; onClicked: { if (backendObject.saveAutomation(editor.working)) editor.close() } }
                }
            }
        }
    }
}
