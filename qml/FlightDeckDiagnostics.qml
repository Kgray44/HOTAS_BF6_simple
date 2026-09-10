import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Native Flight Deck diagnostics. This is deliberately a presentation and
// navigation layer over the existing AppBackend snapshots. It never starts a
// verifier, polls a driver, changes a mapping, or writes virtual output.
Flickable {
    id: root
    objectName: "flightDeckDiagnostics"

    property var readinessModel
    // Startup tests use this display-only override for deterministic visual
    // states. Production always reads the current backend/readiness snapshots.
    property var presentationOverride: null
    property string filterMode: "all"
    property bool technicalDetailsExpanded: false
    property bool inputDetailsExpanded: false
    property bool outputDetailsExpanded: false
    property bool isolationDetailsExpanded: false
    property bool eventLogExpanded: false

    signal navigateToPage(int page)
    signal navigateToDevices(string context)

    readonly property bool wide: width >= 1120
    readonly property bool medium: width >= 760
    readonly property var readinessState: readinessModel ? readinessModel.currentState : ({})
    readonly property var readiness: readinessModel ? readinessModel.readiness : ({})
    readonly property var inputHealth: readinessModel ? readinessModel.input : ({})
    readonly property var outputHealth: readinessModel ? readinessModel.output : ({})
    readonly property var isolationHealth: readinessModel ? readinessModel.isolation : ({})
    readonly property var gameHealth: readinessModel ? readinessModel.game : ({})
    readonly property var profileHealth: readinessModel ? readinessModel.profile : ({})

    readonly property var axes: overrideValue("axes", backend.axes)
    readonly property var buttons: overrideValue("buttons", backend.buttons)
    readonly property var povs: overrideValue("povs", backend.povs)
    readonly property var controllers: overrideValue("controllers", backend.controllers)
    readonly property var automationRules: overrideValue("automationRules", backend.automationRules)
    readonly property var adaptive: overrideValue("adaptive", backend.adaptiveResponseTelemetry)
    readonly property var events: overrideValue("events", backend.eventLog)
    readonly property var checks: overrideValue("checks", readinessState.checks || backend.controllerReadinessChecks)
    readonly property bool physicalConnected: boolValue("physicalConnected", readinessState.physicalConnected === undefined ? backend.physicalConnected : readinessState.physicalConnected)
    readonly property int connectedControllerCount: numberValue("connectedControllerCount", readinessState.connectedControllerCount === undefined ? backend.connectedControllerCount : readinessState.connectedControllerCount)
    readonly property string deviceName: stringValue("deviceName", readinessState.deviceName || backend.deviceName)
    readonly property string deviceId: stringValue("deviceId", backend.deviceId)
    readonly property bool mappingActive: boolValue("mappingActive", readinessState.mappingActive === undefined ? backend.mappingActive : readinessState.mappingActive)
    readonly property bool mappingRequested: boolValue("mappingRequested", readinessState.mappingRequested === undefined ? backend.mappingRequested : readinessState.mappingRequested)
    readonly property string mappingStatus: stringValue("mappingStatus", readinessState.mappingStatus === undefined ? backend.mappingStatus : readinessState.mappingStatus)
    readonly property bool vjoyReady: boolValue("vjoyReady", readinessState.vjoyReady === undefined ? backend.vjoyReady : readinessState.vjoyReady)
    readonly property string vjoyStatus: stringValue("vjoyStatus", readinessState.vjoyStatus === undefined ? backend.vjoyStatus : readinessState.vjoyStatus)
    readonly property string vjoySeverity: stringValue("vjoyStatusSeverity", readinessState.vjoyStatusSeverity === undefined ? backend.vjoyStatusSeverity : readinessState.vjoyStatusSeverity)
    readonly property int vjoyDeviceId: numberValue("vjoyDeviceId", readinessState.vjoyDeviceId === undefined ? backend.vjoyDeviceId : readinessState.vjoyDeviceId)
    readonly property bool hidhideAvailable: boolValue("hidhideAvailable", readinessState.hidhideAvailable === undefined ? backend.hidhideAvailable : readinessState.hidhideAvailable)
    readonly property bool hidhideCloakStateKnown: boolValue("hidhideCloakStateKnown", readinessState.hidhideCloakStateKnown === undefined ? backend.hidhideCloakStateKnown : readinessState.hidhideCloakStateKnown)
    readonly property bool hidhideCloaked: boolValue("hidhideCloaked", readinessState.hidhideCloaked === undefined ? backend.hidhideCloaked : readinessState.hidhideCloaked)
    readonly property bool hidhideMapperAllowed: boolValue("hidhideMapperAllowed", readinessState.hidhideMapperAllowed === undefined ? backend.hidhideMapperAllowed : readinessState.hidhideMapperAllowed)
    readonly property int automationRuleCount: numberValue("automationRuleCount", backend.automationRuleCount)
    readonly property int automationActiveRuleCount: numberValue("automationActiveRuleCount", backend.automationActiveRuleCount)
    readonly property bool automationEngineEnabled: boolValue("automationEngineEnabled", backend.automationEngineEnabled)
    readonly property string automationValidationMessage: stringValue("automationValidationMessage", backend.automationValidationMessage)

    FlightDeckTheme {
        id: deck
    }

    component SectionLabel: Text {
        required property string label
        text: label
        color: deck.textMuted
        font.family: deck.telemetryFont
        font.pixelSize: 10
        font.bold: true
        Layout.fillWidth: true
    }

    component OutlineButton: Button {
        id: outlineButton
        property string tone: "informational"
        implicitHeight: deck.compactControlHeight
        focusPolicy: Qt.StrongFocus
        background: Rectangle {
            radius: deck.radiusControl
            color: outlineButton.down ? Qt.rgba(deck.statusColor(outlineButton.tone).r, deck.statusColor(outlineButton.tone).g, deck.statusColor(outlineButton.tone).b, 0.20) : "transparent"
            border.color: outlineButton.activeFocus ? deck.focus : deck.statusColor(outlineButton.tone)
            border.width: outlineButton.activeFocus ? 2 : 1
        }
        contentItem: Text {
            text: outlineButton.text
            color: deck.statusColor(outlineButton.tone)
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component TechnicalRow: RowLayout {
        id: technicalRow
        required property string label
        required property string value
        property string valueTone: "informational"
        Layout.fillWidth: true
        spacing: deck.space12
        Text {
            text: technicalRow.label.toUpperCase()
            color: deck.textMuted
            font.family: deck.telemetryFont
            font.pixelSize: 9
            font.bold: true
            Layout.preferredWidth: root.medium ? 148 : 112
            elide: Text.ElideRight
        }
        Text {
            text: technicalRow.value
            color: deck.statusColor(technicalRow.valueTone)
            font.family: deck.telemetryFont
            font.pixelSize: 10
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }
    }

    component MetricTile: FlightDeckCard {
        id: metricTile
        tokens: deck
        required property string label
        required property string value
        property string detail: ""
        property string tone: "informational"
        Layout.fillWidth: true
        implicitHeight: metricContent.implicitHeight + deck.space24
        ColumnLayout {
            id: metricContent
            anchors.fill: parent
            anchors.margins: deck.space12
            spacing: deck.space4
            Text {
                text: metricTile.label
                color: deck.textMuted
                font.family: deck.telemetryFont
                font.pixelSize: 8
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: metricTile.value
                color: deck.statusColor(metricTile.tone)
                font.family: deck.displayFont
                font.pixelSize: 16
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                visible: text.length > 0
                text: metricTile.detail
                color: deck.textSecondary
                font.pixelSize: 9
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }

    Rectangle {
        width: root.width
        height: Math.max(root.height, root.contentHeight)
        color: deck.primarySurface
        z: -1
    }

    contentWidth: width
    contentHeight: diagnosticsContent.implicitHeight + deck.space32
    clip: true
    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    function overrideValue(name, fallback) {
        const override = presentationOverride || {};
        return override[name] === undefined ? fallback : override[name];
    }
    function boolValue(name, fallback) {
        return Boolean(overrideValue(name, fallback));
    }
    function numberValue(name, fallback) {
        return Number(overrideValue(name, fallback));
    }
    function stringValue(name, fallback) {
        return String(overrideValue(name, fallback) || "");
    }
    function percent(value) {
        const number = Number(value || 0) * 100;
        return (number >= 0 ? "+" : "") + number.toFixed(1) + "%";
    }
    function metric(value, suffix, precision) {
        return Number(value || 0).toFixed(precision === undefined ? 0 : precision) + suffix;
    }
    function checkFor(names) {
        for (let index = 0; index < checks.length; ++index) {
            const name = String(checks[index].name || "").toUpperCase();
            for (let candidate = 0; candidate < names.length; ++candidate) {
                if (name.indexOf(String(names[candidate]).toUpperCase()) >= 0)
                    return checks[index];
            }
        }
        return {
            state: "Checking",
            message: "Status has not been checked yet.",
            severity: "info"
        };
    }
    function checkTone(check) {
        const severity = String((check || {}).severity || "").toLowerCase();
        const state = String((check || {}).state || "").toUpperCase();
        if (severity === "ready" || state === "READY")
            return "healthy";
        if (severity === "error" || state.indexOf("ERROR") >= 0 || state.indexOf("REQUIRED") >= 0)
            return "fault";
        if (severity === "warning" || state.indexOf("ATTENTION") >= 0)
            return "attention";
        return "informational";
    }
    function readableTone(tone) {
        if (tone === "healthy")
            return "Healthy";
        if (tone === "attention")
            return "Attention";
        if (tone === "fault")
            return "Action needed";
        return "Checking";
    }
    function routeStatus(axis) {
        if (!axis.available)
            return {
                label: "Disconnected",
                tone: "attention",
                detail: "This physical axis is not available on the active controller."
            };
        if (axis.fixed)
            return {
                label: "Fixed input",
                tone: "informational",
                detail: axis.activityDetail || "No meaningful motion was observed during calibration."
            };
        if (!axis.targetAvailable)
            return {
                label: "Output unavailable",
                tone: "fault",
                detail: "The configured virtual axis is unavailable."
            };
        if (!axis.virtualRouted)
            return {
                label: "Not routed",
                tone: "informational",
                detail: "This axis has no active virtual output route."
            };
        if (!axis.virtualValid)
            return {
                label: "Output standby",
                tone: "attention",
                detail: "The configured route is waiting for virtual output."
            };
        return {
            label: "Active",
            tone: "healthy",
            detail: "Current source-to-output route is available."
        };
    }
    function routedAxes() {
        const list = [];
        for (let index = 0; index < axes.length; ++index) {
            if (axes[index] && axes[index].available)
                list.push(axes[index]);
        }
        return list;
    }
    function routeProblems() {
        const results = [];
        for (let index = 0; index < axes.length; ++index) {
            const axis = axes[index];
            if (!axis)
                continue;
            const status = routeStatus(axis);
            if (status.tone === "attention" || status.tone === "fault")
                results.push(axis);
        }
        return results;
    }
    function enabledAutomationCount() {
        let count = 0;
        for (let index = 0; index < automationRules.length; ++index) {
            if (automationRules[index].enabled)
                ++count;
        }
        return count;
    }
    function invalidAutomationCount() {
        let count = 0;
        for (let index = 0; index < automationRules.length; ++index) {
            if (Number(automationRules[index].health || 0) !== 0)
                ++count;
        }
        return count;
    }
    function automationTone() {
        if (invalidAutomationCount() > 0)
            return "attention";
        if (!automationEngineEnabled || automationRuleCount === 0)
            return "informational";
        return "healthy";
    }
    function adaptiveTone() {
        return adaptive.enabled ? "healthy" : "informational";
    }
    function hasAttention(section) {
        if (section === "input")
            return inputHealth.tone === "attention" || inputHealth.tone === "fault";
        if (section === "output")
            return outputHealth.tone === "attention" || outputHealth.tone === "fault";
        if (section === "isolation")
            return isolationHealth.tone === "attention" || isolationHealth.tone === "fault";
        if (section === "routing")
            return routeProblems().length > 0;
        if (section === "profiles")
            return profileHealth.tone === "attention" || profileHealth.tone === "fault";
        if (section === "automation")
            return automationTone() === "attention" || automationTone() === "fault";
        return false;
    }
    function showSection(section) {
        return filterMode === "all" || hasAttention(section);
    }
    function sectionGeometry(section) {
        const item = section === "summary" ? summarySection : section === "path" ? signalPathSection : section === "systems" ? systemsSection : section === "inspection" ? inspectionSection : section === "performance" ? performanceSection : advancedSection;
        return {
            top: item.y,
            bottom: item.y + item.height,
            height: item.height,
            contentHeight: contentHeight
        };
    }
    function scrollToSection(section) {
        const item = section === "summary" ? summarySection : section === "path" ? signalPathSection : section === "systems" ? systemsSection : section === "inspection" ? inspectionSection : section === "performance" ? performanceSection : advancedSection;
        if (!item || !item.visible)
            return false;
        contentY = Math.max(0, Math.min(contentHeight - height, item.y - deck.space12));
        return true;
    }

    ColumnLayout {
        id: diagnosticsContent
        x: deck.space4
        y: deck.space4
        width: Math.max(0, root.width - deck.space8)
        spacing: deck.space16

        Item {
            id: summarySection
            Layout.fillWidth: true
            Layout.preferredHeight: 1
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: deck.space16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: deck.space4
                Text {
                    text: "Diagnostics"
                    color: deck.textPrimary
                    font.family: deck.displayFont
                    font.pixelSize: root.medium ? 30 : 25
                    font.bold: true
                }
                Text {
                    text: "System health, signal routing, live technical state, and recovery navigation."
                    color: deck.textSecondary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
            FlightDeckStatusChip {
                tokens: deck
                label: readiness.label || "CHECKING"
                tone: readiness.tone || "informational"
                visible: root.medium
            }
        }

        FlightDeckCard {
            objectName: "flightDeckDiagnosticsHealthHero"
            tokens: deck
            color: deck.secondarySurface
            Layout.fillWidth: true
            implicitHeight: healthContent.implicitHeight + deck.space32
            border.color: deck.statusColor(readiness.tone || "informational")
            ColumnLayout {
                id: healthContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "SYSTEM HEALTH"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: readiness.label || "CHECKING"
                            color: deck.statusColor(readiness.tone || "informational")
                            font.family: deck.displayFont
                            font.pixelSize: root.medium ? 25 : 21
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Text {
                            text: readiness.detail || "Checking current setup state."
                            color: deck.textSecondary
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    FlightDeckStatusChip {
                        tokens: deck
                        label: "READINESS"
                        value: readableTone(readiness.tone || "informational").toUpperCase()
                        tone: readiness.tone || "informational"
                        visible: root.wide
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: deck.divider
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.wide ? 4 : (root.medium ? 2 : 1)
                    columnSpacing: deck.space12
                    rowSpacing: deck.space8
                    Repeater {
                        model: [
                            {
                                label: "PHYSICAL INPUT",
                                value: inputHealth.title || "Checking",
                                tone: inputHealth.tone || "informational"
                            },
                            {
                                label: "VIRTUAL OUTPUT",
                                value: outputHealth.title || "Checking",
                                tone: outputHealth.tone || "informational"
                            },
                            {
                                label: "DEVICE ISOLATION",
                                value: isolationHealth.title || "Checking",
                                tone: isolationHealth.tone || "informational"
                            },
                            {
                                label: "PROFILE / GAME",
                                value: profileHealth.title || gameHealth.title || "Checking",
                                tone: profileHealth.tone || "informational"
                            }
                        ]
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: deck.space8
                            Rectangle {
                                implicitWidth: 7
                                implicitHeight: 7
                                Layout.preferredWidth: 7
                                Layout.preferredHeight: 7
                                radius: 4
                                color: deck.statusColor(modelData.tone)
                            }
                            Text {
                                text: modelData.label
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 8
                                font.bold: true
                            }
                            Text {
                                text: modelData.value
                                color: deck.textPrimary
                                font.pixelSize: 10
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        Flow {
            objectName: "flightDeckDiagnosticsSectionNavigation"
            Layout.fillWidth: true
            spacing: deck.space8
            Repeater {
                model: [
                    {
                        label: "SUMMARY",
                        section: "summary"
                    },
                    {
                        label: "SIGNAL PATH",
                        section: "path"
                    },
                    {
                        label: "SYSTEMS",
                        section: "systems"
                    },
                    {
                        label: "LIVE",
                        section: "inspection"
                    },
                    {
                        label: "PERFORMANCE",
                        section: "performance"
                    },
                    {
                        label: "ADVANCED",
                        section: "advanced"
                    }
                ]
                delegate: OutlineButton {
                    objectName: "flightDeckDiagnosticsSection_" + modelData.section
                    text: modelData.label
                    tone: "informational"
                    onClicked: root.scrollToSection(modelData.section)
                }
            }
            OutlineButton {
                objectName: "flightDeckDiagnosticsFilterAttention"
                text: root.filterMode === "attention" ? "SHOW ALL" : "NEEDS ATTENTION"
                tone: root.filterMode === "attention" ? "attention" : "informational"
                onClicked: root.filterMode = root.filterMode === "attention" ? "all" : "attention"
            }
        }

        Item {
            id: signalPathSection
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.showSection("input") || root.showSection("output") || root.showSection("routing")
        }
        SectionLabel {
            label: "SIGNAL PATH"
            visible: signalPathSection.visible
        }
        FlightDeckCard {
            objectName: "flightDeckDiagnosticsSignalPath"
            tokens: deck
            Layout.fillWidth: true
            visible: signalPathSection.visible
            implicitHeight: pathContent.implicitHeight + deck.space32
            ColumnLayout {
                id: pathContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space16
                Text {
                    text: "The active selected-controller route is shown from physical input through the configured virtual output."
                    color: deck.textSecondary
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.wide ? 4 : 1
                    columnSpacing: deck.space12
                    rowSpacing: deck.space12
                    Repeater {
                        model: [
                            {
                                label: "PHYSICAL INPUT",
                                title: deviceName || "No controller",
                                detail: inputHealth.detail || "",
                                tone: inputHealth.tone || "informational"
                            },
                            {
                                label: "MAPPING",
                                title: mappingStatus || "Standing by",
                                detail: mappingActive ? "Configured routes are active." : mappingRequested ? "Configured routes are waiting for output." : "Mapping output is neutralized.",
                                tone: mappingActive ? "healthy" : mappingRequested ? "attention" : "informational"
                            },
                            {
                                label: "VIRTUAL OUTPUT",
                                title: vjoyReady ? "vJoy Device " + vjoyDeviceId : "Virtual output unavailable",
                                detail: vjoyStatus,
                                tone: outputHealth.tone || "informational"
                            },
                            {
                                label: "GAME",
                                title: gameHealth.title || "No supported game detected",
                                detail: gameHealth.detail || "",
                                tone: gameHealth.tone || "informational"
                            }
                        ]
                        delegate: FlightDeckCard {
                            required property var modelData
                            tokens: deck
                            contentPadding: deck.cardPaddingCompact
                            Layout.fillWidth: true
                            implicitHeight: pathNode.implicitHeight + contentPadding * 2
                            color: deck.elevatedSurface
                            ColumnLayout {
                                id: pathNode
                                anchors.fill: parent
                                anchors.margins: parent.contentPadding
                                spacing: deck.space4
                                Text {
                                    text: modelData.label
                                    color: deck.textMuted
                                    font.family: deck.telemetryFont
                                    font.pixelSize: 8
                                    font.bold: true
                                }
                                Text {
                                    text: modelData.title
                                    color: deck.textPrimary
                                    font.family: deck.displayFont
                                    font.pixelSize: 15
                                    font.bold: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Rectangle {
                                        implicitWidth: 7
                                        implicitHeight: 7
                                        Layout.preferredWidth: 7
                                        Layout.preferredHeight: 7
                                        radius: 4
                                        color: deck.statusColor(modelData.tone)
                                    }
                                    Text {
                                        text: modelData.detail
                                        color: deck.textSecondary
                                        font.pixelSize: 9
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: deck.divider
                }
                Text {
                    text: "CURRENT AXIS ROUTES"
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 9
                    font.bold: true
                }
                Repeater {
                    model: root.routedAxes()
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: deck.space8
                        Text {
                            text: modelData.label
                            color: deck.textPrimary
                            font.family: deck.telemetryFont
                            font.pixelSize: 10
                            font.bold: true
                            Layout.preferredWidth: root.medium ? 140 : 88
                            elide: Text.ElideRight
                        }
                        Text {
                            text: "→"
                            color: deck.accent
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Text {
                            text: modelData.target || "Disabled"
                            color: deck.textPrimary
                            font.family: deck.telemetryFont
                            font.pixelSize: 10
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        FlightDeckStatusChip {
                            tokens: deck
                            label: root.routeStatus(modelData).label.toUpperCase()
                            tone: root.routeStatus(modelData).tone
                            visible: root.medium
                        }
                    }
                }
                Text {
                    visible: root.routedAxes().length === 0
                    text: "No active physical axis snapshot is available for the selected controller."
                    color: deck.textSecondary
                    font.pixelSize: 10
                }
            }
        }

        Item {
            id: systemsSection
            Layout.fillWidth: true
            Layout.preferredHeight: 1
        }
        SectionLabel {
            label: "SUBSYSTEM HEALTH"
        }
        GridLayout {
            objectName: "flightDeckDiagnosticsSystems"
            Layout.fillWidth: true
            columns: root.wide ? 3 : (root.medium ? 2 : 1)
            columnSpacing: deck.space12
            rowSpacing: deck.space12

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsInput"
                tokens: deck
                Layout.fillWidth: true
                visible: root.showSection("input")
                implicitHeight: inputCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: inputCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: deck.space4
                            Text {
                                text: "PHYSICAL INPUT"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: inputHealth.title || "Checking"
                                color: deck.statusColor(inputHealth.tone || "informational")
                                font.family: deck.displayFont
                                font.pixelSize: 18
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                        FlightDeckStatusChip {
                            tokens: deck
                            label: readableTone(inputHealth.tone || "informational").toUpperCase()
                            tone: inputHealth.tone || "informational"
                            visible: root.medium
                        }
                    }
                    Text {
                        text: inputHealth.detail || "Physical input state is still being checked."
                        color: deck.textSecondary
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: deviceName || "No active physical controller"
                        color: deck.textPrimary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: backend.axisCount + " axes  •  " + backend.buttonCount + " buttons  •  " + backend.povCount + " POV"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: deck.space8
                        OutlineButton {
                            objectName: "flightDeckDiagnosticsOpenDevices"
                            text: "OPEN DEVICES"
                            tone: "informational"
                            onClicked: root.navigateToDevices("controllers")
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        OutlineButton {
                            text: root.inputDetailsExpanded ? "HIDE DETAILS" : "DETAILS"
                            tone: "informational"
                            onClicked: root.inputDetailsExpanded = !root.inputDetailsExpanded
                        }
                    }
                    ColumnLayout {
                        visible: root.inputDetailsExpanded
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: deck.divider
                        }
                        TechnicalRow {
                            label: "Selected device ID"
                            value: root.deviceId || "Not available"
                        }
                        TechnicalRow {
                            label: "Connected devices"
                            value: String(root.connectedControllerCount)
                        }
                        TechnicalRow {
                            label: "Input age"
                            value: backend.lastPhysicalUpdateAgeMs >= 0 ? backend.lastPhysicalUpdateAgeMs + " ms" : "No report received"
                            valueTone: backend.lastPhysicalUpdateAgeMs >= 0 ? "informational" : "attention"
                        }
                        TechnicalRow {
                            label: "Input reports"
                            value: root.metric(backend.inputReportsPerSecond, " Hz", 0)
                        }
                    }
                }
            }

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsOutput"
                tokens: deck
                Layout.fillWidth: true
                visible: root.showSection("output")
                implicitHeight: outputCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: outputCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: deck.space4
                            Text {
                                text: "VIRTUAL OUTPUT"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: vjoyReady ? "Online" : "Action needed"
                                color: deck.statusColor(outputHealth.tone || "informational")
                                font.family: deck.displayFont
                                font.pixelSize: 18
                                font.bold: true
                            }
                        }
                        FlightDeckStatusChip {
                            tokens: deck
                            label: vjoyReady ? "ONLINE" : "OFFLINE"
                            value: "vJoy " + vjoyDeviceId
                            tone: outputHealth.tone || "informational"
                            visible: root.medium
                        }
                    }
                    Text {
                        text: vjoyStatus || outputHealth.detail || "Virtual output status is not available."
                        color: deck.textSecondary
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: backend.virtualAxisStatus
                        color: deck.textPrimary
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: deck.space8
                        OutlineButton {
                            text: "OPEN SETUP"
                            tone: outputHealth.tone || "informational"
                            onClicked: root.navigateToDevices("virtual-output")
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        OutlineButton {
                            text: root.outputDetailsExpanded ? "HIDE DETAILS" : "DETAILS"
                            onClicked: root.outputDetailsExpanded = !root.outputDetailsExpanded
                        }
                    }
                    ColumnLayout {
                        visible: root.outputDetailsExpanded
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: deck.divider
                        }
                        TechnicalRow {
                            label: "Configured device"
                            value: "vJoy Device " + root.vjoyDeviceId
                        }
                        TechnicalRow {
                            label: "Button capacity"
                            value: backend.vjoyButtonCount + " configured / " + backend.vjoyRequiredButtonCount + " required"
                            valueTone: backend.vjoyCapacitySufficient ? "healthy" : "attention"
                        }
                        TechnicalRow {
                            label: "POV capacity"
                            value: backend.vjoyContinuousPovCount + " continuous / " + backend.vjoyDiscretePovCount + " discrete"
                        }
                        TechnicalRow {
                            label: "Write cadence"
                            value: root.metric(backend.vjoyWritesPerSecond, " /s", 0)
                        }
                    }
                }
            }

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsIsolation"
                tokens: deck
                Layout.fillWidth: true
                visible: root.showSection("isolation")
                implicitHeight: isolationCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: isolationCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: deck.space4
                            Text {
                                text: "DEVICE ISOLATION"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: isolationHealth.tone === "healthy" ? "Protected" : readableTone(isolationHealth.tone || "informational")
                                color: deck.statusColor(isolationHealth.tone || "informational")
                                font.family: deck.displayFont
                                font.pixelSize: 18
                                font.bold: true
                            }
                        }
                        FlightDeckStatusChip {
                            tokens: deck
                            label: "HIDHIDE"
                            value: String(checkFor(["HIDHIDE", "ISOLATION"]).state || "CHECKING").toUpperCase()
                            tone: isolationHealth.tone || "informational"
                            visible: root.medium
                        }
                    }
                    Text {
                        text: isolationHealth.detail || "HidHide status has not been checked yet."
                        color: deck.textSecondary
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: "HidHide prevents a game from seeing both the physical controller and virtual output."
                        color: deck.textMuted
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: deck.space8
                        OutlineButton {
                            text: "OPEN SETUP"
                            tone: isolationHealth.tone || "informational"
                            onClicked: root.navigateToDevices("isolation")
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        OutlineButton {
                            text: root.isolationDetailsExpanded ? "HIDE DETAILS" : "DETAILS"
                            onClicked: root.isolationDetailsExpanded = !root.isolationDetailsExpanded
                        }
                    }
                    ColumnLayout {
                        visible: root.isolationDetailsExpanded
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: deck.divider
                        }
                        TechnicalRow {
                            label: "Tool / service"
                            value: root.hidhideAvailable ? "Available" : "Unavailable"
                            valueTone: root.hidhideAvailable ? "healthy" : "attention"
                        }
                        TechnicalRow {
                            label: "Cloaking"
                            value: root.hidhideCloakStateKnown ? (root.hidhideCloaked ? "Enabled" : "Disabled") : "Unknown"
                            valueTone: root.hidhideCloaked ? "healthy" : "informational"
                        }
                        TechnicalRow {
                            label: "App allow list"
                            value: root.hidhideMapperAllowed ? "HOTAS BF6 authorized" : "HOTAS BF6 not authorized"
                            valueTone: root.hidhideMapperAllowed ? "healthy" : "attention"
                        }
                    }
                }
            }

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsRouting"
                tokens: deck
                Layout.fillWidth: true
                visible: root.showSection("routing")
                implicitHeight: routingCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: routingCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: deck.space4
                            Text {
                                text: "ROUTING"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: root.routeProblems().length > 0 ? root.routeProblems().length + " route issue" + (root.routeProblems().length === 1 ? "" : "s") : "Routes available"
                                color: deck.statusColor(root.routeProblems().length > 0 ? "attention" : "healthy")
                                font.family: deck.displayFont
                                font.pixelSize: 18
                                font.bold: true
                            }
                        }
                        FlightDeckStatusChip {
                            tokens: deck
                            label: root.routeProblems().length > 0 ? "ATTENTION" : "ACTIVE"
                            tone: root.routeProblems().length > 0 ? "attention" : "healthy"
                            visible: root.medium
                        }
                    }
                    Text {
                        text: root.routeProblems().length > 0 ? root.routeStatus(root.routeProblems()[0]).detail : "Current effective axis routes are shown in the signal path and Live Inspection sections."
                        color: deck.textSecondary
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    OutlineButton {
                        objectName: "flightDeckDiagnosticsOpenAxes"
                        text: "OPEN AXES"
                        tone: root.routeProblems().length > 0 ? "attention" : "informational"
                        onClicked: root.navigateToPage(0)
                    }
                }
            }

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsProfiles"
                tokens: deck
                Layout.fillWidth: true
                visible: root.showSection("profiles")
                implicitHeight: profilesCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: profilesCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    Text {
                        text: "ACTIVE CONFIGURATION"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        font.bold: true
                    }
                    Text {
                        text: profileHealth.title || "No active profile"
                        color: deck.statusColor(profileHealth.tone || "informational")
                        font.family: deck.displayFont
                        font.pixelSize: 18
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "Category: " + (readinessState.activeCategoryName || backend.activeCategoryName || "Not available")
                        color: deck.textPrimary
                        font.family: deck.telemetryFont
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: profileHealth.detail || "Profile runtime source is not available."
                        color: deck.textSecondary
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    OutlineButton {
                        objectName: "flightDeckDiagnosticsOpenProfile"
                        text: "OPEN PROFILES"
                        onClicked: root.navigateToPage(5)
                    }
                }
            }

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsGame"
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: gameCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: gameCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    Text {
                        text: "GAME DETECTION"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        font.bold: true
                    }
                    Text {
                        text: gameHealth.title || "No supported game detected"
                        color: deck.statusColor(gameHealth.tone || "informational")
                        font.family: deck.displayFont
                        font.pixelSize: 18
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: gameHealth.detail || "No game process data is currently available."
                        color: deck.textSecondary
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: backend.automaticGameDetection ? "Automatic detection is enabled." : "Automatic detection is paused; manual profile selection remains available."
                        color: deck.textMuted
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsAutomation"
                tokens: deck
                Layout.fillWidth: true
                visible: root.showSection("automation")
                implicitHeight: automationCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: automationCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: deck.space4
                            Text {
                                text: "AUTOMATION"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: automationRuleCount === 0 ? "No rules configured" : automationRuleCount + " rules configured"
                                color: deck.statusColor(root.automationTone())
                                font.family: deck.displayFont
                                font.pixelSize: 18
                                font.bold: true
                            }
                        }
                        FlightDeckStatusChip {
                            tokens: deck
                            label: automationEngineEnabled ? "ENABLED" : "DISABLED"
                            value: automationActiveRuleCount + " ACTIVE"
                            tone: root.automationTone()
                            visible: root.medium
                        }
                    }
                    Text {
                        text: invalidAutomationCount() > 0 ? invalidAutomationCount() + " rule" + (invalidAutomationCount() === 1 ? " needs attention." : "s need attention.") : enabledAutomationCount() + " enabled  •  " + automationActiveRuleCount + " active"
                        color: deck.textSecondary
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: automationValidationMessage.length > 0
                        text: automationValidationMessage
                        color: deck.attention
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    OutlineButton {
                        objectName: "flightDeckDiagnosticsOpenAutomation"
                        text: "OPEN AUTOMATION"
                        tone: root.automationTone()
                        onClicked: root.navigateToPage(7)
                    }
                }
            }

            FlightDeckCard {
                objectName: "flightDeckDiagnosticsAdaptive"
                tokens: deck
                Layout.fillWidth: true
                implicitHeight: adaptiveCard.implicitHeight + deck.space32
                ColumnLayout {
                    id: adaptiveCard
                    anchors.fill: parent
                    anchors.margins: deck.space16
                    spacing: deck.space8
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: deck.space4
                            Text {
                                text: "ADAPTIVE RESPONSE"
                                color: deck.textMuted
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                font.bold: true
                            }
                            Text {
                                text: adaptive.enabled ? "Enabled" : "Off"
                                color: deck.statusColor(root.adaptiveTone())
                                font.family: deck.displayFont
                                font.pixelSize: 18
                                font.bold: true
                            }
                        }
                        FlightDeckStatusChip {
                            tokens: deck
                            label: String(adaptive.state || "STABLE").toUpperCase()
                            tone: root.adaptiveTone()
                            visible: root.medium
                        }
                    }
                    Text {
                        text: "Physical " + percent(adaptive.physical) + "  •  Output " + percent(adaptive.virtualOutput) + "  •  " + String(adaptive.model || "auto").toUpperCase()
                        color: deck.textSecondary
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: adaptive.safetyLimited ? "Safety limiting is currently active." : "Telemetry is read-only and follows the selected-axis snapshot."
                        color: adaptive.safetyLimited ? deck.attention : deck.textMuted
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    OutlineButton {
                        objectName: "flightDeckDiagnosticsOpenAdaptive"
                        text: "OPEN ADAPTIVE RESPONSE"
                        onClicked: root.navigateToPage(9)
                    }
                }
            }
        }

        Item {
            id: inspectionSection
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.showSection("input") || root.showSection("routing")
        }
        SectionLabel {
            label: "LIVE INSPECTION"
            visible: inspectionSection.visible
        }
        FlightDeckCard {
            objectName: "flightDeckDiagnosticsLiveInspection"
            tokens: deck
            Layout.fillWidth: true
            visible: inspectionSection.visible
            implicitHeight: inspectionContent.implicitHeight + deck.space32
            ColumnLayout {
                id: inspectionContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                Text {
                    text: physicalConnected ? "Selected-controller UI snapshot. Values update on the existing presentation cadence." : "No active controller snapshot. Stale live values are intentionally not presented as active input."
                    color: physicalConnected ? deck.textSecondary : deck.attention
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    text: "AXES"
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 9
                    font.bold: true
                }
                Repeater {
                    model: axes
                    delegate: FlightDeckCard {
                        required property var modelData
                        tokens: deck
                        Layout.fillWidth: true
                        visible: modelData.available || !physicalConnected
                        implicitHeight: axisRow.implicitHeight + deck.space16
                        color: deck.elevatedSurface
                        RowLayout {
                            id: axisRow
                            anchors.fill: parent
                            anchors.margins: deck.space8
                            spacing: deck.space12
                            Text {
                                text: modelData.label || "Axis"
                                color: deck.textPrimary
                                font.family: deck.telemetryFont
                                font.pixelSize: 10
                                font.bold: true
                                Layout.preferredWidth: root.medium ? 130 : 72
                                elide: Text.ElideRight
                            }
                            Text {
                                text: physicalConnected ? "RAW " + percent(modelData.raw) : "RAW —"
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: physicalConnected ? "NORMAL " + percent(modelData.calibrated) : "NORMAL —"
                                color: deck.textSecondary
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: physicalConnected && modelData.virtualValid ? "OUTPUT " + percent(modelData.virtualValue) : "OUTPUT —"
                                color: deck.statusColor(root.routeStatus(modelData).tone)
                                font.family: deck.telemetryFont
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            FlightDeckStatusChip {
                                tokens: deck
                                label: root.routeStatus(modelData).label.toUpperCase()
                                tone: root.routeStatus(modelData).tone
                                visible: root.medium
                            }
                        }
                    }
                }
                Text {
                    visible: buttons.length > 0
                    text: "BUTTONS"
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 9
                    font.bold: true
                }
                Flow {
                    visible: buttons.length > 0
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Repeater {
                        model: buttons
                        delegate: FlightDeckStatusChip {
                            required property var modelData
                            tokens: deck
                            label: modelData.label || ("B" + (modelData.index + 1))
                            value: modelData.pressed ? "PRESSED" : "RELEASED"
                            tone: modelData.pressed ? "informational" : "informational"
                        }
                    }
                }
                Text {
                    visible: povs.length > 0
                    text: "POV / HATS"
                    color: deck.textMuted
                    font.family: deck.telemetryFont
                    font.pixelSize: 9
                    font.bold: true
                }
                Flow {
                    visible: povs.length > 0
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Repeater {
                        model: povs
                        delegate: FlightDeckStatusChip {
                            required property var modelData
                            tokens: deck
                            label: "POV " + modelData.index
                            value: String(modelData.direction || "Centered").toUpperCase()
                            tone: modelData.centered ? "informational" : "healthy"
                        }
                    }
                }
            }
        }

        FlightDeckCard {
            objectName: "flightDeckDiagnosticsAdaptiveTelemetry"
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: telemetryContent.implicitHeight + deck.space32
            ColumnLayout {
                id: telemetryContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "ADAPTIVE TELEMETRY"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: "Selected-axis read-only snapshot"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: 17
                            font.bold: true
                        }
                    }
                    FlightDeckStatusChip {
                        tokens: deck
                        label: String(adaptive.state || "STABLE").toUpperCase()
                        value: adaptive.enabled ? "ON" : "OFF"
                        tone: root.adaptiveTone()
                        visible: root.medium
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.wide ? 4 : (root.medium ? 2 : 1)
                    columnSpacing: deck.space8
                    rowSpacing: deck.space8
                    MetricTile {
                        label: "PHYSICAL"
                        value: percent(adaptive.physical)
                        tone: "informational"
                    }
                    MetricTile {
                        label: "PREDICTED"
                        value: percent(adaptive.predicted)
                        tone: "informational"
                    }
                    MetricTile {
                        label: "OUTPUT"
                        value: percent(adaptive.virtualOutput)
                        tone: adaptive.safetyLimited ? "attention" : "healthy"
                    }
                    MetricTile {
                        label: "CONFIDENCE"
                        value: Math.round(Number(adaptive.confidence || 0) * 100) + "%"
                        tone: "informational"
                    }
                    MetricTile {
                        label: "VELOCITY"
                        value: metric(adaptive.velocity, "/s", 2)
                        tone: "informational"
                    }
                    MetricTile {
                        label: "ACCELERATION"
                        value: metric(adaptive.acceleration, "/s²", 2)
                        tone: "informational"
                    }
                    MetricTile {
                        label: "HORIZON"
                        value: metric(adaptive.activeHorizonMs, " ms", 1)
                        tone: "informational"
                    }
                    MetricTile {
                        label: "LEAD"
                        value: percent(adaptive.lead)
                        detail: adaptive.safetyLimited ? "Safety limited" : String(adaptive.state || "Stable")
                        tone: adaptive.safetyLimited ? "attention" : "informational"
                    }
                }
            }
        }

        Item {
            id: performanceSection
            Layout.fillWidth: true
            Layout.preferredHeight: 1
        }
        SectionLabel {
            label: "PERFORMANCE"
        }
        FlightDeckCard {
            objectName: "flightDeckDiagnosticsPerformance"
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: performanceContent.implicitHeight + deck.space32
            ColumnLayout {
                id: performanceContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                Text {
                    text: "Existing instrumentation only. These values are informational; the page does not define a separate performance-health threshold."
                    color: deck.textSecondary
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.wide ? 4 : (root.medium ? 2 : 1)
                    columnSpacing: deck.space8
                    rowSpacing: deck.space8
                    MetricTile {
                        label: "INPUT CADENCE"
                        value: metric(backend.inputReportsPerSecond, " Hz", 0)
                        detail: backend.lastPhysicalUpdateAgeMs >= 0 ? backend.lastPhysicalUpdateAgeMs + " ms since update" : "No report"
                    }
                    MetricTile {
                        label: "OUTPUT CADENCE"
                        value: metric(backend.vjoyWritesPerSecond, " /s", 0)
                        detail: "Change-driven vJoy writes"
                    }
                    MetricTile {
                        label: "MAP LATENCY"
                        value: backend.latencyCurrentUs + " µs"
                        detail: "Current sample"
                    }
                    MetricTile {
                        label: "MAP p95 / p99"
                        value: backend.latencyP95Us + " / " + backend.latencyP99Us + " µs"
                        detail: "Rolling 2,048 reports"
                    }
                    MetricTile {
                        label: "MAP AVG / PEAK"
                        value: backend.latencyAverageUs + " / " + backend.latencyPeakUs + " µs"
                        detail: "Since mapping start"
                    }
                    MetricTile {
                        label: "PROFILE SWAP"
                        value: backend.lastProfileSwapUs + " µs"
                        detail: backend.profileSwitchCount + " switches"
                    }
                    MetricTile {
                        label: "AUTOMATION EVAL"
                        value: backend.automationEvaluationUs + " µs"
                        detail: automationActiveRuleCount + " active rules"
                    }
                    MetricTile {
                        label: "CURVE COMPILE"
                        value: backend.lastCurveCompileUs + " µs"
                        detail: "Latest configuration compile"
                    }
                }
            }
        }

        Item {
            id: advancedSection
            Layout.fillWidth: true
            Layout.preferredHeight: 1
        }
        SectionLabel {
            label: "ADVANCED TECHNICAL DETAILS"
        }
        FlightDeckCard {
            objectName: "flightDeckDiagnosticsAdvanced"
            tokens: deck
            Layout.fillWidth: true
            implicitHeight: advancedContent.implicitHeight + deck.space32
            ColumnLayout {
                id: advancedContent
                anchors.fill: parent
                anchors.margins: deck.space16
                spacing: deck.space12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: deck.space4
                        Text {
                            text: "TECHNICAL DETAILS"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: 18
                            font.bold: true
                        }
                        Text {
                            text: "Identifiers, raw status, and existing support evidence stay subordinate to the health summary."
                            color: deck.textSecondary
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    OutlineButton {
                        objectName: "flightDeckDiagnosticsTechnicalToggle"
                        text: root.technicalDetailsExpanded ? "HIDE DETAILS" : "SHOW DETAILS"
                        onClicked: root.technicalDetailsExpanded = !root.technicalDetailsExpanded
                    }
                }
                ColumnLayout {
                    visible: root.technicalDetailsExpanded
                    Layout.fillWidth: true
                    spacing: deck.space8
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: deck.divider
                    }
                    TechnicalRow {
                        label: "Physical device"
                        value: root.deviceName || "Not available"
                    }
                    TechnicalRow {
                        label: "DirectInput ID"
                        value: root.deviceId || "Not available"
                    }
                    TechnicalRow {
                        label: "Mapping status"
                        value: root.mappingStatus || "Not available"
                        valueTone: root.mappingActive ? "healthy" : root.mappingRequested ? "attention" : "informational"
                    }
                    TechnicalRow {
                        label: "vJoy status"
                        value: root.vjoyStatus || "Not available"
                        valueTone: root.vjoySeverity === "error" ? "fault" : root.vjoySeverity === "warning" ? "attention" : "healthy"
                    }
                    TechnicalRow {
                        label: "Readiness status"
                        value: String(readinessState.controllerReadinessStatus || backend.controllerReadinessStatus || "Not available")
                    }
                    Text {
                        text: "CONNECTED CONTROLLERS"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: controllers
                        delegate: TechnicalRow {
                            required property var modelData
                            label: modelData.name || "Controller"
                            value: (modelData.state || "Unknown") + "  •  " + modelData.directInputId
                            valueTone: modelData.connected ? "informational" : "attention"
                        }
                    }
                    Text {
                        text: "ROUTE CONFIGURATION"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: axes
                        delegate: TechnicalRow {
                            required property var modelData
                            label: modelData.label || "Axis"
                            value: "Target " + (modelData.target || "Disabled") + "  •  " + (modelData.curveSummary || "Linear") + "  •  deadzone " + Number(modelData.deadzone || 0).toFixed(3)
                            valueTone: root.routeStatus(modelData).tone
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: deck.space8
                        OutlineButton {
                            visible: backend.controllerDiagnosticsAvailable
                            text: "COPY DIAGNOSTICS"
                            tone: "informational"
                            onClicked: backend.copyControllerDiagnostics()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Text {
                            text: backend.controllerDiagnosticsAvailable ? "Existing redacted support report" : "Copy Diagnostics is available when setup needs attention."
                            color: deck.textMuted
                            font.pixelSize: 9
                            horizontalAlignment: Text.AlignRight
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: deck.divider
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "EVENT LOG"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        OutlineButton {
                            text: root.eventLogExpanded ? "HIDE LOG" : "SHOW LOG"
                            onClicked: root.eventLogExpanded = !root.eventLogExpanded
                        }
                    }
                    ListView {
                        objectName: "flightDeckDiagnosticsEventLog"
                        visible: root.eventLogExpanded
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? Math.min(250, Math.max(80, contentHeight)) : 0
                        clip: true
                        model: events
                        spacing: deck.space4
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }
                        delegate: Text {
                            required property var modelData
                            required property int index
                            width: ListView.view ? ListView.view.width - deck.space8 : 0
                            text: modelData
                            color: ListView.view && index === ListView.view.count - 1 ? deck.textPrimary : deck.textSecondary
                            font.family: deck.telemetryFont
                            font.pixelSize: 9
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }
}
