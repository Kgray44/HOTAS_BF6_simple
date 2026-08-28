import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Presentation-only dashboard: it consumes the existing UI snapshot and never
// enters the DirectInput-to-vJoy report path.
Flickable {
    id: root
    property bool legacy: false
    anchors.fill: parent
    contentWidth: width
    contentHeight: dashboard.implicitHeight + 28
    clip: true
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    Theme { id: theme }
    readonly property color panelColor: legacy ? "#182126" : theme.panel
    readonly property color insetColor: legacy ? "#10191d" : theme.panelInset
    readonly property color borderColor: legacy ? "#49616b" : theme.border
    readonly property color textColor: legacy ? "#eef5f5" : theme.textStrong
    readonly property color mutedColor: legacy ? "#9fb1b5" : theme.textMuted
    readonly property color readyColor: legacy ? "#9fcbbf" : theme.ready
    readonly property color warningColor: legacy ? "#d6bd78" : theme.warning
    readonly property color dangerColor: legacy ? "#c98e97" : theme.danger
    readonly property color primaryColor: legacy ? "#8ec8d0" : theme.orangeBright
    readonly property bool narrow: width < 900

    function statusLabel() {
        if (!backend.physicalConnected) return "WAITING FOR INPUT"
        if (backend.mappingActive) return "MAPPING ACTIVE"
        return backend.mappingRequested ? "OUTPUT SUSPENDED" : "MAPPING STANDBY"
    }

    component Panel: Rectangle {
        default property alias content: panelBody.data
        property string eyebrow: "SYSTEM"
        property string title: ""
        property color accent: root.primaryColor
        implicitHeight: panelBody.implicitHeight + 32
        radius: root.legacy ? 3 : theme.panelRadius
        color: root.panelColor
        border.color: accent
        ColumnLayout {
            id: panelBody
            anchors.fill: parent; anchors.margins: 16; spacing: 10
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Rectangle { width: theme.topGun ? 12 : 7; height: theme.topGun ? 3 : 7; radius: theme.topGun ? 0 : 4; color: parent.parent.parent.accent }
                Text { text: parent.parent.parent.eyebrow; color: root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
                Item { Layout.fillWidth: true }
            }
            Text { visible: parent.parent.title.length > 0; text: parent.parent.title; color: root.textColor; font.pixelSize: theme.topGun ? 18 : 16; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
        }
    }

    component StatusBadge: Rectangle {
        property string label: "READY"
        property color tone: root.readyColor
        implicitWidth: badgeLabel.implicitWidth + 18; implicitHeight: 24
        radius: theme.topGun ? 1 : 12; color: Qt.rgba(tone.r, tone.g, tone.b, theme.topGun ? 0.16 : 0.12); border.color: tone
        Text { id: badgeLabel; anchors.centerIn: parent; text: parent.label; color: parent.tone; font.pixelSize: 9; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
    }

    component DashboardButton: Rectangle {
        property string label: "ACTION"
        property bool enabledAction: true
        signal triggered()
        implicitWidth: Math.max(126, buttonLabel.implicitWidth + 30); implicitHeight: 34
        radius: theme.topGun ? 1 : theme.controlRadius
        color: !enabledAction ? theme.controlDisabled : buttonMouse.containsMouse ? theme.buttonHover : theme.buttonSurface
        border.color: !enabledAction ? root.borderColor : theme.topGun ? theme.orange : root.primaryColor; opacity: enabledAction ? 1.0 : 0.5
        Text { id: buttonLabel; anchors.centerIn: parent; text: parent.label; color: root.textColor; font.pixelSize: 10; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
        Rectangle { visible: theme.topGun && parent.enabledAction; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3; width: 25; height: 2; color: theme.orangeBright }
        MouseArea { id: buttonMouse; anchors.fill: parent; enabled: parent.enabledAction; hoverEnabled: true; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: parent.triggered() }
    }

    component Capability: Rectangle {
        property string value: "0"
        property string label: "AXES"
        Layout.fillWidth: true; implicitHeight: 58
        radius: theme.topGun ? 1 : theme.controlRadius; color: root.insetColor; border.color: root.borderColor
        Column { anchors.centerIn: parent; spacing: 2
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: parent.parent.value; color: root.textColor; font.pixelSize: 18; font.bold: true; font.family: theme.telemetryFont }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: parent.parent.label; color: root.mutedColor; font.pixelSize: 8; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
        }
    }

    component Meter: Rectangle {
        property string label: "INPUT RATE"
        property string value: "0 Hz"
        property real fill: 0.0
        property color tone: root.primaryColor
        Layout.fillWidth: true; implicitHeight: 92
        radius: theme.topGun ? 1 : theme.controlRadius; color: root.insetColor; border.color: root.borderColor
        Column { anchors.fill: parent; anchors.margins: 11; spacing: 6
            Text { text: parent.parent.label; color: root.mutedColor; font.pixelSize: 8; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
            Text { text: parent.parent.value; color: root.textColor; font.pixelSize: 18; font.bold: true; font.family: theme.telemetryFont }
            Rectangle { width: parent.width; height: theme.topGun ? 5 : 6; radius: theme.topGun ? 0 : 3; color: root.panelColor
                Rectangle { width: Math.max(2, parent.width * Math.min(1.0, Math.max(0.0, parent.parent.parent.fill))); height: parent.height; radius: parent.radius; color: parent.parent.parent.tone }
            }
            Text { visible: theme.topGun; text: "LIVE SNAPSHOT"; color: root.mutedColor; font.pixelSize: 7; font.bold: true }
        }
    }

    ColumnLayout {
        id: dashboard
        x: 1; width: root.width - 14; spacing: 14
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { spacing: 3
                Text { text: theme.topGun ? "MISSION OVERVIEW" : "Overview"; color: root.textColor; font.pixelSize: root.legacy ? 27 : theme.topGun ? 24 : 26; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
                Text { text: theme.topGun ? "FLIGHT CONTROL SIGNAL PATH · LIVE SYSTEM STATUS" : "Controller signal path, output readiness, and human-readable mapper telemetry."; color: root.mutedColor; font.pixelSize: 11; font.family: theme.topGun ? theme.telemetryFont : undefined }
            }
            Item { Layout.fillWidth: true }
            StatusBadge { label: root.statusLabel(); tone: backend.mappingActive ? root.readyColor : backend.mappingRequested ? root.warningColor : root.mutedColor }
        }

        // This hero deliberately presents the control chain as a diagram, not a settings card.
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: root.narrow ? 250 : 190
            radius: root.legacy ? 3 : theme.panelRadius; color: theme.topGun ? "#0a181d" : root.panelColor; border.color: backend.mappingActive ? root.readyColor : root.borderColor; clip: true
            Rectangle { visible: theme.topGun; anchors.fill: parent; color: "transparent"; opacity: 0.28
                Repeater { model: 15; delegate: Rectangle { width: 88; height: 1; x: index * 96 - 64; y: parent.height - 18; rotation: -25; color: theme.orange } }
            }
            ColumnLayout { anchors.fill: parent; anchors.margins: root.narrow ? 16 : 20; spacing: 10
                RowLayout { Layout.fillWidth: true
                    Text { text: theme.topGun ? "CONTROL CHAIN" : "Control Signal"; color: root.mutedColor; font.pixelSize: 9; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
                    Item { Layout.fillWidth: true }
                    Text { text: backend.mappingStatus; color: backend.mappingActive ? root.readyColor : backend.mappingRequested ? root.warningColor : root.mutedColor; font.pixelSize: 10; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
                }
                GridLayout { Layout.fillWidth: true; Layout.fillHeight: true; columns: root.narrow ? 1 : 5; columnSpacing: root.narrow ? 5 : 10; rowSpacing: 5
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: root.narrow ? 54 : 78; radius: theme.topGun ? 1 : theme.controlRadius; color: root.insetColor; border.color: root.borderColor
                        Column { anchors.centerIn: parent; width: parent.width - 22; spacing: 4
                            Text { text: theme.topGun ? "PHYSICAL INPUT" : "Physical Controller"; color: root.mutedColor; font.pixelSize: 8; font.bold: true }
                            Text { width: parent.width; text: backend.physicalConnected ? backend.deviceName : "No controller connected"; color: root.textColor; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                            Text { text: backend.physicalConnected ? "DIRECTINPUT READY" : "CONNECT TO BEGIN"; color: backend.physicalConnected ? root.readyColor : root.warningColor; font.pixelSize: 8; font.bold: true }
                        }
                    }
                    Item { visible: !root.narrow; Layout.preferredWidth: 56; Layout.fillHeight: true
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 2; color: backend.mappingActive ? root.readyColor : root.borderColor }
                        Text { anchors.centerIn: parent; text: "››"; color: backend.mappingActive ? root.readyColor : root.mutedColor; font.pixelSize: 18; font.bold: true }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: root.narrow ? 54 : 78; radius: theme.topGun ? 1 : theme.controlRadius; color: theme.topGun ? "#18251f" : theme.panelRaised; border.color: backend.mappingActive ? root.readyColor : root.primaryColor
                        Column { anchors.centerIn: parent; spacing: 3
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "HOTAS BF6"; color: root.textColor; font.pixelSize: 15; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: backend.mappingActive ? "PROCESSING" : "STANDBY"; color: backend.mappingActive ? root.readyColor : root.warningColor; font.pixelSize: 8; font.bold: true }
                        }
                    }
                    Item { visible: !root.narrow; Layout.preferredWidth: 56; Layout.fillHeight: true
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 2; color: backend.mappingActive ? root.readyColor : root.borderColor }
                        Text { anchors.centerIn: parent; text: "››"; color: backend.mappingActive ? root.readyColor : root.mutedColor; font.pixelSize: 18; font.bold: true }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: root.narrow ? 54 : 78; radius: theme.topGun ? 1 : theme.controlRadius; color: root.insetColor; border.color: backend.vjoyReady ? root.readyColor : root.warningColor
                        Column { anchors.centerIn: parent; spacing: 4
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "VJOY " + backend.vjoyDeviceId; color: root.textColor; font.pixelSize: 15; font.bold: true; font.family: theme.telemetryFont }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: backend.vjoyReady ? "VIRTUAL OUTPUT READY" : "OUTPUT NEEDS ATTENTION"; color: backend.vjoyReady ? root.readyColor : root.warningColor; font.pixelSize: 8; font.bold: true }
                        }
                    }
                }
            }
        }

        GridLayout { Layout.fillWidth: true; columns: root.narrow ? 1 : 2; columnSpacing: 14; rowSpacing: 14
            Panel { Layout.fillWidth: true; eyebrow: "ACTIVE CONTROLLER"; title: backend.physicalConnected ? backend.deviceName : "Controller disconnected"; accent: backend.physicalConnected ? root.readyColor : root.warningColor
                RowLayout { Layout.fillWidth: true
                    StatusBadge { label: backend.physicalConnected ? "CONNECTED" : "OFFLINE"; tone: backend.physicalConnected ? root.readyColor : root.warningColor }
                    StatusBadge { label: backend.controllerReadinessState; tone: backend.controllerReadinessState === "READY" ? root.readyColor : root.warningColor }
                    Item { Layout.fillWidth: true }
                }
                Text { Layout.fillWidth: true; text: backend.physicalConnected ? "DIRECTINPUT  ·  " + backend.deviceId : "Connect or select a controller from Settings to begin verification."; color: root.mutedColor; font.pixelSize: 10; elide: Text.ElideRight; font.family: theme.telemetryFont }
                GridLayout { Layout.fillWidth: true; columns: 3; columnSpacing: 7
                    Capability { value: backend.axisCount; label: "AXES" }
                    Capability { value: backend.buttonCount; label: "BUTTONS" }
                    Capability { value: backend.povCount; label: "POVS" }
                }
            }
            Panel { Layout.fillWidth: true; eyebrow: "VIRTUAL OUTPUT"; title: "vJoy Device " + backend.vjoyDeviceId; accent: backend.vjoyReady ? root.readyColor : root.warningColor
                RowLayout { Layout.fillWidth: true
                    StatusBadge { label: backend.vjoyReady ? "READY" : "LIMITED"; tone: backend.vjoyReady ? root.readyColor : root.warningColor }
                    Item { Layout.fillWidth: true }
                    Text { text: backend.vjoyStatusSeverity.toUpperCase(); color: backend.vjoyReady ? root.readyColor : root.warningColor; font.pixelSize: 9; font.bold: true }
                }
                Text { Layout.fillWidth: true; text: backend.virtualAxisStatus; color: root.textColor; font.pixelSize: 10; font.family: theme.telemetryFont; elide: Text.ElideRight }
                RowLayout { Layout.fillWidth: true
                    Capability { value: backend.vjoyButtonCount; label: "BUTTON CAPACITY" }
                    Capability { value: backend.vjoyContinuousPovCount + backend.vjoyDiscretePovCount; label: "POV CAPACITY" }
                }
                Text { Layout.fillWidth: true; text: backend.vjoyReady ? "Output capability is available to the mapper." : backend.vjoyStatus; color: root.mutedColor; font.pixelSize: 9; wrapMode: Text.WordWrap }
            }
        }

        Panel { Layout.fillWidth: true; eyebrow: "SETUP HEALTH"; title: "System readiness"; accent: backend.controllerReadinessState === "READY" ? root.readyColor : root.warningColor
            GridLayout { Layout.fillWidth: true; columns: root.narrow ? 1 : 4; columnSpacing: 12; rowSpacing: 8
                Repeater { model: backend.controllerReadinessChecks
                    delegate: Rectangle { required property var modelData; Layout.fillWidth: true; implicitHeight: 44; radius: theme.topGun ? 1 : theme.controlRadius; color: root.insetColor; border.color: root.borderColor
                        RowLayout { anchors.fill: parent; anchors.margins: 9; spacing: 8
                            Rectangle { width: 8; height: 8; radius: theme.topGun ? 0 : 4; color: modelData.severity === "ready" ? root.readyColor : modelData.severity === "error" ? root.dangerColor : root.warningColor }
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { text: modelData.name; color: root.mutedColor; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.state; color: root.textColor; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                        }
                    }
                }
                DashboardButton { label: "VERIFY SETUP"; Layout.alignment: Qt.AlignVCenter; onTriggered: backend.verifyHotasSetup() }
            }
        }

        Panel { Layout.fillWidth: true; eyebrow: theme.topGun ? "MAPPER INSTRUMENTATION" : "Performance dashboard"; title: "Stable live telemetry"; accent: root.primaryColor
            Text { Layout.fillWidth: true; text: "Smoothed display values. Raw high-frequency instrumentation remains on Diagnostics."; color: root.mutedColor; font.pixelSize: 10 }
            GridLayout { Layout.fillWidth: true; columns: root.narrow ? 2 : 4; columnSpacing: 10; rowSpacing: 10
                Meter { label: "INPUT RATE"; value: Math.round(backend.overviewInputRate) + " Hz"; fill: Math.min(1, backend.overviewInputRate / 250); tone: root.primaryColor }
                Meter { label: "MAPPER LATENCY"; value: Math.round(backend.overviewMapperLatencyUs) + " µs"; fill: Math.min(1, backend.overviewMapperLatencyUs / 100); tone: root.readyColor }
                Meter { label: "OUTPUT RATE"; value: Math.round(backend.overviewOutputRate) + " Hz"; fill: Math.min(1, backend.overviewOutputRate / 250); tone: root.primaryColor }
                Meter { label: "MAPPING STATE"; value: backend.mappingActive ? "ACTIVE" : backend.mappingRequested ? "SUSPENDED" : "OFF"; fill: backend.mappingActive ? 1 : backend.mappingRequested ? 0.55 : 0.12; tone: backend.mappingActive ? root.readyColor : root.warningColor }
            }
        }

        Panel { Layout.fillWidth: true; eyebrow: "ACTIVE CONFIGURATION"; title: backend.activeProfileName; accent: root.primaryColor
            RowLayout { Layout.fillWidth: true; spacing: 22
                Column { spacing: 3
                    Text { text: "PROFILE"; color: root.mutedColor; font.pixelSize: 8; font.bold: true }
                    Text { text: backend.activeProfileName; color: root.textColor; font.pixelSize: 14; font.bold: true }
                }
                Column { spacing: 3
                    Text { text: "MAPPED AXES"; color: root.mutedColor; font.pixelSize: 8; font.bold: true }
                    Text { text: backend.axes.filter(function(axis) { return axis.target !== "Disabled" }).length; color: root.textColor; font.pixelSize: 14; font.bold: true; font.family: theme.telemetryFont }
                }
                Column { spacing: 3
                    Text { text: "MAPPED BUTTONS"; color: root.mutedColor; font.pixelSize: 8; font.bold: true }
                    Text { text: backend.buttons.filter(function(button) { return button.target > 0 }).length; color: root.textColor; font.pixelSize: 14; font.bold: true; font.family: theme.telemetryFont }
                }
                Column { spacing: 3
                    Text { text: "AUTOMATION"; color: root.mutedColor; font.pixelSize: 8; font.bold: true }
                    Text { text: backend.automationActiveRuleCount + " ACTIVE"; color: backend.automationActiveRuleCount > 0 ? root.readyColor : root.textColor; font.pixelSize: 14; font.bold: true; font.family: theme.telemetryFont }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
