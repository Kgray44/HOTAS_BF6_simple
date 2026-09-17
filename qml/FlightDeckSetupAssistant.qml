import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5

// Pass B's one durable setup host. It keeps the journey state in AppBackend's
// small task journal, while all durable Rig/Profile/activation/readiness work
// remains owned by the existing backend commands.
FlightDeckDialog {
    id: root
    objectName: "flightDeckSetupAssistant"

    required property var backendObject
    signal navigateToPage(int page)

    property string pendingIntent: ""
    property var pendingContext: ({})
    property bool replacementChoiceVisible: false
    property string feedback: ""
    property bool feedbackSuccess: true

    readonly property var task: backendObject ? backendObject.setupAssistantTask : ({})
    readonly property string stage: String(task.stage || "controllers")
    readonly property var controllers: backendObject ? (backendObject.controllers || []) : []
    readonly property var rigs: backendObject ? (backendObject.deviceRigs || []) : []
    readonly property var outputs: backendObject ? (backendObject.virtualOutputLayouts || []) : []
    readonly property var categories: backendObject ? (backendObject.profileCategories || []) : []
    readonly property var profiles: backendObject ? (backendObject.profiles || []) : []
    readonly property string taskIntent: String(task.intent || "")

    heading: "Guided setup"
    preferredWidth: 780
    tone: feedbackSuccess ? "informational" : "attention"
    closePolicy: Popup.CloseOnEscape

    function showResult(result, fallback) {
        feedbackSuccess = Boolean(result && result.success)
        feedback = String(result && (result.message || result.title) || fallback || "")
    }

    function openFor(intent, context) {
        pendingIntent = String(intent || "independent")
        pendingContext = context || ({})
        const result = backendObject.beginSetupAssistantTask(pendingIntent, pendingContext)
        showResult(result, "Setup could not start.")
        replacementChoiceVisible = Boolean(result && result.requiresChoice)
        if (!replacementChoiceVisible)
            root.open()
        else
            root.open()
    }

    function openForResume() {
        const result = backendObject.resumeSetupAssistantTask()
        showResult(result, "No saved setup is available.")
        replacementChoiceVisible = false
        if (result && result.success)
            root.open()
    }

    function chooseIntent(intent) {
        const context = { controllerRecordId: String(task.controllerRecordId || ""),
                          rigId: String(task.rigId || ""),
                          outputLayoutId: String(task.outputLayoutId || "") }
        const result = backendObject.replaceUncommittedSetupAssistantTask(intent, context)
        showResult(result, "Setup path could not be changed.")
        replacementChoiceVisible = Boolean(result && result.requiresChoice)
    }

    function controllerIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < controllers.length; ++index) {
            const item = controllers[index] || ({})
            const candidate = String(item.id || item.directInputId || "")
            if (candidate === wanted) return index
        }
        return controllers.length ? 0 : -1
    }

    function rigIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < rigs.length; ++index)
            if (String(rigs[index].id || "") === wanted) return index
        return rigs.length ? 0 : -1
    }

    function outputIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < outputs.length; ++index)
            if (String(outputs[index].id || "") === wanted) return index
        for (let index = 0; index < outputs.length; ++index)
            if (Boolean(outputs[index].active)) return index
        return outputs.length ? 0 : -1
    }

    function categoryIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < categories.length; ++index)
            if (String(categories[index].id || "") === wanted) return index
        return categories.length ? 0 : -1
    }

    function selectedControllerId() {
        const item = controllerChoice.currentIndex >= 0 ? controllers[controllerChoice.currentIndex] : ({})
        // New physical devices have not received a saved record yet. The
        // existing create-Rig command recognizes their exact DirectInput ID.
        return String(item && (item.id || item.directInputId) || "")
    }

    function selectedRigId() {
        const item = rigChoice.currentIndex >= 0 ? rigs[rigChoice.currentIndex] : ({})
        return String(item && item.id || "")
    }

    function selectedOutputId() {
        const item = outputChoice.currentIndex >= 0 ? outputs[outputChoice.currentIndex] : ({})
        return String(item && item.id || "")
    }

    function selectedCategoryId() {
        const item = categoryChoice.currentIndex >= 0 ? categories[categoryChoice.currentIndex] : ({})
        return String(item && item.id || "")
    }

    function saveControllerChoice() {
        const result = backendObject.updateSetupAssistantTask({
            controllerRecordId: selectedControllerId(), stage: "purpose"
        })
        showResult(result, "Controller choice could not be saved.")
    }

    function savePurposeChoice() {
        const result = backendObject.updateSetupAssistantTask({
            rigId: selectedRigId(), outputLayoutId: selectedOutputId(),
            categoryId: selectedCategoryId(), stage: "purpose"
        })
        showResult(result, "Setup choices could not be saved.")
    }

    function commitPurpose() {
        let result
        if (taskIntent === "add-to-rig") {
            result = backendObject.commitSetupAssistantSharedMember(selectedRigId(), selectedControllerId(), requiredMembership.checked)
        } else {
            result = backendObject.commitSetupAssistantRigAndProfile(
                rigName.text, profileName.text, selectedControllerId(), selectedOutputId(),
                selectedCategoryId(), copyProfile.checked ? String(copyChoice.currentValue || "") : "")
        }
        showResult(result, "The selected setup could not be saved.")
    }

    function runConnectionCheck() {
        const rigId = String(task.rigId || "")
        const result = rigId.length
            ? backendObject.startSetupAssistantCheckForScope("deviceRig", rigId)
            : backendObject.startSetupAssistantCheckForScope("device", String(task.controllerRecordId || ""))
        showResult(result, "The scoped setup check could not start.")
    }

    function openEditor(page) {
        const result = backendObject.prepareSetupAssistantEditor(page)
        showResult(result, "The editor could not be prepared.")
        if (result && result.success) {
            root.close()
            root.navigateToPage(page)
        }
    }

    component SetupButton: Button {
        property bool subdued: false
        implicitHeight: root.tokens.compactControlHeight
        leftPadding: root.tokens.space12
        rightPadding: root.tokens.space12
        font.family: root.tokens.bodyFont
        font.pixelSize: root.tokens.bodySmall
        font.bold: true
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? (parent.subdued ? root.tokens.textSecondary
                                                     : (root.tokens.light ? "white" : root.tokens.primarySurface))
                                  : root.tokens.textMuted
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: root.tokens.radiusControl
            color: !parent.enabled ? root.tokens.disabled
                : parent.subdued ? (parent.down ? root.tokens.secondarySurface : root.tokens.elevatedSurface)
                : (parent.down ? root.tokens.accentMuted : root.tokens.accent)
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? root.tokens.focus
                : parent.subdued ? root.tokens.border : root.tokens.accent
        }
    }

    component SetupCombo: ComboBox {
        implicitHeight: root.tokens.controlHeight
        font.family: root.tokens.bodyFont
        font.pixelSize: root.tokens.body
        textRole: "name"
        valueRole: "id"
        background: Rectangle {
            radius: root.tokens.radiusControl
            color: root.tokens.secondarySurface
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border
        }
        contentItem: Text {
            leftPadding: root.tokens.space12
            rightPadding: root.tokens.space32
            text: parent.displayText
            color: root.tokens.textPrimary
            font: parent.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    contentItem: ScrollView {
        id: scroller
        implicitHeight: Math.min(setupContent.implicitHeight, root.maximumBodyHeight)
        contentWidth: availableWidth
        contentHeight: setupContent.implicitHeight
        clip: true

        ColumnLayout {
            id: setupContent
            width: scroller.availableWidth
            spacing: root.tokens.space16

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: stages.implicitHeight + root.tokens.space16
                color: root.tokens.secondarySurface
                radius: root.tokens.radiusCard
                border.width: 1
                border.color: root.tokens.border
                RowLayout {
                    id: stages
                    anchors.fill: parent
                    anchors.margins: root.tokens.space8
                    spacing: root.tokens.space4
                    Repeater {
                        model: [
                            { key: "controllers", label: "1  CONTROLLERS" },
                            { key: "purpose", label: "2  HOW YOU'LL USE THEM" },
                            { key: "connection", label: "3  PREPARE CONNECTION" },
                            { key: "configure", label: "4  CONFIGURE / TEST" }
                        ]
                        delegate: Rectangle {
                            readonly property int order: index
                            readonly property var stageOrder: ({ controllers: 0, purpose: 1, connection: 2, configure: 3, complete: 4 })
                            readonly property int activeOrder: stageOrder[root.stage] === undefined ? 0 : stageOrder[root.stage]
                            Layout.fillWidth: true
                            implicitHeight: root.tokens.compactControlHeight
                            radius: root.tokens.radiusControl
                            color: order === activeOrder ? root.tokens.accentMuted
                                : order < activeOrder ? root.tokens.selected : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: order === activeOrder ? root.tokens.textPrimary : root.tokens.textMuted
                                font.family: root.tokens.bodyFont
                                font.pixelSize: root.tokens.caption
                                font.bold: order === activeOrder
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Text {
                visible: root.feedback.length > 0
                Layout.fillWidth: true
                text: root.feedback
                color: root.feedbackSuccess ? root.tokens.textSecondary : root.tokens.attention
                font.family: root.tokens.bodyFont
                font.pixelSize: root.tokens.bodySmall
                wrapMode: Text.WordWrap
            }

            Rectangle {
                visible: root.replacementChoiceVisible
                Layout.fillWidth: true
                implicitHeight: replacementBody.implicitHeight + root.tokens.space24
                color: root.tokens.secondarySurface
                radius: root.tokens.radiusCard
                border.width: 1
                border.color: root.tokens.attention
                ColumnLayout {
                    id: replacementBody
                    anchors.fill: parent
                    anchors.margins: root.tokens.space12
                    spacing: root.tokens.space8
                    Text { text: "A SAVED SETUP IS IN PROGRESS"; color: root.tokens.textPrimary; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.bodyStrong; font.bold: true }
                    Text { Layout.fillWidth: true; text: "Resume it to preserve its exact selection. You may replace only uncommitted choices; created Rigs or Profiles are never deleted here."; color: root.tokens.textSecondary; font.pixelSize: root.tokens.bodySmall; wrapMode: Text.WordWrap }
                    RowLayout {
                        Layout.fillWidth: true
                        SetupButton { text: "RESUME SETUP"; onClicked: root.openForResume() }
                        SetupButton { visible: Boolean(root.task.operationRefs === undefined || root.task.operationRefs.length === 0); text: "REPLACE UNCOMMITTED"; subdued: true; onClicked: { const result = backendObject.replaceUncommittedSetupAssistantTask(root.pendingIntent, root.pendingContext); root.showResult(result, "Setup could not be replaced."); root.replacementChoiceVisible = Boolean(result && result.requiresChoice) } }
                    }
                }
            }

            ColumnLayout {
                visible: !root.replacementChoiceVisible && root.stage === "controllers"
                Layout.fillWidth: true
                spacing: root.tokens.space12
                Text { text: "CONTROLLERS"; color: root.tokens.textPrimary; font.family: root.tokens.displayFont; font.pixelSize: root.tokens.section; font.bold: true }
                Text { Layout.fillWidth: true; text: "Choose one physical controller. This selection is saved as guidance only; it does not activate a controller or change mapping."; color: root.tokens.textSecondary; font.pixelSize: root.tokens.body; wrapMode: Text.WordWrap }
                Text { text: "PHYSICAL CONTROLLER"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: controllerChoice
                    objectName: "flightDeckSetupControllerChoice"
                    Layout.fillWidth: true
                    model: root.controllers
                    currentIndex: root.controllerIndexFor(root.task.controllerRecordId)
                    textRole: "name"
                }
                Text {
                    Layout.fillWidth: true
                    visible: controllerChoice.currentIndex >= 0
                    text: { const item = root.controllers[controllerChoice.currentIndex] || ({}); return String(item.state || "") }
                    color: root.tokens.textMuted
                    font.family: root.tokens.bodyFont
                    font.pixelSize: root.tokens.bodySmall
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    SetupButton { objectName: "flightDeckSetupControllersNext"; text: "CONTINUE"; enabled: controllerChoice.currentIndex >= 0; onClicked: root.saveControllerChoice() }
                }
            }

            ColumnLayout {
                visible: !root.replacementChoiceVisible && root.stage === "purpose"
                Layout.fillWidth: true
                spacing: root.tokens.space12
                Text { text: "HOW YOU'LL USE THEM"; color: root.tokens.textPrimary; font.family: root.tokens.displayFont; font.pixelSize: root.tokens.section; font.bold: true }
                Text { Layout.fillWidth: true; text: "Choose a topology first. Nothing below changes an existing Rig, Profile, or mapping until the labeled commit action is selected."; color: root.tokens.textSecondary; font.pixelSize: root.tokens.body; wrapMode: Text.WordWrap }
                Flow {
                    Layout.fillWidth: true
                    spacing: root.tokens.space8
                    SetupButton { text: "FIRST CONTROLLER"; subdued: root.taskIntent !== "first-controller"; onClicked: root.chooseIntent("first-controller") }
                    SetupButton { text: "INDEPENDENT RIG"; subdued: root.taskIntent !== "independent"; onClicked: root.chooseIntent("independent") }
                    SetupButton { text: "ADD TO EXISTING RIG"; subdued: root.taskIntent !== "add-to-rig"; onClicked: root.chooseIntent("add-to-rig") }
                    SetupButton { text: "PROFILE FOR EXISTING RIG"; subdued: root.taskIntent !== "profile-for-rig"; onClicked: root.chooseIntent("profile-for-rig") }
                }
                Text { visible: root.taskIntent === "issue"; Layout.fillWidth: true; text: "This issue handoff begins with review. Select the safe topology that matches the current controller and continue."; color: root.tokens.attention; font.pixelSize: root.tokens.bodySmall; wrapMode: Text.WordWrap }
                Text { visible: root.taskIntent === "add-to-rig" || root.taskIntent === "profile-for-rig"; text: "DEVICE RIG"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: rigChoice
                    visible: root.taskIntent === "add-to-rig" || root.taskIntent === "profile-for-rig"
                    Layout.fillWidth: true
                    model: root.rigs
                    currentIndex: root.rigIndexFor(root.task.rigId)
                    textRole: "name"
                }
                Text { visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"; text: "DEVICE RIG NAME"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                TextField {
                    id: rigName
                    visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"
                    Layout.fillWidth: true
                    placeholderText: "My flight controls"
                    color: root.tokens.textPrimary
                    font.family: root.tokens.bodyFont
                    font.pixelSize: root.tokens.body
                    implicitHeight: root.tokens.controlHeight
                    background: Rectangle { radius: root.tokens.radiusControl; color: root.tokens.secondarySurface; border.width: 1; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border }
                }
                Text { visible: root.taskIntent !== "add-to-rig"; text: "PROFILE NAME"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                TextField {
                    id: profileName
                    visible: root.taskIntent !== "add-to-rig"
                    Layout.fillWidth: true
                    placeholderText: "General flight"
                    color: root.tokens.textPrimary
                    font.family: root.tokens.bodyFont
                    font.pixelSize: root.tokens.body
                    implicitHeight: root.tokens.controlHeight
                    background: Rectangle { radius: root.tokens.radiusControl; color: root.tokens.secondarySurface; border.width: 1; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border }
                }
                Text { visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"; text: "VIRTUAL OUTPUT"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: outputChoice
                    visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"
                    Layout.fillWidth: true
                    model: root.outputs
                    currentIndex: root.outputIndexFor(root.task.outputLayoutId)
                    textRole: "name"
                }
                Text { visible: root.taskIntent !== "add-to-rig"; text: "PROFILE CATEGORY"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: categoryChoice
                    visible: root.taskIntent !== "add-to-rig"
                    Layout.fillWidth: true
                    model: root.categories
                    currentIndex: root.categoryIndexFor(root.task.categoryId)
                    textRole: "name"
                }
                CheckBox {
                    id: copyProfile
                    visible: root.taskIntent !== "add-to-rig"
                    text: "Start from an existing Profile"
                    font.family: root.tokens.bodyFont
                    font.pixelSize: root.tokens.bodySmall
                }
                SetupCombo {
                    id: copyChoice
                    visible: copyProfile.visible && copyProfile.checked
                    Layout.fillWidth: true
                    model: root.profiles
                    textRole: "displayName"
                }
                CheckBox {
                    id: requiredMembership
                    visible: root.taskIntent === "add-to-rig"
                    checked: true
                    text: "This controller is required for this Device Rig"
                    font.family: root.tokens.bodyFont
                    font.pixelSize: root.tokens.bodySmall
                }
                Text {
                    visible: root.taskIntent === "add-to-rig" && rigChoice.currentIndex >= 0
                    Layout.fillWidth: true
                    text: { const selected = String(root.selectedRigId()); const affected = []; for (let i = 0; i < root.profiles.length; ++i) if (String(root.profiles[i].deviceRigId || "") === selected) affected.push(String(root.profiles[i].displayName || root.profiles[i].name || "Profile")); return affected.length ? "Existing Profiles affected: " + affected.join(", ") + ". Their routes stay unchanged; the new controller begins independently." : "No existing Profiles are attached to this Device Rig." }
                    color: root.tokens.textSecondary
                    font.pixelSize: root.tokens.bodySmall
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    SetupButton { text: "BACK"; subdued: true; onClicked: backendObject.updateSetupAssistantTask({ stage: "controllers" }) }
                    Item { Layout.fillWidth: true }
                    SetupButton {
                        objectName: "flightDeckSetupCommitPurpose"
                        text: root.taskIntent === "add-to-rig" ? "ADD CONTROLLER TO RIG" : "SAVE RIG AND PROFILE"
                        enabled: root.taskIntent === "add-to-rig" ? rigChoice.currentIndex >= 0 && controllerChoice.currentIndex >= 0
                            : (root.taskIntent === "profile-for-rig" ? rigChoice.currentIndex >= 0 && profileName.text.trim().length > 0
                                : controllerChoice.currentIndex >= 0 && rigName.text.trim().length > 0 && profileName.text.trim().length > 0 && outputChoice.currentIndex >= 0 && categoryChoice.currentIndex >= 0)
                        onClicked: root.commitPurpose()
                    }
                }
            }

            ColumnLayout {
                visible: !root.replacementChoiceVisible && root.stage === "connection"
                Layout.fillWidth: true
                spacing: root.tokens.space12
                Text { text: "PREPARE CONNECTION"; color: root.tokens.textPrimary; font.family: root.tokens.displayFont; font.pixelSize: root.tokens.section; font.bold: true }
                Text { Layout.fillWidth: true; text: "Run the existing scoped Setup Health check. It observes the selected Rig and its output; any repair remains its own explicit, typed workflow."; color: root.tokens.textSecondary; font.pixelSize: root.tokens.body; wrapMode: Text.WordWrap }
                SetupButton { objectName: "flightDeckSetupRunCheck"; text: "RUN SCOPED SETUP CHECK"; onClicked: root.runConnectionCheck() }
                ControllerReadinessPanel {
                    objectName: "flightDeckSetupReadinessPanel"
                    Layout.fillWidth: true
                    backendObject: root.backendObject
                    themeTokens: root.tokens
                    showTitle: false
                    useHostRepairConfirmation: true
                    presentationPage: "CHECK"
                }
                RowLayout {
                    Layout.fillWidth: true
                    SetupButton { text: "BACK"; subdued: true; onClicked: backendObject.updateSetupAssistantTask({ stage: "purpose" }) }
                    Item { Layout.fillWidth: true }
                    SetupButton { objectName: "flightDeckSetupConfigureNext"; text: "CONTINUE TO CONFIGURE"; onClicked: { const result = backendObject.updateSetupAssistantTask({ stage: "configure" }); root.showResult(result, "Could not continue.") } }
                }
            }

            ColumnLayout {
                visible: !root.replacementChoiceVisible && root.stage === "configure"
                Layout.fillWidth: true
                spacing: root.tokens.space12
                Text { text: "CONFIGURE / TEST"; color: root.tokens.textPrimary; font.family: root.tokens.displayFont; font.pixelSize: root.tokens.section; font.bold: true }
                Text { Layout.fillWidth: true; text: "Open the exact editor context, then return here. Opening an editor changes only what is being viewed; it does not activate this Rig or Profile."; color: root.tokens.textSecondary; font.pixelSize: root.tokens.body; wrapMode: Text.WordWrap }
                Flow {
                    Layout.fillWidth: true
                    spacing: root.tokens.space8
                    SetupButton { objectName: "flightDeckSetupOpenAxes"; text: "OPEN AXES"; onClicked: root.openEditor(0) }
                    SetupButton { objectName: "flightDeckSetupOpenButtons"; text: "OPEN BUTTONS"; onClicked: root.openEditor(1) }
                    SetupButton { text: "REVIEW CONNECTION"; subdued: true; onClicked: backendObject.updateSetupAssistantTask({ stage: "connection" }) }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: useSummary.implicitHeight + root.tokens.space20
                    color: root.tokens.secondarySurface
                    radius: root.tokens.radiusCard
                    border.width: 1
                    border.color: root.tokens.border
                    ColumnLayout {
                        id: useSummary
                        anchors.fill: parent
                        anchors.margins: root.tokens.space10
                        spacing: root.tokens.space6
                        Text { text: "WHEN YOU ARE READY"; color: root.tokens.textPrimary; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.bodyStrong; font.bold: true }
                        Text { Layout.fillWidth: true; text: "Use applies the existing Rig/Profile activation transaction. It does not prove live hardware input, game visibility, or a successful physical test; those facts stay in Setup Health."; color: root.tokens.textSecondary; font.pixelSize: root.tokens.bodySmall; wrapMode: Text.WordWrap }
                        SetupButton { objectName: "flightDeckSetupUse"; text: "USE THIS SETUP"; onClicked: { const result = backendObject.useSetupAssistantTask(); root.showResult(result, "This setup was not activated."); if (result && result.success) root.close() } }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: footerActions.implicitHeight + root.tokens.space12
        color: root.tokens.elevatedSurface
        border.width: 0
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: root.tokens.divider }
        RowLayout {
            id: footerActions
            anchors.fill: parent
            anchors.margins: root.tokens.space6
            SetupButton { text: "SAVE FOR LATER"; subdued: true; visible: !root.replacementChoiceVisible && root.backendObject.hasSetupAssistantTask; onClicked: root.showResult(backendObject.saveSetupAssistantForLater(), "Setup was not saved.") }
            SetupButton { text: "DISMISS GUIDANCE"; subdued: true; visible: !root.replacementChoiceVisible && root.backendObject.hasSetupAssistantTask; onClicked: { root.showResult(backendObject.dismissSetupAssistantTask(), "Guidance was not dismissed."); root.close() } }
            Item { Layout.fillWidth: true }
            SetupButton { text: "CLOSE"; subdued: true; onClicked: root.close() }
        }
    }
}
