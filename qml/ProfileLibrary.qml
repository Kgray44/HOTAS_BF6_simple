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
    signal navigateToPage(int page)

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
    function returnToLibrary() { view = "library"; selectedCategoryId = ""; selectedProfileId = "" }
    function openCategory(id) { selectedCategoryId = id; selectedProfileId = ""; view = "category" }
    function openProfile(id) { selectedProfileId = id; view = "profile" }
    function openTransfer(mode, kind, profileId, categoryId) {
        transferMode = mode; transferKind = kind; transferFile = ""
        transferProfileId = profileId || ""; transferCategoryId = categoryId || ""
        transferDialog.open()
    }
    function categoryNameFor(id) { const item = categoryById(id); return item ? item.name : "General" }

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
    component Card: Rectangle {
        default property alias content: contents.data
        property color cardAccent: root.border
        implicitHeight: contents.implicitHeight + 28
        radius: theme.topGun ? 1 : 7; color: root.panel; border.color: cardAccent
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
                Section { label: "CATEGORIES" }
                GridLayout { Layout.fillWidth: true; columns: root.width >= 1100 ? 3 : (root.width >= 730 ? 2 : 1); rowSpacing: 12; columnSpacing: 12
                    Repeater { model: root.categories
                        delegate: Card { required property var modelData; Layout.fillWidth: true; Layout.preferredHeight: 158; cardAccent: modelData.active ? root.accent : root.border
                            RowLayout { Layout.fillWidth: true
                                ColumnLayout { Layout.fillWidth: true; spacing: 3
                                    Text { text: modelData.name; color: root.text; font.pixelSize: 15; font.bold: true; font.family: theme.topGun ? theme.displayFont : undefined; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.profileCount + " PROFILE" + (modelData.profileCount === 1 ? "" : "S") + "  ·  " + (modelData.restoreLastProfile ? "RESTORE LAST" : "DEFAULT FIRST"); color: root.muted; font.pixelSize: 8; font.bold: true }
                                }
                                Pill { visible: modelData.active; label: "ACTIVE"; tone: root.good }
                            }
                            Text { Layout.fillWidth: true; text: modelData.executableRules.length > 0 ? "DETECTS: " + modelData.executableRules.join(", ") : "NO GAME RULES"; color: root.muted; font.pixelSize: 9; elide: Text.ElideRight }
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
                        ActionButton { label: "EXPORT CATEGORY"; subdued: true; onTriggered: root.openTransfer("export", "pack", "", root.selectedCategoryId) }
                        ActionButton { label: "+ PROFILE"; onTriggered: { createProfileDialog.categoryId = root.selectedCategoryId; createProfileDialog.open() } }
                    }
                }
                Section { label: "GAME DETECTION" }
                Card { Layout.fillWidth: true
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 3
                            Text { text: backendObject.automaticGameDetection ? "AUTOMATIC GAME DETECTION ENABLED" : "AUTOMATIC GAME DETECTION DISABLED"; color: root.text; font.pixelSize: 11; font.bold: true }
                            Text { text: root.selectedCategory && root.selectedCategory.executableRules.length > 0 ? root.selectedCategory.executableRules.join(", ") : "No executable rules configured."; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        ActionButton { label: "EDIT RULES"; subdued: true; onTriggered: { detectionDialog.categoryId = root.selectedCategoryId; detectionDialog.rules = root.selectedCategory ? root.selectedCategory.executableRules.join("\n") : ""; detectionDialog.open() } }
                    }
                }
                Card { Layout.fillWidth: true
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: "CATEGORY BEHAVIOR"; color: root.text; font.pixelSize: 11; font.bold: true }
                            Text { text: root.selectedCategory && root.selectedCategory.defaultProfileName.length > 0 ? "Default: " + root.selectedCategory.defaultProfileName : "Choose a default profile."; color: root.muted; font.pixelSize: 9 }
                        }
                        ActionButton { label: root.selectedCategory && root.selectedCategory.enabled ? "DISABLE" : "ENABLE"; subdued: true; actionEnabled: !root.selectedCategory || !root.selectedCategory.active; onTriggered: backendObject.setProfileCategoryEnabled(root.selectedCategoryId, !root.selectedCategory.enabled) }
                        CheckBox { text: "Restore last"; checked: root.selectedCategory ? root.selectedCategory.restoreLastProfile : true; onToggled: backendObject.setCategoryRestoreLastProfile(root.selectedCategoryId, checked) }
                    }
                    RowLayout { Layout.fillWidth: true
                        ComboBox { id: defaultCategoryProfile; Layout.fillWidth: true; model: root.profilesForCategory(root.selectedCategoryId); textRole: "name"; valueRole: "id"; currentIndex: { for (let i = 0; i < model.length; ++i) if (root.selectedCategory && model[i].id === root.selectedCategory.defaultProfileId) return i; return 0 } }
                        ActionButton { label: "SET DEFAULT"; subdued: true; actionEnabled: defaultCategoryProfile.currentValue !== undefined; onTriggered: backendObject.setCategoryDefaultProfile(root.selectedCategoryId, defaultCategoryProfile.currentValue) }
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
                        Repeater { model: root.detail.buttons || []; delegate: Text { required property var modelData; Layout.fillWidth: true; text: modelData.input + " → " + modelData.output; color: root.muted; font.pixelSize: 9 } }
                        Repeater { model: root.detail.povs || []; delegate: Text { required property var modelData; Layout.fillWidth: true; text: modelData.input + " → " + modelData.output; color: root.muted; font.pixelSize: 9 } }
                        ActionButton { label: "OPEN BUTTONS"; subdued: true; onTriggered: root.navigateToPage(1) }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "CURVES"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Repeater { model: root.detail.curves || []; delegate: Text { required property var modelData; Layout.fillWidth: true; text: modelData.axis + "  ·  " + modelData.summary; color: root.muted; font.pixelSize: 9; elide: Text.ElideRight } }
                        Text { visible: (root.detail.curves || []).length === 0; text: "Linear curves only"; color: root.muted; font.pixelSize: 9 }
                        ActionButton { label: "OPEN CURVES"; subdued: true; onTriggered: root.navigateToPage(6) }
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
                        Pill { label: root.detail.vjoyReady ? "READY" : "REVIEW OUTPUT"; tone: root.detail.vjoyReady ? root.good : root.warning }
                        Text { text: root.detail.controllerName ? "CURRENT CONTROLLER: " + root.detail.controllerName : "No current controller"; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    Card { Layout.fillWidth: true; cardAccent: root.border
                        Text { text: "RELATIONSHIPS"; color: root.text; font.pixelSize: 11; font.bold: true }
                        Text { text: "REFERENCED BY"; color: root.muted; font.pixelSize: 8; font.bold: true }
                        Repeater { model: (root.detail.relationships || {}).referencedBy || []; delegate: Text { required property var modelData; text: modelData.profile + " · " + modelData.via; color: root.muted; font.pixelSize: 9 } }
                        Text { text: "REFERENCES"; color: root.muted; font.pixelSize: 8; font.bold: true }
                        Repeater { model: (root.detail.relationships || {}).references || []; delegate: Text { required property var modelData; text: modelData.profile + " · " + modelData.via; color: root.muted; font.pixelSize: 9 } }
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
            ComboBox { id: newProfileCategory; Layout.fillWidth: true; model: root.categories; textRole: "name"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (model[i].id === createProfileDialog.categoryId) return i; return 0 } }
            Text { text: "START FROM"; color: root.muted; font.pixelSize: 9; font.bold: true }
            ComboBox { id: newProfileSource; Layout.fillWidth: true; model: root.profiles; textRole: "displayName"; valueRole: "id"; currentIndex: backendObject.activeProfileIndex }
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
            ComboBox { id: moveCategory; Layout.fillWidth: true; model: root.categories; textRole: "name"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (model[i].id === moveProfileDialog.categoryId) return i; return 0 } }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: moveProfileDialog.close() } ActionButton { label: "MOVE"; onTriggered: { if (backendObject.moveProfileToCategory(moveProfileDialog.profileId, moveCategory.currentValue)) moveProfileDialog.close() } } }
        }
    }
    Dialog { id: duplicateProfileDialog; property string profileId: ""; property string name: ""; property string categoryId: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Duplicate Profile"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 340; spacing: 10
            Field { id: duplicateName; Layout.fillWidth: true }
            ComboBox { id: duplicateCategory; Layout.fillWidth: true; model: root.categories; textRole: "name"; valueRole: "id"; currentIndex: { for (let i=0;i<model.length;++i) if (model[i].id === duplicateProfileDialog.categoryId) return i; return 0 } }
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
    Dialog { id: detectionDialog; property string categoryId: ""; property string rules: ""; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; title: "Game Detection Rules"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: 400; spacing: 10
            Text { text: "ONE EXECUTABLE PER LINE"; color: root.muted; font.pixelSize: 9; font.bold: true }
            TextArea { id: detectionRules; Layout.fillWidth: true; Layout.preferredHeight: 130; color: root.text; selectByMouse: true; background: Rectangle { color: root.inset; border.color: root.border; radius: 4 } }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } ActionButton { label: "CANCEL"; subdued: true; onTriggered: detectionDialog.close() } ActionButton { label: "SAVE RULES"; onTriggered: { if (backendObject.setCategoryGameDetectionRules(detectionDialog.categoryId, detectionRules.text.split("\n"))) detectionDialog.close() } } }
        }
        onOpened: { detectionRules.text = rules; detectionRules.forceActiveFocus() }
    }
    Dialog { id: transferDialog; parent: Overlay.overlay; modal: true; anchors.centerIn: parent; width: Math.min(760, root.width - 40); title: "Import / Export Center"; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout { width: parent.width - 36; spacing: 12
            RowLayout { Layout.fillWidth: true
                Repeater { model: ["IMPORT", "EXPORT"]; delegate: ActionButton { required property string modelData; label: modelData; subdued: (modelData.toLowerCase() !== root.transferMode); onTriggered: root.transferMode = modelData.toLowerCase() } }
                Item { Layout.fillWidth: true }
                Repeater { model: ["PROFILE", "PACK"]; delegate: ActionButton { required property string modelData; label: modelData; subdued: (modelData.toLowerCase() !== root.transferKind); onTriggered: root.transferKind = modelData.toLowerCase() } }
            }
            Card { Layout.fillWidth: true; cardAccent: root.accent
                Text { text: root.transferMode === "import" ? "Select a portable Profile or Pack. It is validated and previewed before any configuration changes." : "Portable exports include profile behavior and required curves. Packs can also include selected categories, Automation, and optional device/calibration data."; color: root.text; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout { visible: root.transferMode === "export" && root.transferKind === "pack"; Layout.fillWidth: true
                    ColumnLayout { Layout.fillWidth: true; Text { text: "PACK NAME"; color: root.muted; font.pixelSize: 8; font.bold: true } Field { id: packName; Layout.fillWidth: true; text: "HOTAS BF6 Pack" } }
                    ColumnLayout { Layout.fillWidth: true; Text { text: "DESCRIPTION"; color: root.muted; font.pixelSize: 8; font.bold: true } Field { id: packDescription; Layout.fillWidth: true; placeholderText: "Optional" } }
                }
                RowLayout { visible: root.transferMode === "export" && root.transferKind === "pack"; Layout.fillWidth: true
                    CheckBox { id: includeDevices; text: "Devices"; checked: false }
                    CheckBox { id: includeCalibration; text: "Calibration"; checked: false; enabled: includeDevices.checked }
                    Text { text: "Both default OFF; imported calibration is never applied automatically."; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                Text { visible: root.transferMode === "export"; text: root.transferKind === "profile" ? (root.transferProfileId.length > 0 ? "READY: " + root.detail.displayName : "Choose a Profile from its detail page for a direct export.") : (root.transferCategoryId.length > 0 ? "READY: " + root.categoryNameFor(root.transferCategoryId) + " and its profiles" : "Exports all selected categories in the Library."); color: root.muted; font.pixelSize: 9 }
            }
            Card { visible: root.transferMode === "import" && backendObject.portableImportPreview && backendObject.portableImportPreview.profileCount > 0; Layout.fillWidth: true; cardAccent: root.good
                Text { text: "IMPORT PREVIEW  ·  " + backendObject.portableImportPreview.kind + "  ·  " + backendObject.portableImportPreview.name; color: root.text; font.pixelSize: 12; font.bold: true }
                Text { text: backendObject.portableImportPreview.categoryCount + " categories  ·  " + backendObject.portableImportPreview.profileCount + " profiles  ·  " + backendObject.portableImportPreview.automationCount + " Automations  ·  " + backendObject.portableImportPreview.curveCount + " curves"; color: root.muted; font.pixelSize: 9 }
                Repeater { model: backendObject.portableImportPreview.profiles || []; delegate: Text { required property var modelData; text: modelData.category + " / " + modelData.name + "  ·  " + modelData.mappedAxes + " axes  ·  " + modelData.mappedButtons + " buttons"; color: root.muted; font.pixelSize: 9 } }
                RowLayout { Layout.fillWidth: true
                    CheckBox { id: replaceImportedProfiles; text: "Replace matching profiles"; checked: false }
                    Item { Layout.fillWidth: true }
                    ActionButton { label: "IMPORT"; onTriggered: { if (backendObject.applyPortableImport("", replaceImportedProfiles.checked, true)) transferDialog.close() } }
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
    FileDialog { id: importFileDialog; title: "Import HOTAS BF6 configuration"; fileMode: FileDialog.OpenFile; nameFilters: root.transferKind === "pack" ? ["HOTAS BF6 Pack (*.hbf6pack)"] : ["HOTAS BF6 Profile (*.hbf6profile)", "HOTAS BF6 Pack (*.hbf6pack)"]
        onAccepted: backendObject.loadPortableImportPreview(selectedFile) }
    FileDialog { id: exportFileDialog; title: "Export HOTAS BF6 configuration"; fileMode: FileDialog.SaveFile; nameFilters: root.transferKind === "pack" ? ["HOTAS BF6 Pack (*.hbf6pack)"] : ["HOTAS BF6 Profile (*.hbf6profile)"]
        onAccepted: { if (root.transferKind === "profile") backendObject.exportPortableProfile(root.transferProfileId, selectedFile); else backendObject.exportPortablePack(root.transferCategoryId.length > 0 ? [root.transferCategoryId] : root.categories.map(function(c) { return c.id }), [], packName.text, packDescription.text, includeDevices.checked, includeCalibration.checked, selectedFile) } }
}
