import QtQuick 6.5

// Central presentation-only interpretation of the low-frequency state already
// published by AppBackend. This object never changes mapper, controller, game,
// or profile semantics; it gives the Flight Deck rail and Overview one set of
// human-readable readiness rules.
Item {
    id: root
    objectName: "flightDeckReadinessModel"
    visible: false

    property var backendObject: backend
    property var runningApplications: []
    // Unset in the application. The startup test uses this only while it
    // renders deterministic visual evidence for a known-ready presentation;
    // it never reaches or substitutes mapper state.
    property var presentationStateOverride: null

    // `backendObject` is intentionally a generic QML value so this reusable
    // presentation model can be supplied a deterministic test backend. QML
    // cannot reliably retain each QObject dependency reached through that
    // value inside a JavaScript snapshot builder. Keep one complete snapshot
    // and rebuild it only at the AppBackend's committed control-plane
    // boundary. That boundary is shared by the active Profile, category, Rig,
    // output, mapper route, and setup truth; this is not a label-only update.
    property var backendState: ({})

    Connections {
        // FlightDeck.qml always supplies the application AppBackend as the
        // context object. Target it directly so Qt can subscribe to its
        // declared control-plane notifier instead of attempting dynamic
        // signal discovery through the generic presentation value above.
        target: backend
        function onStateChanged() {
            root.refreshBackendState()
        }
        function onProfilePresentationChanged() {
            root.refreshBackendState()
        }
    }

    function readinessCheck(checks, name) {
        for (let index = 0; index < checks.length; ++index) {
            if (checks[index].name === name)
                return checks[index];
        }
        return {
            name: name,
            state: "Checking",
            message: "Status has not been checked yet.",
            severity: "info"
        };
    }

    function activeCategory(categories, categoryId) {
        for (let index = 0; index < categories.length; ++index) {
            if (categories[index].id === categoryId)
                return categories[index];
        }
        return {
            name: "General",
            executableRules: []
        };
    }

    function applicationForRules(applications, rules) {
        for (let applicationIndex = 0; applicationIndex < applications.length; ++applicationIndex) {
            const executable = String(applications[applicationIndex].executable || "").toLowerCase();
            for (let ruleIndex = 0; ruleIndex < rules.length; ++ruleIndex) {
                const rule = String(rules[ruleIndex] || "").split(/[\\/]/).pop().toLowerCase();
                if (rule.length > 0 && executable === rule)
                    return applications[applicationIndex];
            }
        }
        return null;
    }

    function stateForBackend() {
        if (!backendObject)
            return {
                physicalConnected: false,
                connectedControllerCount: 0,
                checks: [],
                runningApplications: []
            };

        const categories = backendObject.profileCategories || [];
        const category = activeCategory(categories, backendObject.activeCategoryId);
        return {
            physicalConnected: backendObject.physicalConnected,
            connectedControllerCount: backendObject.connectedControllerCount,
            deviceName: backendObject.deviceName,
            mappingActive: backendObject.mappingActive,
            mappingRequested: backendObject.mappingRequested,
            mappingStatus: backendObject.mappingStatus,
            vjoyReady: backendObject.vjoyReady,
            vjoyStatus: backendObject.vjoyStatus,
            vjoyStatusSeverity: backendObject.vjoyStatusSeverity,
            vjoyDeviceId: backendObject.vjoyDeviceId,
            // Descriptor readiness, vJoy ownership, and mapped-report flow
            // are different facts. The rail must not collapse an acquired
            // output into "unavailable" merely because it is still neutral.
            outputRuntime: backendObject.outputRuntimeTelemetry || ({}),
            vjoyButtonCount: backendObject.vjoyButtonCount,
            vjoyContinuousPovCount: backendObject.vjoyContinuousPovCount,
            vjoyDiscretePovCount: backendObject.vjoyDiscretePovCount,
            outputLayoutName: backendObject.activeOutputLayoutName,
            controllerReadinessState: backendObject.controllerReadinessState,
            controllerReadinessStatus: backendObject.controllerReadinessStatus,
            controllerReadinessProposedChanges: backendObject.controllerReadinessProposedChanges,
            controllerReconnectRequired: backendObject.controllerReconnectRequired,
            controllerDisconnectObserved: backendObject.controllerDisconnectObserved,
            controllerSetupInProgress: backendObject.controllerSetupInProgress,
            controllerSetupCanApply: backendObject.controllerSetupCanApply,
            controllerSetupCanUndo: backendObject.controllerSetupCanUndo,
            hidhideAvailable: backendObject.hidhideAvailable,
            hidhideCloakStateKnown: backendObject.hidhideCloakStateKnown,
            hidhideCloaked: backendObject.hidhideCloaked,
            hidhideMapperAllowed: backendObject.hidhideMapperAllowed,
            checks: backendObject.controllerReadinessChecks,
            // The rail is an activation summary, not a transient mapper
            // override indicator.  Its Profile/category must therefore come
            // from the same committed active Profile used by Profiles,
            // Overview, Rig activation, and the persisted mapper route.
            activeProfileDisplayName: backendObject.activeProfileDisplayName,
            profileSourceLabel: backendObject.profileSourceLabel,
            automaticGameDetection: backendObject.automaticGameDetection,
            activeCategoryName: backendObject.activeCategoryName,
            activeCategoryRules: category.executableRules || [],
            // After a setup session completes, this is the same authoritative
            // aggregate snapshot shown by the Devices dialog. Do not allow
            // the older runtime-only readiness projection to contradict it.
            setupTruth: backendObject.setupTruthSnapshot || ({}),
            runningApplications: runningApplications
        };
    }

    function refreshBackendState() {
        backendState = stateForBackend();
    }

    // Kept as a pure function so the startup test can prove the important
    // no-controller, output, attention, partial, and ready combinations.
    function presentationFor(state) {
        const setupTruth = state.setupTruth || {};
        const truthStatus = String(setupTruth.overallStatus || "");
        // Startup publishes the same Setup Truth object before its first
        // read-only result. Do not let legacy runtime telemetry call that
        // uninspected interval READY (or UNKNOWN) in the rail while Devices
        // and Overview correctly say CHECKING.
        if (truthStatus === "CHECKING") {
            return { label: "CHECKING SETUP", tone: "informational",
                detail: "HOTAS BF6 is reading the current controller, virtual output, and isolation state." };
        }
        if (setupTruth.fresh && truthStatus.length > 0 && truthStatus !== "CHECKING") {
            if (truthStatus === "READY")
                return { label: "READY", tone: "healthy", detail: "The current setup truth is ready for use." };
            if (truthStatus === "ACTION NEEDED" || truthStatus === "ATTENTION")
                return { label: "ACTION NEEDED", tone: "attention", detail: "The current setup truth needs attention." };
            if (truthStatus === "UNKNOWN / INSPECTION FAILED")
                return { label: truthStatus, tone: "informational", detail: "A required setup fact could not be inspected." };
            return { label: truthStatus, tone: "fault", detail: "The current setup truth requires action." };
        }
        const readiness = String(state.controllerReadinessState || "").toUpperCase();
        const outputRuntime = state.outputRuntime || {};
        const outputAvailable = outputRuntime.outputAvailable === true;
        const outputUnavailable = !outputAvailable && (!state.vjoyReady
            || String(state.vjoyStatusSeverity || "").toLowerCase() === "error");
        const checking = readiness.indexOf("CHECK") >= 0 || readiness.indexOf("UNKNOWN") >= 0;
        const attention = readiness.indexOf("ATTENTION") >= 0 || readiness.indexOf("ACTION") >= 0;

        if (!state.physicalConnected && Number(state.connectedControllerCount || 0) > 0) {
            return {
                label: "ACTION NEEDED",
                tone: "attention",
                detail: "A controller is detected but still needs to be selected or verified."
            };
        }
        if (!state.physicalConnected) {
            return {
                label: "NO CONTROLLER",
                tone: "fault",
                detail: "Connect or select a controller before mapping can begin."
            };
        }
        if (outputUnavailable) {
            return {
                label: "ACTION NEEDED",
                tone: "fault",
                detail: "Virtual output is unavailable, so HOTAS BF6 cannot send controller output."
            };
        }
        if (checking) {
            return {
                label: "CHECKING SETUP",
                tone: "informational",
                detail: "Controller setup is still being checked."
            };
        }
        if (attention) {
            return {
                label: "ACTION NEEDED",
                tone: "attention",
                detail: "Setup needs attention before the system is fully ready."
            };
        }
        if (state.mappingActive && readiness === "READY") {
            return {
                label: "READY",
                tone: "healthy",
                detail: "Input, output, and setup checks are ready for use."
            };
        }
        if (state.mappingRequested) {
            return {
                label: "PARTIALLY READY",
                tone: "attention",
                detail: "Setup is healthy, but mapping output has not become active yet."
            };
        }
        return {
            label: "STANDING BY",
            tone: "informational",
            detail: "Setup is available. Start mapping when you are ready."
        };
    }

    function inputFor(state) {
        const count = Math.max(0, Number(state.connectedControllerCount || 0));
        if (!state.physicalConnected && count > 0)
            return {
                title: "Controller needs setup",
                detail: count + " input device" + (count === 1 ? " is" : "s are") + " detected but not active.",
                tone: "attention"
            };
        if (!state.physicalConnected)
            return {
                title: "No controller connected",
                detail: "Connect a controller to begin setup.",
                tone: "fault"
            };
        if (count > 1)
            return {
                title: count + " input devices",
                detail: "Multiple physical controllers are connected.",
                tone: "healthy"
            };
        return {
            title: state.deviceName || "Controller connected",
            detail: "1 input device connected.",
            tone: "healthy"
        };
    }

    function outputFor(state) {
        const outputRuntime = state.outputRuntime || {};
        const descriptorState = String(outputRuntime.descriptorState || "");
        const ownershipState = String(outputRuntime.ownershipState || "");
        const reportState = String(outputRuntime.runtimeReportState || "");
        const deviceId = outputRuntime.configuredVjoyDeviceId || state.vjoyDeviceId || "";
        if (outputRuntime.outputAvailable === true) {
            return {
                title: "vJoy " + deviceId + " acquired",
                detail: reportState === "REPORTING"
                    ? "Publishing mapped reports."
                    : "Waiting for first mapped report.",
                tone: "healthy"
            };
        }
        if (!state.vjoyReady)
            return {
                title: "Virtual output unavailable",
                detail: descriptorState === "CONFIGURED" && ownershipState.length
                    ? "vJoy is configured but " + ownershipState.toLowerCase() + "."
                    : state.vjoyStatus || "vJoy is not ready.",
                tone: "fault"
            };
        if (String(state.vjoyStatusSeverity || "").toLowerCase() === "warning")
            return {
                title: "Output capacity needs attention",
                detail: state.vjoyStatus || "vJoy output is limited.",
                tone: "attention"
            };
        return {
            title: "vJoy " + (state.vjoyDeviceId || "") + " online",
            detail: state.outputLayoutName || state.vjoyStatus || "Virtual output is available.",
            tone: "healthy"
        };
    }

    // Axes and Buttons deliberately share this projection.  The editor pages
    // provide their exact selected saved member and physical capability count;
    // this model supplies the common controller/output truth and never turns
    // an uninspected capability or output into a ready state by inference.
    function setupGroup(snapshot, id) {
        const groups = snapshot && snapshot.groups ? snapshot.groups : []
        for (let index = 0; index < groups.length; ++index) {
            const group = groups[index] || ({})
            if (String(group.id || "") === String(id || "")) {
                return Object.assign({}, group, { present: true })
            }
        }
        return ({ present: false, status: "NOT CHECKED", detail: "Setup status has not been checked yet.",
            checking: false, checked: false, fresh: false })
    }

    function editorInputStatus(context) {
        const details = context || ({})
        const kind = String(details.kind || "input")
        const plural = kind === "axes" ? "axes" : "buttons or hat switches"
        const singular = kind === "axes" ? "axis" : "button"
        const selectedId = String(details.selectedDeviceId || "")
        const selectedName = String(details.selectedDeviceName || "Selected controller")
        const selectedConnected = Boolean(details.selectedDeviceConnected)
        const eligibleMembers = Math.max(0, Number(details.eligibleMemberCount || 0))
        const capabilityKnown = Boolean(details.capabilityKnown)
        const capabilityCount = Math.max(0, Number(details.capabilityCount || 0))
        const alternateCapabilityCount = Math.max(0, Number(details.alternateCapabilityCount || 0))
        const assignedCount = Math.max(0, Number(details.assignedCount || 0))
        const preparedQuickMap = Boolean(details.quickMapAvailable)
        const learningTarget = String(details.learningTarget || "")
        // `setupTruth` is a test seam only; production always consumes the
        // frozen AppBackend projection for the current setup scope.
        const truth = details.setupTruth || (backendObject ? (backendObject.setupTruthSnapshot || ({})) : ({}))
        const output = setupGroup(truth, "vjoy")
        const outputState = String(output.status || "NOT CHECKED").toUpperCase()
        const requestedRigId = String(details.rigId || "")
        const snapshotRigId = String((output.evidence || {}).rigId || truth.setupTargetRigId || truth.rigId || "")
        const scopeMatches = !requestedRigId.length || !snapshotRigId.length || requestedRigId === snapshotRigId
        const hasOutputGroup = Boolean(output.present) && scopeMatches
        const outputChecking = hasOutputGroup && (Boolean(output.checking) || outputState === "CHECKING")
        const outputChecked = hasOutputGroup && (Boolean(output.checked)
            || (outputState.length > 0 && outputState !== "NOT CHECKED" && outputState !== "CHECKING"))
        const outputFresh = outputChecked && (Object.prototype.hasOwnProperty.call(output, "fresh")
            ? Boolean(output.fresh) : Boolean(truth.fresh))
        const outputReady = outputFresh && (outputState === "READY" || outputState === "READY TO ACTIVATE")
        const outputFailed = outputState === "FAILED" || outputState === "UNAVAILABLE"
            || outputState === "UNKNOWN / INSPECTION FAILED"
        const controllerPill = selectedId.length > 0
            ? { label: "CONTROLLER", value: selectedConnected ? "CONNECTED" : "OFFLINE",
                tone: selectedConnected ? "healthy" : "attention" }
            : { label: "CONTROLLER", value: eligibleMembers > 0 ? "CHOOSE" : "NOT ASSIGNED",
                tone: eligibleMembers > 0 ? "informational" : "attention" }
        const outputPillValue = outputChecking ? "CHECKING"
            : !hasOutputGroup || !outputChecked ? "NOT CHECKED"
            : !outputFresh ? "STALE " + (outputState || "RESULT")
            : outputReady ? "READY" : outputFailed ? "FAILED" : outputState
        const outputPill = { label: "OUTPUT", value: outputPillValue,
            tone: outputReady ? "healthy" : outputChecking ? "informational" : "attention" }
        const result = function(state, heading, detail, primaryAction, primaryText,
                                 secondaryAction, secondaryText) {
            return { state: state, heading: heading, detail: detail, pills: [controllerPill, outputPill],
                primaryAction: primaryAction || "", primaryText: primaryText || "",
                secondaryAction: secondaryAction || "", secondaryText: secondaryText || "",
                capabilityKnown: capabilityKnown, learningTarget: learningTarget }
        }
        if (selectedId.length === 0) {
            if (eligibleMembers > 0)
                return result("choose", "Choose a controller", "Choose the saved controller whose " + plural
                    + " you want to configure.", "picker", "CHOOSE CONTROLLER")
            const discovered = backendObject ? (backendObject.controllers || []).length : 0
            if (discovered === 0)
                return result("connect", "Connect your controller", "Plug in a controller, then scan for it here.",
                    "scan", "SCAN FOR CONTROLLERS")
            return result("unassigned", "Add this controller to a setup",
                "A controller was discovered, but it is not a saved member of the selected setup yet.",
                "devices", "OPEN DEVICES & SETUP")
        }
        if (!selectedConnected)
            return result("offline", selectedName + " is not connected",
                "Saved mappings stay editable. Live learning and testing wait for this controller to reconnect.",
                "picker", "CHANGE CONTROLLER", preparedQuickMap ? "quick-map" : "check",
                preparedQuickMap ? "QUICK MAP" : "CHECK SETUP")
        if (!capabilityKnown)
            return result("capabilities", "Checking controller capabilities",
                "HOTAS BF6 has not received a capability record for this exact saved controller. Run a scoped setup check.",
                "check", "CHECK SETUP")
        if (details.verified === false)
            return result("setup", "Set up " + selectedName,
                "Finish identity setup for this exact controller before mapping it.",
                "setup", "SET UP THIS CONTROLLER")
        if (capabilityCount === 0)
            return result("no-controls", "This controller has no " + plural,
                "This is not a fault. Open the other input editor or review the saved controller details.",
                alternateCapabilityCount > 0 ? "other-editor" : "devices",
                alternateCapabilityCount > 0 ? (kind === "axes" ? "OPEN BUTTONS" : "OPEN AXES") : "OPEN DEVICES & SETUP")
        if (!outputReady) {
            const outputHeading = outputChecking ? "Checking virtual output"
                : !hasOutputGroup || !outputChecked ? "Virtual output has not been checked"
                : !outputFresh ? "Virtual output needs a fresh check"
                : "Virtual output needs attention"
            const outputDetail = !hasOutputGroup
                ? "This editor's current Device Rig has no matching setup result. Run a scoped check before testing mapped output."
                : !outputChecked ? "No prior setup result exists for this output. Run a scoped check before testing mapped output."
                : !outputFresh
                    ? "Last checked result: " + (outputState || "unknown") + ". "
                        + String(output.detail || "Run a scoped check before testing mapped output.")
                    : String(output.detail || "Check the selected setup before testing mapped output.")
            return result("output", outputHeading, outputDetail,
                "check", "CHECK SETUP", preparedQuickMap ? "quick-map" : "",
                preparedQuickMap ? "QUICK MAP" : "")
        }
        if (assignedCount === 0)
            return result("first-assignment", "Assign your first " + singular,
                "Choose a " + singular + ", then select the output it should control.",
                preparedQuickMap ? "quick-map" : "", preparedQuickMap ? "QUICK MAP" : "")
        return result("ready", kind === "axes" ? "Axes ready" : "Buttons ready",
            "Choose an input to view its source, output, and simple mapping controls.",
            kind === "buttons" ? "learn-button" : (learningTarget.length > 0 ? "learn-axis" : (preparedQuickMap ? "quick-map" : "")),
            kind === "buttons" ? "LEARN BUTTON" : (learningTarget.length > 0 ? "LEARN INPUT" : (preparedQuickMap ? "QUICK MAP" : "")),
            preparedQuickMap && (kind === "buttons" || learningTarget.length > 0) ? "quick-map" : "",
            preparedQuickMap && (kind === "buttons" || learningTarget.length > 0) ? "QUICK MAP" : "")
    }

    function isolationFor(state) {
        const check = readinessCheck(state.checks || [], "HIDHIDE ISOLATION");
        const severity = String(check.severity || "info").toLowerCase();
        return {
            title: check.state || "Checking",
            detail: check.message || "HidHide status has not been checked yet.",
            tone: severity === "ready" ? "healthy" : severity === "error" ? "fault" : severity === "warning" ? "attention" : "informational"
        };
    }

    function gameFor(state) {
        if (!state.automaticGameDetection)
            return {
                title: "Game detection paused",
                detail: "The active profile can still be selected manually.",
                tone: "informational"
            };
        const rules = state.activeCategoryRules || [];
        if (rules.length === 0)
            return {
                title: "No game associated",
                detail: "The active category has no game rule yet.",
                tone: "informational"
            };
        const application = applicationForRules(state.runningApplications || [], rules);
        if (application) {
            return {
                title: application.name || application.executable,
                detail: "Detected for " + (state.activeCategoryName || "the active category") + ".",
                tone: "healthy"
            };
        }
        return {
            title: "No supported game detected",
            detail: "The active profile remains available while no matching game is running.",
            tone: "informational"
        };
    }

    function profileFor(state) {
        const profile = String(state.activeProfileDisplayName || "").trim();
        if (profile.length === 0)
            return {
                title: "No active profile",
                detail: "Choose a profile to define the current routing.",
                tone: "attention"
            };
        return {
            title: profile,
            detail: state.profileSourceLabel || "Active profile",
            tone: "informational"
        };
    }

    readonly property var currentState: presentationStateOverride || backendState
    readonly property var readiness: presentationFor(currentState)
    readonly property var input: inputFor(currentState)
    readonly property var output: outputFor(currentState)
    readonly property var isolation: isolationFor(currentState)
    readonly property var game: gameFor(currentState)
    readonly property var profile: profileFor(currentState)

    Component.onCompleted: {
        refreshBackendState();
        if (!backendObject)
            return;
        runningApplications = backendObject.runningApplications();
        backendObject.refreshRunningApplications();
    }

    Connections {
        target: root.backendObject
        function onRunningApplicationsChanged() {
            root.runningApplications = root.backendObject.runningApplications();
            root.refreshBackendState();
        }
    }
}
