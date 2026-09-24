import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// The first native Flight Deck page. It consumes the same published UI state
// and command surface as the established Overview, but composes it around
// readiness rather than telemetry density.
Flickable {
    id: root
    objectName: "flightDeckOverview"
    property var readinessModel
    signal navigateToPage(int page)
    signal navigateToDevices(string context)
    signal navigateToIssue(var issue)

    readonly property bool wide: width >= 900
    readonly property var readiness: readinessModel ? readinessModel.readiness : ({})
    readonly property var input: readinessModel ? readinessModel.input : ({})
    readonly property var output: readinessModel ? readinessModel.output : ({})
    readonly property var isolation: readinessModel ? readinessModel.isolation : ({})
    readonly property var game: readinessModel ? readinessModel.game : ({})
    readonly property var profile: readinessModel ? readinessModel.profile : ({})
    readonly property var readinessState: readinessModel ? readinessModel.currentState : ({})
    readonly property var activation: backend.activationResolverState
    // Setup Health is a frozen, shared projection. Do not replay the older
    // readiness presentation while Devices shows the same rig's session.
    readonly property var setupTruth: backend.setupTruthSnapshot || ({})
    readonly property var hidhideHealth: backend.hidhideHealth || ({})
    readonly property bool guidedPresentation: themeManager.guidanceLevel === "Guided"

    function editSetupContext() {
        const rigId = String(setupTruth.setupTargetRigId || backend.activeDeviceRigId || "")
        return rigId.length ? "rig:" + rigId : "controllers"
    }

    function setupGroup(id) {
        const groups = setupTruth.groups || []
        for (let index = 0; index < groups.length; ++index) {
            if (String(groups[index].id || "") === id)
                return groups[index]
        }
        return { title: "Checking", detail: "Setup truth has not completed a fresh inspection.", status: "CHECKING", severity: "checking" }
    }

    function setupTone(statusOrGroup) {
        const group = typeof statusOrGroup === "string" ? ({}) : (statusOrGroup || {})
        const severity = String(group.severity || "").toUpperCase()
        const status = typeof statusOrGroup === "string" ? statusOrGroup : String(group.status || "")
        const normalized = String(status || "").toUpperCase()
        if (severity === "READY" || normalized === "READY") return "healthy"
        if (severity === "ERROR" || normalized === "FAILED" || normalized === "UNAVAILABLE"
                || normalized === "UNKNOWN / INSPECTION FAILED") return "fault"
        if (severity === "ATTENTION" || severity === "WAITING" || normalized === "ACTION NEEDED"
                || normalized === "WAITING FOR USER" || normalized === "ATTENTION") return "attention"
        return "informational"
    }

    readonly property var setupPhysical: setupGroup("physical")
    readonly property var setupOutput: setupGroup("vjoy")
    readonly property var setupIsolation: setupGroup("isolation")
    // Evidence remains available in Full. Basic stays focused on the one
    // actionable setup item and the physical/control checks above.
    readonly property bool connectionEvidenceExpanded: {
        themeManager.guidancePolicyRevision
        return themeManager.guidanceSectionExpanded("overview-connection-evidence")
    }
    readonly property bool additionalAttentionExpanded: {
        themeManager.guidancePolicyRevision
        return themeManager.guidanceSectionExpanded("overview-additional-attention")
    }
    property string issueHandoffMessage: ""
    property int guidedIssueIndex: 0

    function prioritizedIssue() {
        const issues = setupTruth.issues || []
        if (issues.length === 0) return ({})
        if (!guidedPresentation) return issues[0]
        const index = Math.max(0, Math.min(guidedIssueIndex, issues.length - 1))
        return issues[index] || ({})
    }

    function advanceGuidedIssue() {
        const issues = setupTruth.issues || []
        if (issues.length > 1) guidedIssueIndex = (guidedIssueIndex + 1) % issues.length
    }

    function currentIssueById(issueId) {
        const issues = setupTruth.issues || []
        for (let index = 0; index < issues.length; ++index) {
            if (String(issues[index].id || "") === String(issueId || ""))
                return issues[index]
        }
        return null
    }

    function reviewIssue(issue) {
        const issueId = String(issue && issue.id || "")
        const current = currentIssueById(issueId)
        if (!current) {
            issueHandoffMessage = "This setup item changed before it could be opened. Review the current setup details."
            return
        }
        issueHandoffMessage = ""
        navigateToIssue(current)
    }

    function attentionFallbackTitle() {
        return setupTruth.fresh ? "No current setup issue" : "Setup inspection pending"
    }

    function attentionFallbackExplanation() {
        return setupTruth.fresh
            ? "The latest setup inspection did not report an item requiring review."
            : "Setup Health has not completed a current inspection. Run a check before relying on readiness."
    }

    function hidhideTone() {
        if (String(hidhideHealth.freshness || "").toUpperCase() === "STALE") return "attention"
        const state = String(hidhideHealth.overallState || "CHECKING").toUpperCase()
        if (state === "READY") return "healthy"
        if (state.indexOf("REPAIR") >= 0 || state.indexOf("ACTION") >= 0 || state.indexOf("DOCTOR") >= 0) return "attention"
        if (state === "DEGRADED") return "attention"
        return "informational"
    }

    FlightDeckTheme {
        id: deck
    }

    // Standard remains the authoritative route/dialog host, but its Page
    // background belongs to the established theme family. Paint the native
    // Overview canvas explicitly so Flight Deck light mode has its own
    // contrast contract instead of exposing that embedded background.
    Rectangle {
        width: root.width
        height: Math.max(root.height, root.contentHeight)
        color: deck.primarySurface
        z: -1
    }

    contentWidth: width
    contentHeight: overviewContent.implicitHeight + deck.space24
    clip: true
    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    function overviewMessage() {
        return setupTruth.overallStatus
            ? "Review what is ready and the next action for " + String(setupTruth.rigName || "the selected Device Rig") + "."
            : "Checking current setup status.";
    }

    ColumnLayout {
        id: overviewContent
        x: deck.space4
        y: deck.space4
        width: Math.max(0, root.width - deck.space8)
        spacing: deck.space16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: deck.space4
                Text {
                    text: root.overviewMessage()
                    color: deck.textSecondary
                    font.pixelSize: deck.scale(12)
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            FlightDeckStatusChip {
                tokens: deck
                label: setupTruth.overallStatus || "CHECKING"
                tone: root.setupTone(setupTruth.overallStatus || "CHECKING")
                visible: root.wide
            }
        }

        FlightDeckCard {
            tokens: deck
            color: deck.secondarySurface
            Layout.fillWidth: true
            implicitHeight: setupContent.implicitHeight + contentPadding * 2

            ColumnLayout {
                id: setupContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space16

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "CURRENT SETUP"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                        }
                        Text {
                            text: setupTruth.overallStatus || "CHECKING"
                            color: deck.statusColor(root.setupTone(setupTruth.overallStatus || "CHECKING"))
                            font.family: deck.displayFont
                            font.pixelSize: deck.scale(root.wide ? 22 : 18)
                            font.bold: true
                        }
                    }
                    FlightDeckStatusChip {
                        tokens: deck
                        label: readinessState.mappingStatus || backend.mappingStatus
                        value: readinessState.mappingActive ? "LIVE" : "STANDBY"
                        tone: root.setupTone(setupTruth.overallStatus || "CHECKING")
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.wide ? 3 : 1
                    columnSpacing: deck.space16
                    rowSpacing: deck.space12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "PHYSICAL INPUT"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                        }
                        Text {
                            text: input.title || "Checking"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: deck.scale(15)
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: input.detail || ""
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(10)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "ACTIVE PROFILE"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                        }
                        Text {
                            text: profile.title || "Checking"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: deck.scale(15)
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: profile.detail || ""
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(10)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "VIRTUAL OUTPUT"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                        }
                        Text {
                            text: output.title || "Checking"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: deck.scale(15)
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: output.detail || ""
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(10)
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: deck.divider
                }
                RowLayout {
                    visible: !root.guidedPresentation
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Text {
                        text: "GAME"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: deck.scale(9)
                        font.bold: true
                    }
                    Text {
                        text: game.title || "Checking"
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: deck.scale(12)
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: game.detail || ""
                        color: deck.textMuted
                        font.pixelSize: deck.scale(9)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        visible: root.wide
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Button {
                        objectName: "flightDeckOverviewNextSetupAction"
                        readonly property var nextIssue: root.prioritizedIssue()
                        text: nextIssue ? "REVIEW NEXT ISSUE" : (root.setupTruth.fresh ? "VIEW SETUP" : "CHECK SETUP")
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: {
                            if (nextIssue) root.reviewIssue(nextIssue)
                            else root.navigateToDevices(root.setupTruth.fresh ? root.editSetupContext() : "verification")
                        }
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        objectName: "flightDeckOverviewTestInput"
                        text: "TEST PHYSICAL INPUT"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        enabled: String(backend.activeControllerRecordId || "").length > 0
                        onClicked: root.navigateToDevices("input-test:" + String(backend.activeControllerRecordId || ""))
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: parent.enabled ? deck.textSecondary : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        text: "CHANGE SETUP"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.navigateToDevices("controllers")
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "flightDeckMappingToggle"
                        text: backend.mappingRequested ? "STOP MAPPING" : "START MAPPING"
                        // Once Mapping has been requested, Stop must remain
                        // available even if vJoy disappears. Stopping only
                        // clears the request; it never reacquires or repairs.
                        enabled: backend.mappingRequested
                            || (String(backend.activeDeviceRigId || "").length > 0 && backend.vjoyReady)
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.toggleMapping()
                        background: Rectangle { radius: deck.radiusControl; color: parent.enabled ? (parent.down ? deck.accentMuted : deck.accent) : deck.disabled; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: parent.enabled ? (deck.light ? "white" : deck.primarySurface) : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }

        FlightDeckCard {
            objectName: "flightDeckPrioritizedAttention"
            // A clean inspection should leave the overview calm.  The ready
            // summary above remains available; this card exists only when
            // there is a concrete item the operator can review.
            visible: (root.setupTruth.issues || []).length > 0 || root.issueHandoffMessage.length > 0
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: attentionContent.implicitHeight + contentPadding * 2
            ColumnLayout {
                id: attentionContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space8
                readonly property var issue: root.prioritizedIssue()
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text { text: "ATTENTION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                        Text {
                            text: attentionContent.issue.title || root.attentionFallbackTitle()
                            color: attentionContent.issue.title ? deck.statusColor(root.setupTone(attentionContent.issue.severity || "attention")) : deck.healthy
                            font.family: deck.displayFont; font.pixelSize: deck.scale(16); font.bold: true
                            Layout.fillWidth: true; elide: Text.ElideRight
                        }
                        Text {
                            text: root.issueHandoffMessage.length
                                ? root.issueHandoffMessage
                                : (attentionContent.issue.explanation || root.attentionFallbackExplanation())
                            color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                    }
                    Button {
                        objectName: "flightDeckPrioritizedIssueReview"
                        visible: !!attentionContent.issue.title
                        text: "REVIEW DETAILS"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.reviewIssue(attentionContent.issue)
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : deck.accent; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                Button {
                    objectName: "flightDeckAdditionalIssueAction"
                    visible: (setupTruth.issues || []).length > 1
                    text: root.guidedPresentation
                        ? "NEXT ISSUE · " + (Math.max(0, Math.min(root.guidedIssueIndex,
                            (setupTruth.issues || []).length - 1)) + 1) + " OF " + (setupTruth.issues || []).length
                        : (root.additionalAttentionExpanded ? "HIDE ADDITIONAL ITEMS" : "SHOW ADDITIONAL ITEMS · " + ((setupTruth.issues || []).length - 1))
                    focusPolicy: Qt.StrongFocus
                    implicitHeight: deck.compactControlHeight
                    onClicked: {
                        if (root.guidedPresentation) root.advanceGuidedIssue()
                        else themeManager.setGuidanceSectionExpanded("overview-additional-attention", !root.additionalAttentionExpanded)
                    }
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                Repeater {
                    model: !root.guidedPresentation && root.additionalAttentionExpanded
                        ? (root.setupTruth.issues || []).slice(1) : []
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: "• " + String(modelData.title || "Setup item") + " · " + String(modelData.explanation || "Review details in Devices.")
                        color: deck.textSecondary; font.pixelSize: deck.scale(9); wrapMode: Text.WordWrap
                    }
                }
            }
        }

        FlightDeckCard {
            objectName: "flightDeckConnectionEvidence"
            visible: !root.guidedPresentation
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: connectionContent.implicitHeight + contentPadding * 2
            ColumnLayout {
                id: connectionContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space8
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text { text: "CONNECTION & EVIDENCE"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                        Text { text: String(setupPhysical.status || "CHECKING") + " input · " + String(setupOutput.status || "CHECKING") + " output · " + String(setupIsolation.status || "CHECKING") + " isolation"; color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: deck.scale(14); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                        Text { text: "Check controller connections and review anything that needs attention."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    Button {
                        text: root.connectionEvidenceExpanded ? "HIDE EVIDENCE" : "SHOW EVIDENCE"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: themeManager.setGuidanceSectionExpanded("overview-connection-evidence", !root.connectionEvidenceExpanded)
                        background: Rectangle {
                            radius: deck.radiusControl
                            color: parent.down ? deck.secondarySurface : "transparent"
                            border.color: parent.activeFocus ? deck.focus : deck.border
                            border.width: parent.activeFocus ? 2 : 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: deck.textSecondary
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(8)
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Button {
                        visible: themeManager.guidanceSectionHasExplicitPreference("overview-connection-evidence")
                        text: "FOLLOW GUIDANCE"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: themeManager.followGuidanceLevelForSection("overview-connection-evidence")
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                ColumnLayout {
                    visible: root.connectionEvidenceExpanded
                    Layout.fillWidth: true
                    spacing: deck.space4
                    Text { Layout.fillWidth: true; text: "INPUT · " + String(setupPhysical.detail || "No evidence yet."); color: deck.textSecondary; font.pixelSize: deck.scale(9); wrapMode: Text.WordWrap }
                    Text { Layout.fillWidth: true; text: "OUTPUT · " + String(setupOutput.detail || "No evidence yet."); color: deck.textSecondary; font.pixelSize: deck.scale(9); wrapMode: Text.WordWrap }
                    Text { Layout.fillWidth: true; text: "ISOLATION · " + String(root.hidhideHealth.normalSummary || setupIsolation.detail || "No evidence yet."); color: deck.textSecondary; font.pixelSize: deck.scale(9); wrapMode: Text.WordWrap }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: "CHECK SETUP"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.navigateToDevices("verification")
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : deck.accent; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        text: "DIAGNOSTICS"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.navigateToPage(3)
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        text: "OPEN DOCTOR"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.openHidHideDoctor()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        FlightDeckCard {
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: controlsContent.implicitHeight + contentPadding * 2

            ColumnLayout {
                id: controlsContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "LIVE CONTROLS"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(10)
                            font.bold: true
                        }
                        Text {
                            text: "Each card shows physical input separately from its mapped virtual output. Disabled and unassigned routes stay explicit."
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(10)
                        }
                    }
                    Button {
                        text: "OPEN AXES"
                        implicitHeight: deck.compactControlHeight
                        leftPadding: deck.space12
                        rightPadding: deck.space12
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: text
                        onClicked: root.navigateToPage(0)
                        background: Rectangle {
                            radius: deck.radiusControl
                            color: parent.down ? deck.accentMuted : parent.hovered ? deck.secondarySurface : "transparent"
                            border.width: parent.activeFocus ? 2 : 1
                            border.color: parent.activeFocus ? deck.focus : deck.accent
                        }
                        contentItem: Text {
                            text: parent.text
                            color: deck.accent
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                Repeater {
                    model: backend.axes
                    delegate: FlightDeckAxisMeter {
                        required property var modelData
                        tokens: deck
                        axis: modelData
                        Layout.fillWidth: true
                        onEditRequested: function(axisIndex) {
                            backend.setSelectedAxis(axisIndex)
                            root.navigateToPage(0)
                        }
                    }
                }
                Text {
                    visible: !backend.physicalConnected
                    text: "Connect a controller to see live control activity."
                    color: deck.textSecondary
                    font.pixelSize: deck.scale(10)
                    Layout.fillWidth: true
                }
            }
        }
    }
}
