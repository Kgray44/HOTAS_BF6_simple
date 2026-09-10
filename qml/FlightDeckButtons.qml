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
    signal navigateToPage(int page)
    signal navigateToProfile(string profileId)
    signal navigateToAutomation(string automationId)
    signal requestButtonLearning()
    signal requestQuickMap()
    signal requestPovLearning(int virtualButton)

    readonly property var buttonItems: buttonPresentationOverride !== null ? buttonPresentationOverride : backend.buttons
    readonly property var povItems: povPresentationOverride !== null ? povPresentationOverride : backend.povs
    readonly property var povInputItems: povInputsPresentationOverride !== null ? povInputsPresentationOverride : backend.povInputs
    readonly property var automationItems: automationPresentationOverride !== null ? automationPresentationOverride : backend.automationRules
    readonly property var outputChoices: backend.buttonOutputChoices
    readonly property var profileChoices: backend.profileTriggerChoices
    readonly property var behaviorChoices: backend.profileTriggerBehaviorChoices
    readonly property var mappingControlChoices: backend.mappingControlActionChoices
    readonly property var nativePovChoices: backend.nativePovTargetChoices
    readonly property string inputDeviceName: inputDeviceNameOverride.length > 0
        ? inputDeviceNameOverride : (backend.deviceName || "Selected controller")
    readonly property bool hasVisibleButtons: visibleButtonCount() > 0

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
        return Number((button || {}).target || 0) > 0
            || Boolean((button || {}).profileControlEnabled)
            || String((button || {}).mappingControlKey || "none") !== "none"
            || automationForButton(Number((button || {}).index)).length > 0
    }
    function buttonVisible(button) {
        if (!button) return false
        const assigned = isAssigned(button)
        if (filterMode === "assigned" && !assigned) return false
        if (filterMode === "unassigned" && assigned) return false
        const needle = lower(searchText).trim()
        if (needle.length === 0) return true
        const terms = [button.label, button.hardwareLabel, button.targetLabel,
            button.profileControlTargetName, button.mappingControl]
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
        if (backend.setButtonMapping(buttonIndex, target, explicitOverride)) return true
        if (!explicitOverride) {
            conflictButtonIndex = buttonIndex
            conflictHatIndex = -1
            conflictDirectionIndex = -1
            conflictTarget = target
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
            mappingConflict.open()
        }
        return false
    }

    component SectionLabel: Text {
        color: deck.textMuted
        font.family: deck.telemetryFont
        font.pixelSize: 9
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
            font.pixelSize: 8
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
            font.pixelSize: 9
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
                font.pixelSize: 10
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
        color: deck.textPrimary
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
            font.pixelSize: 8
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

    component ButtonCard: FlightDeckCard {
        id: card
        tokens: deck
        property var button: ({})
        readonly property int buttonIndex: Number(button.index)
        readonly property bool expanded: root.expandedButtonIndex === buttonIndex
        readonly property var automations: root.automationForButton(buttonIndex)
        objectName: "flightDeckButtonCard_" + buttonIndex
        width: root.width >= 1180 ? (assignedFlow.width - deck.space12) / 2 : assignedFlow.width
        visible: root.isAssigned(button) && root.buttonVisible(button)
        implicitHeight: visible ? content.implicitHeight + contentPadding * 2 : 0
        color: button.pressed ? deck.selected : deck.elevatedSurface
        border.color: button.pressed ? deck.accent : deck.border

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
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Text {
                        text: root.inputDeviceName + " · " + String(button.hardwareLabel || "Button " + buttonIndex)
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }
                SummaryChip {
                    label: button.pressed ? "PRESSED" : "RELEASED"
                    tone: button.pressed ? "healthy" : "informational"
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
            RowLayout {
                Layout.fillWidth: true
                spacing: deck.space8
                Text {
                    text: "→"
                    color: deck.accent
                    font.pixelSize: 20
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
                        font.pixelSize: 13
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: button.profileControlEnabled && Number(button.target) > 0
                        text: "Saved game route · " + String(button.targetLabel)
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        Layout.fillWidth: true
                    }
                    Text {
                        visible: automations.length > 0
                        text: "Automation · " + automations.map(function(rule) { return rule.name || "Rule" }).join(" · ")
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
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
                    visible: button.profileControlEnabled
                    label: String(button.profileControlMode || "Profile").toUpperCase()
                    tone: button.profileControlTargetAvailable ? "healthy" : "attention"
                }
                SummaryChip {
                    visible: String(button.mappingControlKey || "none") !== "none"
                    label: "MAPPING CONTROL"
                    tone: "informational"
                }
                SummaryChip {
                    visible: Number(button.target) > 0 && !backend.vjoyReady
                    label: "OUTPUT OFFLINE"
                    tone: "attention"
                }
                SummaryChip {
                    visible: Number(button.target) > 0 && button.virtualPressed
                    label: "VIRTUAL PRESSED"
                    tone: "healthy"
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
                            model: root.outputChoices
                            currentIndex: Math.max(0, Number(button.target || 0))
                            onActivated: {
                                if (!root.requestButtonMapping(card.buttonIndex, currentIndex, false))
                                    currentIndex = Math.max(0, Number(button.target || 0))
                            }
                        }
                    }
                }
                Text {
                    text: "Physical control → existing vJoy button route. Changes apply through the current profile command path."
                    color: deck.textMuted
                    font.pixelSize: 9
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
                        text: Number(button.target) > 0 && button.virtualPressed ? "vJoy output is pressed" : ""
                        color: deck.healthy
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                SectionLabel { text: "PROFILE CONTROL" }
                Text {
                    text: "A profile control consumes this physical input while retaining its saved game route for restoration when cleared."
                    color: deck.textMuted
                    font.pixelSize: 9
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
                    visible: button.profileControlEnabled
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: button.profileControlTargetAvailable
                            ? "References " + String(button.profileControlTargetName)
                            : "The referenced profile is unavailable. Choose another profile or clear this control."
                        color: button.profileControlTargetAvailable ? deck.textSecondary : deck.attention
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    DeckButton {
                        text: "OPEN PROFILE"
                        subdued: true
                        enabled: button.profileControlTargetAvailable
                        onClicked: root.navigateToProfile(String(button.profileControlTargetId || ""))
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                SectionLabel { text: "MAPPING CONTROL" }
                Text {
                    text: "This existing global control is independent of the game route above."
                    color: deck.textMuted
                    font.pixelSize: 9
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
                                font.pixelSize: 10
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
            font.pixelSize: 10
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
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        text: root.inputDeviceName + " · " + String(button.hardwareLabel || "Button " + buttonIndex) + " · Unassigned"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                    }
                }
                DeckButton { text: "CLOSE"; subdued: true; onClicked: root.expandedButtonIndex = -1 }
            }
            SectionLabel { text: "ASSIGN TO GAME OUTPUT" }
            DeckCombo {
                objectName: "flightDeckButtonMappingSelector_" + editor.buttonIndex
                Layout.fillWidth: true
                model: root.outputChoices
                currentIndex: 0
                onActivated: {
                    if (!root.requestButtonMapping(editor.buttonIndex, currentIndex, false)) currentIndex = 0
                }
            }
            Text {
                text: "Unassigned is intentional. Choose an existing vJoy button only when this control should have a game output."
                color: deck.textMuted
                font.pixelSize: 9
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
                font.pixelSize: 9
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.assignmentForPov(directionInput)
                color: directionInput.active ? deck.healthy : deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 8
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
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        text: root.inputDeviceName + " · POV " + card.hatIndex
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
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
                        font.pixelSize: 18
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
                        text: "NATIVE vJOY POV"
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        font.bold: true
                    }
                    Text {
                        text: String(hat.nativeTargetLabel || "Off") + " · " + String(hat.nativeStatus || "OFF")
                        color: hat.nativeAvailable || !hat.nativeEnabled ? deck.textMuted : deck.attention
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                DeckButton {
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
                Layout.fillWidth: true
                model: root.nativePovChoices
                textRole: "label"
                valueRole: "key"
                enabled: root.nativePovChoices.length > 0
                currentIndex: root.nativeChoiceIndex(hat.nativeTargetKey)
                onActivated: backend.setNativePovOutput(card.hatIndex, hat.nativeEnabled,
                    root.nativeChoiceKey(currentIndex))
            }
            Text {
                visible: root.nativePovChoices.length === 0
                text: "The selected vJoy device exposes no native POV target. Direction routes above remain available."
                color: deck.attention
                font.pixelSize: 9
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
                    text: "Physical POV direction → existing vJoy button route."
                    color: deck.textMuted
                    font.pixelSize: 9
                }
                DeckCombo {
                    id: povMappingSelector
                    objectName: "flightDeckPovMappingSelector_" + card.hatIndex + "_" + root.expandedPovDirection
                    Layout.fillWidth: true
                    model: root.outputChoices
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
                        text: "Learn a physical hat direction for the current vJoy button route."
                        color: deck.textMuted
                        font.pixelSize: 9
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
                        font.pixelSize: 10
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
                            font.pixelSize: 10
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

    ColumnLayout {
        id: buttonsContent
        x: deck.space4
        width: root.width - deck.space8
        spacing: deck.space16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: "Buttons"
                    color: deck.textPrimary
                    font.family: deck.displayFont
                    font.pixelSize: 24
                    font.bold: true
                }
                Text {
                    text: inputDeviceName + " · " + assignedButtonCount() + " assigned of " + buttonItems.length + " controls"
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            DeckButton {
                objectName: "flightDeckButtonsLearn"
                text: "LEARN ROUTE"
                subdued: true
                enabled: backend.physicalConnected && backend.vjoyButtonCount > 0
                onClicked: root.requestButtonLearning()
            }
            DeckButton {
                objectName: "flightDeckButtonsQuickMap"
                text: "QUICK MAP"
                subdued: true
                enabled: backend.buttonCount > 0
                onClicked: root.requestQuickMap()
            }
        }

        FlightDeckCard {
            tokens: deck
            contentPadding: deck.cardPadding
            Layout.fillWidth: true
            implicitHeight: statusContent.implicitHeight + contentPadding * 2
            color: backend.physicalConnected ? deck.secondarySurface : deck.elevatedSurface
            ColumnLayout {
                id: statusContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space8
                RowLayout {
                    Layout.fillWidth: true
                    SummaryChip {
                        label: backend.physicalConnected ? "CONTROLLER CONNECTED" : "CONTROLLER UNAVAILABLE"
                        tone: backend.physicalConnected ? "healthy" : "attention"
                    }
                    SummaryChip {
                        label: backend.vjoyReady ? "VIRTUAL OUTPUT READY" : "VIRTUAL OUTPUT ATTENTION"
                        tone: backend.vjoyReady ? "healthy" : "attention"
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: backend.lastPhysicalButton > 0 ? "Last control · Button " + backend.lastPhysicalButton : "Live state waits for physical input"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }
                RowLayout {
                    visible: !backend.physicalConnected || !backend.vjoyReady
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: !backend.physicalConnected
                            ? "Reconnect or choose a controller in Devices & setup. Configured routes remain owned by the active profile."
                            : "Virtual output needs attention. You can still inspect existing physical controls and routes."
                        color: deck.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    DeckButton { text: "OPEN SETUP"; subdued: true; onClicked: root.navigateToPage(2) }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: deck.space8
            FilterButton { text: "ALL"; filterValue: "all" }
            FilterButton { text: "ASSIGNED"; filterValue: "assigned" }
            FilterButton { text: "UNASSIGNED"; filterValue: "unassigned" }
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

        SectionLabel { text: "ASSIGNED CONTROLS" }
        Text {
            visible: assignedButtonCount() === 0
            text: "No assigned physical buttons are published for this controller."
            color: deck.textMuted
            font.pixelSize: 10
            Layout.fillWidth: true
        }
        Flow {
            id: assignedFlow
            Layout.fillWidth: true
            spacing: deck.space12
            Repeater {
                model: root.buttonItems
                delegate: ButtonCard {
                    required property var modelData
                    button: modelData
                }
            }
        }

        SectionLabel { text: "UNASSIGNED CONTROLS" }
        Text {
            visible: !hasVisibleButtons
            text: "No DirectInput buttons are currently published. Open Devices & setup to select or reconnect a controller."
            color: deck.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Flow {
            Layout.fillWidth: true
            spacing: deck.space8
            Repeater {
                model: root.buttonItems
                delegate: UnassignedButton {
                    required property var modelData
                    button: modelData
                }
            }
        }
        Repeater {
            model: root.buttonItems
            delegate: UnassignedEditor {
                required property var modelData
                button: modelData
            }
        }

        SectionLabel { visible: root.povItems.length > 0; text: "HATS / POV" }
        Text {
            visible: root.povItems.length > 0
            text: "Directions use the authoritative discrete POV routes. Native vJoy POV output, when available, stays a separate existing path."
            color: deck.textMuted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Repeater {
            model: root.povItems
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
        heading: "Output already assigned"
        tone: "attention"
        preferredWidth: 520
        contentItem: ColumnLayout {
            width: mappingConflict.availableWidth
            spacing: deck.space12
            Text {
                Layout.fillWidth: true
                text: conflictButtonIndex > 0
                    ? "This vJoy button is already routed. Replace moves the existing route; Share keeps both physical buttons on the same vJoy output."
                    : "This vJoy button is already routed. Replace moves the existing route. POV directions do not support shared output routes."
                color: deck.textSecondary
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                DeckButton { text: "CANCEL"; subdued: true; onClicked: mappingConflict.close() }
                Item { Layout.fillWidth: true }
                DeckButton {
                    visible: conflictButtonIndex > 0
                    text: "SHARE"
                    subdued: true
                    onClicked: {
                        const changed = backend.resolveButtonRouteChange(conflictButtonIndex, conflictTarget, "ignore")
                        if (changed) mappingConflict.close()
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
            }
        }
    }
}
