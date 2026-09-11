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
    property var actionFeedback: ({})
    signal verificationRequested(string rigId, string deviceId, string outputId)
    signal calibrationRequested(string deviceId)

    readonly property var rigs: backendObject ? backendObject.deviceRigs : []
    readonly property var controllers: backendObject ? backendObject.controllers : []
    readonly property bool trueEmpty: rigs.length === 0 && controllers.length === 0
    // Theme.qml uses typed colors while Legacy exposes its established tokens
    // as a compact map. Normalize here so every shared Devices surface is
    // equally valid in all four themes.
    property color readyColor: themeTokens.ready
    property color warningColor: themeTokens.warning
    property color dangerColor: themeTokens.danger
    property color mutedColor: themeTokens.textMuted
    property color panelRaisedColor: themeTokens.panelRaised

    function selectableControllerCount() {
        let count = 0
        for (let i = 0; i < controllers.length; ++i)
            if (!controllers[i].ambiguous && (controllers[i].id !== "" || controllers[i].directInputId !== "")) ++count
        return count
    }
    function connectedPhysicalControllers() {
        return controllers.filter(function(item) { return !!item.connected && !item.ambiguous && (item.id !== "" || item.directInputId !== "") })
    }
    function savedOfflinePhysicalControllers() {
        return controllers.filter(function(item) { return !item.connected && item.id !== "" && !item.ambiguous })
    }
    function availableRigMemberChoices() {
        return controllers.filter(function(item) {
            return item.id !== "" && !item.ambiguous && !root.rigHasMember(root.selectedRig, item.id)
        }).map(function(item) {
            return { id: item.id, name: item.name + (item.connected ? " · Connected" : " · Saved / Offline") }
        })
    }
    function showActionFeedback(result, fallbackTitle, fallbackMessage) {
        actionFeedbackDismissTimer.stop()
        actionFeedback = result && result.title ? result : ({ success: false, title: fallbackTitle, message: fallbackMessage })
        // Action banners are acknowledgements, not another health surface.
        // Persistent remediation belongs in Setup Health, where it has a
        // concrete cause and next step.
        if (!actionFeedback.inProgress && !actionFeedback.persistent)
            actionFeedbackDismissTimer.restart()
        return actionFeedback
    }
    function showTransientActionFeedback(result, fallbackTitle, fallbackMessage, durationMs) {
        const feedback = showActionFeedback(result, fallbackTitle, fallbackMessage)
        actionFeedbackDismissTimer.interval = durationMs > 0 ? durationMs : 5000
        actionFeedbackDismissTimer.restart()
        return feedback
    }
    function reportBooleanAction(succeeded, successTitle, successMessage, failureTitle, failureMessage) {
        return showActionFeedback({ success: !!succeeded,
            title: succeeded ? successTitle : failureTitle,
            message: succeeded ? successMessage : failureMessage })
    }
    Timer {
        id: actionFeedbackDismissTimer
        interval: 5000
        repeat: false
        onTriggered: root.actionFeedback = ({})
    }
    function createRigWithInputs(name, ids, outputId) {
        const result = backendObject ? backendObject.createDeviceRigResult(name, ids, outputId) : ({ success: false, title: "Device Rig was not created", message: "HOTAS BF6 is not ready." })
        showActionFeedback(result, "Device Rig was not created", "Refresh the page and try again.")
        if (result.success) {
            pickRig(result.objectId)
            if (result.nextAction === "setup") requestVerification(result.objectId, "")
        }
        return result
    }

    function rigFor(id) {
        for (let i = 0; i < rigs.length; ++i) if (rigs[i].id === id) return rigs[i]
        return rigs.length > 0 ? rigs[0] : null
    }
    readonly property var selectedRig: rigFor(selectedRigId)
    readonly property var configuredRig: rigFor(backendObject ? backendObject.activeDeviceRigId : "")
    function healthColor(key) {
        if (key === "ready") return readyColor
        if (key === "partial") return warningColor
        if (key === "disabled" || key === "offline") return mutedColor
        return dangerColor
    }
    function pickRig(id) {
        if (selectedRigId !== id && rigDetailsActions.visible) rigDetailsActions.close()
        selectedRigId = id
        if (backendObject) backendObject.setEditingDeviceContext(id, [])
    }
    function openDevice(id) { if (rigDetailsActions.visible) rigDetailsActions.close(); selectedDeviceId = id; physicalDeviceDialog.open() }
    function openOutput(id) { if (rigDetailsActions.visible) rigDetailsActions.close(); selectedOutputId = id; outputDetailDialog.open() }
    // The App Health shell sets the backend's editing context before this
    // page is loaded. Complete the deep link here, where the relevant
    // physical-device or Virtual Output detail surface actually exists.
    function focusIssueTarget(target) {
        const type = target && target.objectType !== undefined ? String(target.objectType) : ""
        const id = target && target.objectId !== undefined ? String(target.objectId) : ""
        if (!id) return false
        const rig = selectedRig
        if (type === "physicalDevice" && rig && rigHasMember(rig, id)) {
            openDevice(id)
            return true
        }
        if (type === "virtualOutput" && rig && hasOutput(rig, id)) {
            openOutput(id)
            return true
        }
        if (type === "gameVisibility" && rig) {
            if (rigHasMember(rig, id)) { openDevice(id); return true }
            if (hasOutput(rig, id)) { openOutput(id); return true }
        }
        return type === "deviceRig" && rig && rig.id === id
    }
    // Device Context owns the editing scope. Keep this helper deliberately
    // single-target: multi-device selection remains available only through
    // the explicit top-bar picker.
    function editThisDevice(id) {
        const rig = selectedRig
        if (!backendObject || !rig || !rigHasMember(rig, id)) {
            reportBooleanAction(false, "", "", "Could not edit this device", "Select a device that belongs to the current Device Rig.")
            return false
        }
        const updated = backendObject.setEditingDeviceContext(rig.id, [id])
        reportBooleanAction(updated, "Editing this device", "This device is now the active editing target.", "Could not change editing target", "Refresh the Device Rig and try again.")
        return updated
    }

    // Repeater delegates are visual children rather than QObject children of
    // this page. Keep this lookup in the component's lexical QML scope so the
    // presentation regression can activate the real EDIT THIS control.
    function rigMemberCardFor(deviceId) {
        for (let index = 0; index < rigMemberRepeater.count; ++index) {
            const card = rigMemberRepeater.itemAt(index)
            if (card && card.memberId === deviceId) return card
        }
        return null
    }
    function requestVerification(rigId, deviceId, outputId) {
        if (rigDetailsActions.visible) rigDetailsActions.close()
        // Opening the assistant is a short transition. The assistant owns
        // real verification progress, so this page cannot retain stale state.
        showTransientActionFeedback({ success: true, title: "Opening Setup Assistant", message: "HOTAS BF6 will check the selected setup there." }, "", "", 1200)
        verificationRequested(rigId || selectedRigId, deviceId || "", outputId || "")
    }
    function openStandaloneOutputCreator() {
        createOutputDialog.returnToRig = false
        createOutputDialog.returnToOutputInventory = true
        createOutputDialog.resetForOpen()
        createOutputDialog.open()
    }
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
        function onDeviceRigsChanged() {
            Qt.callLater(root.normalizeSelectionsAfterModelRefresh)
            if (rigDetailsActions.visible && !root.containsRig(root.selectedRigId)) rigDetailsActions.close()
        }
        function onControllersChanged() { Qt.callLater(root.normalizeSelectionsAfterModelRefresh) }
    }
    onVisibleChanged: if (!visible && rigDetailsActions.visible) rigDetailsActions.close()

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
        id: dialogShell
        standardButtons: Dialog.NoButton
        header: ThemedDialogHeader {
            theme: root.themeTokens
            legacy: root.legacy
            heading: parent.title
            dialog: dialogShell
        }
    }

    background: Rectangle { color: themeTokens.background }

    ScrollView {
        id: devicesScroll
        objectName: "devicesScroll"
        anchors.fill: parent
        clip: true
        property var contentLayout: null
        onWidthChanged: if (rigDetailsActions.visible) rigDetailsActions.deferReposition()
        onHeightChanged: if (rigDetailsActions.visible) rigDetailsActions.deferReposition()
        // Track the completed ColumnLayout rather than a guessed page height.
        // Member/output changes and resizes must extend the actual Flickable.
        contentWidth: width
        // Reserve a small end gutter as well as the measured layout height.
        // ScrollView's viewport can be a few pixels shorter than its control
        // during scrollbar/layout transitions; without this gutter the final
        // card's controls can land just below the reachable edge.
        contentHeight: contentLayout ? contentLayout.measuredHeight + 20 : 0

        ColumnLayout {
            objectName: "devicesContent"
            property real measuredHeight: 0
            function refreshMeasuredHeight() {
                measuredHeight = Math.max(implicitHeight, childrenRect.height)
            }
            Component.onCompleted: {
                devicesScroll.contentLayout = this
                refreshMeasuredHeight()
            }
            Component.onDestruction: if (devicesScroll.contentLayout === this) devicesScroll.contentLayout = null
            // Use the stable viewport width; availableWidth can change while
            // the vertical scrollbar is resolving measured content height.
            width: devicesScroll.width
            spacing: 16
            onChildrenRectChanged: refreshMeasuredHeight()
            onImplicitHeightChanged: refreshMeasuredHeight()

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
                ThemedButton { objectName: "openCreateRigButton"; theme: themeTokens; text: "+ CREATE RIG"; commandEnabled: root.selectableControllerCount() > 0
                    onTriggered: createRigDialog.open() }
            }

            ThemedActionFeedback {
                objectName: "deviceActionFeedback"
                Layout.fillWidth: true
                result: root.actionFeedback
                theme: root.themeTokens
                legacy: root.legacy
            }

            // First connection is deliberately concise: show the available
            // physical hardware before discussing a Rig that does not exist.
            Panel {
                objectName: "firstDeviceInputsPanel"
                Layout.fillWidth: true
                visible: !root.trueEmpty && rigs.length === 0 && controllers.length > 0
                implicitHeight: firstDeviceInputsContent.implicitHeight + 28
                ColumnLayout {
                    id: firstDeviceInputsContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 9
                    SmallLabel { text: "PHYSICAL INPUTS" }
                    Text { Layout.fillWidth: true; text: "Connected and saved controllers"; color: themeTokens.textStrong; font.pixelSize: 17; font.bold: true }
                    Flow {
                        Layout.fillWidth: true; spacing: 10
                        Repeater {
                            model: controllers
                            delegate: DevicePanel {
                                required property var modelData
                                theme: root.themeTokens; legacy: root.legacy
                                implicitWidth: Math.max(210, firstInputName.implicitWidth + 28)
                                implicitHeight: firstInputContent.implicitHeight + 18
                                color: root.legacy ? "#e9161d23" : themeTokens.panelRaised
                                border.color: modelData.connected ? themeTokens.ready : themeTokens.border
                                ColumnLayout {
                                    id: firstInputContent
                                    anchors.fill: parent; anchors.margins: 9; spacing: 3
                                    Text { id: firstInputName; width: 230; elide: Text.ElideRight; text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 12; font.bold: true }
                                    Text { text: modelData.state; color: themeTokens.textMuted; font.pixelSize: 10 }
                                    ThemedButton { theme: themeTokens; compact: true; tone: "secondary"; text: modelData.id === "" ? "SET UP" : "DETAILS"
                                        onTriggered: {
                                            if (modelData.id !== "") root.openDevice(modelData.id)
                                            else if (backendObject.selectNewController(modelData.directInputId)) root.requestVerification("", "")
                                            else root.showActionFeedback({ success: false, title: "Device setup could not start", message: "Refresh the connected controller and try again." })
                                        } }
                                }
                            }
                        }
                    }
                }
            }

            Panel {
                objectName: "trueEmptyDevicesPanel"
                Layout.fillWidth: true
                visible: root.trueEmpty
                implicitHeight: emptyDevicesContent.implicitHeight + 32
                border.color: themeTokens.borderStrong
                ColumnLayout {
                    id: emptyDevicesContent
                    anchors.fill: parent; anchors.margins: 16; spacing: 8
                    SmallLabel { text: "BUILD YOUR FIRST DEVICE RIG" }
                    Text { Layout.fillWidth: true; text: "No physical controllers detected"; color: themeTokens.textStrong; font.pixelSize: 20; font.bold: true }
                    Text { Layout.fillWidth: true; text: "Connect a controller to create your first Device Rig."; color: themeTokens.text; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        ThemedButton { objectName: "refreshDevicesButton"; theme: themeTokens; text: "REFRESH DEVICES"; tone: "secondary"
                            onTriggered: { backendObject.refreshControllers(); root.showTransientActionFeedback({ success: true, title: "Refreshing devices", message: "HOTAS BF6 is looking for connected physical controllers." }, "", "", 5000) } }
                        ThemedButton { objectName: "openStandaloneCreateOutputFromEmptyButton"; theme: themeTokens; text: "+ CREATE VIRTUAL OUTPUT"; tone: "secondary"
                            onTriggered: root.openStandaloneOutputCreator() }
                    }
                    Text { Layout.fillWidth: true; text: "No saved or connected physical controllers are available yet. You can still define a Virtual Output now and attach it to a Device Rig later."; color: themeTokens.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap }
                }
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
                visible: rigs.length > 0
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
                            SmallLabel { text: configuredRig && configuredRig.inUse ? "MAPPING ROUTE IN USE" : "CONFIGURED MAPPING ROUTE" }
                            Text {
                                text: configuredRig ? configuredRig.name : "No Device Rig configured"
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
                        text: configuredRig ? configuredRig.members.length + " inputs  →  " + configuredRig.outputs.length
                              + (configuredRig.outputs.length === 1 ? " virtual output" : " virtual outputs")
                                          : "Create a rig to keep a controller arrangement reusable across profiles."
                        color: themeTokens.text; font.pixelSize: 13
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: !root.trueEmpty
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
                            text: "No Device Rigs yet. Create a Rig when you want to group one or more inputs and route them to a Virtual Output."
                            color: themeTokens.textMuted; font.pixelSize: 12
                        }
                    }
                }

                Panel {
                    objectName: "rigDetailsPanel"
                    Layout.fillWidth: true
                    visible: selectedRig !== null
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
                                objectName: "activateRigButton"
                                theme: themeTokens
                                // Keep the header controls within the narrow Flight Deck viewport.
                                // The action still resolves the whole profile/rig/output route atomically.
                                visible: selectedRig && !selectedRig.configured; text: "ACTIVATE ROUTE"
                                commandEnabled: selectedRig && selectedRig.health !== "conflict" && selectedRig.enabled
                                onTriggered: { const rig = selectedRig; root.reportBooleanAction(rig && backendObject.activateDeviceRig(rig.id), "Compatible route activated", "Profile, Device Rig, and Virtual Output were selected together.", "Device Rig could not be activated", "Choose a compatible Profile and check its required controllers and output.") }
                            }
                            ThemedButton {
                                objectName: "checkRigSetupButton"
                                theme: themeTokens; tone: "secondary"
                                visible: selectedRig; text: "CHECK RIG SETUP"
                                onTriggered: root.requestVerification(selectedRig.id, "")
                            }
                            ThemedButton {
                                id: rigDetailsOverflowButton
                                objectName: "rigDetailsOverflowButton"
                                theme: themeTokens; tone: "secondary"; compact: true
                                visible: selectedRig; text: "…"
                                onTriggered: {
                                    if (rigDetailsActions.visible) rigDetailsActions.close()
                                    else {
                                        rigDetailsActions.open()
                                        Qt.callLater(function() {
                                            if (rigDetailsActions.visible) rigDetailsActions.reposition()
                                        })
                                    }
                                }
                            }
                        }
                        Text {
                            visible: !selectedRig; Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: "A rig answers what hardware you use together. Profiles remain separate and answer how that hardware behaves."
                            color: themeTokens.text; font.pixelSize: 13
                        }
                        Repeater {
                            id: rigMemberRepeater
                            objectName: "rigMemberRepeater"
                            visible: selectedRig !== null
                            model: selectedRig ? selectedRig.members : []
                            delegate: DevicePanel {
                                required property var modelData
                                objectName: "rigMemberCard"
                                property string memberId: modelData.id
                                property alias editControl: editThisControl
                                property alias visibilityControl: memberVisibilityControl
                                property bool editingTarget: !!modelData.editing
                                theme: root.themeTokens; legacy: root.legacy
                                Layout.fillWidth: true; implicitHeight: memberCardContent.implicitHeight + 22; radius: root.legacy ? 4 : themeTokens.controlRadius
                                // Do not recolour a LegacyAviationPanel into
                                // a raised Standard card.  Its layered base,
                                // top highlight and lower edge are part of
                                // the Legacy visual language.
                                color: editingTarget ? (root.legacy ? "#21414b" : themeTokens.selectionCurrent)
                                      : root.legacy ? "#e9161d23" : Qt.rgba(panelRaisedColor.r, panelRaisedColor.g, panelRaisedColor.b, 0.54)
                                border.color: editingTarget ? themeTokens.orange
                                      : modelData.ambiguous ? themeTokens.danger : modelData.connected ? themeTokens.ready : themeTokens.border
                                ColumnLayout {
                                    id: memberCardContent
                                    anchors.fill: parent; anchors.margins: 10; spacing: 7
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Rectangle { width: 7; height: 7; radius: 4; color: modelData.ambiguous ? themeTokens.danger : modelData.connected ? themeTokens.ready : themeTokens.textMuted }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 1
                                            Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 13; font.bold: true }
                                            Text { Layout.fillWidth: true; elide: Text.ElideRight; text: editingTarget ? "EDITING TARGET · " + (modelData.connected ? "Connected" : "Saved · Offline") : modelData.ambiguous ? "Selection required" : !modelData.verified ? "Needs verification" : modelData.connected ? "Connected · Verified" : modelData.required ? "Required · Offline" : "Optional · Offline"; color: editingTarget ? themeTokens.orange : themeTokens.textMuted; font.pixelSize: 10; font.bold: editingTarget }
                                        }
                                        ThemedButton { theme: themeTokens; text: "DETAILS"; compact: true; tone: "secondary"; onTriggered: root.openDevice(modelData.id) }
                                        ThemedButton { theme: themeTokens; text: "REMOVE"; compact: true; tone: "danger"; visible: selectedRig && selectedRig.members.length > 1; onTriggered: { const rigId = selectedRig ? selectedRig.id : ""; root.reportBooleanAction(!!rigId && backendObject.removeDeviceRigMember(rigId, modelData.id), "Physical controller removed", "The saved controller is no longer part of this Device Rig.", "Physical controller was not removed", "Refresh the Device Rig and try again.") } }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 10
                                        ThemedCheckBox { theme: themeTokens; text: modelData.required ? "Required" : "Optional"; checked: !!modelData.required; onToggled: function(value) { const rigId = selectedRig ? selectedRig.id : ""; root.reportBooleanAction(!!rigId && backendObject.setDeviceRigMemberRequired(rigId, modelData.id, value), "Controller requirement updated", value ? "This controller is required for the rig." : "This controller is optional and will not block setup when offline.", "Controller requirement was not updated", "Refresh the Device Rig and try again.") } }
                                        ThemedCheckBox { theme: themeTokens; text: "Enabled"; checked: !!modelData.enabled; onToggled: function(value) { const rigId = selectedRig ? selectedRig.id : ""; root.reportBooleanAction(!!rigId && backendObject.setDeviceRigMemberEnabled(rigId, modelData.id, value), "Controller state updated", value ? "This controller is included in the rig." : "This controller is excluded from the rig.", "Controller state was not updated", "Refresh the Device Rig and try again.") } }
                                        ThemedButton {
                                            id: editThisControl
                                            objectName: "editThisButton"
                                            property string deviceId: modelData.id
                                            theme: themeTokens; text: editingTarget ? "EDITING" : "EDIT THIS"; compact: true; tone: "secondary"
                                            commandEnabled: !editingTarget
                                            onTriggered: root.editThisDevice(modelData.id)
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 8
                                        SmallLabel { text: "GAME VISIBILITY" }
                                        Text {
                                            Layout.fillWidth: true
                                            text: !modelData.visibilityManaged ? "SETUP NEEDED"
                                                  : !modelData.visibilityKnown ? "VERIFY"
                                                  : modelData.hiddenFromGames ? "HIDDEN FROM GAMES" : "VISIBLE TO GAMES"
                                            color: !modelData.visibilityManaged || !modelData.visibilityKnown
                                                   ? themeTokens.warning
                                                   : modelData.hiddenFromGames ? themeTokens.ready : themeTokens.warning
                                            font.pixelSize: 10; font.bold: true; elide: Text.ElideRight
                                        }
                                        ThemedButton {
                                            id: memberVisibilityControl
                                            objectName: "memberVisibilityButton"
                                            theme: themeTokens; compact: true; tone: "secondary"
                                            text: modelData.hiddenFromGames ? "SHOW" : "HIDE"
                                            commandEnabled: !!modelData.enabled
                                            onTriggered: {
                                                const rig = selectedRig
                                                if (rig) visibilityConfirmationDialog.openForInputs(
                                                            rig.id, [modelData.id], !modelData.hiddenFromGames)
                                            }
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "Physical game visibility is controlled only here. Automatic profile selection never hides or shows this device."
                                        color: themeTokens.textMuted
                                        font.pixelSize: 9
                                        wrapMode: Text.WordWrap
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
                                            onActivated: function(index, value) { const rigId = selectedRig ? selectedRig.id : ""; root.reportBooleanAction(!!rigId && backendObject.setDeviceRigMemberOutput(rigId, modelData.id, value), "Controller output updated", "The controller now routes to the selected Virtual Output.", "Controller output was not updated", "Choose an available Virtual Output and try again.") }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                        Rectangle { visible: selectedRig !== null; Layout.fillWidth: true; height: 1; color: themeTokens.divider }
                        ColumnLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true; spacing: 8
                            SmallLabel { text: "RIG VIRTUAL OUTPUTS" }
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
                                        ThemedCheckBox { theme: themeTokens; text: "Use"; checked: !!modelData.enabled; onToggled: function(value) { const rigId = selectedRig ? selectedRig.id : ""; root.reportBooleanAction(!!rigId && backendObject.setDeviceRigOutputEnabled(rigId, modelData.id, value), "Virtual Output state updated", value ? "This Virtual Output is included in the rig." : "This Virtual Output is excluded from the rig.", "Virtual Output state was not updated", "Refresh the Device Rig and try again.") } }
                                        Text { text: !modelData.visibilityManaged ? "VISIBLE" : modelData.hiddenFromGames ? "HIDDEN" : "VISIBLE"; color: !modelData.visibilityManaged || !modelData.visibilityKnown ? themeTokens.textMuted : modelData.hiddenFromGames ? themeTokens.warning : themeTokens.ready; font.pixelSize: 9; font.bold: true }
                                        ThemedButton { theme: themeTokens; text: "DETAILS"; compact: true; tone: "secondary"; onTriggered: root.openOutput(modelData.id) }
                                        ThemedButton { visible: selectedRig && selectedRig.outputs.length > 1; theme: themeTokens; text: "REMOVE"; compact: true; tone: "danger"; onTriggered: { const rigId = selectedRig ? selectedRig.id : ""; root.reportBooleanAction(!!rigId && backendObject.removeDeviceRigOutput(rigId, modelData.id), "Virtual Output removed", "The output is no longer part of this Device Rig.", "Virtual Output was not removed", "Refresh the Device Rig and try again.") } }
                                    }
                                }
                            }
                            RowLayout { Layout.fillWidth: true
                                ThemedButton { objectName: "addOutputToRigButton"; theme: themeTokens; text: "+ ADD OUTPUT"; tone: "secondary"; onTriggered: addOutputDialog.open() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                        RowLayout {
                            visible: selectedRig !== null; Layout.fillWidth: true; spacing: 8
                            ThemedButton { objectName: "addInputToRigButton"; theme: themeTokens; text: "+ ADD INPUT DEVICE"; tone: "secondary"; onTriggered: addMemberDialog.open() }
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
                            ThemedCheckBox { theme: themeTokens; text: "Enabled"; checked: selectedRig ? selectedRig.enabled : false; onToggled: function(value) { const rig = selectedRig; root.reportBooleanAction(rig && backendObject.setDeviceRigEnabled(rig.id, value), "Device Rig state updated", value ? "This rig is enabled." : "This rig is disabled.", "Device Rig state was not updated", "Refresh the Device Rig and try again.") } }
                            ThemedCheckBox { theme: themeTokens; text: "Auto activate"; checked: selectedRig ? selectedRig.autoActivate : false; onToggled: function(value) { const rig = selectedRig; root.reportBooleanAction(rig && backendObject.setDeviceRigAutoActivate(rig.id, value), "Automatic activation updated", value ? "HOTAS BF6 can select this rig when its devices are available." : "This rig will not activate automatically.", "Automatic activation was not updated", "Refresh the Device Rig and try again.") } }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            SmallLabel { text: "DISCONNECT" }
                            ThemedComboBox {
                                theme: themeTokens; Layout.preferredWidth: 220
                                model: ["Suspend affected routes", "Deactivate rig", "Use fallback rig"]
                                currentIndex: selectedRig ? Number(selectedRig.disconnectBehavior) : 0
                                onActivated: function(index) { const rig = selectedRig; root.reportBooleanAction(rig && backendObject.setDeviceRigDisconnectBehavior(rig.id, index), "Disconnect behavior updated", "The Device Rig will use the selected behavior when a controller disconnects.", "Disconnect behavior was not updated", "Refresh the Device Rig and try again.") }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            SmallLabel { text: "PRIORITY" }
                            ThemedStepper { theme: themeTokens; value: selectedRig ? Number(selectedRig.activationPriority) : 50; from: 0; to: 100; onValueModified: function(value) { const rig = selectedRig; root.reportBooleanAction(rig && backendObject.setDeviceRigActivationPriority(rig.id, value), "Activation priority updated", "The Device Rig priority was saved.", "Activation priority was not updated", "Refresh the Device Rig and try again.") } }
                            SmallLabel { text: "FALLBACK" }
                            ThemedComboBox {
                                id: fallbackPicker; theme: themeTokens; Layout.preferredWidth: 210
                                model: [{ id: "", name: "No fallback" }].concat(rigs.filter(function(item) { return selectedRig && item.id !== selectedRig.id }))
                                textRole: "name"; valueRole: "id"
                                currentIndex: root.indexFor(model, selectedRig ? selectedRig.fallbackRigId : "")
                                onActivated: function(index, value) { const rig = selectedRig; root.reportBooleanAction(rig && backendObject.setDeviceRigFallback(rig.id, value), "Fallback Device Rig updated", value ? "A fallback rig was selected." : "No fallback rig is selected.", "Fallback Device Rig was not updated", "Refresh the Device Rig and try again.") }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            Panel {
                objectName: "knownDevicesPanel"
                Layout.fillWidth: true; implicitHeight: knownDevicesContent.implicitHeight + 28
                visible: !root.trueEmpty && rigs.length > 0
                ColumnLayout {
                    id: knownDevicesContent
                    anchors.fill: parent; anchors.margins: 14
                    SmallLabel { text: "PHYSICAL INPUTS" }
                    Flow {
                        Layout.fillWidth: true; spacing: 10
                        Repeater {
                            model: controllers
                            delegate: DevicePanel {
                                required property var modelData
                                theme: root.themeTokens; legacy: root.legacy
                                implicitWidth: Math.max(190, deviceName.implicitWidth + 28); implicitHeight: modelData.id === "" && modelData.connected ? 82 : 52
                                radius: themeTokens.controlRadius; color: root.legacy ? "#e9161d23" : themeTokens.panelRaised; border.color: modelData.connected ? themeTokens.ready : themeTokens.border
                                ColumnLayout { anchors.fill: parent; anchors.margins: 9; spacing: 2
                                    Text { id: deviceName; text: modelData.name; color: themeTokens.textStrong; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: 210 }
                                    Text { text: modelData.state; color: themeTokens.textMuted; font.pixelSize: 9 }
                                    ThemedButton { theme: themeTokens; visible: modelData.id === "" && modelData.connected; text: "SET UP"; compact: true; emphasis: "ready"
                                        onTriggered: { if (backendObject.selectNewController(modelData.directInputId)) root.requestVerification("", "")
                                                       else root.showActionFeedback({ success: false, title: "Device setup could not start", message: "Refresh the connected controller and try again." }) } }
                                }
                                MouseArea { anchors.fill: parent; visible: modelData.id !== ""; onClicked: root.openDevice(modelData.id) }
                            }
                        }
                        Text { visible: controllers.length === 0; text: "Connect a controller to begin."; color: themeTokens.textMuted; font.pixelSize: 12 }
                    }
                }
            }

            Panel {
                objectName: "virtualInputsPanel"
                Layout.fillWidth: true
                visible: !root.trueEmpty
                implicitHeight: virtualInputsContent.implicitHeight + 28
                ColumnLayout {
                    id: virtualInputsContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        SmallLabel { text: "VIRTUAL INPUTS" }
                        Item { Layout.fillWidth: true }
                        ThemedButton { objectName: "openAddVirtualInputButton"; theme: themeTokens; text: "+ ADD VIRTUAL INPUT"; tone: "secondary"; onTriggered: virtualInputDialog.open() }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted; font.pixelSize: 11
                        text: "No compatible virtual input sources are currently available. vJoy devices are intentionally excluded as inputs so HOTAS BF6 cannot route one of its outputs back into itself." }
                }
            }

            Panel {
                objectName: "virtualOutputsInventoryPanel"
                Layout.fillWidth: true
                visible: !root.trueEmpty
                implicitHeight: virtualOutputsInventoryContent.implicitHeight + 28
                ColumnLayout {
                    id: virtualOutputsInventoryContent
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        SmallLabel { text: "VIRTUAL OUTPUTS" }
                        Item { Layout.fillWidth: true }
                        ThemedButton { objectName: "openStandaloneCreateOutputButton"; theme: themeTokens; text: "+ ADD VIRTUAL OUTPUT"; tone: "secondary"; onTriggered: root.openStandaloneOutputCreator() }
                    }
                    Repeater {
                        model: backendObject ? backendObject.virtualOutputLayouts : []
                        delegate: DevicePanel {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 64
                            theme: root.themeTokens; legacy: root.legacy
                            color: root.legacy ? "#e9161d23" : themeTokens.panelRaised
                            border.color: modelData.ready ? themeTokens.ready : modelData.readinessState === "SAVED" ? themeTokens.border : themeTokens.warning
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 9; spacing: 8
                                Rectangle { width: 7; height: 7; radius: 4; color: modelData.ready ? themeTokens.ready : modelData.readinessState === "SAVED" ? themeTokens.textMuted : themeTokens.warning }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 1
                                    Text { Layout.fillWidth: true; text: modelData.name + "  ·  vJoy " + modelData.deviceId; color: themeTokens.textStrong; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight }
                                    Text { Layout.fillWidth: true; text: (modelData.readinessState || (modelData.ready ? "READY" : "SAVED")) + " · " + (modelData.status || "Output status unavailable") + "  ·  " + (modelData.axes || "No axes") + "  ·  " + (modelData.profileCount || 0) + " profile" + (modelData.profileCount === 1 ? "" : "s"); color: themeTokens.textMuted; font.pixelSize: 10; elide: Text.ElideRight }
                                }
                                ThemedButton { theme: themeTokens; compact: true; text: "DETAILS"; tone: "secondary"; onTriggered: root.openOutput(modelData.id) }
                            }
                        }
                    }
                    Text { visible: !backendObject || backendObject.virtualOutputLayouts.length === 0; Layout.fillWidth: true; text: "No saved Virtual Outputs yet. Add one when you are ready to route a Device Rig to a vJoy controller."; color: themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                }
            }

        }
    }

    // A menu popup is intentionally non-modal, but its dismiss layer owns an
    // outside click.  Qt's CloseOnPressOutside runs before the ellipsis
    // receives its press, which otherwise turns a close click into a reopen.
    // Keeping this layer beneath the popup makes a trigger click and every
    // other outside click close exactly once, while menu actions remain live.
    MouseArea {
        id: rigDetailsActionsDismissArea
        objectName: "rigDetailsActionsDismissArea"
        parent: root
        anchors.fill: parent
        visible: rigDetailsActions.visible
        z: 100
        onClicked: rigDetailsActions.close()
    }

    Popup {
        id: rigDetailsActions
        objectName: "rigDetailsActionsPopup"
        parent: root
        z: 101
        focus: true
        width: 210; padding: 8
        // The popup and dismiss layer are page children rather than Flickable
        // content, so controls stay interactable while the page scrolls.
        function reposition() {
            const contentFlickable = devicesScroll ? devicesScroll.contentItem : null
            if (!rigDetailsOverflowButton || !parent || !contentFlickable) return
            const rootOrigin = root.mapToGlobal(0, 0)
            const trailingEdge = rigDetailsOverflowButton.mapToGlobal(
                rigDetailsOverflowButton.width, 0).x - rootOrigin.x
            x = Math.max(8, Math.min(parent.width - width - 8, trailingEdge - width))
            const below = rigDetailsOverflowButton.mapToGlobal(0,
                rigDetailsOverflowButton.height + 6).y - rootOrigin.y
            const above = rigDetailsOverflowButton.mapToGlobal(0, -height - 6).y - rootOrigin.y
            y = below + height <= parent.height - 8 ? below : Math.max(8, above)
        }
        function deferReposition() {
            Qt.callLater(function() {
                if (rigDetailsActions.visible) rigDetailsActions.reposition()
            })
        }
        onOpened: deferReposition()
        // A Popup lives outside Flickable layout. While it is open, refresh
        // its anchor through live resize and scroll settling; this timer is
        // UI-only and stops as soon as the small overflow menu closes.
        Timer {
            interval: 10
            repeat: true
            running: rigDetailsActions.visible
            onTriggered: rigDetailsActions.reposition()
        }
        closePolicy: Popup.CloseOnEscape
        // The overflow is part of the Devices presentation system, not a
        // generic Qt popup.  In particular, Legacy needs the same layered
        // surface construction as its established cards and dialogs.
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 5
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: selectedRig && selectedRig.default ? "CLEAR DEFAULT" : "SET DEFAULT"; tone: "secondary"; onTriggered: { const rig = selectedRig; rigDetailsActions.close(); const changed = rig && (rig.default ? backendObject.clearDefaultDeviceRig(rig.id) : backendObject.setDefaultDeviceRig(rig.id)); root.reportBooleanAction(changed, rig && rig.default ? "Default Device Rig cleared" : "Default Device Rig set", "The default Device Rig was saved.", "Default Device Rig was not updated", "Refresh the Device Rig and try again.") } }
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: "RENAME RIG"; tone: "secondary"; onTriggered: { rigNameField.text = selectedRig ? selectedRig.name : ""; rigDetailsActions.close(); renameRigDialog.open() } }
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: selectedRig && selectedRig.enabled ? "DISABLE RIG" : "ENABLE RIG"; tone: "secondary"; onTriggered: { const rig = selectedRig; rigDetailsActions.close(); root.reportBooleanAction(rig && backendObject.setDeviceRigEnabled(rig.id, !rig.enabled), "Device Rig state updated", rig && rig.enabled ? "This rig is disabled." : "This rig is enabled.", "Device Rig state was not updated", "Refresh the Device Rig and try again.") } }
            ThemedButton { theme: themeTokens; Layout.fillWidth: true; text: "DELETE RIG"; tone: "danger"; onTriggered: { rigDetailsActions.close(); deleteRigDialog.open() } }
        }
    }

    DeviceDialog {
        id: visibilityConfirmationDialog
        objectName: "visibilityConfirmationDialog"
        modal: true; title: "Confirm Game Visibility"
        anchors.centerIn: parent; width: Math.min(520, root.width - 48)
        property string rigId: ""
        property var ids: []
        property bool inputs: true
        property bool targetHidden: true
        function openForInputs(targetRigId, targetIds, hidden) {
            rigId = targetRigId; ids = targetIds; inputs = true; targetHidden = hidden; open()
        }
        function openForOutputs(targetRigId, targetIds, visible) {
            rigId = targetRigId; ids = targetIds; inputs = false; targetHidden = !visible; open()
        }
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.text
                text: visibilityConfirmationDialog.inputs
                    ? (visibilityConfirmationDialog.targetHidden
                       ? "Hide the selected physical controllers from games? HOTAS BF6 will keep reading them, change only the controllers you selected, and leave unrelated devices alone."
                       : "Show the selected physical controllers to games? HOTAS BF6 will change only the controllers you selected.")
                    : (visibilityConfirmationDialog.targetHidden
                       ? "Hide the selected inactive virtual controllers from games? HOTAS BF6 will change only the virtual controllers you selected."
                       : "Show the selected virtual outputs to games? Active outputs are kept visible for game binding.") }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted; font.pixelSize: 10
                text: "HOTAS BF6 checks the result before it reports success. Technical Details contains the exact driver evidence." }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: visibilityConfirmationDialog.close() }
                ThemedButton { objectName: "visibilityApplyButton"; theme: themeTokens; text: "APPLY"; emphasis: "warning"
                    commandEnabled: visibilityConfirmationDialog.rigId !== "" && visibilityConfirmationDialog.ids.length > 0
                    onTriggered: {
                        const okay = visibilityConfirmationDialog.inputs
                            ? backendObject.setDeviceRigInputVisibility(visibilityConfirmationDialog.rigId, visibilityConfirmationDialog.ids, visibilityConfirmationDialog.targetHidden)
                            : backendObject.setDeviceRigOutputVisibility(visibilityConfirmationDialog.rigId, visibilityConfirmationDialog.ids, !visibilityConfirmationDialog.targetHidden)
                        root.reportBooleanAction(okay, visibilityConfirmationDialog.targetHidden ? "Game visibility updated" : "Game visibility updated", visibilityConfirmationDialog.targetHidden ? "The selected physical controllers are now hidden from games." : "The selected devices are now visible to games.", "Game visibility was not updated", "HOTAS BF6 could not change the selected device visibility. Check Setup for details.")
                        if (okay) visibilityConfirmationDialog.close()
                    } }
            }
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
            ThemedComboBox { id: memberPicker; theme: themeTokens; Layout.fillWidth: true; model: root.availableRigMemberChoices(); textRole: "name"; valueRole: "id" }
            ThemedCheckBox { id: addMemberOptional; theme: themeTokens; text: "Optional accessory"; checked: false }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: addMemberDialog.close() }
                ThemedButton { theme: themeTokens; text: "ADD INPUT"; commandEnabled: selectedRig && memberPicker.currentValue; onTriggered: { const rig = selectedRig; const id = memberPicker.currentValue; const added = rig && id && backendObject.addDeviceRigMember(rig.id, id, !addMemberOptional.checked); root.reportBooleanAction(added, "Physical controller added", "The controller was added to this Device Rig.", "Physical controller was not added", "Choose an available saved controller and try again."); if (added) addMemberDialog.close() } }
            }
        }
    }

    DeviceDialog {
        id: virtualInputDialog
        objectName: "addVirtualInputDialog"
        modal: true; title: "Add Virtual Input"
        anchors.centerIn: parent; width: Math.min(500, Math.max(340, root.width - 48))
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            SmallLabel { text: "SUPPORTED INPUT SOURCES" }
            Text { Layout.fillWidth: true; text: "No compatible virtual controllers are available to add."; color: themeTokens.textStrong; font.pixelSize: 16; font.bold: true; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; text: "HOTAS BF6 currently accepts physical DirectInput controllers as mapper sources. vJoy is excluded here because accepting a HOTAS BF6-owned output as an input could create a feedback loop."; color: themeTokens.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; text: "Routing loop not allowed: a virtual output can never become its own input, directly or through a Device Rig."; color: themeTokens.warning; font.pixelSize: 10; wrapMode: Text.WordWrap }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CLOSE"; tone: "secondary"; onTriggered: virtualInputDialog.close() }
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
            SmallLabel { text: "EXISTING OUTPUTS" }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted
                text: "Choose an existing compatible virtual output layout for this rig. Each enabled output is verified independently." }
            ThemedComboBox { id: outputPicker; theme: themeTokens; Layout.fillWidth: true
                model: backendObject ? backendObject.virtualOutputLayouts : []; textRole: "name"; valueRole: "id" }
            Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
            SmallLabel { text: "CREATE NEW VIRTUAL OUTPUT" }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted; font.pixelSize: 10
                text: "Create another virtual controller here. HOTAS BF6 saves its capability settings, then Check Setup explains any driver configuration it still needs." }
            ThemedButton { objectName: "openCreateOutputButton"; theme: themeTokens; Layout.fillWidth: true; text: "+ CREATE NEW VJOY DEVICE"; tone: "secondary"
                onTriggered: { addOutputDialog.close(); createOutputDialog.returnToRig = false; createOutputDialog.returnToOutputInventory = false; createOutputDialog.resetForOpen(); createOutputDialog.open() } }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "CANCEL"; tone: "secondary"; onTriggered: addOutputDialog.close() }
                ThemedButton { theme: themeTokens; text: "ADD OUTPUT"; commandEnabled: selectedRig && outputPicker.currentValue
                    onTriggered: { const rig = selectedRig; const added = rig && backendObject.addDeviceRigOutput(rig.id, outputPicker.currentValue); root.reportBooleanAction(added, "Virtual Output added", "The output was added to this Device Rig.", "Virtual Output was not added", "Choose an available Virtual Output and try again."); if (added) addOutputDialog.close() } }
            }
        }
    }

    DeviceDialog {
        id: createOutputDialog
        objectName: "createOutputDialog"
        modal: true; title: "Create Virtual Output"
        anchors.centerIn: parent; width: Math.min(520, root.width - 48)
        property int selectedDeviceId: 0
        property bool returnToRig: false
        property bool returnToOutputInventory: false
        property string mode: "match-physical"
        property var selectedAxes: [1, 2, 3, 6]
        property var axisOptions: [
            { id: 1, name: "X" }, { id: 2, name: "Y" }, { id: 3, name: "Z" }, { id: 4, name: "Rx" },
            { id: 5, name: "Ry" }, { id: 6, name: "Rz" }, { id: 7, name: "Slider 0" }, { id: 8, name: "Slider 1" }
        ]
        property int customButtons: 32
        property int customContinuousPovs: 0
        property int customDiscretePovs: 0
        function physicalChoices() { return controllers.filter(function(item) { return !item.ambiguous && (item.id !== "" || item.directInputId !== "") }).map(function(item) { return { key: item.id || item.directInputId, name: item.name, axisCount: item.axisCount, buttonCount: item.buttonCount, povCount: item.povCount } }) }
        function sourceKey(item) { return item ? (item.key || item.id || item.directInputId || "") : "" }
        function axisEnabled(axis) { return selectedAxes.indexOf(axis) >= 0 }
        function setAxisEnabled(axis, enabled) {
            const next = selectedAxes.filter(function(entry) { return entry !== axis })
            if (enabled) next.push(axis)
            selectedAxes = next
        }
        function selectedSource() {
            return mode === "match-physical" ? matchPhysicalPicker.currentValue : mode === "copy-output" ? copyOutputPicker.currentValue : ""
        }
        function capabilitySummary() {
            if (mode === "match-physical") {
                const choices = physicalChoices()
                for (let i = 0; i < choices.length; ++i) if (sourceKey(choices[i]) === selectedSource())
                    return choices[i].name + " · " + choices[i].axisCount + " axes · " + choices[i].buttonCount + " buttons · " + choices[i].povCount + " continuous POV"
                return "Choose a physical controller to review its capabilities."
            }
            if (mode === "copy-output") {
                const choices = backendObject ? backendObject.virtualOutputLayouts : []
                for (let i = 0; i < choices.length; ++i) if (choices[i].id === selectedSource())
                    return "Copy " + choices[i].name + " → vJoy Device " + selectedDeviceId + " · " + choices[i].axes + " · " + choices[i].buttons + " buttons · " + choices[i].continuousPovs + " continuous POV · " + choices[i].discretePovs + " discrete POV"
                return "Choose a saved Virtual Output to copy."
            }
            return selectedAxes.length + " axes · " + customButtons + " buttons · " + customContinuousPovs + " continuous POV · " + customDiscretePovs + " discrete POV"
        }
        function resetForOpen() {
            outputCreateName.text = ""
            selectedDeviceId = backendObject ? backendObject.suggestedVirtualOutputDeviceId() : 2
            mode = "match-physical"
            selectedAxes = [1, 2, 3, 6]
            customButtons = 32
            customContinuousPovs = 0
            customDiscretePovs = 0
        }
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.text
                text: "Choose how this Virtual Output should be configured. The saved output can be reused by any rig or profile." }
            SmallLabel { text: "NAME" }
            ThemedTextInput { id: outputCreateName; theme: themeTokens; Layout.fillWidth: true; placeholderText: "Secondary Flight Output" }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                SmallLabel { text: "VJOY DEVICE" }
                ThemedStepper { theme: themeTokens; value: createOutputDialog.selectedDeviceId; from: 1; to: 16
                    onValueModified: function(value) { createOutputDialog.selectedDeviceId = value } }
                Text { Layout.fillWidth: true; text: createOutputDialog.selectedDeviceId > 0 ? "Suggested next available ID" : "No unused vJoy device IDs"; color: createOutputDialog.selectedDeviceId > 0 ? themeTokens.ready : themeTokens.warning; font.pixelSize: 10 }
            }
            SmallLabel { text: "CONFIGURATION MODE" }
            Flow { objectName: "outputConfigurationModes"; Layout.fillWidth: true; spacing: 6
                ThemedButton { theme: themeTokens; width: 160; text: "MATCH PHYSICAL DEVICE"; tone: createOutputDialog.mode === "match-physical" ? "primary" : "secondary"; onTriggered: createOutputDialog.mode = "match-physical" }
                ThemedButton { theme: themeTokens; width: 150; text: "COPY VJOY OUTPUT"; tone: createOutputDialog.mode === "copy-output" ? "primary" : "secondary"; onTriggered: createOutputDialog.mode = "copy-output" }
                ThemedButton { theme: themeTokens; width: 100; text: "CUSTOM"; tone: createOutputDialog.mode === "custom" ? "primary" : "secondary"; onTriggered: createOutputDialog.mode = "custom" }
            }
            ColumnLayout { Layout.fillWidth: true; spacing: 7; visible: createOutputDialog.mode === "match-physical"
                SmallLabel { text: "PHYSICAL CONTROLLER" }
                ThemedComboBox { id: matchPhysicalPicker; theme: themeTokens; Layout.fillWidth: true; model: createOutputDialog.physicalChoices(); textRole: "name"; valueRole: "key" }
                Text { Layout.fillWidth: true; text: "HOTAS BF6 will propose axes, buttons, and continuous POVs from this saved or connected physical controller."; color: themeTokens.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }
            ColumnLayout { Layout.fillWidth: true; spacing: 7; visible: createOutputDialog.mode === "copy-output"
                SmallLabel { text: "EXISTING VIRTUAL OUTPUT" }
                ThemedComboBox { id: copyOutputPicker; theme: themeTokens; Layout.fillWidth: true; model: backendObject ? backendObject.virtualOutputLayouts : []; textRole: "name"; valueRole: "id" }
                Text { Layout.fillWidth: true; text: "This creates a new saved output with the same capability configuration, not another reference to the same vJoy device."; color: themeTokens.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }
            ColumnLayout { Layout.fillWidth: true; spacing: 7; visible: createOutputDialog.mode === "custom"
                SmallLabel { text: "AXES" }
                Flow { Layout.fillWidth: true; spacing: 8
                    Repeater { model: createOutputDialog.axisOptions
                        delegate: ThemedCheckBox { required property var modelData; theme: themeTokens; text: modelData.name; checked: createOutputDialog.axisEnabled(modelData.id); onToggled: function(value) { createOutputDialog.setAxisEnabled(modelData.id, value) } }
                    }
                }
                RowLayout { Layout.fillWidth: true
                    SmallLabel { Layout.preferredWidth: 150; text: "BUTTON CAPACITY" }
                    Item { Layout.fillWidth: true }
                    ThemedStepper { objectName: "customButtonCapacityStepper"; theme: themeTokens; value: createOutputDialog.customButtons; from: 0; to: 128; onValueModified: function(value) { createOutputDialog.customButtons = value } }
                }
                RowLayout { Layout.fillWidth: true
                    SmallLabel { Layout.preferredWidth: 150; text: "CONTINUOUS POVS" }
                    Item { Layout.fillWidth: true }
                    ThemedStepper { objectName: "customContinuousPovsStepper"; theme: themeTokens; value: createOutputDialog.customContinuousPovs; from: 0; to: 4; onValueModified: function(value) { createOutputDialog.customContinuousPovs = value; if (value > 0) createOutputDialog.customDiscretePovs = 0 } }
                }
                RowLayout { Layout.fillWidth: true
                    SmallLabel { Layout.preferredWidth: 150; text: "DISCRETE POVS" }
                    Item { Layout.fillWidth: true }
                    ThemedStepper { objectName: "customDiscretePovsStepper"; theme: themeTokens; value: createOutputDialog.customDiscretePovs; from: 0; to: 4; onValueModified: function(value) { createOutputDialog.customDiscretePovs = value; if (value > 0) createOutputDialog.customContinuousPovs = 0 } }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: outputSummary.implicitHeight + 18; color: root.legacy ? "#e9161d23" : themeTokens.panelInset; border.color: themeTokens.border
                Text { id: outputSummary; anchors.fill: parent; anchors.margins: 9; text: "CAPABILITY SUMMARY\n" + createOutputDialog.capabilitySummary(); color: themeTokens.text; font.pixelSize: 10; wrapMode: Text.WordWrap }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted; font.pixelSize: 10
                text: "Creating this saved layout does not change the vJoy driver. Check Setup will explain any required driver configuration or unavailable device." }
            RowLayout {
                Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ThemedButton { theme: themeTokens; text: "BACK"; tone: "secondary"
                    onTriggered: { createOutputDialog.close(); if (createOutputDialog.returnToRig) createRigDialog.open(); else if (!createOutputDialog.returnToOutputInventory) addOutputDialog.open() } }
                ThemedButton { objectName: "createOutputButton"; theme: themeTokens; text: "CREATE OUTPUT"; emphasis: "ready"
                    commandEnabled: outputCreateName.text.trim().length > 0 && createOutputDialog.selectedDeviceId > 0 && (createOutputDialog.mode === "custom" ? createOutputDialog.selectedAxes.length > 0 : createOutputDialog.selectedSource() !== "")
                    onTriggered: {
                        const result = backendObject.createVirtualOutputLayoutResult(outputCreateName.text, createOutputDialog.selectedDeviceId, createOutputDialog.mode, createOutputDialog.selectedSource(), createOutputDialog.selectedAxes, createOutputDialog.customButtons, createOutputDialog.customContinuousPovs, createOutputDialog.customDiscretePovs)
                        root.showActionFeedback(result, "Virtual Output was not created", "Check the selected capabilities and try again.")
                        const created = result.success ? result.objectId : ""
                        const rig = selectedRig
                        if (created !== "" && createOutputDialog.returnToRig) {
                            createRigDialog.outputLayoutId = created
                            createOutputDialog.returnToRig = false
                            createOutputDialog.close()
                            createRigDialog.open()
                        } else if (created !== "" && rig) {
                            if (backendObject.addDeviceRigOutput(rig.id, created)) {
                                root.selectedOutputId = created
                                createOutputDialog.close()
                                addOutputDialog.open()
                            } else root.showActionFeedback({ success: false, title: "Virtual Output was created but not added", message: "Select it from Add Output and try again." })
                        } else if (created !== "") { root.selectedOutputId = created; createOutputDialog.close() }
                    } }
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
                Text { text: physicalDeviceDialog.detail.calibrationStatus || "Using default controller range"; color: themeTokens.text }
                SmallLabel { text: "ACTIVITY" }
                Text { text: physicalDeviceDialog.detail.activityStatus || "Listening for controller input…"; color: physicalDeviceDialog.detail.inputDetected ? themeTokens.ready : themeTokens.textMuted }
                SmallLabel { text: "LAST SEEN" }
                Text { text: physicalDeviceDialog.detail.lastSeen || "No recorded use yet"; color: themeTokens.text }
                SmallLabel { text: "VERIFICATION" }
                Text { text: physicalDeviceDialog.detail.lastVerified || "Needs setup verification"; color: physicalDeviceDialog.detail.verified ? themeTokens.ready : themeTokens.warning }
                SmallLabel { text: "RIGS" }
                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: physicalDeviceDialog.detail.rigs || "Not assigned to a Device Rig"; color: themeTokens.text }
                SmallLabel { text: "MAPPINGS" }
                Text { text: (physicalDeviceDialog.detail.mappedAxes || 0) + " axes · " + (physicalDeviceDialog.detail.mappedButtons || 0) + " buttons · " + (physicalDeviceDialog.detail.mappedPovs || 0) + " POV routes"; color: themeTokens.text }
                SmallLabel { text: "GAME VISIBILITY" }
                Text {
                    text: !physicalDeviceDialog.detail.managedVisibility ? "Set up game visibility"
                          : !physicalDeviceDialog.detail.visibilityKnown ? "Verify current state"
                          : physicalDeviceDialog.detail.hiddenFromGames ? "Hidden from games" : "Visible to games"
                    color: !physicalDeviceDialog.detail.managedVisibility || !physicalDeviceDialog.detail.visibilityKnown
                           ? themeTokens.warning
                           : physicalDeviceDialog.detail.hiddenFromGames ? themeTokens.ready : themeTokens.warning
                }
            }
            Text { Layout.fillWidth: true; visible: !physicalDeviceDialog.detail.connected; text: "Connect " + (physicalDeviceDialog.detail.name || "this controller") + " to calibrate it."; color: themeTokens.textMuted; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
            Text { text: "TECHNICAL DETAILS"; color: themeTokens.textMuted; font.pixelSize: 10; font.bold: true }
            Text { Layout.fillWidth: true; text: physicalDeviceDialog.detail.hidInstanceId || physicalDeviceDialog.detail.directInputId || "No current raw identity"; color: themeTokens.textMuted; font.pixelSize: 10; elide: Text.ElideMiddle }
            RowLayout { Layout.fillWidth: true
                ThemedButton { theme: themeTokens; text: "CHECK DEVICE SETUP"; tone: "secondary"
                    commandEnabled: !!backendObject
                    onTriggered: { root.requestVerification(root.selectedRigId, root.selectedDeviceId); physicalDeviceDialog.close() } }
                ThemedButton { theme: themeTokens; text: (physicalDeviceDialog.detail.calibratedAxes || 0) > 0 ? "RECALIBRATE" : "CALIBRATE"; tone: "secondary"
                    commandEnabled: !!backendObject && !!physicalDeviceDialog.detail.connected
                    onTriggered: { root.calibrationRequested(root.selectedDeviceId); physicalDeviceDialog.close() } }
                ThemedButton {
                    theme: themeTokens; tone: "secondary"
                    visible: root.selectedRig && root.rigHasMember(root.selectedRig, root.selectedDeviceId)
                    text: physicalDeviceDialog.detail.hiddenFromGames ? "SHOW TO GAMES" : "HIDE FROM GAMES"
                    onTriggered: visibilityConfirmationDialog.openForInputs(root.selectedRigId, [root.selectedDeviceId], !physicalDeviceDialog.detail.hiddenFromGames)
                }
                Item { Layout.fillWidth: true }
                ThemedButton { visible: !(physicalDeviceDialog.detail.rigs || ""); theme: themeTokens; text: "FORGET DEVICE"; tone: "danger"; onTriggered: { const forgotten = backendObject.forgetController(root.selectedDeviceId); root.reportBooleanAction(forgotten, "Physical controller forgotten", "The saved device was removed from HOTAS BF6.", "Physical controller was not forgotten", "The device may still be used by a Device Rig."); if (forgotten) physicalDeviceDialog.close() } }
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
                Rectangle { width: 9; height: 9; radius: 5; color: outputDetailDialog.detail.ready ? themeTokens.ready : outputDetailDialog.detail.readinessState === "SAVED" ? themeTokens.textMuted : themeTokens.warning }
                Text { Layout.fillWidth: true; text: (outputDetailDialog.detail.readinessState || (outputDetailDialog.detail.ready ? "READY" : "SAVED")) + " · " + (outputDetailDialog.detail.status || "Verify output"); color: themeTokens.text; font.pixelSize: 12; elide: Text.ElideRight }
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
                Text {
                    text: !outputDetailDialog.detail.managedVisibility ? "Check setup"
                          : !outputDetailDialog.detail.visibilityKnown ? "Verify current state"
                          : outputDetailDialog.detail.hiddenFromGames ? "Hidden from games" : "Visible to games"
                    color: !outputDetailDialog.detail.managedVisibility || !outputDetailDialog.detail.visibilityKnown
                           ? themeTokens.warning
                           : outputDetailDialog.detail.hiddenFromGames ? themeTokens.warning : themeTokens.ready
                }
            }
            ColumnLayout {
                visible: !outputDetailDialog.detail.managedVisibility
                Layout.fillWidth: true; spacing: 6
                SmallLabel { text: "TECHNICAL DETAILS" }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted; font.pixelSize: 10
                    text: "Enter the exact virtual-controller identifier shown in Technical Details. A name alone cannot identify a controller safely." }
                RowLayout { Layout.fillWidth: true
                    ThemedTextInput { id: outputVisibilityIdentity; theme: themeTokens; Layout.fillWidth: true; placeholderText: "HID\\VID_1234&PID_BEAD\\…" }
                    ThemedButton { theme: themeTokens; compact: true; text: "SAVE ID"; tone: "secondary"
                        commandEnabled: outputVisibilityIdentity.text.trim().length > 0
                        onTriggered: { const saved = backendObject.adoptVirtualOutputVisibility(root.selectedOutputId, outputVisibilityIdentity.text); root.reportBooleanAction(saved, "Virtual Output identity saved", "HOTAS BF6 can now check game visibility for this output.", "Virtual Output identity was not saved", "Use the exact vJoy device identity shown in Technical Details."); if (saved) outputVisibilityIdentity.text = "" } }
                }
            }
            RowLayout { Layout.fillWidth: true
                ThemedButton { theme: themeTokens; text: "CHECK OUTPUT"; tone: "secondary"
                    commandEnabled: !!backendObject
                    onTriggered: { root.requestVerification(root.selectedRigId, "", root.selectedOutputId); outputDetailDialog.close() } }
                ThemedButton {
                    theme: themeTokens; tone: "secondary"
                    visible: Boolean(outputDetailDialog.detail.managedVisibility && root.selectedRig)
                    text: outputDetailDialog.detail.hiddenFromGames ? "SHOW TO GAMES" : "HIDE FROM GAMES"
                    commandEnabled: outputDetailDialog.detail.hiddenFromGames || !root.selectedRig || !root.hasOutput(root.selectedRig, root.selectedOutputId)
                                    || !(root.selectedRig.configured && root.selectedRig.inUse)
                    onTriggered: visibilityConfirmationDialog.openForOutputs(root.selectedRigId, [root.selectedOutputId], outputDetailDialog.detail.hiddenFromGames)
                }
                ThemedButton { theme: themeTokens; text: "CONFIGURE VJOY"; tone: "secondary"; onTriggered: root.reportBooleanAction(backendObject.openVjoyConfiguration(), "vJoy configuration opened", "Configure the requested Virtual Output, then return to Check Output.", "vJoy configuration could not open", "Install or repair the vJoy configuration tool, then try again.") }
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
                    onTriggered: { const saved = backendObject.renameVirtualOutputLayout(root.selectedOutputId, outputNameField.text); root.reportBooleanAction(saved, "Virtual Output renamed", "The saved output name was updated.", "Virtual Output was not renamed", "Choose a unique output name and try again."); if (saved) renameOutputDialog.close() } }
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
                    onTriggered: { const rig = selectedRig; const saved = rig && backendObject.renameDeviceRig(rig.id, rigNameField.text); root.reportBooleanAction(saved, "Device Rig renamed", "The saved rig name was updated.", "Device Rig was not renamed", "Choose a unique rig name and try again."); if (saved) renameRigDialog.close() } }
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
                        const applied = backendObject.applyEditingAxisBatch(batchAxisDialog.axisIndex, "inverted",
                            invertAll.checked, "apply-compatible-only")
                        root.reportBooleanAction(applied, "Compatible settings applied", "The selected compatible devices were updated.", "Settings were not applied", "Review the compatible devices and try again.")
                        if (applied) batchAxisDialog.close()
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
        property string outputLayoutId: ""
        function selectedFrom(repeater) {
            let ids = []
            for (let i = 0; i < repeater.count; ++i) {
                const item = repeater.itemAt(i)
                if (item && item.checked) ids.push(item.controllerId)
            }
            return ids
        }
        function selectedControllerIds() {
            return selectedFrom(connectedControllerRepeater).concat(selectedFrom(savedOfflineControllerRepeater))
        }
        function selectedControllersNeedSetup() {
            for (const repeater of [connectedControllerRepeater, savedOfflineControllerRepeater]) {
                for (let i = 0; i < repeater.count; ++i) {
                    const item = repeater.itemAt(i)
                    if (item && item.checked && !item.verified) return true
                }
            }
            return false
        }
        function selectedOutputName() {
            const layouts = backendObject ? backendObject.virtualOutputLayouts : []
            for (let i = 0; i < layouts.length; ++i)
                if (layouts[i].id === outputLayoutId) return layouts[i].name
            return "No virtual output selected"
        }
        onOpened: {
            if (outputLayoutId === "" && backendObject) {
                const layouts = backendObject.virtualOutputLayouts
                outputLayoutId = layouts.length > 0 ? layouts[0].id : ""
            }
        }
        background: DevicePanel { theme: themeTokens; legacy: root.legacy; border.color: themeTokens.borderStrong }
        contentItem: ColumnLayout {
            width: parent.width; spacing: 13
            Text { Layout.fillWidth: true; text: "Choose the physical controllers and virtual controller that belong together. HOTAS BF6 will guide you through setup after the rig is created."; wrapMode: Text.WordWrap; color: themeTokens.text }
            SmallLabel { text: "1  ·  RIG AND INPUTS" }
            ThemedTextInput { id: rigName; theme: themeTokens; Layout.fillWidth: true; placeholderText: "Rig name, for example BF6 Flight Rig" }
            SmallLabel { visible: root.connectedPhysicalControllers().length > 0; text: "CONNECTED" }
            Repeater {
                id: connectedControllerRepeater
                model: root.connectedPhysicalControllers()
                delegate: ThemedCheckBox {
                    required property var modelData
                    theme: themeTokens
                    property string controllerId: modelData.id || modelData.directInputId || ""
                    property bool verified: !!modelData.verified
                    text: modelData.name + (verified ? "  ·  Connected · Verified" : "  ·  Connected · Needs setup")
                    checked: !!modelData.selected
                }
            }
            SmallLabel { visible: root.savedOfflinePhysicalControllers().length > 0; text: "SAVED / OFFLINE" }
            Repeater {
                id: savedOfflineControllerRepeater
                model: root.savedOfflinePhysicalControllers()
                delegate: ThemedCheckBox {
                    required property var modelData
                    theme: themeTokens
                    property string controllerId: modelData.id
                    property bool verified: !!modelData.verified
                    text: modelData.name + (verified ? "  ·  Offline · Verified" : "  ·  Offline · Needs setup")
                    checked: !!modelData.selected
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
            SmallLabel { text: "2  ·  VIRTUAL OUTPUT" }
            ThemedComboBox {
                id: rigOutputPicker; theme: themeTokens; Layout.fillWidth: true
                model: backendObject ? backendObject.virtualOutputLayouts : []
                textRole: "name"; valueRole: "id"
                currentIndex: root.indexFor(model, createRigDialog.outputLayoutId)
                onActivated: function(index, value) { createRigDialog.outputLayoutId = value }
            }
            ThemedButton { theme: themeTokens; text: "+ CREATE NEW VJOY DEVICE"; tone: "secondary"
                onTriggered: { createRigDialog.close(); createOutputDialog.returnToRig = true; createOutputDialog.returnToOutputInventory = false; createOutputDialog.resetForOpen(); createOutputDialog.open() } }
            Rectangle { Layout.fillWidth: true; height: 1; color: themeTokens.divider }
            SmallLabel { text: "3  ·  REVIEW" }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: themeTokens.textMuted; font.pixelSize: 11
                text: createRigDialog.selectedControllerIds().length === 0
                    ? "Connect a controller to create your first Device Rig."
                    : "This rig will use “" + createRigDialog.selectedOutputName() + "”. If a selected controller needs setup, the Setup Assistant will open next." }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ThemedButton {
                    objectName: "createRigButton"
                    theme: themeTokens; text: createRigDialog.selectedControllersNeedSetup() ? "CREATE & SET UP" : "CREATE RIG"
                    commandEnabled: rigName.text.trim().length > 0 && createRigDialog.outputLayoutId !== "" && createRigDialog.selectedControllerIds().length > 0
                    onTriggered: {
                        const result = root.createRigWithInputs(rigName.text, createRigDialog.selectedControllerIds(), createRigDialog.outputLayoutId)
                        if (result.success) {
                            createRigDialog.close()
                            rigName.text = ""
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
                        const rig = root.selectedRig
                        const deleted = rig && backendObject.deleteDeviceRig(rig.id)
                        root.reportBooleanAction(deleted, "Device Rig deleted", "Saved controllers and mappings remain available.", "Device Rig was not deleted", "Refresh the Device Rig and try again.")
                        if (deleted) {
                            root.selectedRigId = ""
                            deleteRigDialog.close()
                        }
                    }
                }
            }
        }
    }
}
