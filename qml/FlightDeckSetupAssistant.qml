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
        const result = backendObject.chooseSetupAssistantIntent(intent)
        showResult(result, "Setup path could not be changed.")
        replacementChoiceVisible = Boolean(result && result.requiresSeparateTask)
    }

    function controllerIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < controllers.length; ++index) {
            const item = controllers[index] || ({})
            const candidate = String(item.id || item.directInputId || "")
            if (candidate === wanted) return index
        }
        return -1
    }

    function rigIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < rigs.length; ++index)
            if (String(rigs[index].id || "") === wanted) return index
        return -1
    }

    function outputIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < outputs.length; ++index)
            if (String(outputs[index].id || "") === wanted) return index
        return -1
    }

    function categoryIndexFor(id) {
        const wanted = String(id || "")
        for (let index = 0; index < categories.length; ++index)
            if (String(categories[index].id || "") === wanted) return index
        return -1
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
            rigId: selectedRigId(), profileId: selectedProfileId(), outputLayoutId: selectedOutputId(),
            categoryId: selectedCategoryId(), copyProfileId: copyProfile.checked ? String(copyChoice.currentValue || "") : "",
            rigNameDraft: rigName.text, profileNameDraft: profileName.text,
            copyExistingDraft: copyProfile.checked, requiredMembershipDraft: requiredMembership.checked,
            stage: "purpose"
        })
        showResult(result, "Setup choices could not be saved.")
    }

    function savePurposeDraft() {
        if (root.stage !== "purpose" || root.replacementChoiceVisible) return
        root.savePurposeChoice()
    }

    function commitPurpose() {
        savePurposeChoice()
        let result
        if (taskIntent === "add-to-rig") {
            result = backendObject.commitSetupAssistantSharedMember(selectedRigId(), selectedControllerId(),
                requiredMembership.checked, selectedProfileId(), profileName.text,
                selectedCategoryId(), copyProfile.checked ? String(copyChoice.currentValue || "") : "")
        } else {
            result = backendObject.commitSetupAssistantRigAndProfile(
                rigName.text, profileName.text, selectedControllerId(), selectedOutputId(),
                selectedCategoryId(), copyProfile.checked ? String(copyChoice.currentValue || "") : "",
                taskIntent === "profile-for-rig" ? selectedRigId() : "")
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

    function selectedProfileId() {
        const item = profileChoice.currentIndex >= 0 ? profilesForSelectedRig[profileChoice.currentIndex] : ({})
        return String(item && item.id || "")
    }

    function openRepairReview() {
        const result = backendObject.recordSetupAssistantRepairOperation("deviceRig", String(task.rigId || ""),
            String(readinessPanel.session.sessionId || ""), "pending-consent")
        showResult(result, "The repair target could not be recorded.")
        if (result && result.success) repairReview.open()
    }

    function approveTaskRepair() {
        const result = backendObject.recordSetupAssistantRepairOperation("deviceRig", String(task.rigId || ""),
            String(readinessPanel.session.sessionId || ""), "applying")
        showResult(result, "The repair approval could not be saved.")
        if (result && result.success) {
            repairReview.close()
            backendObject.repairSetupHealth()
        }
    }

    readonly property var profilesForSelectedRig: {
        const selected = selectedRigId()
        const filtered = []
        for (let index = 0; index < profiles.length; ++index)
            if (String(profiles[index].deviceRigId || "") === selected) filtered.push(profiles[index])
        return filtered
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

    // This remains a Qt Quick CheckBox so accessibility, Tab navigation, and
    // Space activation stay owned by Controls.  The assistant owns every
    // visible part rather than inheriting a platform checkbox in Flight Deck.
    component SetupCheckBox: CheckBox {
        id: setupCheck
        implicitHeight: Math.max(root.tokens.controlHeight,
            checkLabel.implicitHeight + root.tokens.space12)
        leftPadding: root.tokens.space8
        rightPadding: root.tokens.space8
        topPadding: root.tokens.space6
        bottomPadding: root.tokens.space6
        spacing: root.tokens.space10
        hoverEnabled: enabled
        focusPolicy: Qt.StrongFocus
        font.family: root.tokens.bodyFont
        font.pixelSize: root.tokens.body
        Accessible.name: text

        indicator: Rectangle {
            id: checkIndicator
            objectName: setupCheck.objectName + "Indicator"
            implicitWidth: root.tokens.scale(18)
            implicitHeight: implicitWidth
            x: setupCheck.leftPadding
            y: (setupCheck.height - height) / 2
            radius: Math.max(3, Math.round(width * 0.24))
            color: !setupCheck.enabled ? root.tokens.disabled
                : setupCheck.down ? (setupCheck.checked ? root.tokens.accentMuted : root.tokens.secondarySurface)
                : setupCheck.checked ? root.tokens.accent
                : setupCheck.hovered ? root.tokens.elevatedSurface : root.tokens.secondarySurface
            border.width: setupCheck.activeFocus ? 2 : 1
            border.color: setupCheck.activeFocus ? root.tokens.focus
                : !setupCheck.enabled ? root.tokens.border
                : setupCheck.checked ? root.tokens.accent
                : setupCheck.hovered ? root.tokens.focus : root.tokens.border

            Canvas {
                id: checkMark
                anchors.centerIn: parent
                width: parent.width - root.tokens.space8
                height: parent.height - root.tokens.space8
                visible: setupCheck.checked
                antialiasing: true
                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    context.strokeStyle = root.tokens.light ? "#ffffff" : root.tokens.primarySurface
                    context.lineWidth = Math.max(2, Math.round(width * 0.16))
                    context.lineCap = "round"
                    context.lineJoin = "round"
                    context.beginPath()
                    context.moveTo(width * 0.16, height * 0.52)
                    context.lineTo(width * 0.42, height * 0.76)
                    context.lineTo(width * 0.84, height * 0.24)
                    context.stroke()
                }
                onVisibleChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections {
                    target: root.tokens
                    function onLightChanged() { checkMark.requestPaint() }
                }
            }
        }

        contentItem: Text {
            id: checkLabel
            objectName: setupCheck.objectName + "Label"
            leftPadding: setupCheck.indicator.width + setupCheck.spacing
            rightPadding: root.tokens.space4
            text: setupCheck.text
            color: setupCheck.enabled ? root.tokens.textPrimary : root.tokens.textSecondary
            font: setupCheck.font
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: root.tokens.radiusControl
            color: !setupCheck.enabled ? "transparent"
                : setupCheck.down ? root.tokens.secondarySurface
                : setupCheck.hovered ? root.tokens.elevatedSurface : "transparent"
            border.width: setupCheck.activeFocus ? 2 : 0
            border.color: root.tokens.focus
        }
    }

    component SetupCombo: ComboBox {
        id: setupCombo
        property string emptyText: ""
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
            objectName: setupCombo.objectName + "Display"
            leftPadding: root.tokens.space12
            rightPadding: root.tokens.space32
            text: parent.currentIndex >= 0 ? parent.displayText : parent.emptyText
            color: parent.currentIndex >= 0 ? root.tokens.textPrimary : root.tokens.textSecondary
            font: parent.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            objectName: setupCombo.objectName + "Indicator"
            x: parent.width - width - root.tokens.space12
            y: (parent.height - height) / 2
            text: "⌄"
            color: root.tokens.textSecondary
            font.family: root.tokens.bodyFont
            font.pixelSize: root.tokens.bodyStrong
        }
        delegate: ItemDelegate {
            objectName: setupCombo.objectName + "Choice_" + index
            width: parent ? parent.width : 0
            height: root.tokens.controlHeight
            highlighted: parent && parent.highlightedIndex === index
            contentItem: Text {
                leftPadding: root.tokens.space12
                rightPadding: root.tokens.space12
                text: modelData && (modelData[textRole] || modelData.displayName || modelData.name)
                color: root.tokens.textPrimary
                font.family: root.tokens.bodyFont
                font.pixelSize: root.tokens.body
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: parent.highlighted || parent.hovered ? root.tokens.selected : "transparent"
            }
        }
        popup: Popup {
            objectName: setupCombo.objectName + "Popup"
            y: parent.height + root.tokens.space4
            width: parent.width
            implicitHeight: Math.min(contentItem.implicitHeight + root.tokens.space8, root.tokens.scale(280))
            padding: root.tokens.space4
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: setupCombo.delegateModel
                currentIndex: setupCombo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle {
                radius: root.tokens.radiusControl
                color: root.tokens.elevatedSurface
                border.width: 1
                border.color: root.tokens.border
            }
        }
    }

    contentItem: ScrollView {
        id: scroller
        objectName: "flightDeckSetupAssistantScroll"
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
                    SetupButton { objectName: "flightDeckSetupIntentFirst"; text: "FIRST CONTROLLER"; subdued: root.taskIntent !== "first-controller"; onClicked: root.chooseIntent("first-controller") }
                    SetupButton { objectName: "flightDeckSetupIntentIndependent"; text: "INDEPENDENT RIG"; subdued: root.taskIntent !== "independent"; onClicked: root.chooseIntent("independent") }
                    SetupButton { objectName: "flightDeckSetupIntentShared"; text: "ADD TO EXISTING RIG"; subdued: root.taskIntent !== "add-to-rig"; onClicked: root.chooseIntent("add-to-rig") }
                    SetupButton { objectName: "flightDeckSetupIntentProfile"; text: "PROFILE FOR EXISTING RIG"; subdued: root.taskIntent !== "profile-for-rig"; onClicked: root.chooseIntent("profile-for-rig") }
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
                    onActivated: root.savePurposeDraft()
                }
                Text { visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"; text: "DEVICE RIG NAME"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                TextField {
                    id: rigName
                    objectName: "flightDeckSetupRigName"
                    visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"
                    Layout.fillWidth: true
                    placeholderText: "My flight controls"
                    placeholderTextColor: root.tokens.textSecondary
                    text: String(root.task.rigNameDraft || "")
                    onEditingFinished: root.savePurposeDraft()
                    color: root.tokens.textPrimary
                    font.family: root.tokens.bodyFont
                    font.pixelSize: root.tokens.body
                    implicitHeight: root.tokens.controlHeight
                    background: Rectangle { radius: root.tokens.radiusControl; color: root.tokens.secondarySurface; border.width: 1; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border }
                }
                Text { visible: root.taskIntent !== "add-to-rig"; text: "PROFILE NAME"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                TextField {
                    id: profileName
                    objectName: "flightDeckSetupProfileName"
                    visible: root.taskIntent !== "add-to-rig"
                    Layout.fillWidth: true
                    placeholderText: "General flight"
                    placeholderTextColor: root.tokens.textSecondary
                    text: String(root.task.profileNameDraft || "")
                    onEditingFinished: root.savePurposeDraft()
                    color: root.tokens.textPrimary
                    font.family: root.tokens.bodyFont
                    font.pixelSize: root.tokens.body
                    implicitHeight: root.tokens.controlHeight
                    background: Rectangle { radius: root.tokens.radiusControl; color: root.tokens.secondarySurface; border.width: 1; border.color: parent.activeFocus ? root.tokens.focus : root.tokens.border }
                }
                Text { visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"; text: "VIRTUAL OUTPUT"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: outputChoice
                    objectName: "flightDeckSetupOutputChoice"
                    visible: root.taskIntent === "first-controller" || root.taskIntent === "independent"
                    Layout.fillWidth: true
                    model: root.outputs
                    currentIndex: root.outputIndexFor(root.task.outputLayoutId)
                    textRole: "name"
                    onActivated: root.savePurposeDraft()
                }
                Text { visible: root.taskIntent !== "add-to-rig"; text: "PROFILE CATEGORY"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: categoryChoice
                    objectName: "flightDeckSetupCategoryChoice"
                    visible: root.taskIntent !== "add-to-rig"
                    Layout.fillWidth: true
                    model: root.categories
                    currentIndex: root.categoryIndexFor(root.task.categoryId)
                    textRole: "name"
                    onActivated: root.savePurposeDraft()
                }
                SetupCheckBox {
                    id: copyProfile
                    objectName: "flightDeckSetupCopyProfile"
                    visible: root.taskIntent !== "add-to-rig"
                    Layout.fillWidth: true
                    text: "Start from an existing profile."
                    checked: Boolean(root.task.copyExistingDraft)
                    onToggled: root.savePurposeDraft()
                }
                Text { visible: copyProfile.visible && copyProfile.checked; text: "SOURCE PROFILE"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: copyChoice
                    objectName: "flightDeckSetupCopyProfileChoice"
                    visible: copyProfile.visible && copyProfile.checked
                    enabled: copyProfile.checked
                    Layout.fillWidth: true
                    model: root.profiles
                    textRole: "displayName"
                    emptyText: "Choose a profile to copy"
                    currentIndex: root.task.copyProfileId ? root.profiles.findIndex(function(profile) { return String(profile.id || "") === String(root.task.copyProfileId || "") }) : -1
                    onActivated: root.savePurposeDraft()
                }
                SetupCheckBox {
                    id: requiredMembership
                    objectName: "flightDeckSetupRequiredMembership"
                    visible: root.taskIntent === "add-to-rig"
                    Layout.fillWidth: true
                    checked: root.task.requiredMembershipDraft === undefined ? true : Boolean(root.task.requiredMembershipDraft)
                    text: "This controller is required for this Device Rig"
                    onToggled: root.savePurposeDraft()
                }
                Text { visible: root.taskIntent === "add-to-rig"; text: "PROFILE TO EDIT AFTERWARD (OPTIONAL)"; color: root.tokens.textMuted; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.caption; font.bold: true }
                SetupCombo {
                    id: profileChoice
                    objectName: "flightDeckSetupSharedProfileChoice"
                    visible: root.taskIntent === "add-to-rig"
                    Layout.fillWidth: true
                    model: root.profilesForSelectedRig
                    textRole: "displayName"
                    currentIndex: {
                        const wanted = String(root.task.profileId || "")
                        for (let index = 0; index < root.profilesForSelectedRig.length; ++index)
                            if (String(root.profilesForSelectedRig[index].id || "") === wanted) return index
                        return -1
                    }
                    onActivated: root.savePurposeDraft()
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
                    id: readinessPanel
                    objectName: "flightDeckSetupReadinessPanel"
                    Layout.fillWidth: true
                    backendObject: root.backendObject
                    themeTokens: root.tokens
                    showTitle: false
                    useHostRepairConfirmation: true
                    presentationPage: "CHECK"
                    checkScopeType: String(root.task.rigId || "").length > 0 ? "deviceRig" : "device"
                    checkScopeId: String(root.task.rigId || root.task.controllerRecordId || "")
                    onRepairRequested: root.openRepairReview()
                    onOperationStateChanged: {
                        if (root.task.repairOperation && String(root.task.repairOperation.state || "") === "applying"
                                && terminal)
                            backendObject.recordSetupAssistantRepairOperation("deviceRig", String(root.task.rigId || ""),
                                String(session.sessionId || ""), String(operationState || "complete").toLowerCase())
                    }
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
                        Text { text: "TEST RESULTS"; color: root.tokens.textPrimary; font.family: root.tokens.bodyFont; font.pixelSize: root.tokens.bodyStrong; font.bold: true }
                        Text { Layout.fillWidth: true; text: "Physical input: " + String((root.task.testProofs || {}).physical || "not tested") + " · Mapped output: " + String((root.task.testProofs || {}).mapped || "not tested"); color: root.tokens.textSecondary; font.pixelSize: root.tokens.bodySmall; wrapMode: Text.WordWrap }
                        Flow {
                            Layout.fillWidth: true
                            spacing: root.tokens.space8
                            SetupButton { objectName: "flightDeckSetupTestPhysical"; text: "START PHYSICAL TEST"; onClicked: { const result = backendObject.startReadOnlyPhysicalInputTest(String(root.task.controllerRecordId || "")); root.showResult(result, "The physical test could not start."); if (result && result.success) backendObject.markSetupAssistantProof("physical", "started") } }
                            SetupButton { objectName: "flightDeckSetupPhysicalNotTested"; text: "PHYSICAL NOT TESTED"; subdued: true; onClicked: root.showResult(backendObject.markSetupAssistantProof("physical", "not-tested"), "Could not record the physical test result.") }
                            SetupButton { objectName: "flightDeckSetupMappedNotTested"; text: "MAPPED OUTPUT NOT TESTED"; subdued: true; onClicked: root.showResult(backendObject.markSetupAssistantProof("mapped", "not-tested"), "Could not record the mapped output result.") }
                        }
                        Text { Layout.fillWidth: true; text: "Use applies the existing Rig/Profile activation transaction. It does not prove live hardware input, game visibility, or a mapped-output test."; color: root.tokens.textSecondary; font.pixelSize: root.tokens.bodySmall; wrapMode: Text.WordWrap }
                        SetupButton { objectName: "flightDeckSetupUse"; text: "USE THIS SETUP"; enabled: String(root.task.rigId || "").length > 0 && String(root.task.profileId || "").length > 0; onClicked: { const result = backendObject.useSetupAssistantTask(); root.showResult(result, "This setup was not activated.") } }
                    }
                }
            }

            ColumnLayout {
                visible: !root.replacementChoiceVisible && root.stage === "complete"
                Layout.fillWidth: true
                spacing: root.tokens.space12
                Text { text: "SETUP RESULT"; color: root.tokens.textPrimary; font.family: root.tokens.displayFont; font.pixelSize: root.tokens.section; font.bold: true }
                Text { Layout.fillWidth: true; text: "Rig: " + String(root.task.rigId || "not created") + "\nProfile: " + String(root.task.profileId || "not selected") + "\nPhysical input: " + String((root.task.testProofs || {}).physical || "not tested") + "\nMapped output: " + String((root.task.testProofs || {}).mapped || "not tested"); color: root.tokens.textSecondary; font.pixelSize: root.tokens.body; wrapMode: Text.WordWrap }
                Text { Layout.fillWidth: true; text: "Use was requested through the existing activation transaction. Finish clears only this saved guidance and result; it does not undo Rigs, Profiles, mapping, or activation."; color: root.tokens.textMuted; font.pixelSize: root.tokens.bodySmall; wrapMode: Text.WordWrap }
                SetupButton { objectName: "flightDeckSetupFinish"; text: "FINISH"; onClicked: { const result = backendObject.finishSetupAssistantTask(); root.showResult(result, "Setup could not be finished."); if (result && result.success) root.close() } }
            }
        }
    }

    FlightDeckDialog {
        id: repairReview
        objectName: "flightDeckSetupRepairConfirmation"
        tokens: root.tokens
        heading: "Confirm setup repair"
        tone: "attention"
        preferredWidth: 620
        contentItem: Text {
            width: parent.width
            text: "Repair applies only to the saved Device Rig for this setup task. It uses the existing typed repair plan and does not activate this Rig or Profile. Review the plan, then explicitly confirm."
            color: root.tokens.textSecondary
            font.family: root.tokens.bodyFont
            font.pixelSize: root.tokens.body
            wrapMode: Text.WordWrap
        }
        footer: FlightDeckDialogFooter {
            tokens: root.tokens
            RowLayout {
                anchors.fill: parent
                SetupButton { text: "CANCEL"; subdued: true; onClicked: repairReview.close() }
                Item { Layout.fillWidth: true }
                SetupButton { objectName: "flightDeckSetupApproveRepair"; text: "CONFIRM REPAIR"; onClicked: root.approveTaskRepair() }
            }
        }
    }

    footer: FlightDeckDialogFooter {
        objectName: "flightDeckSetupAssistantFooter"
        tokens: root.tokens
        RowLayout {
            id: footerActions
            anchors.fill: parent
            anchors.margins: root.tokens.space6
            SetupButton { text: "SAVE FOR LATER"; subdued: true; visible: !root.replacementChoiceVisible && root.backendObject.hasSetupAssistantTask; onClicked: { const result = backendObject.saveSetupAssistantForLater(); root.showResult(result, "Setup was not saved."); if (result && result.success) root.close() } }
            SetupButton { text: "DISMISS GUIDANCE"; subdued: true; visible: !root.replacementChoiceVisible && root.backendObject.hasSetupAssistantTask; onClicked: { root.showResult(backendObject.dismissSetupAssistantTask(), "Guidance was not dismissed."); root.close() } }
            Item { Layout.fillWidth: true }
            SetupButton { text: "CLOSE"; subdued: true; onClicked: root.close() }
        }
    }
}
