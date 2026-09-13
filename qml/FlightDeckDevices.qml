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
    property var actionFeedback: ({})
    signal navigateToPage(int page)

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
        if (severity === "error" || label.indexOf("ERROR") >= 0 || label.indexOf("REQUIRED") >= 0 || label.indexOf("ACTION NEEDED") >= 0)
            return "fault";
        if (severity === "warning" || severity === "waiting" || label.indexOf("ATTENTION") >= 0 || label.indexOf("WAITING") >= 0)
            return "attention";
        return "informational";
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
        if (!controller.connected)
            return "Disconnected";
        if (controller.ambiguous)
            return "Selection required";
        if (!controller.verified)
            return "Setup required";
        if (controller.active)
            return "Connected · active";
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
        if (controller.active) return "ACTIVE";
        return controller.verified ? "USE CONTROLLER" : "VERIFY CONTROLLER";
    }

    function rigFor(id) {
        for (let index = 0; index < rigItems.length; ++index) {
            if (String(rigItems[index].id || "") === String(id || ""))
                return rigItems[index];
        }
        return null;
    }

    function rigTone(rig) {
        const health = String((rig || {}).health || "offline");
        if (health === "ready") return "healthy";
        if (health === "partial" || health === "needs-attention") return "attention";
        if (health === "conflict") return "fault";
        return "informational";
    }

    function rigState(rig) {
        if (!rig) return "Offline";
        if (rig.configured) return "ACTIVE";
        return "VIEWING";
    }

    function memberState(member) {
        if (!member || !member.enabled) return "Excluded";
        if (member.ambiguous) return "Identity needs selection";
        if (member.needsVerification || !member.verified) return "Setup needed";
        return member.connected ? "Connected" : "Offline";
    }

    function memberTone(member) {
        if (!member || !member.enabled) return "informational";
        if (member.ambiguous || member.needsVerification || !member.verified) return "attention";
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

    function selectedRig() { return rigFor(selectedRigId); }

    function normalizeRigSelection() {
        if (rigFor(selectedRigId)) return;
        const editing = String(backend.editingDeviceRigId || "");
        selectedRigId = rigFor(editing) ? editing : (rigItems.length ? String(rigItems[0].id || "") : "");
    }

    function showActionFeedback(result, fallbackTitle, fallbackMessage) {
        actionFeedbackDismissTimer.stop();
        actionFeedback = result && result.title
            ? result : ({ success: false, title: fallbackTitle, message: fallbackMessage });
        if (!actionFeedback.inProgress && !actionFeedback.persistent)
            actionFeedbackDismissTimer.restart();
        return actionFeedback;
    }

    function showTransientActionFeedback(result, fallbackTitle, fallbackMessage, durationMs) {
        const feedback = showActionFeedback(result, fallbackTitle, fallbackMessage);
        actionFeedbackDismissTimer.interval = durationMs > 0 ? durationMs : 5000;
        actionFeedbackDismissTimer.restart();
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

    Timer {
        id: actionFeedbackDismissTimer
        interval: 5000
        repeat: false
        onTriggered: root.actionFeedback = ({})
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
            font.pixelSize: 9
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
        font.pixelSize: 10
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
            font.pixelSize: 12
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
                font.pixelSize: 10
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
        if (target)
            contentY = Math.max(0, Math.min(contentHeight - height, target.y - deck.space8));
    }

    onRequestedContextChanged: Qt.callLater(revealContext)
    Component.onCompleted: Qt.callLater(revealContext)
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
                    font.pixelSize: 12
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
                            font.pixelSize: 18
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
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: root.setupTruth.overallStatus || readiness.label || "CHECKING"
                            color: deck.statusColor(root.toneFor({ state: root.setupTruth.overallStatus || "CHECKING", severity: "" }))
                            font.family: deck.displayFont
                            font.pixelSize: root.medium ? 22 : 18
                            font.bold: true
                        }
                        Text {
                            text: checking ? "Checking the complete Device Rig…" : (root.setupTruth.rigName
                                ? "Current typed setup truth for " + root.setupTruth.rigName + "."
                                : (readiness.detail || "Checking current controller setup."))
                            color: deck.textSecondary
                            font.pixelSize: 11
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
                            font.pixelSize: 9
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
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: modelData.label.toUpperCase(); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: String(modelData.check.state || "Checking"); color: deck.textPrimary; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
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
                    contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        Rectangle {
            id: deviceActionFeedback
            objectName: "flightDeckDeviceActionFeedback"
            visible: Object.keys(root.actionFeedback).length > 0
            Layout.fillWidth: true
            implicitHeight: visible ? actionFeedbackContent.implicitHeight + deck.space20 : 0
            radius: deck.radiusControl
            color: root.actionFeedback.success
                ? Qt.rgba(deck.healthy.r, deck.healthy.g, deck.healthy.b, 0.12)
                : Qt.rgba(deck.attention.r, deck.attention.g, deck.attention.b, 0.12)
            border.color: root.actionFeedback.success ? deck.healthy : deck.attention
            ColumnLayout {
                id: actionFeedbackContent
                anchors.fill: parent
                anchors.margins: deck.space10
                spacing: deck.space4
                Text { text: String(root.actionFeedback.title || ""); color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: String(root.actionFeedback.message || ""); color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
        }

        Item { id: controllersSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "PHYSICAL CONTROLLERS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
        Text { text: "Each connected controller remains visible here. Selecting or verifying one uses the existing controller workflow."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }

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
                Text { text: "No controllers connected"; color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: 17; font.bold: true }
                Text { text: "Plug in a joystick, HOTAS, throttle, pedals, or another supported controller, then scan for devices."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button {
                    objectName: "flightDeckScanDevices"
                    text: "SCAN FOR DEVICES"
                    implicitHeight: deck.compactControlHeight
                    focusPolicy: Qt.StrongFocus
                    onClicked: backend.refreshControllers()
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
                    border.color: controller.active ? deck.accent : deck.border
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
                                Text { text: "PHYSICAL CONTROLLER"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                                Text {
                                    objectName: "flightDeckControllerName_" + controllerCard.index
                                    text: controllerCard.controller.name || "Controller"
                                    color: deck.textPrimary
                                    font.family: deck.displayFont
                                    font.pixelSize: 16
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
                            Text { text: root.markerFor(root.controllerTone(controllerCard.controller)) + " " + root.controllerState(controllerCard.controller); color: deck.statusColor(root.controllerTone(controllerCard.controller)); font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                            Text { text: controllerCard.controller.verified ? "✓ Verified" : "! Not yet verified"; color: controllerCard.controller.verified ? deck.healthy : deck.attention; font.pixelSize: 10; font.bold: true }
                        }
                        Text { text: controllerCard.controller.axisCount + " axes  •  " + controllerCard.controller.buttonCount + " buttons  •  " + controllerCard.controller.povCount + " hats"; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { visible: controllerCard.controller.active; text: "Used by the current active setup."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true }
                        Text { visible: !controllerCard.controller.connected; text: "This saved controller is no longer available. Reconnect it, then scan again."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            Button {
                                objectName: "flightDeckControllerAction_" + controllerCard.controller.directInputId
                                text: controllerCard.controllerActionLabel
                                enabled: !controllerCard.controller.active
                                focusPolicy: Qt.StrongFocus
                                implicitHeight: deck.compactControlHeight
                                onClicked: {
                                    if (!controllerCard.controller.connected) backend.refreshControllers()
                                    else if (controllerCard.controller.verified && controllerCard.controller.id) backend.setActiveController(controllerCard.controller.id)
                                    else backend.selectNewController(controllerCard.controller.directInputId)
                                }
                                background: Rectangle { radius: deck.radiusControl; color: parent.enabled && parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : (parent.enabled ? deck.accent : deck.border); border.width: parent.activeFocus ? 2 : 1 }
                                contentItem: Text { text: parent.text; color: parent.enabled ? deck.accent : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                            Button {
                                visible: controllerCard.controller.verified && controllerCard.controller.id
                                objectName: "flightDeckControllerForget_" + controllerCard.controller.id
                                text: "FORGET"
                                focusPolicy: Qt.StrongFocus
                                implicitHeight: deck.compactControlHeight
                                onClicked: backend.forgetController(controllerCard.controller.id)
                                background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                                contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                        }
                    }
                }
            }
        }

        Item { id: rigsSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "DEVICE RIGS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
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
            font.pixelSize: 10
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
                Text { text: "Create your first Device Rig"; color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: 17; font.bold: true }
                Text { text: root.controllerItems.length ? "Choose one or more physical controllers, set Required or Optional membership, then attach the Virtual Output they will use." : "Connect or scan for a physical controller before creating a Device Rig."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
                                Text { text: "DEVICE RIG"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                                Text { text: String(rig.name || "Device Rig"); color: deck.textPrimary; font.family: deck.displayFont; font.pixelSize: 17; font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                            }
                            FlightDeckStatusChip { tokens: deck; label: String(rig.healthLabel || "Offline").toUpperCase(); value: root.rigState(rig); tone: root.rigTone(rig) }
                        }
                        Text {
                            text: (rig.members || []).length + " physical controller" + ((rig.members || []).length === 1 ? "" : "s")
                                + "  ·  " + (rig.outputs || []).length + " Virtual Output" + ((rig.outputs || []).length === 1 ? "" : "s")
                            color: deck.textSecondary
                            font.family: deck.telemetryFont
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: rig.members || []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: deck.space8
                                Text { text: root.markerFor(root.memberTone(modelData)); color: deck.statusColor(root.memberTone(modelData)); font.pixelSize: 12; font.bold: true }
                                Text { text: String(modelData.name || "Controller"); color: deck.textPrimary; font.pixelSize: 10; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                                Text { text: modelData.required ? "REQUIRED" : "OPTIONAL"; color: modelData.required ? deck.textSecondary : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                Text { text: root.memberState(modelData).toUpperCase(); color: deck.statusColor(root.memberTone(modelData)); font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
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
                            font.pixelSize: 9
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
                    }
                }
            }
        }

        Item { id: virtualOutputSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "VIRTUAL OUTPUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
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
                        Text { text: "CURRENT ACTIVE OUTPUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                        Text { text: root.toneFor(root.vjoyCheck) === "healthy" ? "Online" : "Action needed"; color: deck.statusColor(root.toneFor(root.vjoyCheck)); font.family: deck.displayFont; font.pixelSize: 18; font.bold: true }
                        Text { text: backend.activeOutputLayoutName + " · vJoy " + root.vjoyDeviceId + "\n" + (root.vjoyCheck.message || output.detail || "Checking virtual output."); color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text {
                            visible: Number(root.outputRuntime.activeVjoyDeviceId || 0) > 0
                            text: root.outputPublicationSummary()
                            color: root.outputRuntime.outputReportsSucceeding ? deck.healthy : deck.warning
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    FlightDeckStatusChip { tokens: deck; label: root.toneFor(root.vjoyCheck) === "healthy" ? "ONLINE" : "ACTION NEEDED"; value: backend.activeOutputLayoutName + " · vJoy " + root.vjoyDeviceId; tone: root.toneFor(root.vjoyCheck); visible: root.medium }
                }
                Text { text: "Virtual output is the controller signal games receive from HOTAS BF6."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        // Manual driver configuration is an advanced fallback,
                        // never a competing normal repair route.
                        visible: root.toneFor(root.vjoyCheck) !== "healthy" && root.virtualDetailsOpen
                        text: "OPEN VJOY SETUP"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.openVjoyConfiguration()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: root.virtualDetailsOpen ? "HIDE TECHNICAL DETAILS" : "TECHNICAL DETAILS"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.virtualDetailsOpen = !root.virtualDetailsOpen
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
                        Text { text: "vJoy Device " + root.vjoyDeviceId; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true }
                        Text { text: root.vjoyStatus; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: root.vjoyButtonCount + " buttons  •  " + root.vjoyContinuousPovCount + " continuous hats  •  " + root.vjoyDiscretePovCount + " discrete hats"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }
            }
        }

        Item { id: isolationSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "DEVICE ISOLATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
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
                        Text { text: "DEVICE ISOLATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                        Text { text: root.toneFor(root.isolationCheck) === "healthy" ? "Protected" : "Action needed"; color: deck.statusColor(root.toneFor(root.isolationCheck)); font.family: deck.displayFont; font.pixelSize: 18; font.bold: true }
                        Text { text: root.isolationCheck.message || isolation.detail || "Checking device isolation."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    FlightDeckStatusChip { tokens: deck; label: "HIDHIDE"; value: String(root.isolationCheck.state || "CHECKING").toUpperCase(); tone: root.toneFor(root.isolationCheck); visible: root.medium }
                }
                Text { text: "Device isolation prevents games from seeing both the physical controller and virtual output."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        visible: root.toneFor(root.isolationCheck) !== "healthy"
                        text: "VIEW SETUP HEALTH"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: setupHealthDialog.open()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.accentMuted : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.accent; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: root.isolationDetailsOpen ? "HIDE TECHNICAL DETAILS" : "TECHNICAL DETAILS"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: root.isolationDetailsOpen = !root.isolationDetailsOpen
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
                        Text { text: "HidHide status"; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true }
                        Text { text: root.hidhideAvailable ? "Service and tools are available." : "Service or tools are unavailable."; color: deck.textSecondary; font.pixelSize: 10 }
                        Text { text: root.hidhideCloakStateKnown ? (root.hidhideCloaked ? "Cloaking is enabled." : "Cloaking is disabled.") : "Cloaking state is still unknown."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: root.hidhideMapperAllowed ? "HOTAS BF6 is allow-listed." : "HOTAS BF6 is not allow-listed."; color: deck.textSecondary; font.pixelSize: 10 }
                    }
                }
            }
        }

        Item { id: verificationSection; Layout.fillWidth: true; Layout.preferredHeight: 1 }
        Text { text: "SETUP / VERIFICATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
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
                        Text { text: "CONTROLLER CALIBRATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                        Text { text: backend.calibrationActive ? "Calibration in progress" : (backend.calibrationSuccess ? "Calibration complete" : "Capture controller ranges and centered controls"); color: backend.calibrationActive ? deck.attention : backend.calibrationSuccess ? deck.healthy : deck.textPrimary; font.family: deck.displayFont; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: backend.calibrationStatus || "Calibration is scoped to the selected controller. Profiles, mappings, and Automation are not changed."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
                Text { text: "ADVANCED / TECHNICAL"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                Text { text: "For device identifiers, raw controller state, and detailed troubleshooting, use Diagnostics."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Button {
                    text: "OPEN DIAGNOSTICS"
                    focusPolicy: Qt.StrongFocus
                    implicitHeight: deck.compactControlHeight
                    onClicked: root.navigateToPage(3)
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
            const deviceId = backend.suggestedVirtualOutputDeviceId();
            const created = backend.createFiveAxisOutputLayout("Flight Deck Output " + deviceId, deviceId);
            if (created) {
                outputLayoutId = created;
                root.showTransientActionFeedback({ success: true, title: "Virtual Output created", message: "The new output is selected for this Device Rig." }, "", "", 5000);
            } else {
                root.showActionFeedback({ success: false, title: "Virtual Output was not created", message: "Review the vJoy Device ID and existing output names, then try again." }, "", "");
            }
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
            root.openRigDetails(root.selectedRigId);
            if (result.nextAction === "setup") {
                root.showTransientActionFeedback({ success: true, title: "Rig saved", message: "Use Check & Repair Setup to inspect this Device Rig without changing its configuration." }, "", "", 5000);
            }
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
                Text { text: "A Device Rig is the canonical physical-controller and Virtual Output group used by Profiles and Automatic Activation."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: "RIG NAME"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
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
                Text { text: "PHYSICAL CONTROLLERS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.topMargin: deck.space4 }
                Text { text: "Include every controller used by this setup. Required controllers gate automatic activation; missing Optional controllers reduce capability without blocking it."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater {
                    model: root.controllerItems
                    delegate: Rectangle {
                        required property var modelData
                        readonly property string controllerId: root.controllerCandidateId(modelData)
                        visible: controllerId.length > 0 && !modelData.ambiguous
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
                                Text { text: String(modelData.name || "Controller"); color: deck.textPrimary; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: modelData.connected ? (modelData.verified ? "Connected · verified" : "Connected · setup needed") : "Saved / Offline"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            RigButton { text: createRigDialog.included(controllerId) ? "INCLUDED" : "INCLUDE"; subdued: !createRigDialog.included(controllerId); onClicked: createRigDialog.setIncluded(modelData, !createRigDialog.included(controllerId)) }
                            RigButton { visible: createRigDialog.included(controllerId); text: createRigDialog.required(controllerId) ? "REQUIRED" : "OPTIONAL"; subdued: createRigDialog.required(controllerId) === false; onClicked: createRigDialog.setRequired(controllerId, !createRigDialog.required(controllerId)) }
                        }
                    }
                }
                Text { visible: root.controllerItems.length === 0; text: "No physical controllers are available. Scan for devices before creating a Rig."; color: deck.attention; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: "VIRTUAL OUTPUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.topMargin: deck.space4 }
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
                Text { text: root.outputLayouts.length ? "Choose the existing Virtual Output this Rig should own. You can add more outputs in Rig Details." : "Create a Virtual Output before this Rig can be saved."; color: deck.textMuted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { visible: rigCreateName.text.trim().length === 0 || createRigDialog.draftMembers.length === 0 || !createRigDialog.outputLayoutId; text: "Enter a name, include at least one controller, and select a Virtual Output to continue."; color: deck.attention; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
        id: rigDetailsDialog
        objectName: "flightDeckRigDetailsDialog"
        tokens: deck
        heading: root.selectedRig() ? String(root.selectedRig().name || "Device Rig") : "Device Rig"
        tone: root.rigTone(root.selectedRig()) === "fault" ? "fault" : root.rigTone(root.selectedRig()) === "attention" ? "attention" : "informational"
        preferredWidth: 780
        property string addMemberId: ""
        property string addOutputId: ""
        function resetChoices() {
            const rig = root.selectedRig();
            const members = root.availableRigMemberChoices(rig);
            const outputs = root.availableOutputChoices(rig);
            addMemberId = members.length ? String(members[0].id || "") : "";
            addOutputId = outputs.length ? String(outputs[0].id || "") : "";
            if (rig) rigRename.text = String(rig.name || "");
        }
        onOpened: resetChoices()
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
                Text { text: rigDetailsContent.rig ? "Rig ID  ·  " + String(rigDetailsContent.rig.id || "") : "This Rig is no longer available."; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                RowLayout {
                    Layout.fillWidth: true
                    FlightDeckStatusChip { tokens: deck; label: rigDetailsContent.rig ? String(rigDetailsContent.rig.healthLabel || "Offline").toUpperCase() : "OFFLINE"; value: root.rigState(rigDetailsContent.rig); tone: root.rigTone(rigDetailsContent.rig) }
                    Text { Layout.fillWidth: true; text: rigDetailsContent.rig && rigDetailsContent.rig.configured ? "Configured by the current Profile and this Rig's primary Virtual Output." : "Viewing and editing this Rig does not activate it."; color: deck.textSecondary; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    RigButton { text: rigDetailsContent.rig && rigDetailsContent.rig.configured ? "ACTIVE" : "SET ACTIVE"; enabled: rigDetailsContent.rig && rigDetailsContent.rig.enabled && !rigDetailsContent.rig.configured; onClicked: root.activateRig(rigDetailsContent.rig) }
                }
                Text { text: "RIG NAME"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: rigRename
                        Layout.fillWidth: true
                        selectByMouse: true
                        color: deck.textPrimary
                        font.family: deck.bodyFont
                        background: Rectangle { radius: deck.radiusControl; color: deck.elevatedSurface; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    }
                    RigButton { text: "SAVE NAME"; enabled: rigDetailsContent.rig && rigRename.text.trim().length > 0 && rigRename.text.trim() !== String(rigDetailsContent.rig.name || ""); onClicked: root.reportBooleanAction(backend.renameDeviceRig(String(rigDetailsContent.rig.id || ""), rigRename.text), "Rig renamed", "The canonical Device Rig name was updated.", "Rig name was not updated", "Names must be unique and contain text.") }
                }

                Text { text: "PHYSICAL CONTROLLERS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.topMargin: deck.space4 }
                Text { text: "Required controllers must be connected, identity-safe, and verified for automatic activation. Optional controllers can be absent without blocking the Rig."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater {
                    model: rigDetailsContent.rig ? (rigDetailsContent.rig.members || []) : []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: memberDetail.implicitHeight + deck.space20
                        radius: deck.radiusControl
                        color: deck.elevatedSurface
                        border.color: deck.statusColor(root.memberTone(modelData))
                        ColumnLayout {
                            id: memberDetail
                            anchors.fill: parent
                            anchors.margins: deck.space10
                            spacing: deck.space6
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: root.markerFor(root.memberTone(modelData)); color: deck.statusColor(root.memberTone(modelData)); font.pixelSize: 14; font.bold: true }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Text { text: String(modelData.name || "Controller"); color: deck.textPrimary; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: root.memberState(modelData) + (modelData.connected ? "" : modelData.required ? " — required controllers block automatic selection while offline." : " — optional controllers do not block automatic selection while offline."); color: deck.statusColor(root.memberTone(modelData)); font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                                RigButton { text: modelData.required ? "REQUIRED" : "OPTIONAL"; subdued: !modelData.required; onClicked: root.reportBooleanAction(backend.setDeviceRigMemberRequired(String(rigDetailsContent.rig.id || ""), String(modelData.id || ""), !modelData.required), "Controller requirement updated", !modelData.required ? "This controller is now required for automatic activation." : "This controller is now optional and will not block automatic activation while offline.", "Controller requirement was not updated", "Refresh the Rig and try again.") }
                            }
                            Text { text: "Expected identity  ·  " + String(modelData.expectedIdentity || "Not recorded") + (modelData.seenIdentity ? "\nSeen identity  ·  " + String(modelData.seenIdentity) : ""); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                            RowLayout {
                                Layout.fillWidth: true
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
                                RigButton { text: "OPEN"; subdued: true; onClicked: { backend.setEditingDeviceContext(String(rigDetailsContent.rig.id || ""), [String(modelData.id || "")]); rigDetailsDialog.close(); Qt.callLater(function() { root.contentY = Math.max(0, controllersSection.y - deck.space8); }); } }
                                RigButton { text: "REMOVE"; destructive: true; enabled: (rigDetailsContent.rig.members || []).length > 1; onClicked: root.reportBooleanAction(backend.removeDeviceRigMember(String(rigDetailsContent.rig.id || ""), String(modelData.id || "")), "Controller removed", "The controller and its Rig-specific mapping payload were removed from this Rig.", "Controller was not removed", "A Device Rig must retain at least one controller.") }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
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
                    RigButton { text: "ADD CONTROLLER"; enabled: rigMemberAdder.currentIndex >= 0; onClicked: { const controller = root.controllerForCandidate(rigDetailsDialog.addMemberId); const added = controller && controller.id ? backend.addDeviceRigMember(String(rigDetailsContent.rig.id || ""), String(controller.id || ""), true) : controller ? backend.addDetectedDeviceToRig(String(rigDetailsContent.rig.id || ""), String(controller.directInputId || ""), true) : false; root.reportBooleanAction(added, "Controller added", "Review the controller routes and requirement before activation.", "Controller was not added", "Refresh devices or choose a controller that is not already in this Rig."); rigDetailsDialog.resetChoices(); } }
                }

                Text { text: "VIRTUAL OUTPUTS · PRIMARY REQUIRED"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.topMargin: deck.space4 }
                Repeater {
                    model: rigDetailsContent.rig ? (rigDetailsContent.rig.outputs || []) : []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: outputDetailRow.implicitHeight + deck.space16
                        radius: deck.radiusControl
                        color: deck.elevatedSurface
                        border.color: modelData.ready ? deck.border : deck.attention
                        RowLayout {
                            id: outputDetailRow
                            anchors.fill: parent
                            anchors.margins: deck.space8
                            spacing: deck.space8
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Text { text: (modelData.primary ? "PRIMARY · " : "") + String(modelData.name || "Virtual Output"); color: deck.textPrimary; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: String(modelData.status || "Output unavailable") + "  ·  " + Number(modelData.routeCount || 0) + " configured routes"; color: modelData.ready ? deck.textMuted : deck.attention; font.family: deck.telemetryFont; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                            RigButton { text: "OPEN"; subdued: true; onClicked: { rigDetailsDialog.close(); root.openRigOutput(String(modelData.id || "")); } }
                            RigButton { text: modelData.primary ? "PRIMARY" : "MAKE PRIMARY"; subdued: !!modelData.primary; enabled: !modelData.primary && !!modelData.enabled; onClicked: root.reportBooleanAction(backend.setDeviceRigPrimaryOutput(String(rigDetailsContent.rig.id || ""), String(modelData.id || "")), "Rig primary output updated", "Profiles assigned to this Rig now use this Virtual Output.", "Rig primary output was not updated", "Choose an enabled Virtual Output in this Rig.") }
                            RigButton { text: modelData.enabled ? "IN USE" : "ENABLE"; subdued: !modelData.enabled; onClicked: root.reportBooleanAction(backend.setDeviceRigOutputEnabled(String(rigDetailsContent.rig.id || ""), String(modelData.id || ""), !modelData.enabled), "Virtual Output updated", !modelData.enabled ? "This Virtual Output is enabled for the Rig." : "This Virtual Output is disabled for the Rig.", "Virtual Output was not updated", "A Rig needs at least one enabled, unassigned output.") }
                            RigButton { text: "REMOVE"; destructive: true; enabled: (rigDetailsContent.rig.outputs || []).length > 1; onClicked: root.reportBooleanAction(backend.removeDeviceRigOutput(String(rigDetailsContent.rig.id || ""), String(modelData.id || "")), "Virtual Output removed", "The output is no longer part of this Rig.", "Virtual Output was not removed", "Move member assignments first, then keep at least one output in the Rig.") }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
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
                }

                Text { visible: root.profilesReferencingRig(rigDetailsContent.rig ? rigDetailsContent.rig.id : "").length > 0; text: "REFERENCED BY PROFILES  ·  " + root.profilesReferencingRig(rigDetailsContent.rig ? rigDetailsContent.rig.id : "").join("  ·  "); color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true
                    RigButton { text: "OPEN SETUP HEALTH"; subdued: true; enabled: !!rigDetailsContent.rig; onClicked: { backend.setEditingDeviceContext(String(rigDetailsContent.rig.id || ""), []); rigDetailsDialog.close(); setupHealthDialog.open(); } }
                    Item { Layout.fillWidth: true }
                    RigButton { text: "DELETE RIG"; destructive: true; enabled: !!rigDetailsContent.rig; onClicked: { deleteRigDialog.rigId = String(rigDetailsContent.rig.id || ""); deleteRigDialog.open(); } }
                    RigButton { text: "CLOSE"; subdued: true; onClicked: rigDetailsDialog.close() }
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
            Text { text: deleteRigDialog.rig ? "Delete “" + String(deleteRigDialog.rig.name || "Device Rig") + "”? This cannot be undone." : "This Device Rig is no longer available."; color: deck.textPrimary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { text: root.profilesReferencingRig(deleteRigDialog.rigId).length ? root.profilesReferencingRig(deleteRigDialog.rigId).length + " Profile(s) reference this Rig. Their Device Rig assignment will be cleared through the canonical configuration path; no Profile will be remapped automatically." : "No Profiles currently reference this Rig. Saved physical-controller records remain available for other Rigs."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Text { visible: deleteRigDialog.rig && deleteRigDialog.rig.configured; text: "This Rig is currently configured. HOTAS BF6 will use its existing safe configuration transition when the canonical delete command clears it."; color: deck.attention; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
                Text { text: "HOTAS BF6 will apply only the scoped changes listed below, then verify the resulting controller state. Windows may request administrator permission."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
                        Text { text: "PLANNED CHANGES"; color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                        Repeater {
                            model: root.setupRepairPlan
                            delegate: Text { text: "• " + (modelData.title || modelData.message || "Scoped repair"); color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Text { text: "• Preserve unrelated HidHide rules and the existing mapping choice."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        text: "REPAIR SETUP"
                        focusPolicy: Qt.StrongFocus
                        onClicked: { repairConfirmation.close(); backend.repairSetupHealth() }
                        background: Rectangle { radius: deck.radiusControl; color: deck.accent; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
                    font.pixelSize: 11
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
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
                Text { text: "AXIS RANGE STATUS"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true }
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
                                Text { text: String(modelData.label || "Axis").toUpperCase(); color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: backend.calibrationStage === "RANGE" ? "CAPTURING RANGE" : backend.calibrationStage === "CENTER" || backend.calibrationStage === "FINALIZING" ? "CAPTURING CENTER" : (modelData.calibrationEnabled ? "CALIBRATED" : "RAW DEFAULT"); color: backend.calibrationActive ? deck.attention : modelData.calibrationEnabled ? deck.healthy : deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 58
                                Text { text: "MIN"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                Text { text: Number(modelData.calibrationMinimum || 0).toFixed(3); color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10 }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 58
                                Text { text: "NOW"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                Text { text: Number(modelData.raw || 0).toFixed(3); color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10 }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 58
                                Text { text: "MAX"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                Text { text: Number(modelData.calibrationMaximum || 0).toFixed(3); color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 10 }
                            }
                        }
                    }
                }
                Text { text: "CALIBRATION HISTORY"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true }
                Text { visible: backend.calibrationHistory.length === 0; text: "Successful calibrations for the selected and saved controllers appear here."; color: deck.textMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater {
                    model: backend.calibrationHistory
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        text: String(modelData.name || "Controller") + (modelData.currentDevice ? " · CURRENT DEVICE" : "") + "\n" + String(modelData.when || "") + " · " + String(modelData.axes || 0) + " axes calibrated"
                        color: modelData.currentDevice ? deck.healthy : deck.textSecondary
                        font.pixelSize: 10
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
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        objectName: "flightDeckCalibrationReset"
                        visible: backend.calibrationStage !== "IDLE" || backend.calibrationSuccess
                        text: "RESET"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.resetCalibration()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.secondarySurface; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        objectName: "flightDeckCalibrationCenter"
                        visible: backend.calibrationStage === "RANGE"
                        text: "CAPTURE CENTER"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.beginCalibrationCenterCapture()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.accentMuted; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        objectName: "flightDeckCalibrationSave"
                        visible: backend.calibrationStage === "CENTER"
                        text: "COMPLETE CALIBRATION"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: backend.saveCalibration()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.selected : deck.accentMuted; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.textPrimary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
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
            Text { text: "HOTAS BF6 will reverse only entries it added during this repair and then verify physical-controller access."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "CANCEL"
                    focusPolicy: Qt.StrongFocus
                    onClicked: undoConfirmation.close()
                    background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.border; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.textSecondary; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                Button {
                    text: "UNDO REPAIR"
                    focusPolicy: Qt.StrongFocus
                    onClicked: { undoConfirmation.close(); backend.undoControllerReadiness() }
                    background: Rectangle { radius: deck.radiusControl; color: deck.secondarySurface; border.color: parent.activeFocus ? deck.focus : deck.attention; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }
}
