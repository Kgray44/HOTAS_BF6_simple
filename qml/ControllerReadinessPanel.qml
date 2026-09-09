import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// A control-plane assistant: it turns readiness state into one understandable
// next action. The mapper still owns every DirectInput and vJoy operation.
Item {
    id: root
    objectName: "setupAssistant"
    property var backendObject
    property var themeTokens: null
    property bool legacy: false
    property bool activityMonitoring: false
    property bool detailsExpanded: false
    property bool technicalDetailsExpanded: false
    property int liveTick: 0
    property string focusedStepId: ""
    property var actionResult: ({})
    readonly property var summary: backendObject ? backendObject.setupAssistantSummary : ({})
    readonly property var issues: backendObject ? backendObject.setupAssistantIssues : []
    readonly property var steps: backendObject ? backendObject.setupAssistantSteps : []
    readonly property var primaryIssue: summary.primaryIssue || (issues.length > 0 ? issues[0] : ({}))
    readonly property var liveTest: {
        liveTick
        return backendObject ? backendObject.setupAssistantLiveTest : ({ active: false, complete: false, steps: [] })
    }
    readonly property color panelColor: themeTokens ? themeTokens.panel : "#1a1d23"
    readonly property color insetColor: themeTokens ? themeTokens.panelInset : "#10171b"
    readonly property color borderColor: themeTokens ? themeTokens.border : "#435660"
    readonly property color textColor: themeTokens ? themeTokens.text : "#e8eeee"
    readonly property color mutedColor: themeTokens ? themeTokens.textMuted : "#9dafb4"
    readonly property color readyColor: themeTokens ? themeTokens.ready : "#8fd5c9"
    readonly property color warningColor: themeTokens ? themeTokens.warning : "#d4ad69"
    readonly property color dangerColor: themeTokens ? themeTokens.danger : "#ca9090"
    readonly property int radius: themeTokens ? themeTokens.panelRadius : 4
    signal closeRequested()
    signal calibrationRequested()

    implicitWidth: 680
    implicitHeight: assistantColumn.implicitHeight

    function stateColor(state) {
        if (state === "READY" || state === "ready") return readyColor
        if (state === "ERROR" || state === "error" || state === "offline") return dangerColor
        if (state === "optional" || state === "listening" || state === "skipped") return mutedColor
        return warningColor
    }
    function activeGuidedStep() {
        for (let i = 0; i < steps.length; ++i) if (steps[i].id === focusedStepId) return i
        for (let i = 0; i < steps.length; ++i) if (steps[i].state === "current") return i
        return 0
    }
    function focusGuidedStep(index) {
        if (index < 0 || index >= steps.length) return
        focusedStepId = steps[index].id
        detailsExpanded = true
    }
    function showActionResult(result) {
        actionResult = result || ({ success: false, title: "Action did not complete", message: "Try checking setup again." })
    }
    function performPrimaryAction() {
        if (!backendObject) return
        const action = summary.primaryAction || "check-again"
        if (action === "done") { closeRequested(); return }
        if (action === "hide-from-games") { fixConfirmation.open(); return }
        if (action === "start-live-test") { showActionResult(backendObject.startSetupAssistantLiveTest()); detailsExpanded = true; return }
        if (action === "start-calibration") { calibrationRequested(); return }
        if (action === "setup-vjoy" || action === "reconfigure-output") {
            const opened = backendObject.openVjoyConfiguration()
            showActionResult({ success: opened, title: opened ? "vJoy configuration opened" : "vJoy configuration could not open",
                message: opened ? "Configure the requested virtual controller, then return here and choose Check Again." : "Install or repair the vJoy configuration tool, then try again." })
            return
        }
        if (action === "setup-hidhide") {
            const opened = backendObject.openHidHideConfiguration()
            showActionResult({ success: opened, title: opened ? "Game visibility setup opened" : "Game visibility setup could not open",
                message: opened ? "Complete the game visibility setup, then return here and choose Check Again." : "Install or repair HidHide, then try again." })
            return
        }
        if (action === "review-routing") {
            detailsExpanded = true
            showActionResult({ success: true, title: "Review routing", message: "The routing details below identify the controls that need attention." })
            return
        }
        showActionResult(backendObject.startSetupAssistantCheck())
    }
    function performStepAction(step) {
        if (!backendObject || !step) return
        const action = step.action || ""
        if (action === "hide-from-games") { fixConfirmation.open(); return }
        if (action === "start-calibration") { calibrationRequested(); return }
        if (action === "skip-calibration") {
            showActionResult(backendObject.skipCalibrationForSetup(root.summary.scopeId || ""))
            return
        }
        if (action === "start-live-test") {
            showActionResult(backendObject.startSetupAssistantLiveTest())
            return
        }
        if (action !== "") root.performPrimaryAction()
    }
    onStepsChanged: {
        for (let i = 0; i < steps.length; ++i) {
            if (steps[i].state === "current") {
                focusedStepId = steps[i].id
                return
            }
        }
        if (steps.length > 0 && focusedStepId === "") focusedStepId = steps[0].id
    }

    Timer {
        interval: 180; repeat: true
        // Activity is passive evidence, not a gated test session. This only
        // refreshes the visible control-plane projection of existing atomics.
        running: root.backendObject && root.activityMonitoring
        onTriggered: root.liveTick++
    }

    ColumnLayout {
        id: assistantColumn
        width: parent.width
        spacing: 12

        Rectangle {
            objectName: "setupAssistantSummary"
            Layout.fillWidth: true
            Layout.preferredHeight: summaryColumn.implicitHeight + 30
            color: root.insetColor; border.color: root.stateColor(root.summary.state); radius: root.radius
            ColumnLayout {
                id: summaryColumn
                anchors.fill: parent; anchors.margins: 15; spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle { width: 10; height: 10; radius: root.radius > 2 ? 5 : 1; color: root.stateColor(root.summary.state) }
                    Text { Layout.fillWidth: true; text: root.summary.state || "CHECKING"; color: root.stateColor(root.summary.state); font.pixelSize: 10; font.bold: true }
                    Text { text: root.summary.scope || "HOTAS BF6"; color: root.mutedColor; font.pixelSize: 10; elide: Text.ElideRight }
                }
                Text { Layout.fillWidth: true; text: root.summary.title || "Checking your setup"; color: root.textColor; font.pixelSize: 20; font.bold: true; wrapMode: Text.WordWrap }
                Text { Layout.fillWidth: true; text: root.summary.message || "HOTAS BF6 is preparing the Setup Assistant."; color: root.textColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
            }
        }

        ThemedActionFeedback {
            objectName: "setupAssistantActionResult"
            Layout.fillWidth: true
            result: root.actionResult
            theme: root.themeTokens
            legacy: root.legacy
        }

        ColumnLayout {
            Layout.fillWidth: true; spacing: 7; visible: root.summary.state === "READY"
            Repeater { model: root.summary.readyItems || []
                delegate: Text { required property var modelData; Layout.fillWidth: true; text: "✓ " + modelData; color: root.textColor; font.pixelSize: 11 }
            }
            ColumnLayout { Layout.fillWidth: true; spacing: 3; visible: (root.liveTest.steps || []).length > 0
                Text { text: "ACTIVITY"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                Repeater { model: root.liveTest.steps || []
                    delegate: RowLayout { required property var modelData; Layout.fillWidth: true; spacing: 7
                        Rectangle { width: 7; height: 7; radius: 4; color: modelData.state === "ready" ? root.readyColor : modelData.state === "offline" ? root.dangerColor : root.mutedColor }
                        Text { Layout.fillWidth: true; text: modelData.title + " · " + modelData.message; color: root.mutedColor; font.pixelSize: 10; elide: Text.ElideRight }
                        Text { text: modelData.state === "ready" ? "INPUT DETECTED" : modelData.state === "offline" ? "OFFLINE" : "LISTENING"; color: modelData.state === "ready" ? root.readyColor : modelData.state === "offline" ? root.dangerColor : root.mutedColor; font.pixelSize: 9; font.bold: true }
                    }
                }
            }
            Text { Layout.fillWidth: true; visible: !!root.summary.secondaryMessage; text: root.summary.secondaryMessage || ""; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            ThemedButton { theme: root.themeTokens; text: root.summary.primaryActionLabel || "CHECK SETUP"; emphasis: "ready"
                commandEnabled: root.backendObject && !root.backendObject.controllerSetupInProgress; onTriggered: root.performPrimaryAction() }
            ThemedButton { theme: root.themeTokens; text: root.detailsExpanded ? "HIDE ALL SETUP STEPS" : "VIEW ALL SETUP STEPS"; tone: "secondary"; onTriggered: root.detailsExpanded = !root.detailsExpanded }
            ThemedButton { visible: root.detailsExpanded; theme: root.themeTokens; text: "BACK"; compact: true; tone: "secondary"; commandEnabled: root.activeGuidedStep() > 0; onTriggered: root.focusGuidedStep(root.activeGuidedStep() - 1) }
            ThemedButton { visible: root.detailsExpanded; theme: root.themeTokens; text: "NEXT"; compact: true; tone: "secondary"; commandEnabled: root.activeGuidedStep() + 1 < root.steps.length; onTriggered: root.focusGuidedStep(root.activeGuidedStep() + 1) }
            Item { Layout.fillWidth: true }
            ThemedButton { theme: root.themeTokens; text: root.summary.state === "READY" ? "DONE" : "CLOSE"; tone: "secondary"; onTriggered: root.closeRequested() }
        }

        ColumnLayout {
            objectName: "setupAssistantGuidedSteps"
            visible: root.detailsExpanded
            Layout.fillWidth: true; spacing: 12
            Rectangle { Layout.fillWidth: true; visible: root.activeGuidedStep() < root.steps.length && root.steps[root.activeGuidedStep()].blocked; Layout.preferredHeight: visible ? focusedStepMessage.implicitHeight + 18 : 0; color: root.insetColor; border.color: root.warningColor; radius: root.radius
                Text { id: focusedStepMessage; anchors.fill: parent; anchors.margins: 9; text: root.steps[root.activeGuidedStep()].message; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }
            Repeater {
                model: root.steps
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: setupStepContents.implicitHeight + 22
                    color: root.panelColor
                    border.color: modelData.state === "current" ? root.warningColor : root.borderColor
                    radius: root.radius
                    ColumnLayout {
                        id: setupStepContents
                        anchors.fill: parent; anchors.margins: 11; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "STEP " + modelData.order + "  ·  " + modelData.title + (modelData.optional ? "  ·  OPTIONAL" : ""); color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                            Item { Layout.fillWidth: true }
                            ThemedButton { theme: root.themeTokens; compact: true; tone: "secondary"; text: (modelData.state || "waiting").toUpperCase(); onTriggered: root.focusGuidedStep(index) }
                        }
                        Text { Layout.fillWidth: true; text: modelData.message; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; visible: (modelData.id === "physical" || modelData.id === "device") && (root.summary.state === "OFFLINE" || (root.backendObject && root.backendObject.controllerDisconnectObserved)); text: "RECONNECT CONTROLLER\nReconnect the required physical controller, then choose Check Setup."; color: root.warningColor; font.pixelSize: 10; font.bold: true; wrapMode: Text.WordWrap }
                        RowLayout { Layout.fillWidth: true; visible: modelData.action !== "" && (modelData.state === "current" || modelData.id === "calibration")
                            ThemedButton { theme: root.themeTokens; text: modelData.actionLabel || "CONTINUE"; emphasis: "ready"; commandEnabled: root.backendObject && (modelData.action !== "hide-from-games" || modelData.issue.automaticallyFixable); onTriggered: root.performStepAction(modelData) }
                            ThemedButton { theme: root.themeTokens; visible: modelData.id === "calibration" && modelData.optional; text: "USE DEFAULT RANGE"; tone: "secondary"; commandEnabled: root.backendObject; onTriggered: root.showActionResult(root.backendObject.skipCalibrationForSetup(root.summary.scopeId || "")) }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }

        ThemedButton { theme: root.themeTokens; Layout.alignment: Qt.AlignLeft; text: root.technicalDetailsExpanded ? "HIDE TECHNICAL DETAILS" : "VIEW TECHNICAL DETAILS"; tone: "secondary"; onTriggered: root.technicalDetailsExpanded = !root.technicalDetailsExpanded }
        Rectangle { visible: root.technicalDetailsExpanded; Layout.fillWidth: true; Layout.preferredHeight: visible ? technicalColumn.implicitHeight + 22 : 0; color: root.insetColor; border.color: root.borderColor; radius: root.radius
            ColumnLayout { id: technicalColumn; anchors.fill: parent; anchors.margins: 11; spacing: 5
                Text { text: "TECHNICAL DETAILS"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                Text { Layout.fillWidth: true; text: "ISSUE  ·  " + (root.primaryIssue.code || "None"); color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                Text { Layout.fillWidth: true; text: "TARGET  ·  " + (root.summary.scope || "HOTAS BF6"); color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                Text { visible: root.summary.scopeType === "application" || root.summary.scopeType === "deviceRig"; Layout.fillWidth: true; text: "CURRENT STATE  ·  " + (root.backendObject ? root.backendObject.controllerReadinessStatus : "Unavailable"); color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                Text { visible: root.summary.scopeType === "application" || root.summary.scopeType === "deviceRig"; Layout.fillWidth: true; text: "LAST CHECK  ·  " + (root.backendObject ? root.backendObject.controllerReadinessLastChecked : "Not recorded"); color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                Text { visible: root.summary.scopeType === "application" || root.summary.scopeType === "deviceRig"; Layout.fillWidth: true; text: "Virtual controller: " + (root.backendObject ? root.backendObject.activeOutputLayoutDescriptor : "Unavailable"); color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                Repeater { model: (root.summary.scopeType === "application" || root.summary.scopeType === "deviceRig") && root.backendObject ? root.backendObject.controllerReadinessChecks : []
                    delegate: Text { required property var modelData; Layout.fillWidth: true; text: modelData.name + "  ·  " + modelData.state + "\n" + modelData.message; color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                }
                Repeater { model: root.issues
                    delegate: Text { required property var modelData; Layout.fillWidth: true; text: modelData.technicalDetails; color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                }
                ThemedButton { theme: root.themeTokens; text: "COPY DIAGNOSTICS"; tone: "secondary"; Layout.alignment: Qt.AlignLeft
                    onTriggered: { const copied = root.backendObject && root.backendObject.copyControllerDiagnostics(); root.showActionResult({ success: copied, title: copied ? "Diagnostics copied" : "Diagnostics could not be copied", message: copied ? "The current setup evidence is ready to paste into a support request." : "Check the current setup state, then try copying diagnostics again." }) } }
            }
        }
    }

    Popup {
        id: fixConfirmation
        objectName: "setupAssistantFixConfirmation"
        parent: Overlay.overlay
        modal: true
        width: Math.min(560, root.width)
        anchors.centerIn: parent
        padding: 0
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: root.panelColor; border.color: root.warningColor; radius: root.radius }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            ThemedDialogHeader { Layout.fillWidth: true; theme: root.themeTokens; legacy: root.legacy; heading: "Fix setup?"; detail: "Review the managed change before it runs"; dialog: fixConfirmation }
            Text { Layout.fillWidth: true; text: "HOTAS BF6 will keep reading your physical controller, apply the recommended game-visibility change, and leave unrelated devices alone."; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: root.themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: fixConfirmation.close() }
                ThemedButton { theme: root.themeTokens; text: "APPLY FIX"; emphasis: "ready"; onTriggered: {
                    fixConfirmation.close()
                    root.showActionResult({ success: true, inProgress: true, title: "Applying game visibility...", message: "Checking and updating only the selected saved controller." })
                    Qt.callLater(function() { root.showActionResult(root.backendObject.applySetupAssistantIssueAction(root.primaryIssue.id)) })
                } }
            }
        }
    }

    // Retain the shared dialog contract used by release checks without
    // exposing a second visible workflow.
    Dialog { id: preservedDialogContract; visible: false; standardButtons: Dialog.NoButton }
}
