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

        const categories = backendObject.profileCategories;
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
            outputLayoutName: backendObject.activeOutputLayoutName,
            controllerReadinessState: backendObject.controllerReadinessState,
            checks: backendObject.controllerReadinessChecks,
            effectiveProfileDisplayName: backendObject.effectiveProfileDisplayName,
            profileSourceLabel: backendObject.profileSourceLabel,
            automaticGameDetection: backendObject.automaticGameDetection,
            activeCategoryName: backendObject.activeCategoryName,
            activeCategoryRules: category.executableRules || [],
            runningApplications: runningApplications
        };
    }

    // Kept as a pure function so the startup test can prove the important
    // no-controller, output, attention, partial, and ready combinations.
    function presentationFor(state) {
        const readiness = String(state.controllerReadinessState || "").toUpperCase();
        const outputUnavailable = !state.vjoyReady || String(state.vjoyStatusSeverity || "").toLowerCase() === "error";
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
        if (!state.vjoyReady)
            return {
                title: "Virtual output unavailable",
                detail: state.vjoyStatus || "vJoy is not ready.",
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
        const profile = String(state.effectiveProfileDisplayName || "").trim();
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

    readonly property var currentState: presentationStateOverride || stateForBackend()
    readonly property var readiness: presentationFor(currentState)
    readonly property var input: inputFor(currentState)
    readonly property var output: outputFor(currentState)
    readonly property var isolation: isolationFor(currentState)
    readonly property var game: gameFor(currentState)
    readonly property var profile: profileFor(currentState)

    Component.onCompleted: {
        if (!backendObject)
            return;
        runningApplications = backendObject.runningApplications();
        backendObject.refreshRunningApplications();
    }

    Connections {
        target: root.backendObject
        function onRunningApplicationsChanged() {
            root.runningApplications = root.backendObject.runningApplications();
        }
    }
}
