import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// One canonical device-context selector for the Flight Deck shell.  It uses
// AppBackend's selected-device aliases; there is no page-local device model.
Button {
    id: control
    required property var backendObject
    required property var tokens
    property bool compact: false
    signal manageDevices()

    objectName: "flightDeckSelectedDeviceSelector"
    // The controller selector is the input editor's primary context.  A
    // narrow header may elide its value, but must never collapse it into an
    // unlabeled dot-and-chevron control.
    implicitWidth: compact ? Math.max(156, selectorRow.implicitWidth + tokens.space16)
        : Math.max(216, selectorRow.implicitWidth + tokens.space20)
    implicitHeight: 38
    focusPolicy: Qt.StrongFocus
    Accessible.name: "Selected Device: " + backendObject.selectedDeviceLabel

    contentItem: RowLayout {
        id: selectorRow
        spacing: tokens.space8
        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: control.currentRig().health === "ready" ? tokens.healthy : tokens.attention
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                text: control.compact ? "CONTROLLER" : "SELECTED DEVICE"
                color: tokens.textMuted
                font.family: tokens.telemetryFont
                font.pixelSize: tokens.scale(7)
                font.bold: true
            }
            Text {
                text: backendObject.selectedDeviceLabel
                color: tokens.textPrimary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.scale(10)
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
        Text {
            text: popup.visible ? "⌃" : "⌄"
            color: tokens.textSecondary
            font.pixelSize: tokens.scale(14)
        }
    }
    background: Rectangle {
        radius: tokens.radiusControl
        color: control.down ? tokens.accentMuted : control.hovered ? tokens.selected : tokens.secondarySurface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? tokens.focus : control.hovered ? tokens.accent : tokens.border
    }

    function currentRig() {
        const rigs = backendObject.deviceRigs || []
        for (let index = 0; index < rigs.length; ++index) {
            if (String(rigs[index].id || "") === String(backendObject.selectedDeviceRigId || ""))
                return rigs[index]
        }
        return ({})
    }
    function selectDevice(deviceId) {
        backendObject.setSelectedDeviceContext(backendObject.selectedDeviceRigId, [String(deviceId)])
        popup.close()
    }
    function openPicker() {
        if (!popup.visible) popup.open()
        forceActiveFocus()
    }
    onClicked: popup.visible ? popup.close() : popup.open()

    Popup {
        id: popup
        objectName: "flightDeckSelectedDevicePopup"
        x: Math.min(0, control.parent ? control.parent.width - control.x - width : 0)
        y: control.height + tokens.space8
        width: Math.max(286, control.width)
        padding: tokens.space12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            radius: tokens.radiusCard
            color: tokens.elevatedSurface
            border.color: tokens.border
        }
        contentItem: ColumnLayout {
            width: parent.width
            spacing: tokens.space8
            Text {
                text: "SELECTED DEVICE"
                color: tokens.textMuted
                font.family: tokens.telemetryFont
                font.pixelSize: tokens.scale(8)
                font.bold: true
            }
            Text {
                text: backendObject.selectedDeviceRigName
                color: tokens.textPrimary
                font.family: tokens.bodyFont
                font.pixelSize: tokens.scale(13)
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: "Choose the saved controller whose mappings, curves, and diagnostics you want to view or configure. Offline devices remain available."
                color: tokens.textSecondary
                font.pixelSize: tokens.scale(9)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: backendObject.selectedDevices
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34
                    checkable: true
                    checked: !!modelData.selected
                    focusPolicy: Qt.StrongFocus
                    contentItem: RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: tokens.space12
                        anchors.rightMargin: tokens.space12
                        Text { text: parent.parent.checked ? "✓" : ""; color: tokens.accent; font.bold: true; Layout.preferredWidth: 12 }
                        Text { text: String(parent.parent.modelData.name || "Saved device"); color: tokens.textPrimary; font.pixelSize: tokens.scale(10); Layout.fillWidth: true; elide: Text.ElideRight }
                        Text { text: parent.parent.modelData.required ? "REQUIRED" : "OPTIONAL"; color: tokens.textMuted; font.family: tokens.telemetryFont; font.pixelSize: tokens.scale(7); font.bold: true }
                    }
                    background: Rectangle {
                        radius: tokens.radiusControl
                        color: parent.checked ? tokens.selected : parent.hovered ? tokens.secondarySurface : "transparent"
                        border.color: parent.checked ? tokens.accent : tokens.border
                    }
                    onClicked: control.selectDevice(modelData.id)
                }
            }
            Button {
                Layout.fillWidth: true
                implicitHeight: 32
                text: "DEVICES & SETUP"
                focusPolicy: Qt.StrongFocus
                contentItem: Text { text: parent.text; color: tokens.textSecondary; font.family: tokens.telemetryFont; font.pixelSize: tokens.scale(8); font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: tokens.radiusControl; color: parent.down ? tokens.accentMuted : parent.hovered ? tokens.selected : tokens.secondarySurface; border.color: tokens.border }
                onClicked: { popup.close(); control.manageDevices() }
            }
        }
    }
}
