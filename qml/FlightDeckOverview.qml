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
            ? "Shared setup truth for " + String(setupTruth.rigName || "the selected Device Rig") + "."
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
                    font.pixelSize: 12
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
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: setupTruth.overallStatus || "CHECKING"
                            color: deck.statusColor(root.setupTone(setupTruth.overallStatus || "CHECKING"))
                            font.family: deck.displayFont
                            font.pixelSize: root.wide ? 22 : 18
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
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: input.title || "Checking"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: 15
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: input.detail || ""
                            color: deck.textSecondary
                            font.pixelSize: 10
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
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: profile.title || "Checking"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: 15
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: profile.detail || ""
                            color: deck.textSecondary
                            font.pixelSize: 10
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
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            text: output.title || "Checking"
                            color: deck.textPrimary
                            font.family: deck.displayFont
                            font.pixelSize: 15
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: output.detail || ""
                            color: deck.textSecondary
                            font.pixelSize: 10
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
                    Layout.fillWidth: true
                    spacing: deck.space12
                    Text {
                        text: "GAME"
                        color: deck.textMuted
                        font.family: deck.telemetryFont
                        font.pixelSize: 9
                        font.bold: true
                    }
                    Text {
                        text: game.title || "Checking"
                        color: deck.textPrimary
                        font.family: deck.displayFont
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: game.detail || ""
                        color: deck.textMuted
                        font.pixelSize: 9
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        visible: root.wide
                    }
                }
            }
        }

        Text {
            text: "SYSTEM HEALTH"
            color: deck.textMuted
            font.family: deck.telemetryFont
            font.pixelSize: 10
            font.bold: true
            Layout.fillWidth: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.wide ? 3 : 1
            columnSpacing: deck.space12
            rowSpacing: deck.space12

            FlightDeckHealthCard {
                objectName: "flightDeckHealthInput"
                tokens: deck
                eyebrow: "PHYSICAL INPUT"
                title: setupPhysical.title || "Checking"
                detail: setupPhysical.detail || ""
                tone: root.setupTone(setupPhysical)
                actionLabel: "OPEN SETUP"
                onActionRequested: root.navigateToDevices("controllers")
            }
            FlightDeckHealthCard {
                objectName: "flightDeckHealthOutput"
                tokens: deck
                eyebrow: "VIRTUAL OUTPUT"
                title: setupOutput.title || "Checking"
                detail: setupOutput.detail || ""
                tone: root.setupTone(setupOutput)
                actionLabel: "OPEN VIRTUAL OUTPUT"
                onActionRequested: root.navigateToDevices("virtual-output")
            }
            FlightDeckHealthCard {
                objectName: "flightDeckHealthIsolation"
                tokens: deck
                eyebrow: "HIDHIDE ISOLATION"
                title: setupIsolation.title || "Checking"
                detail: setupIsolation.detail || ""
                tone: root.setupTone(setupIsolation)
                actionLabel: "OPEN ISOLATION"
                onActionRequested: root.navigateToDevices("isolation")
            }
            FlightDeckHealthCard {
                objectName: "flightDeckHealthGame"
                tokens: deck
                eyebrow: "GAME DETECTION"
                title: game.title || "Checking"
                detail: game.detail || ""
                tone: game.tone || "informational"
                actionLabel: "OPEN PROFILES"
                onActionRequested: root.navigateToPage(5)
            }
            FlightDeckHealthCard {
                objectName: "flightDeckHealthActivationResolver"
                tokens: deck
                eyebrow: "AUTOMATIC ACTIVATION"
                title: activation.profileName ? activation.profileName + " · " + (activation.deviceRigName || "Device Rig") : "No automatic configuration"
                detail: activation.explanation || "Checking the current Game / Application, profile, Device Rig, and virtual output."
                tone: activation.valid ? "healthy" : "attention"
                actionLabel: activation.manualOverride ? "RESUME AUTOMATIC" : "OPEN PROFILES"
                onActionRequested: {
                    if (activation.manualOverride)
                        backend.resumeAutomaticActivation()
                    else
                        root.navigateToPage(5)
                }
            }
            FlightDeckHealthCard {
                objectName: "flightDeckHealthProfile"
                tokens: deck
                eyebrow: "PROFILE ROUTING"
                title: profile.title || "Checking"
                detail: profile.detail || ""
                tone: profile.tone || "informational"
                actionLabel: "OPEN PROFILES"
                onActionRequested: root.navigateToPage(5)
            }
            FlightDeckHealthCard {
                objectName: "flightDeckHealthMapper"
                tokens: deck
                eyebrow: "MAPPER"
                title: readinessState.mappingStatus || backend.mappingStatus
                detail: readinessState.mappingActive ? "Virtual output is receiving mapped controls." : "Use the established mapper controls when you are ready to run output."
                tone: readiness.tone || "informational"
                actionLabel: "OPEN DIAGNOSTICS"
                onActionRequested: root.navigateToPage(3)
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
                            text: "ACTIVE CONTROLS"
                            color: deck.textMuted
                            font.family: deck.telemetryFont
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Text {
                            text: "Live values use the existing bounded UI snapshot."
                            color: deck.textSecondary
                            font.pixelSize: 10
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
                            font.pixelSize: 9
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
                    }
                }
                Text {
                    visible: !backend.physicalConnected
                    text: "Connect a controller to see live control activity."
                    color: deck.textSecondary
                    font.pixelSize: 10
                    Layout.fillWidth: true
                }
            }
        }
    }
}
