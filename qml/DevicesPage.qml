import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

Page {
    id: root
    objectName: "devicesPage"
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
    // Theme.qml uses typed colors while Legacy exposes its established tokens
    // as a compact map. Normalize here so every shared Devices surface is
    // equally valid in all four themes.
    property color readyColor: themeTokens.ready
    property color warningColor: themeTokens.warning
    property color dangerColor: themeTokens.danger
    property color mutedColor: themeTokens.textMuted
    property color panelRaisedColor: themeTokens.panelRaised

    function rigFor(id) {
        for (let i = 0; i < rigs.length; ++i) if (rigs[i].id === id) return rigs[i]
        return rigs.length > 0 ? rigs[0] : null
    }
    readonly property var selectedRig: rigFor(selectedRigId)
    function healthColor(key) {
        if (key === "ready") return readyColor
        if (key === "partial") return warningColor
        if (key === "disabled" || key === "offline") return mutedColor
        return dangerColor
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
    function indexFor(items, id) {
        for (let i = 0; i < items.length; ++i) if (items[i].id === id) return i
        return 0
    }
    function hasOutput(rig, id) {
        if (!rig || !id) return false
        for (let i = 0; i < rig.outputs.length; ++i) if (rig.outputs[i].id === id) return true
        return false
    }
    function selectedEditingCount() {
        if (!backendObject) return 0
        const entries = backendObject.editingDevices
        let count = 0
        for (let i = 0; i < entries.length; ++i) if (entries[i].selected) ++count
        return count
    }
    function containsRig(id) {
        for (let i = 0; i < rigs.length; ++i) if (rigs[i].id === id) return true
        return false
    }
    function normalizeSelectionsAfterModelRefresh() {
        // A device-rig model is a value projection, not a stable QObject
        // collection. Never retain an old map after a backend replacement.
        const editing = backendObject ? backendObject.editingDeviceRigId : ""
        if (containsRig(editing)) selectedRigId = editing
        else if (!containsRig(selectedRigId)) selectedRigId = rigs.length ? rigs[0].id : ""
        if (selectedRig && !rigHasMember(selectedRig, selectedDeviceId)) {
            selectedDeviceId = ""
            if (physicalDeviceDialog.visible) physicalDeviceDialog.close()
        }
        if (!hasOutput(selectedRig, selectedOutputId)) {
            selectedOutputId = ""
            if (outputDetailDialog.visible) outputDetailDialog.close()
        }
    }

    Connections {
        target: backendObject
        function onDeviceRigsChanged() { Qt.callLater(root.normalizeSelectionsAfterModelRefresh) }
        function onControllersChanged() { Qt.callLater(root.normalizeSelectionsAfterModelRefresh) }
    }

    component Panel: DevicePanel {
        theme: root.themeTokens
        legacy: root.legacy
        // The section panels share the page viewport.  In contrast to nested
        // member cards, they must never expand the ScrollView's content area
        // merely because a row inside them has a wide implicit size.
        Layout.minimumWidth: 0
        Layout.preferredWidth: root.width
        Layout.maximumWidth: root.width
    }
    component SmallLabel: Text {
        color: themeTokens.textMuted; font.pixelSize: 10; font.bold: true
        font.family: themeTokens.topGun ? themeTokens.displayFont : root.font.family
    }
    // All Devices dialogs share a real application header. Without this
    // wrapper Qt Quick Controls supplies a platform-default title strip,
    // which is especially visible as an incorrect white bar in Day Ops.
    component DeviceDialog: Dialog {
        standardButtons: Dialog.NoButton
        header: ThemedDialogHeader {
            theme: root.themeTokens
            legacy: root.legacy
            heading: parent.title
        }
    }

    background: Rectangle { color: themeTokens.background }

    ScrollView {
        id: devicesScroll
        objectName: "devicesScroll"
        anchors.fill: parent
        clip: true
        contentWidth: root.width

        ColumnLayout {
            objectName: "devicesContent"
            // Page is the authoritative viewport. ScrollView's availableWidth
            // can momentarily retain the old shell width while the page host
            // applies its margins during a resize, making rig cards extend
            // horizontally past the actual Devices page.
            width: root.width
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
                ThemedButton { theme: themeTokens; text: "+ CREATE RIG"; onTriggered: createRigDialog.open() }
            }

            Panel {
                Layout.fillWidth: true
                implicitHeight: migrationWarning.implicitHeight + 30
                visible: backendObject && backendObject.deviceRigMigrationWarning.length > 0
                // Legacy warning panels retain the established layered
                // aviation surface.  The coloured border carries severity;
                // replacing the surface with a translucent modern fill made
                // these cards look imported from Standard.
                color: root.legacy ? "#e9161d23" : Qt.rgba(warningColor.r, warningColor.g, warningColor.b, 0.10)
                border.color: warningColor
                Text {
                    id: migrationWarning
                    anchors.fill: parent; anchors.margins: 16
                    text: backendObject ? backendObject.deviceRigMigrationWarning : ""
                    color: themeTokens.text; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter
                }
            }

            Panel {
                Layout.fillWidth: true
                implicitHeight: rigDetectionWarning.implicitHeight + 30
                visible: backendObject && backendObject.deviceRigDetectionMessage.length > 0
                color: root.legacy ? "#e9161d23" : Qt.rgba(warningColor.r, warningColor.g, warningColor.b, 0.10)
                border.color: warningColor
                Text {
                    id: rigDetectionWarning
                    anchors.fill: parent; anchors.margins: 16
                    text: backendObject ? backendObject.deviceRigDetectionMessage : ""
                    color: themeTokens.text; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter
                }
            }

            Panel {
                objectName: "activeRigPanel"
                Layout.fillWidth: true
                Layout.preferredHeight: activeRigColumn.implicitHeight + 32
                color: root.legacy ? "#e9161d23" : (selectedRig ? Qt.rgba(healthColor(selectedRig.health).r, healthColor(selectedRig.health).g,
                                             healthColor(selectedRig.health).b, 0.08) : themeTokens.panel)
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

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 16

                Panel {
                    objectName: "deviceRigListPanel"
                    Layout.fillWidth: true
                    implicitHeight: rigListContent.implicitHeight + 28
                    ColumnLayout {
                        id: rigListContent
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
                        Text {
                            visible: rigs.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: "No Device Rigs yet. Start with your normal stick or HOTAS; extra devices can be added when you are ready."
                            color: themeTokens.textMuted; font.pixelSize: 12
                        }
                    }
                }

                Panel {
                    objectName: "rigDetailsPanel"
                    Layout.fillWidth: true
                    implicitHeight: rigDetailsContent.implicitHeight + 32
                    ColumnLayout {
                        id: rigDetailsContent
                        anchors.fill: parent; anchors.margins: 16
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                SmallLabel { text: selectedRig ? "RIG DETAILS" : "GET STARTED" }
                                Text { text: selectedRig ? selectedRig.name : "Build your first Device Rig"; color: themeTokens.textStrong; font.pixelSize: 21; font.bold: true }
                            }
                            Item { Layout.fillWidth: true }
                            ThemedButton {
                                theme: themeTokens
                                visible: selectedRig && !selectedRig.active; text: "ACTIVATE"
                                commandEnabled: selectedRig && selectedRig.health !== "conflict" && selectedRig.enabled
                                onTriggered: backendObject.activateDeviceRig(selectedRig.id)
                            }
                            ThemedButton {
                                theme: themeTokens; tone: "secondary"
                                visible: selectedRig && selectedRig.active; text: "DEACTIVATE"
                                onTriggered: backendObject.deactivateDeviceRig(selectedRig.id)
                            }
                            ThemedButton {
                                theme: themeTokens; tone: "secondary"
                                visible: selectedRig; text: "VERIFY RIG"
                                onTriggered: backendObject.verifyDeviceRig(selectedRig.id)
                            }
                            ThemedButton {
                                theme: themeTokens; tone: "secondary"; compact: true
                                visible: selectedRig; text: "…"
                                onTriggered: rigDetailsActions.open()
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
                            delegate: DevicePanel {
                                required property var modelData
                                theme: root.themeTokens; legacy: root.legacy
                                Layout.fillWidth: true; implicitHeight: memberCardContent.implicitHeight + 22; radius: root.legacy ? 4 : themeTokens.controlRadius
                                // Do not recolour a LegacyAviationPanel into
                                // a raised Standard card.  Its layered base,
                                // top highlight and lower edge are part of
                                // the Legacy visual language.
                                color: root.legacy ? "#e9161d23" : Qt.rgba(panelRaisedColor.r, panelRaisedColor.g, panelRaisedColor.b, 0.54)
                                border.color: modelData.ambiguous ? themeTokens.danger : modelData.connected ? themeTokens.ready : themeTokens.border
                                ColumnLayout {
                                    id: memberCardContent
                                    anchors.fill: parent; anchors.margins: 10; spacing: 7
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Rectangle { width: 7; height: 7; radius: 4; color: modelData.ambiguous ? themeTokens.danger : modelData.connected ? themeTokens.ready : themeTokens.textMuted }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 1
                                            Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 13; font.bold: true }
                                            Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.ambiguous ? "Selection required" : !modelData.verified ? "Needs verification" : modelData.connected ? "Connected · Verified" : modelData.required ? "Required · Offline" : "Optional · Offline"; color: themeTokens.textMuted; font.pixelSize: 10 }
                                        }
                                        ThemedButton { theme: themeTokens; text: "DETAILS"; compact: true; tone: "secondary"; onTriggered: root.openDevice(modelData.id) }
                                        ThemedButton { theme: themeTokens; text: "REMOVE"; compact: true; tone: "danger"; visible: selectedRig && selectedRig.members.length > 1; onTriggered: { const rigId = selectedRig ? selectedRig.id : ""; if (rigId) backendObject.removeDeviceRigMember(rigId, modelData.id) } }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 10
                                        ThemedCheckBox { theme: themeTokens; text: modelData.required ? "Required" : "Optional"; checked: !!modelData.required; onToggled: function(value) { const rigId = selectedRig ? selectedRig.id : ""; if (rigId) backendObject.setDeviceRigMemberRequired(rigId, modelData.id, value) } }
                                        ThemedCheckBox { theme: themeTokens; text: "Enabled"; checked: !!modelData.enabled; onToggled: function(value) { const rigId = selectedRig ? selectedRig.id : ""; if (rigId) backendObject.setDeviceRigMemberEnabled(rigId, modelData.id, value) } }
                                        ThemedButton { theme: themeTokens; text: modelData.selected ? "EDITING" : "EDIT THIS"; compact: true; tone: "secondary"; onTriggered: { let ids = []; const entries = backendObject.editingDevices; for (let i = 0; i < entries.length; ++i) if (entries[i].id === modelData.id ? !modelData.selected : entries[i].selected) ids.push(entries[i].id); if (selectedRig) backendObject.setEditingDeviceContext(selectedRig.id, ids) } }
                                        Item { Layout.fillWidth: true }
                                    }
                                    RowLayout {
                                        visible: selectedRig && selectedRig.outputs.length > 1
                                        Layout.fillWidth: true
                                        SmallLabel { text: "OUTPUT" }
                                        ThemedComboBox {
                                            theme: themeTokens; Layout.preferredWidth: 220
                                            model: selectedRig ? selectedRig.outputs : []
                                            textRole: "name"; valueRole: "id"
                                            currentIndex: root.indexFor(selectedRig ? selectedRig.outputs : [], modelData.preferredOutputLayoutId)
                                            onActivated: function(index, value) { const rigId = selectedRig ? selectedRig.id : ""; if (rigId) backendObject.setDeviceRigMemberOutput(rigId, modelData.id, value) }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                        Rectangle { visible: selectedRig !== null; Layout.fillWidth: true; height: 1; color: themeTokens.divider }
                        ColumnLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true; spacing: 8
                            SmallLabel { text: "VIRTUAL OUTPUTS" }
                            Repeater {
                                model: selectedRig ? selectedRig.outputs : []
                                delegate: DevicePanel {
                                    required property var modelData
                                    theme: root.themeTokens; legacy: root.legacy
                                    Layout.fillWidth: true
                                    implicitHeight: outputCardContent.implicitHeight + 18
                                    color: root.legacy ? "#e9161d23" : themeTokens.panelRaised
                                    border.color: modelData.ready ? themeTokens.ready : themeTokens.warning
                                    RowLayout {
                                        id: outputCardContent
                                        anchors.fill: parent; anchors.margins: 9; spacing: 8
                                        Rectangle { width: 7; height: 7; radius: 4; color: modelData.ready ? themeTokens.ready : themeTokens.warning }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 1
                                            Text { Layout.fillWidth: true; text: modelData.name + "  ·  vJoy " + modelData.deviceId; color: themeTokens.textStrong; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight }
                                            Text { Layout.fillWidth: true; text: (modelData.ready ? "Ready" : modelData.status || "Needs verification") + "  ·  " + (modelData.routeCount || 0) + " configured routes"; color: themeTokens.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                                        }
                                        ThemedCheckBox { theme: themeTokens; text: "Use"; checked: !!modelData.enabled; onToggled: function(value) { const rigId = selectedRig ? selectedRig.id : ""; if (rigId) backendObject.setDeviceRigOutputEnabled(rigId, modelData.id, value) } }
                                        ThemedButton { theme: themeTokens; text: "DETAILS"; compact: true; tone: "secondary"; onTriggered: root.openOutput(modelData.id) }
                                        ThemedButton { visible: selectedRig && selectedRig.outputs.length > 1; theme: themeTokens; text: "REMOVE"; compact: true; tone: "danger"; onTriggered: { const rigId = selectedRig ? selectedRig.id : ""; if (rigId) backendObject.removeDeviceRigOutput(rigId, modelData.id) } }
                                    }
                                }
                            }
                            RowLayout { Layout.fillWidth: true
                                ThemedButton { theme: themeTokens; text: "+ ADD OUTPUT"; tone: "secondary"; onTriggered: addOutputDialog.open() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                        RowLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true; spacing: 8
                            ThemedButton { theme: themeTokens; text: "+ ADD INPUT DEVICE"; tone: "secondary"; onTriggered: addMemberDialog.open() }
                            Item { Layout.fillWidth: true }
                        }
                        Text {
                            visible: selectedRig !== null; Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: root.selectedEditingCount() > 1
                                ? "Multiple physical inputs are selected. Route changes require one source; compatible processing edits can be reviewed and applied together."
                                : "Editing context changes what you view and edit across the application. It never switches the active hardware rig."
                            color: themeTokens.textMuted; font.pixelSize: 11
                        }
                        ThemedButton {
                            visible: selectedRig && root.selectedEditingCount() > 1
                            theme: themeTokens; text: "BATCH AXIS EDIT…"; tone: "secondary"
                            onTriggered: batchAxisDialog.open()
                        }
                    }
                }

                // Automatic controls intentionally have their own section.
                // They are control-plane policy, not member/output controls,
                // and must grow independently from the detail card.
                Panel {
                    objectName: "automaticBehaviorPanel"
                    visible: selectedRig !== null
                    Layout.fillWidth: true
                    implicitHeight: automaticBehaviorContent.implicitHeight + 28
                    ColumnLayout {
                        id: automaticBehaviorContent
                        anchors.fill: parent; anchors.margins: 14; spacing: 10
                        SmallLabel { text: "AUTOMATIC BEHAVIOR" }
                        Flow {
                            Layout.fillWidth: true; spacing: 12
                            ThemedCheckBox { theme: themeTokens; text: "Enabled"; checked: selectedRig ? selectedRig.enabled : false; onToggled: function(value) { if (selectedRig) backendObject.setDeviceRigEnabled(selectedRig.id, value) } }
                            ThemedCheckBox { theme: themeTokens; text: "Auto activate"; checked: selectedRig ? selectedRig.autoActivate : false; onToggled: function(value) { if (selectedRig) backendObject.setDeviceRigAutoActivate(selectedRig.id, value) } }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            SmallLabel { text: "DISCONNECT" }
                            ThemedComboBox {
                                theme: themeTokens; Layout.preferredWidth: 220
                                model: ["Suspend affected routes", "Deactivate rig", "Use fallback rig"]
                                currentIndex: selectedRig ? Number(selectedRig.disconnectBehavior) : 0
                                onActivated: function(index) { if (selectedRig) backendObject.setDeviceRigDisconnectBehavior(selectedRig.id, index) }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            SmallLabel { text: "PRIORITY" }
                            ThemedStepper { theme: themeTokens; value: selectedRig ? Number(selectedRig.activationPriority) : 50; from: 0; to: 100; onValueModified: function(value) { if (selectedRig) backendObject.setDeviceRigActivationPriority(selectedRig.id, value) } }
                            SmallLabel { text: "FALLBACK" }
                            ThemedComboBox {
                                id: fallbackPicker; theme: themeTokens; Layout.preferredWidth: 210
                                model: [{ id: "", name: "No fallback" }].concat(rigs.filter(function(item) { return selectedRig && item.id !== selectedRig.id }))
                                textRole: "name"; valueRole: "id"
                                currentIndex: root.indexFor(model, selectedRig ? selectedRig.fallbackRigId : "")
                                onActivated: function(index, value) { if (selectedRig) backendObject.setDeviceRigFallback(selectedRig.id, value) }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            Panel {
                objectName: "knownDevicesPanel"
                Layout.fillWidth: true; implicitHeight: knownDevicesContent.implicitHeight + 28
                ColumnLayout {
                    id: knownDevicesContent
                    anchors.fill: parent; anchors.margins: 14
                    SmallLabel { text: "KNOWN PHYSICAL DEVICES" }
                    Flow {
                        Layout.fillWidth: true; spacing: 10
                        Repeater {
                            model: controllers
                            delegate: DevicePanel {
                                required property var modelData
                                theme: root.themeTokens; legacy: root.legacy
                                implicitWidth: Math.max(170, deviceName.implicitWidth + 28); implicitHeight: 52
                                radius: themeTokens.controlRadius; color: root.legacy ? "#e9161d23" : themeTokens.panelRaised; border.color: modelData.connected ? themeTokens.ready : themeTokens.border
                                Column { anchors.fill: parent; anchors.margins: 9; spacing: 2
                                    Text { id: deviceName; text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: 210 }
                                    Text { text: modelData.state; color: themeTokens.textMuted; font.pixelSize: 9 }
                                }
                                MouseArea { anchors.fill: parent; onClicked: if (modelData.id) root.openDevice(modelData.id) }
                            }
                        }
                        Text { visible: controllers.length === 0; text: "Connect a controller to begin."; color: themeTokens.textMuted; font.pixelSize: 12 }
                    }
                }
            }
        }
    }

    Popup {
        id: rigDetailsActions
        objectName: "rigDetailsActionsPopup"
        parent: Overlay.overlay
        x: Math.max(12, root.width - width - 24); y: 96; width: 210; padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        // The overflow is part of the Devices presentation system, not a
        // generic Qt popup.  In particular, Legacy needs the same layered
        // surface construction as its established cards and dialogs.
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 5
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: selectedRig && selectedRig.default ? "CLEAR DEFAULT" : "SET DEFAULT"; tone: "secondary"; onTriggered: { const rig = selectedRig; rigDetailsActions.close(); if (rig) { if (rig.default) backendObject.clearDefaultDeviceRig(rig.id); else backendObject.setDefaultDeviceRig(rig.id) } } }
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: "RENAME RIG"; tone: "secondary"; onTriggered: { rigNameField.text = selectedRig ? selectedRig.name : ""; rigDetailsActions.close(); renameRigDialog.open() } }
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: selectedRig && selectedRig.enabled ? "DISABLE RIG" : "ENABLE RIG"; tone: "secondary"; onTriggered: { const rig = selectedRig; rigDetailsActions.close(); if (rig) backendObject.setDeviceRigEnabled(rig.id, !rig.enabled) } }
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: "DELETE RIG"; tone: "danger"; onTriggered: { rigDetailsActions.close(); deleteRigDialog.open() } }
        }
    }

    DeviceDialog {
        id: addMemberDialog
        objectName: "addMemberDialog"
        modal: true; title: "Add Input Device"
        anchors.centerIn: parent; width: Math.min(460, Math.max(340, root.width - 48))
        background: DevicePanel { theme: themeTokens; legacy: root.legacy }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; text: "Add a saved physical controller to this rig. Its calibration and existing mappings remain intact."; color: themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
            ThemedComboBox { id: memberPicker; theme: themeTokens; Layout.fillWidth: true; model: controllers.filter(function(item) { return item.id !== "" && !item.ambiguous && !root.rigHasMember(root.selectedRig, item.id) }); textRole: "name"; valueRole: "id" }
            ThemedCheckBox { id: addMemberOptional; theme: themeTokens; text: "Optional accessory"; checked: false }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: addMemberDialog.close() }
                ThemedButton { theme: themeTokens; text: "ADD INPUT"; commandEnabled: selectedRig && memberPicker.currentValue; onTriggered: { const rig = selectedRig; const id = memberPicker.currentValue; if (rig && id && backendObject.addDeviceRigMember(rig.id, id, !addMemberOptional.checked)) addMemberDialog.close() } }
            }
        }
    }

    DeviceDialog {
        id: addOutputDialog
        objectName: "addOutputDialog"
        modal: true; title: "Add Virtual Output"
        anchors.centerIn: parent; width: Math.min(460, Math.max(340, root.width - 48))
        background: DevicePanel { theme: themeTokens; legacy: root.legacy }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted
                text: "Choose an existing virtual output layout for this rig. Each enabled output is verified independently." }
            ThemedComboBox { id: outputPicker; theme: themeTokens; Layout.fillWidth: true
                model: backendObject ? backendObject.virtualOutputLayouts : []; textRole: "name"; valueRole: "id" }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: addOutputDialog.close() }
                ThemedButton { theme: themeTokens; text: "ADD OUTPUT"; commandEnabled: selectedRig && outputPicker.currentValue
                    onTriggered: { const rig = selectedRig; if (rig && backendObject.addDeviceRigOutput(rig.id, outputPicker.currentValue)) addOutputDialog.close() } }
            }
        }
    }

    DeviceDialog {
        id: physicalDeviceDialog
        objectName: "physicalDeviceDialog"
        modal: true
        title: "Physical Device"
        anchors.centerIn: parent
        width: Math.min(620, root.width - 42)
        property var detail: backendObject ? backendObject.physicalDeviceDetail(root.selectedDeviceId) : ({})
        background: DevicePanel { theme: themeTokens; legacy: root.legacy }
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
                ThemedButton { theme: themeTokens; text: "VERIFY DEVICE"; tone: "secondary"; onTriggered: backendObject.verifyDeviceRig(root.selectedRigId) }
                Item { Layout.fillWidth: true }
                ThemedButton { visible: !(physicalDeviceDialog.detail.rigs || ""); theme: themeTokens; text: "FORGET DEVICE"; tone: "danger"; onTriggered: { backendObject.forgetController(root.selectedDeviceId); physicalDeviceDialog.close() } }
                ThemedButton { theme: themeTokens; text: "CLOSE"; tone: "secondary"; onTriggered: physicalDeviceDialog.close() }
            }
        }
    }

    DeviceDialog {
        id: outputDetailDialog
        objectName: "outputDetailDialog"
        modal: true
        title: "Virtual Output"
        anchors.centerIn: parent
        width: Math.min(620, root.width - 42)
        property var detail: backendObject ? backendObject.virtualOutputDetail(root.selectedOutputId) : ({})
        background: DevicePanel { theme: themeTokens; legacy: root.legacy }
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
                ThemedButton { theme: themeTokens; text: "VERIFY / REPAIR OUTPUT"; tone: "secondary"; onTriggered: backendObject.verifyDeviceRig(root.selectedRigId) }
                ThemedButton { theme: themeTokens; text: "CONFIGURE VJOY"; tone: "secondary"; onTriggered: backendObject.openVjoyConfiguration() }
                ThemedButton {
                    theme: themeTokens; text: "RENAME LAYOUT"; tone: "secondary"
                    onTriggered: { outputNameField.text = outputDetailDialog.detail.name || ""; renameOutputDialog.open() }
                }
                Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CLOSE"; tone: "secondary"; onTriggered: outputDetailDialog.close() }
            }
        }
    }

    DeviceDialog {
        id: renameOutputDialog
        modal: true; title: "Rename Virtual Output"
        anchors.centerIn: parent; width: Math.min(460, root.width - 48)
        background: DevicePanel { theme: themeTokens; legacy: root.legacy }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 14
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted
                text: "This changes the saved output label only. Its vJoy device ID, routes, visibility, and runtime behavior are unchanged." }
            ThemedTextInput { id: outputNameField; theme: themeTokens; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: renameOutputDialog.close() }
                ThemedButton { theme: themeTokens; text: "SAVE"; commandEnabled: outputNameField.text.trim().length > 0
                    onTriggered: if (backendObject.renameVirtualOutputLayout(root.selectedOutputId, outputNameField.text)) renameOutputDialog.close() }
            }
        }
    }

    DeviceDialog {
        id: renameRigDialog
        modal: true; title: "Rename Device Rig"
        anchors.centerIn: parent; width: Math.min(460, root.width - 48)
        background: DevicePanel { theme: themeTokens; legacy: root.legacy }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 14
            Text { text: "A rig name changes only the saved organization and editing context."; color: themeTokens.textMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ThemedTextInput { id: rigNameField; theme: themeTokens; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: renameRigDialog.close() }
                ThemedButton { theme: themeTokens; text: "SAVE"; commandEnabled: rigNameField.text.trim().length > 0
                    onTriggered: if (selectedRig && backendObject.renameDeviceRig(selectedRig.id, rigNameField.text)) renameRigDialog.close() }
            }
        }
    }

    DeviceDialog {
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
        background: DevicePanel { theme: themeTokens; legacy: root.legacy }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; text: batchAxisDialog.preview.summary || "Review selected inputs."; color: themeTokens.text; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                SmallLabel { text: "AXIS" }
                ThemedComboBox {
                    id: batchAxisChoice
                    theme: themeTokens
                    model: ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider 1", "Slider 2"]
                    currentIndex: batchAxisDialog.axisIndex
                    onActivated: function(index) { batchAxisDialog.axisIndex = index; batchAxisDialog.refreshPreview() }
                }
                ThemedCheckBox {
                    id: invertAll; text: "Invert on selected inputs"
                    theme: themeTokens
                    onToggled: function(value) { batchAxisDialog.refreshPreview() }
                }
                Item { Layout.fillWidth: true }
            }
            ThemedButton { theme: themeTokens; text: batchAxisDialog.showReview ? "HIDE REVIEW" : "REVIEW"; tone: "secondary"
                onTriggered: batchAxisDialog.showReview = !batchAxisDialog.showReview }
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
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: batchAxisDialog.close() }
                ThemedButton {
                    theme: themeTokens; text: "APPLY COMPATIBLE ONLY"
                    commandEnabled: !!batchAxisDialog.preview && !!batchAxisDialog.preview.valid
                    onTriggered: {
                        if (backendObject.applyEditingAxisBatch(batchAxisDialog.axisIndex, "inverted",
                                invertAll.checked, "apply-compatible-only")) batchAxisDialog.close()
                    }
                }
            }
        }
    }

    DeviceDialog {
        id: createRigDialog
        objectName: "createRigDialog"
        modal: true; title: "Create Device Rig"
        anchors.centerIn: parent; width: Math.min(560, root.width - 48)
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 13
            Text { Layout.fillWidth: true; text: "Choose the physical devices that belong together. The default output combines them into one virtual controller."; wrapMode: Text.WordWrap; color: themeTokens.text }
            ThemedTextInput { id: rigName; theme: themeTokens; Layout.fillWidth: true; placeholderText: "Rig name, for example BF6 Flight Rig" }
            Repeater {
                id: controllerRepeater
                model: controllers
                delegate: ThemedCheckBox {
                    required property var modelData
                    theme: themeTokens
                    property string controllerId: modelData.id
                    visible: modelData.id !== "" && !modelData.ambiguous
                    text: modelData.name + (modelData.connected ? "  ·  Connected" : "  ·  Saved / Offline")
                    checked: !!modelData.selected
                }
            }
            Text { text: "You can mark accessories optional and choose advanced output routing after creation."; color: themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ThemedButton {
                    theme: themeTokens; text: "CREATE & VERIFY"
                    commandEnabled: rigName.text.trim().length > 0
                    onTriggered: {
                        let ids = []
                        for (let i = 0; i < controllerRepeater.count; ++i) {
                            const item = controllerRepeater.itemAt(i)
                            if (item && item.visible && item.checked) ids.push(item.controllerId)
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

    DeviceDialog {
        id: deleteRigDialog
        modal: true; title: "Delete Device Rig"
        anchors.centerIn: parent; width: Math.min(460, root.width - 48)
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.danger }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.text
                text: root.selectedRig ? "Delete \"" + root.selectedRig.name
                      + "\"? Saved devices and their mappings remain available, but this rig will stop routing and its active/editing context will be cleared." : "Delete this Device Rig?"
            }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: deleteRigDialog.close() }
                ThemedButton {
                    theme: themeTokens; text: "DELETE RIG"; tone: "danger"
                    onTriggered: {
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
