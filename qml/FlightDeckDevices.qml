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
    // A long repair-plan fixture exercises the dialog's internal-scroll
    // contract without asking AppBackend to alter vJoy or HidHide state.
    property var proposedChangesPresentationOverride: null
    property bool virtualDetailsOpen: false
    property bool isolationDetailsOpen: false
    property bool verificationDetailsOpen: false
    signal navigateToPage(int page)

    readonly property bool wide: width >= 1040
    readonly property bool medium: width >= 760
    readonly property var state: readinessModel ? readinessModel.currentState : ({})
    // A newly loaded Devices page reads the same current state as the shared
    // readiness model, so fixture and live updates cannot leave its compact
    // status label one render behind.
    readonly property var readiness: readinessModel
        ? readinessModel.presentationFor(readinessModel.currentState) : ({})
    readonly property var input: readinessModel ? readinessModel.input : ({})
    readonly property var output: readinessModel ? readinessModel.output : ({})
    readonly property var isolation: readinessModel ? readinessModel.isolation : ({})
    readonly property var vjoyCheck: checkFor(["VJOY", "VIRTUAL OUTPUT"])
    readonly property var isolationCheck: checkFor(["HIDHIDE", "ISOLATION"])
    readonly property var physicalCheck: checkFor(["PHYSICAL", "CONTROLLER"])
    readonly property var controllerItems: controllerPresentationOverride === null
        ? backend.controllers : controllerPresentationOverride
    readonly property var axisItems: backend.axes
    readonly property bool checking: state.controllerSetupInProgress === undefined
        ? backend.controllerSetupInProgress : state.controllerSetupInProgress
    readonly property bool canRepairSetup: (state.controllerSetupCanApply === undefined
        ? backend.controllerSetupCanApply : state.controllerSetupCanApply) && !checking
    readonly property bool canUndoRepair: state.controllerSetupCanUndo === undefined
        ? backend.controllerSetupCanUndo : state.controllerSetupCanUndo
    readonly property bool canRepairHidHideAccess: (state.hidhideAvailable === undefined
        ? backend.hidhideAvailable : state.hidhideAvailable)
        && (state.hidhideCloakStateKnown === undefined
            ? backend.hidhideCloakStateKnown : state.hidhideCloakStateKnown)
        && (state.hidhideCloaked === undefined ? backend.hidhideCloaked : state.hidhideCloaked)
        && !(state.hidhideMapperAllowed === undefined
            ? backend.hidhideMapperAllowed : state.hidhideMapperAllowed)
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
    readonly property string verificationState: state.controllerReadinessState === undefined
        ? backend.controllerReadinessState : state.controllerReadinessState
    readonly property string verificationStatus: state.controllerReadinessStatus === undefined
        ? backend.controllerReadinessStatus : state.controllerReadinessStatus
    readonly property var proposedChanges: proposedChangesPresentationOverride !== null
        ? proposedChangesPresentationOverride
        : state.controllerReadinessProposedChanges === undefined
            ? backend.controllerReadinessProposedChanges : state.controllerReadinessProposedChanges
    readonly property bool reconnectRequired: state.controllerReconnectRequired === undefined
        ? backend.controllerReconnectRequired : state.controllerReconnectRequired
    readonly property bool disconnectObserved: state.controllerDisconnectObserved === undefined
        ? backend.controllerDisconnectObserved : state.controllerDisconnectObserved
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
        if (severity === "error" || label.indexOf("ERROR") >= 0 || label.indexOf("REQUIRED") >= 0)
            return "fault";
        if (severity === "warning" || label.indexOf("ATTENTION") >= 0)
            return "attention";
        return "informational";
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

    function revealContext() {
        let target = null;
        if (requestedContext === "virtual-output") target = virtualOutputSection;
        else if (requestedContext === "isolation") target = isolationSection;
        else if (requestedContext === "verification") target = verificationSection;
        else if (requestedContext === "controllers") target = controllersSection;
        if (target)
            contentY = Math.max(0, Math.min(contentHeight - height, target.y - deck.space8));
    }

    onRequestedContextChanged: Qt.callLater(revealContext)
    Component.onCompleted: Qt.callLater(revealContext)

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
                    text: "Devices"
                    color: deck.textPrimary
                    font.family: deck.displayFont
                    font.pixelSize: root.wide ? 30 : 25
                    font.bold: true
                }
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
                        color: Qt.rgba(deck.statusColor(readiness.tone || "informational").r,
                                       deck.statusColor(readiness.tone || "informational").g,
                                       deck.statusColor(readiness.tone || "informational").b, 0.16)
                        border.color: deck.statusColor(readiness.tone || "informational")
                        Text {
                            anchors.centerIn: parent
                            text: root.markerFor(readiness.tone || "informational")
                            color: deck.statusColor(readiness.tone || "informational")
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
                            text: readiness.label || "CHECKING"
                            color: deck.statusColor(readiness.tone || "informational")
                            font.family: deck.displayFont
                            font.pixelSize: root.medium ? 22 : 18
                            font.bold: true
                        }
                        Text {
                            text: checking ? "Checking the controller chain…" : (readiness.detail || "Checking current controller setup.")
                            color: deck.textSecondary
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    Button {
                        objectName: "flightDeckSetupHealthAction"
                        text: checking ? "CHECKING…" : (root.canRepairSetup ? "REPAIR SETUP" : "VERIFY SETUP")
                        enabled: !checking
                        visible: root.medium
                        implicitHeight: deck.controlHeight
                        leftPadding: deck.space16
                        rightPadding: deck.space16
                        focusPolicy: Qt.StrongFocus
                        onClicked: root.canRepairSetup ? repairConfirmation.open() : backend.verifyHotasSetup()
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
                            { label: "Controller verification", check: root.physicalCheck },
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
                    text: checking ? "CHECKING SETUP…" : (root.canRepairSetup ? "REPAIR SETUP" : "VERIFY SETUP")
                    enabled: !checking
                    Layout.fillWidth: true
                    implicitHeight: deck.controlHeight
                    focusPolicy: Qt.StrongFocus
                    onClicked: root.canRepairSetup ? repairConfirmation.open() : backend.verifyHotasSetup()
                    background: Rectangle { radius: deck.radiusControl; color: deck.accent; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                    contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
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
                        Text { text: "VIRTUAL OUTPUT"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                        Text { text: root.vjoyReady ? "Online" : "Action needed"; color: deck.statusColor(root.toneFor(root.vjoyCheck)); font.family: deck.displayFont; font.pixelSize: 18; font.bold: true }
                        Text { text: root.vjoyCheck.message || output.detail || "Checking virtual output."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    FlightDeckStatusChip { tokens: deck; label: root.vjoyReady ? "ONLINE" : "ACTION NEEDED"; value: "vJoy " + root.vjoyDeviceId; tone: root.toneFor(root.vjoyCheck); visible: root.medium }
                }
                Text { text: "Virtual output is the controller signal games receive from HOTAS BF6."; color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        visible: !root.vjoyReady
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
                        text: root.canRepairSetup ? "REPAIR SETUP" : root.canRepairHidHideAccess ? "FIX APP ACCESS" : "OPEN HIDHIDE"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: {
                            if (root.canRepairSetup) repairConfirmation.open()
                            else if (root.canRepairHidHideAccess) backend.repairHidHideAccess()
                            else backend.openHidHideConfiguration()
                        }
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
            objectName: "flightDeckVerification"
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: verificationContent.implicitHeight + deck.space32
            border.color: root.requestedContext === "verification" ? deck.accent : deck.border
            ColumnLayout {
                id: verificationContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text { text: "CONTROLLER VERIFICATION"; color: deck.textMuted; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                        Text { text: checking ? "Checking setup" : String(root.verificationState || "Not checked"); color: deck.statusColor(readiness.tone || "informational"); font.family: deck.displayFont; font.pixelSize: 18; font.bold: true }
                        Text { text: root.verificationStatus || "Verify the selected controller, virtual output, and device isolation."; color: deck.textSecondary; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    Button {
                        objectName: "flightDeckVerifySetup"
                        text: checking ? "VERIFYING…" : "VERIFY SETUP"
                        enabled: !checking
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.controlHeight
                        onClicked: backend.verifyHotasSetup()
                        background: Rectangle { radius: deck.radiusControl; color: parent.enabled ? deck.accent : deck.disabled; border.color: parent.activeFocus ? deck.focus : deck.accent; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.light ? "white" : deck.primarySurface; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                Repeater {
                    model: state.checks || []
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: verificationRow.implicitHeight + deck.space16
                        radius: deck.radiusControl
                        color: deck.elevatedSurface
                        border.color: deck.border
                        RowLayout {
                            id: verificationRow
                            anchors.fill: parent
                            anchors.margins: deck.space8
                            spacing: deck.space8
                            Text { text: root.markerFor(root.toneFor(modelData)); color: deck.statusColor(root.toneFor(modelData)); font.pixelSize: 15; font.bold: true }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: deck.space4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: modelData.name || "Setup check"; color: deck.textPrimary; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: String(modelData.state || "Checking").toUpperCase(); color: deck.statusColor(root.toneFor(modelData)); font.family: deck.telemetryFont; font.pixelSize: 8; font.bold: true }
                                }
                                Text { text: modelData.message || ""; color: deck.textSecondary; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
                Rectangle {
                    visible: root.proposedChanges.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? proposedChanges.implicitHeight + deck.space24 : 0
                    radius: deck.radiusControl
                    color: deck.secondarySurface
                    border.color: deck.attention
                    ColumnLayout {
                        id: proposedChanges
                        anchors.fill: parent
                        anchors.margins: deck.space12
                        spacing: deck.space4
                        Text { text: "RECOMMENDED NEXT STEP"; color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true }
                        Repeater {
                            model: root.proposedChanges
                            delegate: Text { text: "• " + (modelData.message || ""); color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                }
                Text {
                    visible: root.reconnectRequired
                    text: root.disconnectObserved ? "Controller disconnected. Reconnect the selected controller and move a control to complete verification." : "Device isolation changed visibility. Unplug and reconnect the selected controller when prompted to complete verification."
                    color: deck.attention
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Button {
                        visible: root.canUndoRepair
                        text: "UNDO REPAIR"
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: deck.compactControlHeight
                        onClicked: undoConfirmation.open()
                        background: Rectangle { radius: deck.radiusControl; color: parent.down ? deck.secondarySurface : "transparent"; border.color: parent.activeFocus ? deck.focus : deck.attention; border.width: parent.activeFocus ? 2 : 1 }
                        contentItem: Text { text: parent.text; color: deck.attention; font.family: deck.telemetryFont; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Item { Layout.fillWidth: true }
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
                            model: root.proposedChanges
                            delegate: Text { text: "• " + (modelData.message || ""); color: deck.textSecondary; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
                        onClicked: { repairConfirmation.close(); backend.applyControllerReadiness() }
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
