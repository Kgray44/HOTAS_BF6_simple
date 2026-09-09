import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Native presentation for the existing InputLearning state machine. This
// component never observes input reports itself: AppBackend remains the sole
// owner of arming, detection, conflict resolution, assignment, and cancel.
FlightDeckDialog {
    id: control
    objectName: "flightDeckInputLearningDialog"

    // This preview-only popup owns its semantic token object. Keeping it
    // self-contained lets the application-window overlay host it without
    // reparenting the Flight Deck shell's theme resource.
    FlightDeckTheme { id: learningTokens }
    tokens: learningTokens
    property string workflow: ""
    property int selectedVirtualButton: 1
    property var targets: []
    property var assignments: []
    property int step: 0
    property bool complete: false
    property string startError: ""
    property bool confirmResetButtons: false

    readonly property bool quickWorkflow: workflow === "quick-axes" || workflow === "quick-buttons"
    readonly property bool quickAxes: workflow === "quick-axes"
    readonly property var learning: backend.inputLearning
    readonly property string phase: String(learning.phase || "idle")
    readonly property string kind: String(learning.kind || "none")
    readonly property var currentTarget: step >= 0 && step < targets.length ? targets[step] : ({})

    heading: quickWorkflow ? (quickAxes ? "Quick map — axes" : "Quick map — buttons")
        : "Learn input"
    tone: phase === "conflict" || startError.length > 0 ? "attention" : "informational"
    preferredWidth: quickWorkflow ? 560 : 460

    function resetFor(nextWorkflow) {
        if (learning.active)
            backend.cancelInputLearning()
        workflow = nextWorkflow
        targets = []
        assignments = []
        step = 0
        complete = false
        startError = ""
        confirmResetButtons = false
    }

    function openButtonLearning() {
        resetFor("single-button")
        selectedVirtualButton = Math.max(1, Math.min(selectedVirtualButton, backend.vjoyButtonCount))
        open()
    }

    function openAxisLearning(target) {
        resetFor("single-axis")
        targets = [{ "target": String(target || "Disabled") }]
        open()
        startSingle()
    }

    function openPovLearning(virtualButton) {
        resetFor("single-pov")
        selectedVirtualButton = Math.max(1, Number(virtualButton || 1))
        open()
        startSingle()
    }

    function openQuickAxes() {
        resetFor("quick-axes")
        targets = backend.quickAssignAxisTargets
        complete = targets.length === 0
        open()
        if (!complete)
            startCurrent()
    }

    function openQuickButtons() {
        resetFor("quick-buttons")
        targets = backend.quickMapButtonTargets
        complete = targets.length === 0
        open()
        if (!complete)
            startCurrent()
    }

    function startSingle() {
        startError = ""
        let started = false
        if (workflow === "single-axis")
            started = backend.startAxisLearning(String((targets[0] || {}).target || "Disabled"))
        else if (workflow === "single-pov")
            started = backend.startPovLearning(selectedVirtualButton)
        else if (workflow === "single-button")
            started = backend.startButtonLearning(selectedVirtualButton)
        if (!started)
            startError = "The selected controller and virtual output must be available before input learning can begin."
        return started
    }

    function startCurrent() {
        if (complete || step < 0 || step >= targets.length)
            return false
        startError = ""
        const target = currentTarget || ({})
        const started = quickAxes
            ? backend.startAxisLearning(String(target.target || "Disabled"))
            : backend.startButtonLearning(Number(target.virtualButton || 0))
        if (!started)
            startError = "This output is unavailable for the selected controller. Review Devices & setup and try again."
        return started
    }

    function acceptAssignment() {
        if (!quickWorkflow || phase !== "assigned")
            return
        const nextAssignments = assignments.slice(0)
        nextAssignments[step] = String(learning.sourceLabel || "Assigned")
        assignments = nextAssignments
        if (step + 1 >= targets.length) {
            complete = true
            backend.cancelInputLearning()
            return
        }
        step += 1
        Qt.callLater(startCurrent)
    }

    function skipCurrent() {
        if (!quickWorkflow || complete)
            return
        backend.cancelInputLearning()
        const nextAssignments = assignments.slice(0)
        nextAssignments[step] = "Skipped"
        assignments = nextAssignments
        if (step + 1 >= targets.length) {
            complete = true
            return
        }
        step += 1
        Qt.callLater(startCurrent)
    }

    function previousTarget() {
        if (!quickWorkflow || step <= 0)
            return
        backend.cancelInputLearning()
        step -= 1
        assignments = assignments.slice(0, step)
        Qt.callLater(startCurrent)
    }

    function cancelWorkflow() {
        if (learning.active)
            backend.cancelInputLearning()
        close()
    }

    onClosed: {
        if (learning.active)
            backend.cancelInputLearning()
        workflow = ""
    }

    Connections {
        target: backend
        function onInputLearningChanged() {
            if (control.visible && control.quickWorkflow && control.phase === "assigned")
                control.acceptAssignment()
        }
    }

    component DeckButton: Button {
        id: button
        property bool subdued: false
        implicitHeight: control.tokens.compactControlHeight
        focusPolicy: Qt.StrongFocus
        contentItem: Text {
            text: button.text
            color: button.enabled ? (button.subdued ? control.tokens.textSecondary : control.tokens.primarySurface)
                : control.tokens.disabled
            font.family: control.tokens.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: control.tokens.radiusControl
            color: !button.enabled ? control.tokens.secondarySurface
                : button.down ? control.tokens.accentMuted
                : button.hovered ? (button.subdued ? control.tokens.selected : control.tokens.focus)
                : (button.subdued ? control.tokens.secondarySurface : control.tokens.accent)
            border.width: button.activeFocus ? 2 : 1
            border.color: button.activeFocus ? control.tokens.focus : control.tokens.border
        }
    }

    component OutputSelector: ComboBox {
        id: selector
        objectName: "flightDeckLearningOutputSelector"
        Layout.fillWidth: true
        model: backend.buttonOutputChoices.slice(1)
        currentIndex: Math.max(0, control.selectedVirtualButton - 1)
        enabled: !control.learning.active
        onActivated: control.selectedVirtualButton = currentIndex + 1
        contentItem: Text {
            leftPadding: control.tokens.space12
            rightPadding: control.tokens.space12
            text: selector.displayText
            color: selector.enabled ? control.tokens.textPrimary : control.tokens.disabled
            font.family: control.tokens.telemetryFont
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: control.tokens.controlHeight
            radius: control.tokens.radiusControl
            color: control.tokens.secondarySurface
            border.width: selector.activeFocus ? 2 : 1
            border.color: selector.activeFocus ? control.tokens.focus : control.tokens.border
        }
        popup: Popup {
            y: selector.height + 4
            width: selector.width
            implicitHeight: Math.min(contentItem.implicitHeight + control.tokens.space8, 260)
            padding: control.tokens.space4
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: selector.popup.visible ? selector.delegateModel : null
                currentIndex: selector.highlightedIndex
            }
            background: Rectangle {
                radius: control.tokens.radiusControl
                color: control.tokens.elevatedSurface
                border.color: control.tokens.border
            }
        }
        delegate: ItemDelegate {
            width: selector.width - control.tokens.space8
            height: control.tokens.compactControlHeight
            text: modelData
            highlighted: selector.highlightedIndex === index
            contentItem: Text {
                text: parent.text
                color: parent.highlighted ? control.tokens.accent : control.tokens.textPrimary
                font.family: control.tokens.telemetryFont
                font.pixelSize: 10
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: control.tokens.radiusControl
                color: parent.highlighted ? control.tokens.selected : "transparent"
            }
        }
    }

    contentItem: ColumnLayout {
        width: control.width - control.tokens.space32
        spacing: control.tokens.space12

        Text {
            Layout.fillWidth: true
            visible: control.quickWorkflow
            text: control.quickAxes
                ? "Assign each available virtual axis in sequence. This uses the existing input-learning command and does not start mapping output."
                : "Assign each available virtual button in sequence. This uses the existing input-learning command and does not press or execute a button."
            color: control.tokens.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: !control.quickWorkflow
            text: control.workflow === "single-button"
                ? "Choose an existing virtual button, then deliberately start listening for one physical control."
                : control.workflow === "single-pov"
                    ? "Move the desired hat direction when the existing learning command is ready."
                    : "Move the desired physical axis when the existing learning command is ready."
            color: control.tokens.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        OutputSelector {
            visible: control.workflow === "single-button"
        }
        Rectangle {
            Layout.fillWidth: true
            visible: control.quickWorkflow && !control.complete
            implicitHeight: 5
            radius: 3
            color: control.tokens.secondarySurface
            Rectangle {
                width: parent.width * (control.targets.length > 0 ? control.step / control.targets.length : 0)
                height: parent.height
                radius: parent.radius
                color: control.tokens.accent
            }
        }
        Text {
            Layout.fillWidth: true
            visible: control.quickWorkflow && !control.complete
            text: (control.step + 1) + " OF " + control.targets.length + " · "
                + (control.quickAxes ? String(control.currentTarget.label || control.currentTarget.target || "Axis")
                    : String(control.currentTarget.label || "vJoy Button"))
            color: control.tokens.textPrimary
            font.family: control.tokens.displayFont
            font.pixelSize: 15
            font.bold: true
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: !control.quickWorkflow && control.workflow !== "single-button"
            text: String(control.learning.targetLabel || "Configured output")
            color: control.tokens.textMuted
            font.family: control.tokens.telemetryFont
            font.pixelSize: 10
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            visible: control.startError.length > 0 || control.learning.active
            text: control.startError.length > 0 ? control.startError : String(control.learning.message || "Preparing input learning…")
            color: control.startError.length > 0 ? control.tokens.attention : control.tokens.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: control.quickWorkflow && !control.quickAxes && control.confirmResetButtons
            text: "Reset every button route in the active profile? Click CONFIRM RESET to continue, or CANCEL to keep the current routes."
            color: control.tokens.attention
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: control.phase === "assigned" && !control.quickWorkflow
            text: "ASSIGNED · " + String(control.learning.sourceLabel || "Input")
            color: control.tokens.healthy
            font.family: control.tokens.telemetryFont
            font.pixelSize: 11
            font.bold: true
            wrapMode: Text.WordWrap
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: control.quickWorkflow && control.assignments.length > 0
            spacing: control.tokens.space4
            Text {
                text: control.complete ? "QUICK MAP COMPLETE" : "COMPLETED"
                color: control.complete ? control.tokens.healthy : control.tokens.textMuted
                font.family: control.tokens.telemetryFont
                font.pixelSize: 9
                font.bold: true
            }
            Repeater {
                model: control.assignments
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "✓ " + String(control.targets[index].label || control.targets[index].target || "Output")
                        + " · " + String(modelData)
                    color: control.tokens.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: control.tokens.space8
            DeckButton {
                visible: control.quickWorkflow && !control.complete && control.step > 0
                text: "BACK"
                subdued: true
                onClicked: control.previousTarget()
            }
            DeckButton {
                visible: !control.complete && control.phase === "ambiguous"
                text: "RETRY"
                subdued: true
                onClicked: backend.retryInputLearning()
            }
            DeckButton {
                visible: !control.complete && control.phase === "conflict"
                text: "REPLACE"
                onClicked: backend.resolveInputLearningConflict("replace")
            }
            DeckButton {
                visible: !control.complete && control.phase === "conflict" && control.kind === "button"
                text: "IGNORE"
                subdued: true
                onClicked: backend.resolveInputLearningConflict("ignore")
            }
            DeckButton {
                visible: control.quickWorkflow && !control.complete
                text: "SKIP"
                subdued: true
                onClicked: control.skipCurrent()
            }
            DeckButton {
                visible: control.quickWorkflow && !control.quickAxes && !control.complete
                text: control.confirmResetButtons ? "CONFIRM RESET" : "RESET BUTTON MAPPINGS"
                subdued: !control.confirmResetButtons
                onClicked: {
                    if (!control.confirmResetButtons) {
                        control.confirmResetButtons = true
                        return
                    }
                    if (control.learning.active)
                        backend.cancelInputLearning()
                    backend.resetButtonMappings()
                    control.confirmResetButtons = false
                    control.complete = true
                    control.startError = "Button mappings reset for the active profile."
                }
            }
            Item { Layout.fillWidth: true }
            DeckButton {
                visible: control.workflow === "single-button" && !control.learning.active && control.phase !== "assigned"
                text: "START LISTENING"
                enabled: backend.physicalConnected && control.selectedVirtualButton > 0
                onClicked: control.startSingle()
            }
            DeckButton {
                text: control.complete || control.phase === "assigned" ? "DONE" : "CANCEL"
                subdued: !(control.complete || control.phase === "assigned")
                onClicked: control.cancelWorkflow()
            }
        }
    }
}
