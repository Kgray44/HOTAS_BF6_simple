import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// The specialist acquisition surface deliberately inherits Flight Deck
// tokens. Its denser geometry and Consolas readouts distinguish a service
// panel without introducing a second visual application.
Item {
    id: root
    objectName: "flightDeckUltraNerdPanel_" + Number(axis.index)
    required property var tokens
    property var axis: ({})
    property var telemetry: ({})
    property bool expanded: false
    property var sourceMonitor: []
    signal monitorVisibilityRequested(bool visible)
    signal manualOverrideRequested(int axisIndex)
    signal identifyRequested(int axisIndex)
    signal resetRequested(int axisIndex)

    implicitWidth: 420
    implicitHeight: panel.implicitHeight

    function formatted(value) {
        const number = Number(value)
        return isFinite(number) ? Math.round(number).toString() : "—"
    }

    component Readout: Rectangle {
        required property string label
        required property string value
        required property string tone
        Layout.fillWidth: true
        implicitHeight: 58
        radius: 4
        color: root.tokens.primarySurface
        border.width: 1
        border.color: tone === "active" ? root.tokens.accent : root.tokens.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root.tokens.space8
            spacing: 1
            Text {
                text: parent.parent.label
                color: root.tokens.textMuted
                font.family: root.tokens.telemetryFont
                font.pixelSize: root.tokens.tinyTechnical
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: parent.parent.value
                color: parent.parent.tone === "active" ? root.tokens.accent : root.tokens.textPrimary
                font.family: root.tokens.telemetryFont
                font.pixelSize: root.tokens.bodyStrong
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    component TechnicalLine: RowLayout {
        required property string label
        required property string value
        Layout.fillWidth: true
        spacing: root.tokens.space12
        Text {
            text: parent.label
            color: root.tokens.textMuted
            font.family: root.tokens.telemetryFont
            font.pixelSize: root.tokens.tinyTechnical
            font.bold: true
            Layout.preferredWidth: root.tokens.scale(142)
            elide: Text.ElideRight
        }
        Text {
            text: parent.value
            color: root.tokens.textPrimary
            font.family: root.tokens.telemetryFont
            font.pixelSize: root.tokens.bodySmall
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            wrapMode: Text.WrapAnywhere
        }
    }

    Rectangle {
        id: panel
        width: parent.width
        implicitHeight: panelContent.implicitHeight + root.tokens.space16 * 2
        radius: 9
        color: root.tokens.secondarySurface
        border.width: 1
        border.color: root.expanded ? root.tokens.accent : root.tokens.border

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 2
            color: root.tokens.accent
            radius: 1
        }

        ColumnLayout {
            id: panelContent
            anchors.fill: parent
            anchors.margins: root.tokens.space16
            spacing: root.tokens.space10

            Button {
                id: header
                Layout.fillWidth: true
                implicitHeight: root.tokens.compactControlHeight
                focusPolicy: Qt.StrongFocus
                Accessible.name: "Ultra Nerd raw input controls"
                onClicked: root.expanded = !root.expanded
                contentItem: RowLayout {
                    spacing: root.tokens.space8
                    Text {
                        text: "ULTRA NERD · RAW INPUT"
                        color: root.tokens.textPrimary
                        font.family: root.tokens.telemetryFont
                        font.pixelSize: root.tokens.caption
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        implicitWidth: badge.implicitWidth + root.tokens.space8
                        implicitHeight: 18
                        radius: 4
                        color: root.tokens.accentMuted
                        border.color: root.tokens.accent
                        Text {
                            id: badge
                            anchors.centerIn: parent
                            text: "ADVANCED"
                            color: root.tokens.accent
                            font.family: root.tokens.telemetryFont
                            font.pixelSize: root.tokens.tinyTechnical
                            font.bold: true
                        }
                    }
                    Text {
                        text: root.expanded ? "⌃" : "⌄"
                        color: root.tokens.textSecondary
                        font.family: root.tokens.telemetryFont
                        font.pixelSize: root.tokens.section
                    }
                }
                background: Rectangle {
                    radius: 5
                    color: header.down ? root.tokens.accentMuted
                        : header.hovered ? root.tokens.selected : "transparent"
                    border.color: "transparent"
                }
            }

            ColumnLayout {
                visible: root.expanded
                Layout.fillWidth: true
                spacing: root.tokens.space10

                Text {
                    visible: backend.axisAcquisitionPreview
                    text: "SIMULATED CONTROLLER PREVIEW · NO PHYSICAL INPUT OR VJOY OUTPUT"
                    color: root.tokens.attention
                    font.family: root.tokens.telemetryFont
                    font.pixelSize: root.tokens.bodySmall
                    font.bold: true
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                TechnicalLine { label: "RESOLUTION"; value: String(root.axis.manualOverride ? "MANUAL OVERRIDE · " : "AUTOMATIC · ") + String(root.axis.resolutionConfidence || "Low") + " CONFIDENCE" }
                TechnicalLine { label: "CANONICAL AXIS"; value: String(root.axis.canonicalAxis || root.axis.key || "Unknown") }
                TechnicalLine { label: "NATIVE NAME"; value: String(root.axis.nativeObjectName || "Not recorded") }
                TechnicalLine { label: "SEMANTIC GUID"; value: String(root.axis.directInputGuid || "Not recorded") }
                TechnicalLine { label: "REPORTED OFFSET"; value: "0x" + Number(root.axis.directInputOffset || 0).toString(16).toUpperCase() }
                TechnicalLine { label: "RUNTIME SOURCE"; value: "DIJOYSTATE2." + String(root.axis.formattedSource || "Not resolved") }
                TechnicalLine {
                    label: "METADATA"
                    value: Boolean(root.axis.metadataContradiction)
                        ? "Offset conflicts with semantic identity · semantic GUID remains authoritative"
                        : "Semantic identity and state metadata agree"
                }
                Text {
                    visible: Boolean(root.axis.metadataContradiction)
                    text: "CONTRADICTION · reported offset is retained as evidence, not used as the runtime source"
                    color: root.tokens.attention
                    font.family: root.tokens.telemetryFont
                    font.pixelSize: root.tokens.bodySmall
                    font.bold: true
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: Boolean(root.axis.manualOverride)
                    text: "MANUAL OVERRIDE · Automatic acquisition is bypassed for this axis."
                    color: root.tokens.attention
                    font.family: root.tokens.telemetryFont
                    font.pixelSize: root.tokens.bodySmall
                    font.bold: true
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                TechnicalLine { label: "RAW RANGE"; value: root.formatted(root.axis.nativeRangeMinimum) + " to " + root.formatted(root.axis.nativeRangeMaximum) }
                TechnicalLine {
                    label: "OBSERVED RANGE"
                    value: Boolean(root.axis.observedRangeAvailable)
                        ? root.formatted(root.axis.observedMinimum) + " to " + root.formatted(root.axis.observedMaximum)
                        : "Open source monitor to capture"
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width < root.tokens.scale(460) ? 2 : 4
                    columnSpacing: root.tokens.space6
                    rowSpacing: root.tokens.space6
                    Readout { label: "RAW VALUE"; value: root.formatted(root.axis.rawValue); tone: "active" }
                    Readout { label: "NORMALIZED"; value: Number(root.telemetry.calibrated || 0).toFixed(3); tone: "normal" }
                    Readout {
                        label: "LIVE STATE"
                        value: Boolean(root.axis.liveMovementObserved) ? "LIVE" : "WAITING"
                        tone: Boolean(root.axis.liveMovementObserved) ? "active" : "normal"
                    }
                    Readout {
                        label: "LAST MOVEMENT"
                        value: Number(root.axis.lastMovementAgeMs) >= 0
                            ? Math.round(Number(root.axis.lastMovementAgeMs)) + " ms" : "—"
                        tone: "normal"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: monitorContent.implicitHeight + root.tokens.space12 * 2
                    radius: 6
                    color: root.tokens.primarySurface
                    border.color: root.tokens.border
                    ColumnLayout {
                        id: monitorContent
                        anchors.fill: parent
                        anchors.margins: root.tokens.space12
                        spacing: root.tokens.space6
                        Text {
                            text: "LIVE CANDIDATE SOURCE MONITOR"
                            color: root.tokens.textSecondary
                            font.family: root.tokens.telemetryFont
                            font.pixelSize: root.tokens.caption
                            font.bold: true
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: root.tokens.space8
                            rowSpacing: root.tokens.space4
                            Repeater {
                                model: ["SOURCE", "VALUE", "CHANGES", "STATE"]
                                delegate: Text {
                                    required property string modelData
                                    text: modelData
                                    color: root.tokens.textMuted
                                    font.family: root.tokens.telemetryFont
                                    font.pixelSize: root.tokens.tinyTechnical
                                    font.bold: true
                                    Layout.fillWidth: true
                                }
                            }
                            Repeater {
                                model: root.sourceMonitor
                                delegate: Item {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.columnSpan: 4
                                    implicitHeight: monitorRow.implicitHeight
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 4
                                        color: String(modelData.state) === "ACTIVE" ? root.tokens.accentMuted : "transparent"
                                        border.width: String(modelData.state) === "ACTIVE" ? 1 : 0
                                        border.color: root.tokens.accent
                                    }
                                    ColumnLayout {
                                        id: monitorRow
                                        anchors.fill: parent
                                        anchors.margins: root.tokens.space4
                                        spacing: 1
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: root.tokens.space8
                                            Text { text: String(modelData.label || "?"); color: root.tokens.textPrimary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; Layout.fillWidth: true }
                                            Text { text: root.formatted(modelData.value); color: root.tokens.textPrimary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; Layout.preferredWidth: root.tokens.scale(88); horizontalAlignment: Text.AlignRight }
                                            Text { text: root.formatted(modelData.changeCount); color: root.tokens.textSecondary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; Layout.preferredWidth: root.tokens.scale(88); horizontalAlignment: Text.AlignRight }
                                            Text { text: String(modelData.state || "UNAVAILABLE"); color: String(modelData.state) === "ACTIVE" ? root.tokens.accent : root.tokens.textMuted; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.bodySmall; Layout.preferredWidth: root.tokens.scale(78); horizontalAlignment: Text.AlignRight }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: Boolean(modelData.available)
                                                ? "range " + root.formatted(modelData.observedMinimum) + "–" + root.formatted(modelData.observedMaximum)
                                                    + "  ·  Δ " + root.formatted(modelData.recentMovementMagnitude)
                                                    + "  ·  last " + root.formatted(modelData.lastChangeAgeMs) + " ms"
                                                : "Monitor unavailable until this verified controller is live."
                                            color: root.tokens.textMuted
                                            font.family: root.tokens.telemetryFont
                                            font.pixelSize: root.tokens.tinyTechnical
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    text: "Raw acquisition is applied before calibration, deadzone, curves, Adaptive Response, and vJoy routing. It does not alter those settings. Raw HID acquisition is not available in this build."
                    color: root.tokens.textMuted
                    font.pixelSize: root.tokens.bodySmall
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.tokens.space8
                    Button {
                        text: "IDENTIFY AXIS"
                        enabled: Boolean(root.axis.liveAvailable)
                        onClicked: root.identifyRequested(Number(root.axis.index))
                        contentItem: Text { text: parent.text; color: parent.enabled ? root.tokens.textPrimary : root.tokens.disabled; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.caption; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 5; color: parent.down ? root.tokens.accentMuted : parent.hovered ? root.tokens.selected : root.tokens.secondarySurface; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border; border.width: parent.activeFocus ? 2 : 1 }
                    }
                    Button {
                        text: "MANUAL OVERRIDE"
                        enabled: Boolean(root.axis.axisDiscovered)
                        onClicked: root.manualOverrideRequested(Number(root.axis.index))
                        contentItem: Text { text: parent.text; color: parent.enabled ? root.tokens.textPrimary : root.tokens.disabled; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.caption; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 5; color: parent.down ? root.tokens.accentMuted : parent.hovered ? root.tokens.selected : root.tokens.secondarySurface; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border; border.width: parent.activeFocus ? 2 : 1 }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "RESET TO AUTOMATIC"
                        visible: Boolean(root.axis.manualOverride)
                        onClicked: root.resetRequested(Number(root.axis.index))
                        contentItem: Text { text: parent.text; color: root.tokens.textSecondary; font.family: root.tokens.telemetryFont; font.pixelSize: root.tokens.caption; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 5; color: parent.down ? root.tokens.accentMuted : parent.hovered ? root.tokens.selected : "transparent"; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border; border.width: parent.activeFocus ? 2 : 1 }
                    }
                }
            }
        }
    }

    onExpandedChanged: monitorVisibilityRequested(expanded)
    onVisibleChanged: monitorVisibilityRequested(visible && expanded)
    Component.onDestruction: monitorVisibilityRequested(false)
}
