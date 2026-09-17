import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Native Devices experience. It is deliberately a presentation and routing
// layer over AppBackend's published controller inventory and readiness plan;
// it does not enumerate devices, poll drivers, or alter mapper behavior.
Flickable {
    id: root
    objectName: "flightDeckDevices"

    property var readinessModel
    property string requestedContext: ""
    // Structured Overview handoff. The issue is re-resolved against the
    // frozen current snapshot before this page changes an editor selection.
    property var requestedIssueTarget: ({})
    // Presentation-only fixture used by the QML lifecycle test. Production
    // always reads the backend's exact read-only observation projection.
    property var readOnlyPhysicalInputTestPresentationOverride: null
    // The startup test may render controller-card arrangements without
    // touching AppBackend, device discovery, or persisted configuration.
    // Production never assigns this and always consumes backend.controllers.
    property var controllerPresentationOverride: null
    // Test-only plan data keeps dialog geometry fixtures independent from
    // driver state. Production always reads the authoritative Setup Truth plan.
    property var repairPlanPresentationFixture: null
    property bool virtualDetailsOpen: false
    property bool isolationDetailsOpen: false
    // Device Rigs remain configuration-owned data.  This page keeps only the
    // currently inspected ID and transient acknowledgement state; every
    // create/edit/activate operation below is routed through AppBackend.
    property string selectedRigId: ""
    property string handledIssueId: ""
    property var actionFeedback: ({})
    property var hidhideRepairPlan: ({})
    property bool outputCreationForNewRig: false
    // The application shell owns the actual card/queue. Keep this only for
    // the exact-record async verification transition below.
    property var notificationCenter: null
    property var forgetConsequences: ({})
    signal navigateToPage(int page)
    // The shell routes this presentation request to the one canonical
    // Profile Library/create dialog. It carries no runtime activation.
    signal requestProfileWorkflow(string rigId, string mode)

    readonly property bool wide: width >= 1040
    readonly property bool medium: width >= 760
    readonly property var state: readinessModel ? readinessModel.currentState : ({})
    // The compact cards are projections of the same typed snapshot used by
    // the central dialog. They never infer repairability from their labels.
    readonly property var setupTruth: backend.setupTruthSnapshot || ({})
    // A newly loaded Devices page reads the same current state as the shared
    // readiness model, so fixture and live updates cannot leave its compact
    // status label one render behind.
    readonly property var readiness: readinessModel
        ? readinessModel.presentationFor(readinessModel.currentState) : ({})
    readonly property var input: readinessModel ? readinessModel.input : ({})
    readonly property var output: readinessModel ? readinessModel.output : ({})
    readonly property var isolation: readinessModel ? readinessModel.isolation : ({})
    readonly property var vjoyCheck: truthCheck("vjoy", ["VJOY", "VIRTUAL OUTPUT"])
    readonly property var isolationCheck: truthCheck("isolation", ["HIDHIDE", "ISOLATION"])
    readonly property var physicalCheck: truthCheck("physical", ["PHYSICAL", "CONTROLLER"])
    readonly property var verificationCheck: truthCheck("verification", ["VERIFICATION"])
    readonly property var controllerItems: controllerPresentationOverride === null
        ? backend.controllers : controllerPresentationOverride
    readonly property var rigItems: backend.deviceRigs || []
    readonly property var outputLayouts: backend.virtualOutputLayouts || []
    // This is read-only control-plane telemetry sampled from the mapper's
    // fixed atomics. It lets the owner distinguish a configured descriptor
    // from a mapper that has actually published to the active Rig output.
    readonly property var outputRuntime: backend.outputRuntimeTelemetry || ({})
    readonly property var outputOwnership: outputRuntime.ownership || ({})
    readonly property var axisItems: backend.axes
    readonly property bool checking: backend.setupRepairSessionActive
    readonly property bool vjoyReady: state.vjoyReady === undefined ? backend.vjoyReady : state.vjoyReady
    readonly property string vjoyDeviceId: state.vjoyDeviceId === undefined
        ? backend.vjoyDeviceId : state.vjoyDeviceId
    readonly property string vjoyStatus: state.vjoyStatus === undefined ? backend.vjoyStatus : state.vjoyStatus
    readonly property int vjoyButtonCount: state.vjoyButtonCount === undefined
        ? backend.vjoyButtonCount : state.vjoyButtonCount
    readonly property int vjoyContinuousPovCount: state.vjoyContinuousPovCount === undefined
        ? backend.vjoyContinuousPovCount : state.vjoyContinuousPovCount
    readonly property int vjoyDiscretePovCount: state.vjoyDiscretePovCount === undefined
        ? backend.vjoyDiscretePovCount : state.vjoyDiscretePovCount
    // Production repair approval is always the frozen central Setup Truth
    // plan. The fixture never receives a ControllerReadiness plan.
    readonly property var setupRepairPlan: repairPlanPresentationFixture !== null
        ? repairPlanPresentationFixture : (setupTruth.repairPlan || [])
    readonly property bool hidhideAvailable: state.hidhideAvailable === undefined
        ? backend.hidhideAvailable : state.hidhideAvailable
    readonly property bool hidhideCloakStateKnown: state.hidhideCloakStateKnown === undefined
        ? backend.hidhideCloakStateKnown : state.hidhideCloakStateKnown
    readonly property bool hidhideCloaked: state.hidhideCloaked === undefined
        ? backend.hidhideCloaked : state.hidhideCloaked
    readonly property bool hidhideMapperAllowed: state.hidhideMapperAllowed === undefined
        ? backend.hidhideMapperAllowed : state.hidhideMapperAllowed
    readonly property var hidhideHealth: backend.hidhideHealth || ({})

    FlightDeckTheme {
        id: deck
    }

    Rectangle {
        width: root.width
        height: Math.max(root.height, root.contentHeight)
        color: deck.primarySurface
        z: -1
    }

    contentWidth: width
    contentHeight: devicesContent.implicitHeight + deck.space24
    clip: true
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    function truthCheck(id, names) {
        const groups = setupTruth.groups || [];
        for (let index = 0; index < groups.length; ++index) {
            if (String(groups[index].id || "") === id) {
                return { name: groups[index].title || names[0], state: groups[index].status || "CHECKING",
                    message: groups[index].detail || "", severity: groups[index].severity || "info" };
            }
        }
        return checkFor(names);
    }

    function checkFor(names) {
        const checks = state.checks || [];
        for (let index = 0; index < checks.length; ++index) {
            const name = String(checks[index].name || "").toUpperCase();
            for (let candidate = 0; candidate < names.length; ++candidate) {
                if (name.indexOf(String(names[candidate]).toUpperCase()) >= 0)
                    return checks[index];
            }
        }
        return { name: names[0], state: "Checking", message: "Status has not been checked yet.", severity: "info" };
    }

    function toneFor(check) {
        const severity = String((check || {}).severity || "").toLowerCase();
        const label = String((check || {}).state || "").toUpperCase();
        if (severity === "ready" || label === "READY")
            return "healthy";
        if (severity === "error" || label.indexOf("ERROR") >= 0 || label.indexOf("REQUIRED") >= 0 || label.indexOf("ACTION NEEDED") >= 0 || label.indexOf("ACTION REQUIRED") >= 0)
            return "fault";
        if (severity === "warning" || severity === "waiting" || label.indexOf("ATTENTION") >= 0 || label.indexOf("WAITING") >= 0)
            return "attention";
        return "informational";
    }

    function hidhideHealthTone() {
        if (String(hidhideHealth.freshness || "").toUpperCase() === "STALE") return "attention";
        const state = String(hidhideHealth.overallState || "CHECKING").toUpperCase();
        if (state === "READY") return "healthy";
        if (state.indexOf("REPAIR") >= 0 || state.indexOf("ACTION") >= 0 || state.indexOf("DOCTOR") >= 0)
            return "attention";
        if (state === "DEGRADED") return "attention";
        return "informational";
    }

    function normalHidHideDimensions() {
        const wanted = ["installation", "cloak-state", "application-access", "physical-isolation", "virtual-output"]
        const result = []
        const dimensions = hidhideHealth.dimensions || []
        for (let i = 0; i < wanted.length; ++i) {
            for (let j = 0; j < dimensions.length; ++j) {
                if (String(dimensions[j].id || "") === wanted[i]) { result.push(dimensions[j]); break }
            }
        }
        return result
    }

    function normalHidHideTitle(dimension) {
        const id = String((dimension || {}).id || "")
        if (id === "installation") return "HidHide"
        if (id === "cloak-state") return "Cloaking"
        if (id === "application-access") return "HOTAS BF6 access"
        if (id === "physical-isolation") return "Physical controllers"
        if (id === "virtual-output") return "Virtual output"
        return String((dimension || {}).title || "HidHide")
    }

    function outputPublicationSummary() {
        const target = Number(outputRuntime.activeVjoyDeviceId || 0);
        if (target <= 0)
            return "MAPPER OUTPUT · Waiting for an active Device Rig output.";
        const sequence = Number(outputRuntime.successfulOutputReportSequence || 0);
        const failures = Number(outputRuntime.outputWriteFailures || 0);
        if (!outputRuntime.outputReportsSucceeding || sequence <= 0)
            return "MAPPER OUTPUT · vJoy Device " + target + " · Waiting for the first successful mapped report.";
        let summary = "MAPPER OUTPUT · vJoy Device " + target + " · Publishing mapped reports · #" + sequence;
        if (failures > 0)
            summary += " · " + failures + " write error" + (failures === 1 ? "" : "s");
        return summary;
    }

    function markerFor(tone) {
        if (tone === "healthy") return "✓";
        if (tone === "attention" || tone === "fault") return "!";
        return "○";
    }

    function controllerState(controller) {
        if (String(controller.verificationState || "") === "VERIFYING")
            return "Verifying exact controller";
        if (String(controller.verificationState || "") === "FAILED")
            return "Verification failed";
        if (!controller.connected)
            return "Disconnected";
        if (controller.ambiguous)
            return "Selection required";
        if (!controller.verified)
            return "Setup required";
        if (controller.active)
            return "Connected · active";
        if (!controller.inDeviceRig)
            return "Verified · not in a Device Rig";
        if (controller.selected)
            return "Connected · selected";
        return "Connected · verified";
    }

    function controllerTone(controller) {
        if (!controller.connected) return "attention";
        if (controller.ambiguous || !controller.verified) return "attention";
        return "healthy";
    }

    function controllerActionLabel(controller) {
        if (!controller.connected) return "RESCAN";
        if (controller.ambiguous) return "IDENTIFY CONTROLLER";
        if (controller.verified && controller.selected) return "SELECTED";
        return controller.verified ? "SELECT DEVICE" : "VERIFY CONTROLLER";
    }

    function rigFor(id) {
        for (let index = 0; index < rigItems.length; ++index) {
            if (String(rigItems[index].id || "") === String(id || ""))
                return rigItems[index];
        }
        return null;
    }

    function rigTone(rig) {
        const setup = String((rig || {}).setupStatus || "").toUpperCase();
        if (setup === "NOT CHECKED" || setup === "CHECKING") return "informational";
        if (setup === "WAITING FOR USER" || setup === "ACTION NEEDED") return "attention";
        if (setup === "UNKNOWN / INSPECTION FAILED") return "fault";
        const health = String((rig || {}).health || "offline");
        if (health === "ready") return "healthy";
        if (health === "partial" || health === "needs-attention") return "attention";
        if (health === "conflict") return "fault";
        return "informational";
    }

    function rigState(rig) {
        if (!rig) return "Offline";
        if (rig.configured && rig.unmapped) return "ACTIVE · UNMAPPED";
        if (rig.configured) return "ACTIVE";
        return "VIEWING";
    }

    function memberState(member, rig) {
        if (!member || !member.enabled) return "Excluded";
        if (String(member.verificationState || "") === "VERIFYING") return "Verifying exact controller";
        if (String(member.verificationState || "") === "FAILED") return "Verification failed";
        if (member.ambiguous) return "Identity needs selection";
        if (!member.connected) return member.required ? "Required · offline" : "Optional · offline";
        if (member.needsVerification || !member.verified)
            return member.required ? "Verification required" : "Optional · verification available";
        return "Connected · verified";
    }

    function memberTone(member, rig) {
        if (!member || !member.enabled) return "informational";
        if (member.ambiguous || (member.needsVerification || !member.verified) && member.required) return "attention";
        if (!member.required && (member.needsVerification || !member.verified || !member.connected)) return "informational";
        if (member.connected) return "healthy";
        return member.required ? "fault" : "attention";
    }

    function outputName(id) {
        for (let index = 0; index < outputLayouts.length; ++index) {
            if (String(outputLayouts[index].id || "") === String(id || ""))
                return String(outputLayouts[index].name || "Virtual Output");
        }
        return "Virtual Output";
    }

    function configuredBusyVirtualOutput() {
        const groups = setupTruth.groups || [];
        for (let groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            const group = groups[groupIndex] || ({});
            if (String(group.id || "") !== "vjoy") continue;
            const outputs = (group.evidence || {}).outputs || [];
            for (let outputIndex = 0; outputIndex < outputs.length; ++outputIndex) {
                const candidate = outputs[outputIndex] || ({});
                if (candidate.busy && !candidate.ownedByHotasBf6) return candidate;
            }
        }
        return ({});
    }

    readonly property var configuredBusyOutput: configuredBusyVirtualOutput()
    readonly property bool virtualOutputExternallyBusy: !!configuredBusyOutput.busy
    readonly property bool virtualOutputOwnedByHotas: String(outputOwnership.ownershipState || "") === "OWNED BY HOTAS BF6"
    readonly property bool virtualOutputStaleOwnership: String(outputOwnership.ownershipState || "") === "STALE OWNERSHIP / DRIVER STATE"

    function configuredBusyRigId() {
        const target = String(setupTruth.setupTargetRigId || backend.editingDeviceRigId
            || backend.activeDeviceRigId || "");
        return rigFor(target) ? target : "";
    }

    function openCreateVirtualOutput(rigId) {
        createVirtualOutputDialog.openFor(String(rigId || configuredBusyRigId()
            || selectedRigId || backend.editingDeviceRigId || backend.activeDeviceRigId || ""));
    }

    function activeVirtualOutputId() {
        for (let index = 0; index < outputLayouts.length; ++index) {
            if (Boolean((outputLayouts[index] || {}).active)) return String(outputLayouts[index].id || "")
        }
        return ""
    }

    function editVirtualOutput(layoutId) {
        const target = String(layoutId || activeVirtualOutputId() || "")
        if (target.length) createVirtualOutputDialog.openForEdit(target)
    }

    function controllerCandidateId(controller) {
        return String((controller || {}).id || (controller || {}).directInputId || "");
    }

    function controllerForCandidate(id) {
        for (let index = 0; index < controllerItems.length; ++index) {
            if (controllerCandidateId(controllerItems[index]) === String(id || ""))
                return controllerItems[index];
        }
        return null;
    }

    function availableRigMemberChoices(rig) {
        const choices = [];
        const members = (rig || {}).members || [];
        for (let index = 0; index < controllerItems.length; ++index) {
            const controller = controllerItems[index] || ({});
            const id = controllerCandidateId(controller);
            if (!id || controller.ambiguous) continue;
            let alreadyIncluded = false;
            for (let memberIndex = 0; memberIndex < members.length; ++memberIndex) {
                if (String(members[memberIndex].id || "") === id) {
                    alreadyIncluded = true;
                    break;
                }
            }
            if (!alreadyIncluded) {
                choices.push({ id: id, name: String(controller.name || "Controller")
                    + (controller.connected ? " · Connected" : " · Saved / Offline") });
            }
        }
        return choices;
    }

    function availableOutputChoices(rig) {
        const choices = [];
        const existing = (rig || {}).outputs || [];
        for (let index = 0; index < outputLayouts.length; ++index) {
            const output = outputLayouts[index] || ({});
            const id = String(output.id || "");
            let alreadyIncluded = false;
            for (let outputIndex = 0; outputIndex < existing.length; ++outputIndex) {
                if (String(existing[outputIndex].id || "") === id) {
                    alreadyIncluded = true;
                    break;
                }
            }
            if (id && !alreadyIncluded) choices.push(output);
        }
        return choices;
    }

    function profilesReferencingRig(rigId) {
        const names = [];
        const profiles = backend.profiles || [];
        for (let index = 0; index < profiles.length; ++index) {
            const profile = profiles[index] || ({});
            if (String(profile.deviceRigId || "") === String(rigId || ""))
                names.push(String(profile.name || profile.displayName || "Profile"));
        }
        return names;
    }

    function rigDefaultProfileChoices(rig) {
        const choices = [
            { id: "", explicitlyNone: false, name: "Automatic" },
            { id: "", explicitlyNone: true, name: "None · keep Rig active without mapping" }
        ];
        const rigId = String((rig || {}).id || "");
        const profiles = backend.profiles || [];
        for (let index = 0; index < profiles.length; ++index) {
            const profile = profiles[index] || ({});
            if (String(profile.deviceRigId || "") === rigId && profile.enabled)
                choices.push({ id: String(profile.id || ""), explicitlyNone: false,
                    name: String(profile.categoryName || "") + " / " + String(profile.name || "Profile") });
        }
        return choices;
    }

    function selectedRig() { return rigFor(selectedRigId); }

    function normalizeRigSelection() {
        if (rigFor(selectedRigId)) return;
        const editing = String(backend.editingDeviceRigId || "");
        selectedRigId = rigFor(editing) ? editing : (rigItems.length ? String(rigItems[0].id || "") : "");
    }

    function showActionFeedback(result, fallbackTitle, fallbackMessage) {
        actionFeedback = result && result.title
            ? result : ({ success: false, title: fallbackTitle, message: fallbackMessage });
        if (notificationCenter && !actionFeedback.persistent)
            notificationCenter.enqueue(actionFeedback, fallbackTitle, fallbackMessage, 5000);
        return actionFeedback;
    }

    function showTransientActionFeedback(result, fallbackTitle, fallbackMessage, durationMs) {
        const feedback = showActionFeedback(result, fallbackTitle, fallbackMessage);
        if (notificationCenter && !feedback.persistent)
            notificationCenter.enqueue(feedback, fallbackTitle, fallbackMessage, durationMs > 0 ? durationMs : 5000);
        return feedback;
    }

    function reportBooleanAction(succeeded, successTitle, successMessage, failureTitle, failureMessage) {
        return showActionFeedback({ success: !!succeeded,
            title: succeeded ? successTitle : failureTitle,
            message: succeeded ? successMessage : failureMessage }, failureTitle, failureMessage);
    }

    function activateRig(rig) {
        if (!rig || !rig.id) return showActionFeedback({}, "Device Rig was not activated",
                                                        "The selected Device Rig is no longer available.");
        return showActionFeedback(backend.activateDeviceRigResult(String(rig.id)),
                                  "Device Rig was not activated",
                                  "No activation decision was returned.");
    }

    function openReadOnlyPhysicalInputTest(controllerId) {
        const target = String(controllerId || "");
        if (!target) {
            return showActionFeedback({ success: false, title: "Physical input test is unavailable",
                message: "Scan for a connected controller, then choose Test Input." },
                "Physical input test is unavailable", "Scan for a connected controller, then choose Test Input.");
        }
        const result = backend.startReadOnlyPhysicalInputTest(target);
        if (!result.success) {
            return showActionFeedback(result, "Physical input test is unavailable",
                                      "Refresh Devices and choose the controller again.");
        }
        readOnlyPhysicalInputTestDialog.open();
        return result;
    }

    function openUnmappedRigProfileWorkflow(rig, mode) {
        if (!rig || !rig.id) return false;
        requestProfileWorkflow(String(rig.id), String(mode || "choose"));
        return true;
    }

    function verifyController(recordId) {
        return showActionFeedback(backend.verifyController(String(recordId || "")),
            "Controller verification was not started",
            "Select a connected saved controller and try again.");
    }

    function requestForgetController(recordId) {
        const consequences = backend.controllerForgetConsequences(String(recordId || ""));
        if (!consequences.exists) {
            return showTransientActionFeedback({ success: false, title: "Controller is no longer saved",
                message: "Refresh Devices before trying this action again." }, "", "", 5000);
        }
        forgetConsequences = consequences;
        forgetControllerConfirmation.open();
        return consequences;
    }

    function settleControllerVerificationFeedback() {
        if (!actionFeedback.inProgress
                || String(actionFeedback.affectedObjectType || "") !== "physicalDevice") return;
        const targetId = String(actionFeedback.affectedObjectId || "");
        if (!targetId) return;
        const controller = controllerForCandidate(targetId);
        if (!controller || String(controller.verificationState || "") === "VERIFYING") return;
        const verified = Boolean(controller.verified)
            || String(controller.verificationState || "") === "VERIFIED";
        const waiting = String(controller.verificationState || "") === "WAITING FOR USER";
        showTransientActionFeedback({ success: verified,
            title: verified ? "Controller verified" : waiting ? "Controller verification needs input"
                : "Controller verification failed",
            message: String(controller.verificationDetail || (verified
                ? "The exact controller identity was saved."
                : "Review the controller card for the exact next step.")) }, "", "", 5000);
    }

    function rescanController() {
        backend.refreshControllers();
        return showTransientActionFeedback({ success: true, title: "Controller rescan requested",
            message: "HOTAS BF6 is refreshing the physical controller inventory." }, "", "", 3500);
    }

    function openRigDetails(rigId) {
        const rig = rigFor(rigId);
        if (!rig) return false;
        selectedRigId = String(rig.id || "");
        // This establishes the canonical editing context but deliberately
        // does not make the Rig active at runtime.
        backend.setEditingDeviceContext(selectedRigId, []);
        Qt.callLater(function() { rigDetailsDialog.open(); });
        return true;
    }

    function openRigOutput(outputId) {
        if (!outputId) return;
        virtualDetailsOpen = true;
        Qt.callLater(function() {
            contentY = Math.max(0, Math.min(contentHeight - height,
                virtualOutputSection.y - deck.space8));
        });
    }

    function currentSetupIssue(issueId) {
        const issues = setupTruth.issues || []
        for (let index = 0; index < issues.length; ++index) {
            if (String(issues[index].id || "") === String(issueId || ""))
                return issues[index]
        }
        return null
    }

    function revealIssueTarget() {
        const requested = requestedIssueTarget || ({})
        const issueId = String(requested.id || "")
        if (!issueId || handledIssueId === issueId) return
        handledIssueId = issueId
        if (requested.stale) {
            showActionFeedback({ success: false, title: "Setup item changed",
                message: String(requested.message || "This setup item is no longer available. Review the current setup details.") },
                "Setup item changed", "Review the current setup details.")
            return
        }
        const current = currentSetupIssue(issueId)
        if (!current) {
            showActionFeedback({ success: false, title: "Setup item changed",
                message: "This setup item changed before it could be opened. Review the current setup details." },
                "Setup item changed", "Review the current setup details.")
            return
        }
        const target = current.navigationTarget || ({})
        const type = String(target.objectType || "")
        const objectId = String(target.objectId || "")
        if (type.length && objectId.length && !backend.focusIssueTarget(type, objectId)) {
            showActionFeedback({ success: false, title: "Setup target is no longer available",
                message: "The exact target was removed or changed. Review the current setup details." },
                "Setup target is no longer available", "Review the current setup details.")
            return
        }
        const section = String(target.section || "")
        let destination = null
        if (section === "isolation") destination = isolationSection
        else if (section === "virtual-output" || type === "virtualOutput") {
            virtualDetailsOpen = true
            destination = virtualOutputSection
        } else if (section === "verification") destination = verificationSection
        else if (type === "deviceRig") {
            selectedRigId = String(objectId || backend.editingDeviceRigId || "")
            destination = rigsSection
        } else destination = controllersSection
        if (destination)
            contentY = Math.max(0, Math.min(contentHeight - height, destination.y - deck.space8))
    }

    Connections {
        target: backend
        function onControllersChanged() { Qt.callLater(root.settleControllerVerificationFeedback); }
        function onDeviceRigsChanged() { Qt.callLater(root.settleControllerVerificationFeedback); }
    }

    component RigButton: Button {
        id: control
        property bool subdued: false
        property bool destructive: false
        implicitHeight: deck.compactControlHeight
        leftPadding: deck.space12
        rightPadding: deck.space12
        focusPolicy: Qt.StrongFocus
        background: Rectangle {
            radius: deck.radiusControl
            color: !control.enabled ? deck.disabled
                : control.down ? (control.subdued ? deck.selected : deck.accentMuted)
                : control.destructive ? Qt.rgba(deck.fault.r, deck.fault.g, deck.fault.b, 0.10)
                : control.subdued ? deck.secondarySurface : deck.accent
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus
                : control.destructive ? deck.fault : control.subdued ? deck.border : deck.accent
        }
        contentItem: Text {
            text: control.text
            color: !control.enabled ? deck.textMuted
                : control.destructive ? deck.fault
                : control.subdued ? deck.textSecondary : (deck.light ? "white" : deck.primarySurface)
            font.family: deck.telemetryFont
            font.pixelSize: deck.scale(9)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component RigCombo: ComboBox {
        id: control
        implicitHeight: deck.controlHeight
        leftPadding: deck.space12
        rightPadding: deck.space32
        font.family: deck.telemetryFont
        font.pixelSize: deck.scale(10)
        contentItem: Text {
            text: control.displayText
            color: control.enabled ? deck.textPrimary : deck.textMuted
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - deck.space12
            anchors.verticalCenter: parent.verticalCenter
            text: control.popup.visible ? "⌃" : "⌄"
            color: deck.textMuted
            font.pixelSize: deck.scale(12)
        }
        background: Rectangle {
            radius: deck.radiusControl
            color: deck.elevatedSurface
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus ? deck.focus : deck.border
        }
        delegate: ItemDelegate {
            width: ListView.view ? ListView.view.width : control.width
            height: deck.controlHeight
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                text: control.textAt(index)
                color: deck.textPrimary
                font.family: deck.telemetryFont
                font.pixelSize: deck.scale(10)
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle { color: parent.highlighted ? deck.selected : deck.elevatedSurface }
        }
        popup: Popup {
            y: control.height - 1
            width: control.width
            padding: deck.space4
            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 224)
                model: control.delegateModel
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: deck.border }
        }
    }

    function revealContext() {
        let target = null;
        if (requestedContext.indexOf("rig:") === 0) {
            const rigId = requestedContext.slice(4);
            openRigDetails(rigId);
            target = rigsSection;
        } else if (requestedContext === "virtual-output") target = virtualOutputSection;
        else if (requestedContext === "isolation") target = isolationSection;
        else if (requestedContext === "verification") target = verificationSection;
        else if (requestedContext === "controllers") target = controllersSection;
        else if (requestedContext === "input-test" || requestedContext.indexOf("input-test:") === 0) {
            target = controllersSection;
            Qt.callLater(function() {
                const requestedId = requestedContext.indexOf("input-test:") === 0
                    ? requestedContext.slice("input-test:".length) : "";
                const controllerId = requestedId || String(backend.activeControllerRecordId || "");
                if (controllerId) root.openReadOnlyPhysicalInputTest(controllerId);
                else root.showActionFeedback({ success: false,
                    title: "Physical input test is unavailable",
                    message: "Choose the exact controller to inspect, then use Test Input." },
                    "Physical input test is unavailable",
                    "Choose the exact controller to inspect, then use Test Input.");
            });
        }
        if (target)
            contentY = Math.max(0, Math.min(contentHeight - height, target.y - deck.space8));
    }

    onRequestedContextChanged: Qt.callLater(revealContext)
    onRequestedIssueTargetChanged: {
        handledIssueId = ""
        Qt.callLater(revealIssueTarget)
    }
    Component.onCompleted: {
        Qt.callLater(revealContext)
        Qt.callLater(revealIssueTarget)
    }
    onRigItemsChanged: normalizeRigSelection()

    Connections {
        target: backend
        function onDeviceRigsChanged() { Qt.callLater(root.normalizeRigSelection); }
    }

    ColumnLayout {
        id: devicesContent
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
                    text: "See what is connected, what needs attention, and the real next step."
                    color: deck.textSecondary
                    font.pixelSize: deck.scale(12)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
            FlightDeckStatusChip {
                tokens: deck
                label: "SETUP HEALTH"
                value: readiness.label || "CHECKING"
                tone: readiness.tone || "informational"
                visible: root.medium
            }
        }

        FlightDeckCard {
            id: setupHealthHero
            objectName: "flightDeckSetupHealthHero"
            tokens: deck
            contentPadding: deck.cardPadding
            color: deck.secondarySurface
            Layout.fillWidth: true
            implicitHeight: setupHealthContent.implicitHeight + contentPadding * 2

            ColumnLayout {
                id: setupHealthContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space16

                RowLayout {
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Rectangle {
                        width: 34
                        height: 34
                        radius: width / 2
                        color: Qt.rgba(deck.statusColor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" })).r,
                                       deck.statusColor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" })).g,
                                       deck.statusColor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" })).b, 0.16)
                        border.color: deck.statusColor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" }))
                        Text {
                            anchors.centerIn: parent
                            text: root.markerFor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" }))
                            color: deck.statusColor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" }))
                            font.pixelSize: deck.scale(18)
                            font.bold: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "SETUP HEALTH"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                        }
                        Text {
                            text: root.setupTruth.overallStatus || readiness.label || "CHECKING"
                            color: deck.statusColor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" }))
                            font.family: deck.displayFont
                            font.pixelSize: deck.scale(root.medium ? 22 : 18)
                            font.bold: true
                        }
                        Text {
                            text: checking ? "Checking the complete Device Rig…" : (root.setupTruth.rigName
                                ? "Current setup status for " + root.setupTruth.rigName + "."
                                : (readiness.detail || "Checking current controller setup."))
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(11)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    Button {
                        objectName: "flightDeckSetupHealthAction"
                        text: "CHECK & REPAIR SETUP"
                        enabled: !checking
                        visible: root.medium
                        implicitHeight: deck.controlHeight
                        leftPadding: deck.space16
                        rightPadding: deck.space16
                        focusPolicy: Qt.StrongFocus
                        onClicked: setupHealthDialog.open()
                        background: Rectangle {
                            radius: deck.radiusControl
                            color: parent.enabled && parent.down ? deck.accentMuted : deck.accent
                            border.width: parent.activeFocus ? 2 : 1
                            border.color: parent.activeFocus ? deck.focus : deck.accent
                        }
                        contentItem: Text {
                            text: parent.text
                            color: deck.light ? "white" : deck.primarySurface
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.wide ? 4 : (root.medium ? 2 : 1)
                    columnSpacing: deck.space12
                    rowSpacing: deck.space8
                    Repeater {
                        model: [
                            { label: "Physical input", check: root.physicalCheck },
                            { label: "Controller verification", check: root.verificationCheck },
                            { label: "Virtual output", check: root.vjoyCheck },
                            { label: "Device isolation", check: root.isolationCheck }
                        ]
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
                            radius: deck.radiusControl
                            color: deck.elevatedSurface
                            border.color: deck.border
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: deck.space12
                                anchors.rightMargin: deck.space12
                                spacing: deck.space8
                                Text {
                                    text: root.markerFor(root.toneFor(modelData.check))
                                    color: deck.statusColor(root.toneFor(modelData.check))
                                    font.pixelSize: deck.scale(14)
                                    font.bold: true
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: modelData.label.toUpperCase(); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: String(modelData.check.state || "Checking"); color: deck.textPrimary; font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                }

                Button {
                    visible: !root.medium
                    text: "CHECK & REPAIR SETUP"
                    enabled: !checking
                    Layout.fillWidth: true
                    implicitHeight: deck.controlHeight
                    focusPolicy: Qt.StrongFocus
                    onClicked: setupHealthDialog.open()
                    background: Rectangle { radius: deck.radiusControl; color: deck.accent; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        Item { id: controllersSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "PHYSICAL CONTROLLERS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true }
        Text { text: "Each connected controller remains visible here. Selecting or verifying one uses the existing controller workflow."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }

        FlightDeckCard {
            objectName: "flightDeckNoControllers"
            tokens: deck
            Layout.fillWidth: true
            visible: root.controllerItems.length === 0
            implicitHeight: visible ? noControllersContent.implicitHeight + deck.space32 : 0
            ColumnLayout {
                id: noControllersContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space8
                Text { text: "No controllers connected"; color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: deck.scale(17); font.bold: true }
                Text { text: "Plug in a joystick, HOTAS, throttle, pedals, or another supported controller, then scan for devices."; color: deck.textSecondary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button {
                    objectName: "flightDeckScanDevices"
                    text: "SCAN FOR DEVICES"
                    implicitHeight: deck.compactControlHeight
                    focusPolicy: Qt.StrongFocus
                    onClicked: backend.refreshControllers()
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            visible: root.controllerItems.length > 0
            columns: root.wide ? 2 : 1
            columnSpacing: deck.space12
            rowSpacing: deck.space12
            Repeater {
                objectName: "flightDeckControllerRepeater"
                // AppBackend supplies a QVariantList. Use an integer model
                // and read the current element by index so map values remain
                // live even when discovery updates an existing row in place.
                model: root.controllerItems.length
                delegate: FlightDeckCard {
                    id: controllerCard
                    required property int index
                    readonly property var controller: root.controllerItems[index] || ({})
                    objectName: "flightDeckControllerCard_" + controller.directInputId
                    readonly property string controllerActionLabel: root.controllerActionLabel(controller)
                    tokens: deck
                    contentPadding: deck.cardPadding
                    Layout.fillWidth: true
                    implicitHeight: controllerContent.implicitHeight + contentPadding * 2
                    border.color: controller.selected ? deck.accent
                        : controller.active ? deck.healthy : deck.border
                    ColumnLayout {
                        id: controllerContent
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: deck.space4
                                Text { text: "PHYSICAL CONTROLLER"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                                Text {
                                    objectName: "flightDeckControllerName_" + controllerCard.index
                                    text: controllerCard.controller.name || "Controller"
                                    color: deck.textPrimary
                                    font.family: deck.displayFont
                                    font.pixelSize: deck.scale(16)
                                    font.bold: true
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    wrapMode: Text.WrapAnywhere
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: deck.space12
                            Text { text: root.markerFor(root.controllerTone(controllerCard.controller)) + " " + root.controllerState(controllerCard.controller); color: deck.statusColor(root.controllerTone(controllerCard.controller)); font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                            Text { text: controllerCard.controller.verified ? "✓ Verified" : "! Not yet verified"; color: controllerCard.controller.verified ? deck.healthy : deck.attention; font.pixelSize: deck.scale(10); font.bold: true }
                        }
                        Text { text: controllerCard.controller.axisCount + " axes  •  " + controllerCard.controller.buttonCount + " buttons  •  " + controllerCard.controller.povCount + " hats"; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text {
                            visible: controllerCard.controller.verified && !controllerCard.controller.inDeviceRig
                            text: "NOT IN A DEVICE RIG · Add this verified controller to a Device Rig before selecting it for editing."
                            color: deck.attention
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            visible: controllerCard.controller.verified && controllerCard.controller.inDeviceRig
                            text: "Device Rig · " + String(controllerCard.controller.rigNames || "")
                            color: deck.textSecondary
                            font.pixelSize: deck.scale(9)
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Text { visible: controllerCard.controller.selected; text: "Selected for editing."; color: deck.accent; font.pixelSize: deck.scale(10); Layout.fillWidth: true }
                        Text { visible: controllerCard.controller.active; text: "Used by the current active setup."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true }
                        Text { visible: !controllerCard.controller.connected; text: "This saved controller is no longer available. Reconnect it, then scan again."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { visible: String(controllerCard.controller.verificationDetail || "").length > 0; text: String(controllerCard.controller.verificationDetail || ""); color: deck.textSecondary; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            Button {
                                objectName: "flightDeckControllerTestInput_" + controllerCard.index
                                text: "TEST INPUT"
                                enabled: Boolean(root.controllerCandidateId(controllerCard.controller))
                                focusPolicy: Qt.StrongFocus
                                implicitHeight: deck.compactControlHeight
                                onClicked: root.openReadOnlyPhysicalInputTest(
                                    root.controllerCandidateId(controllerCard.controller))
                                background: Rectangle { radius: deck.radiusControl; color: parent.enabled && parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                                contentItem: Text { text: parent.text; color: parent.enabled ? deck.textSecondary : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                            Button {
                                objectName: "flightDeckControllerAction_" + controllerCard.controller.directInputId
                                text: controllerCard.controllerActionLabel
                                enabled: !(controllerCard.controller.connected
                                    && controllerCard.controller.verified
                                    && controllerCard.controller.selected)
                                focusPolicy: Qt.StrongFocus
                                implicitHeight: deck.compactControlHeight
                                onClicked: {
                                    if (!controllerCard.controller.connected) root.rescanController()
                                    else if (controllerCard.controller.verified && controllerCard.controller.id) {
                                        if (!controllerCard.controller.inDeviceRig) {
                                            root.showActionFeedback({ success: false,
                                                title: "Add this controller to a Device Rig",
                                                message: String(controllerCard.controller.name || "This controller")
                                                    + " is verified but is not assigned to a Device Rig. Add it to a Device Rig before selecting it for editing." },
                                                "Add this controller to a Device Rig",
                                                "Add the verified controller to a Device Rig before selecting it for editing.")
                                            return
                                        }
                                        const selected = backend.selectControllerForEditing(controllerCard.controller.id)
                                        root.reportBooleanAction(selected, "Selected Device updated",
                                            "Now viewing " + String(controllerCard.controller.name || "this controller") + ".",
                                            "Could not select this device", "This controller is no longer assigned to the selected Device Rig. Add it to a Device Rig, then try again.")
                                    }
                                    else root.verifyController(String(controllerCard.controller.id
                                        || controllerCard.controller.directInputId || ""))
                                }
                                background: Rectangle { radius: deck.radiusControl; color: parent.enabled && parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : (parent.enabled ? deck.accent : deck.border); border.width: parent.activeFocus ? 2 : 1 }
                                contentItem: Text { text: parent.text; color: parent.enabled ? deck.accent : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                            Button {
                                visible: controllerCard.controller.verified && controllerCard.controller.id
                                objectName: "flightDeckControllerForget_" + controllerCard.controller.id
                                text: "FORGET"
                                focusPolicy: Qt.StrongFocus
                                implicitHeight: deck.compactControlHeight
                                onClicked: root.requestForgetController(controllerCard.controller.id)
                                background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                                contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                        }
                    }
                }
            }
        }

        Item { id: rigsSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "DEVICE RIGS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true }
            RigButton {
                objectName: "flightDeckCreateRig"
                text: "+ CREATE RIG"
                enabled: root.controllerItems.length > 0
                onClicked: createRigDialog.open()
            }
        }
        Text {
            text: "Group the physical controllers and Virtual Outputs a Profile needs. Viewing a Rig never activates it."
            color: deck.textSecondary
            font.pixelSize: deck.scale(10)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        FlightDeckCard {
            objectName: "flightDeckNoDeviceRigs"
            tokens: deck
            Layout.fillWidth: true
            visible: root.rigItems.length === 0
            implicitHeight: visible ? noRigsContent.implicitHeight + deck.space32 : 0
            ColumnLayout {
                id: noRigsContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space8
                Text { text: "Create your first Device Rig"; color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: deck.scale(17); font.bold: true }
                Text { text: root.controllerItems.length ? "Choose one or more physical controllers, set Required or Optional membership, then attach the Virtual Output they will use." : "Connect or scan for a physical controller before creating a Device Rig."; color: deck.textSecondary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true
                    RigButton { text: "SCAN FOR DEVICES"; subdued: true; onClicked: backend.refreshControllers() }
                    RigButton { text: "+ CREATE RIG"; enabled: root.controllerItems.length > 0; onClicked: createRigDialog.open() }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        GridLayout {
            objectName: "flightDeckDeviceRigRepeater"
            Layout.fillWidth: true
            visible: root.rigItems.length > 0
            columns: root.wide ? 2 : 1
            columnSpacing: deck.space12
            rowSpacing: deck.space12
            Repeater {
                model: root.rigItems
                delegate: FlightDeckCard {
                    required property var modelData
                    readonly property var rig: modelData
                    objectName: "flightDeckDeviceRigCard_" + String(rig.id || "")
                    tokens: deck
                    contentPadding: deck.cardPadding
                    Layout.fillWidth: true
                    implicitHeight: rigCardContent.implicitHeight + contentPadding * 2
                    border.color: String(rig.id || "") === root.selectedRigId ? deck.accent : deck.border
                    ColumnLayout {
                        id: rigCardContent
                        anchors.fill: parent
                        anchors.margins: parent.contentPadding
                        spacing: deck.space8
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: deck.space4
                                Text { text: "DEVICE RIG"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                                Text { text: String(rig.name || "Device Rig"); color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: deck.scale(17); font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                            }
                            FlightDeckStatusChip { tokens: deck; label: String(rig.setupStatus || rig.healthLabel || "Offline").toUpperCase(); value: root.rigState(rig); tone: root.rigTone(rig) }
                        }
                        Text {
                            text: (rig.members || []).length + " physical controller" + ((rig.members || []).length === 1 ? "" : "s")
                                + "  ·  " + (rig.outputs || []).length + " Virtual Output" + ((rig.outputs || []).length === 1 ? "" : "s")
                            color: deck.textSecondary
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(10)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: rig.members || []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: deck.space8
                                Text { text: root.markerFor(root.memberTone(modelData, rig)); color: deck.statusColor(root.memberTone(modelData, rig)); font.pixelSize: deck.scale(12); font.bold: true }
                                Text { text: String(modelData.name || "Controller"); color: deck.textPrimary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                                Text { text: modelData.required ? "REQUIRED" : "OPTIONAL"; color: modelData.required ? deck.textSecondary : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true }
                                Text { text: root.memberState(modelData, rig).toUpperCase(); color: deck.statusColor(root.memberTone(modelData, rig)); font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true; elide: Text.ElideRight }
                            }
                        }
                        Text {
                            visible: (rig.outputs || []).length > 0
                            text: "RIG PRIMARY OUTPUT  ·  " + (rig.outputs || []).map(function(output) {
                                return (output.primary ? "PRIMARY · " : "") + String(output.name || "Virtual Output")
                                    + " · vJoy " + String(output.deviceId || "?");
                            }).join("  ·  ")
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            visible: !!rig.unmapped
                            text: "NO PROFILE / UNMAPPED · Hardware is active and safe. Choose, copy, or create a Profile when you are ready to map controls."
                            color: deck.accent
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: deck.space8
                            RigButton {
                                objectName: "flightDeckOpenRig_" + String(rig.id || "")
                                text: "OPEN DETAILS"
                                subdued: true
                                onClicked: root.openRigDetails(String(rig.id || ""))
                            }
                            Item { Layout.fillWidth: true }
                            RigButton {
                                objectName: "flightDeckActivateRig_" + String(rig.id || "")
                                text: rig.configured ? "ACTIVE" : "SET ACTIVE"
                                enabled: !!rig.enabled && !rig.configured
                                onClicked: root.activateRig(rig)
                            }
                        }
                        RowLayout {
                            visible: !!rig.unmapped
                            Layout.fillWidth: true
                            spacing: deck.space8
                            RigButton {
                                text: "CREATE BLANK PROFILE"
                                onClicked: root.openUnmappedRigProfileWorkflow(rig, "blank")
                            }
                            RigButton {
                                text: "COPY CURRENT PROFILE"
                                subdued: true
                                onClicked: root.openUnmappedRigProfileWorkflow(rig, "copy")
                            }
                            RigButton {
                                text: "CHOOSE PROFILE"
                                subdued: true
                                onClicked: root.openUnmappedRigProfileWorkflow(rig, "choose")
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }

        Item { id: virtualOutputSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "VIRTUAL OUTPUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true }
        FlightDeckCard {
            objectName: "flightDeckVirtualOutput"
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: virtualOutputContent.implicitHeight + deck.space32
            border.color: root.requestedContext === "virtual-output" ? deck.accent : deck.border
            ColumnLayout {
                id: virtualOutputContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text { text: "CURRENT ACTIVE OUTPUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                        Text { text: root.virtualOutputStaleOwnership ? "Stale ownership" : (root.virtualOutputExternallyBusy ? "Configured · busy" : (root.virtualOutputOwnedByHotas ? "Configured · acquired" : (root.toneFor(root.vjoyCheck) === "healthy" ? "Online" : "Action needed"))); color: deck.statusColor(root.toneFor(root.vjoyCheck)); font.family: deck.displayFont; font.pixelSize: deck.scale(18); font.bold: true }
                        Text { text: backend.activeOutputLayoutName + "\nVirtual Output · vJoy Device " + root.vjoyDeviceId + "\n" + (root.vjoyCheck.message || output.detail || "Checking virtual output."); color: deck.textSecondary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text {
                            visible: Number(root.outputRuntime.activeVjoyDeviceId || 0) > 0
                            text: root.outputPublicationSummary()
                            color: root.outputRuntime.outputReportsSucceeding ? deck.healthy : deck.warning
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.scale(9)
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    FlightDeckStatusChip { tokens: deck; label: root.virtualOutputStaleOwnership ? "STALE DRIVER STATE" : (root.virtualOutputExternallyBusy ? "CONFIGURED · BUSY" : (root.virtualOutputOwnedByHotas ? "CONFIGURED · ACQUIRED" : (root.toneFor(root.vjoyCheck) === "healthy" ? "ONLINE" : "ACTION NEEDED"))); value: backend.activeOutputLayoutName + " · vJoy " + root.vjoyDeviceId; tone: root.toneFor(root.vjoyCheck); visible: root.medium }
                }
                Text { text: "Virtual output is the controller signal games receive from HOTAS BF6."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true
                    RigButton {
                        visible: root.virtualOutputExternallyBusy
                        text: "RETRY ACQUIRE"
                        onClicked: root.showActionFeedback(backend.retryVirtualOutputAcquire(
                            String(root.configuredBusyOutput.layoutId || "")),
                            "Virtual Output was not retried", "Choose the configured busy output and try again.")
                    }
                    RigButton {
                        visible: root.virtualOutputExternallyBusy
                        text: "CHOOSE ANOTHER OUTPUT"
                        subdued: true
                        onClicked: {
                            const rigId = root.configuredBusyRigId();
                            if (rigId) root.openRigDetails(rigId);
                            else root.showActionFeedback({ success: false, title: "Choose another output", message: "Open a Device Rig to choose its primary Virtual Output." }, "Choose another output", "Open a Device Rig to choose an output.");
                        }
                    }
                    RigButton {
                        visible: root.virtualOutputExternallyBusy
                        text: "CREATE NEW OUTPUT"
                        subdued: true
                        onClicked: root.openCreateVirtualOutput(root.configuredBusyRigId())
                    }
                    Button {
                        // Manual driver configuration is an advanced fallback,
                        // never a competing normal repair route.
                        visible: root.toneFor(root.vjoyCheck) !== "healthy" && root.virtualDetailsOpen
                        text: "OPEN VJOY SETUP"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.openVjoyConfiguration()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
                    RigButton { text: "CREATE VIRTUAL OUTPUT"; subdued: true; onClicked: root.openCreateVirtualOutput("") }
                    RigButton { text: "EDIT OUTPUT"; subdued: true; enabled: root.activeVirtualOutputId().length > 0; onClicked: root.editVirtualOutput("") }
                    Button {
                        text: root.virtualDetailsOpen ? "HIDE TECHNICAL DETAILS" : "TECHNICAL DETAILS"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.virtualDetailsOpen = !root.virtualDetailsOpen
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                Rectangle {
                    visible: root.virtualDetailsOpen
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? virtualDetails.implicitHeight + deck.space24 : 0
                    radius: deck.radiusControl
                    color: deck.elevatedSurface
                    border.color: deck.border
                    ColumnLayout {
                        id: virtualDetails
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space4
                        Text { text: "vJoy Device: " + root.vjoyDeviceId; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true }
                        Text { text: "Descriptor: " + String(root.outputOwnership.descriptor || "Not inspected"); color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: "Raw status: " + String(root.outputOwnership.rawStatusName || "UNKNOWN") + " (" + String(root.outputOwnership.rawStatus === undefined ? "?" : root.outputOwnership.rawStatus) + ")"; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: "Owner PID: " + String(root.outputOwnership.ownerPid || "not reported"); color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: "Owner process: " + (String(root.outputOwnership.ownerProcess || "").length ? String(root.outputOwnership.ownerProcess) : "not available") + (String(root.outputOwnership.ownerProcessPath || "").length ? "\n" + String(root.outputOwnership.ownerProcessPath) : ""); color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                        Text { text: "HOTAS BF6 PID: " + String(root.outputOwnership.hotasProcessId || "not reported"); color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: "Acquire attempt result: " + String(root.outputOwnership.acquireAttempt || "not attempted"); color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: "Last status transition: " + String(root.outputOwnership.lastStatusTransition || "not observed"); color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: root.vjoyStatus; color: deck.textMuted; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: root.vjoyButtonCount + " buttons  •  " + root.vjoyContinuousPovCount + " continuous hats  •  " + root.vjoyDiscretePovCount + " discrete hats"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }
            }
        }

        Item { id: isolationSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "DEVICE ISOLATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true }
        FlightDeckCard {
            objectName: "flightDeckDeviceIsolation"
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: isolationContent.implicitHeight + deck.space32
            border.color: root.requestedContext === "isolation" ? deck.accent : deck.border
            ColumnLayout {
                id: isolationContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text { text: "DEVICE ISOLATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                        Text { text: root.toneFor(root.isolationCheck) === "healthy" ? "Protected" : "Action needed"; color: deck.statusColor(root.toneFor(root.isolationCheck)); font.family: deck.displayFont; font.pixelSize: deck.scale(18); font.bold: true }
                        Text { text: root.isolationCheck.message || isolation.detail || "Checking device isolation."; color: deck.textSecondary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    FlightDeckStatusChip { tokens: deck; label: "HIDHIDE"; value: String(root.isolationCheck.state || "CHECKING").toUpperCase(); tone: root.toneFor(root.isolationCheck); visible: root.medium }
                }
                Text { text: "Device isolation prevents games from seeing both the physical controller and virtual output."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hidhideHealthSummary.implicitHeight + deck.space20
                    radius: deck.radiusControl
                    color: deck.elevatedSurface
                    border.color: deck.statusColor(root.hidhideHealthTone())
                    ColumnLayout {
                        id: hidhideHealthSummary
                        anchors.fill: parent
                        anchors.margins: deck.space10
                        spacing: deck.space4
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "HIDHIDE HEALTH"; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true }
                            Item { Layout.fillWidth: true }
                            Text { text: String(root.hidhideHealth.overallState || "CHECKING") + (String(root.hidhideHealth.freshness || "") === "STALE" ? " · NEEDS VERIFICATION" : ""); color: deck.statusColor(root.hidhideHealthTone()); font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                        }
                        Text { Layout.fillWidth: true; text: root.hidhideHealth.inProgress ? (String(root.hidhideHealth.checksCompleted || 0) + " / " + String(root.hidhideHealth.checksTotal || 0) + " · " + String(root.hidhideHealth.percentComplete || 0) + "%\n" + (root.hidhideHealth.currentCheckTitle || "Checking HidHide")) : (root.hidhideHealth.normalSummary || "HidHide helps keep physical controllers out of games while HOTAS BF6 uses them."); color: deck.textSecondary; font.pixelSize: deck.scale(10); wrapMode: Text.WordWrap }
                        Repeater {
                            model: root.normalHidHideDimensions()
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: "• " + root.normalHidHideTitle(modelData) + " · " + String(modelData.shortSummary || modelData.state || "Checking")
                                color: deck.textMuted; font.pixelSize: deck.scale(9); wrapMode: Text.WordWrap
                            }
                        }
                        Repeater {
                            model: root.hidhideHealth.physicalDevices || []
                            delegate: Text { required property var modelData; Layout.fillWidth: true; text: String(modelData.friendlyName || "Physical controller") + " · " + String(modelData.availabilityState || (String(modelData.state || "") === "REPAIR AVAILABLE" ? "Visible to games" : modelData.state || "Checking")); color: deck.statusColor(modelData.availabilityState ? "attention" : String(modelData.state || "") === "READY" ? "healthy" : "attention"); font.pixelSize: deck.scale(10); wrapMode: Text.WordWrap }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: root.hidhideHealth.inProgress ? "CHECKING…" : "RUN FULL CHECK"
                        enabled: !root.hidhideHealth.inProgress
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.showActionFeedback(backend.runHidHideFullCheck(), "HidHide check did not start", "Try again after the current check completes.")
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        visible: root.hidhideHealth.inProgress
                        text: "CANCEL"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.cancelHidHideFullCheck()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: deck.border; border.width: 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        visible: String(root.hidhideHealth.overallState || "") === "REPAIR AVAILABLE"
                        text: "REVIEW & REPAIR"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: { root.hidhideRepairPlan = backend.reviewHidHideHealthRepair(); root.showActionFeedback(root.hidhideRepairPlan, "HidHide repair plan unavailable", "Run a Full Check for current evidence.") }
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.warning; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.warning; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        visible: root.hidhideRepairPlan && root.hidhideRepairPlan.success === true
                        text: "APPLY EXISTING REPAIR"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: {
                            const next = String(root.hidhideRepairPlan.nextAction || "")
                            const result = next === "repair-hidhide-access" ? { success: backend.repairHidHideAccess(), title: "HidHide access repair requested", message: "The existing mapper-only transaction was used." } : backend.repairSetupHealth()
                            root.showActionFeedback(result, "HidHide repair did not start", "The current setup evidence no longer supports that repair.")
                        }
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        text: "OPEN DOCTOR"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.showActionFeedback(backend.openHidHideDoctor(), "HidHide Doctor is unavailable", "Copy evidence or install the optional Doctor alongside HOTAS BF6.")
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        visible: root.toneFor(root.isolationCheck) !== "healthy"
                        text: "VIEW SETUP HEALTH"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: setupHealthDialog.open()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: root.isolationDetailsOpen ? "HIDE TECHNICAL DETAILS" : "TECHNICAL DETAILS"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.isolationDetailsOpen = !root.isolationDetailsOpen
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                Rectangle {
                    visible: root.isolationDetailsOpen
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? isolationDetails.implicitHeight + deck.space24 : 0
                    radius: deck.radiusControl
                    color: deck.elevatedSurface
                    border.color: deck.border
                    ColumnLayout {
                        id: isolationDetails
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space4
                        Text { text: "HidHide status"; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true }
                        Text { text: root.hidhideAvailable ? "Service and tools are available." : "Service or tools are unavailable."; color: deck.textSecondary; font.pixelSize: deck.scale(10) }
                        Text { text: root.hidhideCloakStateKnown ? (root.hidhideCloaked ? "Cloaking is enabled." : "Cloaking is disabled.") : "Cloaking state is still unknown."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: root.hidhideMapperAllowed ? "HOTAS BF6 is allow-listed." : "HOTAS BF6 is not allow-listed."; color: deck.textSecondary; font.pixelSize: deck.scale(10) }
                        Repeater {
                            model: root.hidhideHealth.checks || []
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: String(modelData.operation || "CHECK") + " · " + String(modelData.state || "UNKNOWN") + (modelData.nativeError ? " · " + String(modelData.nativeError.message || "") : "")
                                color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }
        }

        Item { id: verificationSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "SETUP / VERIFICATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true }
        FlightDeckCard {
            objectName: "flightDeckCalibration"
            tokens: deck
            contentPadding: deck.cardPaddingCompact
            Layout.fillWidth: true
            implicitHeight: calibrationEntry.implicitHeight + contentPadding * 2
            color: deck.secondarySurface
            ColumnLayout {
                id: calibrationEntry
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space8
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text { text: "CONTROLLER CALIBRATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                        Text { text: backend.calibrationActive ? "Calibration in progress" : (backend.calibrationSuccess ? "Calibration complete" : "Capture controller ranges and centered controls"); color: backend.calibrationActive ? deck.attention : backend.calibrationSuccess ? deck.healthy : deck.textPrimary; font.family: deck.displayFont; font.pixelSize: deck.scale(15); font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: backend.calibrationStatus || "Calibration is scoped to the selected controller. Profiles, mappings, and Automation are not changed."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    Button {
                        id: openCalibration
                        objectName: "flightDeckOpenCalibration"
                        text: backend.calibrationActive ? "RESUME" : "CALIBRATE"
                        enabled: backend.physicalConnected
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.controlHeight
                        onClicked: calibrationDialog.open()
                        background: Rectangle { radius: deck.radiusControl; color: parent.enabled ? deck.accent : deck.disabled; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }
        FlightDeckCard {
            tokens: deck
            contentPadding: deck.cardPaddingCompact
            Layout.fillWidth: true
            implicitHeight: advancedContent.implicitHeight + contentPadding * 2
            ColumnLayout {
                id: advancedContent
                anchors.fill: parent
                anchors.margins: parent.contentPadding
                spacing: deck.space4
                Text { text: "ADVANCED / TECHNICAL"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                Text { text: "For device identifiers, raw controller state, and detailed troubleshooting, use Diagnostics."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button {
                    text: "OPEN DIAGNOSTICS"
                    focusPolicy: Qt.StrongFocus
                    implicitHeight: deck.compactControlHeight
                    onClicked: root.navigateToPage(3)
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }

    FlightDeckDialog {
        id: createRigDialog
        objectName: "flightDeckCreateRigDialog"
        tokens: deck
        heading: "Create Device Rig"
        tone: "informational"
        preferredWidth: 720
        property var draftMembers: []
        property string outputLayoutId: ""

        function resetDraft() {
            draftMembers = [];
            outputLayoutId = root.outputLayouts.length ? String(root.outputLayouts[0].id || "") : "";
            rigCreateName.text = "";
        }
        function draftEntry(id) {
            for (let index = 0; index < draftMembers.length; ++index) {
                if (String(draftMembers[index].id || "") === String(id || ""))
                    return draftMembers[index];
            }
            return null;
        }
        function included(id) { return draftEntry(id) !== null; }
        function required(id) {
            const entry = draftEntry(id);
            return entry ? !!entry.required : true;
        }
        function setIncluded(controller, enabled) {
            const id = root.controllerCandidateId(controller);
            if (!id) return;
            const next = draftMembers.slice();
            for (let index = next.length - 1; index >= 0; --index) {
                if (String(next[index].id || "") === id) next.splice(index, 1);
            }
            if (enabled) next.push({ id: id, required: true });
            draftMembers = next;
        }
        function setRequired(id, value) {
            const next = draftMembers.slice();
            for (let index = 0; index < next.length; ++index) {
                if (String(next[index].id || "") === String(id || ""))
                    next[index] = { id: String(id || ""), required: !!value };
            }
            draftMembers = next;
        }
        function createOutput() {
            root.outputCreationForNewRig = true;
            close();
            createVirtualOutputDialog.openFor("");
        }
        function createRig() {
            const ids = draftMembers.map(function(member) { return String(member.id || ""); });
            const result = backend.createDeviceRigResult(rigCreateName.text, ids, outputLayoutId);
            root.showActionFeedback(result, "Device Rig was not created", "Review the selected controllers and Virtual Output, then try again.");
            if (!result.success) return;
            for (let index = 0; index < draftMembers.length; ++index) {
                if (!draftMembers[index].required)
                    backend.setDeviceRigMemberRequired(result.objectId, draftMembers[index].id, false);
            }
            root.selectedRigId = String(result.objectId || "");
            close();
            // A new Rig must offer its next configuration step in the same
            // journey. Opening details alone used to leave a user to infer
            // that a Profile was still needed.
            createdRigNextStepDialog.openFor(root.selectedRig());
        }
        onOpened: resetDraft()
        contentItem: Flickable {
            width: createRigDialog.availableWidth
            implicitHeight: Math.min(createRigContent.implicitHeight, createRigDialog.maximumBodyHeight)
            contentWidth: width
            contentHeight: createRigContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: createRigContent
                width: parent.width
                spacing: deck.space12
                Text { text: "A Device Rig is the canonical physical-controller and Virtual Output group used by Profiles and Automatic Activation."; color: deck.textSecondary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: "RIG NAME"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                TextField {
                    id: rigCreateName
                    objectName: "flightDeckCreateRigName"
                    Layout.fillWidth: true
                    placeholderText: "e.g. Helicopter controls"
                    selectByMouse: true
                    color: deck.textPrimary
                    font.family: deck.bodyFont
                    background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                }
                Text { text: "PHYSICAL CONTROLLERS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; Layout.topMargin: deck.space4 }
                Text { text: "Include every controller used by this setup. Required controllers gate automatic activation; missing Optional controllers reduce capability without blocking it."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater {
                    // QVariantList-backed models do not consistently expose
                    // modelData through a Popup/Flickable delegate. Index the
                    // authoritative presentation list explicitly so a new
                    // Rig always offers every visible physical controller.
                    model: root.controllerItems.length
                    delegate: Rectangle {
                        required property int index
                        readonly property var controller: root.controllerItems[index] || ({})
                        readonly property string controllerId: root.controllerCandidateId(controller)
                        visible: controllerId.length > 0 && !controller.ambiguous
                        Layout.fillWidth: true
                        implicitHeight: visible ? draftControllerRow.implicitHeight + deck.space16 : 0
                        radius: deck.radiusControl
                        color: createRigDialog.included(controllerId) ? deck.secondarySurface : deck.elevatedSurface
                        border.color: createRigDialog.included(controllerId) ? deck.accent : deck.border
                        RowLayout {
                            id: draftControllerRow
                            anchors.fill: parent
                            anchors.margins: deck.space8
                            spacing: deck.space8
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2
                                Text { text: String(controller.name || "Controller"); color: deck.textPrimary; font.pixelSize: deck.scale(11); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: controller.connected ? (controller.verified ? "Connected · verified" : "Connected · setup needed") : "Saved / Offline"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            RigButton { objectName: "flightDeckCreateRigInclude_" + controllerId; text: createRigDialog.included(controllerId) ? "INCLUDED" : "INCLUDE"; subdued: !createRigDialog.included(controllerId); onClicked: createRigDialog.setIncluded(controller, !createRigDialog.included(controllerId)) }
                            RigButton { objectName: "flightDeckCreateRigRequired_" + controllerId; visible: createRigDialog.included(controllerId); text: createRigDialog.required(controllerId) ? "REQUIRED" : "OPTIONAL"; subdued: createRigDialog.required(controllerId) === false; onClicked: createRigDialog.setRequired(controllerId, !createRigDialog.required(controllerId)) }
                        }
                    }
                }
                Text { visible: root.controllerItems.length === 0; text: "No physical controllers are available. Scan for devices before creating a Rig."; color: deck.attention; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: "VIRTUAL OUTPUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; Layout.topMargin: deck.space4 }
                RowLayout {
                    Layout.fillWidth: true
                    RigCombo {
                        id: rigCreateOutput
                        objectName: "flightDeckCreateRigOutput"
                        Layout.fillWidth: true
                        model: root.outputLayouts
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: {
                            for (let index = 0; index < root.outputLayouts.length; ++index)
                                if (String(root.outputLayouts[index].id || "") === createRigDialog.outputLayoutId) return index;
                            return -1;
                        }
                        onActivated: createRigDialog.outputLayoutId = String(currentValue || "")
                    }
                    RigButton { text: "CREATE OUTPUT"; subdued: true; onClicked: createRigDialog.createOutput() }
                }
                Text { text: root.outputLayouts.length ? "Choose the existing Virtual Output this Rig should own. You can add more outputs in Rig Details." : "Create a Virtual Output before this Rig can be saved."; color: deck.textMuted; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { visible: rigCreateName.text.trim().length === 0 || createRigDialog.draftMembers.length === 0 || !createRigDialog.outputLayoutId; text: "Enter a name, include at least one controller, and select a Virtual Output to continue."; color: deck.attention; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    RigButton { text: "CANCEL"; subdued: true; onClicked: createRigDialog.close() }
                    RigButton { objectName: "flightDeckCreateRigConfirm"; text: "CREATE RIG"; enabled: rigCreateName.text.trim().length > 0 && createRigDialog.draftMembers.length > 0 && createRigDialog.outputLayoutId.length > 0; onClicked: createRigDialog.createRig() }
                }
            }
        }
    }

    FlightDeckDialog {
        id: createdRigNextStepDialog
        objectName: "flightDeckCreatedRigNextStepDialog"
        tokens: deck
        heading: "Choose what to configure next"
        tone: "informational"
        preferredWidth: 600
        property var rig: ({})
        function openFor(value) {
            rig = value || ({});
            open();
        }
        contentItem: ColumnLayout {
            width: createdRigNextStepDialog.availableWidth
            spacing: deck.space12
            Text {
                Layout.fillWidth: true
                text: "" + String(createdRigNextStepDialog.rig.name || "This Device Rig")
                    + " owns " + root.outputName(String(createdRigNextStepDialog.rig.primaryOutputLayoutId || ""))
                    + ". Choose a Profile path now, or review the Rig without changing runtime mapping."
                color: deck.textSecondary
                font.pixelSize: deck.scale(11)
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "No controller, profile, or output has been activated. Your current Mapping On/Off choice is unchanged."
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: deck.scale(9)
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                RigButton {
                    objectName: "flightDeckCreatedRigBlankProfile"
                    text: "CREATE BLANK PROFILE"
                    onClicked: {
                        root.openUnmappedRigProfileWorkflow(createdRigNextStepDialog.rig, "blank");
                        createdRigNextStepDialog.close();
                    }
                }
                RigButton {
                    objectName: "flightDeckCreatedRigCopyProfile"
                    text: "COPY PROFILE"
                    subdued: true
                    onClicked: {
                        root.openUnmappedRigProfileWorkflow(createdRigNextStepDialog.rig, "copy");
                        createdRigNextStepDialog.close();
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                RigButton {
                    objectName: "flightDeckCreatedRigChooseProfile"
                    text: "CHOOSE EXISTING PROFILE"
                    subdued: true
                    onClicked: {
                        root.openUnmappedRigProfileWorkflow(createdRigNextStepDialog.rig, "choose");
                        createdRigNextStepDialog.close();
                    }
                }
                Item { Layout.fillWidth: true }
                RigButton {
                    objectName: "flightDeckCreatedRigReview"
                    text: "REVIEW RIG"
                    subdued: true
                    onClicked: {
                        const rigId = String(createdRigNextStepDialog.rig.id || "");
                        createdRigNextStepDialog.close();
                        if (rigId) root.openRigDetails(rigId);
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: readOnlyPhysicalInputTestDialog
        objectName: "flightDeckReadOnlyPhysicalInputTestDialog"
        tokens: deck
        heading: "Test physical input"
        tone: String(test.state || "").indexOf("unavailable") >= 0 || String(test.state || "") === "offline"
            ? "attention" : "informational"
        preferredWidth: 560
        readonly property var test: root.readOnlyPhysicalInputTestPresentationOverride == null
            ? (backend.readOnlyPhysicalInputTest || ({}))
            : root.readOnlyPhysicalInputTestPresentationOverride
        readonly property bool hasCurrentReport: Boolean(test.reportAvailable) && Boolean(test.reportFresh)
        property bool technicalDetailsExpanded: false
        function povDisplay(value) {
            const raw = Number(value)
            if (raw === -1) return "CENTER"
            if (!isFinite(raw) || Math.floor(raw) !== raw || raw < 0 || raw >= 36000)
                return "NO DIRECTION"
            const degrees = raw / 100
            return (degrees % 1 === 0 ? String(degrees) : degrees.toFixed(2)) + "°"
        }
        onClosed: backend.stopReadOnlyPhysicalInputTest()
        contentItem: ColumnLayout {
            width: readOnlyPhysicalInputTestDialog.availableWidth
            spacing: deck.space12
            Text {
                text: String(readOnlyPhysicalInputTestDialog.test.title || "Physical controller")
                color: deck.textPrimary
                font.family: deck.displayFont
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                FlightDeckStatusChip {
                    tokens: deck
                    label: "INPUT"
                    value: String(readOnlyPhysicalInputTestDialog.test.state || "CHECKING").toUpperCase()
                    tone: readOnlyPhysicalInputTestDialog.test.inputDetected ? "healthy"
                        : readOnlyPhysicalInputTestDialog.test.available ? "informational" : "attention"
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: String(readOnlyPhysicalInputTestDialog.test.axisCount || 0) + " axes  ·  "
                        + String(readOnlyPhysicalInputTestDialog.test.buttonCount || 0) + " buttons  ·  "
                        + String(readOnlyPhysicalInputTestDialog.test.povCount || 0) + " hats"
                    color: deck.textSecondary
                    font.family: deck.telemetryFont
                    font.pixelSize: 9
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignRight
                }
            }
            Text {
                text: String(readOnlyPhysicalInputTestDialog.test.message || "")
                color: readOnlyPhysicalInputTestDialog.test.inputDetected ? deck.healthy : deck.textSecondary
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Button {
                objectName: "flightDeckInputTestTechnicalDetailsToggle"
                text: readOnlyPhysicalInputTestDialog.technicalDetailsExpanded ? "HIDE TECHNICAL DETAILS" : "SHOW TECHNICAL DETAILS"
                focusPolicy: Qt.StrongFocus
                implicitHeight: deck.compactControlHeight
                onClicked: readOnlyPhysicalInputTestDialog.technicalDetailsExpanded = !readOnlyPhysicalInputTestDialog.technicalDetailsExpanded
                background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            ColumnLayout {
                visible: readOnlyPhysicalInputTestDialog.technicalDetailsExpanded
                Layout.fillWidth: true
                spacing: deck.space4
                Text {
                    text: "SESSION · " + String(readOnlyPhysicalInputTestDialog.test.session || "not started")
                        + "  ·  generation " + String(readOnlyPhysicalInputTestDialog.test.configurationGeneration || 0)
                        + "  ·  exact DirectInput ID " + String(readOnlyPhysicalInputTestDialog.test.directInputId || "unavailable")
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 8
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                }
                Repeater {
                    model: readOnlyPhysicalInputTestDialog.test.axes || []
                    delegate: Text {
                        required property var modelData
                        text: String(modelData.label || "Axis") + " · canonical slot "
                            + String(modelData.index) + (modelData.rangeKnown
                                ? " · native range " + String(modelData.nativeMinimum) + " to "
                                    + String(modelData.nativeMaximum) : " · native range not reported")
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                    }
                }
                Repeater {
                    model: readOnlyPhysicalInputTestDialog.test.povs || []
                    delegate: Text {
                        required property var modelData
                        text: "POV " + String(modelData.index || 0) + " · raw DirectInput value " + String(modelData.value)
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 8
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 214
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: deck.space8
                    Text { text: "AXES · PHYSICAL VALUES"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                    Flow {
                        Layout.fillWidth: true
                        spacing: deck.space6
                        Repeater {
                            model: readOnlyPhysicalInputTestDialog.test.axes || []
                            delegate: Rectangle {
                                required property var modelData
                                implicitWidth: axisState.implicitWidth + deck.space12
                                implicitHeight: axisState.implicitHeight + deck.space6
                                radius: deck.radiusControl
                                color: deck.secondarySurface
                                border.color: deck.border
                                Text {
                                    id: axisState
                                    anchors.centerIn: parent
                                    text: String(modelData.label || "Axis") + " · "
                                        + (Number(modelData.value || 0) >= 0 ? "+" : "")
                                        + Math.round(Number(modelData.value || 0) * 100) + "%"
                                    color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "flightDeckInputTestAxesEmpty"
                        visible: readOnlyPhysicalInputTestDialog.hasCurrentReport
                            && (readOnlyPhysicalInputTestDialog.test.axes || []).length === 0
                        text: "No axis controls were reported by this controller."
                        color: deck.textMuted; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    Text { text: "BUTTONS · CURRENT STATE"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                    Flow {
                        Layout.fillWidth: true
                        spacing: deck.space6
                        Repeater {
                            model: readOnlyPhysicalInputTestDialog.test.buttons || []
                            delegate: Rectangle {
                                required property var modelData
                                implicitWidth: buttonState.implicitWidth + deck.space10
                                implicitHeight: buttonState.implicitHeight + deck.space6
                                radius: deck.radiusControl
                                color: modelData.pressed ? deck.selected : deck.secondarySurface
                                border.color: modelData.pressed ? deck.healthy : deck.border
                                Text { id: buttonState; anchors.centerIn: parent; text: "B" + String(modelData.index || 0) + " · " + (modelData.pressed ? "PRESSED" : "UP"); color: modelData.pressed ? deck.healthy : deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                            }
                        }
                    }
                    Text {
                        objectName: "flightDeckInputTestButtonsEmpty"
                        visible: readOnlyPhysicalInputTestDialog.hasCurrentReport
                            && (readOnlyPhysicalInputTestDialog.test.buttons || []).length === 0
                        text: "No buttons were reported by this controller."
                        color: deck.textMuted; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    Text { text: "POV · CURRENT STATE"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                    Flow {
                        Layout.fillWidth: true
                        spacing: deck.space6
                        Repeater {
                            model: readOnlyPhysicalInputTestDialog.test.povs || []
                            delegate: Rectangle {
                                required property var modelData
                                implicitWidth: povState.implicitWidth + deck.space10
                                implicitHeight: povState.implicitHeight + deck.space6
                                radius: deck.radiusControl
                                color: deck.secondarySurface
                                border.color: deck.border
                                Text { id: povState; anchors.centerIn: parent; text: "POV " + String(modelData.index || 0) + " · " + readOnlyPhysicalInputTestDialog.povDisplay(modelData.value); color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                            }
                        }
                    }
                    Text {
                        objectName: "flightDeckInputTestPovsEmpty"
                        visible: readOnlyPhysicalInputTestDialog.hasCurrentReport
                            && (readOnlyPhysicalInputTestDialog.test.povs || []).length === 0
                        text: "No POV hats were reported by this controller."
                        color: deck.textMuted; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    Text {
                        objectName: "flightDeckInputTestNoReport"
                        visible: !readOnlyPhysicalInputTestDialog.hasCurrentReport
                        text: "No exact state report is available yet. The message above states the specific DirectInput result and next action."
                        color: deck.textMuted; font.pixelSize: deck.scale(9); Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
            Text {
                text: "Move a control to see its input. This test does not change your mappings or send controls to the virtual controller."
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 9
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "CLOSE"
                    focusPolicy: Qt.StrongFocus
                    implicitHeight: deck.compactControlHeight
                    onClicked: readOnlyPhysicalInputTestDialog.close()
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }

    CreateVirtualOutputDialog {
        id: createVirtualOutputDialog
        objectName: "flightDeckCreateVirtualOutputDialog"
        backendObject: backend
        tokens: deck
        rigItems: root.rigItems
        onCreated: function(result) {
            const message = String(result.message || "Virtual Output saved.")
            root.showActionFeedback(result, "Virtual Output created", message)
            if (root.outputCreationForNewRig) {
                root.outputCreationForNewRig = false
                createRigDialog.outputLayoutId = String(result.objectId || "")
                createRigDialog.open()
            }
        }
        onFailed: function(result) {
            root.showActionFeedback(result, "Virtual Output was not created",
                "Review the name, vJoy Device ID, and capabilities, then try again.")
        }
        onUpdated: function(result) {
            root.showActionFeedback(result, "Virtual Output was not updated",
                String(result.message || "Review the output contract and Setup Health."))
        }
    }

    FlightDeckDialog {
        id: rigDetailsDialog
        objectName: "flightDeckRigDetailsDialog"
        tokens: deck
        heading: root.selectedRig() ? String(root.selectedRig().name || "Device Rig") : "Device Rig"
        tone: root.rigTone(root.selectedRig()) === "fault" ? "fault" : root.rigTone(root.selectedRig()) === "attention" ? "attention" : "informational"
        preferredWidth: 920
        property string addMemberId: ""
        property string addOutputId: ""
        property bool renameVisible: false
        property bool addMemberVisible: false
        property bool manageOutputsVisible: false
        property bool technicalDetailsVisible: false
        property bool rigActionsVisible: false
        property string inspectedRigId: ""
        // Key expansion by the stable saved member ID. A telemetry refresh,
        // resize, text-size, or theme change must not collapse the row the
        // person is reviewing.
        property var expandedMemberIds: ({})
        function memberDetailsVisible(memberId) {
            return Boolean(expandedMemberIds[String(memberId || "")]);
        }
        function toggleMemberDetails(memberId) {
            const key = String(memberId || "");
            const next = {};
            const current = expandedMemberIds || {};
            for (const existingKey in current) next[existingKey] = current[existingKey];
            next[key] = !Boolean(next[key]);
            expandedMemberIds = next;
        }
        function memberOutputName(member, rig) {
            const outputs = (rig || {}).outputs || [];
            for (let index = 0; index < outputs.length; ++index) {
                if (String(outputs[index].id || "") === String((member || {}).preferredOutputLayoutId || ""))
                    return String(outputs[index].name || "Virtual Output");
            }
            return "";
        }
        function resetChoices() {
            const rig = root.selectedRig();
            const members = root.availableRigMemberChoices(rig);
            const outputs = root.availableOutputChoices(rig);
            addMemberId = members.length ? String(members[0].id || "") : "";
            addOutputId = outputs.length ? String(outputs[0].id || "") : "";
            if (rig) rigRename.text = String(rig.name || "");
        }
        onOpened: {
            const currentRigId = String((root.selectedRig() || {}).id || "");
            if (inspectedRigId !== currentRigId) {
                inspectedRigId = currentRigId;
                expandedMemberIds = ({});
                renameVisible = false;
                addMemberVisible = false;
                manageOutputsVisible = false;
                technicalDetailsVisible = false;
                rigActionsVisible = false;
            }
            resetChoices();
        }
        footer: Rectangle {
            objectName: "flightDeckRigDetailsFooter"
            implicitHeight: footerActions.implicitHeight + deck.space16
            color: deck.secondarySurface
            border.width: 1
            border.color: deck.divider
            RowLayout {
                id: footerActions
                anchors.fill: parent
                anchors.leftMargin: deck.dialogPadding
                anchors.rightMargin: deck.dialogPadding
                anchors.topMargin: deck.space8
                anchors.bottomMargin: deck.space8
                spacing: deck.space8
                RigButton {
                    objectName: "flightDeckRigSetupHealth"
                    text: rigDetailsContent.rig && rigDetailsContent.rig.setupNotChecked ? "CHECK SETUP" : "SETUP HEALTH"
                    subdued: true
                    enabled: !!rigDetailsContent.rig
                    onClicked: {
                        backend.setEditingDeviceContext(String(rigDetailsContent.rig.id || ""), []);
                        rigDetailsDialog.close();
                        setupHealthDialog.open();
                    }
                }
                Item { Layout.fillWidth: true }
                RigButton {
                    objectName: "flightDeckRigUseThis"
                    visible: !!rigDetailsContent.rig && !rigDetailsContent.rig.configured
                    text: "USE THIS RIG"
                    enabled: !!rigDetailsContent.rig && !!rigDetailsContent.rig.enabled
                    onClicked: root.activateRig(rigDetailsContent.rig)
                }
                RigButton {
                    objectName: "flightDeckRigDetailsClose"
                    text: "CLOSE"
                    subdued: true
                    onClicked: rigDetailsDialog.close()
                }
            }
        }
        contentItem: Flickable {
            width: rigDetailsDialog.availableWidth
            implicitHeight: Math.min(rigDetailsContent.implicitHeight, rigDetailsDialog.maximumBodyHeight)
            contentWidth: width
            contentHeight: rigDetailsContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: rigDetailsContent
                readonly property var rig: root.selectedRig()
                width: parent.width
                spacing: deck.space12
                RowLayout {
                    objectName: "flightDeckRigDetailsSummary"
                    Layout.fillWidth: true
                    FlightDeckStatusChip {
                        tokens: deck
                        label: rigDetailsContent.rig && rigDetailsContent.rig.configured ? "ACTIVE RIG" : "NOT ACTIVE"
                        value: root.rigState(rigDetailsContent.rig)
                        tone: root.rigTone(rigDetailsContent.rig)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: rigDetailsContent.rig && rigDetailsContent.rig.configured
                            ? "This is the active rig. Editing this view does not change which rig is in use."
                            : "This is not the active rig. Use the explicit action below to make it active."
                        color: deck.textSecondary
                        font.family: deck.bodyFont
                        font.pixelSize: deck.bodySmall
                        wrapMode: Text.WordWrap
                    }
                    RigButton {
                        objectName: "flightDeckRigRenameToggle"
                        text: rigDetailsDialog.renameVisible ? "HIDE RENAME" : "RENAME"
                        subdued: true
                        enabled: !!rigDetailsContent.rig
                        onClicked: {
                            rigDetailsDialog.renameVisible = !rigDetailsDialog.renameVisible;
                            if (rigDetailsDialog.renameVisible)
                                rigRename.text = String(rigDetailsContent.rig.name || "");
                        }
                    }
                }
                Text {
                    objectName: "flightDeckRigSetupImpact"
                    Layout.fillWidth: true
                    visible: rigDetailsContent.rig && (rigDetailsContent.rig.setupNotChecked
                        || rigDetailsContent.rig.unmapped
                        || root.rigTone(rigDetailsContent.rig) === "attention"
                        || root.rigTone(rigDetailsContent.rig) === "fault")
                    text: rigDetailsContent.rig && rigDetailsContent.rig.setupNotChecked
                        ? "Setup has not been checked. Run Setup Health before relying on this rig."
                        : rigDetailsContent.rig && rigDetailsContent.rig.unmapped
                          ? "This rig has no mapped profile. You can choose one without activating the rig."
                          : "This rig needs attention before it is ready to use."
                    color: root.rigTone(rigDetailsContent.rig) === "fault" ? deck.fault : deck.attention
                    font.family: deck.bodyFont
                    font.pixelSize: deck.bodySmall
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    objectName: "flightDeckRigRenameEditor"
                    Layout.fillWidth: true
                    visible: rigDetailsDialog.renameVisible
                    TextField {
                        id: rigRename
                        objectName: "flightDeckRigRename"
                        Layout.fillWidth: true
                        selectByMouse: true
                        color: deck.textPrimary
                        font.family: deck.bodyFont
                        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    }
                    RigButton { text: "SAVE NAME"; enabled: rigDetailsContent.rig && rigRename.text.trim().length > 0 && rigRename.text.trim() !== String(rigDetailsContent.rig.name || ""); onClicked: root.reportBooleanAction(backend.renameDeviceRig(String(rigDetailsContent.rig.id || ""), rigRename.text), "Rig renamed", "The canonical Device Rig name was updated.", "Rig name was not updated", "Names must be unique and contain text.") }
                    RigButton { objectName: "flightDeckRigRenameCancel"; text: "CANCEL"; subdued: true; onClicked: { rigRename.text = String((rigDetailsContent.rig || {}).name || ""); rigDetailsDialog.renameVisible = false; } }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: deck.divider }
                Text { text: "DEFAULT PROFILE"; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: deck.bodySmall; font.bold: true }
                RigCombo {
                    id: defaultRigProfile
                    Layout.fillWidth: true
                    model: root.rigDefaultProfileChoices(rigDetailsContent.rig)
                    textRole: "name"
                    currentIndex: {
                        const rig = rigDetailsContent.rig || ({});
                        for (let index = 0; index < model.length; ++index) {
                            const choice = model[index];
                            if (Boolean(choice.explicitlyNone) === Boolean(rig.defaultProfileNone)
                                    && String(choice.id || "") === String(rig.defaultProfileId || "")) return index;
                        }
                        return 0;
                    }
                    onActivated: {
                        const choice = model[index] || ({});
                        root.reportBooleanAction(backend.setDeviceRigDefaultProfile(
                            String(rigDetailsContent.rig.id || ""), String(choice.id || ""),
                            Boolean(choice.explicitlyNone)), "Rig default Profile updated",
                            "This is a convenience policy only; the Rig remains independently activatable.",
                            "Rig default Profile was not updated", "Choose a Profile assigned to this Device Rig.");
                    }
                }
                Text { text: "Automatic finds a compatible profile. None leaves this rig without a mapping. Choosing a profile does not activate this rig."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: deck.bodySmall; Layout.fillWidth: true; wrapMode: Text.WordWrap }

                Text { text: "CONTROLLERS"; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: deck.bodySmall; font.bold: true; Layout.topMargin: deck.space4 }
                Text { text: "Required controllers affect readiness; optional controllers may be offline. Open Details to change a controller's rig settings."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: deck.bodySmall; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater {
                    model: rigDetailsContent.rig ? (rigDetailsContent.rig.members || []) : []
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool detailsVisible: rigDetailsDialog.memberDetailsVisible(String(modelData.id || ""))
                        Layout.fillWidth: true
                        implicitHeight: memberDetail.implicitHeight + deck.space20
                        radius: deck.radiusControl
                        color: deck.elevatedSurface
                        border.color: deck.statusColor(root.memberTone(modelData, rigDetailsContent.rig))
                        ColumnLayout {
                            id: memberDetail
                            anchors.fill: parent
                            anchors.margins: deck.space10
                            spacing: deck.space6
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: root.markerFor(root.memberTone(modelData, rigDetailsContent.rig)); color: deck.statusColor(root.memberTone(modelData, rigDetailsContent.rig)); font.pixelSize: deck.scale(14); font.bold: true }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Text { text: String(modelData.name || "Controller"); color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: deck.body; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text {
                                        text: (modelData.connected ? "Connected" : "Offline") + " · "
                                            + (modelData.required ? "Required" : "Optional")
                                            + ((rigDetailsContent.rig.outputs || []).length > 1 && rigDetailsDialog.memberOutputName(modelData, rigDetailsContent.rig).length > 0
                                                ? " · " + rigDetailsDialog.memberOutputName(modelData, rigDetailsContent.rig) : "")
                                        color: deck.statusColor(root.memberTone(modelData, rigDetailsContent.rig))
                                        font.family: deck.bodyFont
                                        font.pixelSize: deck.bodySmall
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                RigButton {
                                    objectName: "flightDeckRigMemberDetails_" + String(modelData.id || "")
                                    text: detailsVisible ? "HIDE DETAILS" : "DETAILS"
                                    subdued: true
                                    onClicked: rigDetailsDialog.toggleMemberDetails(String(modelData.id || ""))
                                }
                            }
                            Rectangle { visible: detailsVisible; Layout.fillWidth: visible; Layout.preferredHeight: visible ? 1 : 0; color: deck.divider }
                            Text {
                                visible: detailsVisible
                                Layout.fillWidth: visible
                                text: "Verification: " + root.memberState(modelData, rigDetailsContent.rig)
                                color: deck.statusColor(root.memberTone(modelData, rigDetailsContent.rig))
                                font.family: deck.bodyFont
                                font.pixelSize: deck.bodySmall
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                visible: detailsVisible
                                Layout.fillWidth: true
                                RigButton {
                                    text: modelData.required ? "MAKE OPTIONAL" : "MAKE REQUIRED"
                                    subdued: true
                                    onClicked: root.reportBooleanAction(backend.setDeviceRigMemberRequired(String(rigDetailsContent.rig.id || ""), String(modelData.id || ""), !modelData.required), "Controller requirement updated", !modelData.required ? "This controller is now required for automatic activation." : "This controller is now optional and will not block automatic activation while offline.", "Controller requirement was not updated", "Refresh the Rig and try again.")
                                }
                                RigCombo {
                                    Layout.fillWidth: true
                                    model: rigDetailsContent.rig ? (rigDetailsContent.rig.outputs || []) : []
                                    textRole: "name"
                                    valueRole: "id"
                                    currentIndex: {
                                        const outputs = rigDetailsContent.rig ? (rigDetailsContent.rig.outputs || []) : [];
                                        for (let index = 0; index < outputs.length; ++index)
                                            if (String(outputs[index].id || "") === String(modelData.preferredOutputLayoutId || "")) return index;
                                        return 0;
                                    }
                                    onActivated: root.reportBooleanAction(backend.setDeviceRigMemberOutput(String(rigDetailsContent.rig.id || ""), String(modelData.id || ""), String(currentValue || "")), "Controller output assigned", "This controller will use the selected Virtual Output.", "Controller output was not assigned", "Choose an enabled output in this Rig and try again.")
                                }
                                RigButton { text: "OPEN CONTROLLER"; subdued: true; onClicked: { backend.setEditingDeviceContext(String(rigDetailsContent.rig.id || ""), [String(modelData.id || "")]); rigDetailsDialog.close(); Qt.callLater(function() { root.contentY = Math.max(0, controllersSection.y - deck.space8); }); } }
                                RigButton {
                                    text: modelData.ambiguous ? "IDENTIFY CONTROLLER"
                                        : !modelData.connected ? "RESCAN"
                                        : !modelData.verified ? "VERIFY CONTROLLER" : "✓ VERIFIED"
                                    subdued: !!modelData.verified
                                    enabled: !modelData.verified && String(modelData.verificationState || "") !== "VERIFYING"
                                    onClicked: {
                                        if (!modelData.connected) root.rescanController()
                                        else root.verifyController(String(modelData.id || ""))
                                    }
                                }
                                RigButton { text: "REMOVE"; destructive: true; enabled: (rigDetailsContent.rig.members || []).length > 1; onClicked: root.reportBooleanAction(backend.removeDeviceRigMember(String(rigDetailsContent.rig.id || ""), String(modelData.id || "")), "Controller removed", "The controller and its Rig-specific mapping payload were removed from this Rig.", "Controller was not removed", "A Device Rig must retain at least one controller.") }
                            }
                        }
                    }
                }
                RigButton {
                    objectName: "flightDeckRigAddControllerToggle"
                    text: rigDetailsDialog.addMemberVisible ? "CANCEL ADD CONTROLLER" : "ADD CONTROLLER"
                    subdued: true
                    enabled: !!rigDetailsContent.rig
                    onClicked: rigDetailsDialog.addMemberVisible = !rigDetailsDialog.addMemberVisible
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: rigDetailsDialog.addMemberVisible
                    RigCombo {
                        id: rigMemberAdder
                        Layout.fillWidth: true
                        model: root.availableRigMemberChoices(rigDetailsContent.rig)
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: {
                            const choices = root.availableRigMemberChoices(rigDetailsContent.rig)
                            for (let index = 0; index < choices.length; ++index) {
                                if (String(choices[index].id || "") === rigDetailsDialog.addMemberId)
                                    return index
                            }
                            return choices.length ? 0 : -1
                        }
                        onActivated: rigDetailsDialog.addMemberId = String(currentValue || "")
                    }
                    RigButton { objectName: "flightDeckRigAddController"; text: "ADD SELECTED"; enabled: rigMemberAdder.currentIndex >= 0; onClicked: { const controller = root.controllerForCandidate(rigDetailsDialog.addMemberId); const added = controller && controller.id ? backend.addDeviceRigMember(String(rigDetailsContent.rig.id || ""), String(controller.id || ""), true) : controller ? backend.addDetectedDeviceToRig(String(rigDetailsContent.rig.id || ""), String(controller.directInputId || ""), true) : false; root.reportBooleanAction(added, "Controller added", "Review the controller routes and requirement before activation.", "Controller was not added", "Refresh devices or choose a controller that is not already in this Rig."); rigDetailsDialog.resetChoices(); } }
                }

                Text { text: "VIRTUAL OUTPUTS"; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: deck.bodySmall; font.bold: true; Layout.topMargin: deck.space4 }
                Repeater {
                    model: rigDetailsContent.rig ? (rigDetailsContent.rig.outputs || []) : []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: outputDetail.implicitHeight + deck.space16
                        radius: deck.radiusControl
                        color: deck.elevatedSurface
                        border.color: modelData.ready ? deck.border : deck.attention
                        ColumnLayout {
                            id: outputDetail
                            anchors.fill: parent
                            anchors.margins: deck.space8
                            spacing: deck.space8
                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Text { text: String(modelData.name || "Virtual Output"); color: deck.textPrimary; font.family: deck.bodyFont; font.pixelSize: deck.body; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text {
                                        text: (modelData.primary ? "Primary · " : "Secondary · ")
                                            + (modelData.enabled ? "Enabled" : "Disabled") + " · "
                                            + (modelData.ready ? "Available" : "Needs attention")
                                            + " · " + Number(modelData.routeCount || 0) + " routes"
                                        color: modelData.ready ? deck.textSecondary : deck.attention
                                        font.family: deck.bodyFont
                                        font.pixelSize: deck.bodySmall
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                RigButton { objectName: "flightDeckRigOutputOpen_" + String(modelData.id || ""); text: "OPEN OUTPUT"; subdued: true; onClicked: { rigDetailsDialog.close(); root.openRigOutput(String(modelData.id || "")); } }
                            }
                            RowLayout {
                                visible: rigDetailsDialog.manageOutputsVisible
                                Layout.fillWidth: true
                                RigButton { text: modelData.primary ? "PRIMARY" : "MAKE PRIMARY"; subdued: !!modelData.primary; enabled: !modelData.primary && !!modelData.enabled; onClicked: root.reportBooleanAction(backend.setDeviceRigPrimaryOutput(String(rigDetailsContent.rig.id || ""), String(modelData.id || "")), "Rig primary output updated", "Profiles assigned to this Rig now use this Virtual Output.", "Rig primary output was not updated", "Choose an enabled Virtual Output in this Rig.") }
                                RigButton { text: modelData.enabled ? "DISABLE OUTPUT" : "ENABLE OUTPUT"; subdued: true; onClicked: root.reportBooleanAction(backend.setDeviceRigOutputEnabled(String(rigDetailsContent.rig.id || ""), String(modelData.id || ""), !modelData.enabled), "Virtual Output updated", !modelData.enabled ? "This Virtual Output is enabled for the Rig." : "This Virtual Output is disabled for the Rig.", "Virtual Output was not updated", "A Rig needs at least one enabled, unassigned output.") }
                                RigButton { text: "REMOVE"; destructive: true; enabled: (rigDetailsContent.rig.outputs || []).length > 1; onClicked: root.reportBooleanAction(backend.removeDeviceRigOutput(String(rigDetailsContent.rig.id || ""), String(modelData.id || "")), "Virtual Output removed", "The output is no longer part of this Rig.", "Virtual Output was not removed", "Move member assignments first, then keep at least one output in the Rig.") }
                            }
                        }
                    }
                }
                RigButton {
                    objectName: "flightDeckRigManageOutputs"
                    text: rigDetailsDialog.manageOutputsVisible ? "HIDE OUTPUT MANAGEMENT" : "MANAGE OUTPUTS"
                    subdued: true
                    enabled: !!rigDetailsContent.rig
                    onClicked: rigDetailsDialog.manageOutputsVisible = !rigDetailsDialog.manageOutputsVisible
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: rigDetailsDialog.manageOutputsVisible
                    RigCombo {
                        id: rigOutputAdder
                        Layout.fillWidth: true
                        model: root.availableOutputChoices(rigDetailsContent.rig)
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: {
                            const choices = root.availableOutputChoices(rigDetailsContent.rig)
                            for (let index = 0; index < choices.length; ++index) {
                                if (String(choices[index].id || "") === rigDetailsDialog.addOutputId)
                                    return index
                            }
                            return choices.length ? 0 : -1
                        }
                        onActivated: rigDetailsDialog.addOutputId = String(currentValue || "")
                    }
                    RigButton { text: "ADD OUTPUT"; enabled: rigOutputAdder.currentIndex >= 0; onClicked: { const added = backend.addDeviceRigOutput(String(rigDetailsContent.rig.id || ""), rigDetailsDialog.addOutputId); root.reportBooleanAction(added, "Virtual Output added", "Assign controllers to the new output before removing another output.", "Virtual Output was not added", "Choose an available output and try again."); rigDetailsDialog.resetChoices(); } }
                    RigButton { text: "CREATE OUTPUT"; subdued: true; onClicked: root.openCreateVirtualOutput(String(rigDetailsContent.rig.id || "")) }
                }

                Text { visible: rigDetailsDialog.manageOutputsVisible && root.availableOutputChoices(rigDetailsContent.rig).length === 0; text: "No unused Virtual Output is available for this rig. Create one, or remove an output from another rig first."; color: deck.textMuted; font.family: deck.bodyFont; font.pixelSize: deck.bodySmall; Layout.fillWidth: true; wrapMode: Text.WordWrap }

                Text {
                    objectName: "flightDeckRigProfileReferences"
                    Layout.fillWidth: true
                    readonly property var references: root.profilesReferencingRig(rigDetailsContent.rig ? rigDetailsContent.rig.id : "")
                    text: "Used by " + references.length + (references.length === 1 ? " profile" : " profiles")
                        + (references.length ? ": " + references.join(", ") : ".")
                    color: deck.textSecondary
                    font.family: deck.bodyFont
                    font.pixelSize: deck.bodySmall
                    wrapMode: Text.WordWrap
                }

                RigButton {
                    objectName: "flightDeckRigTechnicalToggle"
                    text: rigDetailsDialog.technicalDetailsVisible ? "HIDE TECHNICAL DETAILS" : "TECHNICAL DETAILS"
                    subdued: true
                    enabled: !!rigDetailsContent.rig
                    onClicked: rigDetailsDialog.technicalDetailsVisible = !rigDetailsDialog.technicalDetailsVisible
                }
                ColumnLayout {
                    objectName: "flightDeckRigTechnicalDetails"
                    Layout.fillWidth: true
                    visible: rigDetailsDialog.technicalDetailsVisible
                    spacing: deck.space4
                    Text { Layout.fillWidth: true; text: "Rig ID: " + String((rigDetailsContent.rig || {}).id || "none"); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.caption; wrapMode: Text.WrapAnywhere }
                    Repeater {
                        model: rigDetailsContent.rig ? (rigDetailsContent.rig.members || []) : []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: "Controller ID: " + String(modelData.id || "none")
                                + "\nExpected identity: " + String(modelData.expectedIdentity || "Not recorded")
                                + "\nSeen identity: " + String(modelData.seenIdentity || "Not seen")
                                + "\nVerification: " + String(modelData.verificationDetail || modelData.verificationState || "Not recorded")
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.caption
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    Repeater {
                        model: rigDetailsContent.rig ? (rigDetailsContent.rig.outputs || []) : []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: "Output ID: " + String(modelData.id || "none")
                                + " · " + String(modelData.descriptor || modelData.status || "No descriptor")
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: deck.caption
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }

                RigButton {
                    objectName: "flightDeckRigActionsToggle"
                    text: rigDetailsDialog.rigActionsVisible ? "HIDE RIG ACTIONS" : "RIG ACTIONS"
                    destructive: rigDetailsDialog.rigActionsVisible
                    subdued: !rigDetailsDialog.rigActionsVisible
                    enabled: !!rigDetailsContent.rig
                    onClicked: rigDetailsDialog.rigActionsVisible = !rigDetailsDialog.rigActionsVisible
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: rigDetailsDialog.rigActionsVisible
                    spacing: deck.space6
                    Text { Layout.fillWidth: true; text: "Deleting a rig is permanent and clears its profile assignments. It never remaps profiles automatically."; color: deck.textSecondary; font.family: deck.bodyFont; font.pixelSize: deck.bodySmall; wrapMode: Text.WordWrap }
                    RigButton { objectName: "flightDeckRigDelete"; text: "DELETE RIG"; destructive: true; enabled: !!rigDetailsContent.rig; onClicked: { deleteRigDialog.rigId = String(rigDetailsContent.rig.id || ""); deleteRigDialog.open(); } }
                }
            }
        }
    }

    FlightDeckDialog {
        id: deleteRigDialog
        objectName: "flightDeckDeleteRigDialog"
        tokens: deck
        heading: "Delete Device Rig?"
        tone: "attention"
        preferredWidth: 560
        property string rigId: ""
        readonly property var rig: root.rigFor(rigId)
        contentItem: ColumnLayout {
            width: deleteRigDialog.availableWidth
            spacing: deck.space12
            Text { text: deleteRigDialog.rig ? "Delete “" + String(deleteRigDialog.rig.name || "Device Rig") + "”? This cannot be undone." : "This Device Rig is no longer available."; color: deck.textPrimary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { text: root.profilesReferencingRig(deleteRigDialog.rigId).length ? root.profilesReferencingRig(deleteRigDialog.rigId).length + " Profile(s) reference this Rig. Their Device Rig assignment will be cleared through the canonical configuration path; no Profile will be remapped automatically." : "No Profiles currently reference this Rig. Saved physical-controller records remain available for other Rigs."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { visible: deleteRigDialog.rig && deleteRigDialog.rig.configured; text: "This Rig is currently configured. HOTAS BF6 will use its existing safe configuration transition when the canonical delete command clears it."; color: deck.attention; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                RigButton { text: "CANCEL"; subdued: true; onClicked: deleteRigDialog.close() }
                RigButton { objectName: "flightDeckDeleteRigConfirm"; text: "DELETE RIG"; destructive: true; enabled: !!deleteRigDialog.rig; onClicked: { const deleted = backend.deleteDeviceRig(deleteRigDialog.rigId); root.reportBooleanAction(deleted, "Device Rig deleted", "Affected Profile assignments were cleared; no Profile was remapped.", "Device Rig was not deleted", "Refresh the Device Rig list and try again."); deleteRigDialog.close(); rigDetailsDialog.close(); } }
            }
        }
    }

    FlightDeckDialog {
        id: setupHealthDialog
        objectName: "flightDeckSetupHealthDialog"
        tokens: deck
        heading: "Setup health & repair"
        preferredWidth: 760
        onOpened: setupHealthPanel.beginNewSession()
        contentItem: Flickable {
            width: setupHealthDialog.availableWidth
            implicitHeight: Math.min(setupHealthPanel.implicitHeight, setupHealthDialog.maximumBodyHeight)
            contentWidth: width
            contentHeight: setupHealthPanel.implicitHeight
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ControllerReadinessPanel {
                id: setupHealthPanel
                width: parent.width
                backendObject: backend
                themeTokens: deck
                showTitle: false
                useHostRepairConfirmation: true
                onCloseRequested: setupHealthDialog.close()
                onRepairRequested: repairConfirmation.open()
            }
        }
    }

    FlightDeckDialog {
        id: repairConfirmation
        objectName: "flightDeckRepairConfirmation"
        tokens: deck
        heading: "Repair setup?"
        tone: "attention"
        preferredWidth: 560
        contentItem: Flickable {
            objectName: "flightDeckRepairConfirmationBody"
            width: repairConfirmation.availableWidth
            implicitHeight: Math.min(repairContent.implicitHeight,
                repairConfirmation.maximumBodyHeight)
            contentWidth: width
            contentHeight: repairContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: repairContent
                width: parent.width
                spacing: deck.space12
                Text { text: "HOTAS BF6 will apply only the scoped changes listed below, then verify the resulting controller state. Windows may request administrator permission."; color: deck.textSecondary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: repairPlan.implicitHeight + deck.space24
                    color: deck.elevatedSurface
                    border.color: deck.border
                    radius: deck.radiusControl
                    ColumnLayout {
                        id: repairPlan
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space4
                        Text { text: "PLANNED CHANGES"; color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true }
                        Repeater {
                            model: root.setupRepairPlan
                            delegate: Text { text: "• " + (modelData.title || modelData.message || "Scoped repair"); color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Text { text: "• Preserve unrelated HidHide rules and the existing mapping choice."; color: deck.textSecondary; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }
                RowLayout {
                    objectName: "flightDeckRepairConfirmationActions"
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "CANCEL"
                        focusPolicy: Qt.StrongFocus
                        onClicked: repairConfirmation.close()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        text: "REPAIR SETUP"
                        focusPolicy: Qt.StrongFocus
                        onClicked: { repairConfirmation.close(); backend.repairSetupHealth() }
                        background: Rectangle { radius: deck.radiusControl; color: deck.accent; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: calibrationDialog
        objectName: "flightDeckCalibrationDialog"
        tokens: deck
        heading: "Controller calibration"
        tone: backend.calibrationActive ? "attention" : backend.calibrationSuccess ? "informational" : "informational"
        preferredWidth: 620
        contentItem: Flickable {
            width: calibrationDialog.availableWidth
            implicitHeight: Math.min(calibrationContent.implicitHeight,
                                      Math.max(260, (calibrationDialog.parent
                                          ? calibrationDialog.parent.height : 650) - deck.space48))
            contentWidth: width
            contentHeight: calibrationContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: calibrationContent
                width: parent.width
                spacing: deck.space12
                Text {
                    Layout.fillWidth: true
                    text: backend.calibrationStatus || "Calibration is ready when a selected controller is connected."
                    color: deck.textSecondary
                    font.pixelSize: deck.scale(11)
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: calibrationInstruction.implicitHeight + deck.space20
                    radius: deck.radiusControl
                    color: backend.calibrationActive ? Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.10) : deck.secondarySurface
                    border.color: backend.calibrationActive ? deck.attention : deck.border
                    Text {
                        id: calibrationInstruction
                        anchors.fill: parent
                        anchors.margins: deck.space10
                        text: backend.calibrationStage === "RANGE"
                            ? "STEP 1 OF 2 — Move every stick, twist, throttle, paddle, slider, and other axis through its full travel several times."
                            : backend.calibrationStage === "CENTER" || backend.calibrationStage === "FINALIZING"
                                ? "STEP 2 OF 2 — Release spring-centered controls and let them rest naturally. Throttles and sliders do not need to be centered."
                                : backend.calibrationSuccess
                                    ? "Calibration completed through the existing controller-scoped command path. You can begin another calibration when ready."
                                    : "Start calibration only when the selected controller is stable and available."
                        color: deck.textPrimary
                        font.pixelSize: deck.scale(11)
                        wrapMode: Text.WordWrap
                    }
                }
                Text { text: "AXIS RANGE STATUS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; Layout.fillWidth: true }
                Repeater {
                    model: root.axisItems
                    delegate: Rectangle {
                        required property var modelData
                        visible: Boolean(modelData.available)
                        Layout.fillWidth: true
                        implicitHeight: rangeRow.implicitHeight + deck.space16
                        radius: deck.radiusControl
                        color: deck.elevatedSurface
                        border.color: backend.calibrationActive ? deck.attention : deck.border
                        RowLayout {
                            id: rangeRow
                            anchors.fill: parent
                            anchors.margins: deck.space8
                            spacing: deck.space8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: String(modelData.label || "Axis").toUpperCase(); color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10); font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: backend.calibrationStage === "RANGE" ? "CAPTURING RANGE" : backend.calibrationStage === "CENTER" || backend.calibrationStage === "FINALIZING" ? "CAPTURING CENTER" : (modelData.calibrationEnabled ? "CALIBRATED" : "RAW DEFAULT"); color: backend.calibrationActive ? deck.attention : modelData.calibrationEnabled ? deck.healthy : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 58
                                Text { text: "MIN"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true }
                                Text { text: Number(modelData.calibrationMinimum || 0).toFixed(3); color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10) }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 58
                                Text { text: "NOW"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true }
                                Text { text: Number(modelData.raw || 0).toFixed(3); color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10) }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 58
                                Text { text: "MAX"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(8); font.bold: true }
                                Text { text: Number(modelData.calibrationMaximum || 0).toFixed(3); color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(10) }
                            }
                        }
                    }
                }
                Text { text: "CALIBRATION HISTORY"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; Layout.fillWidth: true }
                Text { visible: backend.calibrationHistory.length === 0; text: "Successful calibrations for the selected and saved controllers appear here."; color: deck.textMuted; font.pixelSize: deck.scale(10); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater {
                    model: backend.calibrationHistory
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: String(modelData.name || "Controller") + (modelData.currentDevice ? " · CURRENT DEVICE" : "") + "\n" + String(modelData.when || "") + " · " + String(modelData.axes || 0) + " axes calibrated"
                        color: modelData.currentDevice ? deck.healthy : deck.textSecondary
                        font.pixelSize: deck.scale(10)
                        wrapMode: Text.WordWrap
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        objectName: "flightDeckCalibrationClose"
                        text: "CLOSE"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: calibrationDialog.close()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.secondarySurface; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        objectName: "flightDeckCalibrationReset"
                        visible: backend.calibrationStage !== "IDLE" || backend.calibrationSuccess
                        text: "RESET"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.resetCalibration()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.secondarySurface; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "flightDeckCalibrationStart"
                        visible: backend.calibrationStage === "IDLE"
                        text: "START CALIBRATION"
                        enabled: backend.physicalConnected
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.beginCalibration()
                        background: Rectangle { radius: deck.radiusControl; color: parent.enabled ? deck.accent : deck.disabled; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        objectName: "flightDeckCalibrationCenter"
                        visible: backend.calibrationStage === "RANGE"
                        text: "CAPTURE CENTER"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.beginCalibrationCenterCapture()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.accentMuted; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        objectName: "flightDeckCalibrationSave"
                        visible: backend.calibrationStage === "CENTER"
                        text: "COMPLETE CALIBRATION"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.saveCalibration()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.accentMuted; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }
    }

    FlightDeckDialog {
        id: forgetControllerConfirmation
        objectName: "flightDeckForgetControllerConfirmation"
        tokens: deck
        heading: "Forget controller?"
        tone: "attention"
        preferredWidth: 540
        contentItem: ColumnLayout {
            width: forgetControllerConfirmation.availableWidth
            spacing: deck.space12
            Text {
                Layout.fillWidth: true
                text: "Forget \"" + String(root.forgetConsequences.name || "this controller") + "\"?"
                color: deck.textPrimary
                font.family: deck.displayFont
                font.pixelSize: deck.scale(16)
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "This removes the saved controller and its controller-specific configuration from HOTAS BF6."
                    + (root.forgetConsequences.willRestoreGameVisibility
                        ? " Its exact physical HID input will be shown to games again."
                        : "")
                    + " If it is still connected, it may immediately reappear as a new unverified controller."
                color: deck.textSecondary
                font.pixelSize: deck.scale(11)
                wrapMode: Text.WordWrap
            }
            Text {
                visible: (root.forgetConsequences.rigs || []).length > 0
                text: "IT WILL BE REMOVED FROM"
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: deck.scale(9)
                font.bold: true
            }
            Repeater {
                model: root.forgetConsequences.rigs || []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "• " + String(modelData.name || "Device Rig")
                        + (modelData.required ? " · REQUIRED — the Rig will need another controller" : " · Optional")
                        + (modelData.willRemoveRig ? " · empty Rig will be removed" : "")
                    color: modelData.required ? deck.attention : deck.textSecondary
                    font.pixelSize: deck.scale(10)
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                visible: Number(root.forgetConsequences.mappingCount || 0) > 0
                    || Number(root.forgetConsequences.automationReferenceCount || 0) > 0
                Layout.fillWidth: true
                text: (Number(root.forgetConsequences.mappingCount || 0) > 0
                        ? String(root.forgetConsequences.mappingCount) + " controller-specific Profile mapping set"
                            + (Number(root.forgetConsequences.mappingCount) === 1 ? " will" : "s will") + " be removed. " : "")
                    + (Number(root.forgetConsequences.automationReferenceCount || 0) > 0
                        ? String(root.forgetConsequences.automationReferenceCount) + " controller-specific Automation reference"
                            + (Number(root.forgetConsequences.automationReferenceCount) === 1 ? " will" : "s will") + " be removed." : "")
                color: deck.textSecondary
                font.pixelSize: deck.scale(10)
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "CANCEL"
                    focusPolicy: Qt.StrongFocus
                    onClicked: forgetControllerConfirmation.close()
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.secondarySurface; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "FORGET CONTROLLER"
                    focusPolicy: Qt.StrongFocus
                    onClicked: {
                        const name = String(root.forgetConsequences.name || "Controller")
                        const removed = backend.forgetController(String(root.forgetConsequences.recordId || ""))
                        forgetControllerConfirmation.close()
                        root.forgetConsequences = ({})
                        root.showTransientActionFeedback({ success: removed,
                            title: removed ? "Controller forgotten" : "Controller was not forgotten",
                            message: removed ? name + " and its scoped references were removed; its app-managed HID visibility was restored."
                                : "The controller changed before the forget transaction could be applied." }, "", "", 5000)
                    }
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.secondarySurface; border.color: parent.activeFocus ? deck.focus : deck.attention; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }

    FlightDeckDialog {
        id: undoConfirmation
        objectName: "flightDeckUndoRepairConfirmation"
        tokens: deck
        heading: "Undo setup repair?"
        tone: "attention"
        preferredWidth: 520
        contentItem: ColumnLayout {
            width: undoConfirmation.availableWidth
            spacing: deck.space12
            Text { text: "HOTAS BF6 will reverse only entries it added during this repair and then verify physical-controller access."; color: deck.textSecondary; font.pixelSize: deck.scale(11); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "CANCEL"
                    focusPolicy: Qt.StrongFocus
                    onClicked: undoConfirmation.close()
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                Button {
                    text: "UNDO REPAIR"
                    focusPolicy: Qt.StrongFocus
                    onClicked: { undoConfirmation.close(); backend.undoControllerReadiness() }
                    background: Rectangle { radius: deck.radiusControl; color: deck.secondarySurface; border.color: parent.activeFocus ? deck.focus : deck.attention; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: deck.scale(9); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }
}
