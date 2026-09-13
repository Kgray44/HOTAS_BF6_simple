import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// The one authoritative Setup Truth workflow. Presentation page is deliberately
// separate from the backend operation state so a background refresh can never
// throw a person out of their active CHECK -> REPAIR -> COMPLETE session.
Item {
    id: root
    objectName: "setupHealthAndRepair"
    property var backendObject
    property var themeTokens: null
    property bool legacy: false
    property bool activityMonitoring: false
    property bool useHostRepairConfirmation: false
    property bool showTitle: true
    property string presentationPage: "CHECK"
    property string observedSessionId: ""
    property var frozenCheckSnapshot: ({})
    property var activationFeedback: ({})
    property bool beforeAfterDiagnosticsVisible: false
    signal closeRequested()
    signal calibrationRequested()
    signal repairRequested()

    readonly property var snapshot: backendObject ? backendObject.setupTruthSnapshot : ({})
    readonly property var session: backendObject ? backendObject.setupRepairSession : ({})
    readonly property var progress: backendObject ? backendObject.setupRepairProgress : []
    readonly property string operationState: String(session.mode || "IDLE")
    readonly property bool checkRunning: operationState === "CHECKING"
    readonly property bool checkResults: operationState === "RESULTS"
    readonly property bool repairRunning: operationState === "REPAIRING"
                                        || operationState === "WAITING FOR USER"
                                        || operationState === "CHECKING FINAL STATE"
    readonly property bool terminal: operationState === "COMPLETE"
                                   || operationState === "FAILED"
                                   || operationState === "CANCELLED"
    readonly property var checkSnapshot: session.checkSnapshot && session.checkSnapshot.timestamp
                                         ? session.checkSnapshot
                                         : (frozenCheckSnapshot.timestamp ? frozenCheckSnapshot : snapshot)
    readonly property var finalSnapshot: session.afterSnapshot && session.afterSnapshot.timestamp
                                         ? session.afterSnapshot : snapshot
    readonly property var finalManualActions: finalSnapshot.manualActions || []
    readonly property var repairPlan: checkSnapshot.repairPlan || []
    readonly property bool hasRepair: repairPlan.length > 0
    readonly property bool repairOccurred: !!session.repairOccurred
    readonly property int repairedIssueCount: Number(session.repairedIssueCount || 0)
    readonly property int failedIssueCount: Number(session.failedIssueCount || 0)
    readonly property int remainingIssueCount: Number(session.remainingIssueCount || 0)
    readonly property color panelColor: token("panel", token("elevatedSurface", "#1a1d23"))
    readonly property color insetColor: token("panelInset", token("secondarySurface", "#10171b"))
    readonly property color borderColor: token("border", "#435660")
    readonly property color textColor: token("text", token("textPrimary", "#e8eeee"))
    readonly property color mutedColor: token("textMuted", token("textSecondary", "#9dafb4"))
    readonly property color readyColor: token("ready", token("healthy", "#8fd5c9"))
    readonly property color checkingColor: token("informational", token("cyan", "#72baf0"))
    readonly property color warningColor: token("warning", token("attention", "#d4ad69"))
    readonly property color dangerColor: token("danger", token("fault", "#ca9090"))
    readonly property int radius: token("panelRadius", token("radiusCard", 4))

    function token(name, fallback) { return themeTokens && themeTokens[name] !== undefined ? themeTokens[name] : fallback }
    function stateColor(status) {
        const normalized = String(status || "").toUpperCase()
        if (normalized === "READY" || normalized === "READY TO ACTIVATE" || normalized === "SUCCEEDED" || normalized === "COMPLETE") return readyColor
        if (normalized === "CHECKING" || normalized === "RUNNING") return checkingColor
        if (normalized === "PENDING" || normalized === "UNKNOWN / INSPECTION FAILED") return mutedColor
        if (normalized === "FAILED" || normalized === "UNAVAILABLE" || normalized === "CANCELLED") return dangerColor
        return warningColor
    }
    function groupFor(source, id) {
        const groups = (source && source.groups) || []
        for (let index = 0; index < groups.length; ++index) {
            if (String(groups[index].id || "") === id) return groups[index]
        }
        return ({})
    }
    function countReady(source) {
        const groups = (source && source.groups) || []
        let ready = 0
        for (let index = 0; index < groups.length; ++index) {
            if (String(groups[index].status || "") === "READY") ++ready
        }
        return ready
    }
    function currentStep() { return session.active && session.currentStep ? session.currentStep : ({}) }
    function sessionProgressPercent() {
        const value = Number(session.progressPercent)
        return isNaN(value) ? 0 : Math.max(0, Math.min(100, value))
    }
    function stageStatusLabel(status) {
        if (status === "WAITING FOR USER") return "WAITING FOR YOU"
        if (status === "SUCCEEDED") return "COMPLETE"
        if (status === "PENDING") return "QUEUED"
        return status || "QUEUED"
    }
    function stageMarker(status, order) {
        if (status === "SUCCEEDED") return "✓"
        if (status === "FAILED") return "!"
        if (status === "CANCELLED") return "×"
        if (status === "RUNNING") return "›"
        if (status === "WAITING FOR USER") return "?"
        return String(order || "•")
    }
    function checkedAge(source) {
        if (!source.timestamp) return "Not checked yet"
        const seconds = Math.max(0, Math.floor((Date.now() - Date.parse(source.timestamp)) / 1000))
        if (seconds < 5) return "Checked just now"
        if (seconds < 60) return "Checked " + seconds + " seconds ago"
        return "Checked " + Math.floor(seconds / 60) + " minute" + (Math.floor(seconds / 60) === 1 ? "" : "s") + " ago"
    }
    function checkCategories() {
        return [
            { id: "physical", title: "Physical controller", detail: "Read connected required controller input." },
            { id: "verification", title: "Controller identity", detail: "Confirm the exact saved identity and verification record." },
            { id: "vjoy", title: "Virtual output", detail: "Read the configured Device Rig vJoy descriptor." },
            { id: "isolation", title: "Device isolation", detail: "Read HidHide access, cloak, and controller visibility." },
            { id: "mapping", title: "Mapping", detail: "Confirm the selected Device Rig routing compiles." }
        ]
    }
    function checkCategoryStatus(category) {
        const group = groupFor(snapshot, category.id)
        if (checkRunning && String(group.status || "") === "CHECKING") return "CHECKING"
        if (checkResults) return String(groupFor(checkSnapshot, category.id).status || "PENDING")
        return "PENDING"
    }
    function wizardStepState(page) {
        if (page === "CHECK") return presentationPage === "CHECK" ? "CURRENT" : "COMPLETE"
        if (page === "REPAIR") {
            if (presentationPage === "REPAIR") return "CURRENT"
            if (presentationPage === "COMPLETE") return repairOccurred ? "COMPLETE" : "SKIPPED"
            return "UP NEXT"
        }
        return presentationPage === "COMPLETE" ? "CURRENT" : "UP NEXT"
    }
    function wizardStepMarker(page) {
        const state = wizardStepState(page)
        if (state === "COMPLETE") return "✓"
        if (state === "SKIPPED") return "—"
        return page === "CHECK" ? "1" : page === "REPAIR" ? "2" : "3"
    }
    function repairOperations(status) {
        const result = []
        const operations = session.repairOperations || []
        for (let index = 0; index < operations.length; ++index) {
            const operation = operations[index]
            if (String(operation.id || "").indexOf("repair:") !== 0) continue
            if (!status || String(operation.status || "") === status) result.push(operation)
        }
        return result
    }
    function beforeAfterDifferences() {
        const before = session.beforeSnapshot && session.beforeSnapshot.groups ? session.beforeSnapshot : checkSnapshot
        const after = finalSnapshot
        const result = []
        const afterGroups = after.groups || []
        for (let index = 0; index < afterGroups.length; ++index) {
            const current = afterGroups[index]
            const prior = groupFor(before, String(current.id || ""))
            if (prior.status && String(prior.status || "") !== String(current.status || "")) {
                result.push({ title: current.title || "Setup state", before: prior.status, after: current.status })
            }
        }
        return result
    }
    function completeHeadline() {
        if (!repairOccurred) return "SETUP CHECK COMPLETE"
        return String(finalSnapshot.overallStatus || "") === "READY"
            ? "SETUP REPAIR COMPLETE" : "SETUP REPAIR INCOMPLETE"
    }
    function completeDetail() {
        if (String(finalSnapshot.overallStatus || "") === "READY"
                && finalManualActions.length === 1
                && String(finalManualActions[0].kind || "") === "activateDeviceRig")
            return "Setup is healthy. 1 manual action remains: activate "
                + String(finalManualActions[0].rigName || "the selected Device Rig") + "."
        if (!repairOccurred && String(finalSnapshot.overallStatus || "") === "READY")
            return "Everything is ready. No repairs were required."
        if (repairOccurred && String(finalSnapshot.overallStatus || "") === "READY")
            return repairedIssueCount + " issue" + (repairedIssueCount === 1 ? "" : "s") + " repaired. A fresh final inspection passed."
        if (repairOccurred)
            return repairedIssueCount + " succeeded · " + failedIssueCount + " require" + (failedIssueCount === 1 ? "s" : "") + " attention."
        return "The check completed. Review the final system status and required manual action."
    }
    function beginNewSession() {
        presentationPage = "CHECK"
        observedSessionId = ""
        frozenCheckSnapshot = ({})
        beforeAfterDiagnosticsVisible = false
        activationFeedback = ({})
    }
    function startCheck() {
        beginNewSession()
        if (backendObject) backendObject.checkSetupHealth()
    }
    function continueWithoutRepair() {
        if (backendObject) backendObject.completeSetupCheck()
        presentationPage = "COMPLETE"
    }
    function continueToRepair() {
        frozenCheckSnapshot = checkSnapshot
        presentationPage = "REPAIR"
    }
    function approveRepair() {
        presentationPage = "REPAIR"
        if (useHostRepairConfirmation) repairRequested()
        else repairConfirmation.open()
    }
    function activateManualRig(action) {
        if (!backendObject || !action || !action.rigId) return
        activationFeedback = backendObject.activateSetupTruthDeviceRig(String(action.rigId))
        // A successful transaction is represented by the refreshed Current
        // System state. Retain only failures, which already carry the exact
        // resolver explanation rather than a generic banner.
        if (activationFeedback.success) activationFeedback = ({})
    }
    function synchronizePresentation() {
        const id = String(session.sessionId || "")
        if (id && id !== observedSessionId) {
            observedSessionId = id
            frozenCheckSnapshot = ({})
            beforeAfterDiagnosticsVisible = false
            presentationPage = "CHECK"
        }
        if (checkResults && checkSnapshot.timestamp) frozenCheckSnapshot = checkSnapshot
        if (repairRunning) presentationPage = "REPAIR"
        if (terminal) presentationPage = "COMPLETE"
    }

    Component.onCompleted: synchronizePresentation()
    onSessionChanged: synchronizePresentation()
    Connections {
        target: root.backendObject
        function onStateChanged() { root.synchronizePresentation() }
    }

    implicitWidth: 720
    implicitHeight: content.implicitHeight
    ColumnLayout {
        id: content
        width: parent.width
        spacing: 12

        Text {
            visible: root.showTitle
            text: "SETUP HEALTH & REPAIR"
            color: root.textColor
            font.pixelSize: 18
            font.bold: true
        }

        Rectangle {
            objectName: "setupWizardStepper"
            Layout.fillWidth: true
            Layout.preferredHeight: wizardSteps.implicitHeight + 20
            color: root.panelColor
            border.color: root.borderColor
            radius: root.radius
            RowLayout {
                id: wizardSteps
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                Repeater {
                    model: ["CHECK", "REPAIR", "COMPLETE"]
                    delegate: RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 7
                        Rectangle {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            radius: 12
                            color: root.wizardStepState(modelData) === "COMPLETE" ? root.readyColor
                                 : root.wizardStepState(modelData) === "CURRENT" ? root.checkingColor : root.insetColor
                            border.color: root.wizardStepState(modelData) === "CURRENT" ? root.checkingColor : root.borderColor
                            Text { anchors.centerIn: parent; text: root.wizardStepMarker(modelData); color: root.wizardStepState(modelData) === "CURRENT" || root.wizardStepState(modelData) === "COMPLETE" ? root.panelColor : root.mutedColor; font.pixelSize: 11; font.bold: true }
                        }
                        Text { text: modelData; color: root.wizardStepState(modelData) === "CURRENT" ? root.textColor : root.mutedColor; font.pixelSize: 10; font.bold: root.wizardStepState(modelData) === "CURRENT" }
                        Rectangle { visible: modelData !== "COMPLETE"; Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.wizardStepState(modelData) === "COMPLETE" ? root.readyColor : root.borderColor }
                    }
                }
            }
        }

        Item {
            visible: root.presentationPage === "CHECK"
            Layout.fillWidth: true
            Layout.preferredHeight: checkPage.implicitHeight
            ColumnLayout {
                id: checkPage
                width: parent.width
                spacing: 12
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: checkHeader.implicitHeight + 28
                    color: root.insetColor
                    border.color: root.checkRunning ? root.checkingColor : root.checkResults && root.hasRepair ? root.warningColor : root.readyColor
                    radius: root.radius
                    ColumnLayout {
                        id: checkHeader
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 5
                        Text { text: root.checkRunning ? "CHECKING SETUP" : root.checkResults ? "CHECK COMPLETE" : "CHECK YOUR SETUP"; color: root.checkRunning ? root.checkingColor : root.checkResults && root.hasRepair ? root.warningColor : root.readyColor; font.pixelSize: 20; font.bold: true }
                        Text { Layout.fillWidth: true; text: root.checkRunning ? "HOTAS BF6 is inspecting the selected Device Rig. Nothing is being changed." : root.checkResults ? (root.hasRepair ? "The check is frozen. Review the issues before deciding whether to repair them." : "Everything required by the selected Device Rig is ready.") : "HOTAS BF6 will inspect the complete selected Device Rig. Nothing will be changed during this check."; color: root.textColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        Text { visible: root.checkResults; text: root.checkedAge(root.checkSnapshot) + "  ·  " + String(root.checkSnapshot.timestamp || ""); color: root.mutedColor; font.pixelSize: 9 }
                    }
                }

                Rectangle {
                    visible: root.checkRunning
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? checkProgress.implicitHeight + 20 : 0
                    color: root.panelColor
                    border.color: root.checkingColor
                    radius: root.radius
                    ColumnLayout {
                        id: checkProgress
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "STEP " + Number(root.session.currentStepNumber || 1) + " OF " + Math.max(1, Number(root.session.totalStepCount || 1)); color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Text { text: root.sessionProgressPercent() + "%"; color: root.checkingColor; font.pixelSize: 10; font.bold: true }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 8; radius: 4; color: root.insetColor; clip: true; Rectangle { width: parent.width * root.sessionProgressPercent() / 100; height: parent.height; radius: parent.radius; color: root.checkingColor; Behavior on width { NumberAnimation { duration: 160 } } } }
                        Text { text: "CURRENT STEP"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                        Text { text: root.currentStep().title || "Reading complete setup"; color: root.textColor; font.pixelSize: 12; font.bold: true }
                        Text { Layout.fillWidth: true; text: root.currentStep().detail || "Reading the selected Device Rig without changing configuration."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: checkList.implicitHeight + 20
                    color: root.panelColor
                    border.color: root.borderColor
                    radius: root.radius
                    ColumnLayout {
                        id: checkList
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Text { text: root.checkRunning ? "INSPECTING" : root.checkResults ? "CHECK RESULTS" : "PLANNED INSPECTION"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                        Repeater {
                            model: root.checkCategories()
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: root.stageMarker(root.checkCategoryStatus(modelData), "•"); color: root.stateColor(root.checkCategoryStatus(modelData)); font.pixelSize: 14; font.bold: true }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: modelData.title; color: root.textColor; font.pixelSize: 10; font.bold: true }
                                    Text { visible: !root.checkResults; text: modelData.detail; color: root.mutedColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Text { visible: root.checkResults; text: String(root.groupFor(root.checkSnapshot, modelData.id).detail || modelData.detail); color: root.mutedColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                                Text { text: root.stageStatusLabel(root.checkCategoryStatus(modelData)); color: root.stateColor(root.checkCategoryStatus(modelData)); font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 120; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: root.checkResults && root.hasRepair
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? checkIssues.implicitHeight + 20 : 0
                    color: root.insetColor
                    border.color: root.warningColor
                    radius: root.radius
                    ColumnLayout {
                        id: checkIssues
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Text { text: root.countReady(root.checkSnapshot) + " ready  ·  " + root.repairPlan.length + " issue" + (root.repairPlan.length === 1 ? "" : "s") + " can be repaired"; color: root.warningColor; font.pixelSize: 11; font.bold: true }
                        Repeater {
                            model: root.repairPlan
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: issueDetail.implicitHeight + 14
                                color: root.panelColor
                                border.color: root.borderColor
                                radius: root.radius
                                ColumnLayout {
                                    id: issueDetail
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    spacing: 2
                                    Text { text: String(modelData.title || "Setup issue").toUpperCase(); color: root.textColor; font.pixelSize: 10; font.bold: true }
                                    Text { text: modelData.explanation || modelData.proposedRepair || "Action needed."; color: root.mutedColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ThemedButton { visible: !root.checkRunning && !root.checkResults; theme: root.themeTokens; text: "START CHECK"; emphasis: "ready"; commandEnabled: !!root.backendObject; onTriggered: root.startCheck() }
                    ThemedButton { visible: root.checkResults && root.hasRepair; theme: root.themeTokens; text: "CONTINUE TO REPAIR"; emphasis: "warning"; commandEnabled: true; onTriggered: root.continueToRepair() }
                    ThemedButton { visible: root.checkResults && !root.hasRepair; theme: root.themeTokens; text: "CONTINUE"; emphasis: "ready"; commandEnabled: true; onTriggered: root.continueWithoutRepair() }
                    ThemedButton { visible: root.checkResults; theme: root.themeTokens; text: "COPY FULL DIAGNOSTICS"; tone: "secondary"; commandEnabled: !!root.backendObject; onTriggered: root.backendObject.copySetupHealthDiagnostics() }
                }
            }
        }

        Item {
            visible: root.presentationPage === "REPAIR"
            Layout.fillWidth: true
            Layout.preferredHeight: repairPage.implicitHeight
            ColumnLayout {
                id: repairPage
                width: parent.width
                spacing: 12
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: repairHeader.implicitHeight + 28
                    color: root.insetColor
                    border.color: root.repairRunning ? root.checkingColor : root.warningColor
                    radius: root.radius
                    ColumnLayout {
                        id: repairHeader
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 5
                        Text { text: root.repairRunning ? "REPAIRING SETUP" : "REPAIR YOUR SETUP"; color: root.repairRunning ? root.checkingColor : root.warningColor; font.pixelSize: 20; font.bold: true }
                        Text { Layout.fillWidth: true; text: root.repairRunning ? "HOTAS BF6 is performing only the approved operations. You can watch every step below." : root.repairPlan.length + " issue" + (root.repairPlan.length === 1 ? " can" : "s can") + " be repaired automatically from the frozen check."; color: root.textColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    }
                }

                Rectangle {
                    visible: !root.repairRunning
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? repairPlanColumn.implicitHeight + 20 : 0
                    color: root.panelColor
                    border.color: root.warningColor
                    radius: root.radius
                    ColumnLayout {
                        id: repairPlanColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7
                        Text { text: "APPROVED REPAIR PLAN"; color: root.warningColor; font.pixelSize: 9; font.bold: true }
                        Repeater {
                            model: root.repairPlan
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: repairPlanDetail.implicitHeight + 16
                                color: root.insetColor
                                border.color: root.borderColor
                                radius: root.radius
                                ColumnLayout {
                                    id: repairPlanDetail
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 3
                                    Text { text: String(modelData.title || "Setup repair").toUpperCase(); color: root.textColor; font.pixelSize: 10; font.bold: true }
                                    Text { text: modelData.proposedRepair || modelData.explanation || "Approved scoped repair."; color: root.mutedColor; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Text { text: (modelData.requiresElevation ? "ADMINISTRATOR APPROVAL MAY BE REQUIRED" : "AUTOMATIC, SCOPED REPAIR") + (modelData.requiresReconnect ? "  ·  CONTROLLER RECONNECT REQUIRED" : ""); color: root.warningColor; font.pixelSize: 8; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: setupStageTimeline
                    objectName: "setupStageTimeline"
                    visible: root.repairRunning
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? repairTimeline.implicitHeight + 20 : 0
                    color: root.panelColor
                    border.color: root.checkingColor
                    radius: root.radius
                    ColumnLayout {
                        id: repairTimeline
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "REPAIR PROGRESS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Text { text: Number(root.session.completedStepCount || 0) + " OF " + Number(root.session.totalStepCount || 0) + " COMPLETE"; color: root.textColor; font.pixelSize: 9; font.bold: true }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 8; radius: 4; color: root.insetColor; clip: true; Rectangle { width: parent.width * root.sessionProgressPercent() / 100; height: parent.height; radius: parent.radius; color: root.checkingColor; Behavior on width { NumberAnimation { duration: 160 } } } }
                        Text { text: root.session.progressLabel || (root.sessionProgressPercent() + "%"); color: root.checkingColor; font.pixelSize: 10; font.bold: true }
                        Text { text: "CURRENT STEP"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                        Text { text: root.currentStep().title || "Preparing final setup inspection"; color: root.textColor; font.pixelSize: 12; font.bold: true }
                        Text { Layout.fillWidth: true; text: root.currentStep().detail || "Each operation is verified before final setup truth is published."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                        Text { visible: !!root.currentStep().requiresReconnect; text: "RECONNECT CONTROLLER"; color: root.warningColor; font.pixelSize: 10; font.bold: true }
                        Text { visible: !!root.currentStep().requiresReconnect; Layout.fillWidth: true; text: root.backendObject && root.backendObject.controllerDisconnectObserved ? "Disconnect observed. Reconnect the exact controller, then move a control." : "Disconnect the exact controller, reconnect it, then move a control."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                        Repeater {
                            model: root.session.steps || []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: root.stageMarker(String(modelData.status || "PENDING"), modelData.order); color: root.stateColor(String(modelData.status || "PENDING")); font.pixelSize: 14; font.bold: true }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: modelData.title || "Setup stage"; color: root.textColor; font.pixelSize: 10; font.bold: true }
                                    Text { text: modelData.detail || "Waiting to begin."; color: root.mutedColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                                Text { text: root.stageStatusLabel(String(modelData.status || "PENDING")); color: root.stateColor(String(modelData.status || "PENDING")); font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ThemedButton { visible: !root.repairRunning; theme: root.themeTokens; text: "BACK TO CHECK RESULTS"; tone: "secondary"; commandEnabled: true; onTriggered: root.presentationPage = "CHECK" }
                    ThemedButton { visible: !root.repairRunning; theme: root.themeTokens; text: "REPAIR " + root.repairPlan.length + " ISSUE" + (root.repairPlan.length === 1 ? "" : "S"); emphasis: "warning"; commandEnabled: root.repairPlan.length > 0; onTriggered: root.approveRepair() }
                    ThemedButton { theme: root.themeTokens; text: "COPY FULL DIAGNOSTICS"; tone: "secondary"; commandEnabled: !!root.backendObject; onTriggered: root.backendObject.copySetupHealthDiagnostics() }
                }
            }
        }

        Item {
            visible: root.presentationPage === "COMPLETE"
            Layout.fillWidth: true
            Layout.preferredHeight: completePage.implicitHeight
            ColumnLayout {
                id: completePage
                width: parent.width
                spacing: 12
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: completeHeader.implicitHeight + 28
                    color: root.insetColor
                    border.color: String(root.finalSnapshot.overallStatus || "") === "READY" ? root.readyColor : root.warningColor
                    radius: root.radius
                    ColumnLayout {
                        id: completeHeader
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 5
                        Text { text: root.completeHeadline(); color: String(root.finalSnapshot.overallStatus || "") === "READY" ? root.readyColor : root.warningColor; font.pixelSize: 20; font.bold: true }
                        Text { Layout.fillWidth: true; text: root.completeDetail(); color: root.textColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        Text { visible: !!root.finalSnapshot.timestamp; text: root.checkedAge(root.finalSnapshot) + "  ·  " + root.finalSnapshot.timestamp; color: root.mutedColor; font.pixelSize: 9 }
                    }
                }

                Rectangle {
                    visible: root.repairOccurred
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? repairedColumn.implicitHeight + 20 : 0
                    color: root.panelColor
                    border.color: root.readyColor
                    radius: root.radius
                    ColumnLayout {
                        id: repairedColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7
                        Text { text: "WHAT WAS REPAIRED"; color: root.readyColor; font.pixelSize: 10; font.bold: true }
                        Repeater {
                            model: root.repairOperations("SUCCEEDED")
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: "✓  " + String(modelData.subsystem || modelData.title || "Setup repair"); color: root.textColor; font.pixelSize: 10; font.bold: true }
                                Text { text: modelData.detail || "Verified by final read-back."; color: root.mutedColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                        Text { visible: root.repairedIssueCount === 0; text: "No repair completed successfully in this session."; color: root.mutedColor; font.pixelSize: 9 }
                    }
                }

                Rectangle {
                    visible: root.failedIssueCount > 0 || (root.repairOccurred && root.remainingIssueCount > 0)
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? attentionColumn.implicitHeight + 20 : 0
                    color: root.insetColor
                    border.color: root.warningColor
                    radius: root.radius
                    ColumnLayout {
                        id: attentionColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Text { text: "WHAT STILL NEEDS ATTENTION"; color: root.warningColor; font.pixelSize: 10; font.bold: true }
                        Repeater {
                            model: root.repairOperations("FAILED")
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: "✕  " + String(modelData.subsystem || modelData.title || "Setup repair"); color: root.textColor; font.pixelSize: 10; font.bold: true }
                                Text { text: modelData.detail || "Review diagnostics for the required manual action."; color: root.mutedColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                        Repeater {
                            model: root.finalSnapshot.issues || []
                            delegate: Text { required property var modelData; visible: root.failedIssueCount === 0; text: "• " + String(modelData.title || "Setup issue") + " — " + String(modelData.explanation || "Review diagnostics."); color: root.mutedColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: finalSystem.implicitHeight + 20
                    color: root.panelColor
                    border.color: root.borderColor
                    radius: root.radius
                    ColumnLayout {
                        id: finalSystem
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Text { text: "CURRENT SYSTEM"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                        Repeater {
                            model: root.finalSnapshot.groups || []
                            delegate: ColumnLayout {
                                required property var modelData
                                readonly property var manualAction: modelData.evidence && modelData.evidence.manualAction
                                                                    ? modelData.evidence.manualAction : ({})
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: root.stageMarker(String(modelData.status || "PENDING"), "•"); color: root.stateColor(String(modelData.status || "PENDING")); font.pixelSize: 13; font.bold: true }
                                    Text { text: modelData.title || "Setup group"; color: root.textColor; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                                    Text { text: String(modelData.status || "CHECKING"); color: root.stateColor(String(modelData.status || "CHECKING")); font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 145; horizontalAlignment: Text.AlignRight; wrapMode: Text.WordWrap }
                                }
                                Text {
                                    visible: String(modelData.status || "READY") !== "READY" && !!modelData.detail
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 21
                                    text: String(modelData.detail || "")
                                    color: root.mutedColor
                                    font.pixelSize: 9
                                    wrapMode: Text.WordWrap
                                }
                                ThemedButton {
                                    objectName: "completePageActivateRigButton"
                                    visible: String(parent.manualAction.kind || "") === "activateDeviceRig"
                                    Layout.leftMargin: 21
                                    theme: root.themeTokens
                                    text: "ACTIVATE " + String(parent.manualAction.rigName || "DEVICE RIG").toUpperCase()
                                    emphasis: "ready"
                                    commandEnabled: !!root.backendObject
                                    onTriggered: root.activateManualRig(parent.manualAction)
                                }
                            }
                        }
                        Rectangle {
                            visible: !!root.activationFeedback.title && !root.activationFeedback.success
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            implicitHeight: activationFailure.implicitHeight + 16
                            color: Qt.rgba(root.dangerColor.r, root.dangerColor.g, root.dangerColor.b, 0.12)
                            border.color: root.dangerColor
                            radius: root.radius
                            ColumnLayout {
                                id: activationFailure
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 3
                                Text { text: String(root.activationFeedback.title || "Device Rig was not activated"); color: root.dangerColor; font.pixelSize: 10; font.bold: true }
                                Text { text: String(root.activationFeedback.message || root.activationFeedback.detail || "Review the activation details."); color: root.textColor; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: root.beforeAfterDifferences().length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? differencesColumn.implicitHeight + 20 : 0
                    color: root.panelColor
                    border.color: root.borderColor
                    radius: root.radius
                    ColumnLayout {
                        id: differencesColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5
                        Text { text: "BEFORE → AFTER"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                        Repeater {
                            model: root.beforeAfterDifferences()
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text { text: modelData.title; color: root.textColor; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                                Text { text: modelData.before + " → " + modelData.after; color: root.readyColor; font.pixelSize: 9; font.bold: true }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: root.beforeAfterDiagnosticsVisible
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? diagnosticsColumn.implicitHeight + 20 : 0
                    color: root.insetColor
                    border.color: root.borderColor
                    radius: root.radius
                    ColumnLayout {
                        id: diagnosticsColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Text { text: "BEFORE / AFTER DIAGNOSTICS"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                        Text { text: "BEFORE\n" + String((root.session.beforeSnapshot || root.checkSnapshot).diagnostics || "No before diagnostic was recorded."); color: root.mutedColor; font.family: "Consolas"; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                        Text { text: "AFTER\n" + String(root.finalSnapshot.diagnostics || "No after diagnostic was recorded."); color: root.mutedColor; font.family: "Consolas"; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    ThemedButton { theme: root.themeTokens; text: "DONE"; emphasis: "ready"; commandEnabled: true; onTriggered: root.closeRequested() }
                    ThemedButton { theme: root.themeTokens; text: "COPY REPAIR REPORT"; tone: "secondary"; commandEnabled: !!root.backendObject; onTriggered: root.backendObject.copySetupHealthDiagnostics() }
                    ThemedButton { theme: root.themeTokens; text: root.beforeAfterDiagnosticsVisible ? "HIDE BEFORE / AFTER" : "VIEW BEFORE / AFTER DIAGNOSTICS"; tone: "secondary"; commandEnabled: true; onTriggered: root.beforeAfterDiagnosticsVisible = !root.beforeAfterDiagnosticsVisible }
                    ThemedButton { theme: root.themeTokens; text: "RUN ANOTHER CHECK"; tone: "secondary"; commandEnabled: !!root.backendObject; onTriggered: root.startCheck() }
                }
            }
        }
    }

    Dialog {
        id: repairConfirmation
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        title: ""
        standardButtons: Dialog.NoButton
        padding: 16
        width: Math.min(560, Math.max(320, (parent ? parent.width : 560) - 36))
        height: Math.min(implicitHeight, Math.max(260, (parent ? parent.height : 650) - 36))
        header: ThemedDialogHeader {
            id: repairConfirmationHeader
            theme: root.themeTokens
            legacy: root.legacy
            heading: "Repair setup"
            dialog: repairConfirmation
        }
        background: Rectangle { color: root.panelColor; border.color: root.warningColor; radius: root.radius }
        contentItem: ColumnLayout {
            width: parent.width
            spacing: 10
            Text { Layout.fillWidth: true; text: "HOTAS BF6 will apply only the frozen scoped repair plan, then perform a fresh complete read-back. Windows may ask for administrator approval."; color: root.textColor; wrapMode: Text.WordWrap; font.pixelSize: 11 }
            Text { Layout.fillWidth: true; text: root.repairPlan.length + " approved repair" + (root.repairPlan.length === 1 ? " is" : "s are") + " ready. Unrelated HidHide rules and the current mapping choice are preserved."; color: root.mutedColor; wrapMode: Text.WordWrap; font.pixelSize: 10 }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ThemedButton { theme: root.themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: repairConfirmation.close() }
                ThemedButton { theme: root.themeTokens; text: "REPAIR " + root.repairPlan.length + " ISSUE" + (root.repairPlan.length === 1 ? "" : "S"); emphasis: "warning"; onTriggered: { repairConfirmation.close(); root.backendObject.repairSetupHealth() } }
            }
        }
    }
}
