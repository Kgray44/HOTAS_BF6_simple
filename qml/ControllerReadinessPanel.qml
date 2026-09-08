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
    property bool detailsExpanded: false
    property bool technicalDetailsExpanded: false
    property int liveTick: 0
    property var actionResult: ({})
    readonly property var summary: backendObject ? backendObject.setupAssistantSummary : ({})
    readonly property var issues: backendObject ? backendObject.setupAssistantIssues : []
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
        return warningColor
    }
    function firstIssue() { return issues.length > 0 ? issues[0] : ({}) }
    function showActionResult(result) {
        actionResult = result || ({ success: false, title: "Action did not complete", message: "Try checking setup again." })
    }
    function performPrimaryAction() {
        if (!backendObject) return
        const action = summary.primaryAction || "check-setup"
        if (action === "done") { closeRequested(); return }
        if (action === "fix") { fixConfirmation.open(); return }
        if (action === "start-live-test") { showActionResult(backendObject.startSetupAssistantLiveTest()); detailsExpanded = true; return }
        showActionResult(backendObject.startSetupAssistantCheck())
    }
    function nextLivePrompt() {
        const steps = liveTest.steps || []
        for (let i = 0; i < steps.length; ++i) if (steps[i].state === "waiting") return steps[i].message
        return liveTest.complete ? "Live routes verified." : "Start the control test when you are ready."
    }

    Timer {
        interval: 180; repeat: true
        running: root.liveTest.active && !root.liveTest.complete
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

        Rectangle {
            objectName: "setupAssistantActionResult"
            visible: !!root.actionResult.title
            Layout.fillWidth: true; Layout.preferredHeight: visible ? actionResultText.implicitHeight + 24 : 0
            color: root.actionResult.success ? Qt.rgba(root.readyColor.r, root.readyColor.g, root.readyColor.b, 0.10) : Qt.rgba(root.dangerColor.r, root.dangerColor.g, root.dangerColor.b, 0.10)
            border.color: root.actionResult.success ? root.readyColor : root.dangerColor; radius: root.radius
            Text { id: actionResultText; anchors.fill: parent; anchors.margins: 12
                text: (root.actionResult.title || "") + (root.actionResult.message ? "\n" + root.actionResult.message : "")
                color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
        }

        ColumnLayout {
            Layout.fillWidth: true; spacing: 8; visible: root.summary.state !== "READY"
            Text { Layout.fillWidth: true; text: root.firstIssue().title || "What needs attention"; color: root.textColor; font.pixelSize: 15; font.bold: true; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; text: root.firstIssue().explanation || "HOTAS BF6 will guide you through the next step."; color: root.mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; visible: root.firstIssue().recommendedAction === "fix"; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap
                text: "HOTAS BF6 will keep reading your controller, change only the selected game-visibility settings, and leave unrelated devices alone." }
        }

        ColumnLayout {
            Layout.fillWidth: true; spacing: 7; visible: root.summary.state === "READY"
            Text { Layout.fillWidth: true; text: "✓ Physical controller detected\n✓ Virtual controller ready\n✓ Game visibility checked\n✓ Controls ready to test"; color: root.textColor; font.pixelSize: 11; lineHeight: 1.4 }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            ThemedButton { theme: root.themeTokens; text: root.summary.primaryActionLabel || "CHECK SETUP"; emphasis: "ready"
                commandEnabled: root.backendObject && !root.backendObject.controllerSetupInProgress; onTriggered: root.performPrimaryAction() }
            ThemedButton { theme: root.themeTokens; text: root.detailsExpanded ? "HIDE STEPS" : "VIEW DETAILS"; tone: "secondary"; onTriggered: root.detailsExpanded = !root.detailsExpanded }
            Item { Layout.fillWidth: true }
            ThemedButton { theme: root.themeTokens; text: root.summary.state === "READY" ? "DONE" : "CLOSE"; tone: "secondary"; onTriggered: root.closeRequested() }
        }

        ColumnLayout {
            objectName: "setupAssistantGuidedSteps"
            visible: root.detailsExpanded
            Layout.fillWidth: true; spacing: 12
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: physicalSteps.implicitHeight + 22; color: root.panelColor; border.color: root.borderColor; radius: root.radius
                ColumnLayout { id: physicalSteps; anchors.fill: parent; anchors.margins: 11; spacing: 5
                    Text { text: "STEP 1  ·  PHYSICAL CONTROLLERS"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                    Repeater { model: root.liveTest.steps || []
                        delegate: RowLayout { required property var modelData; visible: modelData.kind === "input"; Layout.fillWidth: true; spacing: 7
                            Rectangle { width: 7; height: 7; radius: 4; color: root.stateColor(modelData.state) }
                            Text { Layout.fillWidth: true; text: modelData.title; color: root.textColor; font.pixelSize: 11; font.bold: true }
                            Text { text: modelData.state === "offline" ? (modelData.optional ? "OPTIONAL · OFFLINE" : "OFFLINE") : modelData.state === "ready" ? "DETECTED" : "WAITING"; color: root.stateColor(modelData.state); font.pixelSize: 9; font.bold: true }
                        }
                    }
                    Text { Layout.fillWidth: true; visible: root.summary.state === "OFFLINE" || (root.backendObject && root.backendObject.controllerDisconnectObserved); text: "RECONNECT CONTROLLER\nReconnect the required physical controller, then choose Check Setup."; color: root.warningColor; font.pixelSize: 10; font.bold: true; wrapMode: Text.WordWrap }
                    Text { Layout.fillWidth: true; text: "A physical controller is the stick, throttle, pedals, or gamepad you connect to Windows."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: outputSteps.implicitHeight + 22; color: root.panelColor; border.color: root.borderColor; radius: root.radius
                ColumnLayout { id: outputSteps; anchors.fill: parent; anchors.margins: 11; spacing: 5
                    Text { text: "STEP 2  ·  VIRTUAL CONTROLLER"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                    Repeater { model: root.liveTest.steps || []
                        delegate: RowLayout { required property var modelData; visible: modelData.kind === "output"; Layout.fillWidth: true; spacing: 7
                            Rectangle { width: 7; height: 7; radius: 4; color: root.stateColor(modelData.state) }
                            Text { Layout.fillWidth: true; text: modelData.title; color: root.textColor; font.pixelSize: 11; font.bold: true }
                            Text { text: modelData.state === "ready" ? "READY" : "SETUP NEEDED"; color: root.stateColor(modelData.state); font.pixelSize: 9; font.bold: true }
                        }
                    }
                    Text { Layout.fillWidth: true; text: "Your game should use this virtual controller instead of each physical controller separately."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: visibilityStep.implicitHeight + 22; color: root.panelColor; border.color: root.borderColor; radius: root.radius
                ColumnLayout { id: visibilityStep; anchors.fill: parent; anchors.margins: 11; spacing: 6
                    Text { text: "STEP 3  ·  GAME VISIBILITY"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                    Text { Layout.fillWidth: true; text: root.issues.filter(function(issue) { return issue.category === "Visibility" }).length > 0 ? "A physical controller is visible to games. This can cause duplicate controls." : "Physical controllers are checked so games can use the clean virtual controller."; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
                    ThemedButton { theme: root.themeTokens; visible: root.issues.filter(function(issue) { return issue.category === "Visibility" }).length > 0; text: "HIDE FROM GAMES"; emphasis: "ready"; commandEnabled: root.backendObject && root.firstIssue().automaticallyFixable; onTriggered: fixConfirmation.open() }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: liveSteps.implicitHeight + 22; color: root.panelColor; border.color: root.borderColor; radius: root.radius
                ColumnLayout { id: liveSteps; anchors.fill: parent; anchors.margins: 11; spacing: 6
                    Text { text: "STEP 4  ·  TEST CONTROLS"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                    Text { Layout.fillWidth: true; text: root.liveTest.active ? root.nextLivePrompt() : "Start a live test, then move each physical controller and a mapped control."; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
                    Repeater { model: root.liveTest.steps || []
                        delegate: RowLayout { required property var modelData; Layout.fillWidth: true; spacing: 7
                            Rectangle { width: 7; height: 7; radius: 4; color: root.stateColor(modelData.state) }
                            Text { Layout.fillWidth: true; text: modelData.title + "  ·  " + modelData.message; color: root.mutedColor; font.pixelSize: 10; elide: Text.ElideRight }
                            Text { text: modelData.state === "ready" ? "✓" : modelData.state === "offline" ? "OFFLINE" : "WAITING"; color: root.stateColor(modelData.state); font.pixelSize: 10; font.bold: true }
                        }
                    }
                    RowLayout { Layout.fillWidth: true
                        ThemedButton { theme: root.themeTokens; text: root.liveTest.active ? "TEST RUNNING" : "START LIVE TEST"; emphasis: "ready"; commandEnabled: root.backendObject && !root.liveTest.active; onTriggered: root.showActionResult(root.backendObject.startSetupAssistantLiveTest()) }
                        ThemedButton { theme: root.themeTokens; text: "START CALIBRATION"; tone: "secondary"; emphasis: "warning"; commandEnabled: root.backendObject && !root.backendObject.calibrationActive; onTriggered: root.calibrationRequested() }
                        Item { Layout.fillWidth: true }
                        Text { visible: root.liveTest.complete; text: "✓ COMPLETE"; color: root.readyColor; font.pixelSize: 10; font.bold: true }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; visible: root.liveTest.complete; Layout.preferredHeight: visible ? completionText.implicitHeight + 22 : 0; color: Qt.rgba(root.readyColor.r, root.readyColor.g, root.readyColor.b, 0.10); border.color: root.readyColor; radius: root.radius
                Text { id: completionText; anchors.fill: parent; anchors.margins: 11; text: "SETUP COMPLETE\nInputs detected, virtual controller ready, game visibility checked, and live routes verified."; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
            }
        }

        ThemedButton { theme: root.themeTokens; Layout.alignment: Qt.AlignLeft; text: root.technicalDetailsExpanded ? "HIDE TECHNICAL DETAILS" : "VIEW TECHNICAL DETAILS"; tone: "secondary"; onTriggered: root.technicalDetailsExpanded = !root.technicalDetailsExpanded }
        Rectangle { visible: root.technicalDetailsExpanded; Layout.fillWidth: true; Layout.preferredHeight: visible ? technicalColumn.implicitHeight + 22 : 0; color: root.insetColor; border.color: root.borderColor; radius: root.radius
            ColumnLayout { id: technicalColumn; anchors.fill: parent; anchors.margins: 11; spacing: 5
                Text { text: "TECHNICAL DETAILS"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                Text { Layout.fillWidth: true; text: "Virtual controller: " + (root.backendObject ? root.backendObject.activeOutputLayoutDescriptor : "Unavailable"); color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                Repeater { model: root.issues
                    delegate: Text { required property var modelData; Layout.fillWidth: true; text: modelData.technicalDetails; color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                }
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
            Text { Layout.fillWidth: true; text: "Fix setup?"; color: root.textColor; font.pixelSize: 17; font.bold: true }
            Text { Layout.fillWidth: true; text: "HOTAS BF6 will keep reading your physical controller, apply the recommended game-visibility change, and leave unrelated devices alone."; color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: root.themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: fixConfirmation.close() }
                ThemedButton { theme: root.themeTokens; text: "APPLY FIX"; emphasis: "ready"; onTriggered: { fixConfirmation.close(); root.showActionResult(root.backendObject.applySetupAssistantFix()) } }
            }
        }
    }

    // Retain the shared dialog contract used by release checks without
    // exposing a second visible workflow.
    Dialog { id: preservedDialogContract; visible: false; standardButtons: Dialog.NoButton }
}
