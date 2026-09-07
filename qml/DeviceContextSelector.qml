import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Item {
    id: control
    objectName: "deviceContextSelector"
    required property var backendObject
    required property var theme
    property bool legacy: !!theme.legacy
    signal manageDevices()
    // At the supported compact shell width this control keeps its affordance
    // but yields label detail before it crowds profile or mapping state.
    implicitWidth: Math.max(174, contextLabel.implicitWidth + 58)
    implicitHeight: 34

    function healthColor(key) {
        if (key === "ready") return theme.ready
        if (key === "partial") return theme.warning
        if (key === "offline" || key === "disabled") return theme.textMuted
        return theme.danger
    }
    function currentRig() {
        const rigs = backendObject.deviceRigs || []
        for (let i = 0; i < rigs.length; ++i)
            if (rigs[i].id === backendObject.editingDeviceRigId) return rigs[i]
        return rigs.length > 0 ? rigs[0] : null
    }
    function connectedSummary() {
        const rig = currentRig()
        if (!rig) return "No saved rig"
        const members = rig.members || []
        let connected = 0
        for (let i = 0; i < members.length; ++i) if (members[i].connected) ++connected
        return connected + "/" + members.length + " connected"
    }
    function selectRig(rigId) { backendObject.setEditingDeviceContext(rigId, []) }
    function selectScope(ids) { backendObject.setEditingDeviceContext(backendObject.editingDeviceRigId, ids) }

    Rectangle {
        anchors.fill: parent; radius: theme.controlRadius
        color: trigger.containsMouse ? (control.legacy ? "#142128" : theme.controlHover) : theme.control
        border.color: popup.visible ? (control.legacy ? "#78aab9" : theme.orange)
                     : trigger.containsMouse ? (control.legacy ? "#527482" : theme.borderStrong)
                                           : (control.legacy ? "#435660" : theme.border)
        RowLayout { anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 8; spacing: 6
            Rectangle { width: 7; height: 7; radius: theme.topGun ? 1 : 4; color: control.healthColor((control.currentRig() || {}).health || "offline") }
            Text { id: contextLabel; Layout.fillWidth: true; text: backendObject.editingDeviceRigName + " / " + backendObject.editingScopeLabel; elide: Text.ElideRight; color: theme.textStrong; font.pixelSize: 10; font.bold: true; verticalAlignment: Text.AlignVCenter }
            Text { text: popup.visible ? "⌃" : "⌄"; color: theme.textMuted; font.pixelSize: 14 }
        }
    }
    MouseArea { id: trigger; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: popup.visible ? popup.close() : popup.open() }
    Popup {
        id: popup
        objectName: "deviceContextPopup"
        property bool choosingMultiple: false
        // The selector can live near the right edge of a compact header.  Keep
        // its custom popup on screen instead of letting a platform popup choose
        // an opaque fallback placement.
        x: control.parent ? Math.min(0, control.parent.width - control.x - width - 8) : 0
        y: control.height + 5; width: Math.max(330, control.width); padding: 10
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: DevicePanel { theme: control.theme; legacy: control.legacy; border.color: theme.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 6
            Text { text: "CURRENT RIG"; color: theme.textMuted; font.pixelSize: 9; font.bold: true }
            DevicePanel { Layout.fillWidth: true; implicitHeight: 48; theme: control.theme; legacy: control.legacy; border.color: control.legacy ? "#52717c" : theme.border
                Column { anchors.fill: parent; anchors.margins: 8; spacing: 2
                    Text { text: backendObject.editingDeviceRigName; color: theme.textStrong; font.pixelSize: 12; font.bold: true }
                    Text { text: ((control.currentRig() || {}).healthLabel || "Offline") + " · " + control.connectedSummary() + (backendObject.editingDeviceRigId === backendObject.activeDeviceRigId ? "   ACTIVE RUNTIME" : ""); color: theme.textMuted; font.pixelSize: 9 }
                }
            }
            Text { text: "VIEW / EDIT"; color: theme.textMuted; font.pixelSize: 9; font.bold: true; Layout.topMargin: 3 }
            ContextRow { label: "All Devices"; selected: backendObject.editingScopeLabel === "All Devices"; onTriggered: control.selectScope([]) }
            Repeater {
                model: backendObject.editingDevices
                delegate: ContextRow {
                    required property var modelData
                    label: modelData.name + (modelData.required ? "" : " · optional")
                    selected: !!modelData.selected && backendObject.editingScopeLabel !== "All Devices"
                    onTriggered: control.selectScope([modelData.id])
                }
            }
            ContextRow { label: popup.choosingMultiple ? "Done selecting" : "Select Multiple…"; selected: popup.choosingMultiple || backendObject.editingScopeLabel.indexOf("Devices") > 0
                onTriggered: popup.choosingMultiple = !popup.choosingMultiple }
            Repeater {
                model: backendObject.editingDevices
                delegate: ThemedCheckBox {
                    required property var modelData
                    visible: popup.choosingMultiple
                    Layout.fillWidth: true
                    theme: control.theme
                    text: modelData.name
                    checked: !!modelData.selected
                    onToggled: function(nowChecked) {
                        const ids = []
                        const devices = backendObject.editingDevices
                        for (let i = 0; i < devices.length; ++i)
                            if (devices[i].id === modelData.id ? nowChecked : devices[i].selected) ids.push(devices[i].id)
                        control.selectScope(ids)
                    }
                }
            }
            Text { visible: backendObject.deviceRigs.length > 1; text: "OTHER RIGS"; color: theme.textMuted; font.pixelSize: 9; font.bold: true; Layout.topMargin: 3 }
            Repeater {
                model: backendObject.deviceRigs
                delegate: ContextRow {
                    required property var modelData
                    visible: modelData.id !== backendObject.editingDeviceRigId
                    label: modelData.name
                    detail: modelData.healthLabel
                    status: modelData.health
                    onTriggered: control.selectRig(modelData.id)
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: theme.divider; Layout.topMargin: 3 }
            ThemedButton { theme: control.theme; Layout.fillWidth: true; text: "MANAGE DEVICES…"; tone: "secondary"; onTriggered: { popup.close(); control.manageDevices() } }
        }
    }
    component ContextRow: Item {
        property string label: ""
        property string detail: ""
        property string status: ""
        property bool selected: false
        signal triggered()
        Layout.fillWidth: true; implicitHeight: 30
        Rectangle { anchors.fill: parent; radius: theme.controlRadius; color: rowHit.containsMouse ? theme.selection : parent.selected ? theme.selectionCurrent : "transparent"; border.color: parent.selected ? theme.borderStrong : "transparent" }
        RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 7
            Text { text: selected ? "✓" : ""; color: theme.orange; font.bold: true; Layout.preferredWidth: 10 }
            Rectangle { visible: status !== ""; width: 6; height: 6; radius: theme.topGun ? 1 : 3; color: control.healthColor(status) }
            Text { Layout.fillWidth: true; text: label; color: theme.text; font.pixelSize: 10; elide: Text.ElideRight }
            Text { text: detail; color: theme.textMuted; font.pixelSize: 9 }
        }
        MouseArea { id: rowHit; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.triggered() }
    }
}
