import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Flickable {
    id: root
    property bool legacy: false
    anchors.fill: parent
    contentWidth: width
    contentHeight: content.implicitHeight + 24
    clip: true
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Theme { id: theme }

    component Card: Rectangle {
        default property alias content: body.data
        property string title: ""
        property string detail: ""
        implicitHeight: body.implicitHeight + 34
        color: root.legacy ? "#1a2024" : theme.panel
        border.color: root.legacy ? "#465964" : theme.border
        radius: root.legacy ? 4 : theme.panelRadius
        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
            Text { text: parent.parent.title; color: root.legacy ? "#f0f4f5" : theme.textStrong; font.pixelSize: 12; font.bold: true }
            Text { visible: parent.parent.detail.length > 0; text: parent.parent.detail; color: root.legacy ? "#9dafb4" : theme.textMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }

    ColumnLayout {
        id: content
        x: 1
        width: root.width - 14
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 3
                Text { text: "Overview"; color: root.legacy ? "#f0f4f5" : theme.textStrong; font.pixelSize: root.legacy ? 28 : 26; font.bold: true }
                Text { text: "Controller, virtual output, setup health, and mapper performance at a glance."; color: root.legacy ? "#9dafb4" : theme.textMuted; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Button { text: backend.mappingRequested ? "Stop Mapping" : "Start Mapping"; onClicked: backend.toggleMapping() }
        }
        Card {
            Layout.fillWidth: true
            title: backend.physicalConnected ? backend.deviceName : "No Physical Controller"
            detail: backend.physicalConnected
                ? (backend.mappingActive ? "Connected · Mapping Active" : backend.mappingRequested ? "Connected · Mapping Suspended" : "Connected · Mapping Off")
                : "Connect a DirectInput controller to begin setup."
            RowLayout {
                Layout.fillWidth: true
                Text { text: "PROFILE  ·  " + backend.activeProfileName; color: root.legacy ? "#cbd7d9" : theme.ivory; font.pixelSize: 11; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: backend.controllerReadinessState; color: backend.controllerReadinessState === "READY" ? theme.ready : theme.warning; font.pixelSize: 10; font.bold: true }
                Button { text: "Verify Setup"; onClicked: backend.verifyHotasSetup() }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Card {
                Layout.fillWidth: true
                title: "Physical Input"
                detail: backend.physicalConnected ? "DirectInput · " + backend.axisCount + " axes · " + backend.buttonCount + " buttons · " + backend.povCount + " POV" : "No physical DirectInput device detected"
                Text { text: backend.physicalConnected ? "Verified device status is shown in Input Controllers." : "HidHide remains observable even without a controller."; color: root.legacy ? "#9dafb4" : theme.textMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            Card {
                Layout.fillWidth: true
                title: "Virtual Output"
                detail: backend.vjoyReady ? "vJoy Device " + backend.vjoyDeviceId + " · Ready" : backend.vjoyStatus
                Text { text: backend.virtualAxisStatus + " · " + backend.vjoyButtonCount + " buttons · " + backend.vjoyContinuousPovCount + " POV"; color: root.legacy ? "#9dafb4" : theme.textMuted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
        }
        Card {
            Layout.fillWidth: true
            title: "Setup Health"
            detail: backend.controllerReadinessStatus
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Physical Input"; color: root.legacy ? "#cbd7d9" : theme.ivory; font.pixelSize: 10; Layout.fillWidth: true }
                Text { text: backend.physicalConnected ? "CONNECTED" : "WAITING"; color: backend.physicalConnected ? theme.ready : theme.warning; font.pixelSize: 10; font.bold: true }
                Text { text: "HidHide"; color: root.legacy ? "#cbd7d9" : theme.ivory; font.pixelSize: 10; Layout.fillWidth: true }
                Text { text: backend.hidhideMapperAllowed ? "ACCESS OK" : backend.hidhideAvailable ? "CHECK REQUIRED" : "OPTIONAL"; color: backend.hidhideMapperAllowed ? theme.ready : theme.warning; font.pixelSize: 10; font.bold: true }
                Button { visible: backend.hidhideAvailable && !backend.hidhideMapperAllowed; text: "Repair Access"; onClicked: backend.repairHidHideAccess() }
            }
        }
        Card {
            Layout.fillWidth: true
            title: "Performance"
            detail: "Smoothed presentation metrics; Diagnostics retains its fast raw instrumentation."
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [ {label: "Input Rate", value: Math.round(backend.overviewInputRate) + " Hz"},
                             {label: "Mapper Latency", value: Math.round(backend.overviewMapperLatencyUs) + " µs"},
                             {label: "Output Rate", value: Math.round(backend.overviewOutputRate) + " Hz"},
                             {label: "Mapping", value: backend.mappingStatus} ]
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: modelData.label.toUpperCase(); color: root.legacy ? "#8d9ba0" : theme.textMuted; font.pixelSize: 9; font.bold: true }
                        Text { text: modelData.value; color: root.legacy ? "#e5eeee" : theme.ivory; font.pixelSize: 16; font.bold: true; font.family: "Consolas" }
                    }
                }
            }
        }
        Card {
            Layout.fillWidth: true
            title: "Active Configuration"
            detail: backend.activeProfileName + " · " + backend.axes.filter(function(axis) { return axis.target !== "Disabled" }).length + " mapped axes · " + backend.buttons.filter(function(button) { return button.target > 0 }).length + " mapped buttons · " + backend.automationActiveRuleCount + " active automations"
        }
    }
}
