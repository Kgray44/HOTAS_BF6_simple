import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// The single Devices setup surface. It renders the typed Setup Truth snapshot
// and never infers driver state or repair eligibility from display text.
Item {
    id: root
    objectName: "setupHealthAndRepair"
    property var backendObject
    property var themeTokens: null
    property bool legacy: false
    property bool activityMonitoring: false
    // Hosts with an established native confirmation surface (Flight Deck)
    // retain their own chrome; Standard and Legacy use the fully themed
    // shared confirmation below.
    property bool useHostRepairConfirmation: false
    property bool showTitle: true
    signal closeRequested()
    signal calibrationRequested()
    signal repairRequested()
    readonly property var snapshot: backendObject ? backendObject.setupTruthSnapshot : ({})
    readonly property var session: backendObject ? backendObject.setupRepairSession : ({})
    readonly property var progress: backendObject ? backendObject.setupRepairProgress : []
    function token(name, fallback) { return themeTokens && themeTokens[name] !== undefined ? themeTokens[name] : fallback }
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

    function stateColor(status) {
        if (status === "READY" || status === "SUCCEEDED") return readyColor
        if (status === "CHECKING") return checkingColor
        if (status === "PENDING") return mutedColor
        if (status === "UNKNOWN / INSPECTION FAILED") return mutedColor
        if (status === "FAILED" || status === "UNAVAILABLE") return dangerColor
        return warningColor
    }
    function hasRepair() { return (snapshot.repairPlan || []).length > 0 }
    function modeHeading() {
        if (session.mode === "CHECKING") return "CHECKING YOUR SETUP"
        if (session.mode === "CHECKING FINAL STATE") return "VERIFYING FINAL SETUP STATE"
        if (session.mode === "RESULTS") return "SETUP CHECK COMPLETE"
        if (session.mode === "REPAIRING") return "REPAIRING SETUP"
        if (session.mode === "WAITING FOR USER") return "WAITING FOR YOU"
        if (session.mode === "COMPLETE") return "SETUP REPAIRED"
        if (session.mode === "FAILED") return "SETUP NEEDS ATTENTION"
        if (session.mode === "CANCELLED") return "SETUP REPAIR CANCELLED"
        return snapshot.overallStatus || "CHECKING"
    }
    function modeDetail() {
        if (session.mode === "CHECKING") return "Reading the exact controller, Device Rig outputs, HidHide, and mapping without changing configuration."
        if (session.mode === "CHECKING FINAL STATE") return "Performing a new full read-back. The final state is not inferred from the before snapshot."
        if (session.mode === "RESULTS") return Number(session.completedStepCount || 0) + " checks completed. These frozen results are the authority for repair."
        if (session.mode === "REPAIRING") return "Applying only the approved setup repairs and recording every result."
        if (session.mode === "WAITING FOR USER") return "The current repair is paused for your required confirmation or reconnect action."
        if (session.mode === "COMPLETE") return "A fresh final inspection confirmed the repaired setup."
        if (session.mode === "FAILED") return "A fresh final inspection found remaining setup work. Review the completed and failed steps below."
        if (session.mode === "CANCELLED") return "The approved repair was cancelled; the final read-back below remains authoritative."
        return snapshot.rigName ? ("Complete setup truth for " + snapshot.rigName) : "Check physical input, saved verification, virtual output, isolation, and mapping."
    }
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
    function currentStep() {
        // Results are intentionally frozen and have no current operation.
        // This prevents a completed log entry from masquerading as a spinner.
        return session.active && session.currentStep ? session.currentStep : ({})
    }
    function checkedAge() {
        if (!snapshot.timestamp) return "Not checked yet"
        const seconds = Math.max(0, Math.floor((Date.now() - Date.parse(snapshot.timestamp)) / 1000))
        if (seconds < 5) return "Checked just now"
        if (seconds < 60) return "Checked " + seconds + " seconds ago"
        return "Checked " + Math.floor(seconds / 60) + " minute" + (Math.floor(seconds / 60) === 1 ? "" : "s") + " ago"
    }

    implicitWidth: 720
    implicitHeight: content.implicitHeight
    ColumnLayout {
        id: content
        width: parent.width
        spacing: 12
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: titleColumn.implicitHeight + 30
            color: root.insetColor; border.color: root.stateColor(root.snapshot.overallStatus || "CHECKING"); radius: root.radius
            ColumnLayout {
                id: titleColumn; anchors.fill: parent; anchors.margins: 15; spacing: 5
                Text { visible: root.showTitle; text: "SETUP HEALTH & REPAIR"; color: root.mutedColor; font.pixelSize: 10; font.bold: true }
                Text { text: root.modeHeading(); color: root.stateColor(root.snapshot.overallStatus || "CHECKING"); font.pixelSize: 20; font.bold: true }
                Text { Layout.fillWidth: true; text: root.modeDetail(); color: root.textColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                Text { visible: !root.session.active && !!root.snapshot.timestamp; text: root.checkedAge() + "  ·  " + root.snapshot.timestamp; color: root.mutedColor; font.pixelSize: 9 }
            }
        }
        Rectangle {
            visible: !!root.session.active && root.currentStep().title !== undefined
            Layout.fillWidth: true; Layout.preferredHeight: visible ? progressColumn.implicitHeight + 20 : 0
            color: root.panelColor; border.color: root.stateColor(root.currentStep().status || "RUNNING"); radius: root.radius
            ColumnLayout {
                id: progressColumn; anchors.fill: parent; anchors.margins: 10; spacing: 3
                Text { text: root.session.mode === "WAITING FOR USER" ? "WAITING FOR YOU" : "CURRENT STEP"; color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                Text { text: root.currentStep().title || "Checking setup"; color: root.textColor; font.pixelSize: 12; font.bold: true }
                Text { Layout.fillWidth: true; text: root.currentStep().detail || ""; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                Text { visible: !!root.currentStep().requiresReconnect; text: "RECONNECT CONTROLLER"; color: root.warningColor; font.pixelSize: 10; font.bold: true }
                Text { visible: !!root.currentStep().requiresReconnect; text: root.backendObject && root.backendObject.controllerDisconnectObserved ? "Disconnect observed. Reconnect the exact controller and move a control." : "Disconnect the exact controller, reconnect it, then move a control."; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }
        }
        Rectangle {
            id: setupStageTimeline
            objectName: "setupStageTimeline"
            readonly property int total: Number(root.session.totalStepCount || root.session.stepCount || 0)
            readonly property int completed: Number(root.session.completedStepCount || 0)
            visible: total > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? stageTimelineColumn.implicitHeight + 20 : 0
            color: root.panelColor
            border.color: root.session.active ? root.stateColor(root.currentStep().status || "RUNNING") : root.borderColor
            border.width: root.session.active ? 2 : 1
            radius: root.radius
            ColumnLayout {
                id: stageTimelineColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: root.session.active ? "STAGE PROGRESS" : (root.session.mode === "RESULTS" ? "CHECK RESULTS" : "REPAIR RESULTS"); color: root.mutedColor; font.pixelSize: 9; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Text { text: setupStageTimeline.completed + " OF " + setupStageTimeline.total + " COMPLETE"; color: root.textColor; font.pixelSize: 9; font.bold: true }
                }
                Rectangle {
                    id: setupStageProgress
                    objectName: "setupStageProgress"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    color: root.insetColor
                    border.color: root.borderColor
                    radius: height / 2
                    clip: true
                    Rectangle {
                        width: parent.width * root.sessionProgressPercent() / 100
                        height: parent.height
                        color: root.session.active ? root.stateColor(root.currentStep().status || "RUNNING")
                                                   : root.stateColor(root.session.result || "SUCCEEDED")
                        radius: parent.radius
                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    }
                }
                Text {
                    objectName: "setupProgressPercent"
                    Layout.fillWidth: true
                    text: root.session.active
                        ? (root.session.progressLabel || ("Stage 1 of " + setupStageTimeline.total + " · " + root.sessionProgressPercent() + "%"))
                        : (setupStageTimeline.completed + " completed checks")
                    color: root.checkingColor
                    font.pixelSize: 10
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: root.session.active
                        ? "Progress is based on completed setup stages, not an estimate of elapsed time."
                        : "No step is active. These are the completed results from this setup session."
                    color: root.mutedColor
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: root.session.steps || []
                    delegate: Rectangle {
                        required property var modelData
                        readonly property string status: modelData.status || modelData.state || "PENDING"
                        readonly property bool current: !!modelData.current
                        Layout.fillWidth: true
                        Layout.preferredHeight: stageRow.implicitHeight + 14
                        color: current ? root.insetColor : root.panelColor
                        border.color: current ? root.stateColor(status) : root.borderColor
                        border.width: current ? 2 : 1
                        radius: root.radius
                        opacity: status === "PENDING" ? 0.78 : 1.0
                        RowLayout {
                            id: stageRow
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 8
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 12
                                color: root.stateColor(status)
                                Text {
                                    anchors.centerIn: parent
                                    text: root.stageMarker(status, modelData.order)
                                    color: root.panelColor
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { Layout.fillWidth: true; text: modelData.title || "Setup stage"; color: root.textColor; font.pixelSize: 10; font.bold: true; wrapMode: Text.WordWrap }
                                Text { Layout.fillWidth: true; text: modelData.detail || "Waiting to begin."; color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
                                Text { visible: !!modelData.requiresElevation; text: "ADMINISTRATOR APPROVAL MAY BE REQUIRED"; color: root.warningColor; font.pixelSize: 8; font.bold: true }
                                Text { visible: !!modelData.requiresReconnect; text: "CONTROLLER RECONNECT REQUIRED"; color: root.warningColor; font.pixelSize: 8; font.bold: true }
                            }
                            Text {
                                Layout.alignment: Qt.AlignTop | Qt.AlignRight
                                Layout.preferredWidth: 106
                                text: root.stageStatusLabel(status)
                                color: root.stateColor(status)
                                font.pixelSize: 8
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
        Repeater {
            model: root.snapshot.groups || []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true; Layout.preferredHeight: groupRow.implicitHeight + 18
                color: root.panelColor; border.color: root.borderColor; radius: root.radius
                RowLayout {
                    id: groupRow; anchors.fill: parent; anchors.margins: 9; spacing: 9
                    Rectangle { width: 8; height: 8; radius: 4; color: root.stateColor(modelData.status) }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Text { text: modelData.title; color: root.textColor; font.pixelSize: 11; font.bold: true }
                        Text { Layout.fillWidth: true; text: modelData.detail; color: root.mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    }
                    Text { text: modelData.status; color: root.stateColor(modelData.status); font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight; wrapMode: Text.WordWrap; Layout.preferredWidth: 150 }
                }
            }
        }
        Rectangle {
            visible: (root.snapshot.issues || []).length > 0
            Layout.fillWidth: true; Layout.preferredHeight: visible ? issueColumn.implicitHeight + 20 : 0
            color: root.insetColor; border.color: root.warningColor; radius: root.radius
            ColumnLayout {
                id: issueColumn; anchors.fill: parent; anchors.margins: 10; spacing: 6
                Text { text: "REPAIR PLAN"; color: root.warningColor; font.pixelSize: 10; font.bold: true }
                Repeater { model: root.snapshot.issues || []
                    delegate: Text { required property var modelData; Layout.fillWidth: true; text: "• " + modelData.title + " — " + modelData.explanation; color: root.textColor; font.pixelSize: 10; wrapMode: Text.WordWrap }
                }
            }
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            ThemedButton { theme: root.themeTokens; text: "↻"; compact: true; tone: "secondary"; commandEnabled: root.backendObject && !root.backendObject.setupRepairSessionActive; onTriggered: root.backendObject.checkSetupHealth() }
            ThemedButton { theme: root.themeTokens; text: root.hasRepair() ? "REPAIR " + root.snapshot.repairPlan.length + " ISSUE" + (root.snapshot.repairPlan.length === 1 ? "" : "S") : "CHECK & REPAIR SETUP"; emphasis: root.hasRepair() ? "warning" : "ready"; commandEnabled: root.backendObject && !root.backendObject.setupRepairSessionActive; onTriggered: { if (!root.hasRepair()) root.backendObject.checkSetupHealth(); else if (root.useHostRepairConfirmation) root.repairRequested(); else repairConfirmation.open() } }
            ThemedButton { theme: root.themeTokens; text: "COPY FULL DIAGNOSTICS"; tone: "secondary"; commandEnabled: root.backendObject; onTriggered: root.backendObject.copySetupHealthDiagnostics() }
            ThemedButton { theme: root.themeTokens; text: "DONE"; tone: "secondary"; onTriggered: root.closeRequested() }
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
        contentItem: Flickable {
            width: parent.width
            implicitHeight: Math.min(repairConfirmationContent.implicitHeight,
                                     Math.max(180, (repairConfirmation.parent ? repairConfirmation.parent.height : 650)
                                              - repairConfirmationHeader.implicitHeight - repairConfirmation.padding * 2 - 36))
            contentWidth: width
            contentHeight: repairConfirmationContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: repairConfirmationContent
                width: parent.width
                spacing: 10
                Text { Layout.fillWidth: true; text: "HOTAS BF6 will apply only the scoped repair plan shown above, then perform a fresh read-back of the complete selected Device Rig. Windows may ask for administrator approval."; color: root.textColor; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                Text { Layout.fillWidth: true; text: root.snapshot.repairPlan.length + " safe repair" + (root.snapshot.repairPlan.length === 1 ? " is" : "s are") + " ready. Existing unrelated HidHide rules and the current mapping choice are preserved."; color: root.mutedColor; wrapMode: Text.WordWrap; font.pixelSize: 10 }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ThemedButton { theme: root.themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: repairConfirmation.close() }
                    ThemedButton { theme: root.themeTokens; text: "REPAIR " + root.snapshot.repairPlan.length + " ISSUE" + (root.snapshot.repairPlan.length === 1 ? "" : "S"); emphasis: "warning"; onTriggered: { repairConfirmation.close(); root.backendObject.repairSetupHealth() } }
                }
            }
        }
    }
}
