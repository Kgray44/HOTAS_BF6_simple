import QtQuick 6.5
import QtQuick.Controls 6.5
import QtQuick.Layouts 6.5
import QtQuick.Dialogs 6.5

// A deliberately control-plane-only presentation. Every model below is bound
// to AppBackend stateChanged; no telemetry property or mapper report drives a
// Profile Library rebuild.
Flickable {
    id: root
    property var backendObject
    property bool legacy: false
    property int requestedPage: 5
    property string view: "library" // library, category, profile
    property string selectedCategoryId: ""
    property string selectedProfileId: ""
    property string transferMode: "import" // import, export
    property string transferKind: "profile" // profile, pack
    property string transferFile: ""
    property var selectedPackCategoryIds: []
    property var selectedPackProfileIds: []
    property string categoryConflictMode: "merge"
    property string adaptivePresetConflictMode: "copy"
    property bool applyImportedCalibration: false
    property bool replaceCategoryConfirmed: false
    property bool replaceProfilesConfirmed: false
    property var runningApplications: []
    // The shell owns this compact snapshot while the heavy page Loader is
    // inactive. It intentionally excludes delegates, dialogs, and models.
    property var presentationState: ({})
    signal navigateToPage(int page)
    signal presentationStateCaptured(var state)

    anchors.fill: parent
    contentWidth: width
    contentHeight: body.implicitHeight + 26
    clip: true
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    Theme { id: theme }
    readonly property color panel: legacy ? "#182126" : theme.panel
    readonly property color inset: legacy ? "#10191d" : theme.panelInset
    readonly property color border: legacy ? "#49616b" : theme.border
    readonly property color text: legacy ? "#eef5f5" : theme.textStrong
    readonly property color muted: legacy ? "#9fb1b5" : theme.textMuted
    readonly property color accent: legacy ? "#8ec8d0" : theme.orangeBright
    readonly property color good: legacy ? "#9fcbbf" : theme.ready
    readonly property color warning: legacy ? "#d6bd78" : theme.warning
    readonly property color danger: legacy ? "#c98e97" : theme.danger
    readonly property bool narrow: width < 900
    readonly property var categories: backendObject ? backendObject.profileCategories : []
    readonly property var profiles: backendObject ? backendObject.profiles : []
    readonly property var detail: backendObject && selectedProfileId.length > 0
        ? backendObject.profileDetail(selectedProfileId) : ({})
    readonly property var selectedCategory: categoryById(selectedCategoryId)

    function categoryById(id) {
        for (let i = 0; i < categories.length; ++i) if (categories[i].id === id) return categories[i]
        return null
    }
    function profilesForCategory(id) {
        const result = []
        for (let i = 0; i < profiles.length; ++i) if (profiles[i].categoryId === id) result.push(profiles[i])
        return result
    }
    function copyValue(value) { return JSON.parse(JSON.stringify(value)) }
    function restorePresentationState() {
        const saved = presentationState || ({})
        if (!saved.view) return
        view = saved.view
        selectedCategoryId = saved.selectedCategoryId || ""
        selectedProfileId = saved.selectedProfileId || ""
        transferMode = saved.transferMode || "import"
        transferKind = saved.transferKind || "profile"
        transferFile = saved.transferFile || ""
        transferProfileId = saved.transferProfileId || ""
        transferCategoryId = saved.transferCategoryId || ""
        selectedPackCategoryIds = copyValue(saved.selectedPackCategoryIds || [])
        selectedPackProfileIds = copyValue(saved.selectedPackProfileIds || [])
        categoryConflictMode = saved.categoryConflictMode || "merge"
        adaptivePresetConflictMode = saved.adaptivePresetConflictMode || "copy"
        applyImportedCalibration = !!saved.applyImportedCalibration
        replaceCategoryConfirmed = !!saved.replaceCategoryConfirmed
        replaceProfilesConfirmed = !!saved.replaceProfilesConfirmed
        contentY = Number(saved.contentY || 0)
        if (saved.transferDialogOpen) Qt.callLater(function() { transferDialog.open() })
    }
    function capturePresentationState() {
        presentationStateCaptured({
            view: view,
            selectedCategoryId: selectedCategoryId,
            selectedProfileId: selectedProfileId,
            transferMode: transferMode,
            transferKind: transferKind,
            transferFile: transferFile,
            transferProfileId: transferProfileId,
            transferCategoryId: transferCategoryId,
            selectedPackCategoryIds: copyValue(selectedPackCategoryIds),
            selectedPackProfileIds: copyValue(selectedPackProfileIds),
            categoryConflictMode: categoryConflictMode,
            adaptivePresetConflictMode: adaptivePresetConflictMode,
            applyImportedCalibration: applyImportedCalibration,
            replaceCategoryConfirmed: replaceCategoryConfirmed,
            replaceProfilesConfirmed: replaceProfilesConfirmed,
            contentY: contentY,
            transferDialogOpen: transferDialog.visible
        })
    }
    Component.onCompleted: restorePresentationState()
    Component.onDestruction: capturePresentationState()
    function returnToLibrary() { view = "library"; selectedCategoryId = ""; selectedProfileId = "" }
    function openCategory(id) { selectedCategoryId = id; selectedProfileId = ""; view = "category"; refreshRunningApplications() }
    function openProfile(id) { selectedProfileId = id; view = "profile" }
    function openTransfer(mode, kind, profileId, categoryId) {
        transferMode = mode; transferKind = kind; transferFile = ""
        transferProfileId = profileId || ""; transferCategoryId = categoryId || ""
        selectedPackCategoryIds = categoryId ? [categoryId] : []
        selectedPackProfileIds = profileId ? [profileId] : []
        categoryConflictMode = "merge"; adaptivePresetConflictMode = "copy"; applyImportedCalibration = false
        replaceCategoryConfirmed = false; replaceProfilesConfirmed = false
        transferDialog.open()
    }
    function selectTransferKind(kind) {
        transferKind = kind
        if (kind === "category" && selectedPackCategoryIds.length === 0 && categories.length > 0) {
            selectedPackCategoryIds = [transferCategoryId || categories[0].id]
            selectedPackProfileIds = []
        }
    }
    function categoryNameFor(id) { const item = categoryById(id); return item ? item.name : "General" }
    function friendlyGameName(executable) {
        const base = String(executable || "").split(/[\\/]/).pop()
        const stem = base.replace(/\.exe$/i, "")
        if (stem.toLowerCase() === "bf6") return "Battlefield 6"
        if (stem.toLowerCase() === "starcitizen") return "Star Citizen"
        return stem.replace(/([a-z])([A-Z])/g, "$1 $2").replace(/[_-]+/g, " ") || base
    }
    function runningApplicationFor(executable) {
        const target = String(executable || "").split(/[\\/]/).pop().toLowerCase()
        for (let i = 0; i < runningApplications.length; ++i) {
            if (String(runningApplications[i].executable || "").toLowerCase() === target) return runningApplications[i]
        }
        return null
    }
    function refreshRunningApplications() {
        if (!backendObject) return
        runningApplications = backendObject.runningApplications()
        backendObject.refreshRunningApplications()
    }
    Connections {
        target: backendObject
        function onRunningApplicationsChanged() {
            root.runningApplications = backendObject.runningApplications()
        }
    }
    function saveGameRule(rawRule, previousRule) {
        if (!root.selectedCategory) return false
        const rules = root.selectedCategory.executableRules.slice()
        if (previousRule) {
            const previousIndex = rules.findIndex(function(rule) { return String(rule).toLowerCase() === String(previousRule).toLowerCase() })
            if (previousIndex >= 0) rules.splice(previousIndex, 1)
        }
        rules.push(rawRule)
        return backendObject.setCategoryGameDetectionRules(root.selectedCategoryId, rules)
    }
    function removeGameRule(rule) {
        if (!root.selectedCategory) return false
        return backendObject.setCategoryGameDetectionRules(root.selectedCategoryId,
            root.selectedCategory.executableRules.filter(function(candidate) {
                return String(candidate).toLowerCase() !== String(rule).toLowerCase()
            }))
    }
    function hasId(values, id) { return values.indexOf(id) >= 0 }
    function togglePackCategory(id, checked) {
        let values = selectedPackCategoryIds.slice(0)
        const index = values.indexOf(id)
        if (checked && index < 0) values.push(id)
        if (!checked && index >= 0) values.splice(index, 1)
        selectedPackCategoryIds = values
    }
    function togglePackProfile(id, categoryId, checked) {
        let values = selectedPackProfileIds.slice(0)
        if (hasId(selectedPackCategoryIds, categoryId)) {
            let categories = selectedPackCategoryIds.slice(0)
            categories.splice(categories.indexOf(categoryId), 1)
            selectedPackCategoryIds = categories
            const siblings = profilesForCategory(categoryId)
            for (let i = 0; i < siblings.length; ++i) {
                if (siblings[i].id !== id && values.indexOf(siblings[i].id) < 0) values.push(siblings[i].id)
            }
        }
        const index = values.indexOf(id)
        if (checked && index < 0) values.push(id)
        if (!checked && index >= 0) values.splice(index, 1)
        selectedPackProfileIds = values
    }
    function commitPortableImport() {
        if (backendObject.applyPortableImport(backendObject.portableImportPreview.categoryCount === 1 ? importDestinationCategory.currentValue : "", replaceImportedProfiles.checked, root.categoryConflictMode, root.applyImportedCalibration, root.adaptivePresetConflictMode)) transferDialog.close()
    }
    function requestPortableImport() {
        if (root.categoryConflictMode === "replace" && !root.replaceCategoryConfirmed) {
            replaceCategoryConfirmation.open()
            return
        }
        if (replaceImportedProfiles.checked && !root.replaceProfilesConfirmed) {
            replaceProfilesConfirmation.open()
            return
        }
        root.commitPortableImport()
    }

    property string transferProfileId: ""
    property string transferCategoryId: ""

    component ActionButton: Rectangle {
        property string label: "ACTION"
        property bool subdued: false
        property bool destructive: false
        property bool actionEnabled: true
        signal triggered()
        implicitWidth: Math.max(96, buttonLabel.implicitWidth + 26); implicitHeight: 32
        radius: theme.topGun ? 1 : 5
        color: !actionEnabled ? theme.controlDisabled : hit.containsMouse ? (subdued ? theme.buttonSecondaryHover : theme.buttonHover) : (destructive ? Qt.rgba(root.danger.r, root.danger.g, root.danger.b, 0.16) : subdued ? theme.buttonSecondary : theme.buttonSurface)
        border.color: !actionEnabled ? root.border : destructive ? root.danger : subdued ? root.border : root.accent
        opacity: actionEnabled ? 1 : 0.5
        Text { id: buttonLabel; anchors.centerIn: parent; text: parent.label; color: parent.destructive ? root.danger : root.text; font.pixelSize: 9; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
        MouseArea { id: hit; anchors.fill: parent; hoverEnabled: true; enabled: parent.actionEnabled; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: parent.triggered() }
    }
    component SelectionToggle: Item {
        id: selectionToggle
        property string label: ""
        property bool checked: false
        property bool actionEnabled: true
        signal toggled(bool checked)
        implicitWidth: row.implicitWidth
        implicitHeight: 24
        opacity: actionEnabled ? 1.0 : 0.42
        Row { id: row; spacing: 7; anchors.verticalCenter: parent.verticalCenter
            Rectangle { width: 15; height: 15; radius: 3; border.width: 1; border.color: root.border
                color: selectionToggle.checked ? root.accent : root.inset
                Text { anchors.centerIn: parent; visible: selectionToggle.checked; text: "✓"; color: root.panel; font.pixelSize: 11; font.bold: true }
            }
            Text { text: selectionToggle.label; color: root.text; font.pixelSize: 9; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { anchors.fill: parent; enabled: selectionToggle.actionEnabled; cursorShape: Qt.PointingHandCursor
            onClicked: { selectionToggle.checked = !selectionToggle.checked; selectionToggle.toggled(selectionToggle.checked) }
        }
    }
    Timer { interval: 2000; running: root.view === "category"; repeat: true; onTriggered: root.refreshRunningApplications() }
    component Card: Item {
        id: card
        default property alias content: contents.data
        property color cardAccent: root.border
        implicitHeight: contents.implicitHeight + 28
        LegacyAviationPanel { anchors.fill: parent; visible: root.legacy }
        Rectangle { anchors.fill: parent; visible: !root.legacy; radius: theme.topGun ? 1 : 7; color: root.panel; border.color: card.cardAccent }
        ColumnLayout { id: contents; anchors.fill: parent; anchors.margins: 14; spacing: 8 }
    }
    component Pill: Rectangle {
        property string label: "READY"
        property color tone: root.good
        implicitWidth: pillText.implicitWidth + 15; implicitHeight: 21; radius: theme.topGun ? 1 : 11
        color: Qt.rgba(tone.r, tone.g, tone.b, 0.13); border.color: tone
        Text { id: pillText; anchors.centerIn: parent; text: parent.label; color: parent.tone; font.pixelSize: 8; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
    }
    component Section: RowLayout {
        property string label: "SECTION"
        Layout.fillWidth: true; spacing: 8
        Rectangle { width: theme.topGun ? 13 : 7; height: theme.topGun ? 3 : 7; radius: theme.topGun ? 0 : 4; color: root.accent }
        Text { text: parent.label; color: root.muted; font.pixelSize: 10; font.bold: true; font.family: theme.topGun ? theme.telemetryFont : undefined }
        Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
    }
    component Field: TextField {
        implicitHeight: 33; color: root.text; selectByMouse: true
        background: Rectangle { radius: theme.topGun ? 1 : 5; color: root.inset; border.color: parent.activeFocus ? root.accent : root.border }
    }
    component ThemedComboBox: ComboBox {
        id: themedComboBox
        implicitHeight: 33
        contentItem: Text {
            leftPadding: 10; rightPadding: themedComboBox.indicator.width + 16
            text: themedComboBox.displayText; color: root.text; font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        indicator: Text {
            x: themedComboBox.width - width - 10; y: (themedComboBox.height - height) / 2
            text: "v"; color: root.muted; font.pixelSize: 10; font.bold: true
        }
        background: Rectangle {
            radius: theme.topGun ? 1 : 5; color: themedComboBox.pressed ? root.panel : root.inset
            border.color: themedComboBox.visualFocus ? root.accent : root.border
        }
        delegate: ItemDelegate {
            required property var modelData
            width: themedComboBox.width; height: 31
            highlighted: themedComboBox.highlightedIndex === index
            contentItem: Text {
                leftPadding: 10; rightPadding: 10
                text: themedComboBox.textRole.length > 0 ? modelData[themedComboBox.textRole] : modelData
                color: root.text; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            background: Rectangle { color: parent.highlighted ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.18) : root.panel }
        }
        popup: Popup {
            y: themedComboBox.height - 1; width: themedComboBox.width
            implicitHeight: Math.min(contentItem.implicitHeight, 248); padding: 1
            contentItem: ListView {
                clip: true; implicitHeight: contentHeight
                model: themedComboBox.popup.visible ? themedComboBox.delegateModel : null
                currentIndex: themedComboBox.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle { radius: theme.topGun ? 1 : 5; color: root.panel; border.color: root.border }
        }
    }

    ColumnLayout {
        id: body
        x: 1; width: root.width - 14; spacing: 14

        RowLayout { Layout.fillWidth: true; spacing: 10
            ActionButton { visible: root.view !== "library"; label: "← LIBRARY"; subdued: true; onTriggered: root.returnToLibrary() }
            ColumnLayout { Layout.fillWidth: true; spacing: 2
                Text { text: root.view === "library" ? (theme.topGun ? "PROFILE LIBRARY" : "Profile Library") : root.view === "category" ? root.categoryNameFor(root.selectedCategoryId) : root.detail.displayName || "Profile Detail"; color: root.text; font.pixelSize: theme.topGun ? 25 : 25; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
                Text { text: root.view === "library" ? "Game-aware configurations, safe switching, and portable Profile and Pack files." : root.view === "category" ? "Category behavior, profile defaults, and automatic game detection." : "Configuration summary and navigation hub. Changes apply only at configuration boundaries."; color: root.muted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            ActionButton { visible: root.view === "library"; label: "IMPORT / EXPORT"; onTriggered: root.openTransfer("import", "profile", "", "") }
            ActionButton { visible: root.view === "library"; label: "+ CATEGORY"; subdued: true; onTriggered: newCategoryDialog.open() }
            ActionButton { visible: root.view === "library"; label: "+ PROFILE"; onTriggered: { createProfileDialog.categoryId = backendObject.activeCategoryId; createProfileDialog.open() } }
        }

        // Library
        Item { Layout.fillWidth: true; Layout.preferredHeight: root.view === "library" ? libraryColumn.implicitHeight : 0; visible: root.view === "library"
            ColumnLayout { id: libraryColumn; width: parent.width; spacing: 13
                Card { Layout.fillWidth: true; cardAccent: root.accent
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: backendObject.activeProfileDisplayName; color: root.text; font.pixelSize: 16; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
                            Text { text: "ACTIVE CATEGORY / PROFILE"; color: root.muted; font.pixelSize: 8; font.bold: true }
                        }
                        Pill { label: backendObject.automaticGameDetection ? "GAME DETECTION ON" : "MANUAL"; tone: backendObject.automaticGameDetection ? root.good : root.warning }
                    }
                }
                RowLayout { Layout.fillWidth: true; spacing: 7
                    Text { text: "GAME DETECTION"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    Text { text: "→"; color: root.accent; font.pixelSize: 12; font.bold: true }
                    Text { text: "CATEGORY"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    Text { text: "→"; color: root.accent; font.pixelSize: 12; font.bold: true }
                    Text { text: "PROFILE"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    Text { text: "→"; color: root.accent; font.pixelSize: 12; font.bold: true }
                    Text { text: "VIRTUAL OUTPUT"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    Item { Layout.fillWidth: true }
                }
                Section { label: "CATEGORIES" }
                GridLayout { Layout.fillWidth: true; columns: root.width >= 1100 ? 3 : (root.width >= 730 ? 2 : 1); rowSpacing: 12; columnSpacing: 12
                    Repeater { model: root.categories
                        delegate: Card { required property var modelData; Layout.fillWidth: true; Layout.preferredHeight: 168; cardAccent: modelData.active ? root.accent : root.border
                            RowLayout { Layout.fillWidth: true
                                ColumnLayout { Layout.fillWidth: true; spacing: 3
                                    Text { text: modelData.name; color: root.text; font.pixelSize: 15; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.profileCount + " PROFILE" + (modelData.profileCount === 1 ? "" : "S") + (modelData.active ? "  ·  ACTIVE: " + backendObject.activeProfileName : ""); color: root.muted; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Pill { visible: modelData.active; label: "ACTIVE"; tone: root.good }
                            }
                            Text { Layout.fillWidth: true; text: "GAME DETECTION: " + (modelData.executableRules.length > 0 ? modelData.executableRules.map(root.friendlyGameName).join(", ") : "Manual only"); color: root.muted; font.pixelSize: 9; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: "ACTIVATION: " + (modelData.restoreLastProfile ? "Restore last-used profile" : "Always use " + (modelData.defaultProfileName || "selected profile")); color: root.muted; font.pixelSize: 9; elide: Text.ElideRight }
                            Item { Layout.fillHeight: true }
                            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "OPEN"; subdued: true; onTriggered: root.openCategory(modelData.id) } }
                        }
                    }
                }
                Section { label: "ALL PROFILES" }
                GridLayout { Layout.fillWidth: true; columns: root.width >= 1140 ? 3 : (root.width >= 760 ? 2 : 1); rowSpacing: 12; columnSpacing: 12
                    Repeater { model: root.profiles
                        delegate: Card { required property var modelData; Layout.fillWidth: true; Layout.preferredHeight: 180; cardAccent: modelData.active ? root.accent : root.border
                            MouseArea { anchors.fill: parent; z: -1; cursorShape: Qt.PointingHandCursor; onClicked: root.openProfile(modelData.id) }
                            RowLayout { Layout.fillWidth: true
                                ColumnLayout { Layout.fillWidth: true; spacing: 2
                                    Text { text: modelData.name; color: root.text; font.pixelSize: 14; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.categoryName.toUpperCase(); color: root.muted; font.pixelSize: 8; font.bold: true }
                                }
                                Pill { visible: modelData.active; label: "ACTIVE"; tone: root.good }
                            }
                            Text { text: modelData.mappedAxes + " AXES  ·  " + modelData.mappedButtons + " BUTTONS  ·  " + modelData.mappedPovs + " POV"; color: root.muted; font.pixelSize: 9; font.family: theme.telemetryFont }
                            Text { text: modelData.customCurves + " CUSTOM CURVES  ·  " + modelData.automationCount + " AUTOMATIONS  ·  VJOY " + modelData.outputDeviceId; color: root.muted; font.pixelSize: 8; font.family: theme.telemetryFont }
                            Item { Layout.fillHeight: true }
                            RowLayout { Layout.fillWidth: true
                                ActionButton { label: "DETAIL"; subdued: true; onTriggered: root.openProfile(modelData.id) }
                                Item { Layout.fillWidth: true }
                                ActionButton { label: modelData.active ? "ACTIVE" : "ACTIVATE"; actionEnabled: !modelData.active && modelData.enabled; onTriggered: backendObject.activateProfile(modelData.id) }
                            }
                        }
                    }
                }
            }
        }

        // Category detail
        Item { Layout.fillWidth: true; Layout.preferredHeight: root.view === "category" ? categoryColumn.implicitHeight : 0; visible: root.view === "category"
            ColumnLayout { id: categoryColumn; width: parent.width; spacing: 13
                Card { Layout.fillWidth: true; cardAccent: root.accent
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 3
                            Text { text: root.selectedCategory ? root.selectedCategory.profileCount + " Profiles" : ""; color: root.text; font.pixelSize: 14; font.bold: true }
                            Text { text: root.selectedCategory && root.selectedCategory.restoreLastProfile ? "When detected, restores the last active profile." : "When detected, selects the category default profile."; color: root.muted; font.pixelSize: 9 }
                        }
                        ActionButton { label: "RENAME"; subdued: true; onTriggered: { renameCategoryDialog.categoryId = root.selectedCategoryId; renameCategoryDialog.categoryName = root.selectedCategory.name; renameCategoryDialog.open() } }
                        ActionButton { label: "EXPORT CATEGORY"; subdued: true; onTriggered: root.openTransfer("export", "category", "", root.selectedCategoryId) }
                        ActionButton { label: "+ PROFILE"; onTriggered: { createProfileDialog.categoryId = root.selectedCategoryId; createProfileDialog.open() } }
                    }
                }
                Section { label: "GAME DETECTION" }
                Card { Layout.fillWidth: true
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 3
                            Text { text: "Automatically activate this category when one of these games is running."; color: root.text; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { text: backendObject.automaticGameDetection ? "Detection is enabled globally." : "Detection is paused globally; this category can still be activated manually."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        ActionButton { label: backendObject.automaticGameDetection ? "DETECTION ON" : "DETECTION OFF"; subdued: true; onTriggered: backendObject.setAutomaticGameDetection(!backendObject.automaticGameDetection) }
                    }
                    Repeater { model: root.selectedCategory ? root.selectedCategory.executableRules : []
                        delegate: Rectangle { id: gameRuleRow; required property string modelData; Layout.fillWidth: true; implicitHeight: 58; radius: theme.topGun ? 1 : 5; color: root.inset; border.color: root.border
                            readonly property var runningApplication: root.runningApplicationFor(modelData)
                            RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 8
                                ColumnLayout { Layout.fillWidth: true; spacing: 1
                                    Text { text: root.friendlyGameName(modelData); color: root.text; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData + "  ·  " + (gameRuleRow.runningApplication ? "RUNNING" : "NOT RUNNING"); color: gameRuleRow.runningApplication ? root.good : root.muted; font.pixelSize: 8; font.bold: true }
                                }
                                ActionButton { label: "EDIT"; subdued: true; onTriggered: { addGameDialog.editingRule = modelData; addGameDialog.mode = "manual"; addGameDialog.open() } }
                                ActionButton { label: "REMOVE"; subdued: true; onTriggered: root.removeGameRule(modelData) }
                            }
                        }
                    }
                    Text { visible: !root.selectedCategory || root.selectedCategory.executableRules.length === 0; text: "No games are linked to this category. This category can still be activated manually."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "+ ADD GAME"; onTriggered: { addGameDialog.editingRule = ""; addGameDialog.mode = "running"; addGameDialog.open() } } }
                }
                Section { label: "AUTOMATIC ACTIVATION" }
                Card { Layout.fillWidth: true
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: root.selectedCategory && root.selectedCategory.enabled ? "Automatic activation is enabled" : "Automatic activation is disabled"; color: root.text; font.pixelSize: 10; font.bold: true }
                            Text { text: "Game detection ignores disabled categories without changing their profiles or game rules."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        ActionButton { label: root.selectedCategory && root.selectedCategory.enabled ? "DISABLE" : "ENABLE"; subdued: true; actionEnabled: !root.selectedCategory || !root.selectedCategory.active; onTriggered: backendObject.setProfileCategoryEnabled(root.selectedCategoryId, !root.selectedCategory.enabled) }
                    }
                }
                Section { label: "WHEN THIS CATEGORY ACTIVATES" }
                Card { Layout.fillWidth: true
                    SelectionToggle { label: "Restore the last-used profile"; checked: root.selectedCategory ? root.selectedCategory.restoreLastProfile : true; onToggled: backendObject.setCategoryRestoreLastProfile(root.selectedCategoryId, checked) }
                    Text { text: "Return to whichever profile was most recently active inside this category."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    SelectionToggle { label: "Always use a specific profile"; checked: root.selectedCategory ? !root.selectedCategory.restoreLastProfile : false; onToggled: backendObject.setCategoryRestoreLastProfile(root.selectedCategoryId, !checked) }
                    Text { visible: root.selectedCategory && !root.selectedCategory.restoreLastProfile; text: "Choose the profile that should become active every time this category activates."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    RowLayout { visible: root.selectedCategory && !root.selectedCategory.restoreLastProfile; Layout.fillWidth: true
                        Text { text: "PROFILE"; color: root.muted; font.pixelSize: 8; font.bold: true }
                        ThemedComboBox { id: defaultCategoryProfile; Layout.fillWidth: true; model: root.profilesForCategory(root.selectedCategoryId); textRole: "name"; valueRole: "id"; currentIndex: { for (let i = 0; i < model.length; ++i) if (root.selectedCategory && model[i].id === root.selectedCategory.defaultProfileId) return i; return 0 } onActivated: backendObject.setCategoryDefaultProfile(root.selectedCategoryId, currentValue) }
                    }
                }
                Section { label: "CATEGORY PROFILES" }
                Repeater { model: root.profilesForCategory(root.selectedCategoryId)
                    delegate: Card { required property var modelData; Layout.fillWidth: true
                        RowLayout { Layout.fillWidth: true
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                Text { text: modelData.name; color: root.text; font.pixelSize: 13; font.bold: true }
                                Text { text: modelData.mappedAxes + " AXES  ·  " + modelData.mappedButtons + " BUTTONS  ·  " + modelData.customCurves + " CURVES"; color: root.muted; font.pixelSize: 8; font.family: theme.telemetryFont }
                            }
                            Pill { visible: modelData.active; label: "ACTIVE"; tone: root.good }
                            ActionButton { label: "OPEN"; subdued: true; onTriggered: root.openProfile(modelData.id) }
                        }
                    }
                }
                RowLayout { Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ActionButton { label: "DELETE CATEGORY"; destructive: true; actionEnabled: root.selectedCategory && root.selectedCategory.profileCount === 0 && !root.selectedCategory.active; onTriggered: { deleteCategoryDialog.categoryId = root.selectedCategoryId; deleteCategoryDialog.categoryName = root.selectedCategory.name; deleteCategoryDialog.open() } }
                }
            }
        }

        // Profile detail
        Item { Layout.fillWidth: true; Layout.preferredHeight: root.view === "profile" ? profileColumn.implicitHeight : 0; visible: root.view === "profile"
            ColumnLayout { id: profileColumn; width: parent.width; spacing: 13
                Card { Layout.fillWidth: true; cardAccent: root.detail.active ? root.good : root.accent
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 3
                            Text { text: root.detail.name || ""; color: root.text; font.pixelSize: 19; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
                            Text { text: (root.detail.category || "").toUpperCase() + "  ·  " + (root.detail.active ? "ACTIVE" : "INACTIVE") + "  ·  " + (root.detail.enabled ? "ENABLED" : "DISABLED"); color: root.muted; font.pixelSize: 9; font.bold: true }
                        }
                        ActionButton { label: "EXPORT PROFILE"; subdued: true; onTriggered: root.openTransfer("export", "profile", root.selectedProfileId, "") }
                        ActionButton { label: root.detail.active ? "ACTIVE" : "SET ACTIVE"; actionEnabled: !root.detail.active && root.detail.enabled; onTriggered: backendObject.activateProfile(root.selectedProfileId) }
                    }
                    RowLayout { Layout.fillWidth: true; spacing: 6
                        Pill { label: root.detail.mappedAxes + " AXES"; tone: root.accent }
                        Pill { label: root.detail.mappedButtons + " BUTTONS"; tone: root.accent }
                        Pill { label: root.detail.mappedPovs + " POV"; tone: root.accent }
                        Pill { label: root.detail.customCurves + " CURVES"; tone: root.accent }
                        Pill { label: "VJOY " + root.detail.vjoyDevice; tone: root.accent }
                        Pill { label: root.detail.compatibility || "Compatibility pending"; tone: (root.detail.compatibility || "").indexOf("Partial") >= 0 ? root.warning : root.good }
                    }
                }
                GridLayout { Layout.fillWidth: true; columns: root.narrow ? 1 : 2; rowSpacing: 13; columnSpacing: 13
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "AXIS DETAILS"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Repeater { model: root.detail.axes || []; delegate: RowLayout { required property var modelData; Layout.fillWidth: true
                            Text { text: modelData.physical; color: root.text; font.pixelSize: 10; Layout.preferredWidth: 92 }
                            Text { text: "→ " + modelData.virtual + "  ·  " + modelData.curve + "  ·  DZ " + Number(modelData.deadzone).toFixed(1) + "%"; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                        } }
                        ActionButton { label: "OPEN AXES"; subdued: true; onTriggered: root.navigateToPage(0) }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "BUTTONS & POV"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Text { text: "Mapped Buttons: " + root.detail.mappedButtons; color: root.text; font.pixelSize: 10; font.bold: true }
                        Text { text: "Profile-Control Buttons: " + root.detail.profileControlButtons; color: root.muted; font.pixelSize: 9 }
                        Text { text: "Mapped POV Hats: " + root.detail.mappedPovHats; color: root.muted; font.pixelSize: 9 }
                        Text { text: "Direct POV Outputs: " + root.detail.directPovOutputs; color: root.muted; font.pixelSize: 9 }
                        ActionButton { label: "OPEN BUTTONS"; subdued: true; onTriggered: root.navigateToPage(1) }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "CURVES"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Repeater { model: root.detail.curves || []; delegate: Text { required property var modelData; Layout.fillWidth: true; text: modelData.axis + "  ·  " + modelData.summary; color: root.muted; font.pixelSize: 9; elide: Text.ElideRight } }
                        Text { visible: (root.detail.curves || []).length === 0; text: "Linear curves only"; color: root.muted; font.pixelSize: 9 }
                        ActionButton { label: "OPEN CURVES"; subdued: true; onTriggered: root.navigateToPage(6) }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "ADVANCED CONTROLS"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Text { text: "CURVE TRANSITION SMOOTHING"; color: root.text; font.pixelSize: 10; font.bold: true }
                        Text { text: "Bumpless transfer prevents mapper-created virtual-axis jumps. It does not filter physical stick movement."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        SelectionToggle { label: "Override for This Profile"; checked: !!root.detail.curveTransitionSmoothingOverride
                            onToggled: backendObject.setProfileCurveTransitionSmoothingOverride(root.selectedProfileId, checked) }
                        Text { visible: !root.detail.curveTransitionSmoothingOverride; text: "Use Global Setting  ·  " + (root.detail.globalCurveTransitionSmoothingEnabled ? "Enabled" : "Disabled") + "  ·  " + root.detail.globalCurveTransitionDurationMs + " ms"; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        RowLayout { visible: !!root.detail.curveTransitionSmoothingOverride; Layout.fillWidth: true; spacing: 8
                            SelectionToggle { label: "Enabled"; checked: !!root.detail.curveTransitionSmoothingEnabled
                                onToggled: backendObject.setProfileCurveTransitionSmoothingEnabled(root.selectedProfileId, checked) }
                            Item { Layout.fillWidth: true }
                            Text { text: "TIME"; color: root.muted; font.pixelSize: 8; font.bold: true }
                            Rectangle { implicitWidth: 54; implicitHeight: 26; radius: theme.topGun ? 1 : 4; color: root.inset; border.color: root.border; opacity: root.detail.curveTransitionSmoothingEnabled ? 1.0 : 0.5
                                TextInput { anchors.fill: parent; anchors.margins: 6; text: Number(root.detail.curveTransitionDurationMs).toFixed(0); enabled: root.detail.curveTransitionSmoothingEnabled; color: root.text; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter; font.family: theme.telemetryFont; validator: IntValidator { bottom: 0; top: 1000 }
                                    onEditingFinished: { backendObject.setProfileCurveTransitionDurationMs(root.selectedProfileId, Number(text)); text = Number(root.detail.curveTransitionDurationMs).toFixed(0) } }
                            }
                            Text { text: "ms"; color: root.muted; font.pixelSize: 9; font.bold: true }
                        }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "AUTOMATION"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Repeater { model: root.detail.automations || []; delegate: RowLayout { required property var modelData; Layout.fillWidth: true
                            Text { text: modelData.name; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true }
                            Pill { label: modelData.enabled ? "ENABLED" : "DISABLED"; tone: modelData.enabled ? root.good : root.warning }
                        } }
                        Text { visible: (root.detail.automations || []).length === 0; text: "No profile-linked Automation"; color: root.muted; font.pixelSize: 9 }
                        ActionButton { label: "OPEN AUTOMATION"; subdued: true; onTriggered: root.navigateToPage(7) }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "VIRTUAL OUTPUT"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Text { text: root.detail.outputName + "  ·  vJoy Device " + root.detail.vjoyDevice; color: root.text; font.pixelSize: 10; font.bold: true }
                        Text { text: "Active output axes: " + root.detail.outputAxes + "  ·  Unmapped output axes: " + root.detail.unmappedOutputAxes; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Pill { label: root.detail.vjoyReady ? "READY" : "REVIEW OUTPUT"; tone: root.detail.vjoyReady ? root.good : root.warning }
                        Text { text: root.detail.controllerName ? "CURRENT CONTROLLER: " + root.detail.controllerName : "No current controller"; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "GAME / CATEGORY"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Text { text: "Category: " + (root.detail.category || "General"); color: root.text; font.pixelSize: 10; font.bold: true }
                        Text { text: "Games: " + ((root.detail.categoryGames || []).length > 0 ? root.detail.categoryGames.map(root.friendlyGameName).join(", ") : "Manual only"); color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: "When category activates: " + (root.detail.categoryActivationBehavior || "Restore the last-used profile"); color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        ActionButton { label: "OPEN CATEGORY"; subdued: true; onTriggered: root.openCategory(root.detail.categoryId) }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "RELATIONSHIPS"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Text { visible: ((root.detail.relationships || {}).referencedBy || []).length > 0; text: "REFERENCED BY"; color: root.muted; font.pixelSize: 8; font.bold: true }
                        Repeater { model: (root.detail.relationships || {}).referencedBy || []; delegate: Text { required property var modelData; text: modelData.profile + " · " + modelData.via; color: root.muted; font.pixelSize: 9 } }
                        Text { visible: ((root.detail.relationships || {}).references || []).length > 0; text: "REFERENCES"; color: root.muted; font.pixelSize: 8; font.bold: true }
                        Repeater { model: (root.detail.relationships || {}).references || []; delegate: Text { required property var modelData; text: modelData.profile + " · " + modelData.via; color: root.muted; font.pixelSize: 9 } }
                        Text { visible: ((root.detail.relationships || {}).referencedBy || []).length === 0 && ((root.detail.relationships || {}).references || []).length === 0; text: "No profile dependencies."; color: root.muted; font.pixelSize: 9 }
                    }
                }
                RowLayout { Layout.fillWidth: true
                    ActionButton { label: "RENAME"; subdued: true; actionEnabled: !root.detail.active; onTriggered: { renameProfileDialog.profileId = root.selectedProfileId; renameProfileDialog.profileName = root.detail.name; renameProfileDialog.open() } }
                    ActionButton { label: "DUPLICATE"; subdued: true; onTriggered: { duplicateProfileDialog.profileId = root.selectedProfileId; duplicateProfileDialog.name = root.detail.name + " Copy"; duplicateProfileDialog.categoryId = root.detail.categoryId; duplicateProfileDialog.open() } }
                    ActionButton { label: "MOVE CATEGORY"; subdued: true; onTriggered: { moveProfileDialog.profileId = root.selectedProfileId; moveProfileDialog.categoryId = root.detail.categoryId; moveProfileDialog.open() } }
                    ActionButton { label: root.detail.enabled ? "DISABLE" : "ENABLE"; subdued: true; actionEnabled: !root.detail.active; onTriggered: backendObject.setProfileEnabled(root.selectedProfileId, !root.detail.enabled) }
                    Item { Layout.fillWidth: true }
                    ActionButton { label: "DELETE"; destructive: true; actionEnabled: !root.detail.active; onTriggered: { deleteProfileDialog.profileId = root.selectedProfileId; deleteProfileDialog.profileName = root.detail.displayName; deleteProfileDialog.open() } }
                }
            }
        }
    }

    Dialog { id: renameCategoryDialog; property string categoryId: ""; property string categoryName: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Rename Category"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 340; spacing: 10
            Field { id: renameCategoryName; Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: renameCategoryDialog.close() } ActionButton { label: "RENAME"; actionEnabled: renameCategoryName.text.trim().length > 0; onTriggered: { if (backendObject.renameProfileCategory(renameCategoryDialog.categoryId, renameCategoryName.text)) renameCategoryDialog.close() } } }
        }
        onOpened: { renameCategoryName.text = categoryName; renameCategoryName.forceActiveFocus() }
    }
    Dialog { id: deleteCategoryDialog; property string categoryId: ""; property string categoryName: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Delete Empty Category?"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 380; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Delete the empty category \"" + deleteCategoryDialog.categoryName + "\"? Categories with profiles or the active category cannot be deleted."; color: root.text; font.pixelSize: 10 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: deleteCategoryDialog.close() } ActionButton { label: "DELETE"; destructive: true; onTriggered: { if (backendObject.deleteProfileCategory(deleteCategoryDialog.categoryId)) { deleteCategoryDialog.close(); root.returnToLibrary() } } } }
        }
    }
    Dialog { id: newCategoryDialog; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "New Category"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 340; spacing: 10
            Text { text: "CATEGORY NAME"; color: root.muted; font.pixelSize: 9; font.bold: true }
            Field { id: categoryName; Layout.fillWidth: true; placeholderText: "Battlefield 6" }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: newCategoryDialog.close() } ActionButton { label: "CREATE"; actionEnabled: categoryName.text.trim().length > 0; onTriggered: { if (backendObject.createProfileCategory(categoryName.text)) newCategoryDialog.close() } } }
        }
        onOpened: { categoryName.text = ""; categoryName.forceActiveFocus() }
    }
    Dialog { id: createProfileDialog; property string categoryId: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "New Profile"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 360; spacing: 10
            Text { text: "PROFILE NAME"; color: root.muted; font.pixelSize: 9; font.bold: true }
            Field { id: newProfileName; Layout.fillWidth: true; placeholderText: "Helicopter" }
            Text { text: "CATEGORY"; color: root.muted; font.pixelSize: 9; font.bold: true }
            ThemedComboBox { id: newProfileCategory; Layout.fillWidth: true; model: root.categories; textRole: "name"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (model[i].id === createProfileDialog.categoryId) return i; return 0 } }
            Text { text: "START FROM"; color: root.muted; font.pixelSize: 9; font.bold: true }
            ThemedComboBox { id: newProfileSource; Layout.fillWidth: true; model: root.profiles; textRole: "displayName"; valueRole: "id"; currentIndex: backendObject.activeProfileIndex }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: createProfileDialog.close() } ActionButton { label: "CREATE"; actionEnabled: newProfileName.text.trim().length > 0; onTriggered: { if (backendObject.createProfileInCategory(newProfileName.text, newProfileCategory.currentValue, newProfileSource.currentValue)) createProfileDialog.close() } } }
        }
        onOpened: { newProfileName.text = ""; newProfileName.forceActiveFocus() }
    }
    Dialog { id: renameProfileDialog; property string profileId: ""; property string profileName: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Rename Profile"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 340; spacing: 10
            Field { id: renameProfileName; Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: renameProfileDialog.close() } ActionButton { label: "RENAME"; onTriggered: { if (backendObject.renameProfile(renameProfileDialog.profileId, renameProfileName.text)) renameProfileDialog.close() } } }
        }
        onOpened: { renameProfileName.text = profileName; renameProfileName.selectAll(); renameProfileName.forceActiveFocus() }
    }
    Dialog { id: moveProfileDialog; property string profileId: ""; property string categoryId: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Move Profile"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 340; spacing: 10
            Text { text: "DESTINATION CATEGORY"; color: root.muted; font.pixelSize: 9; font.bold: true }
            ThemedComboBox { id: moveCategory; Layout.fillWidth: true; model: root.categories; textRole: "name"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (model[i].id === moveProfileDialog.categoryId) return i; return 0 } }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: moveProfileDialog.close() } ActionButton { label: "MOVE"; onTriggered: { if (backendObject.moveProfileToCategory(moveProfileDialog.profileId, moveCategory.currentValue)) moveProfileDialog.close() } } }
        }
    }
    Dialog { id: duplicateProfileDialog; property string profileId: ""; property string name: ""; property string categoryId: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Duplicate Profile"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 340; spacing: 10
            Field { id: duplicateName; Layout.fillWidth: true }
            ThemedComboBox { id: duplicateCategory; Layout.fillWidth: true; model: root.categories; textRole: "name"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (model[i].id === duplicateProfileDialog.categoryId) return i; return 0 } }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: duplicateProfileDialog.close() } ActionButton { label: "DUPLICATE"; onTriggered: { if (backendObject.duplicateProfileToCategory(duplicateProfileDialog.profileId, duplicateName.text, duplicateCategory.currentValue)) duplicateProfileDialog.close() } } }
        }
        onOpened: { duplicateName.text = name; duplicateName.forceActiveFocus() }
    }
    Dialog { id: deleteProfileDialog; property string profileId: ""; property string profileName: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Delete Profile?"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 380; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Delete \"" + deleteProfileDialog.profileName + "\"? References from profile controls and Automation are disabled rather than silently retargeted."; color: root.text; font.pixelSize: 10 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: deleteProfileDialog.close() } ActionButton { label: "DELETE"; destructive: true; onTriggered: { if (backendObject.deleteProfile(deleteProfileDialog.profileId)) { deleteProfileDialog.close(); root.returnToLibrary() } } } }
        }
    }
    Dialog { id: addGameDialog; property string mode: "running"; property string editingRule: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; width: Math.min(610, root.width - 36); title: ""; standardButtons: Dialog.NoButton; padding: 0
        header: Rectangle { implicitHeight: 66; color: root.panel; border.color: root.border; radius: theme.topGun ? 1 : 7
            ColumnLayout { anchors.fill: parent; anchors.margins: 15; spacing: 1
                Text { text: addGameDialog.editingRule.length > 0 ? "EDIT GAME" : "ADD GAME"; color: root.text; font.pixelSize: 15; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
                Text { text: "Choose a running application, browse for an EXE, or enter one manually."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
        }
        background: Rectangle { color: root.panel; border.color: root.border; radius: theme.topGun ? 1 : 7 }
        contentItem: ColumnLayout { width: addGameDialog.width - 30; spacing: 11
            RowLayout { Layout.fillWidth: true
                Repeater { model: [{label:"RUNNING APPLICATIONS", value:"running"}, {label:"BROWSE FOR GAME", value:"browse"}, {label:"MANUAL", value:"manual"}]
                    delegate: ActionButton { required property var modelData; label: modelData.label; subdued: addGameDialog.mode !== modelData.value; onTriggered: addGameDialog.mode = modelData.value }
                }
            }
            Text { visible: addGameDialog.mode === "running"; text: "Select an application currently running on this PC. System and background processes are filtered out."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            ScrollView { visible: addGameDialog.mode === "running"; Layout.fillWidth: true; Layout.preferredHeight: 230; clip: true
                contentWidth: availableWidth
                ColumnLayout { width: addGameDialog.width - 30; spacing: 6
                    Repeater { model: root.runningApplications
                        delegate: Rectangle { required property var modelData; Layout.fillWidth: true; implicitHeight: 52; radius: theme.topGun ? 1 : 5; color: root.inset; border.color: root.border
                            RowLayout { anchors.fill: parent; anchors.margins: 9; spacing: 8
                                ColumnLayout { Layout.fillWidth: true; spacing: 1
                                    Text { text: modelData.name; color: root.text; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.executable; color: root.muted; font.pixelSize: 8 }
                                }
                                ActionButton { label: "ADD"; onTriggered: { if (root.saveGameRule(modelData.executable, addGameDialog.editingRule)) addGameDialog.close() } }
                            }
                        }
                    }
                    Text { visible: root.runningApplications.length === 0; text: "No suitable running applications were found. You can still browse for an EXE or enter one manually."; color: root.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            ColumnLayout { visible: addGameDialog.mode === "browse"; Layout.fillWidth: true; spacing: 8
                Text { text: "Choose the game's executable. HOTAS BF6 saves only the filename, so detection continues to work after an install moves or is reinstalled."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout { Layout.fillWidth: true
                    Field { id: browseGamePath; Layout.fillWidth: true; readOnly: true; placeholderText: "No executable selected" }
                    ActionButton { label: "BROWSE"; onTriggered: gameExecutableDialog.open() }
                }
                Text { visible: browseGamePath.text.length > 0; text: "Game: " + root.friendlyGameName(browseGamePath.text) + "  ·  Executable: " + browseGamePath.text.split(/[\\/]/).pop(); color: root.text; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            ColumnLayout { visible: addGameDialog.mode === "manual"; Layout.fillWidth: true; spacing: 8
                Text { text: "Enter an executable filename for an advanced manual rule."; color: root.muted; font.pixelSize: 9 }
                Field { id: manualGameExecutable; Layout.fillWidth: true; placeholderText: "bf6.exe" }
                Text { text: manualGameExecutable.text.trim().length > 0 ? "Game: " + root.friendlyGameName(manualGameExecutable.text) + "  ·  Executable: " + manualGameExecutable.text.split(/[\\/]/).pop() : ""; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true }
                ActionButton { label: "CANCEL"; subdued: true; onTriggered: addGameDialog.close() }
                ActionButton { visible: addGameDialog.mode !== "running"; label: addGameDialog.editingRule.length > 0 ? "SAVE GAME" : "ADD GAME"; actionEnabled: addGameDialog.mode === "browse" ? browseGamePath.text.length > 0 : manualGameExecutable.text.trim().length > 0; onTriggered: { const rule = addGameDialog.mode === "browse" ? browseGamePath.text : manualGameExecutable.text; if (root.saveGameRule(rule, addGameDialog.editingRule)) addGameDialog.close() } }
            }
        }
        onOpened: { root.refreshRunningApplications(); browseGamePath.text = ""; manualGameExecutable.text = editingRule; if (mode === "manual") manualGameExecutable.forceActiveFocus() }
    }
    Dialog { id: transferDialog; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; width: Math.min(820, root.width - 40); title: ""; standardButtons: Dialog.NoButton; padding: 0
        header: Rectangle { implicitHeight: 68; color: root.panel; border.color: root.border; radius: theme.topGun ? 1 : 7
            ColumnLayout { anchors.fill: parent; anchors.margins: 15; spacing: 1
                Text { text: "IMPORT / EXPORT"; color: root.text; font.pixelSize: 16; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined }
                Text { text: root.transferMode === "import" ? "Select a file, review the validated preview, then confirm the import." : "Choose exactly what to export and a destination file."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
        }
        background: Rectangle { color: root.panel; border.color: root.border; radius: theme.topGun ? 1 : 7 }
        contentItem: ScrollView { implicitWidth: transferDialog.width - 30; implicitHeight: Math.min(620, Math.max(300, root.height - 170)); clip: true
            contentWidth: availableWidth
            ColumnLayout { width: transferDialog.width - 30; spacing: 12
            RowLayout { Layout.fillWidth: true
                Repeater { model: ["IMPORT", "EXPORT"]; delegate: ActionButton { required property string modelData; label: modelData; subdued: (modelData.toLowerCase() !== root.transferMode); onTriggered: root.transferMode = modelData.toLowerCase() } }
                Item { Layout.fillWidth: true }
                Repeater { model: ["PROFILE", "CATEGORY", "PACK"]; delegate: ActionButton { required property string modelData; label: modelData; subdued: (modelData.toLowerCase() !== root.transferKind); onTriggered: root.selectTransferKind(modelData.toLowerCase()) } }
            }
            Card { Layout.fillWidth: true; cardAccent: root.accent
                Text { text: root.transferMode === "import" ? "Nothing changes until the imported Profile, Category, or Pack has passed validation and you confirm it below." : root.transferKind === "profile" ? "One individual HOTAS configuration." : root.transferKind === "category" ? "One Category and all profiles it contains." : "A portable collection of selected configuration items."; color: root.text; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                ColumnLayout { visible: root.transferMode === "export" && root.transferKind === "profile"; Layout.fillWidth: true; spacing: 5
                    Text { text: "PROFILE TO EXPORT"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    ThemedComboBox { id: exportProfileChoice; Layout.fillWidth: true; model: root.profiles; textRole: "displayName"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (model[i].id === root.transferProfileId) return i; return backendObject.activeProfileIndex } onActivated: root.transferProfileId = currentValue }
                    Text { text: "The complete Profile behavior, required curves, profile controls, Automation relationships, vJoy contract, and safe source-controller compatibility summary are included."; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                ColumnLayout { visible: root.transferMode === "export" && root.transferKind === "category"; Layout.fillWidth: true; spacing: 5
                    Text { text: "CATEGORY TO EXPORT"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    ThemedComboBox { id: exportCategoryChoice; Layout.fillWidth: true; model: root.categories; textRole: "name"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (root.hasId(root.selectedPackCategoryIds, model[i].id)) return i; return 0 } onActivated: { root.selectedPackCategoryIds = [currentValue]; root.selectedPackProfileIds = [] } }
                    Text { text: "Includes all profiles in this category, their required curves, and the category's game-detection behavior."; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                RowLayout { visible: root.transferMode === "export" && root.transferKind === "pack"; Layout.fillWidth: true
                    ColumnLayout { Layout.fillWidth: true; Text { text: "PACK NAME"; color: root.muted; font.pixelSize: 8; font.bold: true } Field { id: packName; Layout.fillWidth: true; text: "HOTAS BF6 Pack" } }
                    ColumnLayout { Layout.fillWidth: true; Text { text: "DESCRIPTION"; color: root.muted; font.pixelSize: 8; font.bold: true } Field { id: packDescription; Layout.fillWidth: true; placeholderText: "Optional" } }
                }
                RowLayout { visible: root.transferMode === "export" && root.transferKind === "pack"; Layout.fillWidth: true
                    SelectionToggle { id: includeDevices; label: "DEVICES"; checked: false; onToggled: { if (!checked) includeCalibration.checked = false } }
                    SelectionToggle { id: includeCalibration; label: "CALIBRATION"; checked: false; actionEnabled: includeDevices.checked; onToggled: { if (!includeDevices.checked) checked = false } }
                    Text { text: "Both default OFF; imported calibration is never applied automatically."; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                ColumnLayout { visible: root.transferMode === "export" && root.transferKind === "pack"; Layout.fillWidth: true; spacing: 3
                    Text { text: "SELECT CATEGORIES OR INDIVIDUAL PROFILES"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    Repeater { model: root.categories; delegate: ColumnLayout { required property var modelData; Layout.fillWidth: true; spacing: 2
                        SelectionToggle { label: modelData.name.toUpperCase(); checked: root.hasId(root.selectedPackCategoryIds, modelData.id); onToggled: root.togglePackCategory(modelData.id, checked) }
                        Repeater { model: root.profilesForCategory(modelData.id); delegate: SelectionToggle { required property var modelData; label: "    " + modelData.name; checked: root.hasId(root.selectedPackCategoryIds, modelData.categoryId) || root.hasId(root.selectedPackProfileIds, modelData.id); onToggled: root.togglePackProfile(modelData.id, modelData.categoryId, checked) } }
                    } }
                }
                ColumnLayout { visible: root.transferMode === "export" && root.transferKind === "pack"; Layout.fillWidth: true; spacing: 3
                    Text { text: "RELATED CONFIGURATION"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    RowLayout { Layout.fillWidth: true
                        SelectionToggle { id: includeAutomations; label: "RELATED AUTOMATIONS"; checked: true }
                        SelectionToggle { id: includeRelationships; label: "PROFILE CONTROL RELATIONSHIPS"; checked: true }
                        SelectionToggle { id: includeGameDetection; label: "GAME DETECTION RULES"; checked: true }
                    }
                    Text { text: "Required custom curves and vJoy requirements are always included so a selected profile never exports broken behavior."; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                ColumnLayout { visible: root.transferMode === "export" && root.transferKind === "pack"; Layout.fillWidth: true; spacing: 3
                    Text { text: "ADAPTIVE RESPONSE"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    RowLayout { Layout.fillWidth: true
                        SelectionToggle { label: "ADAPTIVE RESPONSE"; checked: true; actionEnabled: false }
                        SelectionToggle { label: "GLOBAL DEFAULTS"; checked: true; actionEnabled: false }
                        SelectionToggle { label: "CATEGORY OVERRIDES"; checked: true; actionEnabled: false }
                        SelectionToggle { label: "PROFILE OVERRIDES"; checked: true; actionEnabled: false }
                    }
                    RowLayout { Layout.fillWidth: true
                        SelectionToggle { label: "REQUIRED PRESET DEPENDENCIES"; checked: true; actionEnabled: false }
                        Text { text: "Only presets referenced by the selected Global, Categories, Profiles, or included Automations are exported. Unrelated custom presets stay local."; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }
            }
            Card { visible: root.transferMode === "import" && backendObject.portableImportPreview && backendObject.portableImportPreview.profileCount > 0; Layout.fillWidth: true; cardAccent: root.good
                Text { text: "IMPORT PREVIEW  ·  " + backendObject.portableImportPreview.kind + "  ·  " + backendObject.portableImportPreview.name; color: root.text; font.pixelSize: 12; font.bold: true }
                Text { text: "Exported by " + backendObject.portableImportPreview.exporterVersion + " · " + backendObject.portableImportPreview.categoryCount + " categories · " + backendObject.portableImportPreview.profileCount + " profiles · " + backendObject.portableImportPreview.automationCount + " Automations · " + backendObject.portableImportPreview.curveCount + " curves"; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: "SOURCE CONTROLLER: " + ((backendObject.portableImportPreview.sourceController || {}).name || "Not recorded") + "  ·  CURRENT: " + backendObject.portableImportPreview.currentControllerName; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater { model: backendObject.portableImportPreview.categories || []; delegate: Text { required property var modelData; text: "CATEGORY  " + modelData.name + " · " + modelData.profileCount + " profiles · " + modelData.conflict + (modelData.rules.length ? " · detects " + modelData.rules.join(", ") : ""); color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap } }
                Repeater { model: backendObject.portableImportPreview.profiles || []; delegate: Text { required property var modelData; text: modelData.category + " / " + modelData.name + " · " + modelData.mappedAxes + " axes · " + modelData.mappedButtons + " buttons · " + modelData.povMappings + " POV · " + modelData.automationCount + " Automation · " + modelData.compatibility + (modelData.nameConflict ? " · NAME CONFLICT" : ""); color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap } }
                Repeater { model: backendObject.portableImportPreview.outputLayouts || []; delegate: Text { required property var modelData; text: "vJOY: " + modelData.name + " · Device " + modelData.vjoyDevice + " · " + modelData.axes + " axes · " + modelData.buttons + " buttons"; color: root.muted; font.pixelSize: 9 } }
                Text { text: "RELATED: " + backendObject.portableImportPreview.profileControlCount + " profile controls · " + (backendObject.portableImportPreview.curves || []).length + " curves · " + (backendObject.portableImportPreview.automations || []).length + " Automations"; color: root.muted; font.pixelSize: 9 }
                RowLayout { visible: backendObject.portableImportPreview.categoryCount === 1; Layout.fillWidth: true
                    Text { text: "PROFILE DESTINATION:"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    ThemedComboBox { id: importDestinationCategory; Layout.fillWidth: true; model: [{id:"", name:"SOURCE CATEGORY (create or merge)"}].concat(root.categories); textRole: "name"; valueRole: "id" }
                }
                RowLayout { visible: (backendObject.portableImportPreview.categories || []).some(function(c) { return c.exists }); Layout.fillWidth: true
                    Text { text: "EXISTING CATEGORY:"; color: root.muted; font.pixelSize: 8; font.bold: true }
                    Repeater { model: [{label:"MERGE", value:"merge"}, {label:"IMPORT AS NEW", value:"new"}, {label:"REPLACE", value:"replace"}]; delegate: ActionButton { required property var modelData; label: modelData.label; subdued: root.categoryConflictMode !== modelData.value; onTriggered: { root.categoryConflictMode = modelData.value; root.replaceCategoryConfirmed = false } } }
                }
                ColumnLayout { visible: Number(backendObject.portableImportPreview.adaptiveResponsePresetCount || 0) > 0; Layout.fillWidth: true; spacing: 4
                    Text { text: "ADAPTIVE RESPONSE PRESETS  ·  " + backendObject.portableImportPreview.adaptiveResponsePresetCount + " REQUIRED DEPENDENC" + (Number(backendObject.portableImportPreview.adaptiveResponsePresetCount) === 1 ? "Y" : "IES"); color: root.muted; font.pixelSize: 8; font.bold: true }
                    Text { text: "When an imported Response Preset id already exists locally with different values, choose the conflict behavior before importing."; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    RowLayout { Layout.fillWidth: true
                        Repeater { model: [{label:"KEEP LOCAL", value:"keep"}, {label:"IMPORT AS COPY", value:"copy"}, {label:"REPLACE", value:"replace"}]; delegate: ActionButton { required property var modelData; label: modelData.label; subdued: root.adaptivePresetConflictMode !== modelData.value; onTriggered: root.adaptivePresetConflictMode = modelData.value } }
                    }
                }
                Repeater { model: backendObject.portableImportPreview.devices || []; delegate: ColumnLayout { required property var modelData; Layout.fillWidth: true; spacing: 2
                    Text { text: "DEVICE: " + modelData.name + " · " + modelData.axisCount + " axes · " + modelData.buttonCount + " buttons · " + modelData.state; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    ThemedComboBox { visible: modelData.choices.length > 1; Layout.fillWidth: true; model: modelData.choices; textRole: "name"; valueRole: "id"; onActivated: backendObject.selectPortableImportDevice(modelData.index, currentValue) }
                } }
                SelectionToggle { visible: backendObject.portableImportPreview.includesCalibration; label: "APPLY IMPORTED CALIBRATION TO THE MATCHED CONTROLLER"; checked: root.applyImportedCalibration; onToggled: root.applyImportedCalibration = checked }
                Text { visible: backendObject.portableImportPreview.includesCalibration; text: "Default is Keep Local Calibration. Applying requires the controller match shown above; ambiguous matches require your selection."; color: root.warning; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Repeater { model: backendObject.portableImportPreview.warnings || []; delegate: Text { required property var modelData; text: "REVIEW: " + modelData; color: root.warning; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap } }
                RowLayout { Layout.fillWidth: true
                    SelectionToggle { id: replaceImportedProfiles; label: "REPLACE MATCHING PROFILES"; checked: false; onToggled: root.replaceProfilesConfirmed = false }
                    Item { Layout.fillWidth: true }
                    ActionButton { label: "IMPORT"; onTriggered: root.requestPortableImport() }
                }
            }
            Text { visible: backendObject.portableImportStatus.length > 0; text: backendObject.portableImportStatus; color: root.transferMode === "import" && (!backendObject.portableImportPreview || backendObject.portableImportPreview.profileCount === 0) ? root.warning : root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { label: "CANCEL"; subdued: true; onTriggered: transferDialog.close() }
                ActionButton { label: root.transferMode === "import" ? "SELECT FILE" : "CHOOSE DESTINATION"; onTriggered: { if (root.transferMode === "import") importFileDialog.open(); else exportFileDialog.open() } }
            }
            }
        }
    }
    Dialog { id: replaceCategoryConfirmation; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Replace Existing Category?"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 390; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "This replaces the matching non-active category and its profiles with the imported category. General, active, and last remaining categories are protected."; color: root.text; font.pixelSize: 10 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: replaceCategoryConfirmation.close() } ActionButton { label: "REPLACE CATEGORY"; destructive: true; onTriggered: { root.replaceCategoryConfirmed = true; replaceCategoryConfirmation.close(); root.requestPortableImport() } } }
        }
    }
    Dialog { id: replaceProfilesConfirmation; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Replace Matching Profiles?"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 390; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Matching profile names will be replaced with the imported configuration. Leave this unchecked to import safe renamed copies instead."; color: root.text; font.pixelSize: 10 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: replaceProfilesConfirmation.close() } ActionButton { label: "REPLACE PROFILES"; destructive: true; onTriggered: { root.replaceProfilesConfirmed = true; replaceProfilesConfirmation.close(); root.requestPortableImport() } } }
        }
    }
    FileDialog { id: gameExecutableDialog; title: "Choose a game executable"; fileMode: FileDialog.OpenFile; nameFilters: ["Windows applications (*.exe)"]
        onAccepted: browseGamePath.text = selectedFile.toString()
    }
    FileDialog { id: importFileDialog; title: "Import HOTAS BF6 configuration"; fileMode: FileDialog.OpenFile; nameFilters: root.transferKind === "pack" || root.transferKind === "category" ? ["HOTAS BF6 Pack (*.hbf6pack)"] : ["HOTAS BF6 Profile (*.hbf6profile)", "HOTAS BF6 Pack (*.hbf6pack)"]
        onAccepted: backendObject.loadPortableImportPreview(selectedFile) }
    FileDialog { id: exportFileDialog; title: "Export HOTAS BF6 configuration"; fileMode: FileDialog.SaveFile; nameFilters: root.transferKind === "pack" || root.transferKind === "category" ? ["HOTAS BF6 Pack (*.hbf6pack)"] : ["HOTAS BF6 Profile (*.hbf6profile)"]
        onAccepted: { if (root.transferKind === "profile") backendObject.exportPortableProfile(exportProfileChoice.currentValue, selectedFile); else backendObject.exportPortablePack(root.selectedPackCategoryIds, root.selectedPackProfileIds, packName.text, packDescription.text, includeDevices.checked, includeCalibration.checked, includeAutomations.checked, includeRelationships.checked, includeGameDetection.checked, selectedFile) } }
}
