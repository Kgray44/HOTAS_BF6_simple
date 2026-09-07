import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Page {
    id: root
    padding: 0
    property var backendObject
    property var themeTokens
    property bool legacy: false
    property string selectedRigId: backendObject && backendObject.editingDeviceRigId !== ""
                                   ? backendObject.editingDeviceRigId : ""
    property string selectedDeviceId: ""
    property string selectedOutputId: ""

    readonly property var rigs: backendObject ? backendObject.deviceRigs : []
    readonly property var controllers: backendObject ? backendObject.controllers : []

    function rigFor(id) {
        for (let i = 0; i < rigs.length; ++i) if (rigs[i].id === id) return rigs[i]
        return rigs.length > 0 ? rigs[0] : null
    }
    readonly property var selectedRig: rigFor(selectedRigId)
    function healthColor(key) {
        if (key === "ready") return themeTokens.ready
        if (key === "partial") return themeTokens.warning
        if (key === "disabled" || key === "offline") return themeTokens.textMuted
        return themeTokens.danger
    }
    function pickRig(id) {
        selectedRigId = id
        if (backendObject) backendObject.setEditingDeviceContext(id, [])
    }
    function openDevice(id) { selectedDeviceId = id; physicalDeviceDialog.open() }
    function openOutput(id) { selectedOutputId = id; outputDetailDialog.open() }
    function rigHasMember(rig, id) {
        if (!rig) return false
        for (let i = 0; i < rig.members.length; ++i) if (rig.members[i].id === id) return true
        return false
    }
    function selectedEditingCount() {
        if (!backendObject) return 0
        const entries = backendObject.editingDevices
        let count = 0
        for (let i = 0; i < entries.length; ++i) if (entries[i].selected) ++count
        return count
    }

    component Panel: AviationPanel { theme: root.themeTokens }
    component SmallLabel: Text {
        color: themeTokens.textMuted; font.pixelSize: 10; font.bold: true
        font.family: themeTokens.topGun ? themeTokens.displayFont : root.font.family
    }

    background: Rectangle { color: themeTokens.background }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 3
                ColumnLayout {
                    spacing: 3
                    Text {
                        text: themeTokens.topGun ? "DEVICES  //  FLIGHT RIGS" : "Devices"
                        color: themeTokens.textStrong; font.pixelSize: themeTokens.topGun ? 38 : 28
                        font.bold: true; font.family: themeTokens.topGun ? themeTokens.displayFont : root.font.family
                    }
                    Text {
                        text: "Group physical controls into reusable rigs, then route them to clean virtual outputs."
                        color: themeTokens.textMuted; font.pixelSize: 13
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "+  CREATE RIG"; highlighted: true
                    onClicked: createRigDialog.open()
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: migrationWarning.visible ? 76 : 0
                visible: backendObject && backendObject.deviceRigMigrationWarning.length > 0
                color: Qt.rgba(themeTokens.warning.r, themeTokens.warning.g, themeTokens.warning.b, 0.10)
                border.color: themeTokens.warning
                Text {
                    id: migrationWarning
                    anchors.fill: parent; anchors.margins: 16
                    text: backendObject ? backendObject.deviceRigMigrationWarning : ""
                    color: themeTokens.text; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: rigDetectionWarning.visible ? 58 : 0
                visible: backendObject && backendObject.deviceRigDetectionMessage.length > 0
                color: Qt.rgba(themeTokens.warning.r, themeTokens.warning.g, themeTokens.warning.b, 0.10)
                border.color: themeTokens.warning
                Text {
                    id: rigDetectionWarning
                    anchors.fill: parent; anchors.margins: 16
                    text: backendObject ? backendObject.deviceRigDetectionMessage : ""
                    color: themeTokens.text; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter
                }
            }

            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: activeRigColumn.implicitHeight + 32
                color: selectedRig ? Qt.rgba(healthColor(selectedRig.health).r, healthColor(selectedRig.health).g,
                                             healthColor(selectedRig.health).b, 0.08) : themeTokens.panel
                border.color: selectedRig ? healthColor(selectedRig.health) : themeTokens.border
                ColumnLayout {
                    id: activeRigColumn
                    anchors.fill: parent; anchors.margins: 16
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            spacing: 2
                            SmallLabel { text: "ACTIVE DEVICE RIG" }
                            Text {
                                text: backendObject ? backendObject.activeDeviceRigName : "No active Device Rig"
                                color: themeTokens.textStrong; font.pixelSize: 20; font.bold: true
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            visible: selectedRig !== null
                            radius: height / 2; color: Qt.rgba(healthColor(selectedRig ? selectedRig.health : "offline").r,
                                healthColor(selectedRig ? selectedRig.health : "offline").g,
                                healthColor(selectedRig ? selectedRig.health : "offline").b, 0.17)
                            border.color: healthColor(selectedRig ? selectedRig.health : "offline")
                            implicitWidth: healthText.implicitWidth + 22; implicitHeight: 29
                            Text { id: healthText; anchors.centerIn: parent; text: selectedRig ? selectedRig.healthLabel : "Offline"
                                color: healthColor(selectedRig ? selectedRig.health : "offline"); font.pixelSize: 11; font.bold: true }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: selectedRig ? selectedRig.members.length + " inputs  →  " + selectedRig.outputs.length
                              + (selectedRig.outputs.length === 1 ? " virtual output" : " virtual outputs")
                                          : "Create a rig to keep a controller arrangement reusable across profiles."
                        color: themeTokens.text; font.pixelSize: 13
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 16

                Panel {
                    Layout.preferredWidth: Math.max(285, root.width * 0.34)
                    Layout.fillHeight: true
                    Layout.minimumHeight: 310
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 14
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            SmallLabel { text: "DEVICE RIGS" }
                            Item { Layout.fillWidth: true }
                            Text { text: rigs.length; color: themeTokens.textMuted; font.pixelSize: 11; font.bold: true }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
                        Repeater {
                            model: rigs
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 68; radius: themeTokens.controlRadius
                                color: root.selectedRigId === modelData.id ? themeTokens.selection : rigHover.containsMouse ? themeTokens.controlHover : "transparent"
                                border.color: root.selectedRigId === modelData.id ? healthColor(modelData.health) : "transparent"
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 10; spacing: 9
                                    Rectangle { width: 8; height: 8; radius: 4; color: healthColor(modelData.health) }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 1
                                        Text { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideRight; color: themeTokens.textStrong; font.pixelSize: 13; font.bold: true }
                                        Text { text: modelData.healthLabel + "  ·  " + modelData.members.length + " inputs"; color: themeTokens.textMuted; font.pixelSize: 10 }
                                    }
                                    Text { visible: modelData.default; text: "DEFAULT"; color: themeTokens.orange; font.pixelSize: 9; font.bold: true }
                                }
                                MouseArea { id: rigHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.pickRig(modelData.id) }
                            }
                        }
                        Item { Layout.fillHeight: true }
                        Text {
                            visible: rigs.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: "No Device Rigs yet. Start with your normal stick or HOTAS; extra devices can be added when you are ready."
                            color: themeTokens.textMuted; font.pixelSize: 12
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 310
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                SmallLabel { text: selectedRig ? "RIG DETAILS" : "GET STARTED" }
                                Text { text: selectedRig ? selectedRig.name : "Build your first Device Rig"; color: themeTokens.textStrong; font.pixelSize: 21; font.bold: true }
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                visible: selectedRig && !selectedRig.active; text: "ACTIVATE"
                                enabled: selectedRig && selectedRig.health !== "conflict" && selectedRig.enabled
                                onClicked: backendObject.activateDeviceRig(selectedRig.id)
                            }
                            Button {
                                visible: selectedRig && selectedRig.active; text: "DEACTIVATE"
                                onClicked: backendObject.deactivateDeviceRig(selectedRig.id)
                            }
                            Button {
                                visible: selectedRig; text: "VERIFY RIG"
                                onClicked: backendObject.verifyDeviceRig(selectedRig.id)
                            }
                            Button {
                                visible: selectedRig && !selectedRig.default; text: "MAKE DEFAULT"
                                onClicked: backendObject.setDefaultDeviceRig(selectedRig.id)
                            }
                            Button {
                                visible: selectedRig && selectedRig.default; text: "CLEAR DEFAULT"
                                onClicked: backendObject.clearDefaultDeviceRig(selectedRig.id)
                            }
                            Button {
                                visible: selectedRig; text: "RENAME"; flat: true
                                onClicked: { rigNameField.text = selectedRig.name; renameRigDialog.open() }
                            }
                            Button {
                                visible: selectedRig; text: "DELETE RIG"; flat: true
                                onClicked: deleteRigDialog.open()
                            }
                        }
                        Text {
                            visible: !selectedRig; Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: "A rig answers what hardware you use together. Profiles remain separate and answer how that hardware behaves."
                            color: themeTokens.text; font.pixelSize: 13
                        }
                        Repeater {
                            visible: selectedRig !== null
                            model: selectedRig ? selectedRig.members : []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true; implicitHeight: 58; radius: themeTokens.controlRadius
                                color: Qt.rgba(themeTokens.panelRaised.r, themeTokens.panelRaised.g, themeTokens.panelRaised.b, 0.54)
                                border.color: modelData.ambiguous ? themeTokens.danger : modelData.connected ? themeTokens.ready : themeTokens.border
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 10
                                    Rectangle { width: 7; height: 7; radius: 4; color: modelData.ambiguous ? themeTokens.danger : modelData.connected ? themeTokens.ready : themeTokens.textMuted }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 1
                                        Text { text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 13; font.bold: true }
                                        Text { text: modelData.ambiguous ? "Selection required" : !modelData.verified ? "Needs verification" : modelData.connected ? "Connected · Verified" : modelData.required ? "Required · Offline" : "Optional · Offline"; color: themeTokens.textMuted; font.pixelSize: 10 }
                                    }
                                    Button { text: "DETAILS"; flat: true; onClicked: root.openDevice(modelData.id) }
                                    Button {
                                        text: modelData.required ? "REQUIRED" : "OPTIONAL"; flat: true
                                        onClicked: backendObject.setDeviceRigMemberRequired(selectedRig.id, modelData.id, !modelData.required)
                                    }
                                    ComboBox {
                                        visible: selectedRig && selectedRig.outputs.length > 1
                                        Layout.preferredWidth: 138
                                        model: selectedRig ? selectedRig.outputs : []
                                        textRole: "name"; valueRole: "id"
                                        currentIndex: {
                                            if (!selectedRig) return -1
                                            for (let outputIndex = 0; outputIndex < selectedRig.outputs.length; ++outputIndex) {
                                                if (selectedRig.outputs[outputIndex].id === modelData.preferredOutputLayoutId) return outputIndex
                                            }
                                            return 0
                                        }
                                        onActivated: backendObject.setDeviceRigMemberOutput(selectedRig.id, modelData.id, currentValue)
                                        ToolTip.visible: hovered; ToolTip.text: "Virtual output used by this physical input"
                                    }
                                    CheckBox {
                                        text: "Use"; checked: !!modelData.enabled
                                        onToggled: backendObject.setDeviceRigMemberEnabled(selectedRig.id, modelData.id, checked)
                                    }
                                    CheckBox {
                                        checked: !!modelData.selected; text: "Edit"
                                        onToggled: {
                                            let ids = []
                                            const entries = backendObject.editingDevices
                                            for (let i = 0; i < entries.length; ++i) {
                                                if (entries[i].id === modelData.id ? checked : entries[i].selected) ids.push(entries[i].id)
                                            }
                                            backendObject.setEditingDeviceContext(selectedRig.id, ids)
                                        }
                                    }
                                    Button {
                                        visible: selectedRig && selectedRig.members.length > 1
                                        text: "REMOVE"; flat: true
                                        onClicked: backendObject.removeDeviceRigMember(selectedRig.id, modelData.id)
                                    }
                                }
                            }
                        }
                        Rectangle { visible: selectedRig !== null; Layout.fillWidth: true; height: 1; color: themeTokens.divider }
                        RowLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true
                            SmallLabel { text: "VIRTUAL OUTPUTS" }
                            Item { Layout.fillWidth: true }
                            Repeater {
                                model: selectedRig ? selectedRig.outputs : []
                                delegate: RowLayout {
                                    required property var modelData
                                    Button {
                                        text: modelData.name + "  ·  vJoy " + modelData.deviceId + "  ›"
                                        flat: true; onClicked: root.openOutput(modelData.id)
                                    }
                                    CheckBox {
                                        text: "Use"; checked: !!modelData.enabled
                                        onToggled: backendObject.setDeviceRigOutputEnabled(selectedRig.id, modelData.id, checked)
                                    }
                                    Button {
                                        visible: selectedRig && selectedRig.outputs.length > 1
                                        text: "REMOVE"; flat: true
                                        onClicked: backendObject.removeDeviceRigOutput(selectedRig.id, modelData.id)
                                    }
                                }
                            }
                            ComboBox {
                                id: outputPicker
                                Layout.preferredWidth: 190
                                model: backendObject ? backendObject.virtualOutputLayouts : []
                                textRole: "name"; valueRole: "id"
                                ToolTip.visible: hovered; ToolTip.text: "Add an existing vJoy layout as another output"
                            }
                            Button {
                                text: "+ ADD OUTPUT"; flat: true
                                enabled: selectedRig && outputPicker.currentValue
                                onClicked: backendObject.addDeviceRigOutput(selectedRig.id, outputPicker.currentValue)
                            }
                        }
                        RowLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true; spacing: 8
                            SmallLabel { text: "PHYSICAL INPUTS" }
                            ComboBox {
                                id: memberPicker
                                Layout.preferredWidth: 220
                                model: controllers
                                textRole: "name"; valueRole: "id"
                                delegate: ItemDelegate {
                                    required property var modelData
                                    width: memberPicker.width
                                    enabled: modelData.id !== "" && !modelData.ambiguous
                                             && !root.rigHasMember(root.selectedRig, modelData.id)
                                    text: modelData.name + (modelData.connected ? " · Connected" : " · Saved / Offline")
                                }
                                ToolTip.visible: hovered; ToolTip.text: "Add a saved physical device to this Device Rig"
                            }
                            CheckBox { id: addMemberOptional; text: "Optional" }
                            Button {
                                text: "+ ADD DEVICE"; flat: true
                                enabled: selectedRig && memberPicker.currentValue
                                         && !root.rigHasMember(selectedRig, memberPicker.currentValue)
                                onClicked: backendObject.addDeviceRigMember(selectedRig.id,
                                    memberPicker.currentValue, !addMemberOptional.checked)
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Text {
                            visible: selectedRig !== null; Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: root.selectedEditingCount() > 1
                                ? "Multiple physical inputs are selected. Route changes require one source; compatible processing edits can be reviewed and applied together."
                                : "Editing context changes what you view and edit across the application. It never switches the active hardware rig."
                            color: themeTokens.textMuted; font.pixelSize: 11
                        }
                        Button {
                            visible: selectedRig && root.selectedEditingCount() > 1
                            text: "BATCH AXIS EDIT…"; flat: true
                            onClicked: batchAxisDialog.open()
                        }
                        Rectangle { visible: selectedRig !== null; Layout.fillWidth: true; height: 1; color: themeTokens.divider }
                        RowLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true; spacing: 12
                            SmallLabel { text: "AUTOMATIC BEHAVIOR" }
                            CheckBox { text: "Enabled"; checked: selectedRig ? selectedRig.enabled : false
                                onToggled: if (selectedRig) backendObject.setDeviceRigEnabled(selectedRig.id, checked) }
                            CheckBox { text: "Auto activate"; checked: selectedRig ? selectedRig.autoActivate : false
                                onToggled: if (selectedRig) backendObject.setDeviceRigAutoActivate(selectedRig.id, checked) }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                visible: selectedRig && selectedRig.members.length > 0
                                model: ["Suspend affected routes", "Deactivate rig", "Use fallback rig"]
                                currentIndex: selectedRig ? Number(selectedRig.disconnectBehavior) : 0
                                onActivated: if (selectedRig) backendObject.setDeviceRigDisconnectBehavior(selectedRig.id, currentIndex)
                                ToolTip.visible: hovered; ToolTip.text: "When a required input disappears"
                            }
                        }
                        RowLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true; spacing: 12
                            SmallLabel { text: "PRIORITY / FALLBACK" }
                            SpinBox {
                                id: priorityBox
                                from: 0; to: 100; stepSize: 1
                                value: selectedRig ? Number(selectedRig.activationPriority) : 50
                                editable: true
                                onValueModified: if (selectedRig) backendObject.setDeviceRigActivationPriority(selectedRig.id, value)
                                ToolTip.visible: hovered; ToolTip.text: "Higher priority wins only when no healthy active rig is retained"
                            }
                            Text { text: "Priority"; color: themeTokens.textMuted; font.pixelSize: 10 }
                            ComboBox {
                                id: fallbackPicker
                                Layout.preferredWidth: 200
                                model: [{ id: "", name: "No fallback" }].concat(rigs.filter(function(item) {
                                    return selectedRig && item.id !== selectedRig.id
                                }))
                                textRole: "name"; valueRole: "id"
                                currentIndex: {
                                    const items = fallbackPicker.model
                                    for (let i = 0; i < items.length; ++i)
                                        if (selectedRig && items[i].id === selectedRig.fallbackRigId) return i
                                    return 0
                                }
                                onActivated: if (selectedRig) backendObject.setDeviceRigFallback(selectedRig.id, currentValue)
                                ToolTip.visible: hovered; ToolTip.text: "Used only when the configured required-device behavior is Use fallback rig"
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true; Layout.preferredHeight: 116
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14
                    SmallLabel { text: "KNOWN PHYSICAL DEVICES" }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Repeater {
                            model: controllers
                            delegate: Rectangle {
                                required property var modelData
                                implicitWidth: Math.max(170, deviceName.implicitWidth + 28); implicitHeight: 52
                                radius: themeTokens.controlRadius; color: themeTokens.panelRaised; border.color: modelData.connected ? themeTokens.ready : themeTokens.border
                                Column { anchors.fill: parent; anchors.margins: 9; spacing: 2
                                    Text { id: deviceName; text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: 210 }
                                    Text { text: modelData.state; color: themeTokens.textMuted; font.pixelSize: 9 }
                                }
                                MouseArea { anchors.fill: parent; onClicked: if (modelData.id) root.openDevice(modelData.id) }
                            }
                        }
                        Text { visible: controllers.length === 0; text: "Connect a controller to begin."; color: themeTokens.textMuted; font.pixelSize: 12 }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    Dialog {
        id: physicalDeviceDialog
        modal: true
        title: "Physical Device"
        anchors.centerIn: parent
        width: Math.min(620, root.width - 42)
        property var detail: backendObject ? backendObject.physicalDeviceDetail(root.selectedDeviceId) : ({})
        background: Rectangle { color: themeTokens.panel; border.color: themeTokens.borderStrong; radius: themeTokens.panelRadius }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; text: physicalDeviceDialog.detail.name || "Saved device"; color: themeTokens.textStrong; font.pixelSize: 22; font.bold: true }
            RowLayout { Layout.fillWidth: true
                Rectangle { width: 9; height: 9; radius: 5; color: physicalDeviceDialog.detail.connected ? themeTokens.ready : themeTokens.textMuted }
                Text { text: physicalDeviceDialog.detail.connected ? "Connected" : "Saved · Offline"; color: themeTokens.text; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Text { text: physicalDeviceDialog.detail.verified ? "VERIFIED" : "NEEDS VERIFICATION"; color: physicalDeviceDialog.detail.verified ? themeTokens.ready : themeTokens.warning; font.pixelSize: 10; font.bold: true }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
            GridLayout { Layout.fillWidth: true; columns: 2; rowSpacing: 8; columnSpacing: 20
                SmallLabel { text: "CAPABILITIES" }
                Text { text: (physicalDeviceDialog.detail.axisCount || 0) + " axes · " + (physicalDeviceDialog.detail.buttonCount || 0) + " buttons · " + (physicalDeviceDialog.detail.povCount || 0) + " POV"; color: themeTokens.text }
                SmallLabel { text: "CALIBRATION" }
                Text { text: (physicalDeviceDialog.detail.calibratedAxes || 0) + " calibrated axes"; color: themeTokens.text }
                SmallLabel { text: "LAST SEEN" }
                Text { text: physicalDeviceDialog.detail.lastSeen || "No recorded use yet"; color: themeTokens.text }
                SmallLabel { text: "VERIFICATION" }
                Text { text: physicalDeviceDialog.detail.lastVerified || "Needs setup verification"; color: physicalDeviceDialog.detail.verified ? themeTokens.ready : themeTokens.warning }
                SmallLabel { text: "RIGS" }
                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: physicalDeviceDialog.detail.rigs || "Not assigned to a Device Rig"; color: themeTokens.text }
                SmallLabel { text: "MAPPINGS" }
                Text { text: (physicalDeviceDialog.detail.mappedAxes || 0) + " axes · " + (physicalDeviceDialog.detail.mappedButtons || 0) + " buttons · " + (physicalDeviceDialog.detail.mappedPovs || 0) + " POV routes"; color: themeTokens.text }
                SmallLabel { text: "HIDHIDE" }
                Text { text: physicalDeviceDialog.detail.hidhideManaged ? "Managed for this device" : "Not adopted"; color: themeTokens.text }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
            Text { text: "ADVANCED IDENTITY"; color: themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
            Text { Layout.fillWidth: true; text: physicalDeviceDialog.detail.hidInstanceId || physicalDeviceDialog.detail.directInputId || "No current raw identity"; color: themeTokens.textMuted; font.pixelSize: 10; elide: Text.ElideMiddle }
            RowLayout { Layout.fillWidth: true
                Button { text: "VERIFY DEVICE"; onClicked: backendObject.verifyDeviceRig(root.selectedRigId) }
                Item { Layout.fillWidth: true }
                Button { visible: !(physicalDeviceDialog.detail.rigs || ""); text: "FORGET DEVICE"; onClicked: { backendObject.forgetController(root.selectedDeviceId); physicalDeviceDialog.close() } }
                Button { text: "CLOSE"; onClicked: physicalDeviceDialog.close() }
            }
        }
    }

    Dialog {
        id: outputDetailDialog
        modal: true
        title: "Virtual Output"
        anchors.centerIn: parent
        width: Math.min(620, root.width - 42)
        property var detail: backendObject ? backendObject.virtualOutputDetail(root.selectedOutputId) : ({})
        background: Rectangle { color: themeTokens.panel; border.color: themeTokens.borderStrong; radius: themeTokens.panelRadius }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; text: outputDetailDialog.detail.name || "Virtual output"; color: themeTokens.textStrong; font.pixelSize: 22; font.bold: true }
            RowLayout { Layout.fillWidth: true
                Rectangle { width: 9; height: 9; radius: 5; color: outputDetailDialog.detail.ready ? themeTokens.ready : themeTokens.warning }
                Text { Layout.fillWidth: true; text: outputDetailDialog.detail.ready ? "Ready / usable" : outputDetailDialog.detail.status || "Verify output"; color: themeTokens.text; font.pixelSize: 12; elide: Text.ElideRight }
                Text { text: "VJOY " + (outputDetailDialog.detail.deviceId || "—"); color: themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
            GridLayout { Layout.fillWidth: true; columns: 2; rowSpacing: 8; columnSpacing: 20
                SmallLabel { text: "AXES" }
                Text { Layout.fillWidth: true; text: outputDetailDialog.detail.axes || "None"; color: themeTokens.text; wrapMode: Text.WordWrap }
                SmallLabel { text: "BUTTONS" }
                Text { text: outputDetailDialog.detail.buttons || 0; color: themeTokens.text }
                SmallLabel { text: "POVS" }
                Text { text: (outputDetailDialog.detail.continuousPovs || 0) + " continuous · " + (outputDetailDialog.detail.discretePovs || 0) + " discrete"; color: themeTokens.text }
                SmallLabel { text: "USED BY" }
                Text { Layout.fillWidth: true; text: outputDetailDialog.detail.rigs || "No Device Rigs"; color: themeTokens.text; elide: Text.ElideRight }
                SmallLabel { text: "ROUTE USAGE" }
                Text { text: (outputDetailDialog.detail.routeCount || 0) + " configured routes"; color: themeTokens.text }
                SmallLabel { text: "VISIBILITY" }
                Text { text: outputDetailDialog.detail.managedVisibility ? "Managed" : "Not adopted"; color: themeTokens.text }
            }
            RowLayout { Layout.fillWidth: true
                Button { text: "VERIFY / REPAIR OUTPUT"; onClicked: backendObject.verifyDeviceRig(root.selectedRigId) }
                Button { text: "CONFIGURE VJOY"; onClicked: backendObject.openVjoyConfiguration() }
                Button {
                    text: "RENAME LAYOUT"; flat: true
                    onClicked: { outputNameField.text = outputDetailDialog.detail.name || ""; renameOutputDialog.open() }
                }
                Item { Layout.fillWidth: true }
                Button { text: "CLOSE"; onClicked: outputDetailDialog.close() }
            }
        }
    }

    Dialog {
        id: renameOutputDialog
        modal: true; title: "Rename Virtual Output"
        anchors.centerIn: parent; width: Math.min(460, root.width - 48)
        background: Rectangle { color: themeTokens.panel; border.color: themeTokens.borderStrong; radius: themeTokens.panelRadius }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 14
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted
                text: "This changes the saved output label only. Its vJoy device ID, routes, visibility, and runtime behavior are unchanged." }
            TextField { id: outputNameField; Layout.fillWidth: true; selectByMouse: true }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                Button { text: "CANCEL"; onClicked: renameOutputDialog.close() }
                Button { text: "SAVE"; highlighted: true; enabled: outputNameField.text.trim().length > 0
                    onClicked: if (backendObject.renameVirtualOutputLayout(root.selectedOutputId, outputNameField.text)) renameOutputDialog.close() }
            }
        }
    }

    Dialog {
        id: renameRigDialog
        modal: true; title: "Rename Device Rig"
        anchors.centerIn: parent; width: Math.min(460, root.width - 48)
        standardButtons: Dialog.Cancel
        background: Rectangle { color: themeTokens.panel; border.color: themeTokens.borderStrong; radius: themeTokens.panelRadius }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 14
            Text { text: "A rig name changes only the saved organization and editing context."; color: themeTokens.textMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextField { id: rigNameField; Layout.fillWidth: true; selectByMouse: true }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                Button { text: "SAVE"; highlighted: true; enabled: rigNameField.text.trim().length > 0
                    onClicked: if (selectedRig && backendObject.renameDeviceRig(selectedRig.id, rigNameField.text)) renameRigDialog.close() }
            }
        }
    }

    Dialog {
        id: batchAxisDialog
        modal: true; title: "Review Multi-Device Axis Edit"
        anchors.centerIn: parent; width: Math.min(540, root.width - 48)
        property int axisIndex: 0
        property bool showReview: true
        property var preview: ({})
        function refreshPreview() {
            preview = backendObject.editingAxisBatchPreview(axisIndex, "inverted", invertAll.checked)
        }
        onOpened: refreshPreview()
        background: Rectangle { color: themeTokens.panel; border.color: themeTokens.borderStrong; radius: themeTokens.panelRadius }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; text: batchAxisDialog.preview.summary || "Review selected inputs."; color: themeTokens.text; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                SmallLabel { text: "AXIS" }
                ComboBox {
                    id: batchAxisChoice
                    model: ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider 1", "Slider 2"]
                    currentIndex: batchAxisDialog.axisIndex
                    onActivated: { batchAxisDialog.axisIndex = currentIndex; batchAxisDialog.refreshPreview() }
                }
                CheckBox {
                    id: invertAll; text: "Invert on selected inputs"
                    onToggled: batchAxisDialog.refreshPreview()
                }
                Item { Layout.fillWidth: true }
            }
            Button { text: batchAxisDialog.showReview ? "HIDE REVIEW" : "REVIEW"; flat: true
                onClicked: batchAxisDialog.showReview = !batchAxisDialog.showReview }
            Repeater {
                visible: batchAxisDialog.showReview
                model: batchAxisDialog.preview.targets || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Rectangle { width: 7; height: 7; radius: 4; color: modelData.compatible ? themeTokens.ready : themeTokens.warning }
                    Text { Layout.fillWidth: true; text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 12 }
                    Text { text: "Compatible"; color: themeTokens.ready; font.pixelSize: 10; font.bold: true }
                }
            }
            Text { Layout.fillWidth: true; text: "Only compatible processing settings are batched. Axis routes always require one explicit physical source."; color: themeTokens.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                Button { text: "CANCEL"; onClicked: batchAxisDialog.close() }
                Button {
                    text: "APPLY COMPATIBLE ONLY"; highlighted: true
                    enabled: !!batchAxisDialog.preview && !!batchAxisDialog.preview.valid
                    onClicked: {
                        if (backendObject.applyEditingAxisBatch(batchAxisDialog.axisIndex, "inverted",
                                invertAll.checked, "apply-compatible-only")) batchAxisDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: createRigDialog
        modal: true; title: "Create Device Rig"
        anchors.centerIn: parent; width: Math.min(560, root.width - 48)
        standardButtons: Dialog.Cancel
        background: Rectangle { color: themeTokens.panel; border.color: themeTokens.borderStrong; radius: themeTokens.panelRadius }
        ColumnLayout {
            width: parent.width; spacing: 13
            Text { Layout.fillWidth: true; text: "Choose the physical devices that belong together. The default output combines them into one virtual controller."; wrapMode: Text.WordWrap; color: themeTokens.text }
            TextField { id: rigName; Layout.fillWidth: true; placeholderText: "Rig name, for example BF6 Flight Rig" }
            Repeater {
                id: controllerRepeater
                model: controllers
                delegate: CheckBox {
                    required property var modelData
                    visible: modelData.id !== "" && !modelData.ambiguous
                    text: modelData.name + (modelData.connected ? "  ·  Connected" : "  ·  Saved / Offline")
                    checked: !!modelData.selected
                }
            }
            Text { text: "You can mark accessories optional and choose advanced output routing after creation."; color: themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "CREATE & VERIFY"; highlighted: true
                    enabled: rigName.text.trim().length > 0
                    onClicked: {
                        let ids = []
                        for (let i = 0; i < controllerRepeater.count; ++i) {
                            const item = controllerRepeater.itemAt(i)
                            if (item && item.visible && item.checked) ids.push(item.modelData.id)
                        }
                        const created = backendObject.createDeviceRig(rigName.text, ids)
                        if (created !== "") {
                            // This is an explicit setup transaction, not a
                            // top-bar context change. Select the new rig for
                            // the unified verifier only after its creator
                            // asks to create and verify it.
                            backendObject.activateDeviceRig(created)
                            root.pickRig(created)
                            createRigDialog.close()
                            rigName.text = ""
                            backendObject.verifyDeviceRig(created)
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteRigDialog
        modal: true; title: "Delete Device Rig"
        anchors.centerIn: parent; width: Math.min(460, root.width - 48)
        background: Rectangle { color: themeTokens.panel; border.color: themeTokens.danger; radius: themeTokens.panelRadius }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.text
                text: root.selectedRig ? "Delete \"" + root.selectedRig.name
                      + "\"? Saved devices and their mappings remain available, but this rig will stop routing and its active/editing context will be cleared." : "Delete this Device Rig?"
            }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                Button { text: "CANCEL"; onClicked: deleteRigDialog.close() }
                Button {
                    text: "DELETE RIG"; highlighted: true
                    onClicked: {
                        if (root.selectedRig && backendObject.deleteDeviceRig(root.selectedRig.id)) {
                            root.selectedRigId = ""
                            deleteRigDialog.close()
                        }
                    }
                }
            }
        }
    }
}
