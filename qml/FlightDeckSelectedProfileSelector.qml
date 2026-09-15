import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// The Flight Deck's editor-context selector. It deliberately calls only
// selectProfileForEditing(): choosing a Profile never reaches activation,
// controller verification, vJoy, HidHide, or the mapper runtime.
Button {
    id: control
    required property var backendObject
    required property var tokens
    property var notificationCenter: null
    property bool compact: false

    objectName: "flightDeckSelectedProfileSelector"
    implicitWidth: compact ? 42 : Math.max(214, selectorRow.implicitWidth + tokens.space20)
    implicitHeight: 38
    focusPolicy: Qt.StrongFocus
    Accessible.name: "Selected Profile: " + backendObject.selectedProfileDisplayName

    readonly property bool active: backendObject.selectedProfileActive

    function profilesForCategory(categoryId) {
        const profiles = backendObject.profiles || []
        return profiles.filter(function(profile) {
            return String(profile.categoryId || "") === String(categoryId || "")
        })
    }
    function selectProfile(profileId) {
        backendObject.selectProfileForEditing(String(profileId || ""))
    }
    function activateSelectedProfile(profileId) {
        const result = backendObject.activateProfileResult(String(profileId || "")) || ({})
        if (result.requiresRigSwitchConfirmation) {
            rigSwitchActivationDialog.openFor(result)
            return false
        }
        if (notificationCenter && !result.persistent)
            notificationCenter.enqueue(result, "Profile activation", "", 5000)
        return !!result.success
    }

    contentItem: RowLayout {
        id: selectorRow
        spacing: tokens.space8
        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: control.active ? tokens.healthy : tokens.focus
        }
        ColumnLayout {
            visible: !control.compact
            Layout.fillWidth: true
            spacing: 0
            Text {
                text: "SELECTED PROFILE"
                color: tokens.textMuted
                font.family: tokens.telemetryFont
                font.pixelSize: 7
                font.bold: true
            }
            Text {
                text: backendObject.selectedProfileName
                color: tokens.textPrimary
                font.family: tokens.bodyFont
                font.pixelSize: 10
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: backendObject.selectedCategoryName + " · "
                    + (control.active ? "ACTIVE" : (String(backendObject.activeProfileId || "").length === 0
                        ? "VIEWING · NONE ACTIVE" : "VIEWING"))
                color: control.active ? tokens.healthy : tokens.textSecondary
                font.family: tokens.telemetryFont
                font.pixelSize: 7
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
        Text {
            text: popup.visible ? "⌃" : "⌄"
            color: tokens.textSecondary
            font.pixelSize: 14
        }
    }
    background: Rectangle {
        radius: tokens.radiusControl
        color: control.down ? tokens.accentMuted : control.hovered ? tokens.selected : tokens.secondarySurface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? tokens.focus : control.hovered ? tokens.accent : tokens.border
    }
    onClicked: popup.visible ? popup.close() : popup.open()

    Popup {
        id: popup
        objectName: "flightDeckSelectedProfilePopup"
        x: Math.min(0, control.parent ? control.parent.width - control.x - width : 0)
        y: control.height + tokens.space8
        width: Math.max(306, control.width)
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
                text: "SELECTED PROFILE"
                color: tokens.textMuted
                font.family: tokens.telemetryFont
                font.pixelSize: 8
                font.bold: true
            }
            Text {
                text: control.active ? "ACTIVE AT RUNTIME"
                    : (String(backendObject.activeProfileId || "").length === 0
                        ? "NO PROFILE ACTIVE · VIEWING FOR EDITING" : "VIEWING FOR EDITING")
                color: control.active ? tokens.healthy : tokens.focus
                font.family: tokens.telemetryFont
                font.pixelSize: 8
                font.bold: true
            }
            Text {
                text: "Choose the Profile to view and edit. Activation is a separate explicit action and retains its runtime safety checks."
                color: tokens.textSecondary
                font.pixelSize: 9
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(360, profileList.implicitHeight)
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    id: profileList
                    width: parent.width
                    spacing: tokens.space8
                    Repeater {
                        model: backendObject.profileCategories
                        delegate: ColumnLayout {
                            id: categoryColumn
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: String(categoryColumn.modelData.name || "General").toUpperCase()
                                color: tokens.textMuted
                                font.family: tokens.telemetryFont
                                font.pixelSize: 8
                                font.bold: true
                            }
                            Repeater {
                                model: control.profilesForCategory(categoryColumn.modelData.id)
                                delegate: ColumnLayout {
                                    id: profileEntry
                                    required property var modelData
                                    readonly property bool selected: String(modelData.id || "") === String(backendObject.selectedProfileId || "")
                                    readonly property bool active: !!modelData.active
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Button {
                                        id: profileButton
                                        objectName: "flightDeckSelectedProfileRow_" + String(profileEntry.modelData.id || "")
                                        Layout.fillWidth: true
                                        implicitHeight: 36
                                        focusPolicy: Qt.StrongFocus
                                        contentItem: RowLayout {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.leftMargin: tokens.space8
                                            anchors.rightMargin: tokens.space8
                                            Text { text: profileEntry.selected ? "✓" : ""; color: tokens.accent; font.bold: true; Layout.preferredWidth: 12 }
                                            Text { text: String(profileEntry.modelData.name || "Profile"); color: tokens.textPrimary; font.pixelSize: 10; font.bold: profileEntry.selected; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text { visible: profileEntry.active; text: "ACTIVE"; color: tokens.healthy; font.family: tokens.telemetryFont; font.pixelSize: 7; font.bold: true }
                                            Text { visible: profileEntry.selected && !profileEntry.active; text: "VIEWING"; color: tokens.focus; font.family: tokens.telemetryFont; font.pixelSize: 7; font.bold: true }
                                        }
                                        background: Rectangle {
                                            radius: tokens.radiusControl
                                            color: profileEntry.selected ? tokens.selected : profileButton.hovered ? tokens.secondarySurface : "transparent"
                                            border.color: profileEntry.selected ? tokens.focus : profileEntry.active ? tokens.healthy : tokens.border
                                        }
                                        // Viewing remains a pure editor-context action.
                                        onClicked: control.selectProfile(profileEntry.modelData.id)
                                    }
                                    Button {
                                        objectName: "flightDeckSelectedProfileActivate_" + String(profileEntry.modelData.id || "")
                                        visible: profileEntry.selected && !profileEntry.active
                                        Layout.alignment: Qt.AlignRight
                                        implicitWidth: 94
                                        implicitHeight: 26
                                        text: "ACTIVATE"
                                        focusPolicy: Qt.StrongFocus
                                        contentItem: Text {
                                            text: parent.text
                                            color: tokens.light ? "white" : tokens.primarySurface
                                            font.family: tokens.telemetryFont
                                            font.pixelSize: 8
                                            font.bold: true
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            radius: tokens.radiusControl
                                            color: parent.down ? tokens.accentMuted : tokens.accent
                                            border.color: parent.activeFocus ? tokens.focus : tokens.accent
                                            border.width: parent.activeFocus ? 2 : 1
                                        }
                                        // This is the only selector control that can enter the
                                        // explicit runtime activation transaction.
                                        onClicked: control.activateSelectedProfile(profileEntry.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Keep this dialog in the same QML component as the selector.  A
    // separately-instantiated derived FlightDeckDialog left its required
    // theme property uninitialised during application startup, which meant a
    // selector appearing anywhere in the shell could prevent the entire UI
    // from loading.
    FlightDeckDialog {
        id: rigSwitchActivationDialog
        tokens: control.tokens
        heading: "ACTIVATE ON ANOTHER RIG"
        tone: "attention"
        preferredWidth: 530
        property var activationRequest: ({})
        property string profileId: ""

        function openFor(result) {
            activationRequest = result || ({})
            profileId = String(activationRequest.requestedProfileId || "")
            open()
        }

        function notify(result) {
            if (control.notificationCenter && result && !result.persistent)
                control.notificationCenter.enqueue(result, "Profile activation", "", 5000)
        }

        component ModalButton: Button {
            property bool subdued: false
            implicitHeight: rigSwitchActivationDialog.tokens.controlHeight
            implicitWidth: buttonText.implicitWidth + rigSwitchActivationDialog.tokens.space24
            focusPolicy: Qt.StrongFocus
            contentItem: Text {
                id: buttonText
                text: parent.text
                color: parent.subdued ? rigSwitchActivationDialog.tokens.textSecondary
                    : rigSwitchActivationDialog.tokens.primarySurface
                font.family: rigSwitchActivationDialog.tokens.telemetryFont
                font.pixelSize: 9
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: rigSwitchActivationDialog.tokens.radiusControl
                color: parent.down ? rigSwitchActivationDialog.tokens.accentMuted
                    : parent.subdued ? rigSwitchActivationDialog.tokens.secondarySurface
                    : rigSwitchActivationDialog.tokens.accent
                border.width: parent.activeFocus ? 2 : 1
                border.color: parent.activeFocus ? rigSwitchActivationDialog.tokens.focus
                    : parent.subdued ? rigSwitchActivationDialog.tokens.border
                    : rigSwitchActivationDialog.tokens.accent
            }
        }

        contentItem: ColumnLayout {
            width: rigSwitchActivationDialog.availableWidth
            spacing: rigSwitchActivationDialog.tokens.space12
            Text {
                Layout.fillWidth: true
                text: "PROFILE  ·  " + String(rigSwitchActivationDialog.activationRequest.requestedProfileName || "Profile")
                color: rigSwitchActivationDialog.tokens.textPrimary
                font.family: rigSwitchActivationDialog.tokens.telemetryFont
                font.pixelSize: 11
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: rigSwitchSummary.implicitHeight + rigSwitchActivationDialog.tokens.space16
                radius: rigSwitchActivationDialog.tokens.radiusControl
                color: rigSwitchActivationDialog.tokens.primarySurface
                border.width: 1
                border.color: rigSwitchActivationDialog.tokens.border
                ColumnLayout {
                    id: rigSwitchSummary
                    anchors.fill: parent
                    anchors.margins: rigSwitchActivationDialog.tokens.space8
                    spacing: rigSwitchActivationDialog.tokens.space4
                    Text {
                        text: "ACTIVE HARDWARE RIG"
                        color: rigSwitchActivationDialog.tokens.textMuted
                        font.family: rigSwitchActivationDialog.tokens.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                    }
                    Text {
                        text: String(rigSwitchActivationDialog.activationRequest.currentDeviceRigName || "No Device Rig")
                        color: rigSwitchActivationDialog.tokens.textPrimary
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "PROFILE'S CONFIGURED RIG"
                        color: rigSwitchActivationDialog.tokens.accent
                        font.family: rigSwitchActivationDialog.tokens.telemetryFont
                        font.pixelSize: 8
                        font.bold: true
                    }
                    Text {
                        text: String(rigSwitchActivationDialog.activationRequest.configuredDeviceRigName || "Device Rig assignment required")
                        color: rigSwitchActivationDialog.tokens.textPrimary
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "To activate this Profile, HOTAS BF6 will switch the active hardware Rig to the Profile's configured Rig. This does not change the Profile's Rig assignment or its mappings."
                color: rigSwitchActivationDialog.tokens.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ModalButton {
                    text: "CANCEL"
                    subdued: true
                    onClicked: rigSwitchActivationDialog.close()
                }
                ModalButton {
                    text: "SWITCH RIG & ACTIVATE"
                    onClicked: {
                        const result = control.backendObject.activateProfileAfterRigSwitchConfirmation(rigSwitchActivationDialog.profileId)
                        rigSwitchActivationDialog.notify(result)
                        rigSwitchActivationDialog.close()
                    }
                }
            }
        }
    }
}
